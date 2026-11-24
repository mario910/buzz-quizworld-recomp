// PPU transform and weighted-vector inner loops from the EU disc.
#pragma once
#include "native_ppu_matrix.h"

namespace buzz
{
inline void ppu_object_transform(ppu_context& c, uint32_t toc_offset = 0x11ac)
{
    __m128 weights[4], basis[4];
    weights[0] = ppu_matrix_load(c.gpr[4]);
    for (unsigned i = 1; i < 4; ++i)
        weights[i] = ppu_matrix_load(uint64_t(uint32_t(c.gpr[4])) + i * 16);

    const uint64_t address = uint32_t(c.gpr[11] + 48);
    c.gpr[9] = vm_read32(c.gpr[31] + 0x94);
    for (unsigned i : { 0u, 1u, 3u, 2u })
        basis[i] = ppu_matrix_load(address + i * 16);
    c.gpr[11] = vm_read32(c.gpr[2] - toc_offset);
    c.gpr[0] = vm_read32(c.gpr[11]);
    vm_write32(c.gpr[31] + 0x24, c.gpr[0]);
    c.gpr[3] = c.gpr[31];
    c.gpr[7] = 32;
    c.gpr[8] = 16;
    c.gpr[10] = 48;
    ppu_compose_loaded_matrix<true>(c, weights, basis);
}

inline __m128i ppu_vector_bits(const ppu_context& c, unsigned reg)
{
    __m128i v;
    memcpy(&v, &c.vr[reg], sizeof(v));
    return v;
}

// A VMX byte selection addresses a 32-byte pair with its low five bits.
inline __m128i ppu_select_bytes(__m128i first, __m128i second, __m128i selector)
{
    const __m128i index = _mm_and_si128(selector, _mm_set1_epi8(15));
    const __m128i high = _mm_slli_epi16(_mm_and_si128(selector, _mm_set1_epi8(16)), 3);
    return _mm_blendv_epi8(_mm_shuffle_epi8(first, index), _mm_shuffle_epi8(second, index), high);
}

inline void ppu_unpack_transform_rows(ppu_context& c)
{
    const __m128i first = ppu_vector_bits(c, 13);
    const __m128i shared = ppu_vector_bits(c, 0);
    const __m128i third = ppu_vector_bits(c, 1);
    const __m128i x = ppu_select_bytes(first, shared, ppu_vector_bits(c, 30));
    const __m128i y = ppu_select_bytes(first, shared, ppu_vector_bits(c, 29));
    const __m128i z = ppu_select_bytes(third, shared, ppu_vector_bits(c, 28));
    memcpy(&c.vr[12], &x, 16);
    memcpy(&c.vr[13], &y, 16);
    memcpy(&c.vr[1], &z, 16);
}

template<bool updated = false>
inline void ppu_accumulate_weighted_rows(ppu_context& c)
{
    constexpr unsigned disc_dest[] = {8, 10, 11, 9}, update_dest[] = {10, 11, 6, 8, 7, 5, 29};
    constexpr unsigned disc_weight[] = {8, 9, 11, 13}, update_weight[] = {10, 9, 8, 7, 6, 12, 11};
    const bool nj = (c.vscr & VMX_NJ) != 0;
    const __m128 bias = ppu_matrix_flush(ppu_matrix_swap(_mm_castsi128_ps(ppu_vector_bits(c, updated ? 13 : 10))), nj);
    __m128 product, sum;
    for (unsigned i = 0; i < (updated ? 7u : 4u); ++i) {
        // The source can be a previous destination. Reload it for each row.
        const uint64_t address = c.gpr[updated ? update_dest[i] : disc_dest[i]] & ~uint64_t(15);
        const __m128 source = ppu_matrix_flush(ppu_matrix_load(c.gpr[28]), nj);
        const __m128 accumulator = ppu_matrix_flush(ppu_matrix_load(address), nj);
        const __m128 weight
            = ppu_matrix_flush(ppu_matrix_swap(_mm_castsi128_ps(ppu_vector_bits(c, updated ? update_weight[i] : disc_weight[i]))), nj);
        product = ppu_matrix_finish<false>(_mm_fmadd_ps(source, weight, bias), nj);
        // For NaN inputs, preserve the scalar VMX payload and exception behavior.
        const __m128i magnitude = _mm_and_si128(_mm_castps_si128(accumulator), _mm_set1_epi32(0x7fffffff));
        const __m128i nan = _mm_cmpgt_epi32(magnitude, _mm_set1_epi32(0x7f800000));
        if (_mm_testz_si128(nan, nan)) {
            sum = ppu_matrix_finish<false>(_mm_add_ps(accumulator, product), nj);
        } else {
            ppu_matrix_register(c, 0, product);
            ppu_matrix_register(c, 1, accumulator);
            vmx_vaddfp(&c.vr[1], &c.vr[1], &c.vr[0], c.vscr);
            sum = ppu_matrix_swap(_mm_castsi128_ps(ppu_vector_bits(c, 1)));
        }
        const __m128 guest = ppu_matrix_swap(sum);
        vm_raw_st16(address, &guest);
    }
    ppu_matrix_register(c, 0, product);
    ppu_matrix_register(c, 1, sum);
}

// Six plane lengths share the same Newton correction. The sixth result feeds
// a later multiply-add, so its residual and scale are both live at the join.
inline void ppu_refine_plane_lengths(ppu_context& c)
{
    constexpr unsigned lengths[] = { 10, 1, 13, 6, 12, 11 };
    constexpr unsigned estimates[] = { 8, 9, 5, 0, 4, 3 };
    const bool nj = (c.vscr & VMX_NJ) != 0;
    auto input = [&](unsigned reg) {
        return ppu_matrix_flush(ppu_matrix_swap(_mm_castsi128_ps(ppu_vector_bits(c, reg))), nj);
    };
    const __m128 sign = _mm_set1_ps(-0.0f);
    const __m128 target = _mm_xor_ps(input(7), sign);
    const __m128 factor = input(15);
    const __m128 bias = input(17);
    const __m128i last = _mm_shuffle_epi32(ppu_vector_bits(c, 11), 0);
    memcpy(&c.vr[11], &last, 16);
    vmx_vrefp(&c.vr[3], &c.vr[11], c.vscr);
    for (unsigned i = 0; i < 6; ++i) {
        const __m128 estimate = input(estimates[i]);
        // The guest negates the rounded FMA. Moving its sign inside the FMA
        // would change results in directed rounding modes.
        const __m128 residual = ppu_matrix_finish<false>(
            _mm_xor_ps(_mm_fmadd_ps(input(lengths[i]), estimate, target), sign), nj);
        const __m128 scale = ppu_matrix_finish<false>(_mm_fmadd_ps(factor, estimate, bias), nj);
        const __m128 result
            = i == 5 ? residual : ppu_matrix_finish<false>(_mm_fmadd_ps(residual, scale, scale), nj);
        ppu_matrix_register(c, lengths[i], result);
        ppu_matrix_register(c, i == 5 ? 15 : estimates[i], scale);
    }
}
// Finish the three orientation rows and fourth affine row of 0x2836f0.
// All four sums combine canonicalized VMX multiply-add results.
inline void ppu_finish_pose_rows(ppu_context& c)
{
    const bool nj = (c.vscr & VMX_NJ) != 0;
    auto raw = [&](unsigned reg) { return ppu_matrix_swap(_mm_castsi128_ps(ppu_vector_bits(c, reg))); };
    auto input = [&](unsigned reg) { return ppu_matrix_flush(raw(reg), nj); };
    auto madd = [&](const __m128 a, const __m128 x, const __m128 bias) {
        return ppu_matrix_finish<false>(_mm_fmadd_ps(a, x, bias), nj);
    };
    auto add = [&](const __m128 a, const __m128 b) {
        return ppu_matrix_finish<false>(_mm_add_ps(a, b), nj);
    };
    const __m128 w = input(27);
    const __m128 odd0 = madd(w, input(5), input(11));
    const __m128 loaded = ppu_matrix_load(c.gpr[1] + c.gpr[9]);
    ppu_matrix_register(c, 26, loaded);
    const __m128 z = ppu_matrix_flush(loaded, nj);
    const __m128 odd1 = madd(w, input(6), input(3));
    const __m128 odd2 = madd(w, input(7), input(19));
    const __m128 affine = madd(input(30), input(31), input(13));
    const __m128 mask = raw(17);
    const __m128 selected = _mm_or_ps(_mm_andnot_ps(mask, affine), _mm_and_ps(mask, raw(0)));
    const __m128 row = ppu_matrix_flush(selected, nj);
    const __m128 bias = input(12);
    const __m128 x = madd(input(25), _mm_shuffle_ps(row, row, 0x00), bias);
    const __m128 y = madd(input(24), _mm_shuffle_ps(row, row, 0x55), bias);
    const __m128 even3 = madd(z, _mm_shuffle_ps(row, row, 0xaa), x);
    const __m128 odd3 = madd(w, _mm_shuffle_ps(row, row, 0xff), y);
    const __m128 rows[] = {add(madd(z, input(8), input(18)), odd0),
                           add(madd(z, input(9), input(4)), odd1),
                           add(madd(z, input(10), input(2)), odd2), add(even3, odd3)};
    ppu_matrix_register(c, 0, x);
    ppu_matrix_register(c, 1, y);
    ppu_matrix_register(c, 5, odd0);
    ppu_matrix_register(c, 6, odd1);
    ppu_matrix_register(c, 7, odd2);
    ppu_matrix_register(c, 11, odd3);
    ppu_matrix_register(c, 31, selected);
    constexpr unsigned destinations[] = {8, 9, 10, 13}, offsets[] = {25, 24, 27, 26};
    for (unsigned i = 0; i < 4; ++i) {
        ppu_matrix_register(c, destinations[i], rows[i]);
        const uint64_t address = (c.gpr[1] + c.gpr[offsets[i]]) & ~uint64_t(15);
        vm_raw_st16(address, &c.vr[destinations[i]]);
    }
}
} // namespace buzz
