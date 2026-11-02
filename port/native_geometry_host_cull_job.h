/* Eight host triangles per AVX2 batch, stable in-place index compaction.
 * Job metadata and clip records are read-only; only owned host indices
 * change. No SPU bank, LS pipeline, lookup compaction or ABI is reproduced. */
#pragma once
#include "native_geometry_host_clip_job.h"
#include "native_geometry_host_index_job.h"

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
    for (unsigned mask = 0; mask < 16; ++mask) {
        uint8_t selected[32]; memset(selected,128,sizeof selected); unsigned bytes = 0;
        for (unsigned triangle = 0; triangle < 4; ++triangle) if (!(mask & (8 >> triangle)))
            for (unsigned byte = 0; byte < 6; ++byte) selected[bytes++] = uint8_t(triangle * 6 + byte);
        if (memcmp(c.ls + 0xdaf0 + mask * 16,selected,16) || memcmp(c.ls + 0xdbf0 + mask * 16,selected + 16,16))
            return reject("host cull compaction definition");
        for (unsigned byte = 0; byte < 16; ++byte)
            if (c.ls[0xdcf0 + mask * 16 + byte] != 16 - mask + byte) return reject("host cull alignment definition");
    }
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
    unsigned kept = 0, maximum = 0, final_group_kept = 0;
    for (unsigned first = 0; first < indices.triangles; first += 8) {
        const unsigned active = indices.triangles - first < 8 ? indices.triangles - first : 8;
        v8 index[3], x[3], y[3], flags[3], packed[3];
        alignas(32) uint32_t values[3][8];
        for (unsigned corner = 0; corner < 3; ++corner) {
            index[corner] = _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(indices.component[corner] + first)));
            _mm256_store_si256(reinterpret_cast<v8*>(values[corner]),index[corner]);
            x[corner] = _mm256_i32gather_epi32(reinterpret_cast<const int*>(clips.component[0]),index[corner],4);
            y[corner] = _mm256_i32gather_epi32(reinterpret_cast<const int*>(clips.component[1]),index[corner],4);
            flags[corner] = _mm256_i32gather_epi32(reinterpret_cast<const int*>(clips.component[2]),index[corner],4);
            packed[corner] = _mm256_i32gather_epi32(reinterpret_cast<const int*>(clips.component[3]),index[corner],4);
        }
        const v8 area = v8_fs(v8_fm(v8_fs(x[1],x[0]),v8_fs(y[2],y[0])),v8_fm(v8_fs(y[1],y[0]),v8_fs(x[2],x[0])));
        const v8 same = _mm256_and_si256(_mm256_cmpeq_epi16(packed[0],packed[1]),_mm256_cmpeq_epi16(packed[0],packed[2]));
        const v8 zero = _mm256_or_si256(v8_fcmeq(area,_mm256_setzero_si256()),v8_nonzero(same));
        const v8 common = v8_nonzero(_mm256_and_si256(_mm256_and_si256(flags[0],flags[1]),flags[2]));
        const v8 back = _mm256_and_si256(_mm256_and_si256(v8_fcgt(_mm256_xor_si256(area,v8_set(params.winding)),_mm256_setzero_si256()),
            v8_clgt(v8_set(0x80000000),_mm256_or_si256(_mm256_or_si256(flags[0],flags[1]),flags[2]))),
            _mm256_srai_epi32(_mm256_and_si256(_mm256_and_si256(packed[0],packed[1]),packed[2]),31));
        // The original FNMS uses boolean float masks. Its low-bit truth
        // table is exactly common || (back && enabled), including -MAX.
        const v8 drop = _mm256_or_si256(zero,_mm256_or_si256(common,_mm256_and_si256(back,v8_set(params.mode == 1 ? 0 : -1))));
        const unsigned mask = unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(drop)));
        if (first + 8 >= indices.triangles) {
            const unsigned last = (active - 1) & ~3u;
            const unsigned live = (1u << (active - last)) - 1;
            final_group_kept = unsigned(_mm_popcnt_u32((~(mask >> last)) & live));
        }
        for (unsigned lane = 0; lane < active; ++lane) if (!(mask & (1u << lane))) {
            for (unsigned corner = 0; corner < 3; ++corner) {
                const unsigned value = values[corner][lane]; indices.component[corner][kept] = uint16_t(value);
                maximum = value > maximum ? value : maximum;
            }
            ++kept;
        }
    }
    for (unsigned triangle = kept; triangle < ((kept + 7) & ~7u); ++triangle)
        for (auto& component : indices.component) component[triangle] = 0;
    indices.triangles = kept; indices.maximum_index = maximum;
    indices.final_group_kept = final_group_kept; indices.ready = true;
    if (reason) *reason = nullptr; return true;
}
} // namespace buzz
