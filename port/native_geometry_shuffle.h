#pragma once
/* Reuse selector decoding when a pure geometry kernel never changes its mask.
 * This handles ALL 256 selectors, including zero/FF/80 constants. No assumed
 * transpose mask, floating-point shortcut or change to register byte order. */
#if defined(__aarch64__)
#include <arm_neon.h>
typedef struct { uint8x16_t index, special; } BuzzGeometryShuffle;
static inline BuzzGeometryShuffle buzz_geometry_shuffle_prepare(u128 control)
{
    uint8x16_t c;
    memcpy(&c, &control, sizeof(c));
    const uint8x16_t constants = vcgeq_u8(c, vdupq_n_u8(0xc0));
    const uint8x16_t high = vcgeq_u8(c, vdupq_n_u8(0x80));
    const uint8x16_t top = vcgeq_u8(c, vdupq_n_u8(0xe0));
    const BuzzGeometryShuffle plan = {
        vorrq_u8(veorq_u8(vandq_u8(c, vdupq_n_u8(31)), vdupq_n_u8(3)), high),
        vbslq_u8(top, vdupq_n_u8(0x80), constants)
    };
    return plan;
}
static inline u128 buzz_geometry_shuffle_apply(u128 a, u128 b, BuzzGeometryShuffle plan)
{
    uint8x16x2_t table;
    memcpy(&table.val[0], &a, sizeof(a));
    memcpy(&table.val[1], &b, sizeof(b));
    const uint8x16_t result = vorrq_u8(vqtbl2q_u8(table, plan.index), plan.special);
    u128 out;
    memcpy(&out, &result, sizeof(out));
    return out;
}
#else
#include <immintrin.h>
typedef struct { __m128i a,b,special; } BuzzGeometryShuffle;
static inline BuzzGeometryShuffle buzz_geometry_shuffle_prepare(u128 control)
{
    __m128i c;memcpy(&c,&control,16);
    const __m128i index=_mm_xor_si128(_mm_and_si128(c,_mm_set1_epi8(15)),_mm_set1_epi8(3));
    const __m128i select_b=_mm_slli_epi16(_mm_and_si128(c,_mm_set1_epi8(16)),3);
    const __m128i high=_mm_and_si128(c,_mm_set1_epi8((char)0x80));
    BuzzGeometryShuffle p;
    p.a=_mm_or_si128(index,_mm_or_si128(high,select_b));
    p.b=_mm_or_si128(index,_mm_or_si128(high,_mm_xor_si128(select_b,_mm_set1_epi8((char)0x80))));
    const __m128i c0=_mm_cmpeq_epi8(_mm_and_si128(c,_mm_set1_epi8((char)0xc0)),_mm_set1_epi8((char)0xc0));
    const __m128i e0=_mm_cmpeq_epi8(_mm_and_si128(c,_mm_set1_epi8((char)0xe0)),_mm_set1_epi8((char)0xe0));
    p.special=_mm_blendv_epi8(c0,_mm_set1_epi8((char)0x80),e0);
    return p;
}
static inline u128 buzz_geometry_shuffle_apply(u128 a,u128 b,BuzzGeometryShuffle p)
{
    __m128i x,y;memcpy(&x,&a,16);memcpy(&y,&b,16);
    __m128i r=_mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(x,p.a),_mm_shuffle_epi8(y,p.b)),p.special);
    u128 out;memcpy(&out,&r,16);return out;
}
#endif
