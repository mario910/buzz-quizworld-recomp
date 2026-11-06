#pragma once

// Scoped single-precision arithmetic for the reviewed SPU leaves.
// Ordinary lanes run under FPCR RZ+FZ. SPU extended exponents and IEEE
// saturation are repaired with the same exact lane helpers as the lift.
#include <arm_neon.h>

static inline uint32x4_t buzz_neon_magnitude(uint32x4_t value)
{
    return vandq_u32(value, vdupq_n_u32(0x7fffffffu));
}

// Keep exceptional SPU exponents out of the hot instruction expansion. The
// ordinary path can then inline without duplicating the scalar repair tree.
static __attribute__((noinline, cold)) u128 buzz_neon_repair(u128 result, uint32x4_t valid,
                                                           u128 a, u128 b, u128 c, unsigned operation)
{
    uint32_t mask[4];
    vst1q_u32(mask, valid);
    for (unsigned lane = 0; lane < 4; ++lane) {
        if (mask[lane]) continue;
        const uint32_t x = a._u32[lane], y = b._u32[lane], z = c._u32[lane];
        result._u32[lane] = operation == 0 ? spu__fa_lane(x, y) :
                            operation == 1 ? spu__fm_lane(x, y) :
                                             spu__fma_lane(x, y, z, 0, 0);
    }
    return result;
}

static inline __attribute__((always_inline)) u128 buzz_neon_finish(float32x4_t value, uint32x4_t operands,
                                    u128 a, u128 b, u128 c, unsigned operation)
{
    uint32x4_t bits = vreinterpretq_u32_f32(value);
    const uint32x4_t magnitude = vandq_u32(bits, vdupq_n_u32(0x7fffffffu));
    // One classification for operands and result replaces separate masks for
    // every input. Max-finite operands conservatively use the exact repair.
    const uint32x4_t valid = vcltq_u32(vmaxq_u32(operands, magnitude), vdupq_n_u32(0x7f7fffffu));
    bits = vbicq_u32(bits, vceqq_u32(magnitude, vdupq_n_u32(0)));
    u128 result;
    vst1q_u32(result._u32, bits);
    if (__builtin_expect(vminvq_u32(valid) != UINT32_MAX, 0))
        return buzz_neon_repair(result, valid, a, b, c, operation);
    return result;
}

static inline uint32x4_t buzz_neon_ieee(uint32x4_t value)
{
    const uint32x4_t exponent = vandq_u32(value, vdupq_n_u32(0x7f800000u));
    return vcltq_u32(exponent, vdupq_n_u32(0x7f800000u));
}

static inline __attribute__((always_inline)) u128 buzz_neon_add(u128 a, u128 b, uint32_t negate)
{
    const uint32x4_t x = vld1q_u32(a._u32);
    const uint32x4_t y = veorq_u32(vld1q_u32(b._u32), vdupq_n_u32(negate));
    vst1q_u32(b._u32, y);
    return buzz_neon_finish(vaddq_f32(vreinterpretq_f32_u32(x), vreinterpretq_f32_u32(y)),
                            vmaxq_u32(buzz_neon_magnitude(x), buzz_neon_magnitude(y)), a, b, a, 0);
}

static inline __attribute__((always_inline)) u128 buzz_neon_mul(u128 a, u128 b)
{
    const uint32x4_t x = vld1q_u32(a._u32), y = vld1q_u32(b._u32);
    return buzz_neon_finish(vmulq_f32(vreinterpretq_f32_u32(x), vreinterpretq_f32_u32(y)),
                            vmaxq_u32(buzz_neon_magnitude(x), buzz_neon_magnitude(y)), a, b, a, 1);
}

static inline __attribute__((always_inline)) u128 buzz_neon_fused(u128 a, u128 b, u128 c, uint32_t negp, uint32_t negc)
{
    const uint32x4_t x = veorq_u32(vld1q_u32(a._u32), vdupq_n_u32(negp));
    const uint32x4_t y = vld1q_u32(b._u32);
    const uint32x4_t z = veorq_u32(vld1q_u32(c._u32), vdupq_n_u32(negc));
    vst1q_u32(a._u32, x);
    vst1q_u32(c._u32, z);
    const float32x4_t value = vfmaq_f32(vreinterpretq_f32_u32(z), vreinterpretq_f32_u32(x),
                                      vreinterpretq_f32_u32(y));
    return buzz_neon_finish(value, vmaxq_u32(vmaxq_u32(buzz_neon_magnitude(x), buzz_neon_magnitude(y)),
                                          buzz_neon_magnitude(z)), a, b, c, 2);
}

#ifndef BUZZ_NEON_RTZ_NO_REMAP
#define spu_fa(a,b) buzz_neon_add((a),(b),0)
#define spu_fs(a,b) buzz_neon_add((a),(b),0x80000000u)
#define spu_fm(a,b) buzz_neon_mul((a),(b))
#define spu_fma(a,b,c) buzz_neon_fused((a),(b),(c),0,0)
#define spu_fms(a,b,c) buzz_neon_fused((a),(b),(c),0,0x80000000u)
#define spu_fnms(a,b,c) buzz_neon_fused((a),(b),(c),0x80000000u,0)
#endif
