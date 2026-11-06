#pragma once
#include <cstdint>
#if defined(__aarch64__)
#include "spu_arm_fp_state.h"
#else
#include <immintrin.h>
#endif

inline void buzz_spu_scoped_rtz(spu_context* ctx, void (*native)(spu_context*))
{
#if defined(__aarch64__)
    const auto saved = buzz_arm_fp_save();
    buzz_arm_fp_enter_rtz(saved);
    native(ctx);
    buzz_arm_fp_restore(saved);
#else
    const unsigned saved = _mm_getcsr();
    _mm_setcsr((saved & ~0x6000u) | 0x6000u | 0x8040u | 0x1f80u);
    native(ctx);
    _mm_setcsr(saved);
#endif
}
