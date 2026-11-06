#include "../ps3recomp/runtime/spu/spu_helpers.h"
int spu_native_extra_fp_mode;
#ifdef TEST_IMAGE39_FP
int spu_native_image39_fp_mode;
#define BUZZ_NATIVE_FP_MODE spu_native_image39_fp_mode
#define BUZZ_NATIVE_FP_LABEL "image39-fp"
#endif
#include "../port/native_image40_fp.h"
#if defined(__aarch64__)
#include "../port/spu_arm_fp_state.h"
typedef buzz_arm_fp_state fp_state;
static fp_state save_fp(void) { return buzz_arm_fp_save(); }
static void restore_fp(fp_state state) { buzz_arm_fp_restore(state); }
static void enter_fp(int mode)
{
    fp_state clean = {0,0};
    restore_fp(clean);
    if (mode) buzz_arm_fp_enter_rtz(clean);
}
#else
typedef unsigned fp_state;
static fp_state save_fp(void) { return _mm_getcsr(); }
static void restore_fp(fp_state state) { _mm_setcsr(state); }
static void enter_fp(int mode) { _mm_setcsr(mode ? 0x1f80|0x6000|0x8040 : 0x1f80); }
#endif
static u128 original_double_fa(u128 a,u128 b)
{
    fp_state saved=save_fp(); enter_fp(0);
    u128 out=spu_dfa(a,b); restore_fp(saved); return out;
}
#define spu_dfa original_double_fa
#define main float_suite
#include "../ps3recomp/runtime/spu/tests/test_spu_float_neon.c"
#undef main
int main(void)
{
    fp_state saved=save_fp();
    int result=0;
    for(int mode=0; mode<3; ++mode) {
        BUZZ_NATIVE_FP_MODE=mode;
        enter_fp(mode);
        result |= float_suite();
    }
    restore_fp(saved);
    return result;
}
