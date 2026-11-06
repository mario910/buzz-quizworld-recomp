/* Decode the remaining columns of 9148/92A0 as one pure kernel. Eight unit
 * vectors share SIMD arithmetic. The original last column reproduces every
 * dead register, speculative padded read and aliased sentinel-store byte.
 */
#pragma once
#if defined(__aarch64__)
#include "native_geometry_ls.h"
#define BUZZ_NEON_RTZ_NO_REMAP
#include "spu_neon_rtz.h"
#undef BUZZ_NEON_RTZ_NO_REMAP
#else
#include "native_stagef_avx2.h"
#endif

static inline u128 buzz_unit24_row(unsigned selector)
{
    u128 row;
    static const uint32_t order[3][3] = {
        {0x00010203, 0x10111213, 0x14151617},
        {0x10111213, 0x00010203, 0x14151617},
        {0x10111213, 0x14151617, 0x00010203}};
    for (unsigned i = 0; i < 3; ++i) row._u32[i] = order[selector >> 2][i];
    if (!(selector & 1)) { row._u32[0] |= 0x80000000; row._u32[2] |= 0x80000000; }
    row._u32[3] = selector & 2 ? 0x3f800000 : 0xbf800000;
    return row;
}

#if defined(__aarch64__)
static inline void buzz_unit24_decode4(uint8_t* memory, uint32_t output, unsigned base,
                                      unsigned vertices, unsigned column, const uint32_t* packed)
{
    const uint32x4_t bits = vld1q_u32(packed);
    const uint32x4_t one_bits = vdupq_n_u32(0x3f800000u);
    const uint32x4_t fraction = vdupq_n_u32(1023);
    u128 u, v, one, scale, bias;
    vst1q_u32(u._u32, vorrq_u32(one_bits, vandq_u32(vshrq_n_u32(bits, 14), fraction)));
    vst1q_u32(v._u32, vorrq_u32(one_bits, vandq_u32(vshrq_n_u32(bits, 4), fraction)));
    vst1q_u32(one._u32, one_bits);
    vst1q_u32(scale._u32, vdupq_n_u32(0x46352e7c));
    vst1q_u32(bias._u32, vdupq_n_u32(0xc6353150));
    u = buzz_neon_fused(u, scale, bias, 0, 0);
    v = buzz_neon_fused(v, scale, bias, 0, 0);
    const u128 sum = buzz_neon_add(buzz_neon_mul(u, u), buzz_neon_mul(v, v), 0);
    const u128 t = buzz_neon_add(one, sum, 0x80000000u);
    const u128 d = buzz_neon_mul(t, spu_fi(t, spu_frsqest(t)));
    for (unsigned i = 0; i < 4 && base + i < vertices; ++i) {
        const unsigned col = (base + i) & 15;
        if (col < column || col == 15) continue;
        const unsigned selector = packed[i] & 15;
        const unsigned axis = selector >> 2;
        const uint32_t signed_d = (d._u32[i] & 0x7fffffffu) | (((selector ^ 1u) & 1u) << 31);
        u128 row;
        row._u32[0] = axis == 0 ? signed_d : u._u32[i];
        row._u32[1] = axis == 1 ? signed_d : axis == 0 ? u._u32[i] : v._u32[i];
        row._u32[2] = axis == 2 ? signed_d : v._u32[i];
        row._u32[3] = selector & 2 ? 0x3f800000u : 0xbf800000u;
        buzz_geometry_ls_write(memory, output + (base + i)*16, row);
    }
}
#else
static inline void buzz_unit24_store4(uint8_t* memory, uint32_t output, unsigned first,
                                     unsigned vertices, unsigned column, __m128i u,
                                     __m128i v, __m128i d, __m128i selector)
{
    const __m128i axis = _mm_srli_epi32(selector, 2);
    const __m128i axis0 = _mm_cmpeq_epi32(axis, _mm_setzero_si128());
    const __m128i axis1 = _mm_cmpeq_epi32(axis, _mm_set1_epi32(1));
    const __m128i axis2 = _mm_cmpeq_epi32(axis, _mm_set1_epi32(2));
    const __m128i sign = _mm_slli_epi32(_mm_xor_si128(selector, _mm_set1_epi32(1)), 31);
    d = _mm_or_si128(_mm_and_si128(d, _mm_set1_epi32(0x7fffffff)), sign);
    __m128 x = _mm_castsi128_ps(_mm_blendv_epi8(u, d, axis0));
    __m128 y = _mm_castsi128_ps(_mm_blendv_epi8(_mm_blendv_epi8(v, u, axis0), d, axis1));
    __m128 z = _mm_castsi128_ps(_mm_blendv_epi8(v, d, axis2));
    __m128 w = _mm_castsi128_ps(_mm_or_si128(_mm_set1_epi32(0x3f800000),
        _mm_slli_epi32(_mm_xor_si128(_mm_srli_epi32(selector, 1), _mm_set1_epi32(1)), 31)));
    _MM_TRANSPOSE4_PS(x, y, z, w);
    const __m128 rows[4] = {x, y, z, w};
    for (unsigned i = 0; i < 4 && first + i < vertices; ++i) {
        const unsigned col = (first + i) & 15;
        if (col < column || col == 15) continue;
        u128 row; memcpy(&row, &rows[i], 16);
        buzz_geometry_ls_write(memory, output + (first + i) * 16, row);
    }
}
#endif

static inline int buzz_unit24_native(spu_context* ctx, void (*setup)(spu_context*), void (*tail)(spu_context*))
{
    if (ctx->ls != ctx->ls_store || buzz_geometry_debug || g_spu_ls_probe > 0 ||
        g_spu_ls_watch_n > 0 || g_spu_smc_watch > 0 ||
        (ctx->int_enable && (ctx->event_status & ctx->event_mask))) return 0;
    uint8_t* memory = ctx->ls_store;
    const uint32_t column = ctx->gpr[43]._u32[0], stride = ctx->gpr[106]._u32[0];
    const uint32_t first = ctx->gpr[41]._u32[0], limit = ctx->gpr[80]._u32[0];
    const uint32_t input = ctx->gpr[36]._u32[0], attribute = ctx->gpr[42]._u32[0];
    const uint32_t scratch = ctx->gpr[82]._u32[0];
    if (column >= 15 || ctx->gpr[44]._u32[0] != column * 16 || ctx->gpr[107]._u32[0] != 16 ||
        stride < 3 || stride > 256 || (input & 15) || attribute > 4096 || first < 0xe680 ||
        first >= limit || limit >= SPU_LS_SIZE || scratch < 0xe680 || scratch + 16ull > SPU_LS_SIZE) return 0;
    const unsigned bases[4] = {36, 35, 34, 33}, second[4] = {40, 39, 38, 37};
    for (unsigned i = 0; i < 4; ++i)
        if (ctx->gpr[81]._u32[i] != 1024 || ctx->gpr[83]._u32[i] != stride * 64 ||
            ctx->gpr[80]._u32[i] != limit || ctx->gpr[82]._u32[i] != scratch ||
            ctx->gpr[41]._u32[i] != first + i * 256 ||
            ctx->gpr[bases[i]]._u32[0] != input + i * 16 * stride ||
            ctx->gpr[second[i]]._u32[0] != input + i * 16 * stride + 16) return 0;
    const unsigned vertices = (limit - first) / 16 + 1;
    if (vertices < 64 || !buzz_unit24_constants_match(ctx)) return 0;
    const uint32_t output = first & ~15u;
    const uint64_t input_start = (uint64_t)input + attribute;
    // Include the original pipeline's overread, never assume missing padding
    // is zero. No stream wraps or aliases the output/context sentinel here.
    const uint64_t input_end = input_start + (vertices + 192ull) * stride + 32;
    const uint64_t output_end = output + vertices * 16ull;
    if (input_start < 0xe680 || input_end > SPU_LS_SIZE || output_end > SPU_LS_SIZE ||
        (output < input_end && input_start < output_end) ||
        (scratch < input_end && input_start < scratch + 16ull) ||
        (scratch < output_end && output < scratch + 16ull)) return 0;
    const u128 gather = buzz_geometry_ls_read(memory, 0xd8b0);
    if (gather._u32[0] != 0x80800280 || gather._u32[1] != 0x80800001 ||
        gather._u32[2] != 0x80800102 || gather._u32[3] != 0x80808080) return 0;
    const uint32_t table = ctx->gpr[25]._u32[0] & 0x3fff0;
    if (table + 192 > SPU_LS_SIZE || (output < table + 192 && table < output_end)) return 0;
    for (unsigned selector = 0; selector < 12; ++selector) {
        u128 expected = buzz_unit24_row(selector), actual = buzz_geometry_ls_read(memory, table + 16*selector);
#if defined(__aarch64__)
        if (memcmp(&expected, &actual, sizeof(expected))) return 0;
#else
        __m128i x, y; memcpy(&x, &expected, 16); memcpy(&y, &actual, 16);
        x = _mm_xor_si128(x, y);
        if (!_mm_testz_si128(x, x)) return 0;
#endif
    }
    for (unsigned i = 0; i < vertices; ++i)
        if ((i & 15) >= column && (i & 15) != 15 && (memory[input_start + i * stride + 2] & 15) >= 12) return 0;
#if !defined(__aarch64__)
    const v8 scale = v8_set(0x46352e7c), bias = v8_set(0xc6353150), one = v8_set(0x3f800000);
#endif
    for (unsigned base = 0; base < vertices; base += 8) {
        uint32_t packed[8] = {};
        for (unsigned i = 0; i < 8 && base + i < vertices; ++i) {
            uint32_t word; memcpy(&word, memory + input_start + (base + i) * stride, 4);
            packed[i] = SPU_BSWAP32(word) >> 8;
        }
#if defined(__aarch64__)
        buzz_unit24_decode4(memory, output, base, vertices, column, packed);
        buzz_unit24_decode4(memory, output, base + 4, vertices, column, packed + 4);
#else
        const v8 bits = _mm256_loadu_si256((const __m256i*)packed);
        const v8 selector = _mm256_and_si256(bits, v8_set(15));
        v8 u = _mm256_or_si256(one, _mm256_and_si256(_mm256_srli_epi32(bits, 14), v8_set(1023)));
        v8 v = _mm256_or_si256(one, _mm256_and_si256(_mm256_srli_epi32(bits, 4), v8_set(1023)));
        u = v8_fma(u, scale, bias); v = v8_fma(v, scale, bias);
        const v8 uu = v8_fm(u, u), vv = v8_fm(v, v);
        const v8 sum = v8_finish(_mm256_add_ps(_mm256_castsi256_ps(uu), _mm256_castsi256_ps(vv)), uu, vv, uu, 0);
        const v8 t = v8_fs(one, sum), d = v8_fm(t, v8_fi(t, v8_frsqest(t)));
        buzz_unit24_store4(memory, output, base, vertices, column, _mm256_castsi256_si128(u),
            _mm256_castsi256_si128(v), _mm256_castsi256_si128(d), _mm256_castsi256_si128(selector));
        buzz_unit24_store4(memory, output, base + 4, vertices, column, _mm256_extracti128_si256(u, 1),
            _mm256_extracti128_si256(v, 1), _mm256_extracti128_si256(d, 1), _mm256_extracti128_si256(selector, 1));
#endif
    }
    ctx->gpr[43] = spu_ai(ctx->gpr[43], 15 - column);
    ctx->gpr[44] = spu_ai(ctx->gpr[44], (15 - column) * 16);
    setup(ctx); tail(ctx);
    return 1;
}
