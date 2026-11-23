/* Serialize the final culled index stream once, including all alignment
 * bytes. The original final quad store can repeat bytes of the last retained
 * group; only that published tail is reproduced, without its LS pipeline. */
#pragma once
#include "native_geometry_host_index_job.h"
#if defined(__SSSE3__) && !defined(__aarch64__)
#include <tmmintrin.h>
#endif

namespace buzz {
inline bool emit_geometry_host_indices(const GeometryHostIndexData& indices,
    uint8_t* destination, size_t capacity, const char** reason = nullptr)
{
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    if (!indices.ready || indices.triangles > 65535 / 3 ||
        indices.final_group_kept > 4 || indices.final_group_kept > indices.triangles)
        return reject("host index output readiness/range");
    const unsigned logical = indices.triangles * 6, bytes = (logical + 15) & ~15u;
    if (capacity < bytes || (bytes && !destination)) return reject("host index output span");
    unsigned triangle = 0;
#if defined(__SSSE3__) && !defined(__aarch64__)
    // Each 16-byte result contains eight BE indices in A,B,C order.
    // In each mask lane select the source byte from exactly one corner.
    static constexpr auto masks = [] {
        std::array<std::array<std::array<uint8_t,16>,3>,3> m{};
        for (unsigned packet=0;packet<3;++packet)
            for (unsigned corner=0;corner<3;++corner)
                for (unsigned byte=0;byte<16;++byte) {
                    const unsigned half=packet*8+byte/2;
                    m[packet][corner][byte]=half%3==corner ? (half/3)*2+1-byte%2 : 128;
                }
        return m;
    }();
    for (; triangle+8<=indices.triangles; triangle+=8) {
        __m128i source[3];
        for (unsigned k=0;k<3;++k)
            source[k]=_mm_loadu_si128(reinterpret_cast<const __m128i*>(indices.component[k]+triangle));
        for (const auto& packet : masks) {
            __m128i result = _mm_setzero_si128();
            for (unsigned k=0;k<3;++k)
                result=_mm_or_si128(result,_mm_shuffle_epi8(source[k],
                    _mm_loadu_si128(reinterpret_cast<const __m128i*>(packet[k].data()))));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(destination),result);
            destination+=16;
        }
    }
#endif
    for (; triangle < indices.triangles; ++triangle)
        for (unsigned corner = 0; corner < 3; ++corner) {
            const unsigned value = indices.component[corner][triangle];
            *destination++ = uint8_t(value >> 8); *destination++ = uint8_t(value);
        }
    // The last group has at most 24 packed bytes, followed by zeros. The
    // original third quad repeats the packet's second 16-byte half. Earlier
    // quads supply only zeros after the logical end. Compute those <=15
    // observable bytes directly from the retained host indices.
    const unsigned start = (indices.triangles - indices.final_group_kept) * 6;
    for (unsigned at = logical; at < bytes; ++at) {
        unsigned value = 0;
        const unsigned local = at - (start & ~15u);
        if (local >= 32) {
            const unsigned packet_byte = 16 + ((local - (start & 15)) & 15);
            if (packet_byte < indices.final_group_kept * 6) {
                const unsigned half = packet_byte / 2;
                const unsigned word = indices.component[half % 3]
                    [indices.triangles - indices.final_group_kept + half / 3];
                value = packet_byte & 1 ? word & 255 : word >> 8;
            }
        }
        *destination++ = uint8_t(value);
    }
    if (reason) *reason = nullptr; return true;
}
} // namespace buzz
