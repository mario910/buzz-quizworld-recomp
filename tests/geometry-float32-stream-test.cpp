#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_float32_stream.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_drain_call(spu_context*,uint32_t){abort();}
void spu_img_restore(spu_context*,int32_t){abort();}
}
#include "../analysis/native-geometry/phase3/test-float32-stream-reference.inc"
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& ctx,unsigned trial)
{
    memset(&ctx,0,sizeof ctx);ctx.ls=ctx.ls_store;ctx.image_id=41;ctx.pc=0x87cc;ctx.host_depth=trial&1;
    uint32_t random=trial+1;
    for(auto& r:ctx.gpr)for(auto& word:r._u32)word=random_word(random);
    for(unsigned i=0;i<SPU_LS_SIZE;++i)ctx.ls[i]=uint8_t(random_word(random));
    ctx.gpr[1]._u32[0]=0x3d000;ctx.gpr[121]._u32[0]=1;ctx.gpr[107]._u32[0]=1+(trial/16)%16;
    ctx.gpr[106]._u32[0]=1+(trial/256)%16;ctx.gpr[42]._u32[0]=trial%16;
    ctx.gpr[25]._u32[0]=1+(trial/64)%16;
    const unsigned counts[]={0,1,2,3,4,7,15,31};
    const unsigned groups=counts[(trial/4)%8];
    for(unsigned lane=0;lane<4;++lane){
        ctx.gpr[41]._u32[lane]=0x24000+lane*256+trial%16;
        ctx.gpr[80]._u32[lane]=0x24000+groups*1024+240;
        ctx.gpr[81]._u32[lane]=1024;ctx.gpr[83]._u32[lane]=256;
        ctx.gpr[82]._u32[lane]=lane?0xdead0000+random_word(random)%0xffff:(trial&1?0x3d020:0x3c000);
    }
    const unsigned bases[]={118,117,116,124};
    for(unsigned row=0;row<4;++row){
        ctx.gpr[bases[row]]._u32[0]=0x10000+row*0x4000;
        u128 pointer=ctx.gpr[row+4];pointer._u32[0]=ctx.gpr[bases[row]]._u32[0]+16;
        spu_ls_write128(&ctx,0x3d070-row*16,pointer);
    }
    for(unsigned byte=0;byte<16;++byte){ctx.gpr[112]._u8[byte^3]=uint8_t(byte);ctx.gpr[108]._u8[byte]=3;}
    ctx.int_enable=trial&1;ctx.resv_valid=trial&1;ctx.mfc_tag_status=trial*0x123;
}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    memcpy(&reference,&native,sizeof native);reference.ls=reference.ls_store;
    GeometryFloat32Stream p;const char* reason=nullptr;
    if(!read_geometry_float32_stream(native,p,&reason)){fprintf(stderr,"float32 preflight rejected trial%u: %s\n",trial,reason);abort();}
    buzz_job_6eba_spu_func_000087CC(&reference);unsigned steps=0;
    while(g_spu_trampoline_fn && reference.pc!=0x83c8){assert(++steps<65536);auto fn=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;fn(&reference);}
    assert(reference.pc==0x83c8 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);g_spu_trampoline_fn=nullptr;
    decode_geometry_float32_stream(native,p);native.pc=0x83c8;
    const auto diff=compare_function_state(native,reference);
    if(!diff.exact()){
        fprintf(stderr,"float32 trial%u cols%u differs r%d byte%d LS=%X/%u state=%s\n",trial,p.columns,
            diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
        if(diff.first_register>=0)for(unsigned i=0;i<4;++i)fprintf(stderr,"lane%u %08X / %08X\n",i,native.gpr[diff.first_register]._u32[i],reference.gpr[diff.first_register]._u32[i]);
        abort();
    }
    assert(native.host_depth==reference.host_depth && native.image_id==reference.image_id);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial);compare(*native,*reference,trial);}
    for(unsigned trial=0;trial<256;++trial){
        seed(*native,trial);native->gpr[107]._u32[0]=16;native->gpr[106]._u32[0]=17;
        native->gpr[25]._u32[0]=1+trial/16;
        for(auto& limit:native->gpr[80]._u32)limit=0x24000+trial%16;
        compare(*native,*reference,4096+trial);
    }
    const uint32_t bits[]={0,0x80000000,1,0x007fffff,0x00800000,0x3f800000,0x7f800000,0xff800000,0x7fc00001,0x7fffffff,0xffffffff,0x12345678,0x89abcdef,0x01020304,0x00008000,0x80000001};
    for(unsigned trial=0;trial<1024;++trial){
        seed(*native,trial);
        for(unsigned at=0x10000;at<0x22000;at+=4)geometry_job_put32(native->ls+at,bits[(at/4+trial)%16]);
        compare(*native,*reference,4352+trial);
    }
    for(unsigned trial=0;trial<64;++trial){seed(*native,trial);native->gpr[121]._u32[0]=0;native->gpr[1]._u32[0]=3;compare(*native,*reference,5376+trial);}
    for(unsigned rejection=0;rejection<25;++rejection){
        seed(*native,16);GeometryFloat32Stream p;
        switch(rejection){
        case 0:native->ls=reference->ls_store;break;
        case 1:native->gpr[107]._u32[0]=0;break;
        case 2:native->gpr[107]._u32[0]=17;break;
        case 3:native->gpr[106]._u32[0]=0;break;
        case 4:native->gpr[42]._u32[0]=256;break;
        case 5:native->gpr[25]._u32[0]=0;break;
        case 6:native->gpr[25]._u32[0]=17;break;
        case 7:native->gpr[83]._u32[0]=1;break;
        case 8:native->gpr[81]._u32[0]=0;break;
        case 9:native->gpr[80]._u32[0]=SPU_LS_SIZE;break;
        case 10:native->gpr[1]._u32[0]=3;break;
        case 11:native->gpr[112]._u32[0]^=1;break;
        case 12:native->gpr[108]._u32[0]^=1;break;
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
        case 23:native->gpr[41]._u32[0]=0x3d000;for(auto& limit:native->gpr[80]._u32)limit=0x3d000;break;
        case 24:native->gpr[82]._u32[0]=0x10000;break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));assert(!read_geometry_float32_stream(*native,p));
        assert(!memcmp(reference.get(),native.get(),sizeof(*native)));
    }
    puts("Complete float32 stream:5440 strict GPR/LS/control/continuation cases;cross/within payload bits and preferred scratch;25unchanged/noIO rejects PASS");
}
