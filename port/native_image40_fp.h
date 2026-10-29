#ifndef BUZZ_NATIVE_IMAGE40_FP_H
#define BUZZ_NATIVE_IMAGE40_FP_H
/* Include AFTER the original helpers, in an audited single-precision TU. The owner sets RTZ,
 * FTZ and DAZ only in modes 1/2, and restores them even after a guest HALT. */
#include "spu_native_rtz.h"
#include "spu_native_extra_fp.h"
#include <stdio.h>
#include <stdlib.h>
#ifndef BUZZ_NATIVE_FP_MODE
#define BUZZ_NATIVE_FP_MODE spu_native_extra_fp_mode
#endif
#ifndef BUZZ_NATIVE_FP_LABEL
#define BUZZ_NATIVE_FP_LABEL "image40-fp"
#endif

static unsigned image40_fp_checked[6];
static __attribute__((noinline, cold)) void image40_fp_check(
    unsigned op, u128 a, u128 b, u128 c, u128 actual)
{
    static const char* const names[] = {"fa", "fs", "fm", "fma", "fms", "fnms"};
    const unsigned limit = 65536;
    if (__atomic_load_n(&image40_fp_checked[op], __ATOMIC_RELAXED) >= limit) return;
    const unsigned sample = __atomic_fetch_add(&image40_fp_checked[op], 1, __ATOMIC_RELAXED);
    if (sample >= limit) return;
    /* Integer reference: independent of the active host rounding mode. */
    u128 expected;
    switch (op) {
    case 0: expected = spu_fa_ref(a,b); break;
    case 1: expected = spu_fs_ref(a,b); break;
    case 2: expected = spu_fm_ref(a,b); break;
    case 3: expected = spu_fma_ref(a,b,c); break;
    case 4: expected = spu_fms_ref(a,b,c); break;
    default: expected = spu_fnms_ref(a,b,c); break;
    }
    for (int lane=0; lane<4; ++lane) if (actual._u32[lane] != expected._u32[lane]) {
        fprintf(stderr, "[" BUZZ_NATIVE_FP_LABEL "] MISMATCH %s sample=%u lane=%d a=%08X b=%08X c=%08X got=%08X expected=%08X\n",
                names[op], sample, lane, a._u32[lane], b._u32[lane], c._u32[lane],
                actual._u32[lane], expected._u32[lane]);
        abort();
    }
    if (sample+1 == limit)
        fprintf(stderr, "[" BUZZ_NATIVE_FP_LABEL "] %s checked %u vectors (%u lanes); no mismatch in this sample\n",
                names[op], limit, 4*limit);
}
#define IMAGE40_BINARY(name, op, native) \
static inline __attribute__((always_inline)) u128 image40_##name(u128 a,u128 b) { \
    if (!BUZZ_NATIVE_FP_MODE) return spu_##name(a,b); \
    u128 out = (native); \
    if (BUZZ_NATIVE_FP_MODE == 2) image40_fp_check(op,a,b,a,out); \
    return out; \
}
#define IMAGE40_TERNARY(name, op, native) \
static inline __attribute__((always_inline)) u128 image40_##name(u128 a,u128 b,u128 c) { \
    if (!BUZZ_NATIVE_FP_MODE) return spu_##name(a,b,c); \
    u128 out = (native); \
    if (BUZZ_NATIVE_FP_MODE == 2) image40_fp_check(op,a,b,c,out); \
    return out; \
}
IMAGE40_BINARY(fa, 0, spu_native_rtz_fa(a,b,0))
IMAGE40_BINARY(fs, 1, spu_native_rtz_fa(a,b,0x80000000u))
IMAGE40_BINARY(fm, 2, spu_native_rtz_fm(a,b))
IMAGE40_TERNARY(fma, 3, spu_native_rtz_fma(a,b,c,0,0))
IMAGE40_TERNARY(fms, 4, spu_native_rtz_fma(a,b,c,0,0x80000000u))
IMAGE40_TERNARY(fnms, 5, spu_native_rtz_fma(a,b,c,0x80000000u,0))
#undef IMAGE40_BINARY
#undef IMAGE40_TERNARY
#define spu_fa image40_fa
#define spu_fs image40_fs
#define spu_fm image40_fm
#define spu_fma image40_fma
#define spu_fms image40_fms
#define spu_fnms image40_fnms
#endif
