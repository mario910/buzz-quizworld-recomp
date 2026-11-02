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
    auto output = decode_geometry_host_floats8(source, 12, 3, vertices);
    output.component[3] = v8_set(0x3f800000);
    return output;
}
} // namespace buzz
