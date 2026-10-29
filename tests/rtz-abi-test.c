#include "../ps3recomp/runtime/spu/spu_helpers.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#include <assert.h>
#if defined(SPU_RTZ_RESULT_GUARD)
#include "../ps3recomp/runtime/spu/spu_native_rtz.h"
static void check_result_guard(void){
    /* Under RZ, overflow returns finite 0x7f7fffff, NOT infinity. A mere
     * isfinite() check would silently accept the wrong SPU extended result. */
    u128 a=spu_splat_u32(0x7f7fffff),b=spu_splat_u32(0x40000000);
    __m128 overflow=_mm_mul_ps(_mm_castsi128_ps(rtz_load(a)),_mm_castsi128_ps(rtz_load(b)));
    u128 raw=rtz_store(_mm_castps_si128(overflow));
    assert(raw._u32[0]==0x7f7fffff);
    assert(_mm_movemask_ps(_mm_castsi128_ps(rtz_result_valid(overflow)))==0);
    u128 fixed=spu_native_rtz_fm(a,b),expected=spu_fm_ref(a,b);
    assert(!memcmp(&fixed,&expected,16) && fixed._u32[0]==0x7fffffff);
    const uint32_t candidates[]={0,0x80000000u,1,0x00800000u,0x3f800000u,0x7f7ffffeu,0xff7ffffeu,
        0x7f7fffffu,0xff7fffffu,0x7f800000u,0xff800000u,0x7fc00000u,0xffffffffu};
    for(unsigned i=0;i<sizeof candidates/sizeof(*candidates);++i){
        u128 value=spu_splat_u32(candidates[i]);
        unsigned mask=(unsigned)_mm_movemask_ps(_mm_castsi128_ps(rtz_result_valid(_mm_castsi128_ps(rtz_load(value)))));
        assert(mask==((candidates[i]&0x7fffffffu)<0x7f7fffffu?15u:0u));
    }
    puts("PASS: RZ overflow-to-MAX_FINITE, exact extended repair, zeros, sign, NaN/Inf encodings and guard boundaries");
}
#endif
void rtz_abi_legacy(u128*,const u128*,unsigned);
void rtz_abi_vector(u128*,const u128*,unsigned);
enum {N=1024};
static u128 input[N*3],legacy[N],vector[N];
static uint32_t rng=0xb182cc32u;
static uint32_t word(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static double now(void){
#ifdef _WIN32
    LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return(double)t.QuadPart/f.QuadPart;
#else
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(double)t.tv_sec+t.tv_nsec*1e-9;
#endif
}
static void check(void){
    rtz_abi_legacy(legacy,input,N);rtz_abi_vector(vector,input,N);
    assert(!memcmp(legacy,vector,sizeof vector));
    for(unsigned i=0;i<N;++i){
        u128 a=input[i*3],b=input[i*3+1],c=input[i*3+2];
        u128 expected=spu_fa_ref(spu_fma_ref(a,b,c),spu_fm_ref(a,b));
        assert(!memcmp(&vector[i],&expected,sizeof expected));
    }
}
int main(void){
    unsigned saved=_mm_getcsr();_mm_setcsr(0x1f80|0x6000|0x8040);
#if defined(SPU_RTZ_RESULT_GUARD)
    check_result_guard();
#endif
    for(unsigned run=0;run<256;++run){
        for(unsigned i=0;i<N*3;++i)for(unsigned lane=0;lane<4;++lane)input[i]._u32[lane]=word();
        check();
    }
    static const uint32_t edge[]={0,0x80000000u,1,0x007fffffu,0x00800000u,0x3f800000u,
        0xbf800000u,0x7f000000u,0x7f7fffffu,0x7f800000u,0x7fffffffu,0xffffffffu};
    for(unsigned mask=0;mask<16;++mask){
        for(unsigned i=0;i<N*3;++i)for(unsigned lane=0;lane<4;++lane)
            input[i]._u32[lane]=(mask&(1u<<lane))?edge[word()%12]:((word()&0x807fffffu)|0x3f000000u);
        check();
    }
    /* Ordinary stream microbenchmark; does not imply a whole-game FPS gain. */
    for(unsigned i=0;i<N*3;++i)for(unsigned lane=0;lane<4;++lane)
        input[i]._u32[lane]=(word()&0x807fffffu)|((120u+word()%12)<<23);
    check();
    double elapsed[2]={0,0};volatile uint32_t checksum=0;
    for(unsigned run=0;run<8;++run){
        unsigned order=(run^(run>>1))&1;
        for(unsigned n=0;n<2;++n){
            unsigned which=order^n;double start=now();
            for(unsigned repeat=0;repeat<2000;++repeat){
                if(which)rtz_abi_vector(vector,input,N);else rtz_abi_legacy(vector,input,N);
                checksum+=vector[repeat&(N-1)]._u32[repeat&3];
            }
            elapsed[which]+=now()-start;
        }
    }
    _mm_setcsr(saved);
    printf("PASS: 279552 compound vectors, all cold-lane masks, legacy/vector ABI and exact reference agree\n");
    printf("ABI micro: legacy=%.2f ns/vector, vector-return=%.2f ns/vector, checksum=%u (not game FPS)\n",
        elapsed[0]*1e9/(8*2000*N),elapsed[1]*1e9/(8*2000*N),checksum);
}
