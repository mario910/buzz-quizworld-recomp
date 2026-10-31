#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_fixed_stream.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
static void drain(spu_context*,uint32_t);
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*){abort();}
void spu_drain_call(spu_context* ctx,uint32_t stop){drain(ctx,stop);}
void spu_img_restore(spu_context* ctx,int32_t image){assert(ctx->image_id==image);}
}
#include "../analysis/native-geometry/phase3/test-fixed-stream-reference.inc"
static uint8_t fixture[264192];
static void drain(spu_context* ctx,uint32_t stop)
{
    unsigned steps=0;
    while(g_spu_trampoline_fn){assert(++steps<65536);auto fn=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;fn(ctx);}
    assert(ctx->pc==stop);
}
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& ctx,unsigned trial)
{
    memset(&ctx,0,sizeof ctx);ctx.ls=ctx.ls_store;ctx.image_id=41;ctx.pc=0x9058;ctx.host_depth=trial&1;
    uint32_t random=trial+1;
    for(auto& r:ctx.gpr)for(auto& word:r._u32)word=random_word(random);
    for(unsigned i=0;i<SPU_LS_SIZE;++i)ctx.ls[i]=uint8_t(random_word(random));
    memcpy(ctx.ls,fixture+2048,0xe680);
    ctx.gpr[1]._u32[0]=0x3d000;ctx.gpr[107]._u32[0]=1+(trial/16)%16;
    ctx.gpr[106]._u32[0]=1+(trial/256)%16;ctx.gpr[42]._u32[0]=trial%16;
    const unsigned counts[]={0,1,2,3,4,7,15,31};
    const unsigned groups=counts[(trial/4)%8],input_step=256,output_step=1024;
    for(unsigned lane=0;lane<4;++lane){
        ctx.gpr[41]._u32[lane]=0x24000+lane*256+trial%16;
        ctx.gpr[80]._u32[lane]=0x24000+groups*output_step+240;
        ctx.gpr[81]._u32[lane]=output_step;ctx.gpr[83]._u32[lane]=input_step;
        ctx.gpr[82]._u32[lane]=trial&2?0x3d020:0x3c000+(trial&1?lane*16:0);
    }
    const unsigned bases[]={118,117,116,124};
    for(unsigned row=0;row<4;++row){
        ctx.gpr[bases[row]]._u32[0]=0x10000+row*0x4000;
        u128 pointer=ctx.gpr[row+4];pointer._u32[0]=ctx.gpr[bases[row]]._u32[0]+16;
        spu_ls_write128(&ctx,0x3d070-row*16,pointer);
    }

    u128 pointer=spu_il(0x3b000);spu_ls_write128(&ctx,0x3d0d0,pointer);
    pointer=spu_il(0x3a000);spu_ls_write128(&ctx,0x3d110,pointer);
    spu_ls_write128(&ctx,0x3d0e0,spu_ila(0x10203));ctx.gpr[84]=spu_il(0);
    for(unsigned lane=0;lane<4;++lane){ctx.ls[0x3a000+lane*2]=8;ctx.ls[0x3a001+lane*2]=0;geometry_job_put32(ctx.ls+0x3b000+lane*4,random_word(random));}
    ctx.int_enable=trial&1;ctx.resv_valid=trial&1;ctx.mfc_tag_status=trial*0x123;
}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    memcpy(&reference,&native,sizeof native);reference.ls=reference.ls_store;
    GeometryFixedStream p;const char* reason=nullptr;
    if(!read_geometry_fixed_stream(native,p,&reason)){fprintf(stderr,"fixed preflight rejected trial%u: %s\n",trial,reason);abort();}
    buzz_job_6eba_spu_func_00009058(&reference);
    assert(reference.pc==0x83c8 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);g_spu_trampoline_fn=nullptr;
    decode_geometry_fixed_stream(native,p);native.pc=0x83c8;
    const auto diff=compare_function_state(native,reference);
    if(!diff.exact()){
        fprintf(stderr,"fixed trial%u cols%u differs r%d byte%d LS=%X/%u state=%s\n",trial,p.columns,
            diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
        if(diff.first_register>=0)for(unsigned i=0;i<4;++i)fprintf(stderr,"lane%u %08X / %08X\n",i,native.gpr[diff.first_register]._u32[i],reference.gpr[diff.first_register]._u32[i]);
        abort();
    }
    assert(native.host_depth==reference.host_depth && native.image_id==reference.image_id);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto* file=fopen("analysis/native-geometry/phase3/test-fixed-stream-fixture.bin","rb");assert(file);
    assert(fread(fixture,1,sizeof fixture,file)==sizeof fixture);fclose(file);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial);compare(*native,*reference,trial);}
    for(unsigned trial=0;trial<4096;++trial){
        seed(*native,trial);native->gpr[107]._u32[0]=trial%17;
        for(unsigned lane=0;lane<4;++lane){
            const unsigned width=(trial+lane*11)%33,fraction=(trial/33+lane*3)%(width+1);
            native->ls[0x3a000+lane*2]=uint8_t(width-fraction);native->ls[0x3a001+lane*2]=uint8_t(fraction);
        }
        compare(*native,*reference,4096+trial);
    }
    const uint32_t offsets[]={0,1,0xffffffff,0x80000000,0x7fffffff,0x01000001,0xffff0001,0x00ffffff};
    for(unsigned trial=0;trial<1024;++trial){
        seed(*native,trial);native->gpr[107]._u32[0]=trial%3;
        const unsigned rotation=trial%16;
        native->gpr[84]=spu_il(rotation);spu_ls_write128(native.get(),0x3d0e0,spu_ila(0x10203));
        native->gpr[121]._u32[0]=trial&1; // Fixed has no unit/float32 zero-count shortcut.
        for(unsigned lane=0;lane<4;++lane){
            const unsigned width=(trial/16+lane)%33,fraction=(trial/64+lane)%(width+1);
            native->ls[0x3a000+((rotation+lane*2)&15)]=uint8_t(width-fraction);
            native->ls[0x3a000+((rotation+lane*2+1)&15)]=uint8_t(fraction);
            geometry_job_put32(native->ls+0x3b000+lane*4,offsets[(trial+lane)%8]);
        }
        memset(native->ls+0x10000,int(trial%3==0?0:trial%3==1?0xff:0x80),0x11000);
        compare(*native,*reference,8192+trial);
    }
    for(unsigned rejection=0;rejection<33;++rejection){
        seed(*native,16);GeometryFixedStream p;
        switch(rejection){
        case 0:native->ls=reference->ls_store;break;
        case 1:native->gpr[107]._u32[0]=17;break;
        case 2:native->gpr[106]._u32[0]=0;break;
        case 3:native->gpr[106]._u32[0]=256;break;
        case 4:native->gpr[42]._u32[0]=256;break;
        case 5:native->gpr[83]._u32[0]=0;break;
        case 6:native->gpr[83]._u32[0]=1;break;
        case 7:native->gpr[81]._u32[0]=0;break;
        case 8:native->gpr[81]._u32[0]=1;break;
        case 9:native->gpr[80]._u32[0]=SPU_LS_SIZE;break;
        case 10:native->gpr[1]._u32[0]=3;break;
        case 11:native->ls[0xd230]^=1;break;
        case 12:native->ls[0xd240]^=1;break;
        case 13:native->ls[0xd5d0]^=1;break;
        case 14:native->ls[0xd5e0]^=1;break;
        case 15:native->ls[0xd5f0]^=1;break;
        case 16:geometry_job_put32(native->ls+0x3d110,SPU_LS_SIZE);break;
        case 17:geometry_job_put32(native->ls+0x3d0d0,SPU_LS_SIZE);break;
        case 18:native->ls[0x3a000]=33;break;
        case 19:native->ls[0x3d070]^=1;break;
        case 20:native->gpr[83]._u32[1]^=1;break;
        case 21:native->gpr[81]._u32[1]^=1;break;
        case 22:native->gpr[80]._u32[1]^=1;break;
        case 23:native->gpr[118]._u32[0]|=1;break;
        case 24:native->gpr[41]._u32[0]=0x10000;break;
        case 25:native->gpr[41]._u32[0]=0x3d000;for(auto& limit:native->gpr[80]._u32)limit=0x3d000;break;
        case 26:native->gpr[82]._u32[0]=0x3d040;break;
        case 27:native->gpr[82]._u32[0]=0x3a000;break;
        case 28:native->gpr[82]._u32[0]=0;break;
        case 29:native->gpr[124]._u32[0]=0x3fff0;break;
        case 30:native->gpr[41]._u32[0]=0x80000000;break;
        case 31:geometry_job_put32(native->ls+0x3d110,0x3d0d0);break;
        case 32:native->gpr[82]._u32[0]=0x3b000;break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));assert(!read_geometry_fixed_stream(*native,p));
        assert(!memcmp(reference.get(),native.get(),sizeof(*native)));
    }
    puts("Complete fixed stream:9216 strict GPR/LS/control/continuation cases;variable widths/fractions/offsets/rotated records;33 unchanged/noIO rejects PASS");
}
