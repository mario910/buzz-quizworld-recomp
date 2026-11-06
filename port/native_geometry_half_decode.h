/* CAD8/CB78: decode remaining halfword columns, retaining the original last one. */
#pragma once
#if defined(__aarch64__)
#include "native_geometry_ls.h"

static inline u128 buzz_dehalf_neon(const uint8_t* memory, unsigned address)
{
    const uint8x8_t bytes = vrev16_u8(vld1_u8(memory + address));
    const uint16x4_t halves = vreinterpret_u16_u8(bytes);
    const uint32x4_t input = vorrq_u32(vshll_n_u16(halves, 16),
                                      vdupq_n_u32(memory[address & ~15u]*0x101u));
    const uint32x4_t shifted = vreinterpretq_u32_s32(vshrq_n_s32(vreinterpretq_s32_u32(input), 3));
    const uint32x4_t magnitude = vandq_u32(input, vdupq_n_u32(0x7fffffff));
    const uint32x4_t decoded = vaddq_u32(vbicq_u32(shifted, vdupq_n_u32(0x70000000)),
        vandq_u32(vcgtq_u32(magnitude, vdupq_n_u32(0)), vdupq_n_u32(0x38000000)));
    u128 result;
    memcpy(&result, &decoded, sizeof(result));
    return result;
}
#else
#include "native_stagef_avx2.h"

static inline v8 buzz_dehalf_pair(const uint8_t* memory, unsigned first, unsigned second)
{
    v8 input = _mm256_set_m128i(_mm_loadu_si128((const __m128i*)(memory + second)),
                              _mm_loadu_si128((const __m128i*)(memory + first)));
    const __m128i expand = _mm_setr_epi8(-128, -128, 1, 0, -128, -128, 3, 2,
                                      -128, -128, 5, 4, -128, -128, 7, 6);
    input = _mm256_shuffle_epi8(input, _mm256_broadcastsi128_si256(expand));
    // The original table shuffle repeats byte 0 into the low halfword.
    // Its contribution to the final low 13 bits must survive even for zero.
    const unsigned low_first = memory[first & ~15u]*0x101;
    const unsigned low_second = memory[second & ~15u]*0x101;
    input = _mm256_or_si256(input, _mm256_setr_epi32(low_first, low_first, low_first, low_first,
                                                  low_second, low_second, low_second, low_second));
    const v8 magnitude = _mm256_and_si256(input, v8_set(0x7fffffff));
    return _mm256_add_epi32(_mm256_andnot_si256(v8_set(0x70000000), _mm256_srai_epi32(input, 3)),
        _mm256_and_si256(_mm256_cmpgt_epi32(magnitude, v8_set(0)), v8_set(0x38000000)));
}
#endif

static inline int buzz_dehalf_native(spu_context* ctx, void (*last_column)(spu_context*))
{
    if (ctx->ls != ctx->ls_store || buzz_geometry_debug || g_spu_ls_probe > 0 ||
        g_spu_ls_watch_n > 0 || g_spu_smc_watch > 0 ||
        (ctx->int_enable && (ctx->event_status & ctx->event_mask)) ||
        !buzz_dehalf_constants_match(ctx)) return 0;
    const unsigned column = ctx->gpr[44]._u32[0], remaining = ctx->gpr[3]._u32[0];
    const unsigned stride = ctx->gpr[5]._u32[0], attribute = ctx->gpr[6]._u32[0];
    const unsigned output_step = ctx->gpr[19]._u32[0], input_step = ctx->gpr[20]._u32[0];
    const unsigned limit = ctx->gpr[17]._u32[0];
    if (remaining < 2 || remaining > 16 || column >= 16 || column + remaining > 16 ||
        !stride || stride > 32 || attribute > 32 || !output_step || (output_step & 15) ||
        (input_step & 15) || limit >= SPU_LS_SIZE) return 0;
    const unsigned first = ctx->gpr[4]._u32[0] + column*16;
    const unsigned final_first = ctx->gpr[4]._u32[0] + (column + remaining - 1)*16;
    if (first > limit || final_first > limit) return 0;
    const unsigned groups = (limit - first)/output_step + 2;
    if (groups > 4096 || groups != (limit - final_first)/output_step + 2) return 0;
    uint64_t input_first = SPU_LS_SIZE, input_end = 0, output_first = SPU_LS_SIZE, output_end = 0;
    for (unsigned j = 0; j < 4; ++j) {
        const unsigned base = ctx->gpr[7 + 2*j]._u32[0];
        if ((base & 15) || ctx->gpr[8 + 2*j]._u32[0] != base + 16 ||
            ctx->gpr[19]._u32[j] != output_step || ctx->gpr[20]._u32[j] != input_step ||
            ctx->gpr[17]._u32[j] != limit) return 0;
        uint64_t begin = (uint64_t)base + column*stride + attribute;
        uint64_t end = (uint64_t)base + (column + remaining - 1)*stride + attribute + groups*(uint64_t)input_step + 32;
        if (begin < input_first) input_first = begin & ~15ull;
        if (end > input_end) input_end = end;
        begin = ctx->gpr[4]._u32[j] + column*16ull;
        end = (uint64_t)ctx->gpr[4]._u32[j] + (column + remaining - 1)*16 + (groups - 1ull)*output_step + 16;
        if (begin < output_first) output_first = begin & ~15ull;
        if (end > output_end) output_end = end;
        begin = ctx->gpr[18]._u32[j]; end = begin + 16;
        if (begin < output_first) output_first = begin & ~15ull;
        if (end > output_end) output_end = end;
    }
    if (input_first < 0xe680 || output_first < 0xe680 || input_end > SPU_LS_SIZE ||
        output_end > SPU_LS_SIZE || (input_first < output_end && output_first < input_end)) return 0;
    for (unsigned col = column; col < column + remaining - 1; ++col) {
        for (unsigned group = 0; group < groups; ++group) {
            const unsigned index = col*stride + attribute + group*input_step;
#if defined(__aarch64__)
            u128 values[4];
            for (unsigned j = 0; j < 4; ++j)
                values[j] = buzz_dehalf_neon(ctx->ls, ctx->gpr[7 + 2*j]._u32[0] + index);
#else
            const v8 first_pair = buzz_dehalf_pair(ctx->ls, ctx->gpr[7]._u32[0] + index, ctx->gpr[9]._u32[0] + index);
            const v8 second_pair = buzz_dehalf_pair(ctx->ls, ctx->gpr[11]._u32[0] + index, ctx->gpr[13]._u32[0] + index);
            const __m128i a = _mm256_castsi256_si128(first_pair), b = _mm256_extracti128_si256(first_pair, 1);
            const __m128i c = _mm256_castsi256_si128(second_pair), d = _mm256_extracti128_si256(second_pair, 1);
            u128 values[4]; memcpy(&values[0], &a, 16); memcpy(&values[1], &b, 16);
            memcpy(&values[2], &c, 16); memcpy(&values[3], &d, 16);
#endif
            const unsigned order[4] = {1, 2, 0, 3};
            for (unsigned n = 0; n < 4; ++n) {
                const unsigned j = order[n];
                unsigned address = ctx->gpr[4]._u32[j] + col*16 + group*output_step;
                if (address > limit) address = ctx->gpr[18]._u32[j];
                buzz_geometry_ls_write(ctx->ls, address, values[j]);
            }
        }
    }
    for (unsigned j = 0; j < 4; ++j) {
        ctx->gpr[3]._u32[j] -= remaining - 1; ctx->gpr[44]._u32[j] += remaining - 1;
    }
    last_column(ctx); return 1;
}
