/* Skinning and object transforms on eight host vertices, without SPU ABI or matrix
 * scratch. Palette and influences are read once before job LS is reused.
 * Only logical vertices are handled here; publication tails are separate. */
#pragma once
#include "native_geometry_host_input_job.h"

namespace buzz {
struct GeometryHostSkinRecipe {
    unsigned palette = 0, matrices = 0, weights = 0;
    std::array<unsigned, 4> field{{16,16,16,16}};
    bool active = false, object = false;
};
struct GeometryHostSkinWorkspace {
    alignas(16) uint32_t object[12];
    // Original packed palette words, little-endian and ready for AVX2 gather.
    alignas(32) uint32_t coefficient[12][256];
};
inline bool geometry_host_skin_codec_matches(const spu_context& c, bool object)
{
    struct Quad { unsigned at, modes; uint32_t word[4]; };
    static constexpr Quad tables[] = {
        {0xd930,3,{0x00010203,0x10111213,0x04050607,0x14151617}},
        {0xcd60,1,{0,0,0,0x3f800000}},
        {0xdac0,1,{0x0c0d0e0f,0x1c1d1e1f,0x08090a0b,0x18191a1b}},
        {0xdaa0,1,{0x18191a1b,0x1c1d1e1f,0x00010203,0x04050607}},
        {0xda70,1,{0x08090a0b,0x18191a1b,0x08090a0b,0x18191a1b}},
        {0xd920,1,{0x00010203,0x00010203,0x00010203,0x10111213}},
        {0xd900,2,{0x80808080,0x0c0d0e0f,0x1c1d1e1f,0x80808080}},
        {0xdab0,2,{0x1c1d1e1f,0x04050607,0x08090a0b,0x80808080}},
        {0xda00,2,{0x04050607,0x08090a0b,0x00010203,0x0c0d0e0f}},
        {0xd940,2,{0x00010203,0x10111213,0x08090a0b,0x18191a1b}},
        {0xd890,2,{0x80808000,0x80808002,0x80808004,0x80808006}}};
    for (const auto& table : tables) if (table.modes & (object ? 1 : 2))
        for (unsigned lane = 0; lane < 4; ++lane)
            if (geometry_job_be32(c.ls + table.at + lane * 4) != table.word[lane]) return false;
    return true;
}
inline bool read_geometry_host_skin_recipe(const spu_context& c, const GeometryJobEntry& e,
    const GeometryHostInputRecipe& input, GeometryHostSkinRecipe& output, const char** reason = nullptr)
{
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    if (c.ls != c.ls_store || input.vertices != e.vertices || !e.vertices || (e.config[0] & 16))
        return reject("host skin input family");
    GeometryHostSkinRecipe recipe;
    for (unsigned field = 0; field < input.fields; ++field) {
        const unsigned semantic = input.field[field].semantic;
        if (semantic >= 1 && semantic <= 4) recipe.field[semantic - 1] = field;
    }
    if (e.flags & 0x10000000) {
        const auto& object = e.input[GeometryJobEntry::object];
        if ((object.lsa & 15) || object.size < 48 || object.lsa < e.io_lsa ||
            uint64_t(object.lsa) + 48 > uint64_t(e.io_lsa) + e.io_bytes)
            return reject("host object matrix span");
        recipe.palette = object.lsa; recipe.object = true;
        if (recipe.field[0] == 16 || input.field[recipe.field[0]].storage != 2)
            return reject("host object position source");
        for (unsigned semantic = 1; semantic < 3; ++semantic)
            if (recipe.field[semantic] != 16 && input.field[recipe.field[semantic]].storage != 9)
                return reject("host object direction source");
        if (!geometry_host_skin_codec_matches(c, true)) return reject("host object codec definition");
        output = recipe; if (reason) *reason = nullptr; return true;
    }
    const unsigned matrices = (e.input[4].size_flags + e.input[5].size_flags) / 48;
    if (e.skin_flavor() && matrices) {
        if (e.skin_flavor() != 1 || !(e.config[0] & 128)) return reject("host skin mode");
        recipe.palette = e.input[4].lsa; recipe.weights = e.input[6].lsa;
        recipe.matrices = matrices < 256 ? matrices : 256;
        const uint64_t palette_end = uint64_t(e.input[5].lsa) + e.input[5].size;
        const uint64_t io_end = uint64_t(e.io_lsa) + e.io_bytes;
        if ((recipe.palette & 15) || (recipe.weights & 15) || recipe.palette < e.io_lsa || recipe.weights < e.io_lsa ||
            uint64_t(recipe.palette) + uint64_t(matrices) * 48 > palette_end || palette_end > io_end ||
            uint64_t(e.vertices) * 8 > e.input[6].size || uint64_t(recipe.weights) + uint64_t(e.vertices) * 8 > io_end)
            return reject("host skin palette/weight span");
        if (recipe.field[0] == 16 || input.field[recipe.field[0]].storage != 2)
            return reject("host skin position source");
        for (unsigned semantic = 1; semantic < 4; ++semantic)
            if (recipe.field[semantic] != 16 && input.field[recipe.field[semantic]].storage != 9)
                return reject("host skin direction source");
        if (geometry_job_be32(c.ls + 0xda10) != 0x14151617 || geometry_job_be32(c.ls + 0xda14) != 0x08090a0b ||
            geometry_job_be32(c.ls + 0xda18) != 0x10111213 || geometry_job_be32(c.ls + 0xda1c) != 0x0c0d0e0f ||
            geometry_job_be32(c.ls + 0xda60) != 0x18191a1b || geometry_job_be32(c.ls + 0xda64) != 0x10111213 ||
            geometry_job_be32(c.ls + 0xda68) != 0x04050607 || geometry_job_be32(c.ls + 0xda6c) != 0x0c0d0e0f)
            return reject("host skin palette codec definition");
        if (!geometry_host_skin_codec_matches(c, false)) return reject("host weighted codec definition");
        recipe.active = true;
    }
    output = recipe; if (reason) *reason = nullptr; return true;
}

inline void transform_geometry_host_object_value(GeometryHostSoA8& value,
    const v8* matrix, bool direction, v8 mask)
{
    const v8 x = value.component[0], y = value.component[1], z = value.component[2], w = value.component[3];
    // Preserve each coordinate's cyclic order, including zero
    // translation FMAs for directions. They affect SPU rounding.
    const v8 zero = _mm256_setzero_si256();
    v8 a = v8_fm(x, matrix[0]); a = v8_fma(y, matrix[1], a); a = v8_fma(z, matrix[2], a);
    a = v8_fma(w, direction ? zero : matrix[3], a);
    v8 b = v8_fm(y, matrix[5]); b = v8_fma(z, matrix[6], b);
    b = v8_fma(w, direction ? zero : matrix[7], b); b = v8_fma(x, matrix[4], b);
    v8 d = v8_fm(z, matrix[10]); d = v8_fma(w, direction ? zero : matrix[11], d);
    d = v8_fma(x, matrix[8], d); d = v8_fma(y, matrix[9], d);
    if (direction) {
        v8 length = v8_fm(a, a); length = v8_fma(b, b, length); length = v8_fma(d, d, length);
        const v8 inverse = v8_fi(length, v8_frsqest(length));
        a = v8_fm(a, inverse); b = v8_fm(b, inverse); d = v8_fm(d, inverse);
    }
    value.component[0] = _mm256_and_si256(a, mask);
    value.component[1] = _mm256_and_si256(b, mask);
    value.component[2] = _mm256_and_si256(d, mask);
}

inline void transform_geometry_host_weighted_value(GeometryHostSoA8& value,
    const v8* blended, bool point, v8 mask)
{
    const v8 x = value.component[0], y = value.component[1], z = value.component[2];
    v8 a = point ? v8_fma(x, blended[0], blended[3]) : v8_fm(x, blended[0]);
    v8 b = point ? v8_fma(y, blended[5], blended[7]) : v8_fm(y, blended[5]);
    v8 d = point ? v8_fma(z, blended[10], blended[11]) : v8_fm(z, blended[10]);
    a = v8_fma(y, blended[1], a); a = v8_fma(z, blended[2], a);
    b = v8_fma(z, blended[6], b); b = v8_fma(x, blended[4], b);
    d = v8_fma(x, blended[8], d); d = v8_fma(y, blended[9], d);
    value.component[0] = _mm256_and_si256(a, mask);
    value.component[1] = _mm256_and_si256(b, mask);
    value.component[2] = _mm256_and_si256(d, mask);
}

inline bool skin_geometry_host_inputs(const spu_context& c, const GeometryJobEntry& e,
    const GeometryHostInputRecipe& input, GeometryHostInputData& data,
    GeometryHostSkinWorkspace& workspace, const char** reason = nullptr)
{
    if (!data.ready) { if (reason) *reason = "host skin input not ready"; return false; }
    data.ready = false;
    GeometryHostSkinRecipe recipe;
    if (!read_geometry_host_skin_recipe(c, e, input, recipe, reason)) return false;
    if (recipe.object) {
        v8 matrix[12];
        for (unsigned coefficient = 0; coefficient < 12; ++coefficient) {
            workspace.object[coefficient] = geometry_job_be32(c.ls + recipe.palette + coefficient * 4);
            matrix[coefficient] = v8_set(workspace.object[coefficient]);
        }
        for (unsigned first = 0; first < input.vertices; first += 8) {
            const unsigned active = input.vertices - first < 8 ? input.vertices - first : 8;
            const v8 mask = _mm256_cmpgt_epi32(v8_set(active), _mm256_setr_epi32(0,1,2,3,4,5,6,7));
            for (unsigned semantic = 0; semantic < 3; ++semantic) {
                if (recipe.field[semantic] == 16 || (semantic && e.position_only())) continue;
                auto& value = data.at(input, recipe.field[semantic], first / 8);
                transform_geometry_host_object_value(value,matrix,semantic!=0,mask);
            }
        }
        data.ready = true; if (reason) *reason = nullptr; return true;
    }
    if (!recipe.active) { data.ready = true; return true; }
    for (unsigned matrix = 0; matrix < recipe.matrices; ++matrix)
        for (unsigned coefficient = 0; coefficient < 12; ++coefficient)
            workspace.coefficient[coefficient][matrix] = geometry_job_be32(c.ls + recipe.palette + matrix * 48 + coefficient * 4);
    for (unsigned first = 0; first < input.vertices; first += 8) {
        const unsigned active = input.vertices - first < 8 ? input.vertices - first : 8;
        alignas(32) uint32_t indices[4][8]{}, weights[4][8]{};
        for (unsigned lane = 0; lane < active; ++lane) for (unsigned influence = 0; influence < 4; ++influence) {
            const auto* at = c.ls + recipe.weights + (first + lane) * 8 + influence * 2;
            weights[influence][lane] = at[0]; indices[influence][lane] = at[1];
        }
        v8 index[4], factor[4];
        for (unsigned influence = 0; influence < 4; ++influence) {
            index[influence] = _mm256_load_si256(reinterpret_cast<const v8*>(indices[influence]));
            if (_mm256_movemask_epi8(_mm256_cmpgt_epi32(index[influence], v8_set(recipe.matrices - 1)))) {
                if (reason) *reason = "host skin palette index"; return false;
            }
            factor[influence] = v8_fm(_mm256_castps_si256(_mm256_cvtepi32_ps(
                _mm256_load_si256(reinterpret_cast<const v8*>(weights[influence])))), v8_set(0x3b808080));
        }
        v8 blended[12];
        for (unsigned coefficient = 0; coefficient < 12; ++coefficient) {
            const auto* source = reinterpret_cast<const int*>(workspace.coefficient[coefficient]);
            blended[coefficient] = v8_fm(factor[0], _mm256_i32gather_epi32(source, index[0], 4));
            for (unsigned influence = 1; influence < 4; ++influence)
                blended[coefficient] = v8_fma(factor[influence], _mm256_i32gather_epi32(source, index[influence], 4), blended[coefficient]);
        }
        const v8 mask = _mm256_cmpgt_epi32(v8_set(active), _mm256_setr_epi32(0,1,2,3,4,5,6,7));
        transform_geometry_host_weighted_value(data.at(input, recipe.field[0], first / 8),blended,true,mask);
        for (unsigned semantic = 1; semantic < 4; ++semantic) if (recipe.field[semantic] != 16)
            transform_geometry_host_weighted_value(data.at(input, recipe.field[semantic], first / 8),blended,false,mask);
    }
    data.ready = true; if (reason) *reason = nullptr; return true;
}
} // namespace buzz
