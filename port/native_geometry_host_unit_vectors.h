/* Recognized Edge storage-9 packed directions -> eight host SoA vertices.
 * The job reader owns byte gathering, bounds and selector0..11 admission.
 * No LS, SPU register bank, selector-row ABI table or lookahead is used.
 * Arithmetic requires the image-41 RTZ/FTZ/DAZ environment. */
#pragma once
#include "native_geometry_bounded_position.h"

namespace buzz {
inline v8 geometry_host_unit_words8(const uint8_t* source,unsigned stride,unsigned vertices)
{
#if BUZZ_GEOMETRY_MOBILE_KERNELS && defined(__aarch64__)
    if (vertices == 8 && stride == 3) {
        const uint8x8x3_t bytes = vld3_u8(source);
        const uint16x8_t high = vorrq_u16(vshlq_n_u16(vmovl_u8(bytes.val[0]),8),vmovl_u8(bytes.val[1]));
        const uint16x8_t low = vmovl_u8(bytes.val[2]);
        return {vorrq_u32(vshll_n_u16(vget_low_u16(high),8),vmovl_u16(vget_low_u16(low))),
                vorrq_u32(vshll_n_u16(vget_high_u16(high),8),vmovl_u16(vget_high_u16(low)))};
    }
#endif
    if (vertices==8 && stride>=3 && stride<=255) {
        // Four-byte reads for lanes 0..6 stay within the full source span.
        // Lane 7 is masked out and read as exactly three bytes at the end.
#if (defined(__AVX2__) && !defined(__aarch64__)) || (BUZZ_GEOMETRY_MOBILE_KERNELS && defined(__aarch64__))
        // Independent word loads stay inside the validated stream.
        auto word = [&](unsigned lane) {
            uint32_t value;
            memcpy(&value,source+lane*stride,sizeof(value));
            return __builtin_bswap32(value)>>8;
        };
        const uint8_t* tail=source+7*stride;
        const unsigned final=(unsigned(tail[0])<<16)|(unsigned(tail[1])<<8)|tail[2];
        return _mm256_setr_epi32(word(0),word(1),word(2),word(3),word(4),word(5),word(6),final);
#else
        const v8 offset=_mm256_mullo_epi32(_mm256_setr_epi32(0,1,2,3,4,5,6,7),v8_set(stride));
        const v8 first_seven=_mm256_setr_epi32(-1,-1,-1,-1,-1,-1,-1,0);
        const v8 words=_mm256_mask_i32gather_epi32(_mm256_setzero_si256(),
            reinterpret_cast<const int*>(source),offset,first_seven,1);
        const auto* last=source+7*stride;
        const uint32_t last_word=(uint32_t(last[0])<<16)|(uint32_t(last[1])<<8)|last[2];
        return _mm256_blend_epi32(_mm256_srli_epi32(v8_bswap(words),8),v8_set(last_word),0x80);
#endif
    }
    alignas(32) uint32_t packed[8]{};
    for(unsigned lane=0;lane<vertices;++lane){
        const auto* at=source+lane*stride;
        packed[lane]=(uint32_t(at[0])<<16)|(uint32_t(at[1])<<8)|at[2];
    }
    return _mm256_load_si256(reinterpret_cast<const v8*>(packed));
}
inline GeometryHostSoA8 decode_geometry_host_unit8(v8 packed)
{
    const v8 one = v8_set(0x3f800000);
    const v8 scale = v8_set(0x46352e7c), bias = v8_set(0xc6353150);
    // Ten mantissa bits select a value less than 1.001. Scale and bias are
    // below 2^14, so the decoded coordinates, squares and sum all stay finite.
    const v8 u = geometry_bounded_fma(_mm256_or_si256(one,
        _mm256_and_si256(_mm256_srli_epi32(packed, 14), v8_set(1023))), scale, bias);
    const v8 v = geometry_bounded_fma(_mm256_or_si256(one,
        _mm256_and_si256(_mm256_srli_epi32(packed, 4), v8_set(1023))), scale, bias);
#if BUZZ_GEOMETRY_MOBILE_KERNELS && defined(__aarch64__)
    const __m256 uf = _mm256_castsi256_ps(u), vf = _mm256_castsi256_ps(v);
    const v8 sum = _mm256_castps_si256(_mm256_add_ps(_mm256_mul_ps(uf, uf), _mm256_mul_ps(vf, vf)));
#else
    const v8 uu = geometry_bounded_mul(u, u), vv = geometry_bounded_mul(v, v);
    const v8 sum = geometry_bounded_finish(_mm256_add_ps(_mm256_castsi256_ps(uu), _mm256_castsi256_ps(vv)));
#endif
    const v8 remainder = geometry_bounded_finish(_mm256_sub_ps(_mm256_castsi256_ps(one), _mm256_castsi256_ps(sum)));
#if BUZZ_GEOMETRY_MOBILE_KERNELS && defined(__aarch64__)
    // A nonzero difference from one here is at least 2^-24. Its
    // inverse-square-root estimate and the product are ordinary finite.
    const v8 inverse = geometry_inverse_sqrt_estimate(remainder);
    const v8 zero = _mm256_cmpeq_epi32(remainder, _mm256_setzero_si256());
    const v8 magnitude = geometry_bounded_mul(remainder, _mm256_andnot_si256(zero, inverse));
#else
    const v8 magnitude = v8_fm(remainder, v8_fi(remainder, v8_frsqest(remainder)));
#endif
    const v8 selector = _mm256_and_si256(packed, v8_set(15));
    const v8 axis = _mm256_srli_epi32(selector, 2);
    const v8 dominant = _mm256_or_si256(_mm256_and_si256(magnitude, v8_set(0x7fffffff)),
        _mm256_slli_epi32(_mm256_xor_si256(selector, v8_set(1)), 31));
    const v8 x_axis = _mm256_cmpeq_epi32(axis, _mm256_setzero_si256());
    GeometryHostSoA8 output;
    output.component[0] = _mm256_blendv_epi8(u, dominant, x_axis);
    output.component[1] = _mm256_blendv_epi8(_mm256_blendv_epi8(v, u, x_axis), dominant,
        _mm256_cmpeq_epi32(axis, v8_set(1)));
    output.component[2] = _mm256_blendv_epi8(v, dominant, _mm256_cmpeq_epi32(axis, v8_set(2)));
    output.component[3] = _mm256_blendv_epi8(one, v8_set(0xbf800000),
        _mm256_cmpeq_epi32(_mm256_and_si256(selector, v8_set(2)), _mm256_setzero_si256()));
    return output;
}
// Admission has checked each active three-byte field and selector0..11.
// The final field and every tail lane are read as exactly three bytes.
inline GeometryHostSoA8 decode_geometry_host_unit_stream8(const uint8_t* source,
    unsigned stride, unsigned vertices)
{
    auto output = decode_geometry_host_unit8(geometry_host_unit_words8(source,stride,vertices));
    const v8 active = _mm256_cmpgt_epi32(v8_set(vertices), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
    for (auto& component : output.component) component = _mm256_and_si256(component, active);
    return output;
}
} // namespace buzz
