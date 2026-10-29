#include "../port/spu/buzz_job_abe1/spu_recomp.h"
#include "../ps3recomp/runtime/spu/spu_helpers.h"
int spu_native_extra_fp_mode=1;
#include "../port/native_image40_fp.h"
#include <assert.h>
int g_spu_ls_watch_n,g_spu_smc_watch,g_spu_ls_probe,g_wws_code_probe;
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_ls_watch_slow(uint32_t a,int b,const uint8_t*c,uint32_t d,uint32_t e){abort();}
void buzz_job_abe1_spu_func_000073C8(spu_context* ctx){abort();}
#include "../analysis/native-image40/test-kernels.inc"
static spu_context a,b;
static uint32_t rng=0x51673492;
static uint32_t word(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void) {
    unsigned saved=_mm_getcsr();_mm_setcsr(0x1f80|0x6000|0x8040);
    for(unsigned n=0;n<1600;++n) {
        memset(&a,0,sizeof a);a.ls=a.ls_store;a.image_id=40;
        for(unsigned i=0;i<128;++i)for(unsigned j=0;j<4;++j)a.gpr[i]._u32[j]=word();
        for(unsigned i=0;i<SPU_LS_SIZE;i+=4){uint32_t v=word();memcpy(a.ls+i,&v,4);}
        a.gpr[55]._u32[0]=1+n%4; /* decremented once per loop; bound random input */
        memcpy(&b,&a,sizeof b);b.ls=b.ls_store;
        g_spu_trampoline_fn=NULL;buzz_job_abe1_spu_func_00006D78_reference(&a);
        void (*out)(spu_context*)=g_spu_trampoline_fn;
        g_spu_trampoline_fn=NULL;buzz_job_abe1_spu_func_00006D78_locals(&b);
        assert(!memcmp(a.gpr,b.gpr,sizeof a.gpr));
        assert(!memcmp(a.ls,b.ls,SPU_LS_SIZE));
        assert(a.pc==b.pc && a.status==b.status && out==g_spu_trampoline_fn);
    }
    _mm_setcsr(saved);
    puts("PASS: image40 1600 full states, 1..4 loop iterations, GPR/LS/PC/trampoline");
}
