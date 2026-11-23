// EU 1.00 PPU 0x2cbb58, VMX matrix composition. Guest vectors contain
// big-endian words in memory; keep host-order rows throughout the arithmetic.
#pragma once
#include <immintrin.h>
#include <initializer_list>

namespace buzz
{
inline __m128 ppu_matrix_swap(__m128 row)
{
    return _mm_castsi128_ps(_mm_shuffle_epi8(
        _mm_castps_si128(row), _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12)));
}

inline __m128 ppu_matrix_load(uint64_t address)
{
    __m128 row;
    vm_raw_ld16(&row, address & ~uint64_t(15));
    return ppu_matrix_swap(row);
}

inline void ppu_matrix_register(ppu_context& c, unsigned reg, __m128 value)
{
    value = ppu_matrix_swap(value);
    memcpy(&c.vr[reg], &value, sizeof(value));
}

// VMX non-Java mode flushes tiny values to signed zero. Every input to the
// final add is an FMA result, whose NaN has already been canonicalized.
inline __m128 ppu_matrix_flush(__m128 value, bool nj)
{
    if (!nj)
        return value;
    const __m128i bits = _mm_castps_si128(value);
    const __m128i tiny
        = _mm_cmpeq_epi32(_mm_and_si128(bits, _mm_set1_epi32(0x7f800000)), _mm_setzero_si128());
    return _mm_castsi128_ps(_mm_andnot_si128(_mm_and_si128(tiny, _mm_set1_epi32(0x7fffffff)), bits));
}

template <bool Ordinary> inline __m128 ppu_matrix_finish(__m128 value, bool nj)
{
    if constexpr (Ordinary)
        return value;
    const __m128i bits = _mm_castps_si128(ppu_matrix_flush(value, nj));
    const __m128i nan
        = _mm_cmpgt_epi32(_mm_and_si128(bits, _mm_set1_epi32(0x7fffffff)), _mm_set1_epi32(0x7f800000));
    return _mm_castsi128_ps(_mm_blendv_epi8(bits, _mm_set1_epi32(0x7fc00000), nan));
}

template <bool Ordinary, bool ObjectTransform> inline void ppu_compose_rows(ppu_context& c, const __m128* weights, __m128* basis)
{
    const bool nj = (c.vscr & VMX_NJ) != 0;
    if constexpr (!Ordinary)
        for (unsigned i = 0; i < 4; ++i)
            basis[i] = ppu_matrix_flush(basis[i], nj);

    // Other lifted code can observe the VMX scratch registers at the end of
    // the call. These four rows name their exact architectural destinations.
    constexpr unsigned xreg[] = { 2, 18, 10, ObjectTransform ? 5u : 3u }, yreg[] = { 19, 17, 9, ObjectTransform ? 6u : 4u };
    constexpr unsigned oddreg[] = { ObjectTransform ? 4u : 7u, ObjectTransform ? 3u : 6u, 1, 8 }, resultreg[] = { 11, 12, 0, 13 };
    ppu_matrix_register(c, ObjectTransform ? 7 : 5, _mm_shuffle_ps(weights[2], weights[2], 0xaa));
    ppu_matrix_register(c, 16, _mm_shuffle_ps(weights[2], weights[2], 0xff));
    for (unsigned i = 0; i < 4; ++i) {
        const __m128 row = Ordinary ? weights[i] : ppu_matrix_flush(weights[i], nj);
        const auto x = ppu_matrix_finish<Ordinary>(
            _mm_fmadd_ps(basis[0], _mm_shuffle_ps(row, row, 0x00), _mm_setzero_ps()), nj);
        const auto y = ppu_matrix_finish<Ordinary>(
            _mm_fmadd_ps(basis[1], _mm_shuffle_ps(row, row, 0x55), _mm_setzero_ps()), nj);
        const auto even
            = ppu_matrix_finish<Ordinary>(_mm_fmadd_ps(basis[2], _mm_shuffle_ps(row, row, 0xaa), x), nj);
        const auto odd
            = ppu_matrix_finish<Ordinary>(_mm_fmadd_ps(basis[3], _mm_shuffle_ps(row, row, 0xff), y), nj);
        ppu_matrix_register(c, xreg[i], x);
        ppu_matrix_register(c, yreg[i], y);
        ppu_matrix_register(c, oddreg[i], odd);
        ppu_matrix_register(c, resultreg[i], ppu_matrix_finish<Ordinary>(_mm_add_ps(even, odd), nj));
    }
}

template <bool ObjectTransform>
inline void ppu_compose_loaded_matrix(ppu_context& c, const __m128 (&weights)[4], __m128 (&basis)[4])
{
    // With zero or 2^-20 <= |input| <= 2^20, the smallest nonzero
    // product quantum is 2^-86. Neither sums nor differences can underflow.
    __m128i outside = _mm_setzero_si128();
    for (unsigned i = 0; i < 4; ++i) {
        for (__m128 row : { weights[i], basis[i] }) {
            const __m128i magnitude = _mm_and_si128(_mm_castps_si128(row), _mm_set1_epi32(0x7fffffff));
            const __m128i too_large = _mm_cmpgt_epi32(magnitude, _mm_set1_epi32(0x49800000));
            const __m128i too_small = _mm_cmpgt_epi32(_mm_set1_epi32(0x35800000), magnitude);
            const __m128i zero = _mm_cmpeq_epi32(magnitude, _mm_setzero_si128());
            outside = _mm_or_si128(outside, _mm_or_si128(too_large, _mm_andnot_si128(zero, too_small)));
        }
    }
    if (_mm_testz_si128(outside, outside))
        ppu_compose_rows<true, ObjectTransform>(c, weights, basis);
    else
        ppu_compose_rows<false, ObjectTransform>(c, weights, basis);
}
inline void ppu_compose_matrix(ppu_context& c)
{
    const uint64_t weights_address = c.gpr[9];
    const uint64_t basis_address = c.gpr[11];
    __m128 weights[4], basis[4];
    for (unsigned i : { 0u, 3u, 1u, 2u })
        weights[i] = ppu_matrix_load(weights_address + 16 * i);
    for (unsigned i : { 0u, 1u, 3u, 2u })
        basis[i] = ppu_matrix_load(basis_address + 16 * i);

    ppu_compose_loaded_matrix<false>(c, weights, basis);
}
} // namespace buzz
