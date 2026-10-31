#pragma once
#include "native_stagef_avx2.h"
/* Raw BE float3 -> float4, with exact 1.0 W and the pending first group. */
static inline v8 buzz_float3_load(const uint8_t* ls, unsigned a, unsigned b)
{
    return v8_bswap(_mm256_set_m128i(_mm_loadu_si128((const __m128i*)(ls + b)),
                                    _mm_loadu_si128((const __m128i*)(ls + a))));
}
static int buzz_float3_native(spu_context* ctx, void (*tail)(spu_context*))
{
    if (ctx->ls != ctx->ls_store || buzz_geometry_debug || g_spu_ls_probe > 0 ||
        g_spu_ls_watch_n > 0 || g_spu_smc_watch > 0 ||
        (ctx->int_enable && (ctx->event_status & ctx->event_mask)) ||
        !buzz_float3_constants_match(ctx)) return 0;
    const int32_t offset = ctx->gpr[9]._s32[0];
    const unsigned source = ctx->gpr[14]._u32[0];
    if (offset >= 0 || (offset & 63) || (source & 15) || source < 0xe680) return 0;
    const unsigned groups = (unsigned)(-(int64_t)offset)/64;
    if (groups < 4 || (uint64_t)source + groups*48ull > SPU_LS_SIZE) return 0;
    const unsigned bases[4] = {8, 13, 12, 11};
    unsigned destinations[4];
    for (unsigned j = 0; j < 4; ++j) {
        const int64_t start = (int64_t)ctx->gpr[bases[j]]._u32[0] + offset;
        const int64_t end = start + (int64_t)(groups - 1)*64 + 16;
        if (start < 0xe680 || (start & 15) || end > SPU_LS_SIZE ||
            (start < (int64_t)(source + groups*48ull) && source < end)) return 0;
        destinations[j] = (unsigned)start;
    }
    __m128i a, b, c;
    memcpy(&a, &ctx->gpr[20], 16); memcpy(&b, &ctx->gpr[16], 16); memcpy(&c, &ctx->gpr[10], 16);
    const __m128i one = _mm_set1_epi32(0x3f800000);
    const __m128i first[4] = {
        _mm_blend_epi32(a, one, 8), _mm_blend_epi32(_mm_alignr_epi8(b, a, 12), one, 8),
        _mm_blend_epi32(_mm_alignr_epi8(c, b, 8), one, 8), _mm_blend_epi32(_mm_srli_si128(c, 4), one, 8)};
    for (unsigned j = 0; j < 4; ++j) {
        u128 value; memcpy(&value, &first[j], 16);
        buzz_geometry_ls_write(ctx->ls, destinations[j], value);
    }
    const v8 w = v8_set(0x3f800000);
    for (unsigned group = 1; group < groups - 1; ++group) {
        const unsigned p = source + (group - 1)*48;
        const v8 ab = _mm256_blend_epi32(buzz_float3_load(ctx->ls, p, p + 12), w, 0x88);
        const v8 cd = _mm256_blend_epi32(buzz_float3_load(ctx->ls, p + 24, p + 36), w, 0x88);
        v8_st2(ctx->ls, destinations[0] + group*64, destinations[1] + group*64, ab);
        v8_st2(ctx->ls, destinations[2] + group*64, destinations[3] + group*64, cd);
    }
    const unsigned pending = source + (groups - 2)*48;
    ctx->gpr[20] = buzz_geometry_ls_read(ctx->ls, pending);
    ctx->gpr[16] = buzz_geometry_ls_read(ctx->ls, pending + 16);
    ctx->gpr[10] = buzz_geometry_ls_read(ctx->ls, pending + 32);
    for (unsigned j = 0; j < 4; ++j) {
        ctx->gpr[9]._u32[j] += (groups - 1)*64;
        ctx->gpr[14]._u32[j] += (groups - 1)*48;
    }
    tail(ctx);
    return 1;
}
