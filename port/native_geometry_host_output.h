/* Edge output arithmetic on eight vertices in host little-endian SoA form.
 * No SPU register bank, LS cursor, call frame or speculative iteration lives
 * here. The job owns admission, source decoding and the final stream write.
 * Published FP arithmetic requires the image-41 RTZ/FTZ/DAZ environment. */
#pragma once
#include "native_geometry_host_data.h"
#include <cstdint>
#include <cstring>

namespace buzz {
// Edge's storage-3 representation flushes small magnitudes and saturates
// large magnitudes. It is deliberately not the IEEE F16C conversion.
inline v8 geometry_host_half8(v8 bits)
{
    const v8 magnitude = _mm256_and_si256(_mm256_srli_epi32(bits, 16), v8_set(0x7fff));
    const v8 sign = _mm256_and_si256(_mm256_srli_epi32(bits, 16), v8_set(0x8000));
    const v8 fraction = _mm256_and_si256(
        _mm256_srli_epi32(_mm256_sub_epi32(bits, v8_set(0x38000000)), 13), v8_set(0x7fff));
    const v8 saturated = _mm256_blendv_epi8(fraction, v8_set(0x7bff),
        _mm256_cmpgt_epi32(magnitude, v8_set(0x477f)));
    return _mm256_and_si256(_mm256_or_si256(sign, saturated),
        _mm256_cmpgt_epi32(magnitude, v8_set(0x37ff)));
}

// Truncate float * 2^31 to signed int with SPU saturation, including the
// extended exponent 255. Integer shifts avoid hardware NaN/overflow cases.
inline v8 geometry_host_signed31_8(v8 bits)
{
    const v8 exponent = _mm256_and_si256(_mm256_srli_epi32(bits, 23), v8_set(255));
    const v8 mantissa = _mm256_or_si256(_mm256_and_si256(bits, v8_set(0x7fffff)), v8_set(0x800000));
    const v8 left = _mm256_sllv_epi32(mantissa, _mm256_sub_epi32(exponent, v8_set(119)));
    const v8 right = _mm256_srlv_epi32(mantissa, _mm256_sub_epi32(v8_set(119), exponent));
    const v8 magnitude = _mm256_or_si256(left, right);
    const v8 sign = _mm256_srai_epi32(bits, 31);
    const v8 signed_value = _mm256_sub_epi32(_mm256_xor_si256(magnitude, sign), sign);
    const v8 limit = _mm256_xor_si256(v8_set(0x7fffffff), sign);
    return _mm256_blendv_epi8(signed_value, limit,
        _mm256_cmpgt_epi32(exponent, v8_set(126)));
}

// Storage 1: Edge SNORM16. SPU FMA x*32767.5-0.5 (formatter immediates
// 0x46ffff00/0xbf000000, not LS tables), then cflts scale 2^16 with RTZ and
// saturation, keeping each word's high halfword (an arithmetic >>16, so a
// small negative result becomes -1). Integer shifts give the SPU result for
// every exponent, including 255; no hardware float-to-int conversion.
inline v8 geometry_host_snorm16_8(v8 bits)
{
    const v8 scaled = v8_fma(bits, v8_set(0x46ffff00), v8_set(0xbf000000));
    const v8 exponent = _mm256_and_si256(_mm256_srli_epi32(scaled, 23), v8_set(255));
    const v8 mantissa = _mm256_or_si256(_mm256_and_si256(scaled, v8_set(0x7fffff)), v8_set(0x800000));
    const v8 left = _mm256_sllv_epi32(mantissa, _mm256_sub_epi32(exponent, v8_set(134)));
    const v8 right = _mm256_srlv_epi32(mantissa, _mm256_sub_epi32(v8_set(134), exponent));
    const v8 magnitude = _mm256_or_si256(left, right);
    const v8 sign = _mm256_srai_epi32(scaled, 31);
    const v8 signed_value = _mm256_sub_epi32(_mm256_xor_si256(magnitude, sign), sign);
    const v8 limit = _mm256_xor_si256(v8_set(0x7fffffff), sign);
    const v8 integer = _mm256_blendv_epi8(signed_value, limit, _mm256_cmpgt_epi32(exponent, v8_set(141)));
    return _mm256_and_si256(_mm256_srai_epi32(integer, 16), v8_set(0xffff));
}

// Storage 6: Edge CMP11/11/10, not storage 7 (I16). These are the canonical
// Edge scale/bias values; job admission must prove that codec definition.
inline v8 geometry_host_cmp8(const GeometryHostSoA8& input)
{
    const v8 xy_scale = v8_set(0x3f7fe000), xy_bias = v8_set(0xba000000);
    const v8 x = geometry_host_signed31_8(v8_fma(input.component[0], xy_scale, xy_bias));
    const v8 y = geometry_host_signed31_8(v8_fma(input.component[1], xy_scale, xy_bias));
    const v8 z = geometry_host_signed31_8(v8_fma(input.component[2],
        v8_set(0x3f7fc000), v8_set(0xba800000)));
    return _mm256_or_si256(
        _mm256_or_si256(_mm256_and_si256(_mm256_srli_epi32(x, 21), v8_set(2047)),
            _mm256_slli_epi32(_mm256_and_si256(_mm256_srli_epi32(y, 21), v8_set(2047)), 11)),
        _mm256_and_si256(z, v8_set(0xffc00000)));
}

// These helpers fill the job's host output chunk. The caller writes that
// complete chunk to its final LS/DMA location once, preserving any holes and
// publication padding. Bounds/field overlap are admitted before the job runs.
inline void geometry_host_float_field8(const GeometryHostSoA8& input, unsigned components,
    uint8_t* destination, unsigned stride, unsigned vertices)
{
    for (unsigned component = 0; component < components; ++component) {
        uint32_t words[8];
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(words), v8_bswap(input.component[component]));
        for (unsigned vertex = 0; vertex < vertices; ++vertex)
            memcpy(destination + vertex * stride + component * 4, words + vertex, 4);
    }
}
inline void geometry_host_half_field8(const GeometryHostSoA8& input, unsigned components,
    uint8_t* destination, unsigned stride, unsigned vertices)
{
    for (unsigned component = 0; component < components; ++component) {
        uint32_t words[8];
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(words), geometry_host_half8(input.component[component]));
        for (unsigned vertex = 0; vertex < vertices; ++vertex) {
            auto* at = destination + vertex * stride + component * 2;
            at[0] = uint8_t(words[vertex] >> 8); at[1] = uint8_t(words[vertex]);
        }
    }
}
inline void geometry_host_snorm_field8(const GeometryHostSoA8& input, unsigned components,
    uint8_t* destination, unsigned stride, unsigned vertices)
{
    for (unsigned component = 0; component < components; ++component) {
        uint32_t words[8];
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(words), geometry_host_snorm16_8(input.component[component]));
        for (unsigned vertex = 0; vertex < vertices; ++vertex) {
            auto* at = destination + vertex * stride + component * 2;
            at[0] = uint8_t(words[vertex] >> 8); at[1] = uint8_t(words[vertex]);
        }
    }
}
inline void geometry_host_cmp_field8(const GeometryHostSoA8& input,
    uint8_t* destination, unsigned stride, unsigned vertices)
{
    uint32_t words[8];
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(words), v8_bswap(geometry_host_cmp8(input)));
    for (unsigned vertex = 0; vertex < vertices; ++vertex)
        memcpy(destination + vertex * stride, words + vertex, 4);
}
} // namespace buzz
