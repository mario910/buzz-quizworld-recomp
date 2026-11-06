#pragma once
#if defined(__aarch64__)
#include "native_geometry_ls.h"
#define BUZZ_NEON_RTZ_NO_REMAP
#include "spu_neon_rtz.h"
#undef BUZZ_NEON_RTZ_NO_REMAP

static inline uint32_t buzz_compact_pack4(u128 input, u128 scale, u128 bias)
{
    const u128 value = buzz_neon_fused(input, scale, bias, 0, 0);
    const uint32x4_t bits = vld1q_u32(value._u32);
    const uint32x4_t ieee = buzz_neon_ieee(bits);
    const float32x4_t safe = vreinterpretq_f32_u32(vandq_u32(bits, ieee));
    const int32x4_t converted = vcvtq_s32_f32(vmulq_n_f32(safe, 2147483648.0f));
    u128 quantized;
    vst1q_s32(quantized._s32, converted);
    if (vminvq_u32(ieee) == 0)
        for (unsigned lane = 0; lane < 3; ++lane)
            if ((value._u32[lane] & 0x7f800000u) == 0x7f800000u)
                quantized._u32[lane] = spu__cfl_lane(value._u32[lane], 31, 0);
    return ((quantized._u32[0] >> 21) & 0x7ffu) |
           (((quantized._u32[1] >> 21) & 0x7ffu) << 11) |
           ((quantized._u32[2] >> 22) << 22);
}
#else
#include "native_stagef_avx2.h"
static __attribute__((noinline, cold)) v8 buzz_compact_cfl31_fix(v8 out, int valid, v8 a)
{
    uint32_t o[8], x[8];
    _mm256_storeu_si256((__m256i *)o, out);
    _mm256_storeu_si256((__m256i *)x, a);
    for (unsigned n = 0; n < 8; ++n)
        if (!(valid & (1 << n)))
            o[n] = spu__cfl_lane(x[n], 31, 0);
    return _mm256_loadu_si256((const __m256i *)o);
}
static inline v8 buzz_compact_cfl31(v8 a)
{
    v8 e = _mm256_and_si256(_mm256_srli_epi32(a, 23), v8_set(255));
    v8 good = _mm256_cmpgt_epi32(v8_set(255), e);
    __m256 value = _mm256_mul_ps(_mm256_castsi256_ps(a), _mm256_set1_ps(2147483648.0f));
    v8 result = _mm256_cvttps_epi32(value),
       over = _mm256_castps_si256(_mm256_cmp_ps(value, _mm256_set1_ps(2147483648.0f), _CMP_GE_OQ));
    result = _mm256_or_si256(_mm256_andnot_si256(over, result), _mm256_and_si256(over, v8_set(0x7fffffff)));
    int mask = _mm256_movemask_ps(_mm256_castsi256_ps(good));
    return mask == 255 ? result : buzz_compact_cfl31_fix(result, mask, a);
}
static inline u128 buzz_compact_pack(v8 input, v8 scale, v8 bias)
{
    v8 q = buzz_compact_cfl31(v8_fma(input, scale, bias));
    q = _mm256_srlv_epi32(q, _mm256_setr_epi32(21, 21, 22, 32, 21, 21, 22, 32));
    q = _mm256_and_si256(q, _mm256_setr_epi32(0x7ff, 0x7ff, 0x3ff, 0, 0x7ff, 0x7ff, 0x3ff, 0));
    q = _mm256_sllv_epi32(q, _mm256_setr_epi32(0, 11, 22, 0, 0, 11, 22, 0));
    q = _mm256_or_si256(q, _mm256_shuffle_epi32(q, _MM_SHUFFLE(0, 3, 2, 1)));
    q = _mm256_or_si256(q, _mm256_shuffle_epi32(q, _MM_SHUFFLE(1, 0, 3, 2)));
    q = _mm256_permutevar8x32_epi32(q, _mm256_setr_epi32(0, 4, 0, 4, 0, 4, 0, 4));
    __m128i lo = _mm256_castsi256_si128(q);
    u128 result;
    memcpy(&result, &lo, 16);
    return result;
}
#endif

/* 5008 -> 51E0 -> 51C8: bounded word-aligned 11/11/10-bit output.
 * The lifted final column restores all registers, padding and continuation. */

static int buzz_compact_native(spu_context *ctx, void (*last)(spu_context *))
{
    if (ctx->ls != ctx->ls_store || buzz_geometry_debug || g_spu_ls_probe > 0 || g_spu_ls_watch_n > 0 ||
        g_spu_smc_watch > 0 || (ctx->int_enable && (ctx->event_status & ctx->event_mask)) ||
        !buzz_compact_constants_match(ctx))
        return 0;
    const unsigned column = ctx->gpr[44]._u32[0], columns = ctx->gpr[60]._u32[0];
    const unsigned base = ctx->gpr[19]._u32[0], stride = ctx->gpr[55]._u32[0];
    const unsigned attribute = ctx->gpr[53]._u32[0], in_step = ctx->gpr[29]._u32[0];
    const unsigned out_step = ctx->gpr[31]._u32[0], source = ctx->gpr[52]._u32[0];
    const unsigned limit = ctx->gpr[32]._u32[0], sentinel = ctx->gpr[30]._u32[0];
    if (columns > 16 || column >= columns || columns - column < 2 || ctx->gpr[47]._u32[0] != column * 16 ||
        (base & 15) || base < 0xe680 || stride < 4 || stride > 64 || (stride & 3) || attribute > 64 ||
        (attribute & 3) || !in_step || (in_step & 15) || !out_step || (out_step & 15) || source < 0xe680 ||
        limit >= SPU_LS_SIZE)
        return 0;
    const uint64_t first = (uint64_t)source + column * 16;
    const uint64_t final = (uint64_t)source + (columns - 1) * 16;
    if (first > limit || final > limit)
        return 0;
    const unsigned groups = (unsigned)((limit - first) / in_step + 1);
    if (groups > 4096 || groups != (limit - final) / in_step + 1)
        return 0;
    const unsigned offsets[4] = {0, ctx->gpr[28]._u32[0], ctx->gpr[27]._u32[0], ctx->gpr[26]._u32[0]};
    unsigned max_offset = 0;
    for (unsigned j = 0; j < 4; ++j)
    {
        if ((offsets[j] & 15) || ctx->gpr[55]._u32[j] != stride || ctx->gpr[53]._u32[j] != attribute ||
            ctx->gpr[29]._u32[j] != in_step || ctx->gpr[31]._u32[j] != out_step ||
            ctx->gpr[32]._u32[j] != limit || ctx->gpr[30]._u32[j] != sentinel ||
            ctx->gpr[52]._u32[j] + (columns - 1) * 16ull + groups * (uint64_t)in_step >= SPU_LS_SIZE)
            return 0;
        if (offsets[j] > max_offset)
            max_offset = offsets[j];
    }
    // Include the final speculative prefetch, even though its temporary
    // registers are reconstructed by the original final column.
    const uint64_t input_first = first & ~15ull;
    const uint64_t input_end = final + groups * (uint64_t)in_step + max_offset + 16;
    const uint64_t output_first = ((uint64_t)base + stride * column + attribute) & ~15ull;
    const uint64_t output_end =
        (uint64_t)base + stride * (columns - 1 + 12) + attribute + (groups - 1ull) * out_step + 16;
    const uint64_t sent_first = ((uint64_t)base + sentinel) & ~15ull, sent_end = sent_first + 16;
    if (input_end > SPU_LS_SIZE || output_end > SPU_LS_SIZE || sent_first < 0xe680 ||
        sent_end > SPU_LS_SIZE || (input_first < output_end && output_first < input_end) ||
        (input_first < sent_end && sent_first < input_end) ||
        (output_first < sent_end && sent_first < output_end))
        return 0;
#if defined(__aarch64__)
    const u128 scale = ctx->gpr[35], bias = ctx->gpr[34];
#else
    const v8 scale = v8_bcast(ctx->gpr[35]), bias = v8_bcast(ctx->gpr[34]);
#endif
    for (unsigned col = column; col < columns - 1; ++col)
    {
        const unsigned within = (stride * col + attribute) & 15;
        for (unsigned group = 0; group < groups; ++group)
        {
            const unsigned p = source + col * 16 + group * in_step;
#if defined(__aarch64__)
            uint32_t packed[4];
            for (unsigned lane = 0; lane < 4; ++lane)
                packed[lane] = buzz_compact_pack4(buzz_geometry_ls_read(ctx->ls, p + offsets[lane]), scale, bias);
#else
            const u128 ab = buzz_compact_pack(v8_ld2(ctx->ls, p, p + offsets[1]), scale, bias);
            const u128 cd = buzz_compact_pack(v8_ld2(ctx->ls, p + offsets[2], p + offsets[3]), scale, bias);
            const uint32_t packed[4] = {ab._u32[0], ab._u32[1], cd._u32[0], cd._u32[1]};
#endif
            for (unsigned lane = 0; lane < 4; ++lane)
            {
                const unsigned cursor = ctx->gpr[52]._u32[lane] + col * 16 + (group + 1) * in_step;
                const unsigned route =
                    cursor > limit ? sentinel : stride * (col + lane * 4) + attribute + group * out_step;
                const unsigned output = ((base + route) & ~15u) + within;
                const uint32_t value = SPU_BSWAP32(packed[lane]);
                memcpy(ctx->ls + output, &value, 4);
            }
        }
    }
    for (unsigned j = 0; j < 4; ++j)
    {
        ctx->gpr[44]._u32[j] += columns - column - 1;
        ctx->gpr[47]._u32[j] += (columns - column - 1) * 16;
    }
    last(ctx);
    return 1;
}
