/* Host RSX vertex binding/reset packets (image 41, 100/20). */
#pragma once
#include "native_geometry_commands.h"

namespace buzz {
struct GeometryCommandAttribute {
    uint8_t offset = 0, type = 0, components = 0, slot = 0;
};
struct GeometryCommandFormat {
    uint32_t descriptor = 0, bytes = 0, count = 0, stride = 0;
    GeometryCommandAttribute attributes[255]{};
};
inline bool read_geometry_command_format(const spu_context& ctx, uint32_t context, GeometryCommandFormat& output)
{
    if (ctx.ls != ctx.ls_store || !geometry_output_range(context, 0x80)) return false;
    GeometryCommandFormat p;
    p.descriptor = geometry_job_be32(ctx.ls + context + 0x74);
    if (!geometry_output_range(p.descriptor, 16)) return false;
    p.count = ctx.ls[p.descriptor]; p.stride = ctx.ls[p.descriptor + 1];
    p.bytes = p.count ? ((8 * p.count + 6 + 15) & ~15u) : 16;
    if (!geometry_output_range(p.descriptor, p.bytes)) return false;
    for (unsigned i = 0; i < p.count; ++i) {
        const auto* field = ctx.ls + p.descriptor + 8 + i * 8;
        p.attributes[i] = {field[0], field[1], field[2], field[5]};
    }
    output = p; return true;
}
inline bool geometry_command_attribute_enabled(uint32_t mask, unsigned slot)
{
    // ROTMA uses a six-bit shift count. Preserve even nonstandard slots.
    return (slot & 63) < 16 && ((mask >> (slot & 63)) & 1);
}
struct GeometryCommandBindings {
    GeometryCommandFormat format;
    uint32_t context = 0, record = 0, source = 0, cursor = 0, mask = 0, enabled = 0;
    uint32_t base = 0, location = 0;
};
inline bool read_geometry_command_bindings(const spu_context& ctx, bool reset, GeometryCommandBindings& output)
{
    GeometryCommandBindings p;
    p.context = ctx.gpr[3]._u32[0]; p.record = ctx.gpr[4]._u32[0]; p.source = ctx.gpr[5]._u32[0];
    p.mask = ctx.gpr[reset ? 5 : 6]._u32[0] & 65535;
    if (!read_geometry_command_format(ctx, p.context, p.format)) return false;
    if (!reset) {
        if (!geometry_output_range(p.source, 16)) return false;
        p.base = geometry_job_be32(ctx.ls + p.source + 4);
        p.location = geometry_job_be32(ctx.ls + p.source + 8) << 31;
    }
    for (unsigned i = 0; i < p.format.count; ++i)
        p.enabled += geometry_command_attribute_enabled(p.mask, p.format.attributes[i].slot);
    // Empty/masked lists never dereference the command record.
    if (p.enabled) {
        if (!geometry_output_range(p.record, 16)) return false;
        p.cursor = geometry_job_be32(ctx.ls + p.record + 8);
        const uint32_t bytes = p.enabled * (reset ? 8 : 16);
        if ((p.cursor & 3) || p.cursor < 0xe680 || p.cursor > SPU_LS_SIZE || bytes > SPU_LS_SIZE - p.cursor) return false;
        const uint32_t starts[] = {p.context, p.format.descriptor, p.record, p.cursor & ~15u, p.source};
        const uint32_t sizes[] = {0x80, p.format.bytes, 16, ((p.cursor + bytes + 15) & ~15u) - (p.cursor & ~15u), 16};
        for (unsigned a = 0; a < (reset ? 4u : 5u); ++a)
            for (unsigned b = a + 1; b < (reset ? 4u : 5u); ++b)
                if ((a == 2 || a == 3 || b == 2 || b == 3) &&
                    geometry_output_overlap(starts[a], sizes[a], starts[b], sizes[b])) return false;
    }
    output = p; return true;
}

inline void reset_geometry_command_bindings(spu_context& ctx, const GeometryCommandBindings& p)
{
    auto& r = ctx.gpr;
    r[9] = r[4]; r[7] = geometry_output_quad(ctx, p.context + 0x70); r[14] = spu_rotqbyi(r[7], 4);
    r[6] = geometry_output_quad(ctx, p.format.descriptor); r[4] = spu_rotqbyi(r[6], 13);
    r[3] = spu_ceqbi(r[4], 0); r[2] = spu_xsbh(r[3]);
    if (!p.format.count) return;
    r[8] = spu_ila(65535); r[11] = spu_il(0); r[17] = spu_il(5952);
    r[15] = spu_and(r[5], r[8]); r[16] = spu_ilhu(4); r[18] = spu_il(2); r[7] = spu_ai(r[14], 13);
    uint32_t cursor = p.cursor;
    for (unsigned i = 0; i < p.format.count; ++i) {
        const auto& field = p.format.attributes[i];
        r[29] = spu_ai(r[7], 13); r[28] = geometry_output_quad(ctx, r[7]._u32[0] & ~15u);
        r[10] = spu_il(8); r[12] = spu_cwd(r[9], 8); r[5] = spu_ai(r[9], 8); r[13] = spu_il(4);
        r[11] = spu_ai(r[11], 1); r[7] = spu_ai(r[7], 8); r[27] = spu_rotqby(r[28], r[29]);
        r[26] = spu_andi(r[27], 255); r[25] = spu_sfi(r[26], 0); r[23] = spu_shli(r[26], 2);
        r[24] = spu_rotma(r[15], r[25]); r[21] = spu_a(r[23], r[17]); r[22] = spu_andi(r[24], 1);
        r[8] = spu_or(r[21], r[16]); r[20] = spu_ceqbi(r[22], 0); r[19] = spu_xsbh(r[20]);
        if (geometry_command_attribute_enabled(p.mask, field.slot)) {
            r[39] = geometry_output_quad(ctx, p.record); r[32] = spu_rotqby(r[39], r[5]);
            r[31] = spu_ai(r[32], 8); r[38] = spu_cwd(r[32], 0); r[35] = spu_cwd(r[32], 4);
            r[36] = geometry_command_word(ctx, cursor, 0x41740 + field.slot * 4, &r[37]);
            r[33] = geometry_command_word(ctx, cursor + 4, 2, &r[34]);
            cursor += 8; r[13] = geometry_command_word(ctx, p.record + 8, cursor, &r[30]);
            r[5] = geometry_output_quad(ctx, p.format.descriptor); r[4] = spu_rotqbyi(r[5], 13);
        }
        r[12] = spu_andi(r[4], 255); r[10] = spu_clgt(r[12], r[11]);
    }
}

inline void bind_geometry_command_attributes(spu_context& ctx, const GeometryCommandBindings& p)
{
    auto& r = ctx.gpr;
    r[14] = r[4]; r[12] = geometry_output_quad(ctx, p.context + 0x70); r[11] = geometry_output_quad(ctx, p.source);
    r[21] = spu_rotqbyi(r[12], 4); r[5] = spu_rotqbyi(r[11], 8); r[28] = spu_rotqbyi(r[11], 4);
    r[10] = geometry_output_quad(ctx, p.format.descriptor); r[7] = spu_rotqbyi(r[10], 13); r[3] = spu_rotqbyi(r[10], 14);
    r[4] = spu_ceqbi(r[7], 0); r[3] = spu_andi(r[3], 255); r[2] = spu_xsbh(r[4]);
    if (!p.format.count) return;
    r[13] = spu_ila(65535); r[18] = spu_il(0); r[25] = spu_shli(r[3], 8); r[24] = spu_shli(r[5], 31);
    r[22] = spu_and(r[6], r[13]); r[13] = r[21]; r[27] = spu_il(5952); r[23] = spu_ilhu(4); r[26] = spu_il(5760);
    uint32_t cursor = p.cursor;
    for (unsigned i = 0; i < p.format.count; ++i) {
        const auto& field = p.format.attributes[i];
        r[39] = spu_il(13); r[20] = spu_cwd(r[14], 8); r[38] = spu_ai(r[13], 26); r[15] = spu_il(8);
        r[37] = geometry_output_quad(ctx, (r[13]._u32[0] + 13) & ~15u); r[8] = spu_ai(r[14], 8);
        r[9] = spu_ai(r[13], 23); r[16] = spu_il(4); r[11] = spu_ai(r[13], 22); r[17] = spu_ai(r[13], 21);
        r[18] = spu_ai(r[18], 1); r[36] = spu_rotqby(r[37], r[38]); r[35] = spu_andi(r[36], 255);
        r[34] = spu_sfi(r[35], 0); r[32] = spu_shli(r[35], 2); r[33] = spu_rotma(r[22], r[34]);
        r[31] = spu_a(r[32], r[27]); r[30] = spu_andi(r[33], 1); r[19] = spu_a(r[32], r[26]);
        r[29] = spu_ceqbi(r[30], 0); r[10] = spu_or(r[31], r[23]); r[6] = spu_xsbh(r[29]); r[19] = spu_or(r[19], r[23]);
        if (geometry_command_attribute_enabled(p.mask, field.slot)) {
            r[73] = spu_il(10); r[71] = geometry_output_quad(ctx, p.record); r[72] = spu_il(9);
            r[63] = geometry_output_quad(ctx, (r[13]._u32[0] + 8) & ~15u);
            r[70] = geometry_output_quad(ctx, (r[13]._u32[0] + 10) & ~15u);
            r[68] = geometry_output_quad(ctx, (r[13]._u32[0] + 9) & ~15u);
            r[46] = spu_rotqby(r[71], r[8]); r[62] = spu_rotqby(r[63], r[17]); r[69] = spu_rotqby(r[70], r[9]);
            r[67] = spu_rotqby(r[68], r[11]); r[41] = spu_ai(r[46], 8); r[57] = spu_andi(r[62], 255);
            r[65] = spu_cwd(r[46], 0); r[66] = spu_andi(r[69], 255); r[55] = spu_cwd(r[46], 4);
            r[61] = spu_andi(r[67], 255); r[49] = spu_cwd(r[46], 8); r[60] = spu_shli(r[66], 4);
            r[45] = spu_cwd(r[41], 4); r[56] = spu_a(r[28], r[57]); r[40] = spu_ai(r[46], 16);
            r[44] = spu_or(r[56], r[24]); r[58] = spu_or(r[60], r[61]); r[54] = spu_or(r[25], r[58]);
            r[59] = geometry_command_word(ctx, cursor, 0x41740 + field.slot * 4, &r[64]);
            const uint32_t format = (p.format.stride << 8) | (unsigned(field.components) << 4) | field.type;
            r[52] = geometry_command_word(ctx, cursor + 4, format, &r[53]);
            r[50] = geometry_command_word(ctx, p.record + 8, cursor + 8, &r[51]);
            r[47] = geometry_command_word(ctx, cursor + 8, 0x41680 + field.slot * 4, &r[48]);
            r[42] = geometry_command_word(ctx, cursor + 12, (p.base + field.offset) | p.location, &r[43]);
            r[17] = geometry_output_quad(ctx, p.record); cursor += 16;
            r[16] = geometry_command_word(ctx, p.record + 8, cursor);
            r[15] = geometry_output_quad(ctx, p.format.descriptor); r[7] = spu_rotqbyi(r[15], 13);
        }
        r[74] = spu_andi(r[7], 255); r[13] = spu_ai(r[13], 8); r[20] = spu_clgt(r[74], r[18]);
    }
}
} // namespace buzz
