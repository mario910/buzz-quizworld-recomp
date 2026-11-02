/* Final vertex streams from persistent host SoA. Only published position
 * padding is decoded/transformed in addition to logical vertices. No legacy
 * float4 LS tables, SPU register bank or scratch pipeline is constructed. */
#pragma once
#include "native_geometry_host_output_job.h"
#include "native_geometry_host_skin_job.h"

namespace buzz {
struct GeometryHostVertexRecipe {
    GeometryHostOutputRecipe output;
    bool ready = false;
};
struct GeometryHostVertexTail {
    uint32_t position[3]{};
    bool ready = false;
};
inline bool read_geometry_host_vertex_recipe(const spu_context& c, const GeometryJobEntry& e,
    const GeometryHostInputRecipe& input, GeometryHostVertexRecipe& result,
    const char** reason = nullptr)
{
    result.ready = false;
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    GeometryHostVertexRecipe recipe;
    if (!read_geometry_host_output_recipe(c,e,recipe.output,reason)) return false;
    const auto& output = recipe.output;
    if (input.vertices != e.vertices || !input.fields || input.fields > 16 ||
        input.blocks_per_field != (input.vertices+7)/8 ||
        uint64_t(input.fields)*input.blocks_per_field > GeometryHostInputData::capacity)
        return reject("host vertex input shape");
    // Dense records need no old destination bytes. Current observed final
    // alignment reaches at most XYZ of one extra float3 position.
    const auto& position = output.field[0];
    if ((output.stride & 3) || position.offset || position.slot ||
        position.storage != 2 || position.components != 3 || input.field[0].semantic != 1 ||
        input.field[0].storage != 2 || input.field[0].components != 3 ||
        output.bytes - output.vertices*output.stride > 12)
        return reject("host vertex publication layout");
    uint64_t occupied = 0;
    for (unsigned field = 0; field < output.fields; ++field) {
        const auto& f = output.field[field];
        if (f.slot >= input.fields || f.components > (input.field[f.slot].storage == 3 ? input.field[f.slot].components : 4))
            return reject("host vertex field source");
        const unsigned width = f.storage == 6 ? 4 : f.components * (f.storage == 2 ? 4 : 2);
        occupied |= ((uint64_t(1)<<width)-1)<<f.offset;
    }
    if (occupied != (output.stride == 64 ? UINT64_MAX : (uint64_t(1)<<output.stride)-1))
        return reject("host vertex record holes");
    recipe.ready = true; result = recipe; if (reason) *reason = nullptr; return true;
}

inline bool prepare_geometry_host_vertex_tail(const spu_context& c, const GeometryJobEntry& e,
    const GeometryHostInputRecipe& input, const GeometryHostSkinRecipe& skin,
    const GeometryHostSkinWorkspace& workspace, const GeometryHostVertexRecipe& recipe,
    GeometryHostVertexTail& result, const char** reason = nullptr)
{
    result.ready = false;
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    if (!recipe.ready || input.vertices != e.vertices || recipe.output.vertices != e.vertices ||
        c.ls != c.ls_store) return reject("host vertex tail readiness");
    GeometryHostVertexTail tail;
    if (recipe.output.bytes == e.vertices*recipe.output.stride) {
        tail.ready = true; result = tail; if (reason) *reason = nullptr; return true;
    }
    const uint64_t source = uint64_t(input.field[0].source) + uint64_t(e.vertices)*input.field[0].stride;
    const uint64_t io_end = uint64_t(e.io_lsa)+e.io_bytes;
    if (source < e.io_lsa || source+12 > io_end || io_end > SPU_LS_SIZE)
        return reject("host vertex position tail span");
    auto value = decode_geometry_host_positions8(c.ls+source,1);
    const v8 mask = _mm256_setr_epi32(-1,0,0,0,0,0,0,0);
    if (skin.object) {
        v8 matrix[12]; for (unsigned coefficient=0;coefficient<12;++coefficient)
            matrix[coefficient] = v8_set(workspace.object[coefficient]);
        transform_geometry_host_object_value(value,matrix,false,mask);
    } else if (skin.active) {
        const uint64_t weights = uint64_t(skin.weights)+uint64_t(e.vertices)*8;
        if (!skin.matrices || skin.matrices > 256 || weights < e.io_lsa || weights+8 > io_end)
            return reject("host vertex weight tail span");
        unsigned index[4]; v8 factor[4];
        for (unsigned influence=0;influence<4;++influence) {
            index[influence] = c.ls[weights+influence*2+1];
            if (index[influence] >= skin.matrices) return reject("host vertex padding palette index");
            factor[influence] = v8_fm(_mm256_castps_si256(_mm256_cvtepi32_ps(v8_set(c.ls[weights+influence*2]))),v8_set(0x3b808080));
        }
        v8 blended[12];
        for (unsigned coefficient=0;coefficient<12;++coefficient) {
            blended[coefficient] = v8_fm(factor[0],v8_set(workspace.coefficient[coefficient][index[0]]));
            for (unsigned influence=1;influence<4;++influence)
                blended[coefficient] = v8_fma(factor[influence],v8_set(workspace.coefficient[coefficient][index[influence]]),blended[coefficient]);
        }
        transform_geometry_host_weighted_value(value,blended,true,mask);
    }
    for (unsigned component=0;component<3;++component)
        tail.position[component] = unsigned(_mm256_extract_epi32(value.component[component],0));
    tail.ready = true; result = tail; if (reason) *reason = nullptr; return true;
}

inline bool emit_geometry_host_vertices(const GeometryHostInputRecipe& input,
    const GeometryHostInputData& data, const GeometryHostVertexRecipe& recipe,
    const GeometryHostVertexTail& tail, uint8_t* destination, size_t capacity,
    const char** reason = nullptr)
{
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    const auto& output = recipe.output;
    if (!recipe.ready || !data.ready || !tail.ready || !input.vertices || input.vertices != output.vertices)
        return reject("host vertex output readiness");
    if (!destination || capacity < output.bytes) return reject("host vertex output span");
    alignas(32) uint8_t chunk[8*64+16];
    for (unsigned first=0;first<input.vertices;first+=8) {
        const unsigned active = input.vertices-first<8 ? input.vertices-first : 8;
        for (unsigned field=0;field<output.fields;++field) {
            const auto& f = output.field[field]; const auto& value = data.at(input,f.slot,first/8);
            if (f.storage == 2) geometry_host_float_field8(value,f.components,chunk+f.offset,output.stride,active);
            else if (f.storage == 3) geometry_host_half_field8(value,f.components,chunk+f.offset,output.stride,active);
            else geometry_host_cmp_field8(value,chunk+f.offset,output.stride,active);
        }
        unsigned bytes = active*output.stride;
        if (first+active == input.vertices) {
            const unsigned padding = output.bytes-input.vertices*output.stride;
            for (unsigned at=0;at<padding;++at)
                chunk[bytes+at] = uint8_t(tail.position[at/4]>>(24-(at%4)*8));
            bytes += padding;
        }
        memcpy(destination+first*output.stride,chunk,bytes);
    }
    if (reason) *reason = nullptr; return true;
}
} // namespace buzz
