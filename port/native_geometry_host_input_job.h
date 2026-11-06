/* One-pass Edge input capture into job-owned host SoA. Metadata admission
 * and payload capture do not change LS, registers or external I/O. A failed
 * capture discards host data before the whole job falls back to the lift.
 * This captures logical vertices; publication-tail provenance is separate. */
#pragma once
#include "native_geometry_job_input.h"
#include "native_geometry_host_streams.h"
#include "native_geometry_host_unit_vectors.h"

namespace buzz {
struct GeometryHostInputField {
    unsigned source = 0, stride = 0, storage = 0, components = 0, semantic = 0;
};
struct GeometryHostInputRecipe {
    std::array<GeometryHostInputField, 16> field{};
    unsigned fields = 0, vertices = 0, blocks_per_field = 0;
};
struct GeometryHostInputData {
    // Same bytes per vertex as float4 scratch, bounded by the entire LS.
    // Leave unused blocks alone: no hot-path allocation or bank-sized copy.
    static constexpr unsigned capacity = SPU_LS_SIZE / sizeof(GeometryHostSoA8);
    std::array<GeometryHostSoA8, capacity> block;
    bool ready = false;
    GeometryHostSoA8& at(const GeometryHostInputRecipe& recipe, unsigned field, unsigned batch)
    { return block[field * recipe.blocks_per_field + batch]; }
    const GeometryHostSoA8& at(const GeometryHostInputRecipe& recipe, unsigned field, unsigned batch) const
    { return block[field * recipe.blocks_per_field + batch]; }
};

inline bool read_geometry_host_input_recipe(const spu_context& c, const GeometryJobEntry& e,
    GeometryHostInputRecipe& output, const char** reason = nullptr)
{
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    if (c.ls != c.ls_store || !e.vertices || e.vertices > 4096 || (e.config[0] & 16) || e.config[2] != 0)
        return reject("host input job family");
    const uint64_t io_end = uint64_t(e.io_lsa) + e.io_bytes;
    if (io_end > SPU_LS_SIZE) return reject("host input IO range");
    GeometryHostInputRecipe recipe;
    recipe.vertices = e.vertices; recipe.blocks_per_field = (e.vertices + 7) / 8;
    auto stream_end = [&](unsigned first) {
        uint64_t end = e.input[first].lsa;
        // A logical stream can span all three staged DMA entries. Never
        // mistake the 16 KiB first chunk for the entire vertex stream.
        for (unsigned i = first; i < first + 3; ++i) if (e.input[i].size) {
            const uint64_t last = uint64_t(e.input[i].lsa) + e.input[i].size;
            if (e.input[i].lsa < e.io_lsa || last > io_end) return uint64_t(0);
            if (last > end) end = last;
        }
        return end;
    };
    auto append = [&](unsigned first, unsigned offset, unsigned stride, unsigned storage,
        unsigned components, unsigned width, unsigned semantic) {
        if (!semantic || recipe.fields >= recipe.field.size() || !stride || offset + width > stride) return false;
        const uint64_t source = uint64_t(e.input[first].lsa) + offset;
        const uint64_t end = source + uint64_t(e.vertices - 1) * stride + width;
        if (source < e.io_lsa || end > stream_end(first)) return false;
        recipe.field[recipe.fields++] = {unsigned(source), stride, storage, components, semantic};
        return true;
    };
    if ((e.input[8].lsa & 15) || !append(8, 0, 12, 2, 3, 12, 1)) return reject("host primary staged span");
    bool half = false, unit = false;
    if (e.input[11].size_flags) {
        if (e.config[3] < 6) return reject("host secondary builtin format");
        const auto& d = e.input[20];
        if ((e.input[11].lsa & 15) || (d.lsa & 15) || d.size < 8 || d.lsa < e.io_lsa || uint64_t(d.lsa) + d.size > io_end)
            return reject("host secondary descriptor span");
        const auto* descriptor = c.ls + d.lsa;
        const unsigned count = descriptor[0], stride = descriptor[1];
        if (count > 16 || !stride || 8 + count * 8 > d.size) return reject("host secondary descriptor header");
        for (unsigned row = 0; row < count; ++row) {
            const auto* a = descriptor + 8 + row * 8;
            const unsigned storage = a[1], components = a[2], width = a[4];
            if (!a[3]) return reject("host unsupported input semantic");
            if (storage == 3 && components && components <= 4 && width == components * 2) half = true;
            else if (storage == 9 && (components == 3 || components == 4) && width == 3) unit = true;
            else {
                static constexpr const char* storage_reason[] = {
                "host unsupported input storage 0",
                "host unsupported input storage 1",
                "host unsupported input storage 2",
                "host unsupported input storage 3",
                "host unsupported input storage 4",
                "host unsupported input storage 5",
                "host unsupported input storage 6",
                "host unsupported input storage 7",
                "host unsupported input storage 8",
                "host unsupported input storage 9",
                "host unsupported input storage 10",
                "host unsupported input storage 11",
                "host unsupported input storage 12",
                "host unsupported input storage 13",
                "host unsupported input storage 14",
                "host unsupported input storage 15",
                };
                return reject(storage < 16 ? storage_reason[storage] : "host unsupported input storage");
            }
            if (!append(11, a[0], stride, storage, components, width, a[3])) return reject("host secondary staged span");
        }
    }
    if (recipe.fields > e.uniform_tables() || uint64_t(recipe.fields) * recipe.blocks_per_field * sizeof(GeometryHostSoA8) > e.scratch_bytes ||
        recipe.fields * recipe.blocks_per_field > GeometryHostInputData::capacity)
        return reject("host input SoA capacity");
    auto quad = [&](unsigned at, uint32_t a, uint32_t b, uint32_t d, uint32_t f) {
        return geometry_job_be32(c.ls + at) == a && geometry_job_be32(c.ls + at + 4) == b &&
            geometry_job_be32(c.ls + at + 8) == d && geometry_job_be32(c.ls + at + 12) == f;
    };
    if (!quad(0xd250,0x04050607,0x08090a0b,0x0c0d0e0f,0x10111213) ||
        !quad(0xd260,0x08090a0b,0x0c0d0e0f,0x10111213,0x14151617) ||
        !quad(0xd270,0x0c0d0e0f,0x10111213,0x14151617,0x18191a1b)) return reject("host raw input codec definition");
    if (half && (!quad(0xd5d0,0x03030303,0x03030303,0x03030303,0x03030303) ||
        !quad(0xd5e0,0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f) ||
        !quad(0xd5f0,0x00010203,0x00010203,0x00010203,0x00010203) ||
        !quad(0xd600,0x00018080,0x02038080,0x04058080,0x06078080))) return reject("host half input codec definition");
    if (unit) {
        if (!quad(0xd1d0,0xfffffffc,0xfffffffa,0xfffffffc,0) || !quad(0xd1e0,0xf0,0x3ff,0x3ff,0) ||
            !quad(0xd1f0,0,0x3f800000,0x3f800000,0) || !quad(0xd200,0,0,0,0x0c0d0e0f) ||
            !quad(0xd210,0x7fffffff,0x7fffffff,0x7fffffff,0) || !quad(0xd220,0x80000000,0x80000000,0x80000000,0xffffffff) ||
            !quad(0xd980,0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f) ||
            !quad(0xda20,0x04050607,0x08090a0b,0x14151617,0x18191a1b) ||
            !quad(0xd8b0,0x80800280,0x80800001,0x80800102,0x80808080)) return reject("host unit input codec definition");
        static constexpr uint32_t order[3][3] = {{0x00010203,0x10111213,0x14151617},
            {0x10111213,0x00010203,0x14151617},{0x10111213,0x14151617,0x00010203}};
        for (unsigned selector = 0; selector < 12; ++selector) {
            const auto* row = order[selector >> 2]; const unsigned sign = selector & 1 ? 0 : 0x80000000;
            if (!quad(0xddf0 + selector * 16, row[0] | sign, row[1], row[2] | sign,
                selector & 2 ? 0x3f800000 : 0xbf800000)) return reject("host unit selector definition");
        }
    }
    output = recipe; if (reason) *reason = nullptr; return true;
}

inline bool capture_geometry_host_inputs(const spu_context& c, const GeometryHostInputRecipe& recipe,
    GeometryHostInputData& data, const char** reason = nullptr)
{
    data.ready = false;
    for (unsigned field = 0; field < recipe.fields; ++field) {
        const auto& f = recipe.field[field];
        for (unsigned first = 0; first < recipe.vertices; first += 8) {
            const unsigned active = recipe.vertices - first < 8 ? recipe.vertices - first : 8;
            auto& output = data.at(recipe, field, first / 8);
            const unsigned source = f.source + first * f.stride;
            if (f.storage == 2) output = decode_geometry_host_positions8(c.ls + source, active);
            else if (f.storage == 3) output = decode_geometry_host_half_stream8(c.ls, source, f.stride, f.components, active);
            else {
                const v8 input = geometry_host_unit_words8(c.ls+source,f.stride,active);
                if (_mm256_movemask_epi8(_mm256_cmpgt_epi32(_mm256_and_si256(input, v8_set(15)), v8_set(11)))) {
                    if (reason) *reason = "host unsupported unit selector"; return false;
                }
                output = decode_geometry_host_unit8(input);
                const v8 mask = _mm256_cmpgt_epi32(v8_set(active), _mm256_setr_epi32(0,1,2,3,4,5,6,7));
                for (auto& component : output.component) component = _mm256_and_si256(component, mask);
            }
        }
    }
    data.ready = true; if (reason) *reason = nullptr; return true;
}

inline bool prepare_geometry_host_inputs(const spu_context& c, const GeometryJobEntry& e,
    GeometryHostInputRecipe& output, GeometryHostInputData& data, const char** reason = nullptr)
{
    data.ready = false;
    GeometryHostInputRecipe recipe;
    if (!read_geometry_host_input_recipe(c, e, recipe, reason) ||
        !capture_geometry_host_inputs(c, recipe, data, reason)) return false;
    output = recipe; return true;
}
} // namespace buzz
