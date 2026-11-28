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
    bool position_only_ready = false;
};
struct GeometryHostVertexTail {
    uint32_t position[4]{};   // [3] only covers zero-padded 13..15-byte tails
    bool ready = false;
};
// Only alignment bytes after the final logical vertex are synthesized. The
// caller must have culled/range-checked every index before selecting this
// policy; no logical attribute or indexed position can come from this tail.
inline bool prepare_geometry_host_zero_vertex_tail(const GeometryHostInputRecipe& input,
    const GeometryHostVertexRecipe& recipe,GeometryHostVertexTail& result,const char** reason=nullptr)
{
    result.ready=false;
    const auto& output=recipe.output;
    const uint64_t logical=uint64_t(input.vertices)*output.stride;
    if(!recipe.ready || !input.vertices || input.vertices>4096 || output.vertices!=input.vertices ||
       !output.stride || output.stride>64 || output.bytes<logical || output.bytes-logical>15 || (output.bytes&15)) {
        if(reason)*reason="host zero vertex alignment span";return false;
    }
    result={};result.ready=true;if(reason)*reason=nullptr;return true;
}
inline bool read_geometry_host_vertex_recipe(const spu_context& c, const GeometryJobEntry& e,
    const GeometryHostInputRecipe& input, GeometryHostVertexRecipe& result,
    const char** reason = nullptr)
{
    result.ready = false;
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    GeometryHostVertexRecipe recipe;
    if (!read_geometry_host_output_recipe(c,e,recipe.output,reason,true)) return false;
    const auto& output = recipe.output;
    if (input.vertices != e.vertices || !input.fields || input.fields > 16 ||
        input.blocks_per_field != (input.vertices+7)/8 ||
        uint64_t(input.fields)*input.blocks_per_field > GeometryHostInputData::capacity)
        return reject("host vertex input shape");
    // Dense records need no old destination bytes. Current observed final
    // alignment reaches at most XYZ of one extra float3 position.
    const auto& position = output.field[0];
    // All supported record strides start with XYZ at byte offset zero.
    if (position.offset || position.slot || position.storage != 2 || position.components != 3 ||
        input.field[0].semantic != 1 || input.field[0].storage != 2 || input.field[0].components != 3)
        return reject("host vertex publication layout: position");
    // At most 15 alignment bytes (16-byte aligned output). The exact tail
    // helper formats one extra XYZ position and still admits only 12.
    if (output.bytes - output.vertices*output.stride > 15)
        return reject("host vertex publication layout: padding");
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
    if (recipe.output.bytes - e.vertices*recipe.output.stride > 12)
        return reject("host vertex publication layout: padding");
    const uint64_t source = uint64_t(input.field[0].source) + uint64_t(e.vertices)*input.field[0].stride;
    const uint64_t io_end = uint64_t(e.io_lsa)+e.io_bytes;
    if (source < e.io_lsa || source+12 > io_end || io_end > SPU_LS_SIZE)
        return reject("host vertex position tail span");
    auto value = decode_geometry_host_positions8(c.ls+source,1);
    const v8 mask = _mm256_setr_epi32(-1,0,0,0,0,0,0,0);
    if (skin.object) {
        v8 matrix[12]; for (unsigned coefficient=0;coefficient<12;++coefficient)
            matrix[coefficient] = v8_set(workspace.object[coefficient]);
        if (!geometry_object_matrix_bounded(workspace.object) ||
            !transform_geometry_bounded_position(value,matrix,mask))
            transform_geometry_host_object_value(value,matrix,false,mask);
    } else if (skin.active) {
        const uint64_t weights = uint64_t(skin.weights)+uint64_t(e.vertices)*8;
        if (!skin.matrices || skin.matrices > 256 || weights < e.io_lsa || weights+8 > io_end)
            return reject("host vertex weight tail span");
        // An out-of-palette padding index stays a fallback: reading that LS
        // quad at entry was tried and differs from the lift on 192 corpus jobs.
        unsigned index[4]; v8 factor[4];
        for (unsigned influence=0;influence<4;++influence) {
            index[influence] = c.ls[weights+influence*2+1];
            if (index[influence] >= skin.matrices) return reject("host vertex padding palette index");
            factor[influence] = v8_fm(_mm256_castps_si256(_mm256_cvtepi32_ps(v8_set(c.ls[weights+influence*2]))),v8_set(0x3b808080));
        }
        v8 blended[12];
        for (unsigned coefficient=0;coefficient<12;++coefficient) {
            blended[coefficient] = v8_fm(factor[0],v8_set(workspace.word(index[0], coefficient)));
            for (unsigned influence=1;influence<4;++influence)
                blended[coefficient] = v8_fma(factor[influence],v8_set(workspace.word(index[influence], coefficient)),blended[coefficient]);
        }
        transform_geometry_host_weighted_value(value,blended,true,mask);
    }
    for (unsigned component=0;component<3;++component)
        tail.position[component] = unsigned(_mm256_extract_epi32(value.component[component],0));
    tail.ready = true; result = tail; if (reason) *reason = nullptr; return true;
}

#if defined(__AVX2__) || (BUZZ_GEOMETRY_MOBILE_KERNELS && defined(__aarch64__))
// Write eight complete position/normal records. Transposing within both
// 128-bit halves forms vertices (0,4), (1,5), (2,6), (3,7) in four registers.
template<unsigned Stride>
inline void geometry_host_store_position_normal8(const GeometryHostSoA8& position,
    const GeometryHostSoA8& normal, uint8_t* destination)
{
#if defined(__aarch64__)
    const v8 packed = v8_bswap(geometry_host_cmp8(normal));
    const v8 x=v8_bswap(position.component[0]), y=v8_bswap(position.component[1]), z=v8_bswap(position.component[2]);
    auto store_four=[](uint8_t* base, uint32x4_t x,uint32x4_t y,uint32x4_t z,uint32x4_t n) {
        const uint32x4x4_t fields={{x,y,z,n}};
        alignas(16) uint32_t records[16];
        vst4q_u32(records,fields);
        for (unsigned i=0;i<4;++i) memcpy(base+i*Stride,records+4*i,16);
    };
    store_four(destination,x.lo,y.lo,z.lo,packed.lo);
    store_four(destination+4*Stride,x.hi,y.hi,z.hi,packed.hi);
#else
    const v8 xy0 = _mm256_unpacklo_epi32(position.component[0], position.component[1]);
    const v8 xy1 = _mm256_unpackhi_epi32(position.component[0], position.component[1]);
    const v8 packed = geometry_host_cmp8(normal);
    const v8 zn0 = _mm256_unpacklo_epi32(position.component[2], packed);
    const v8 zn1 = _mm256_unpackhi_epi32(position.component[2], packed);
    const v8 records[] = {
        v8_bswap(_mm256_unpacklo_epi64(xy0, zn0)), v8_bswap(_mm256_unpackhi_epi64(xy0, zn0)),
        v8_bswap(_mm256_unpacklo_epi64(xy1, zn1)), v8_bswap(_mm256_unpackhi_epi64(xy1, zn1))};
    for (unsigned i = 0; i < 4; ++i) {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + i * Stride),
            _mm256_castsi256_si128(records[i]));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + (i + 4) * Stride),
            _mm256_extracti128_si256(records[i], 1));
    }
#endif
}

template<unsigned Stride>
inline unsigned emit_geometry_host_position_normal_blocks(const GeometryHostInputRecipe& input,
    const GeometryHostInputData& data, const GeometryHostOutputRecipe& output, uint8_t* destination)
{
    const unsigned vertices = input.vertices & ~7u;
    for (unsigned first = 0; first < vertices; first += 8, destination += 8 * Stride) {
        const unsigned block = first / 8;
        geometry_host_store_position_normal8<Stride>(data.at(input, 0, block),
            data.at(input, output.field[1].slot, block), destination);
        if constexpr (Stride == 20) {
            const auto& uv = data.at(input, output.field[2].slot, block);
            const v8 pair = _mm256_or_si256(_mm256_slli_epi32(geometry_host_half8(uv.component[0]), 16),
                geometry_host_half8(uv.component[1]));
            uint32_t words[8];
            _mm256_storeu_si256(reinterpret_cast<v8*>(words), v8_bswap(pair));
            for (unsigned i = 0; i < 8; ++i)
                memcpy(destination + i * Stride + 16, &words[i], 4);
        }
    }
    return vertices;
}
#endif

inline bool emit_geometry_host_vertices(const GeometryHostInputRecipe& input,
    const GeometryHostInputData& data, const GeometryHostVertexRecipe& recipe,
    const GeometryHostVertexTail& tail, uint8_t* destination, size_t capacity,
    const char** reason = nullptr)
{
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    const auto& output = recipe.output;
    // Unmaterialized attributes are allowed only for a position-only record.
    const bool pending = data.pending_fields && (!recipe.position_only_ready || output.fields != 1 || output.stride != 12 ||
        output.field[0].slot != 0 || (data.pending_fields & 1));
    if (!recipe.ready || !data.ready || pending || !tail.ready || !input.vertices || input.vertices != output.vertices)
        return reject("host vertex output readiness");
    if (!destination || capacity < output.bytes) return reject("host vertex output span");
    // Admission proves every byte of each output record is defined.
    unsigned first = 0;
#if BUZZ_GEOMETRY_MOBILE_KERNELS && defined(__aarch64__)
    if (output.stride==20 && output.fields==3 && output.field[1].storage==6 &&
        output.field[1].offset==12 && output.field[2].offset==16 &&
        output.field[2].storage==3 && output.field[2].components==2)
        first=emit_geometry_host_position_normal_blocks<20>(input,data,output,destination);
    else if (output.stride==16 && output.fields==2 && output.field[1].offset==12 &&
        output.field[1].storage==6 && !(uintptr_t(destination)&3)) {
        for (; first+8<=input.vertices;first+=8) {
            const auto& position=data.at(input,0,first/8);
            const auto& normal=data.at(input,output.field[1].slot,first/8);
            const v8 packed=v8_bswap(geometry_host_cmp8(normal));
            uint32x4x4_t lo,hi;
            for (unsigned k=0;k<3;++k) {
                const v8 word=v8_bswap(position.component[k]);
                lo.val[k]=word.lo; hi.val[k]=word.hi;
            }
            lo.val[3]=packed.lo; hi.val[3]=packed.hi;
            auto* out=reinterpret_cast<uint32_t*>(destination+first*16);
            vst4q_u32(out,lo); vst4q_u32(out+16,hi);
        }
    }
#endif
#if defined(__AVX2__)
    // Admission proves that field zero is float3 position in slot zero.
    if (output.fields >= 2 && output.field[1].offset == 12 && output.field[1].storage == 6) {
        if (output.stride == 16 && output.fields == 2)
            first = emit_geometry_host_position_normal_blocks<16>(input, data, output, destination);
        else if (output.stride == 20 && output.fields == 3 && output.field[2].offset == 16 &&
                 output.field[2].storage == 3 && output.field[2].components == 2)
            first = emit_geometry_host_position_normal_blocks<20>(input, data, output, destination);
    }
#endif
    for (; first<input.vertices;first+=8) {
        uint8_t* const chunk = destination + first * output.stride;
        const unsigned active = input.vertices-first<8 ? input.vertices-first : 8;
        for (unsigned field=0;field<output.fields;++field) {
            const auto& f = output.field[field]; const auto& value = data.at(input,f.slot,first/8);
            if (f.storage == 2) geometry_host_float_field8(value,f.components,chunk+f.offset,output.stride,active);
            else if (f.storage == 3) geometry_host_half_field8(value,f.components,chunk+f.offset,output.stride,active);
            else if (f.storage == 1) geometry_host_snorm_field8(value,f.components,chunk+f.offset,output.stride,active);
            else geometry_host_cmp_field8(value,chunk+f.offset,output.stride,active);
        }
        unsigned bytes = active*output.stride;
        if (first+active == input.vertices) {
            const unsigned padding = output.bytes-input.vertices*output.stride;
            for (unsigned at=0;at<padding;++at)
                chunk[bytes+at] = uint8_t(tail.position[at/4]>>(24-(at%4)*8));
        }
    }
    if (reason) *reason = nullptr; return true;
}
} // namespace buzz
