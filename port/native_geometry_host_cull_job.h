/* Eight host triangles per AVX2 batch, stable in-place index compaction.
 * Job metadata and clip records are read-only; only owned host indices
 * change. No SPU bank, LS pipeline, lookup compaction or ABI is reproduced. */
#pragma once
#include "native_geometry_host_clip_job.h"
#include "native_geometry_host_index_job.h"
#ifndef BUZZ_GEOMETRY_CULL_COMPACT
#define BUZZ_GEOMETRY_CULL_COMPACT 0
#endif

namespace buzz {
struct GeometryHostCullParameters { unsigned mode = 1; uint32_t winding = 0; };
inline bool read_geometry_host_cull_parameters(const spu_context& c, const GeometryJobEntry& e,
    GeometryHostCullParameters& output, const char** reason = nullptr)
{
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    if (c.ls != c.ls_store || (e.config[0] & 16)) return reject("host cull job family");
    GeometryHostCullParameters params;
    if (e.flags & 8) {
        if (e.flags & 32) params.mode = e.flags & 0x100 ? 3 : 2;
        else if (e.flags & 64) params.mode = e.flags & 0x100 ? 2 : 3;
    } else if (e.input[0].size_flags) {
        const auto& command = e.input[0];
        const uint64_t io_end = uint64_t(e.io_lsa) + e.io_bytes;
        if (command.lsa < e.io_lsa || command.size < 4 || uint64_t(command.lsa) + command.size > io_end || io_end > SPU_LS_SIZE)
            return reject("host cull command span");
        if (geometry_job_be32(c.ls + command.lsa) == 1) {
            if (command.size < 40) return reject("host cull command mode span");
            const unsigned mode = geometry_job_be32(c.ls + command.lsa + 36);
            if (mode == 2305) params.mode = e.flags & 0x100 ? 3 : 2;
            else if (mode == 2304) params.mode = e.flags & 0x100 ? 2 : 3;
        }
    }
    const unsigned orientation = e.config[6] >> 4;
    params.winding = (params.mode == 2 ? 0 : 0x80000000u) ^
        (orientation == 0 || orientation == 2 ? 0x80000000u : 0);
    struct Quad { unsigned at; uint32_t word[4]; };
    static constexpr Quad tables[] = {
        {0xd020,{0x00010203,0x10111213,0x80808080,0x80808080}},
        {0xd030,{0x08090a0b,0x18191a1b,0x80808080,0x80808080}},
        {0xd040,{0x0c0d0e0f,0x1c1d1e1f,0x80808080,0x80808080}},
        {0xd050,{0x00010203,0x04050607,0x10111213,0x14151617}},
        {0xd060,{0x04050607,0x00010203,0x14151617,0x10111213}},
        {0xd070,{0x00010203,0x08090a0b,0x10111213,0x18191a1b}},
        {0xd980,{0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f}},
        {0xcff0,{0x08080808,0x08080808,0x18181818,0x18181818}},
        {0xcfe0,{0xffffffef,0xffffffdf,0,0}},
        {0xdaa0,{0x18191a1b,0x1c1d1e1f,0x00010203,0x04050607}}};
    for (const auto& table : tables) for (unsigned lane = 0; lane < 4; ++lane)
        if (geometry_job_be32(c.ls + table.at + lane * 4) != table.word[lane]) return reject("host cull codec definition");
    // The expected codec is immutable; guest bytes are still checked on EVERY
    // job. Build the reference once at compile time, not in each worker call.
    static constexpr auto compaction = [] {
        std::array<uint8_t, 768> expected{};
        for (unsigned mask = 0; mask < 16; ++mask) {
            uint8_t selected[32]{}; for (auto& byte : selected) byte = 128;
            unsigned bytes = 0;
            for (unsigned triangle = 0; triangle < 4; ++triangle) if (!(mask & (8 >> triangle)))
                for (unsigned byte = 0; byte < 6; ++byte) selected[bytes++] = uint8_t(triangle * 6 + byte);
            for (unsigned byte = 0; byte < 16; ++byte) {
                expected[mask * 16 + byte] = selected[byte];
                expected[256 + mask * 16 + byte] = selected[16 + byte];
                expected[512 + mask * 16 + byte] = uint8_t(16 - mask + byte);
            }
        }
        return expected;
    }();
    if (memcmp(c.ls + 0xdaf0, compaction.data(), 512)) return reject("host cull compaction definition");
    if (memcmp(c.ls + 0xdcf0, compaction.data() + 512, 256)) return reject("host cull alignment definition");
    output = params; if (reason) *reason = nullptr; return true;
}

inline bool cull_geometry_host_triangles(const spu_context& c, const GeometryJobEntry& e,
    const GeometryHostClipData& clips, GeometryHostIndexData& indices, const char** reason = nullptr)
{
    const bool ready = indices.ready; indices.ready = false;
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    if (!ready || !clips.ready || clips.vertices != e.vertices || !clips.vertices || clips.vertices > 4096 ||
        indices.triangles > GeometryHostIndexData::capacity || indices.triangles * 3 != e.index_count ||
        indices.maximum_index >= clips.vertices) return reject("host cull source readiness/range");
    GeometryHostCullParameters params;
    if (!read_geometry_host_cull_parameters(c,e,params,reason)) return false;
#if BUZZ_GEOMETRY_CULL_COMPACT
    static constexpr auto compact = [] {
        std::array<std::array<uint8_t,16>,256> table{};
        for (unsigned mask = 0; mask < 256; ++mask) {
            auto& row = table[mask]; for (auto& byte : row) byte = 128;
            unsigned out = 0;
            for (unsigned lane = 0; lane < 8; ++lane) if (mask & (1u << lane)) {
                row[out++] = uint8_t(lane * 2); row[out++] = uint8_t(lane * 2 + 1);
            }
        }
        return table;
    }();
#endif
    unsigned kept = 0, maximum = 0, final_group_kept = 0;
#if BUZZ_GEOMETRY_CULL_COMPACT
    __m128i maximum_lanes = _mm_setzero_si128();
#endif
    for (unsigned first = 0; first < indices.triangles; first += 8) {
        const unsigned active = indices.triangles - first < 8 ? indices.triangles - first : 8;
        v8 index[3], x[3], y[3], flags[3], packed[3];
#if BUZZ_GEOMETRY_CULL_COMPACT
        __m128i source[3];
#else
        alignas(32) uint32_t values[3][8];
#endif
        for (unsigned corner = 0; corner < 3; ++corner) {
#if BUZZ_GEOMETRY_CULL_COMPACT
            source[corner] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(indices.component[corner] + first));
            index[corner] = _mm256_cvtepu16_epi32(source[corner]);
#else
            index[corner] = _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(indices.component[corner] + first)));
#endif
            flags[corner] = _mm256_i32gather_epi32(reinterpret_cast<const int*>(clips.component[2]),index[corner],4);
        }
        const v8 common = v8_nonzero(_mm256_and_si256(_mm256_and_si256(flags[0],flags[1]),flags[2]));
        const unsigned live_mask = (1u << active) - 1;
        // Entire batches outside a common clip plane need no packed XY or
        // position gathers. Inspect only active lanes in a partial batch.
        if ((unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(common))) & live_mask) == live_mask) {
            if (first + 8 >= indices.triangles) final_group_kept = 0;
            continue;
        }
        for (unsigned corner = 0; corner < 3; ++corner)
            packed[corner] = _mm256_i32gather_epi32(reinterpret_cast<const int*>(clips.component[3]),index[corner],4);
        const v8 same = _mm256_and_si256(_mm256_cmpeq_epi16(packed[0],packed[1]),_mm256_cmpeq_epi16(packed[0],packed[2]));
        const v8 early_drop = _mm256_or_si256(common, v8_nonzero(same));
        if ((unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(early_drop))) & live_mask) == live_mask) {
            if (first + 8 >= indices.triangles) final_group_kept = 0;
            continue;
        }
        for (unsigned corner = 0; corner < 3; ++corner) {
            x[corner] = _mm256_i32gather_epi32(reinterpret_cast<const int*>(clips.component[0]),index[corner],4);
            y[corner] = _mm256_i32gather_epi32(reinterpret_cast<const int*>(clips.component[1]),index[corner],4);
        }
        const v8 area = v8_fs(v8_fm(v8_fs(x[1],x[0]),v8_fs(y[2],y[0])),v8_fm(v8_fs(y[1],y[0]),v8_fs(x[2],x[0])));
        v8 back = _mm256_setzero_si256();
        if (params.mode != 1)
            back = _mm256_and_si256(_mm256_and_si256(v8_fcgt(_mm256_xor_si256(area,v8_set(params.winding)),_mm256_setzero_si256()),
                v8_clgt(v8_set(0x80000000),_mm256_or_si256(_mm256_or_si256(flags[0],flags[1]),flags[2]))),
                _mm256_srai_epi32(_mm256_and_si256(_mm256_and_si256(packed[0],packed[1]),packed[2]),31));
        // The original FNMS uses boolean float masks. Its low-bit truth
        // table is exactly common || (back && enabled), including -MAX.
        const v8 drop = _mm256_or_si256(early_drop, _mm256_or_si256(v8_fcmeq(area,_mm256_setzero_si256()), back));
        const unsigned mask = unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(drop)));
#if !BUZZ_GEOMETRY_CULL_COMPACT
        for (unsigned corner = 0; corner < 3; ++corner)
            _mm256_store_si256(reinterpret_cast<v8*>(values[corner]),index[corner]);
#endif
        if (first + 8 >= indices.triangles) {
            const unsigned last = (active - 1) & ~3u;
            const unsigned live = (1u << (active - last)) - 1;
            final_group_kept = unsigned(_mm_popcnt_u32((~(mask >> last)) & live));
        }
#if BUZZ_GEOMETRY_CULL_COMPACT
        const unsigned selected = ~mask & live_mask;
        if (!selected) continue;
        const __m128i shuffle = _mm_loadu_si128(reinterpret_cast<const __m128i*>(compact[selected].data()));
        // Inputs are already loaded and kept <= first. Padded capacity makes
        // every eight-lane store safe without overwriting an unread group.
        for (unsigned corner = 0; corner < 3; ++corner) {
            const __m128i values = _mm_shuffle_epi8(source[corner], shuffle);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(indices.component[corner] + kept), values);
            maximum_lanes = _mm_max_epu16(maximum_lanes, values);
        }
        kept += unsigned(_mm_popcnt_u32(selected));
#else
        for (unsigned lane = 0; lane < active; ++lane) if (!(mask & (1u << lane))) {
            for (unsigned corner = 0; corner < 3; ++corner) {
                const unsigned value = values[corner][lane]; indices.component[corner][kept] = uint16_t(value);
                maximum = value > maximum ? value : maximum;
            }
            ++kept;
        }
#endif
    }
#if BUZZ_GEOMETRY_CULL_COMPACT
    uint16_t maxima[8]; _mm_storeu_si128(reinterpret_cast<__m128i*>(maxima), maximum_lanes);
    for (unsigned value : maxima) maximum = value > maximum ? value : maximum;
#endif
    for (unsigned triangle = kept; triangle < ((kept + 7) & ~7u); ++triangle)
        for (auto& component : indices.component) component[triangle] = 0;
    indices.triangles = kept; indices.maximum_index = maximum;
    indices.final_group_kept = final_group_kept; indices.ready = true;
    if (reason) *reason = nullptr; return true;
}
} // namespace buzz
