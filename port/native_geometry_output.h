/* Shared host output layout for sizing, allocation and vertex publication.
 * ABI materialization preserves the original getter's full register vectors;
 * no translated getter or size-driver block runs on the native path. */
#pragma once
#include "native_geometry_context.h"
#include "spu_helpers.h"

namespace buzz {
inline bool geometry_output_range(uint32_t at, uint32_t bytes)
{
    return !(at & 15) && at <= SPU_LS_SIZE && bytes <= SPU_LS_SIZE - at;
}
inline u128 geometry_output_quad(const spu_context& ctx, uint32_t at)
{
    u128 value;
    for (unsigned lane = 0; lane < 4; ++lane) value._u32[lane] = geometry_job_be32(ctx.ls + at + lane * 4);
    return value;
}
inline bool geometry_output_overlap(uint32_t a, uint32_t bytes_a, uint32_t b, uint32_t bytes_b)
{
    return a < b + bytes_b && b < a + bytes_a;
}
struct GeometryOutputLayout {
    uint32_t context = 0, format = 0, stride = 0, vertices = 0;
    uint32_t dispatch_lsa = 0, descriptor_lsa = 0;
    u128 configuration{}, custom{}, callbacks{}, dispatch{}, descriptor{}, stride_vector{};
};

inline bool read_geometry_output_layout(const spu_context& ctx, uint32_t context, GeometryOutputLayout& output)
{
    if (ctx.ls != ctx.ls_store || !geometry_output_range(context, 0x90)) return false;
    GeometryOutputLayout layout;
    layout.context = context;
    layout.configuration = geometry_output_quad(ctx, context);
    layout.format = ctx.ls[context + 4];
    layout.vertices = geometry_job_be16(ctx.ls + context + 8);
    layout.dispatch_lsa = (0x4104 + 4 * layout.format) & ~15u;
    layout.dispatch = geometry_output_quad(ctx, layout.dispatch_lsa);
    if (layout.format < 4) {
        static constexpr uint32_t offsets[] = {0x68, 0x60, 0x58, 0x58};
        static constexpr uint32_t strides[] = {12, 20, 24, 24};
        if (geometry_job_be32(ctx.ls + 0x4104 + 4 * layout.format) != offsets[layout.format]) return false;
        layout.stride = strides[layout.format];
        layout.stride_vector = spu_il(int(layout.stride));
    } else {
        layout.custom = geometry_output_quad(ctx, context + 0x70);
        layout.callbacks = geometry_output_quad(ctx, context + 0x80);
        if (layout.callbacks._u32[2]) return false; // Retain arbitrary custom getters.
        const uint32_t descriptor = layout.custom._u32[1];
        if (descriptor > SPU_LS_SIZE - 2) return false;
        layout.descriptor_lsa = (descriptor + 1) & ~15u;
        layout.descriptor = geometry_output_quad(ctx, layout.descriptor_lsa);
        const u128 pointer = spu_rotqbyi(layout.custom, 4);
        const u128 selected = spu_andi(spu_rotqby(layout.descriptor, spu_ai(pointer, 14)), 255);
        layout.stride_vector = spu_selb(selected, spu_il(0), spu_ceqi(pointer, 0));
        layout.stride = descriptor ? ctx.ls[descriptor + 1] : 0;
    }
    output = layout;
    return true;
}

inline bool geometry_output_layout_matches(const spu_context& ctx, uint32_t context, const GeometryOutputLayout& layout)
{
    if (ctx.ls != ctx.ls_store || context != layout.context || !geometry_output_range(context, 0x90) ||
        !geometry_output_range(layout.dispatch_lsa, 16) ||
        (layout.format >= 4 && !geometry_output_range(layout.descriptor_lsa, 16))) return false;
    auto same = [&](uint32_t at, u128 expected) {
        const u128 actual = geometry_output_quad(ctx, at);
        return memcmp(&actual, &expected, 16) == 0;
    };
    return same(context, layout.configuration) && same(layout.dispatch_lsa, layout.dispatch) &&
        (layout.format < 4 || (same(context + 0x70, layout.custom) &&
            same(context + 0x80, layout.callbacks) && same(layout.descriptor_lsa, layout.descriptor)));
}

inline bool geometry_output_layout_outside_stack(const GeometryOutputLayout& layout, uint32_t start, uint32_t bytes)
{
    return !geometry_output_overlap(layout.context, 0x90, start, bytes) &&
        !geometry_output_overlap(layout.dispatch_lsa, 16, start, bytes) &&
        (layout.format < 4 || !geometry_output_overlap(layout.descriptor_lsa, 16, start, bytes));
}

inline u128 geometry_output_vertex_bytes(const GeometryOutputLayout& layout)
{
    const u128 counts = spu_rotqbyi(layout.configuration, 6);
    u128 bytes;
    for (unsigned lane = 0; lane < 4; ++lane)
        bytes._u32[lane] = uint32_t(uint16_t(layout.stride_vector._u32[lane])) * uint16_t(counts._u32[lane]);
    return bytes;
}

inline void materialize_geometry_output_stride(spu_context& ctx, const GeometryOutputLayout& layout)
{
    const u128 selector = spu_andi(spu_rotqbyi(layout.configuration, 1), 255);
    ctx.gpr[13] = spu_ila(0x40d0); ctx.gpr[12] = spu_link(0x40d0);
    ctx.gpr[10] = spu_shli(selector, 2); ctx.gpr[11] = spu_ila(0x4104);
    ctx.gpr[6] = spu_clgti(selector, 3); ctx.gpr[79] = spu_sf(ctx.gpr[13], ctx.gpr[12]);
    ctx.gpr[7] = spu_a(ctx.gpr[11], ctx.gpr[79]); ctx.gpr[5] = spu_a(ctx.gpr[10], ctx.gpr[7]);
    ctx.gpr[9] = layout.dispatch;
    ctx.gpr[2] = spu_a(spu_rotqby(ctx.gpr[9], ctx.gpr[5]), ctx.gpr[7]);
    if (layout.format >= 4) {
        ctx.gpr[14] = layout.callbacks; ctx.gpr[5] = spu_rotqbyi(ctx.gpr[14], 8);
        ctx.gpr[20] = spu_il(1); ctx.gpr[21] = layout.custom;
        ctx.gpr[19] = spu_rotqbyi(ctx.gpr[21], 4); ctx.gpr[18] = spu_ai(ctx.gpr[19], 14);
        ctx.gpr[17] = layout.descriptor; ctx.gpr[15] = spu_ceqi(ctx.gpr[19], 0);
        ctx.gpr[16] = spu_rotqby(ctx.gpr[17], ctx.gpr[18]); ctx.gpr[4] = spu_andi(ctx.gpr[16], 255);
    }
    ctx.gpr[3] = layout.stride_vector;
}

struct GeometryOutputSize {
    GeometryOutputLayout layout;
    uint32_t stack = 0;
    u128 indices{}, vertex_bytes{}, allocation_bytes{};
};
inline bool read_geometry_output_size(const spu_context& ctx, GeometryOutputSize& output)
{
    GeometryOutputSize size;
    size.stack = ctx.gpr[1]._u32[0]; size.indices = ctx.gpr[4];
    if ((size.stack & 15) || size.stack < 0x40 || size.stack > SPU_LS_SIZE - 32 ||
        !read_geometry_output_layout(ctx, ctx.gpr[3]._u32[0], size.layout) ||
        !geometry_output_layout_outside_stack(size.layout, size.stack - 0x40, 0x60)) return false;
    size.vertex_bytes = geometry_output_vertex_bytes(size.layout);
    for (unsigned lane = 0; lane < 4; ++lane) {
        const uint32_t index_bytes = ((size.indices._u32[lane] + 7u) & ~7u) * 2u;
        size.allocation_bytes._u32[lane] = (size.vertex_bytes._u32[lane] + index_bytes + 127u) & ~127u;
    }
    output = size;
    return true;
}
inline void measure_geometry_output(spu_context& ctx, const GeometryOutputSize& size)
{
    const u128 stack = ctx.gpr[1];
    spu_ls_write128(&ctx, size.stack + 16, ctx.gpr[0]);
    spu_ls_write128(&ctx, size.stack - 16, ctx.gpr[80]);
    spu_ls_write128(&ctx, size.stack - 32, ctx.gpr[81]);
    spu_ls_write128(&ctx, size.stack - 64, stack);
    ctx.gpr[1] = spu_ai(stack, -64);
    ctx.gpr[0] = spu_link(0x2bb8);
    materialize_geometry_output_stride(ctx, size.layout);
    ctx.gpr[7] = size.layout.configuration; ctx.gpr[9] = spu_ai(size.indices, 7);
    ctx.gpr[8] = spu_andi(ctx.gpr[9], -8); ctx.gpr[6] = spu_a(ctx.gpr[8], ctx.gpr[8]);
    ctx.gpr[2] = size.vertex_bytes; ctx.gpr[5] = spu_il(0); ctx.gpr[4] = size.vertex_bytes;
    ctx.gpr[3] = size.allocation_bytes;
    ctx.gpr[1] = stack; ctx.gpr[0] = spu_ls_read128(&ctx, size.stack + 16);
    ctx.gpr[80] = spu_ls_read128(&ctx, size.stack - 16); ctx.gpr[81] = spu_ls_read128(&ctx, size.stack - 32);
}
} // namespace buzz
