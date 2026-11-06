/* Recognized Edge storage-9 packed directions -> eight host SoA vertices.
 * The job reader owns byte gathering, bounds and selector0..11 admission.
 * No LS, SPU register bank, selector-row ABI table or lookahead is used.
 * Arithmetic requires the image-41 RTZ/FTZ/DAZ environment. */
#pragma once
#include "native_geometry_host_data.h"

namespace buzz {
inline v8 geometry_host_unit_words8(const uint8_t* source,unsigned stride,unsigned vertices)
{
    if (vertices==8 && stride>=3 && stride<=255) {
        // Four-byte reads for lanes 0..6 stay within the full source span.
        // Lane 7 is masked out and read as exactly three bytes at the end.
        const v8 offset=_mm256_mullo_epi32(_mm256_setr_epi32(0,1,2,3,4,5,6,7),v8_set(stride));
        const v8 first_seven=_mm256_setr_epi32(-1,-1,-1,-1,-1,-1,-1,0);
        const v8 words=_mm256_mask_i32gather_epi32(_mm256_setzero_si256(),
            reinterpret_cast<const int*>(source),offset,first_seven,1);
        const auto* last=source+7*stride;
        const uint32_t last_word=(uint32_t(last[0])<<16)|(uint32_t(last[1])<<8)|last[2];
        return _mm256_blend_epi32(_mm256_srli_epi32(v8_bswap(words),8),v8_set(last_word),0x80);
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
    const v8 u = v8_fma(_mm256_or_si256(one,
        _mm256_and_si256(_mm256_srli_epi32(packed, 14), v8_set(1023))), scale, bias);
    const v8 v = v8_fma(_mm256_or_si256(one,
        _mm256_and_si256(_mm256_srli_epi32(packed, 4), v8_set(1023))), scale, bias);
    const v8 uu = v8_fm(u, u), vv = v8_fm(v, v);
    const v8 sum = v8_finish(_mm256_add_ps(_mm256_castsi256_ps(uu), _mm256_castsi256_ps(vv)), uu, vv, uu, 0);
    const v8 remainder = v8_fs(one, sum);
    const v8 magnitude = v8_fm(remainder, v8_fi(remainder, v8_frsqest(remainder)));
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
