#pragma once
#if defined(__aarch64__)
#include "../port/spu_arm_fp_state.h"
inline void buzz_test_enter_rtz() { buzz_arm_fp_enter_rtz(buzz_arm_fp_save()); }
#else
#include <immintrin.h>
inline void buzz_test_enter_rtz() { _mm_setcsr((_mm_getcsr() & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO | 0x8040); }
#endif
