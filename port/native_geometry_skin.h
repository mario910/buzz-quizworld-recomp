/* B880/B934/B940 blended skin matrices and position transform. Process pairs
 * with AVX2, then retain the original final pair for exact pipeline state. */
#pragma once
#include "native_stagef_avx2.h"

static inline u128 buzz_skin_broadcast(uint32_t word)
{
    u128 result;
    for (unsigned i = 0; i < 4; ++i) result._u32[i] = word;
    return result;
}

static inline int buzz_skin_native(spu_context* ctx, void (*setup)(spu_context*), void (*tail)(spu_context*))
{
    if (ctx->ls != ctx->ls_store || buzz_geometry_debug || g_spu_ls_probe > 0 ||
        g_spu_ls_watch_n > 0 || g_spu_smc_watch > 0 ||
        (ctx->int_enable && (ctx->event_status & ctx->event_mask))) return 0;
    const uint32_t count = ctx->gpr[84]._u32[0], position = ctx->gpr[80]._u32[0];
    const uint32_t weights = ctx->gpr[85]._u32[0], destination = ctx->gpr[96]._u32[0];
    const uint32_t palette = ctx->gpr[54]._u32[0];
    if (count < 4 || (count & 1) || ((position | weights | destination | palette) & 15) ||
        position < 0xe680 || weights < 0xe680 || destination < 0xe680 || palette < 0xe680 ||
        position + (count + 2ull)*16 > SPU_LS_SIZE || weights + (count + 2ull)*8 > SPU_LS_SIZE ||
        destination + count*48ull > SPU_LS_SIZE || ctx->gpr[53]._u32[0] != palette + 16 ||
        ctx->gpr[52]._u32[0] != palette + 32 || !buzz_skin_constants_match(ctx)) return 0;
    uint8_t* memory = ctx->ls_store;
    __m128i maximum = _mm_setzero_si128();
    for (unsigned i = 0; i < count; i += 2)
        maximum = _mm_max_epu8(maximum, _mm_and_si128(
            _mm_loadu_si128((const __m128i*)(memory + weights + 8*i)), _mm_set1_epi16((short)0xff00)));
    maximum = _mm_max_epu8(maximum, _mm_srli_si128(maximum, 8));
    maximum = _mm_max_epu8(maximum, _mm_srli_si128(maximum, 4));
    maximum = _mm_max_epu8(maximum, _mm_srli_si128(maximum, 2));
    maximum = _mm_max_epu8(maximum, _mm_srli_si128(maximum, 1));
    const unsigned max_index = (unsigned)_mm_cvtsi128_si32(maximum) & 255;
    const uint64_t palette_end = palette + (max_index + 1ull)*48;
    const uint64_t position_end = position + (count + 2ull)*16;
    const uint64_t weights_end = weights + (count + 2ull)*8;
    const uint64_t destination_end = destination + count*48ull;
    if (palette_end > SPU_LS_SIZE) return 0;
#define BUZZ_SKIN_OVERLAP(a,b,c,d) ((a) < (d) && (c) < (b))
    if (BUZZ_SKIN_OVERLAP(position, position_end, weights, weights_end) ||
        BUZZ_SKIN_OVERLAP(position, position_end, palette, palette_end) ||
        BUZZ_SKIN_OVERLAP(destination, destination_end, position, position_end) ||
        BUZZ_SKIN_OVERLAP(destination, destination_end, weights, weights_end) ||
        BUZZ_SKIN_OVERLAP(destination, destination_end, palette, palette_end)) return 0;
#undef BUZZ_SKIN_OVERLAP
    const v8 xyz = _mm256_setr_epi32(-1, -1, -1, 0, -1, -1, -1, 0);
    for (unsigned vertex = 0; vertex < count - 2; vertex += 2) {
        const uint8_t* skin = memory + weights + vertex*8;
        const v8 quantized = _mm256_setr_epi32(skin[0], skin[2], skin[4], skin[6], skin[8], skin[10], skin[12], skin[14]);
        // Byte integers convert exactly; multiply with the SPU RTZ 1/255.
        const v8 coefficients = v8_fm(_mm256_castps_si256(_mm256_cvtepi32_ps(quantized)), v8_bcast(ctx->gpr[90]));
        v8 factors[4], raw[3], matrix[3];
        for (unsigned j = 0; j < 4; ++j)
            factors[j] = _mm256_permutevar8x32_epi32(coefficients, _mm256_setr_epi32(j, j, j, j, j+4, j+4, j+4, j+4));
        for (unsigned row = 0; row < 3; ++row) {
            raw[row] = v8_set(0);
            int started = 0;
            // Zero byte weights contribute canonical +0 in these SPU ops.
            // Keep the order of every nonzero contribution, including FMA.
            for (unsigned j = 0; j < 4; ++j) if (skin[2*j] || skin[8 + 2*j]) {
                const v8 values = v8_ld2(memory, palette + skin[2*j + 1]*48 + row*16,
                                                palette + skin[8 + 2*j + 1]*48 + row*16);
                raw[row] = started ? v8_fma(factors[j], values, raw[row]) : v8_fm(factors[j], values);
                started = 1;
            }
            matrix[row] = _mm256_and_si256(raw[row], xyz);
            if (!row) matrix[row] = _mm256_or_si256(matrix[row],
                _mm256_setr_epi32(0, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000));
            v8_st2(memory, destination + vertex*48 + row*16,
                           destination + (vertex + 1)*48 + row*16, matrix[row]);
        }
        v8 translation = _mm256_blend_epi32(_mm256_shuffle_epi32(raw[0], 255), _mm256_shuffle_epi32(raw[1], 255), 0x22);
        translation = _mm256_and_si256(_mm256_blend_epi32(translation, _mm256_shuffle_epi32(raw[2], 255), 0x44), xyz);
        const v8 point = v8_ld2(memory, position + vertex*16, position + (vertex + 1)*16);
        v8 output = v8_fma(point, matrix[0], translation);
        output = v8_fma(_mm256_shuffle_epi32(point, _MM_SHUFFLE(3,0,2,1)), matrix[1], output);
        output = v8_fma(_mm256_shuffle_epi32(point, _MM_SHUFFLE(3,1,0,2)), matrix[2], output);
        v8_st2(memory, position + vertex*16, position + (vertex + 1)*16, output);
    }
    const unsigned skipped = count - 2;
    const u128 saved_count = ctx->gpr[84], saved_destination = ctx->gpr[96];
    ctx->gpr[80] = spu_a(ctx->gpr[80], buzz_skin_broadcast(skipped*16));
    ctx->gpr[85] = spu_a(ctx->gpr[85], buzz_skin_broadcast(skipped*8));
    ctx->gpr[84] = spu_sf(buzz_skin_broadcast(skipped), ctx->gpr[84]);
    ctx->gpr[96] = spu_a(ctx->gpr[96], buzz_skin_broadcast(skipped*48));
    setup(ctx); tail(ctx);
    ctx->gpr[84] = saved_count; ctx->gpr[96] = saved_destination;
    return 1;
}
