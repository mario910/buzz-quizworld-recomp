#define SPU_NATIVE_RTZ 1
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include <assert.h>
#include <stdio.h>
static uint32_t random_state=0x317a3265;
static uint32_t next(void) { random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state; }
static unsigned long long checks;
static void check(u128 a,int scale)
{
    u128 ref[4]={spu_csflt_ref(a,scale),spu_cuflt_ref(a,scale),spu_cflts_ref(a,scale),spu_cfltu_ref(a,scale)};
    u128 out[4]={spu_csflt(a,scale),spu_cuflt(a,scale),spu_cflts(a,scale),spu_cfltu(a,scale)};
    for (int op=0;op<4;++op) for(int n=0;n<4;++n) {
        if (ref[op]._u32[n]!=out[op]._u32[n]) {
            printf("FAIL op=%d scale=%d input=%08X want=%08X got=%08X\n",op,scale,a._u32[n],ref[op]._u32[n],out[op]._u32[n]);
            assert(0);
        }
        ++checks;
    }
}
int main(void)
{
    unsigned saved=_mm_getcsr();_mm_setcsr(0x1f80|0x6000|0x8040);
    const uint32_t edge[]={0,1,0x80000000,0xffffffff,0x7fffffff,0x1000001,0x1000003,
        0x3f800000,0x007fffff,0x00800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,
        0x4f000000,0xcf000000,0x4f800000,0x4effffff,0x4f7fffff};
    for (int scale=0;scale<256;++scale) for(unsigned n=0;n<sizeof edge/sizeof *edge;++n) {
        u128 a;for(int k=0;k<4;++k)a._u32[k]=edge[(n+k)%(sizeof edge/sizeof *edge)];check(a,scale);
    }
    for (unsigned n=0;n<2000000;++n) { u128 a;for(int k=0;k<4;++k)a._u32[k]=next();check(a,(int)(next()&255)); }
    _mm_setcsr(saved);
    printf("SPU native conversion: %llu bitwise reference comparisons, 256 scales, saturation and extended exponents PASS\n",checks);
}
