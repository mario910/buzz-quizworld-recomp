#pragma once
#include "spu_helpers.h"

#if defined(__aarch64__) && !defined(SPU_SCALAR_HELPERS) && !defined(SPU_SCALAR_INT)
// Host lane order is already shared by the destination and control. Adjust
// source indices instead of reversing A, B, C and the result (four REV32s).
static inline u128 buzz_spu_shufb(u128 a, u128 b, u128 c) {
    uint8x16x2_t table;
    table.val[0] = vld1q_u8(a._u8);
    table.val[1] = vld1q_u8(b._u8);
    const uint8x16_t control = vld1q_u8(c._u8);
    const uint8x16_t indices = veorq_u8(vandq_u8(control, vdupq_n_u8(31)), vdupq_n_u8(3));
    const uint8x16_t value = vqtbl2q_u8(table, indices);
    const uint8x16_t m80 = vtstq_u8(control, vdupq_n_u8(0x80));
    const uint8x16_t mc0 = vceqq_u8(vandq_u8(control, vdupq_n_u8(0xc0)), vdupq_n_u8(0xc0));
    const uint8x16_t me0 = vceqq_u8(vandq_u8(control, vdupq_n_u8(0xe0)), vdupq_n_u8(0xe0));
    const uint8x16_t special = vbslq_u8(me0, vdupq_n_u8(0x80), mc0);
    u128 result;
    vst1q_u8(result._u8, vbslq_u8(m80, special, value));
    return result;
}
#define spu_shufb buzz_spu_shufb
#endif

#if defined(__aarch64__) && !defined(SPU_SCALAR_HELPERS) && !defined(SPU_SCALAR_INT)
static inline __attribute__((always_inline)) u128 buzz_spu_shlqbyi(u128 a, int shift) {
    const uint8x16_t value = vld1q_u8(a._u8), zero = vdupq_n_u8(0);
    uint8x16_t out;
    switch (shift & 31) {
    case 0: return a;
    case 4: out = vextq_u8(value, zero, 4); break;
    case 8: out = vextq_u8(value, zero, 8); break;
    case 12: out = vextq_u8(value, zero, 12); break;
    default: return spu_shlqbyi(a, shift);
    }
    u128 result; vst1q_u8(result._u8, out); return result;
}
static inline __attribute__((always_inline)) u128 buzz_spu_rotqbyi(u128 a, int shift) {
    const uint8x16_t value = vld1q_u8(a._u8);
    uint8x16_t out;
    switch (shift & 15) {
    case 0: return a;
    case 4: out = vextq_u8(value, value, 4); break;
    case 8: out = vextq_u8(value, value, 8); break;
    case 12: out = vextq_u8(value, value, 12); break;
    default: return spu_rotqbyi(a, shift);
    }
    u128 result; vst1q_u8(result._u8, out); return result;
}
#define spu_shlqbyi buzz_spu_shlqbyi
#define spu_rotqbyi buzz_spu_rotqbyi
#elif defined(__SSSE3__) && !defined(SPU_SCALAR_HELPERS) && !defined(SPU_SCALAR_INT)
// Word-aligned byte operations need no endian shuffle on either host ISA.
static inline __attribute__((always_inline)) u128 buzz_spu_shlqbyi(u128 a, int shift) {
    __m128i value, out; memcpy(&value, &a, 16);
    switch (shift & 31) {
    case 0: return a;
    case 4: out = _mm_srli_si128(value, 4); break;
    case 8: out = _mm_srli_si128(value, 8); break;
    case 12: out = _mm_srli_si128(value, 12); break;
    default: return spu_shlqbyi(a, shift);
    }
    u128 result; memcpy(&result, &out, 16); return result;
}
static inline __attribute__((always_inline)) u128 buzz_spu_rotqbyi(u128 a, int shift) {
    __m128i value, out; memcpy(&value, &a, 16);
    switch (shift & 15) {
    case 0: return a;
    case 4: out = _mm_alignr_epi8(value, value, 4); break;
    case 8: out = _mm_alignr_epi8(value, value, 8); break;
    case 12: out = _mm_alignr_epi8(value, value, 12); break;
    default: return spu_rotqbyi(a, shift);
    }
    u128 result; memcpy(&result, &out, 16); return result;
}
#define spu_shlqbyi buzz_spu_shlqbyi
#define spu_rotqbyi buzz_spu_rotqbyi
#endif
