#define main stack_regression_main
#include "../ps3recomp/runtime/spu/tests/test_spurs_job_stack.c"
#undef main
#include <immintrin.h>
#include <assert.h>
#ifdef SPU_NATIVE_RTZ_EXTRA_IMAGE
#include "spu_native_extra_fp.h"
#endif
#ifdef SPU_NATIVE_RTZ_IMAGE39
#include "spu_native_extra_fp.h"
#endif
static int native_expected, halt_expected;
static unsigned incoming;
static void check_environment(spu_context* ctx)
{
    unsigned wanted = native_expected ? ((incoming & ~0x6000u) | 0x6000u | 0x8040u | 0x1f80u) : incoming;
    assert(_mm_getcsr() == wanted);
    if (halt_expected) spu_halt(ctx);
}
int main(void)
{
    unsigned saved = _mm_getcsr();
    incoming = 0x1f80u | 0x2000u; /* deliberately not RNE, no FTZ/DAZ */
    for (int n = 0; n < 4; ++n) {
        _mm_setcsr(incoming);
        native_expected = n & 1; halt_expected = n & 2;
        build_job(0,0);
        assert(spu_run_spurs_job(check_environment, native_expected ? 41 : 7, JOB_EA, 256) == 0);
        assert(_mm_getcsr() == incoming);
    }
#ifdef SPU_NATIVE_RTZ_EXTRA_IMAGE
    for (int mode = 0; mode < 3; ++mode) for (int h = 0; h < 2; ++h) {
        /* Tests may set the immutable-in-production startup option directly. */
        spu_native_extra_fp_mode = mode;
        _mm_setcsr(incoming);
        native_expected = mode != 0; halt_expected = h;
        build_job(0,0);
        assert(spu_run_spurs_job(check_environment, 40, JOB_EA, 256) == 0);
        assert(_mm_getcsr() == incoming);
        native_expected = 0;
        assert(spu_run_spurs_job(check_environment, 7, JOB_EA, 256) == 0);
        assert(_mm_getcsr() == incoming);
    }
#endif
#ifdef SPU_NATIVE_RTZ_IMAGE39
    for (int mode = 0; mode < 3; ++mode) for (int h = 0; h < 2; ++h) {
        spu_native_image39_fp_mode = mode;
        _mm_setcsr(incoming);
        native_expected = mode != 0; halt_expected = h;
        build_job(0,0);
        assert(spu_run_spurs_job(check_environment, 39, JOB_EA, 256) == 0);
        assert(_mm_getcsr() == incoming);
        native_expected = 0;
        assert(spu_run_spurs_job(check_environment, 38, JOB_EA, 256) == 0);
        assert(_mm_getcsr() == incoming);
    }
#endif
    _mm_setcsr(saved);
    puts("SPU RTZ owner: image-scoped environment, normal return and HALT/longjmp restoration PASS");
}
