#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
static uint32_t rng=0xd267ad91u;
static uint32_t word(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static unsigned long long checked;
static void check(u128 a,u128 b){
    u128 ref=spu_fi_ref(a,b),got=spu_fi(a,b);
    if(memcmp(&ref,&got,16)){
        for(unsigned n=0;n<4;++n)fprintf(stderr,"FI lane%u a=%08X b=%08X got=%08X ref=%08X\n",n,a._u32[n],b._u32[n],got._u32[n],ref._u32[n]);
        abort();
    }
    ++checked;
}
static uint32_t estimate(unsigned base,unsigned step,unsigned exponent,unsigned sign){
    return(sign<<31)|(exponent<<23)|(base<<10)|step;
}
static double now(void){
#ifdef _WIN32
    LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return(double)t.QuadPart/f.QuadPart;
#else
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(double)t.tv_sec+t.tv_nsec*1e-9;
#endif
}
enum{N=1024};
static u128 inputs[N][2],outputs[N];
static __attribute__((noinline)) void stream_ref(void){for(unsigned n=0;n<N;++n)outputs[n]=spu_fi_ref(inputs[n][0],inputs[n][1]);}
static __attribute__((noinline)) void stream_simd(void){for(unsigned n=0;n<N;++n)outputs[n]=spu_fi(inputs[n][0],inputs[n][1]);}
int main(void){
    /* Every base/step pair; lanes cover zero, underflow, ordinary and extended
     * exponent, signs, and different Y. Separate Y coverage follows below. */
    for(unsigned base=0;base<8192;++base)for(unsigned step=0;step<1024;++step){
        u128 a,b;
        const unsigned y[]={0,1,0x7ffff,0x3ffff},e[]={0,1,127,255};
        for(unsigned n=0;n<4;++n){a._u32[n]=y[n];b._u32[n]=estimate(base,step,e[n],n&1);}
        check(a,b);
    }
    /* Exhaust all Y values against low/high bases and steps. */
    const unsigned bases[]={0,1,8191},steps[]={0,1,1023};
    for(unsigned y=0;y<0x80000;++y)for(unsigned bi=0;bi<3;++bi)for(unsigned si=0;si<3;++si){
        u128 a,b;const unsigned e[]={1,2,254,255};
        for(unsigned n=0;n<4;++n){a._u32[n]=y^(n&1?0x7ffff:0);b._u32[n]=estimate(bases[bi],steps[si],e[n],n&1);}
        check(a,b);
    }
    /* Just below/equal/above the exact normalization boundary for every
     * base that can borrow and every nonzero step. */
    for(unsigned base=0;base<1024;++base)for(unsigned step=1;step<1024;++step){
        uint32_t threshold=(base<<19)/step;
        if(threshold>0x7ffff)continue;
        u128 a,b;
        const uint32_t ys[]={threshold?threshold-1:0,threshold,threshold<0x7ffff?threshold+1:threshold,0x7ffff};
        for(unsigned n=0;n<4;++n){a._u32[n]=ys[n];b._u32[n]=estimate(base,step,n==0?0:n==1?1:n==2?2:255,n&1);}
        check(a,b);
    }
    for(unsigned mode=0;mode<16;++mode){
        unsigned saved=_mm_getcsr(),chosen=0x1f80|((mode&3)<<13)|((mode&4)?0x8000:0)|((mode&8)?0x40:0)|0x25;
        _mm_setcsr(chosen);
        for(unsigned n=0;n<20000;++n){u128 a,b;for(unsigned k=0;k<4;++k){a._u32[k]=word();b._u32[k]=word();}check(a,b);}
        assert(_mm_getcsr()==chosen);_mm_setcsr(saved);
    }
    for(unsigned n=0;n<N;++n)for(unsigned k=0;k<4;++k){inputs[n][0]._u32[k]=word();inputs[n][1]._u32[k]=word();}
    double elapsed[2]={0,0};volatile uint32_t checksum=0;
    for(unsigned run=0;run<4;++run)for(unsigned order=0;order<2;++order){
        unsigned which=order^(run&1);double start=now();
        for(unsigned r=0;r<2000;++r){if(which)stream_simd();else stream_ref();checksum+=outputs[r&(N-1)]._u32[r&3];}
        elapsed[which]+=now()-start;
    }
    printf("PASS: FI %llu vectors, all base/step pairs, exhaustive Y edges, borrow boundaries, 16 MXCSR modes unchanged\n",checked);
    printf("FI stream: reference %.2f ns/vector, selected %.2f ns/vector, checksum=%u (not game FPS)\n",elapsed[0]*1e9/(4*2000*N),elapsed[1]*1e9/(4*2000*N),checksum);
    return 0;
}
