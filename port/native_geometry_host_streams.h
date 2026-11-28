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

#if defined(__AVX2__) && !defined(__aarch64__)
// Load from the first field and up to the end of the fourth. The two
// shuffles retain just the four big-endian fields in each 128-bit lane.
template<int Stride>
inline v8 geometry_host_half_pairs(const uint8_t* field)
{
    static_assert(Stride == 7 || Stride == 10);
    const v8 first = _mm256_set_m128i(_mm_loadu_si128(reinterpret_cast<const __m128i*>(field + 4 * Stride)),
                                      _mm_loadu_si128(reinterpret_cast<const __m128i*>(field)));
    const v8 last = _mm256_set_m128i(_mm_loadu_si128(reinterpret_cast<const __m128i*>(field + 7 * Stride - 12)),
                                     _mm_loadu_si128(reinterpret_cast<const __m128i*>(field + 3 * Stride - 12)));
    const __m128i left = _mm_setr_epi8(3,2,1,0, Stride+3,Stride+2,Stride+1,Stride, -1,-1,-1,-1, -1,-1,-1,-1);
    const __m128i right = _mm_setr_epi8(-1,-1,-1,-1, -1,-1,-1,-1, 15-Stride,14-Stride,13-Stride,12-Stride, 15,14,13,12);
    return _mm256_or_si256(_mm256_shuffle_epi8(first, _mm256_broadcastsi128_si256(left)),
                           _mm256_shuffle_epi8(last, _mm256_broadcastsi128_si256(right)));
}

inline GeometryHostSoA8 geometry_host_half_pairs7(const uint8_t* arena, unsigned field_offset)
{
    const v8 words = geometry_host_half_pairs<7>(arena + field_offset);
    const unsigned aligned = field_offset & ~15u;
    const unsigned low = field_offset & 15u;
    // Eight adjacent fields touch at most five aligned quads. Read the
    // fifth prefix only when the last field actually starts in that quad.
    const uint32_t prefixes = uint32_t(arena[aligned]) | uint32_t(arena[aligned + 16]) << 8 |
                              uint32_t(arena[aligned + 32]) << 16 | uint32_t(arena[aligned + 48]) << 24;
    // Each lane needs byte ((low + 7 * lane) / 16) of the packed prefixes.
    const v8 shifts = _mm256_and_si256(_mm256_srli_epi32(_mm256_add_epi32(v8_set(low),
        _mm256_setr_epi32(0,7,14,21,28,35,42,49)), 1), v8_set(~7u));
    v8 prefix = _mm256_and_si256(_mm256_srlv_epi32(v8_set(prefixes), shifts), v8_set(255));
    if (low == 15) prefix = _mm256_insert_epi32(prefix, arena[aligned + 64], 7);
    const GeometryHostSoA8 halves{{_mm256_srli_epi32(words,16), _mm256_and_si256(words,v8_set(65535)),
                                   _mm256_setzero_si256(), _mm256_setzero_si256()}};
    return decode_geometry_host_halves8(halves, prefix, 2);
}

inline GeometryHostSoA8 geometry_host_half_pairs10(const uint8_t* arena, unsigned field_offset)
{
    const v8 words = geometry_host_half_pairs<10>(arena + field_offset);
    // The caller admits offset 3 within the first quad; eight records
    // advance by 80 bytes and preserve that alignment.
    const uint8_t* quad = arena + (field_offset & ~15u);
    const v8 prefix = _mm256_setr_epi32(quad[0],quad[0],quad[16],quad[32],
                                         quad[32],quad[48],quad[48],quad[64]);
    const GeometryHostSoA8 halves{{_mm256_srli_epi32(words,16), _mm256_and_si256(words,v8_set(65535)),
                                   _mm256_setzero_si256(), _mm256_setzero_si256()}};
    return decode_geometry_host_halves8(halves,prefix,2);
}

inline GeometryHostSoA8 geometry_host_half_triples18(const uint8_t* arena, unsigned field_offset)
{
    // Read forward from each of the first four fields. End each load of the
    // second group at Z, including the very last byte of the stream.
    const auto* field = arena + field_offset;
    const auto load_four = [](const uint8_t* p) {
        int64_t a,b,c,d;
        memcpy(&a,p,8); memcpy(&b,p+18,8); memcpy(&c,p+36,8); memcpy(&d,p+54,8);
        return _mm256_setr_epi64x(a,b,c,d);
    };
    const v8 first_four = load_four(field);
    const v8 last_four = load_four(field + 4 * 18 - 2);
    const v8 xy = v8_bswap(_mm256_permutevar8x32_epi32(_mm256_blend_epi32(
        first_four, _mm256_slli_epi64(_mm256_srli_epi64(last_four, 16),32), 0xaa),
        _mm256_setr_epi32(0,2,4,6,1,3,5,7)));
    const v8 z = _mm256_permutevar8x32_epi32(_mm256_blend_epi32(
        _mm256_srli_epi64(first_four, 32), _mm256_slli_epi64(_mm256_srli_epi64(last_four, 48),32), 0xaa),
        _mm256_setr_epi32(0,2,4,6,1,3,5,7));
    const auto prefix_at = [&](unsigned lane) { return arena[(field_offset + lane * 18) & ~15u]; };
    const v8 prefix = _mm256_setr_epi32(prefix_at(0),prefix_at(1),prefix_at(2),prefix_at(3),
                                        prefix_at(4),prefix_at(5),prefix_at(6),prefix_at(7));
    const GeometryHostSoA8 halves{{_mm256_srli_epi32(xy,16), _mm256_and_si256(xy,v8_set(65535)),
        _mm256_srli_epi32(v8_bswap(z),16), _mm256_setzero_si256()}};
    return decode_geometry_host_halves8(halves,prefix,3);
}
#endif

// The arena begins on a 16-byte boundary. Admission covers each active
// field and its aligned prefix byte; no speculative row or wide overread.
inline GeometryHostSoA8 decode_geometry_host_half_stream8(const uint8_t* arena,
    unsigned field_offset, unsigned stride, unsigned components, unsigned vertices)
{
#if defined(__AVX2__) && !defined(__aarch64__)
    if (vertices == 8 && components == 3) {
        const v8 offsets = _mm256_mullo_epi32(_mm256_setr_epi32(0,1,2,3,4,5,6,7), v8_set(stride));
        const uint8_t* field = arena + field_offset;
        // Two overlapping loads cover exactly the six bytes of each XYZ.
        const v8 xy = v8_bswap(_mm256_i32gather_epi32(reinterpret_cast<const int*>(field), offsets, 1));
        const v8 yz = v8_bswap(_mm256_i32gather_epi32(reinterpret_cast<const int*>(field + 2), offsets, 1));
        const v8 prefix_at = _mm256_and_si256(_mm256_add_epi32(v8_set(field_offset), offsets), v8_set(~15u));
        // Each aligned word ends before the corresponding six-byte field ends.
        const v8 prefixes = _mm256_and_si256(_mm256_i32gather_epi32(
            reinterpret_cast<const int*>(arena), prefix_at, 1), v8_set(255));
        const GeometryHostSoA8 halves{{_mm256_srli_epi32(xy, 16), _mm256_and_si256(xy, v8_set(65535)),
                                       _mm256_and_si256(yz, v8_set(65535)), _mm256_setzero_si256()}};
        return decode_geometry_host_halves8(halves, prefixes, 3);
    }
#endif
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

#if BUZZ_GEOMETRY_MOBILE_KERNELS && defined(__aarch64__)
template<unsigned Stride>
inline GeometryHostSoA8 geometry_mobile_half_pair(const uint8_t* arena,unsigned offset)
{
    alignas(32) uint32_t words[8],prefixes[8];
    for (unsigned lane=0;lane<8;++lane) {
        const unsigned at=offset+lane*Stride;
        uint32_t word;
        memcpy(&word,arena+at,sizeof(word));
        words[lane]=__builtin_bswap32(word);
        prefixes[lane]=arena[at&~15u];
    }
    const v8 pair=_mm256_load_si256(reinterpret_cast<const v8*>(words));
    const GeometryHostSoA8 halves{{_mm256_srli_epi32(pair,16),_mm256_and_si256(pair,v8_set(65535)),{}, {}}};
    return decode_geometry_host_halves8(halves,_mm256_load_si256(reinterpret_cast<const v8*>(prefixes)),2);
}
#endif

// Decode a complete field while its stride and starting alignment are fixed.
// The scalar tail also clears every inactive lane and unused component.
inline void decode_geometry_host_half_stream(const uint8_t* arena, unsigned source, unsigned stride,
    unsigned components, unsigned vertices, GeometryHostSoA8* out)
{
    unsigned first = 0;
#if BUZZ_GEOMETRY_MOBILE_KERNELS && defined(__aarch64__)
    if (components==2 && stride==7) {
        for (;first+8<=vertices;first+=8)
            *out++=geometry_mobile_half_pair<7>(arena,source+7*first);
    } else if (components==2 && stride==10) {
        for (;first+8<=vertices;first+=8)
            *out++=geometry_mobile_half_pair<10>(arena,source+10*first);
    }
#endif
#if defined(__AVX2__) && !defined(__aarch64__)
    const unsigned full = vertices & ~7u;
    if (stride == 7 && components == 2) {
        for (; first < full; first += 8)
            *out++ = geometry_host_half_pairs7(arena, source + first * 7);
    } else if (stride == 10 && components == 2 && (source & 15u) == 3) {
        for (; first < full; first += 8)
            *out++ = geometry_host_half_pairs10(arena, source + first * 10);
    } else if (stride == 18 && components == 3) {
        for (; first < full; first += 8)
            *out++ = geometry_host_half_triples18(arena, source + first * 18);
    }
#endif
    for (; first < vertices; first += 8) {
        const unsigned active = vertices - first < 8 ? vertices - first : 8;
        *out++ = decode_geometry_host_half_stream8(arena, source + first * stride, stride, components, active);
    }
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
