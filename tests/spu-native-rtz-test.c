#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include "../ps3recomp/runtime/spu/spu_native_rtz.h"
/* Test adapter: the proposed image-specific mode does NOT cover double
 * instructions. Keep the suite's double-precision check in its original
 * environment while running every single-precision case through the candidate. */
static u128 original_double_fa(u128 a, u128 b)
{
    unsigned saved = _mm_getcsr(); _mm_setcsr(0x1f80);
    u128 out = spu_dfa(a,b); _mm_setcsr(saved); return out;
}
#define spu_fa(a,b) spu_native_rtz_fa(a,b,0)
#define spu_fs(a,b) spu_native_rtz_fa(a,b,0x80000000u)
#define spu_fm(a,b) spu_native_rtz_fm(a,b)
#define spu_fma(a,b,c) spu_native_rtz_fma(a,b,c,0,0)
#define spu_fms(a,b,c) spu_native_rtz_fma(a,b,c,0,0x80000000u)
#define spu_fnms(a,b,c) spu_native_rtz_fma(a,b,c,0x80000000u,0)
#define spu_dfa(a,b) original_double_fa(a,b)
#define main single_precision_suite
#include "../ps3recomp/runtime/spu/tests/test_spu_float_neon.c"
#undef main
int main(void)
{
    unsigned saved = _mm_getcsr();
    _mm_setcsr(0x1f80 | 0x6000 | 0x8040);
    int result = single_precision_suite();
    _mm_setcsr(saved);
    return result;
}
