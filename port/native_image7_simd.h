#pragma once
#include "spu_context.h"

// Exact bulk conversion for the pinned 0x86B8 image-7 leaf. Call only inside
// buzz_spu_scoped_rtz, after the generated wrapper has proved the layout.
// Admission is read-only; false leaves all guest state unchanged.
#if defined(__AVX2__) && defined(__FMA__) && !SPU_LS_DEBUG_HOOKS
#include <immintrin.h>
#include <cstring>
#include <cstdint>

extern "C" void buzz_image7_8638_native(spu_context*);

namespace buzz_image7_simd {

inline __m256 color_constant(uint32_t bits)
{
    return _mm256_castsi256_ps(_mm256_set1_epi32(bits));
}

inline __m256i color8(__m256 y, __m256 u, __m256 v)
{
    y = _mm256_sub_ps(y, _mm256_set1_ps(16));
    u = _mm256_sub_ps(u, _mm256_set1_ps(128));
    v = _mm256_sub_ps(v, _mm256_set1_ps(128));
    const uint32_t cu[3] = {0, 0xbec8b439, 0x40011687};
    const uint32_t cv[3] = {0x3fcc49ba, 0xbf5020c5, 0};
    __m256i result = _mm256_set1_epi32(0xff000000);
    for (unsigned k = 0; k < 3; ++k) {
        // Preserve the original U, V, Y FMA order under RTZ/FTZ/DAZ.
        __m256 a = _mm256_fmadd_ps(u, color_constant(cu[k]), _mm256_set1_ps(.5f));
        a = _mm256_fmadd_ps(v, color_constant(cv[k]), a);
        a = _mm256_fmadd_ps(y, color_constant(0x3f94fdf4), a);
        a = _mm256_min_ps(_mm256_max_ps(a, _mm256_setzero_ps()), _mm256_set1_ps(255));
        // Equivalent to the top byte of SPU CFLTU 149 for every YUV byte triple.
        const __m256i integer = _mm256_cvttps_epi32(a);
        result = _mm256_or_si256(result, _mm256_sllv_epi32(integer, _mm256_set1_epi32(k * 8)));
    }
    return result;
}

inline __m256 bytes8(const uint8_t* p)
{
    return _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(
        _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p))));
}

inline __m256 chroma8(const uint8_t* p)
{
    uint32_t word;
    memcpy(&word, p, 4);
    const __m128i b = _mm_cvtsi32_si128(word);
    return _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(_mm_unpacklo_epi8(b, b)));
}

inline bool try_convert(spu_context* c)
{
    if (c->ls != c->ls_store) return false;
    // Exact packing, scale, offset and color coefficients, including unused lanes.
    static constexpr uint32_t registers[7][4] = {
        {0x00000000u, 0x00000000u, 0x65d55555u, 0x55555555u},
        {0x3f94fdf4u, 0x3f94fdf4u, 0x3f94fdf4u, 0x3f800000u},
        {0x41800000u, 0x41800000u, 0x41800000u, 0x41800000u},
        {0x43000000u, 0x43000000u, 0x43000000u, 0x43000000u},
        {0x00000000u, 0xbec8b439u, 0x40011687u, 0x3f800000u},
        {0x3fcc49bau, 0xbf5020c5u, 0x00000000u, 0x3f800000u},
        {0x3f000000u, 0x3f000000u, 0x3f000000u, 0x00000000u},
    };
    // Every shuffle quad read by the original leaf, in host LS byte order.
    static constexpr unsigned table_addresses[] = {
        0xc9b0, 0xc9c0, 0xc9d0, 0xc9e0, 0xc9f0, 0xca00, 0xca10, 0xca20,
        0xca90, 0xcaa0, 0xcab0, 0xcac0, 0xcad0, 0xcae0, 0xcaf0, 0xcb00,
        0xcb10, 0xcb20, 0xcb30, 0xcb40, 0xcb50, 0xcb60, 0xcb70, 0xcb80,
        0xcb90, 0xcba0, 0xcbb0, 0xcbc0, 0xcbd0, 0xcbe0, 0xcbf0, 0xcc00,
        0xcc10, 0xcc20,
    };
    static constexpr uint8_t tables[][16] = {
        {0, 0, 0, 16, 0, 0, 0, 16, 0, 0, 0, 16, 0, 0, 0, 9},
        {0, 0, 0, 17, 0, 0, 0, 17, 0, 0, 0, 17, 0, 0, 0, 9},
        {0, 0, 0, 18, 0, 0, 0, 18, 0, 0, 0, 18, 0, 0, 0, 9},
        {0, 0, 0, 19, 0, 0, 0, 19, 0, 0, 0, 19, 0, 0, 0, 9},
        {0, 0, 0, 20, 0, 0, 0, 20, 0, 0, 0, 20, 0, 0, 0, 9},
        {0, 0, 0, 21, 0, 0, 0, 21, 0, 0, 0, 21, 0, 0, 0, 9},
        {0, 0, 0, 22, 0, 0, 0, 22, 0, 0, 0, 22, 0, 0, 0, 9},
        {0, 0, 0, 23, 0, 0, 0, 23, 0, 0, 0, 23, 0, 0, 0, 9},
        {0, 4, 8, 12, 16, 20, 24, 28, 128, 128, 128, 128, 128, 128, 128, 128},
        {128, 128, 128, 128, 128, 128, 128, 128, 0, 4, 8, 12, 16, 20, 24, 28},
        {0, 0, 0, 24, 0, 0, 0, 24, 0, 0, 0, 24, 0, 0, 0, 9},
        {0, 0, 0, 25, 0, 0, 0, 25, 0, 0, 0, 25, 0, 0, 0, 9},
        {0, 0, 0, 26, 0, 0, 0, 26, 0, 0, 0, 26, 0, 0, 0, 9},
        {0, 0, 0, 27, 0, 0, 0, 27, 0, 0, 0, 27, 0, 0, 0, 9},
        {0, 0, 0, 28, 0, 0, 0, 28, 0, 0, 0, 28, 0, 0, 0, 9},
        {0, 0, 0, 29, 0, 0, 0, 29, 0, 0, 0, 29, 0, 0, 0, 9},
        {0, 0, 0, 30, 0, 0, 0, 30, 0, 0, 0, 30, 0, 0, 0, 9},
        {0, 0, 0, 31, 0, 0, 0, 31, 0, 0, 0, 31, 0, 0, 0, 9},
        {0, 0, 0, 16, 0, 0, 0, 16, 0, 0, 0, 16, 0, 0, 0, 8},
        {0, 0, 0, 17, 0, 0, 0, 17, 0, 0, 0, 17, 0, 0, 0, 8},
        {0, 0, 0, 18, 0, 0, 0, 18, 0, 0, 0, 18, 0, 0, 0, 8},
        {0, 0, 0, 19, 0, 0, 0, 19, 0, 0, 0, 19, 0, 0, 0, 8},
        {0, 0, 0, 20, 0, 0, 0, 20, 0, 0, 0, 20, 0, 0, 0, 8},
        {0, 0, 0, 21, 0, 0, 0, 21, 0, 0, 0, 21, 0, 0, 0, 8},
        {0, 0, 0, 22, 0, 0, 0, 22, 0, 0, 0, 22, 0, 0, 0, 8},
        {0, 0, 0, 23, 0, 0, 0, 23, 0, 0, 0, 23, 0, 0, 0, 8},
        {0, 0, 0, 24, 0, 0, 0, 24, 0, 0, 0, 24, 0, 0, 0, 8},
        {0, 0, 0, 25, 0, 0, 0, 25, 0, 0, 0, 25, 0, 0, 0, 8},
        {0, 0, 0, 26, 0, 0, 0, 26, 0, 0, 0, 26, 0, 0, 0, 8},
        {0, 0, 0, 27, 0, 0, 0, 27, 0, 0, 0, 27, 0, 0, 0, 8},
        {0, 0, 0, 28, 0, 0, 0, 28, 0, 0, 0, 28, 0, 0, 0, 8},
        {0, 0, 0, 29, 0, 0, 0, 29, 0, 0, 0, 29, 0, 0, 0, 8},
        {0, 0, 0, 30, 0, 0, 0, 30, 0, 0, 0, 30, 0, 0, 0, 8},
        {0, 0, 0, 31, 0, 0, 0, 31, 0, 0, 0, 31, 0, 0, 0, 8},
    };
    static_assert(sizeof tables / sizeof tables[0] == sizeof table_addresses / sizeof table_addresses[0]);
    if (memcmp(&c->gpr[100], registers, sizeof registers)) return false;
    for (unsigned i = 0; i < sizeof(table_addresses) / sizeof(table_addresses[0]); ++i)
        if (memcmp(c->ls + table_addresses[i], tables[i], 16)) return false;

    // Subtraction is intentionally modulo 2^32, matching the loop comparison.
    const uint32_t count = c->gpr[119]._u32[0] - c->gpr[111]._u32[0];
    if (!count || count > 1024) return false;

    // Widen before adding: wrapped addresses and overlapping stores need the lift.
    struct Span { uint64_t base, size; };
    const Span input[4] = {
        {uint64_t(c->gpr[112]._u32[0]) + c->gpr[109]._u32[0], uint64_t(count) * 32},
        {uint64_t(c->gpr[113]._u32[0]) + c->gpr[109]._u32[0], uint64_t(count) * 32},
        {uint64_t(c->gpr[114]._u32[0]) + c->gpr[110]._u32[0], uint64_t(count) * 16},
        {uint64_t(c->gpr[115]._u32[0]) + c->gpr[110]._u32[0], uint64_t(count) * 16},
    };
    const Span output[2] = {
        {c->gpr[108]._u32[0], uint64_t(count) * 128},
        {c->gpr[107]._u32[0], uint64_t(count) * 128},
    };
    const auto valid = [](Span s) { return !(s.base & 15) && s.base + s.size <= SPU_LS_SIZE; };
    const auto overlap = [](Span a, Span b) {
        return a.base < b.base + b.size && b.base < a.base + a.size;
    };
    for (auto x : input)
        if (!valid(x)) return false;
    for (auto x : output) {
        if (!valid(x) || overlap(x, {0xc9b0, 0xcc30 - 0xc9b0})) return false;
        for (auto y : input)
            if (overlap(x, y)) return false;
    }
    if (overlap(output[0], output[1])) return false;

    // The last iteration retains the complete original register/PC publication.
    // Earlier iterations carry only registers 107..111; the leaf has no channels.
    for (unsigned iteration = 0; iteration + 1 < count; ++iteration) {
        for (unsigned x = 0; x < 32; x += 8) {
            const auto u = chroma8(c->ls + input[2].base + iteration * 16 + x / 2);
            const auto v = chroma8(c->ls + input[3].base + iteration * 16 + x / 2);
            for (unsigned row = 0; row < 2; ++row) {
                const auto y = bytes8(c->ls + input[row].base + iteration * 32 + x);
                const auto packed = color8(y, u, v);
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(
                    c->ls + output[row].base + iteration * 128 + x * 4), packed);
            }
        }
    }
    const unsigned done = count - 1;
    for (unsigned lane = 0; lane < 4; ++lane) {
        c->gpr[107]._u32[lane] += done * 128;
        c->gpr[108]._u32[lane] += done * 128;
        c->gpr[109]._u32[lane] += done * 32;
        c->gpr[110]._u32[lane] += done * 16;
        c->gpr[111]._u32[lane] += done;
    }
    buzz_image7_8638_native(c);
    return true;
}

} // namespace buzz_image7_simd
#else
namespace buzz_image7_simd {
inline bool try_convert(spu_context*) { return false; }
} // namespace buzz_image7_simd
#endif
