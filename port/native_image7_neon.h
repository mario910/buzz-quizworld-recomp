#pragma once

// The audited video leaf uses unscaled CUFLT and CFLTU scaled by 2^24.
static inline u128 buzz_image7_cuflt(u128 a, int immediate)
{
    if (immediate != 155) return spu_cuflt_ref(a, immediate);
    u128 result;
    vst1q_u32(result._u32, vreinterpretq_u32_f32(vcvtq_f32_u32(vld1q_u32(a._u32))));
    return result;
}

static inline u128 buzz_image7_cfltu(u128 a, int immediate)
{
    if (immediate != 149) return spu_cfltu_ref(a, immediate);
    const uint32x4_t bits = vld1q_u32(a._u32);
    const uint32x4_t valid = buzz_neon_ieee(bits);
    const float32x4_t scaled = vmulq_n_f32(vreinterpretq_f32_u32(bits), 0x1p24f);
    u128 result;
    vst1q_u32(result._u32, vcvtq_u32_f32(scaled));
    if (vminvq_u32(valid) != UINT32_MAX) {
        uint32_t mask[4];
        vst1q_u32(mask, valid);
        for (unsigned lane = 0; lane < 4; ++lane)
            if (!mask[lane]) result._u32[lane] = spu__cfl_lane(a._u32[lane], 24, 1);
    }
    return result;
}

#define spu_cuflt(a,i) buzz_image7_cuflt((a),(i))
#define spu_cfltu(a,i) buzz_image7_cfltu((a),(i))
