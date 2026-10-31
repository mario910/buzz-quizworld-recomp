#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_blend_scatter.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*){abort();}
void spu_stop(spu_context*){abort();}
void spu_drain_call(spu_context*,uint32_t){abort();}
void spu_img_restore(spu_context*,int32_t){abort();}
}
#include "../analysis/native-geometry/phase3/test-blend-scatter-reference.inc"
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& ctx,unsigned trial)
{
    memset(&ctx,0,sizeof ctx);ctx.ls=ctx.ls_store;ctx.image_id=41;ctx.pc=0xc750;ctx.host_depth=trial&1;
    uint32_t state=trial+1;
    for(auto& r:ctx.gpr)for(auto& word:r._u32)word=random_word(state);
    for(unsigned i=0;i<SPU_LS_SIZE;++i)ctx.ls[i]=uint8_t(random_word(state));
    const unsigned counts[]={1,2,3,4,5,7,8,9,15,16,17,31,32,33,63,64,127,128,255,256};
    const unsigned pairs=counts[(trial/16)%20],mode=(trial/320)%8;
    ctx.gpr[0]._u32[0]=trial&2?0xc7a8:0x1380;
    ctx.gpr[3]._u32[0]=0x30000+trial%16;ctx.gpr[4]._u32[0]=0x18000+trial%16+(mode==7?0x80000000:0);
    ctx.gpr[5]._u32[0]=0x10000+pairs*32+(trial/16)%16;ctx.gpr[7]._u32[0]=0u-pairs*32;
    for(unsigned i=0;i<pairs+8;++i){
        unsigned a=i*32,b=a+16;
        switch(mode){
        case 1:a=b=(i%32)*16;break;
        case 2:a=(i%32)*16;b=((i+1)%32)*16;break;
        case 3:a=(i%32)*16;b=((i+31)%32)*16;break;
        case 4:a=b=0;break;
        case 5:a=(random_word(state)%128)*16;b=(random_word(state)%128)*16;break;
        case 6:a=(i%64)*16+i%3;b=((i*7)%64)*16+5;break;
        case 7:a=((i*3)%4)*16;b=((i*5)%4)*16;break;
        }
        geometry_job_put32(ctx.ls+0x30000+i*4,(a<<16)|b);
    }
    ctx.int_enable=trial&1;ctx.resv_valid=trial&1;ctx.mfc_tag_status=trial*0x123;
}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    memcpy(&reference,&native,sizeof native);reference.ls=reference.ls_store;
    GeometryBlendScatter p;const char* reason=nullptr;
    if(!read_geometry_blend_scatter(native,p,&reason)){fprintf(stderr,"blend preflight rejected trial%u:%s\n",trial,reason);abort();}
    g_spu_trampoline_fn=nullptr;buzz_job_6eba_spu_func_0000C750(&reference);
    assert(g_spu_trampoline_fn==buzz_job_6eba_spu_func_0000C7A8);g_spu_trampoline_fn=nullptr;
    buzz_job_6eba_spu_func_0000C7A8(&reference);
    auto expected=g_spu_trampoline_fn;assert(expected==(reference.host_depth?nullptr:spu_indirect_branch));
    g_spu_trampoline_fn=nullptr;blend_geometry_scatter(native,p);native.pc=native.gpr[0]._u32[0];
    g_spu_trampoline_fn=native.host_depth?nullptr:spu_indirect_branch;
    assert(g_spu_trampoline_fn==expected);g_spu_trampoline_fn=nullptr;
    const auto diff=compare_function_state(native,reference);
    if(!diff.exact()){
        fprintf(stderr,"blend trial%u pairs%u:r%d byte%d LS=%X/%u state=%s\n",trial,p.pairs,diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
        if(diff.first_register>=0)for(unsigned i=0;i<4;++i)fprintf(stderr,"lane%u %08X/%08X\n",i,native.gpr[diff.first_register]._u32[i],reference.gpr[diff.first_register]._u32[i]);
        abort();
    }
    assert(native.host_depth==reference.host_depth && native.image_id==reference.image_id);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial);compare(*native,*reference,trial);}
    const uint32_t bits[]={0,0x80000000,1,0x007fffff,0x00800000,0x3f800000,0xbf800000,0x3f000000,0x7f800000,0xff800000,0x7fc00001,0x7fffffff,0xffffffff,0x12345678,0x89abcdef,0x80000001};
    const uint32_t masks[]={0,0xffffffff,0xffffff00,0x80000000,0x7fffffff,0xffff0000,0x0000ffff,0x55555555};
    for(unsigned trial=0;trial<1024;++trial){
        seed(*native,trial+320);const unsigned pairs=(0u-native->gpr[7]._u32[0])/32;
        for(unsigned lane=0;lane<4;++lane){native->gpr[6]._u32[lane]=masks[(trial+lane)%8];native->gpr[8]._u32[lane]=bits[(trial/8+lane)%16];}
        for(unsigned at=0x10000;at<0x10000+pairs*32+64;at+=4)geometry_job_put32(native->ls+at,bits[(at/4+trial)%16]);
        compare(*native,*reference,4096+trial);
    }
    for(unsigned mode=0;mode<2;++mode){
        seed(*native,0);native->gpr[5]._u32[0]=0x10040;native->gpr[7]._u32[0]=0u-64;
        native->gpr[6]=spu_il(-1);native->gpr[8]=spu_splat_u32(0x3f800000);
        memset(native->ls+0x30000,0,32);memset(native->ls+0x18000,0,32);
        const uint32_t values[]={0x3f800000,0x41200000,0x42c80000,0x447a0000};
        for(unsigned i=0;i<4;++i)spu_ls_write128(native.get(),0x10000+i*16,spu_splat_u32(values[i]));
        if(mode){geometry_job_put32(native->ls+0x30000,0x00000010);geometry_job_put32(native->ls+0x30004,0x00100000);}
        compare(*native,*reference,5120+mode);
        for(unsigned lane=0;lane<4;++lane){
            assert(geometry_job_be32(native->ls+0x18000+lane*4)==(mode?0x447a0000:0x42dc0000));
            if(mode)assert(geometry_job_be32(native->ls+0x18010+lane*4)==0x42dc0000);
        }
    }
    seed(*native,0);geometry_job_put32(native->ls+0x30008,0xffffffff);compare(*native,*reference,5122);
    seed(*native,0);native->gpr[3]._u32[0]=0x10000;
    for(unsigned i=0;i<4;++i)geometry_job_put32(native->ls+0x10000+i*4,0);
    compare(*native,*reference,5123);
    const unsigned large_counts[]={257,512,1024,4096};
    for(unsigned i=0;i<4;++i){
        seed(*native,0);const unsigned pairs=large_counts[i];native->gpr[5]._u32[0]=0x10000+pairs*32;
        native->gpr[7]._u32[0]=0u-pairs*32;native->gpr[3]._u32[0]=0x2000;native->gpr[4]._u32[0]=0x3e000;
        memset(native->ls+0x2000,0,((pairs+2)*4+15)&~15u);
        compare(*native,*reference,5124+i);
    }
    for(unsigned trial=0;trial<1024;++trial){
        seed(*native,trial%640);native->gpr[4]._u32[0]=0x10000+(trial&7)*16;
        if(trial&1){native->gpr[6]=spu_il(-1);native->gpr[8]=spu_splat_u32(0x3f000000);}
        compare(*native,*reference,5128+trial);
    }
    seed(*native,0);geometry_job_put32(native->ls+0x30004,0x00030000);
    compare(*native,*reference,6152); // Speculative destination read below data, never stored.
    for(unsigned rejection=0;rejection<14;++rejection){
        seed(*native,0);GeometryBlendScatter p;
        switch(rejection){
        case 0:native->ls=reference->ls_store;break;
        case 1:native->gpr[7]._u32[0]=0;break;
        case 2:native->gpr[7]._u32[0]=32;break;
        case 3:native->gpr[7]._u32[0]=0u-31;break;
        case 4:native->gpr[7]._u32[0]=0u-4097*32;break;
        case 5:native->gpr[5]._u32[0]=16;break;
        case 6:native->gpr[5]._u32[0]=0xe670;break;
        case 7:native->gpr[5]._u32[0]=SPU_LS_SIZE-16;break;
        case 8:native->gpr[3]._u32[0]=0xffffffff;break;
        case 9:native->gpr[3]._u32[0]=SPU_LS_SIZE;break;
        case 10:native->gpr[3]._u32[0]=SPU_LS_SIZE-4;break;
        case 11:native->gpr[5]._u32[0]=0xe680;break;
        case 12:native->gpr[4]._u32[0]=0;break;
        case 13:native->gpr[4]._u32[0]=0x30000;break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));assert(!read_geometry_blend_scatter(*native,p));
        assert(!memcmp(reference.get(),native.get(),sizeof(*native)));
    }
    puts("Blend scatter:6153 strict GPR/LS/control/root-return cases;biased pointers,duplicate indices,pipeline aliases,raw FP/mask/weight bits;14 unchanged/noIO rejects PASS");
}
