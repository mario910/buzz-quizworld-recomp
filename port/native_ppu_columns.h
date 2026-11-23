// Vector arithmetic shared by the EU 1.00 transform and rotation kernels.
#pragma once
#include "native_ppu_transform.h"

namespace buzz {
struct PpuColumnMath {
    ppu_context& c;
    bool nj;

    __m128 raw(unsigned reg) const { return ppu_matrix_swap(_mm_castsi128_ps(ppu_vector_bits(c, reg))); }
    __m128 input(unsigned reg) const { return ppu_matrix_flush(raw(reg), nj); }
    __m128 madd(__m128 a, __m128 scale, __m128 bias) const {
        return ppu_matrix_finish<false>(_mm_fmadd_ps(a, scale, bias), nj);
    }
    __m128 add(__m128 a, __m128 b) const { return ppu_matrix_finish<false>(_mm_add_ps(a, b), nj); }
    void put(unsigned reg, __m128 value) const { ppu_matrix_register(c, reg, value); }
};

inline __m128 ppu_select_column(__m128 a, __m128 b, __m128 mask)
{
    return _mm_or_ps(_mm_andnot_ps(mask, a), _mm_and_ps(mask, b));
}

// Both guest entries share this tail. Each row has two independently rounded
// partial sums. The first partial sums also remain live in guest registers.
inline void ppu_finish_basis_columns(ppu_context& c)
{
    const PpuColumnMath m{c, (c.vscr & VMX_NJ) != 0};
    const __m128 x = m.input(12), y = m.input(13), bias = m.input(10);
    const __m128 z = ppu_matrix_flush(ppu_matrix_load(c.gpr[1] + c.gpr[9]), m.nj);
    const __m128 w = ppu_matrix_flush(ppu_matrix_load(c.gpr[1] + c.gpr[0]), m.nj);
    c.gpr[9] = 0x20;
    c.gpr[0] = 0x10;
    constexpr unsigned xb[] = {16, 4, 15, 29}, yb[] = {17, 3, 14, 30};
    constexpr unsigned zb[] = {18, 2, 19, 27}, wb[] = {5, 7, 6, 28};
    constexpr unsigned xr[] = {16, 4, 15, 12}, yr[] = {17, 3, 14, 13};
    constexpr unsigned zr[] = {18, 2, 19, 1}, result[] = {5, 7, 6, 0};
    for (unsigned i = 0; i < 4; ++i) {
        const __m128 xx = m.madd(x, m.input(xb[i]), bias);
        const __m128 yy = m.madd(y, m.input(yb[i]), bias);
        const __m128 even = m.madd(z, m.input(zb[i]), xx);
        const __m128 odd = m.madd(w, m.input(wb[i]), yy);
        m.put(xr[i], xx);
        m.put(yr[i], yy);
        m.put(zr[i], even);
        m.put(result[i], m.add(odd, even));
    }
}

inline void ppu_transpose_pose_columns(ppu_context& c)
{
    const PpuColumnMath m{c, (c.vscr & VMX_NJ) != 0};
    const __m128 z = m.input(0), w = m.input(17);
    const __m128 a = m.madd(w, m.input(10), m.input(8));
    const __m128 b = m.madd(w, m.input(18), m.input(4));
    const __m128 d = m.madd(w, m.input(2), m.input(6));
    const __m128 row0 = m.add(m.madd(z, m.input(12), m.input(3)), a);
    const __m128 row1 = m.add(m.madd(z, m.input(9), m.input(5)), b);
    const __m128 row2 = m.add(m.madd(z, m.input(13), m.input(7)), d);
    const __m128 origin = m.madd(m.input(1), m.input(11), m.input(19));
    const __m128 row = ppu_select_column(origin, m.raw(25), m.raw(26));
    const __m128 value = ppu_matrix_flush(row, m.nj);
    const __m128 bias = m.input(31);
    const __m128 x = m.madd(m.input(15), _mm_shuffle_ps(value, value, 0x00), bias);
    const __m128 y = m.madd(m.input(16), _mm_shuffle_ps(value, value, 0x55), bias);
    const __m128 even = m.madd(z, _mm_shuffle_ps(value, value, 0xaa), x);
    const __m128 odd = m.madd(w, _mm_shuffle_ps(value, value, 0xff), y);
    const __m128 row3 = m.add(even, odd);
    const __m128 lo01 = _mm_unpacklo_ps(row0, row1), hi01 = _mm_unpackhi_ps(row0, row1);
    const __m128 lo23 = _mm_unpacklo_ps(row2, row3), hi23 = _mm_unpackhi_ps(row2, row3);
    m.put(18, b);
    m.put(2, d);
    m.put(9, row1);
    m.put(15, x);
    m.put(16, y);
    m.put(17, odd);
    m.put(8, _mm_shuffle_ps(row, row, 0xff));
    m.put(11, hi23);
    m.put(13, lo23);
    m.put(10, _mm_unpackhi_ps(hi01, hi23));
    m.put(0, _mm_unpackhi_ps(lo01, lo23));
    m.put(1, _mm_unpacklo_ps(hi01, hi23));
    m.put(12, _mm_unpacklo_ps(lo01, lo23));
    const int32_t flag = int32_t(c.gpr[0]);
    c.cr = (c.cr & ~15u) | (flag < 0 ? 8u : flag ? 4u : 2u) | ((c.xer >> 31) & 1u);
}

inline void ppu_shuffle_rotation_candidates(ppu_context& c)
{
    const __m128i a = ppu_vector_bits(c, 11), b = ppu_vector_bits(c, 7);
    for (unsigned reg : {12u, 6u, 0u}) {
        const __m128i selected = ppu_select_bytes(a, b, ppu_vector_bits(c, reg));
        memcpy(&c.vr[reg == 0 ? 11 : reg], &selected, sizeof(selected));
    }
}

inline void ppu_finish_rotation_candidates(ppu_context& c)
{
    const PpuColumnMath m{c, (c.vscr & VMX_NJ) != 0};
    const __m128 bias = m.input(4);
    const __m128 selected = ppu_select_column(m.raw(13), m.raw(1), m.raw(0));
    const __m128 value = m.madd(ppu_matrix_flush(selected, m.nj), m.input(17), bias);
    const __m128 x = _mm_shuffle_ps(value, value, 0x00), y = _mm_shuffle_ps(value, value, 0x55);
    const __m128 z = _mm_shuffle_ps(value, value, 0xaa), w = _mm_shuffle_ps(value, value, 0xff);
    const __m128 a = m.madd(m.input(12), x, bias), b = m.madd(m.input(6), y, bias);
    const __m128 d = m.madd(m.input(11), z, bias), e = m.madd(m.input(7), w, bias);
    const __m128 xy = ppu_select_column(a, b, m.raw(15));
    const __m128 xyz = ppu_select_column(xy, d, m.raw(9));
    m.put(0, x);
    m.put(1, y);
    m.put(10, z);
    m.put(13, w);
    m.put(6, b);
    m.put(11, d);
    m.put(7, e);
    m.put(12, ppu_select_column(xyz, e, m.raw(8)));
}
} // namespace buzz
