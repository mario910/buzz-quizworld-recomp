// Microbenchmark only; does not measure game FPS. Build with -O2 and run on
// real hardware. The old and new functions deliberately stay out of line.
#include "spu_helpers.h"
#include <time.h>
#include <stdio.h>
static __attribute__((noinline)) u128 before(u128 a,u128 b,u128 c) { return spu_shufb(a,b,c); }
#include "spu_shuffle.h"
static __attribute__((noinline)) u128 after(u128 a,u128 b,u128 c) { return spu_shufb(a,b,c); }
static volatile unsigned checksum;
static double seconds(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec + t.tv_nsec*1e-9;
}
static double run(int optimized) {
    u128 a,b,c;
    for(unsigned i=0;i<4;++i){a._u32[i]=0x12345678u*i;b._u32[i]=0x76543210u*i;c._u32[i]=0x070e0318u*i;}
    double start=seconds();
    for(unsigned i=0;i<5000000;++i){
        c._u32[0]^=i; a=optimized?after(a,b,c):before(a,b,c);
        b._u32[0]+=a._u32[1]+i;
    }
    double elapsed=seconds()-start;checksum=a._u32[0];return elapsed;
}
int main(void) {
    for(unsigned i=0;i<6;++i){
        double old,new;
        if(i&1){new=run(1);old=run(0);}else{old=run(0);new=run(1);}
        printf("shuffle sample %u: before=%.6f after=%.6f speedup=%.3f checksum=%u\n",i,old,new,old/new,checksum);
    }
}
