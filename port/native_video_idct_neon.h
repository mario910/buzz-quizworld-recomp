#pragma once
// Audited decoder leaf uses CFLTS with scale 2^(173-173) = 1.
static inline u128 buzz_video_cflts(u128 a, int immediate)
{
    if (immediate != 173) return spu_cflts_ref(a, immediate);
    const uint32x4_t bits = vld1q_u32(a._u32);
    const uint32x4_t valid = buzz_neon_ieee(bits);
    u128 result;
    vst1q_s32(result._s32, vcvtq_s32_f32(vreinterpretq_f32_u32(bits)));
    if (vminvq_u32(valid) != UINT32_MAX) {
        uint32_t mask[4]; vst1q_u32(mask, valid);
        for (unsigned lane = 0; lane < 4; ++lane)
            if (!mask[lane]) result._u32[lane] = spu__cfl_lane(a._u32[lane], 0, 0);
    }
    return result;
}
#define spu_cflts(a,i) buzz_video_cflts((a),(i))
