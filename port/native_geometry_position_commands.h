/* Native position-only command emission (image 41, 2E8). */
#pragma once
#include "native_geometry_command_bindings.h"
#include "native_geometry_command_publish.h"

namespace buzz {
struct GeometryPositionCommands {
    GeometryCommandBuffer reservation;
    GeometryCommandFormat format;
    uint32_t stack = 0, metadata = 0, kind = 0, raster = 0, enabled = 0;
    uint32_t indices = 0, leading = 0, raw_end = 0, relocated_end = 0;
};
inline bool read_geometry_position_commands(const spu_context& ctx, GeometryPositionCommands& output)
{
    GeometryPositionCommands p;
    p.stack = ctx.gpr[1]._u32[0]; p.metadata = ctx.gpr[9]._u32[0]; p.indices = ctx.gpr[8]._u32[0];
    if (ctx.ls != ctx.ls_store || ctx.int_enable || (p.stack & 15) || p.stack < 0x170 || p.stack > SPU_LS_SIZE - 32) return false;
    u128 args[8]{};
    args[1] = spu_ai(ctx.gpr[1], -224); args[3] = ctx.gpr[3]; args[4] = spu_ai(args[1], 32);
    args[5] = ctx.gpr[4]; args[6] = ctx.gpr[5]; args[7] = spu_il(1);
    if (!read_geometry_command_buffer(ctx, p.reservation, args) ||
        !read_geometry_command_format(ctx, args[3]._u32[0], p.format)) return false;
    uint32_t metadata_base = 0, metadata_bytes = 0;
    if (p.metadata) {
        if (p.metadata & 3) return false;
        metadata_base = p.metadata & ~15u; metadata_bytes = ((p.metadata & 15) + 40 + 15) & ~15u;
        if (!geometry_output_range(metadata_base, metadata_bytes)) return false;
        p.kind = geometry_job_be32(ctx.ls + p.metadata + 32);
        p.raster = geometry_job_be32(ctx.ls + p.metadata + 36);
    }
    const uint32_t mask = ctx.gpr[12]._u32[0] & 65535;
    for (unsigned i = 0; i < p.format.count; ++i)
        p.enabled += geometry_command_attribute_enabled(mask, p.format.attributes[i].slot);
    const uint32_t offset = ctx.gpr[7]._u32[1] & 0x1fffffff;
    const uint32_t leading = (((offset + 127) & ~127u) - offset) / 2;
    p.leading = leading && p.indices > leading ? leading : 0;
    uint32_t remaining = p.indices - p.leading;
    // Bound large unpublished streams as well as ordinary published jobs.
    // A zero destination still assembles commands, potentially past the
    // logical reservation, without running the final publication stage.
    if (((uint64_t(remaining) + 255) / 256) * 4 > SPU_LS_SIZE - p.reservation.start) return false;
    GeometryCommandFootprint packets(p.reservation.start,args[5]._u32[0]!=0);
    if (p.reservation.first_ready < p.reservation.count) for (unsigned i = 0; i < 4; ++i) packets.add(1);
    for (unsigned i = 0; i < p.enabled * 2; ++i) packets.add(1);
    if (p.metadata && p.kind != 8) { packets.add(1); if (p.raster) packets.add(1); }
    packets.add(1); packets.add(2); packets.add(1); // cache, indices, begin
    if (p.leading) packets.add(1);
    while (remaining > 0x7ff00) { packets.add(2047); remaining -= 0x7ff00; }
    packets.add((remaining + 255) / 256); packets.add(1); // batches, end
    for (unsigned i = 0; i < p.enabled; ++i) packets.add(1);
    if (args[5]._u32[0] && geometry_job_be32(ctx.ls + p.reservation.records + 16)) { packets.add(1); packets.add(1); }
    const uint32_t raw=packets.raw,relocated=packets.relocated;
    // PUTF finalization can only pad forward. Its split loop also touches
    // physical padding through the end of the final 128-byte block.
    if (args[5]._u32[0] && relocated > p.reservation.limit) return false;
    const uint32_t touched = std::max(p.reservation.end, args[5]._u32[0] ?
        ((relocated + 127) & ~127u) + 128 : ((raw + 15) & ~15u));
    if (touched > SPU_LS_SIZE || raw > SPU_LS_SIZE) return false;
    const uint32_t starts[] = {p.reservation.context, p.format.descriptor, p.reservation.records,
        p.stack - 0x170, p.reservation.workspace, metadata_base};
    const uint32_t sizes[] = {0x160, p.format.bytes, 32, 0x190,
        touched - p.reservation.workspace, metadata_bytes};
    for (unsigned a = 0; a < 6; ++a) for (unsigned b = a + 1; b < 6; ++b)
        if (sizes[a] && sizes[b] && geometry_output_overlap(starts[a], sizes[a], starts[b], sizes[b])) return false;
    p.raw_end = raw; p.relocated_end = args[5]._u32[0] ? relocated : raw;
    output = p; return true;
}

inline void emit_geometry_position_commands(spu_context& ctx, const GeometryPositionCommands& p,
                                           const buzz_geometry_command_functions& functions)
{
    GeometryCommandAdmissionScope admission(ctx, p.reservation, p.raw_end, p.relocated_end, 0x368, 0x760);
    auto& r = ctx.gpr; const u128 old_stack = r[1];
    for (unsigned reg = 80; reg <= 88; ++reg) spu_ls_write128(&ctx, p.stack - (reg - 79) * 16, r[reg]);
    spu_ls_write128(&ctx, p.stack + 16, r[0]); spu_ls_write128(&ctx, p.stack - 0xa0, r[126]);
    spu_ls_write128(&ctx, p.stack - 0xe0, old_stack);
    r[80] = r[8]; r[81] = r[9]; r[82] = r[3]; r[83] = r[10]; r[86] = r[4]; r[87] = r[5]; r[88] = r[12];
    r[5] = r[4]; r[1] = spu_ai(old_stack, -224); r[84] = spu_shlqbyi(r[7], 8); r[85] = spu_shlqbyi(r[7], 4);
    r[4] = spu_ai(r[1], 32); spu_ls_write128(&ctx, r[1]._u32[0] + 0x30, r[6]);
    r[6] = r[87]; r[7] = spu_il(1); geometry_command_call(ctx, functions.prepare, 0x368);
    r[3] = r[82]; r[6] = r[88]; r[4] = spu_ai(r[1], 32); r[5] = spu_ai(r[1], 48);
    geometry_command_call(ctx, functions.bind, 0x380);
    if (!p.metadata) {
        r[49] = geometry_output_quad(ctx, r[1]._u32[0] + 0x20); r[18] = spu_rotqbyi(r[49], 8);
    } else {
        r[4] = spu_ai(r[81], 32); r[5] = geometry_output_quad(ctx, (p.metadata + 32) & ~15u);
        r[3] = spu_rotqby(r[5], r[81]); r[2] = spu_ceqi(r[3], 8);
        if (p.kind == 8) {
            r[49] = geometry_output_quad(ctx, r[1]._u32[0] + 0x20); r[18] = spu_rotqbyi(r[49], 8);
        } else {
            r[9] = spu_il(4); r[8] = spu_ai(r[81], 36); r[6] = geometry_output_quad(ctx, (p.metadata + 36) & ~15u);
            r[7] = spu_rotqby(r[6], r[8]);
            if (!p.raster) {
                r[16] = spu_splat_u32(0x4183c); r[19] = geometry_output_quad(ctx, r[1]._u32[0] + 0x20);
                r[10] = spu_cwd(r[1], 8); r[18] = spu_rotqbyi(r[19], 8);
                r[17] = spu_cwd(r[18], 0); r[13] = spu_cwd(r[18], 4);
                const uint32_t at = r[18]._u32[0];
                r[14] = geometry_command_word(ctx, at, 0x4183c, &r[15]);
                r[11] = geometry_command_word(ctx, at + 4, 0, &r[12]); r[18] = spu_ai(r[18], 8);
                r[4] = geometry_command_word(ctx, r[1]._u32[0] + 0x28, at + 8, &r[9]);
            } else {
                r[27] = spu_splat_u32(0x4183c); r[29] = geometry_output_quad(ctx, r[1]._u32[0] + 0x20);
                r[23] = spu_il(1); r[30] = spu_il(2305); r[20] = spu_ceq(r[7], r[30]);
                r[10] = spu_rotqbyi(r[29], 8); r[8] = spu_ai(r[10], 8);
                r[28] = spu_cwd(r[10], 0); r[24] = spu_cwd(r[10], 4);
                const uint32_t at = r[10]._u32[0];
                r[25] = geometry_command_word(ctx, at, 0x4183c, &r[26]);
                r[21] = geometry_command_word(ctx, at + 4, 1, &r[22]);
                if (p.raster == 2305) {
                    r[34] = spu_splat_u32(0x41830); r[37] = spu_ceqbi(r[83], 0);
                    r[35] = spu_cwd(r[8], 0); r[36] = spu_xsbh(r[37]); r[6] = spu_cwd(r[8], 4);
                    r[18] = spu_ai(r[10], 16); r[7] = spu_cwd(r[1], 8); r[32] = spu_ceqhi(r[36], 0);
                    r[2] = spu_il(1029); r[3] = spu_il(1028);
                    r[31] = geometry_command_word(ctx, at + 8, 0x41830, &r[33]); r[4] = spu_fsm(r[32]);
                } else {
                    r[41] = spu_splat_u32(0x41830); r[40] = geometry_output_quad(ctx, (at + 8) & ~15u);
                    r[44] = spu_ceqbi(r[83], 0); r[42] = spu_cwd(r[8], 0); r[6] = spu_cwd(r[8], 4);
                    r[43] = spu_xsbh(r[44]); r[7] = spu_cwd(r[1], 8); r[18] = spu_ai(r[10], 16);
                    r[39] = spu_ceqhi(r[43], 0); r[2] = spu_il(1028); r[3] = spu_il(1029); r[4] = spu_fsm(r[39]);
                    r[38] = geometry_command_word(ctx, at + 8, 0x41830);
                }
                r[5] = geometry_output_quad(ctx, (at + 12) & ~15u); r[48] = spu_selb(r[2], r[3], r[4]);
                r[47] = geometry_command_word(ctx, at + 12, r[48]._u32[0]);
                r[45] = geometry_command_word(ctx, r[1]._u32[0] + 0x28, at + 16, &r[46]);
            }
        }
    }
    // Cache invalidation, index buffer, and triangle-strip begin.
    const uint32_t begin = r[18]._u32[0];
    r[13] = spu_splat_u32(0x41714); r[20] = spu_il(4); r[14] = spu_cwd(r[18], 0);
    r[62] = spu_cwd(r[18], 4); r[4] = spu_il(0); r[7] = spu_cwd(r[18], 8); r[79] = spu_il(8);
    r[22] = spu_cwd(r[1], 8); r[5] = spu_ilhu(8); r[21] = r[18]; r[68] = spu_ai(r[18], 8);
    r[11] = geometry_command_word(ctx, begin, 0x41714, &r[12]); r[5] = spu_iohl(r[5], 0x181c);
    r[6] = spu_splat_u32(0x1fffffff); r[73] = spu_cwd(r[68], 4); r[9] = spu_andi(r[84], 255);
    r[66] = spu_cwd(r[68], 8); r[65] = spu_ori(r[9], 16); r[72] = spu_and(r[85], r[6]);
    r[58] = spu_il(20); r[61] = spu_splat_u32(0x41808); r[53] = spu_ai(r[18], 20);
    r[2] = spu_ai(r[72], 127); r[56] = spu_il(5); r[57] = spu_cwd(r[53], 4);
    r[3] = geometry_command_word(ctx, begin + 4, 0, &r[8]); r[85] = spu_andi(r[2], -128);
    r[84] = spu_sf(r[72], r[85]); r[83] = geometry_output_quad(ctx, (begin + 8) & ~15u);
    r[19] = spu_rotmi(r[84], -1); r[78] = spu_ceqi(r[19], 0); r[76] = spu_clgt(r[80], r[19]);
    r[81] = geometry_command_word(ctx, begin + 8, 0x8181c); r[77] = spu_nor(r[78], r[78]);
    r[75] = spu_sfi(r[76], 0); r[74] = spu_sfi(r[77], 0); r[70] = spu_and(r[74], r[75]);
    r[67] = spu_ceqbi(r[70], 0); r[50] = spu_xsbh(r[67]);
    r[69] = geometry_command_word(ctx, begin + 12, r[72]._u32[0], &r[71]);
    r[63] = geometry_command_word(ctx, begin + 16, r[65]._u32[0], &r[64]);
    r[59] = geometry_command_word(ctx, begin + 20, 0x41808, &r[60]); r[18] = spu_ai(r[18], 28);
    r[54] = geometry_command_word(ctx, begin + 24, 5, &r[55]);
    r[51] = geometry_command_word(ctx, r[1]._u32[0] + 0x28, begin + 28, &r[52]);
    if (p.leading) {
        r[26] = spu_il(28); r[29] = spu_cwd(r[21], 12); r[10] = spu_splat_u32(0x41824);
        r[25] = spu_cwd(r[18], 4); r[30] = spu_ai(r[19], -1); r[24] = spu_shli(r[30], 24);
        r[80] = spu_sf(r[19], r[80]);
        r[27] = geometry_command_word(ctx, begin + 28, 0x41824, &r[28]);
        r[17] = geometry_command_word(ctx, begin + 32, r[24]._u32[0], &r[23]); r[18] = spu_ai(r[21], 36);
        r[15] = geometry_command_word(ctx, r[1]._u32[0] + 0x28, begin + 36, &r[16]);
    } else r[19] = spu_il(0);
    r[42] = spu_splat_u32(0x7ff00); r[9] = r[18]; r[41] = spu_clgt(r[80], r[42]);
    while (r[80]._u32[0] > 0x7ff00) {
        r[31] = spu_splat_u32(0x5ffc1824); r[34] = spu_ilhu(0xfff8); r[32] = spu_cwd(r[9], 0);
        r[33] = spu_ori(r[34], 256); r[6] = r[9]; r[21] = spu_il(8188); r[7] = r[19];
        r[80] = spu_a(r[80], r[33]); r[8] = spu_a(r[9], r[21]); r[10] = spu_ilhu(0xff00);
        r[20] = geometry_command_word(ctx, r[9]._u32[0], 0x5ffc1824, &r[22]);
        const uint32_t at = r[9]._u32[0], first_index = r[19]._u32[0];
        // Only the final iteration's scratch vectors survive. Generate the
        // preceding batch words directly, preserving the final RMW snapshot.
        for (unsigned i = 0; i < 2046; ++i) geometry_job_put32(ctx.ls + at + 4 + i * 4, 0xff000000u | (first_index + i * 256));
        r[6] = spu_ai(r[6], 2046 * 4); r[7] = spu_ai(r[7], 2046 * 256);
        r[36] = spu_il(4); r[40] = spu_cwd(r[6], 4); r[39] = spu_or(r[7], r[10]); r[7] = spu_ai(r[7], 256);
        r[37] = geometry_command_word(ctx, r[6]._u32[0] + 4, r[39]._u32[0], &r[38]); r[6] = spu_ai(r[6], 4);
        r[35] = spu_ceq(r[8], r[6]); r[44] = spu_splat_u32(0x7ff00); r[45] = spu_il(8192);
        r[43] = spu_clgt(r[80], r[44]); r[9] = spu_a(r[9], r[45]); r[19] = spu_a(r[19], r[44]);
        if (r[80]._u32[0] <= 0x7ff00) {
            r[18] = r[9]; r[48] = spu_cwd(r[1], 8);
            r[46] = geometry_command_word(ctx, r[1]._u32[0] + 0x28, r[9]._u32[0], &r[47]);
        }
    }
    const uint32_t batch_start = r[18]._u32[0];
    r[60] = spu_ai(r[80], 255); r[58] = spu_splat_u32(0x40001824); r[56] = spu_cwd(r[18], 0);
    r[59] = spu_rotmi(r[60], -8); r[52] = spu_cwd(r[1], 8); r[49] = spu_clgti(r[80], 256);
    r[57] = spu_shli(r[59], 18); r[55] = spu_or(r[57], r[58]);
    r[53] = geometry_command_word(ctx, batch_start, r[55]._u32[0], &r[54]); r[18] = spu_ai(r[18], 4);
    r[50] = geometry_command_word(ctx, r[1]._u32[0] + 0x28, batch_start + 4, &r[51]);
    if (r[80]._u32[0] > 256) {
        r[7] = r[18]; r[6] = spu_ilhu(0xff00);
        const unsigned full = (r[80]._u32[0] - 1) / 256;
        for (unsigned i = 0; i < full; ++i) {
            r[63] = spu_or(r[19], r[6]); r[80] = spu_ai(r[80], -256); r[64] = spu_cwd(r[7], 0);
            r[19] = spu_ai(r[19], 256); r[18] = spu_clgti(r[80], 256);
            r[61] = geometry_command_word(ctx, r[7]._u32[0], r[63]._u32[0], &r[62]); r[7] = spu_ai(r[7], 4);
        }
        r[18] = r[7]; r[67] = spu_cwd(r[1], 8);
        r[65] = geometry_command_word(ctx, r[1]._u32[0] + 0x28, r[7]._u32[0], &r[66]);
    }
    if (r[80]._u32[0]) {
        r[74] = spu_ai(r[80], -1); r[72] = spu_cwd(r[18], 0); r[73] = spu_shli(r[74], 24);
        r[68] = spu_cwd(r[1], 8); r[71] = spu_or(r[73], r[19]);
        r[69] = geometry_command_word(ctx, r[18]._u32[0], r[71]._u32[0], &r[70]); r[18] = spu_ai(r[18], 4);
        r[80] = geometry_command_word(ctx, r[1]._u32[0] + 0x28, r[18]._u32[0], &r[19]);
    }
    const uint32_t end = r[18]._u32[0];
    r[2] = spu_splat_u32(0x41808); r[78] = spu_il(4); r[3] = spu_cwd(r[18], 0);
    r[84] = spu_cwd(r[18], 4); r[83] = spu_il(0); r[76] = spu_ai(r[18], 8); r[77] = spu_cwd(r[1], 8);
    r[5] = r[88]; r[4] = spu_ai(r[1], 32); r[7] = geometry_command_word(ctx, end, 0x41808, &r[85]); r[3] = r[82];
    r[79] = geometry_command_word(ctx, end + 4, 0, &r[81]);
    r[88] = geometry_command_word(ctx, r[1]._u32[0] + 0x28, end + 8, &r[75]);
    geometry_command_call(ctx, functions.reset, 0x748);
    r[4] = spu_ai(r[1], 32); r[3] = r[82]; r[5] = r[86]; r[6] = r[87]; r[7] = spu_il(1);
    geometry_command_call(ctx, functions.publish, 0x760);
    r[0] = spu_ls_read128(&ctx, p.stack + 16); r[1] = spu_ai(r[1], 224);
    for (unsigned reg = 80; reg <= 88; ++reg) r[reg] = spu_ls_read128(&ctx, p.stack - (reg - 79) * 16);
    r[126] = spu_ls_read128(&ctx, p.stack - 0xa0);
}
} // namespace buzz
