#include "../ps3recomp/runtime/spu/spu_helpers.h"
int spu_native_extra_fp_mode;
#ifdef TEST_IMAGE39_FP
int spu_native_image39_fp_mode;
#define BUZZ_NATIVE_FP_MODE spu_native_image39_fp_mode
#define BUZZ_NATIVE_FP_LABEL "image39-fp"
#endif
#include "../port/native_image40_fp.h"
static u128 original_double_fa(u128 a,u128 b)
{
    unsigned saved=_mm_getcsr(); _mm_setcsr(0x1f80);
    u128 out=spu_dfa(a,b); _mm_setcsr(saved); return out;
}
#define spu_dfa original_double_fa
#define main float_suite
#include "../ps3recomp/runtime/spu/tests/test_spu_float_neon.c"
#undef main
int main(void)
{
    unsigned saved=_mm_getcsr();
    int result=0;
    for(int mode=0; mode<3; ++mode) {
        BUZZ_NATIVE_FP_MODE=mode;
        _mm_setcsr(mode ? 0x1f80|0x6000|0x8040 : 0x1f80);
        result |= float_suite();
    }
    _mm_setcsr(saved);
    return result;
}
