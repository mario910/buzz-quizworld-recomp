#pragma once
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
/* Image 41's repeated BE word fill. The last lifted iteration reconstructs
 * quadword padding, all temporary registers and the queued continuation. */
static int buzz_fill_native(spu_context* ctx, void (*tail)(spu_context*))
{
    if (ctx->ls != ctx->ls_store || buzz_geometry_debug || g_spu_ls_probe > 0 ||
        g_spu_ls_watch_n > 0 || g_spu_smc_watch > 0 ||
        (ctx->int_enable && (ctx->event_status & ctx->event_mask))) return 0;
    const unsigned pointer = ctx->gpr[5]._u32[0];
    const unsigned remaining = ctx->gpr[7]._u32[0] - ctx->gpr[6]._u32[0];
    if (remaining < 4 || (pointer & 3) || pointer < 0xe680 ||
        (uint64_t)pointer + (uint64_t)remaining * 4 > SPU_LS_SIZE) return 0;
    const unsigned skipped = remaining - 1, bytes = skipped * 4;
    const uint32_t value = SPU_BSWAP32(ctx->gpr[9]._u32[0]);
    unsigned offset = 0;
#if defined(__aarch64__)
    const uint8x16_t repeated = vreinterpretq_u8_u32(vdupq_n_u32(value));
    for (; offset + 16 <= bytes; offset += 16)
        vst1q_u8(ctx->ls + pointer + offset, repeated);
#else
    const __m256i repeated = _mm256_set1_epi32((int)value);
    for (; offset + 32 <= bytes; offset += 32)
        _mm256_storeu_si256((__m256i*)(ctx->ls + pointer + offset), repeated);
#endif
    for (; offset < bytes; offset += 4)
        memcpy(ctx->ls + pointer + offset, &value, 4);
    for (unsigned lane = 0; lane < 4; ++lane) {
        ctx->gpr[5]._u32[lane] += bytes;
        ctx->gpr[6]._u32[lane] += skipped;
    }
    tail(ctx);
    return 1;
}
