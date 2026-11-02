/* Transitional job adapter for the host output serializer. Recognition and
 * descriptor capture happen before the job mutates LS. Producers currently
 * leave float4 tables in scratch; subsequent 3b work moves those producers
 * directly onto the same host SoA representation. */
#pragma once
#include "native_geometry_context.h"
#include "native_geometry_host_output.h"
#include "native_geometry_job.h"

namespace buzz {
struct GeometryHostOutputField {
    unsigned offset = 0, storage = 0, components = 0, slot = 0;
};
struct GeometryHostOutputRecipe {
    std::array<GeometryHostOutputField, 16> field{};
    unsigned fields = 0, stride = 0, vertices = 0, bytes = 0;
};
inline bool read_geometry_host_output_recipe(const spu_context& c, const GeometryJobEntry& e,
    GeometryHostOutputRecipe& output, const char** reason = nullptr)
{
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    if (c.ls != c.ls_store || !e.vertices || e.vertices > 4096 || (e.config[0] & 16) ||
        e.config[2] != 0 || e.config[4] < 4 || e.skin_flavor() > 1 || e.index_flavor() != 3)
        return reject("host output job family");
    // Predict the semantic slots allocated by the recognized primary float3
    // stream and optional secondary descriptor. No payload decoding or bank
    // clone is needed here. The ordinary input-layout proof runs first.
    std::array<unsigned, 16> semantics{}; semantics[0] = 1; unsigned next = 1;
    if (e.input[11].size_flags) {
        if (e.config[3] == 0) semantics[next++] = 1;
        else if (e.config[3] >= 6) {
            const auto& descriptor = e.input[20];
            if ((descriptor.lsa & 15) || descriptor.size < 8 || descriptor.lsa > SPU_LS_SIZE - descriptor.size)
                return reject("host secondary descriptor span");
            const unsigned count = c.ls[descriptor.lsa];
            if (count > 16 || 8 + count * 8 > descriptor.size) return reject("host secondary descriptor count");
            for (unsigned row = 0; row < count; ++row) {
                const unsigned semantic = c.ls[descriptor.lsa + 8 + row * 8 + 3];
                if (next >= semantics.size()) return reject("host semantic slot capacity");
                semantics[next] = semantic; if (semantic) ++next;
            }
        } else return reject("host secondary builtin family");
    }
    if (next > e.uniform_tables()) return reject("host semantic table capacity");
    const auto& descriptor = e.input[1];
    if ((descriptor.lsa & 15) || descriptor.size < 16 || descriptor.lsa > SPU_LS_SIZE - descriptor.size)
        return reject("host output descriptor span");
    GeometryHostOutputRecipe recipe;
    recipe.fields = e.position_only() ? 1 : c.ls[descriptor.lsa];
    recipe.stride = e.position_only() ? 12 : c.ls[descriptor.lsa + 1];
    recipe.vertices = e.vertices; recipe.bytes = (e.vertices * recipe.stride + 15) & ~15u;
    if (!recipe.fields || recipe.fields > recipe.field.size() || 8 + recipe.fields * 8 > descriptor.size ||
        recipe.stride < 12 || recipe.stride > 64) return reject("host output header");
    uint64_t occupied = 0;
    for (unsigned row = 0; row < recipe.fields; ++row) {
        const auto* bytes = c.ls + descriptor.lsa + 8 + row * 8;
        auto& field = recipe.field[row]; field.offset = bytes[0]; field.storage = bytes[1];
        const unsigned width = bytes[4], scalar = field.storage == 2 ? 4 : field.storage == 3 ? 2 : 0;
        if (field.storage == 6) { if (width != 4) return reject("host CMP field width"); field.components = 3; }
        else if (scalar && width && width % scalar == 0 && width / scalar <= 4) field.components = width / scalar;
        else return reject("host unsupported output storage");
        if (!width || field.offset + width > recipe.stride) return reject("host output field extent");
        const uint64_t mask = ((uint64_t(1) << width) - 1) << field.offset;
        if (occupied & mask) return reject("host overlapping output fields");
        occupied |= mask;
        for (; field.slot < next && semantics[field.slot] != bytes[3]; ++field.slot) {}
        if (!bytes[3] || field.slot == next) return reject("host missing output semantic");
    }
    // Sources occupy declared scratch tables, disjoint from the input/workspace
    // storage reused for index and vertex output. Bound both conversion cursor
    // families: the attribute cursor, or the culled index publication's end.
    const unsigned stream = e.input[11].size_flags && e.input[11].lsa < e.input[8].lsa ? 11 : 8;
    const uint64_t index_end = uint64_t(e.input[2].lsa) + ((e.index_count * 2 + 15) & ~15u);
    const uint64_t cursor_limit = index_end > e.input[stream].lsa ? index_end : e.input[stream].lsa;
    if (cursor_limit + recipe.bytes > uint64_t(e.io_lsa) + e.io_bytes ||
        e.scratch_lsa < uint64_t(e.io_lsa) + e.io_bytes ||
        uint64_t(e.scratch_lsa) + next * uint64_t((e.vertices + 7) & ~7u) * 16 > uint64_t(e.scratch_lsa) + e.scratch_bytes)
        return reject("host output workspace/scratch capacity");
    // These tables define the original codec's source ordering and CMP math.
    static constexpr uint32_t canonical[][4] = {
        {0xfffffffb, 0xfffffffe, 0, 0}, {0x3f7fe000, 0x3f7fe000, 0x3f7fc000, 0},
        {0xba000000, 0xba000000, 0xba800000, 0}, {0, 16, 32, 48}, {0, 1, 2, 3}};
    for (unsigned row = 0; row < 5; ++row) for (unsigned lane = 0; lane < 4; ++lane)
        if (geometry_job_be32(c.ls + 0xcf20 + row * 16 + lane * 4) != canonical[row][lane])
            return reject("host output codec definition");
    output = recipe; if (reason) *reason = nullptr; return true;
}

inline void emit_geometry_host_output(spu_context& c, const GeometryJobEntry& e, const GeometryHostOutputRecipe& recipe)
{
    const unsigned manager = e.geometry_context_lsa;
    const unsigned destination = geometry_job_be32(c.ls + manager + 0x58);
    const unsigned table_stride = ((e.vertices + 7) & ~7u) * 16;
    if ((destination & 15) || destination < e.io_lsa ||
        uint64_t(destination) + recipe.bytes > uint64_t(e.io_lsa) + e.io_bytes)
        buzz_geometry_job_native_fail(&c, "host output cursor escaped admitted workspace");
    std::array<unsigned, 16> sources{};
    for (unsigned field = 0; field < recipe.fields; ++field) {
        const auto& f = recipe.field[field];
        sources[field] = geometry_job_be32(c.ls + manager + 0x10 + f.slot * 4);
        if (sources[field] != e.scratch_lsa + f.slot * table_stride)
            buzz_geometry_job_native_fail(&c, "host output source escaped admitted table");
    }
    // Preserve holes and alignment bytes, fill complete padded vertices in a
    // bounded host chunk, then copy only the published portion once to LS.
    alignas(32) uint8_t chunk[8 * 64];
    const unsigned padded = (recipe.vertices + 3) & ~3u;
    for (unsigned first = 0; first < padded; first += 8) {
        const unsigned count = padded - first < 8 ? 4 : 8;
        const unsigned offset = first * recipe.stride;
        if (offset >= recipe.bytes) break;
        const unsigned bytes = recipe.bytes - offset < count * recipe.stride ? recipe.bytes - offset : count * recipe.stride;
        memcpy(chunk, c.ls + destination + offset, bytes);
        for (unsigned field = 0; field < recipe.fields; ++field) {
            const auto& f = recipe.field[field]; GeometryHostSoA8 input;
            const auto offsets = _mm256_setr_epi32(0, 16, 32, 48, 64, 80, 96, 112);
            for (unsigned component = 0; component < f.components; ++component)
                input.component[component] = v8_bswap(_mm256_i32gather_epi32(
                    reinterpret_cast<const int*>(c.ls + sources[field] + first * 16 + component * 4), offsets, 1));
            if (f.storage == 2) geometry_host_float_field8(input, f.components, chunk + f.offset, recipe.stride, count);
            else if (f.storage == 3) geometry_host_half_field8(input, f.components, chunk + f.offset, recipe.stride, count);
            else geometry_host_cmp_field8(input, chunk + f.offset, recipe.stride, count);
        }
        memcpy(c.ls + destination + offset, chunk, bytes);
    }
    geometry_job_put32(c.ls + manager + 0x54, destination);
    geometry_job_put32(c.ls + manager + 0x58, destination + recipe.bytes);
}
} // namespace buzz
