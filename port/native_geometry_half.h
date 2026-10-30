/* 4AB0 float4 -> halfword stream: AVX2 prefix, exact final pipeline iteration. */
#pragma once
#include "native_stagef_avx2.h"

static inline u128 buzz_half_pack(v8 input)
{
    // SPU conversion saturates, truncates and flushes small magnitudes to +0.
    const v8 magnitude = _mm256_and_si256(_mm256_srli_epi32(input, 16), v8_set(0x7fff));
    v8 bits = _mm256_and_si256(_mm256_srli_epi32(_mm256_sub_epi32(input, v8_set(0x38000000)), 13), v8_set(0x7fff));
    bits = _mm256_blendv_epi8(bits, v8_set(0x7bff), _mm256_cmpgt_epi32(magnitude, v8_set(0x477f)));
    bits = _mm256_or_si256(bits, _mm256_and_si256(_mm256_srli_epi32(input, 16), v8_set(0x8000)));
    bits = _mm256_andnot_si256(_mm256_cmpgt_epi32(v8_set(0x3800), magnitude), bits);
    __m128i packed = _mm_packus_epi32(_mm256_castsi256_si128(bits), _mm256_extracti128_si256(bits, 1));
    packed = _mm_shuffle_epi8(packed, _mm_setr_epi8(2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13));
    u128 result; memcpy(&result, &packed, 16); return result;
}

static inline int buzz_half_native(spu_context* ctx, void (*tail)(spu_context*))
{
    if (ctx->ls != ctx->ls_store || buzz_geometry_debug || g_spu_ls_probe > 0 ||
        g_spu_ls_watch_n > 0 || g_spu_smc_watch > 0 ||
        (ctx->int_enable && (ctx->event_status & ctx->event_mask)) ||
        !buzz_half_constants_match(ctx)) return 0;
    const unsigned cursor = ctx->gpr[22]._u32[0], limit = ctx->gpr[32]._u32[0];
    const unsigned stride = ctx->gpr[29]._u32[0], base = ctx->gpr[19]._u32[0];
    if (!stride || (stride & 15) || cursor > limit || cursor < 0xe680 ||
        limit >= SPU_LS_SIZE || base >= SPU_LS_SIZE) return 0;
    const unsigned iterations = (limit - cursor)/stride + 2;
    if (iterations > 4096) return 0;
    const unsigned first_source = cursor & ~15u;
    const uint64_t source_end = first_source + (iterations - 1ull)*stride + 16;
    const unsigned offsets[4] = {0, ctx->gpr[28]._u32[0], ctx->gpr[27]._u32[0], ctx->gpr[26]._u32[0]};
    if ((offsets[1] | offsets[2] | offsets[3]) & 15) return 0;
    for (unsigned j = 0; j < 4; ++j) if (source_end + offsets[j] > SPU_LS_SIZE) return 0;
    u128 routes = ctx->gpr[24], pointer = ctx->gpr[22];
    // Check every routed quadword, including the final aliased sentinel.
    // All rejection checks run before the first native write.
    for (unsigned n = 0; n < iterations; ++n) {
        const u128 chosen = spu_selb(routes, ctx->gpr[30], spu_cgt(pointer, ctx->gpr[32]));
        for (unsigned j = 0; j < 4; ++j) {
            uint64_t address = (uint64_t)base + chosen._u32[j];
            if (address + 16 > SPU_LS_SIZE || address < 0xe680) return 0;
            address &= ~15ull;
            for (unsigned k = 0; k < 4; ++k)
                if (address < source_end + offsets[k] && first_source + offsets[k] < address + 16) return 0;
        }
        pointer = spu_a(pointer, ctx->gpr[29]); routes = spu_a(routes, ctx->gpr[31]);
    }
    const BuzzGeometryShuffle mask = buzz_geometry_shuffle_prepare(ctx->gpr[18]);
    for (unsigned n = 0; n < iterations - 1; ++n) {
        const u128 chosen = spu_selb(ctx->gpr[24], ctx->gpr[30], spu_cgt(ctx->gpr[22], ctx->gpr[32]));
        const unsigned registers[4] = {16, 21, 17, 20}; u128 old[4];
        for (unsigned j = 0; j < 4; ++j) old[j] = buzz_geometry_ls_read(ctx->ls, base + chosen._u32[j]);
        for (unsigned j = 0; j < 4; ++j)
            buzz_geometry_ls_write(ctx->ls, base + chosen._u32[j], buzz_geometry_shuffle_apply(ctx->gpr[registers[j]], old[j], mask));
        const unsigned input = ctx->gpr[22]._u32[0];
        ctx->gpr[16] = buzz_half_pack(v8_ld2(ctx->ls, input, input + offsets[1]));
        ctx->gpr[17] = buzz_half_pack(v8_ld2(ctx->ls, input + offsets[2], input + offsets[3]));
        ctx->gpr[21] = spu_rotqbyi(ctx->gpr[16], 8); ctx->gpr[20] = spu_rotqbyi(ctx->gpr[17], 8);
        ctx->gpr[22] = spu_a(ctx->gpr[22], ctx->gpr[29]); ctx->gpr[24] = spu_a(ctx->gpr[24], ctx->gpr[31]);
    }
    tail(ctx); return 1;
}
