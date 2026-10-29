#include "../port/spu/buzz_job_6eba/spu_recomp.h"
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include <assert.h>
int g_spu_ls_watch_n, g_spu_smc_watch, g_spu_ls_probe, g_wws_code_probe;
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_ls_watch_slow(uint32_t a,int b,const uint8_t* c,uint32_t d,uint32_t e) { abort(); }
void spu_indirect_branch(spu_context* c) { abort(); }
void buzz_job_6eba_spu_func_00006DF8(spu_context* c) { abort(); }
void buzz_job_6eba_spu_func_00006888(spu_context* c) { abort(); }
void buzz_job_6eba_spu_func_00009008(spu_context* c) { abort(); }
void buzz_job_6eba_spu_func_000082B8(spu_context* c) { abort(); }
#ifdef TEST_GEOMETRY_SHUFFLE_PLANS
#include "../analysis/native-geometry-plans/test-kernels.inc"
#elif defined(TEST_DIRECT_GEOMETRY_LS)
#include "../analysis/native-geometry-direct-ls/test-kernels.inc"
#else
#include "../analysis/native-geometry/test-kernels.inc"
#endif
static uint32_t rng = 0xabcdef12;
static uint32_t word(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static spu_context a, b;
#ifdef TEST_GEOMETRY_DIGEST
static uint64_t digest=14695981039346656037ull;
static void hash_state(const void* memory,size_t bytes){const unsigned char* p=memory;
    for(size_t i=0;i<bytes;++i){digest^=p[i];digest*=1099511628211ull;}}
#endif
int main(void) {
    unsigned saved = _mm_getcsr(); _mm_setcsr(0x1f80 | 0x6000 | 0x8040);
    void (*ref[])(spu_context*) = {buzz_job_6eba_spu_func_000063E8_reference,
        buzz_job_6eba_spu_func_00006E00_reference,buzz_job_6eba_spu_func_00009158_reference};
    void (*opt[])(spu_context*) = {buzz_job_6eba_spu_func_000063E8_native,
        buzz_job_6eba_spu_func_00006E00_native,buzz_job_6eba_spu_func_00009158_native};
    for (unsigned n=0;n<1200;++n) for (unsigned k=0;k<3;++k) {
        memset(&a,0,sizeof a); a.ls=a.ls_store; a.image_id=41; a.host_depth=n&1;
        for (unsigned i=0;i<128;++i) for(unsigned j=0;j<4;++j) a.gpr[i]._u32[j]=word();
        for (unsigned i=0;i<SPU_LS_SIZE;i+=4) { uint32_t v=word(); memcpy(a.ls+i,&v,4); }
        // Keep randomized pure blocks bounded: 63E8 increments r30 by 64;
        // 9158's signed end comparison takes the exit after one iteration.
        if(k==0) a.gpr[30]._u32[0]=0u-64u*(1+n%4);
        if(k==2) { a.gpr[32]._u32[0]=5; a.gpr[80]._u32[0]=4; }
        memcpy(&b,&a,sizeof b); b.ls=b.ls_store;
        g_spu_trampoline_fn=NULL; ref[k](&a); void (*out)(spu_context*)=g_spu_trampoline_fn;
        g_spu_trampoline_fn=NULL; opt[k](&b);
        assert(!memcmp(a.gpr,b.gpr,sizeof a.gpr)); assert(!memcmp(a.ls,b.ls,SPU_LS_SIZE));
        assert(a.pc==b.pc && a.status==b.status && out==g_spu_trampoline_fn);
#ifdef TEST_GEOMETRY_DIGEST
        hash_state(a.gpr,sizeof a.gpr);hash_state(a.ls,SPU_LS_SIZE);hash_state(&a.pc,sizeof a.pc);hash_state(&a.status,sizeof a.status);
#endif
    }
    _mm_setcsr(saved);
    puts("PASS: 3600 randomized complete geometry states, all GPRs, full LS, PC and trampoline");
#ifdef TEST_GEOMETRY_DIGEST
    printf("GEOMETRY_DIGEST=%016llX\n",(unsigned long long)digest);
#endif
    return 0;
}
