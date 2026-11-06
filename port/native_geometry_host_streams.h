/* BE Edge float words -> host little-endian SoA, eight vertices at a time.
 * Source decoding is independent of LS/register layout. The job admits
 * vertices<=8,components<=4,stride>=components*4 and the active source span.
 * Inactive lanes/components are zero; raw position W is explicitly one. */
#pragma once
#include "native_geometry_host_data.h"

namespace buzz {
// Storage-3 input is not IEEE half conversion. The original Edge shuffle
// retains the first byte of the field's aligned source quad in both low
// bytes, even for a field that crosses into the following quad. Keep that
// provenance explicit; it can change zero and subsequent FP arithmetic.
// halves: one encoded half in each lane's low16; prefix_bytes: low8.
inline GeometryHostSoA8 decode_geometry_host_halves8(const GeometryHostSoA8& halves,
    v8 prefix_bytes, unsigned components)
{
    const v8 prefix = _mm256_mullo_epi32(prefix_bytes, v8_set(0x101));
    GeometryHostSoA8 output;
    for (auto& component : output.component) component = _mm256_setzero_si256();
    for (unsigned component = 0; component < components; ++component) {
        const v8 encoded = _mm256_or_si256(_mm256_slli_epi32(halves.component[component], 16), prefix);
        output.component[component] = _mm256_add_epi32(
            _mm256_andnot_si256(v8_set(0x70000000), _mm256_srai_epi32(encoded, 3)),
            _mm256_and_si256(_mm256_cmpgt_epi32(_mm256_and_si256(encoded, v8_set(0x7fffffff)),
                _mm256_setzero_si256()), v8_set(0x38000000)));
    }
    return output;
}

// The arena begins on a 16-byte boundary. Admission covers each active
// field and its aligned prefix byte; no speculative row or wide overread.
inline GeometryHostSoA8 decode_geometry_host_half_stream8(const uint8_t* arena,
    unsigned field_offset, unsigned stride, unsigned components, unsigned vertices)
{
    if (vertices == 8 && (components == 2 || components == 4)) {
        // Each admitted field contains two or four complete half words. Read
        // pairs together, retaining the original aligned-prefix provenance.
        const v8 offsets = _mm256_mullo_epi32(_mm256_setr_epi32(0,1,2,3,4,5,6,7),v8_set(stride));
        const v8 first = v8_bswap(_mm256_i32gather_epi32(reinterpret_cast<const int*>(arena+field_offset),offsets,1));
        GeometryHostSoA8 halves{{_mm256_srli_epi32(first,16),_mm256_and_si256(first,v8_set(65535)),
            _mm256_setzero_si256(),_mm256_setzero_si256()}};
        if (components == 4) {
            const v8 second = v8_bswap(_mm256_i32gather_epi32(reinterpret_cast<const int*>(arena+field_offset+4),offsets,1));
            halves.component[2] = _mm256_srli_epi32(second,16);
            halves.component[3] = _mm256_and_si256(second,v8_set(65535));
        }
        alignas(32) uint32_t prefixes[8];
        for (unsigned lane=0;lane<8;++lane) prefixes[lane]=arena[(field_offset+lane*stride)&~15u];
        return decode_geometry_host_halves8(halves,_mm256_load_si256(reinterpret_cast<const v8*>(prefixes)),components);
    }
    alignas(32) uint32_t halves[4][8]{}, prefixes[8]{};
    for (unsigned vertex = 0; vertex < vertices; ++vertex) {
        const unsigned at = field_offset + vertex * stride;
        prefixes[vertex] = arena[at & ~15u];
        for (unsigned component = 0; component < components; ++component)
            halves[component][vertex] = (uint32_t(arena[at + component * 2]) << 8) |
                arena[at + component * 2 + 1];
    }
    GeometryHostSoA8 input;
    for (unsigned component = 0; component < 4; ++component)
        input.component[component] = _mm256_load_si256(reinterpret_cast<const v8*>(halves[component]));
    return decode_geometry_host_halves8(input, _mm256_load_si256(reinterpret_cast<const v8*>(prefixes)), components);
}

inline GeometryHostSoA8 decode_geometry_host_floats8(const uint8_t* source,
    unsigned stride, unsigned components, unsigned vertices)
{
    const v8 lane = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    const v8 active = _mm256_cmpgt_epi32(v8_set(vertices), lane);
    const v8 offset = _mm256_mullo_epi32(lane, v8_set(stride));
    GeometryHostSoA8 output;
    for (auto& component : output.component) component = _mm256_setzero_si256();
    for (unsigned component = 0; component < components; ++component)
        output.component[component] = v8_bswap(_mm256_mask_i32gather_epi32(_mm256_setzero_si256(),
            reinterpret_cast<const int*>(source + component * 4), offset, active, 1));
    return output;
}
inline GeometryHostSoA8 decode_geometry_host_positions8(const uint8_t* source, unsigned vertices)
{
    if (vertices == 8) {
        // A full position block is exactly 96 bytes. Deinterleave contiguous
        // loads instead of gathering 24 separate words. Tails retain masked
        // gathers so the last admitted byte is never overread.
        GeometryHostSoA8 output;
#if defined(__aarch64__)
        const auto lo = vld3q_u32(reinterpret_cast<const uint32_t*>(source));
        const auto hi = vld3q_u32(reinterpret_cast<const uint32_t*>(source + 48));
        for (unsigned component = 0; component < 3; ++component)
            output.component[component] = v8_bswap({lo.val[component],hi.val[component]});
#else
        const v8 a = _mm256_loadu_si256(reinterpret_cast<const v8*>(source));
        const v8 b = _mm256_loadu_si256(reinterpret_cast<const v8*>(source + 32));
        const v8 c = _mm256_loadu_si256(reinterpret_cast<const v8*>(source + 64));
        auto select = [](v8 value, v8 order) { return _mm256_permutevar8x32_epi32(value,order); };
        output.component[0] = v8_bswap(_mm256_blend_epi32(_mm256_blend_epi32(
            select(a,_mm256_setr_epi32(0,3,6,0,0,0,0,0)),
            select(b,_mm256_setr_epi32(0,0,0,1,4,7,0,0)),0x38),
            select(c,_mm256_setr_epi32(0,0,0,0,0,0,2,5)),0xc0));
        output.component[1] = v8_bswap(_mm256_blend_epi32(_mm256_blend_epi32(
            select(a,_mm256_setr_epi32(1,4,7,0,0,0,0,0)),
            select(b,_mm256_setr_epi32(0,0,0,2,5,0,0,0)),0x18),
            select(c,_mm256_setr_epi32(0,0,0,0,0,0,3,6)),0xe0));
        output.component[2] = v8_bswap(_mm256_blend_epi32(_mm256_blend_epi32(
            select(a,_mm256_setr_epi32(2,5,0,0,0,0,0,0)),
            select(b,_mm256_setr_epi32(0,0,0,3,6,0,0,0)),0x1c),
            select(c,_mm256_setr_epi32(0,0,0,0,0,1,4,7)),0xe0));
#endif
        output.component[3] = v8_set(0x3f800000);
        return output;
    }
    auto output = decode_geometry_host_floats8(source, 12, 3, vertices);
    output.component[3] = v8_set(0x3f800000);
    return output;
}
} // namespace buzz
