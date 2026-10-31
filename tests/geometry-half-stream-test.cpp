#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_half_stream.h"
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
#include "../analysis/native-geometry/phase3/test-half-stream-reference.inc"
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
    memset(&ctx,0,sizeof ctx);ctx.ls=ctx.ls_store;ctx.image_id=41;ctx.pc=0x8378;ctx.host_depth=trial&1;
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
    ctx.int_enable=trial&1;ctx.resv_valid=trial&1;ctx.mfc_tag_status=trial*0x123;
}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    memcpy(&reference,&native,sizeof native);reference.ls=reference.ls_store;
    GeometryHalfStream p;const char* reason=nullptr;
    if(!read_geometry_half_stream(native,p,&reason)){fprintf(stderr,"half preflight rejected trial%u: %s\n",trial,reason);abort();}
    buzz_job_6eba_spu_func_00008378(&reference);
    assert(reference.pc==0x83c8 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);g_spu_trampoline_fn=nullptr;
    decode_geometry_half_stream(native,p);native.pc=0x83c8;
    const auto diff=compare_function_state(native,reference);
    if(!diff.exact()){
        fprintf(stderr,"half trial%u cols%u differs r%d byte%d LS=%X/%u state=%s\n",trial,p.columns,
            diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
        if(diff.first_register>=0)for(unsigned i=0;i<4;++i)fprintf(stderr,"lane%u %08X / %08X\n",i,native.gpr[diff.first_register]._u32[i],reference.gpr[diff.first_register]._u32[i]);
        abort();
    }
    assert(native.host_depth==reference.host_depth && native.image_id==reference.image_id);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto* file=fopen("analysis/native-geometry/phase3/test-half-stream-fixture.bin","rb");assert(file);
    assert(fread(fixture,1,sizeof fixture,file)==sizeof fixture);fclose(file);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial);compare(*native,*reference,trial);}
    const unsigned bases[]={118,117,116,124};
    for(unsigned trial=0;trial<4096;++trial){
        seed(*native,0);native->gpr[42]._u32[0]=8;
        for(unsigned lane=0;lane<4;++lane)native->gpr[80]._u32[lane]=0x243f0;
        for(unsigned row=0;row<4;++row){auto* at=native->ls+native->gpr[bases[row]]._u32[0];at[0]=uint8_t(trial+row);
            for(unsigned component=0;component<4;++component){const unsigned half=trial*16+row*4+component;
                at[8+component*2]=uint8_t(half>>8);at[9+component*2]=uint8_t(half);}}
        compare(*native,*reference,4096+trial);
    }
    for(unsigned rejection=0;rejection<23;++rejection){
        seed(*native,16);GeometryHalfStream p;
        switch(rejection){
        case 0:native->ls=reference->ls_store;break;
        case 1:native->gpr[107]._u32[0]=0;break;
        case 2:native->gpr[107]._u32[0]=17;break;
        case 3:native->gpr[106]._u32[0]=0;break;
        case 4:native->gpr[42]._u32[0]=256;break;
        case 5:native->gpr[83]._u32[0]=1;break;
        case 6:native->gpr[81]._u32[0]=0;break;
        case 7:native->gpr[80]._u32[0]=SPU_LS_SIZE;break;
        case 8:native->gpr[1]._u32[0]=3;break;
        case 9:native->ls[0xd5d0]^=1;break;
        case 10:native->ls[0xd5e0]^=1;break;
        case 11:native->ls[0xd5f0]^=1;break;
        case 12:native->ls[0xd600]^=1;break;
        case 13:native->gpr[118]._u32[0]|=1;break;
        case 14:native->ls[0x3d070]^=1;break;
        case 15:native->gpr[82]._u32[0]=SPU_LS_SIZE;break;
        case 16:native->gpr[41]._u32[0]=0x10000;break;
        case 17:native->gpr[41]._u32[0]=0xd200;break;
        case 18:native->gpr[82]._u32[0]=0x3d040;break;
        case 19:native->gpr[83]._u32[1]^=1;break;
        case 20:native->gpr[81]._u32[1]^=1;break;
        case 21:native->gpr[80]._u32[1]^=1;break;
        case 22:native->gpr[124]._u32[0]=0x3fff0;break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));assert(!read_geometry_half_stream(*native,p));
        assert(!memcmp(reference.get(),native.get(),sizeof(*native)));
    }
    puts("Complete half stream:8192 strict GPR/LS/control/continuation cases;all65536half patterns;23unchanged/noIO rejects PASS");
}
