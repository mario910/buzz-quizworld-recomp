/* Host multipass command emission (image 41, 12F0). */
#pragma once
#include "native_geometry_position_commands.h"

namespace buzz {
struct GeometryFullPass { uint32_t state[16]{}; };
struct GeometryFullCommands {
    GeometryCommandBuffer reservation;
    GeometryCommandFormat format;
    std::vector<GeometryFullPass> passes;
    uint32_t stack = 0, metadata = 0, groups = 0, enabled = 0, indices = 0, leading = 0;
    uint32_t raw_end = 0, relocated_end = 0;
};
inline bool read_geometry_full_commands(const spu_context& ctx, GeometryFullCommands& output)
{
    GeometryFullCommands p;
    p.stack = ctx.gpr[1]._u32[0]; p.metadata = ctx.gpr[9]._u32[0]; p.indices = ctx.gpr[8]._u32[0];
    // The lift's GB/CGTI sequence selects word 1, not the preferred word.
    p.groups = ~ctx.gpr[11]._u32[1] & 15;
    if (ctx.ls != ctx.ls_store || ctx.int_enable || (p.stack & 15) || p.stack < 0x180 || p.stack > SPU_LS_SIZE - 32) return false;
    u128 args[8]{};
    args[1] = spu_ai(ctx.gpr[1], -240); args[3] = ctx.gpr[3]; args[4] = spu_ai(args[1], 32);
    args[5] = ctx.gpr[4]; args[6] = ctx.gpr[5]; args[7] = spu_il(1);
    if (!read_geometry_command_buffer(ctx, p.reservation, args) ||
        !read_geometry_command_format(ctx, args[3]._u32[0], p.format)) return false;
    uint32_t count = 1, metadata_base = 0, metadata_bytes = 0;
    if (p.metadata) {
        if ((p.metadata & 3) || !geometry_output_range(p.metadata & ~15u, 16)) return false;
        count = geometry_job_be32(ctx.ls + p.metadata);
        const uint64_t span = uint64_t(p.metadata & 15) + 16 + uint64_t(count) * 64;
        if (!count || span > SPU_LS_SIZE) return false;
        metadata_base = p.metadata & ~15u; metadata_bytes = (uint32_t(span) + 15) & ~15u;
        if (!geometry_output_range(metadata_base, metadata_bytes)) return false;
    }
    for (unsigned i = 0; i < p.format.count; ++i)
        p.enabled += geometry_command_attribute_enabled(ctx.gpr[12]._u32[0] & 65535, p.format.attributes[i].slot);
    const uint32_t offset = ctx.gpr[7]._u32[1] & 0x1fffffff;
    const uint32_t leading = (((offset + 127) & ~127u) - offset) / 2;
    p.leading = leading && p.indices > leading ? leading : 0;
    const uint32_t remaining = p.indices - p.leading;
    const uint64_t minimum = uint64_t(count) * (40 + (p.leading ? 8 : 0) + ((uint64_t(remaining) + 255) / 256) * 4);
    if (minimum > SPU_LS_SIZE - p.reservation.start) return false;
    std::vector<unsigned> packets;
    if (p.reservation.first_ready < p.reservation.count) for (unsigned i = 0; i < 4; ++i) packets.push_back(1);
    for (unsigned i = 0; i < p.enabled * 2; ++i) packets.push_back(1);
    for (unsigned pass = 0; pass < count; ++pass) {
        GeometryFullPass entry;
        if (p.metadata) {
            for (unsigned i = 0; i < 16; ++i) entry.state[i] = geometry_job_be32(ctx.ls + p.metadata + 16 + pass * 64 + i * 4);
            if (p.groups & 2) { if (entry.state[0] != 8) packets.push_back(1); if (entry.state[2] != 8) packets.push_back(2); }
            if (p.groups & 1) { if (entry.state[12] != 8) packets.push_back(1); if (entry.state[14] != 8) packets.push_back(1); }
            if (p.groups & 4) { if (entry.state[6] != 8) packets.push_back(1); if (entry.state[8] != 8) packets.push_back(2); }
            if ((p.groups & 8) && entry.state[4] != 8) { packets.push_back(1); if (entry.state[5]) packets.push_back(1); }
        }
        packets.push_back(1); packets.push_back(2); packets.push_back(1);
        if (p.leading) packets.push_back(1);
        uint32_t left = remaining;
        while (left > 0x7ff00) { packets.push_back(2047); left -= 0x7ff00; }
        packets.push_back((left + 255) / 256); packets.push_back(1);
        p.passes.push_back(entry);
    }
    for (unsigned i = 0; i < p.enabled; ++i) packets.push_back(1);
    if (args[5]._u32[0] && geometry_job_be32(ctx.ls + p.reservation.records + 16)) { packets.push_back(1); packets.push_back(1); }
    uint32_t raw = p.reservation.start, relocated = raw;
    for (unsigned words : packets) {
        raw += 4 * (words + 1);
        if (!args[5]._u32[0]) { relocated = raw; continue; }
        unsigned bytes = words * 4;
        while (bytes > 124 - (relocated & 127)) {
            const unsigned available = 124 - (relocated & 127);
            bytes -= available; relocated += available + 4;
        }
        relocated += bytes + 4;
    }
    if (args[5]._u32[0] && relocated > p.reservation.limit) return false;
    const uint32_t touched = std::max(p.reservation.end, args[5]._u32[0] ?
        ((relocated + 127) & ~127u) + 128 : ((raw + 15) & ~15u));
    if (raw > SPU_LS_SIZE || touched > SPU_LS_SIZE) return false;
    const uint32_t starts[] = {p.reservation.context, p.format.descriptor, p.reservation.records,
        p.stack - 0x180, p.reservation.workspace, metadata_base};
    const uint32_t sizes[] = {0x160, p.format.bytes, 32, 0x1a0, touched - p.reservation.workspace, metadata_bytes};
    for (unsigned a = 0; a < 6; ++a) for (unsigned b = a + 1; b < 6; ++b)
        if (sizes[a] && sizes[b] && geometry_output_overlap(starts[a], sizes[a], starts[b], sizes[b])) return false;
    p.raw_end = raw; p.relocated_end = relocated;
    output = std::move(p); return true;
}

inline void emit_geometry_full_commands(spu_context& ctx, const GeometryFullCommands& p,
                                       const buzz_geometry_command_functions& functions)
{
    auto& r = ctx.gpr; const u128 old_stack = r[1];
    spu_ls_write128(&ctx, p.stack + 16, r[0]);
    for (unsigned reg = 80; reg <= 89; ++reg) spu_ls_write128(&ctx, p.stack - (reg - 79) * 16, r[reg]);
    spu_ls_write128(&ctx, p.stack - 0xb0, r[126]); spu_ls_write128(&ctx, p.stack - 0xf0, old_stack);
    r[81] = r[11]; r[82] = r[10]; r[84] = r[3]; r[85] = r[4]; r[86] = r[5]; r[5] = r[85];
    r[88] = r[9]; r[87] = r[8]; r[89] = r[12]; r[82] = spu_ceqbi(r[82], 0);
    r[1] = spu_ai(old_stack, -240); r[83] = spu_shlqbyi(r[7], 4); r[82] = spu_xsbh(r[82]); r[80] = spu_shlqbyi(r[7], 8);
    r[4] = spu_ai(r[1], 32); spu_ls_write128(&ctx, r[1]._u32[0] + 0x30, r[6]);
    r[6] = r[86]; r[7] = spu_il(1); geometry_command_call(ctx, functions.prepare, 0x1380);
    r[3] = r[84]; r[6] = r[89]; r[4] = spu_ai(r[1], 32); r[5] = spu_ai(r[1], 48);
    geometry_command_call(ctx, functions.bind, 0x1398);
    r[18] = spu_splat_u32(0x1fffffff); r[40] = spu_fsmbi(0x101); r[7] = spu_il(0); r[37] = spu_fsmbi(0x101);
    r[35] = spu_fsmbi(0x101); r[26] = spu_ceqi(r[88], 0); r[32] = spu_fsmbi(0x101); r[18] = spu_and(r[83], r[18]);
    r[39] = geometry_output_quad(ctx, p.metadata & ~15u); r[30] = spu_andbi(r[40], 2);
    r[38] = geometry_output_quad(ctx, r[1]._u32[0] + 0x20); r[36] = spu_ai(r[18], 127);
    r[21] = spu_andhi(r[37], 257); r[34] = spu_andi(r[36], -128); r[19] = spu_andbi(r[35], 4);
    r[20] = spu_sf(r[18], r[34]); r[13] = spu_rotqby(r[39], r[88]); r[31] = spu_andbi(r[32], 8);
    r[15] = spu_rotqbyi(r[38], 8); r[20] = spu_rotmi(r[20], -1); r[39] = spu_il(0);
    r[12] = spu_and(r[81], r[31]); r[5] = spu_and(r[81], r[30]); r[6] = spu_and(r[81], r[21]);
    r[29] = spu_ceqi(r[20], 0); r[11] = spu_and(r[81], r[19]); r[25] = spu_nor(r[29], r[29]);
    r[28] = spu_clgt(r[87], r[20]); r[22] = spu_sfi(r[25], 0); r[24] = spu_sfi(r[28], 0);
    r[9] = spu_ceq(r[12], r[7]); r[8] = spu_ceq(r[5], r[7]); r[4] = spu_ceq(r[6], r[7]);
    r[33] = spu_gb(r[9]); r[2] = spu_ceq(r[11], r[7]); r[23] = spu_gb(r[8]); r[17] = spu_and(r[22], r[24]);
    r[27] = spu_gb(r[4]); r[24] = spu_ilhu(4); r[81] = spu_gb(r[2]); r[25] = spu_ilhu(8);
    r[16] = spu_ai(r[20], -1); r[28] = r[24]; r[10] = spu_nor(r[26], r[26]); r[26] = spu_fsmbi(0x7777);
    r[29] = r[24]; r[34] = r[24]; r[37] = r[24]; r[38] = r[24]; r[40] = r[25]; r[30] = r[24]; r[31] = r[25];
    r[41] = r[24]; r[44] = r[24]; r[80] = spu_andi(r[80], 255); r[3] = spu_ceqbi(r[17], 0);
    r[45] = spu_ilhu(0xfff8); r[46] = spu_ilhu(0x5ffc); r[48] = spu_ilhu(7); r[50] = spu_ilhu(0x4000);
    r[14] = spu_il(1); r[19] = spu_shli(r[16], 24); r[21] = spu_selb(r[14], r[13], r[10]);
    r[33] = spu_cgti(r[33], 11); r[80] = spu_ori(r[80], 16); r[23] = spu_cgti(r[23], 11);
    r[24] = spu_iohl(r[24], 0x304); r[25] = spu_iohl(r[25], 0x308); r[27] = spu_cgti(r[27], 11);
    r[28] = spu_iohl(r[28], 0xa74); r[29] = spu_iohl(r[29], 0xa70); r[81] = spu_cgti(r[81], 11);
    r[30] = spu_iohl(r[30], 0x310); r[31] = spu_iohl(r[31], 0x314); r[34] = spu_iohl(r[34], 0x183c);
    r[37] = spu_iohl(r[37], 0x1830); r[38] = spu_iohl(r[38], 0x1714); r[40] = spu_iohl(r[40], 0x181c);
    r[41] = spu_iohl(r[41], 0x1808); r[43] = spu_xsbh(r[3]); r[44] = spu_iohl(r[44], 0x1824);
    r[45] = spu_ori(r[45], 256); r[46] = spu_iohl(r[46], 0x1824); r[48] = spu_iohl(r[48], 0xff00);
    r[50] = spu_iohl(r[50], 0x1824); r[22] = spu_sfi(r[10], 0); r[32] = spu_ila(65535);
    r[35] = spu_il(1); r[36] = spu_il(2305); r[42] = spu_il(5); r[49] = spu_il(8192); r[51] = spu_il(16); r[47] = spu_il(8188);
    for (unsigned pass = 0; pass < p.passes.size(); ++pass) {
        const auto& state = p.passes[pass].state;
        r[21] = spu_ai(r[21], -1);
        if (p.metadata) {
            r[13] = spu_a(r[88], r[51]); const uint32_t at = r[13]._u32[0];
            if (p.groups & 2) {
                r[54] = geometry_output_quad(ctx, at & ~15u); r[53] = spu_rotqby(r[54], r[13]); r[52] = spu_ceqi(r[53], 8);
                if (state[0] != 8) {
                    r[55] = spu_il(4); r[62] = spu_ai(r[13], 4); r[64] = spu_cwd(r[15], 0);
                    r[61] = geometry_output_quad(ctx, (at + 4) & ~15u); r[59] = spu_cwd(r[15], 4); r[58] = spu_rotqby(r[61], r[62]);
                    r[60] = geometry_command_word(ctx, r[15]._u32[0], 0x40304, &r[63]);
                    r[56] = geometry_command_word(ctx, r[15]._u32[0] + 4, state[1], &r[57]); r[15] = spu_ai(r[15], 8);
                }
                r[9] = spu_il(8); r[3] = spu_ai(r[13], 8); r[67] = geometry_output_quad(ctx, (at + 8) & ~15u);
                r[66] = spu_rotqby(r[67], r[3]); r[65] = spu_ceqi(r[66], 8);
                if (state[2] != 8) {
                    r[72] = spu_il(4); r[10] = spu_ai(r[13], 12); r[11] = spu_cwd(r[15], 0);
                    r[79] = geometry_output_quad(ctx, (at + 12) & ~15u); r[76] = spu_cwd(r[15], 4); r[71] = spu_cwd(r[15], 8);
                    r[77] = spu_rotqby(r[79], r[10]); r[78] = geometry_command_word(ctx, r[15]._u32[0], 0x80308, &r[2]);
                    r[75] = spu_and(r[77], r[26]); r[70] = spu_rotmi(r[77], -24);
                    r[73] = geometry_command_word(ctx, r[15]._u32[0] + 4, state[3] & 0xffffff, &r[74]);
                    r[68] = geometry_command_word(ctx, r[15]._u32[0] + 8, state[3] >> 24, &r[69]); r[15] = spu_ai(r[15], 12);
                }
            }
            if (p.groups & 1) {
                r[3] = spu_ai(r[13], 48); r[6] = geometry_output_quad(ctx, (at + 48) & ~15u);
                r[4] = spu_rotqby(r[6], r[13]); r[7] = spu_ceqi(r[4], 8);
                if (state[12] != 8) {
                    r[8] = spu_il(4); r[17] = spu_ai(r[13], 52); r[52] = spu_cwd(r[15], 0);
                    r[16] = geometry_output_quad(ctx, (at + 52) & ~15u); r[12] = spu_cwd(r[15], 4); r[3] = spu_rotqby(r[16], r[17]);
                    r[14] = geometry_command_word(ctx, r[15]._u32[0], 0x40a74, &r[83]);
                    r[5] = geometry_command_word(ctx, r[15]._u32[0] + 4, state[13], &r[9]); r[15] = spu_ai(r[15], 8);
                }
                r[56] = spu_il(56); r[8] = spu_ai(r[13], 56); r[55] = geometry_output_quad(ctx, (at + 56) & ~15u);
                r[54] = spu_rotqby(r[55], r[8]); r[53] = spu_ceqi(r[54], 8);
                if (state[14] != 8) {
                    r[57] = spu_il(4); r[64] = spu_ai(r[13], 60); r[66] = spu_cwd(r[15], 0);
                    r[63] = geometry_output_quad(ctx, (at + 60) & ~15u); r[61] = spu_cwd(r[15], 4); r[60] = spu_rotqby(r[63], r[64]);
                    r[62] = geometry_command_word(ctx, r[15]._u32[0], 0x40a70, &r[65]);
                    r[58] = geometry_command_word(ctx, r[15]._u32[0] + 4, state[15], &r[59]); r[15] = spu_ai(r[15], 8);
                }
            }
            if (p.groups & 4) {
                r[70] = spu_il(24); r[8] = spu_ai(r[13], 24); r[69] = geometry_output_quad(ctx, (at + 24) & ~15u);
                r[68] = spu_rotqby(r[69], r[8]); r[67] = spu_ceqi(r[68], 8);
                if (state[6] != 8) {
                    r[71] = spu_il(4); r[78] = spu_ai(r[13], 28); r[10] = spu_cwd(r[15], 0);
                    r[77] = geometry_output_quad(ctx, (at + 28) & ~15u); r[75] = spu_cwd(r[15], 4); r[74] = spu_rotqby(r[77], r[78]);
                    r[76] = geometry_command_word(ctx, r[15]._u32[0], 0x40310, &r[79]);
                    r[72] = geometry_command_word(ctx, r[15]._u32[0] + 4, state[7], &r[73]); r[15] = spu_ai(r[15], 8);
                }
                r[3] = spu_ai(r[13], 32); r[7] = geometry_output_quad(ctx, (at + 32) & ~15u); r[11] = spu_rotqby(r[7], r[13]);
                r[2] = spu_ceqi(r[11], 8);
                if (state[8] != 8) {
                    r[16] = spu_il(4); r[61] = spu_ai(r[13], 36); r[63] = spu_cwd(r[15], 0); r[64] = spu_il(44);
                    r[60] = geometry_output_quad(ctx, (at + 36) & ~15u); r[57] = spu_ai(r[13], 44); r[9] = spu_cwd(r[15], 4);
                    r[12] = spu_il(8); r[56] = geometry_output_quad(ctx, (at + 44) & ~15u); r[14] = spu_cwd(r[15], 8);
                    r[59] = spu_rotqby(r[60], r[61]); r[58] = geometry_command_word(ctx, r[15]._u32[0], 0x80314, &r[62]);
                    r[53] = spu_and(r[59], r[32]); r[55] = spu_rotqby(r[56], r[57]); r[54] = spu_shli(r[53], 16);
                    r[17] = spu_and(r[55], r[32]); r[3] = spu_or(r[53], r[54]); r[83] = spu_shli(r[17], 16); r[4] = spu_or(r[17], r[83]);
                    const uint32_t first = (state[9] & 65535) * 0x10001, last = (state[11] & 65535) * 0x10001;
                    r[5] = geometry_command_word(ctx, r[15]._u32[0] + 4, first, &r[52]);
                    r[6] = geometry_command_word(ctx, r[15]._u32[0] + 8, last, &r[8]); r[15] = spu_ai(r[15], 12);
                }
            }
            if (p.groups & 8) {
                r[4] = spu_ai(r[13], 16); r[67] = geometry_output_quad(ctx, (at + 16) & ~15u);
                r[66] = spu_rotqby(r[67], r[13]); r[65] = spu_ceqi(r[66], 8);
                if (state[4] != 8) {
                    r[8] = spu_il(4); r[68] = spu_ai(r[13], 20); r[13] = geometry_output_quad(ctx, (at + 20) & ~15u); r[5] = spu_rotqby(r[13], r[68]);
                    if (!state[5]) {
                        r[74] = spu_cwd(r[15], 0); r[71] = spu_cwd(r[15], 4);
                        r[72] = geometry_command_word(ctx, r[15]._u32[0], 0x4183c, &r[73]);
                        r[69] = geometry_command_word(ctx, r[15]._u32[0] + 4, 0, &r[70]); r[15] = spu_ai(r[15], 8);
                    } else {
                        r[75] = spu_ceq(r[5], r[36]); r[7] = spu_ai(r[15], 8); r[2] = spu_cwd(r[15], 0); r[78] = spu_cwd(r[15], 4);
                        r[79] = geometry_command_word(ctx, r[15]._u32[0], 0x4183c, &r[10]);
                        r[76] = geometry_command_word(ctx, r[15]._u32[0] + 4, 1, &r[77]);
                        if (state[5] == 2305) {
                            r[4] = spu_ceqhi(r[82], 0); r[15] = spu_ai(r[15], 16); r[11] = geometry_output_quad(ctx, r[7]._u32[0] & ~15u);
                            r[3] = spu_il(1029); r[12] = spu_cwd(r[7], 0); r[2] = spu_il(1028); r[6] = spu_cwd(r[7], 4);
                            r[4] = spu_fsm(r[4]); r[5] = spu_shufb(r[37], r[11], r[12]);
                        } else {
                            r[17] = spu_ceqhi(r[82], 0); r[14] = geometry_output_quad(ctx, r[7]._u32[0] & ~15u); r[15] = spu_ai(r[15], 16);
                            r[16] = spu_cwd(r[7], 0); r[3] = spu_il(1028); r[6] = spu_cwd(r[7], 4); r[2] = spu_il(1029);
                            r[4] = spu_fsm(r[17]); r[5] = spu_shufb(r[37], r[14], r[16]);
                        }
                        r[3] = spu_selb(r[3], r[2], r[4]); spu_ls_write128(&ctx, r[7]._u32[0], r[5]);
                        r[83] = geometry_command_word(ctx, r[7]._u32[0] + 4, r[3]._u32[0], &r[52]);
                    }
                }
            }
        }
        const uint32_t begin = r[15]._u32[0];
        r[53] = spu_il(4); r[65] = spu_il(8); r[71] = spu_cwd(r[15], 0); r[61] = spu_ai(r[15], 8); r[57] = spu_cwd(r[15], 4);
        r[55] = spu_il(20); r[5] = spu_cwd(r[15], 8); r[6] = spu_ai(r[15], 20); r[64] = spu_cwd(r[61], 4);
        r[17] = spu_ai(r[15], 28); r[60] = spu_cwd(r[61], 8); r[14] = r[87]; r[16] = spu_il(0); r[54] = spu_cwd(r[6], 4);
        r[69] = geometry_command_word(ctx, begin, 0x41714, &r[70]);
        r[13] = geometry_command_word(ctx, begin + 4, 0, &r[68]); r[66] = geometry_command_word(ctx, begin + 8, 0x8181c, &r[67]);
        r[62] = geometry_command_word(ctx, begin + 12, r[18]._u32[0], &r[63]);
        r[58] = geometry_command_word(ctx, begin + 16, r[80]._u32[0], &r[59]);
        r[9] = geometry_command_word(ctx, begin + 20, 0x41808, &r[56]); r[7] = geometry_command_word(ctx, begin + 24, 5, &r[8]);
        if (p.leading) {
            r[75] = spu_il(28); r[78] = spu_cwd(r[15], 12); r[14] = spu_sf(r[20], r[87]); r[74] = spu_cwd(r[17], 4);
            r[16] = r[20]; r[17] = spu_ai(r[15], 36); r[76] = geometry_command_word(ctx, begin + 28, 0x41824, &r[77]);
            r[72] = geometry_command_word(ctx, begin + 32, r[19]._u32[0], &r[73]);
        }
        r[3] = spu_clgt(r[14], r[48]); r[10] = r[17];
        while (r[14]._u32[0] > 0x7ff00) {
            r[14] = spu_a(r[14], r[45]); r[6] = r[10]; r[7] = r[16]; r[2] = spu_cwd(r[10], 0); r[9] = spu_ilhu(0xff00);
            r[8] = spu_a(r[10], r[47]); r[15] = geometry_command_word(ctx, r[10]._u32[0], 0x5ffc1824, &r[79]);
            const uint32_t at = r[10]._u32[0], index = r[16]._u32[0];
            for (unsigned i = 0; i < 2046; ++i) geometry_job_put32(ctx.ls + at + 4 + i * 4, 0xff000000u | (index + i * 256));
            r[6] = spu_ai(r[6], 2046 * 4); r[7] = spu_ai(r[7], 2046 * 256); r[12] = spu_il(4); r[52] = spu_cwd(r[6], 4);
            r[83] = spu_or(r[7], r[9]); r[7] = spu_ai(r[7], 256);
            r[4] = geometry_command_word(ctx, r[6]._u32[0] + 4, r[83]._u32[0], &r[17]); r[6] = spu_ai(r[6], 4);
            r[11] = spu_ceq(r[8], r[6]); r[53] = spu_clgt(r[14], r[48]); r[16] = spu_a(r[16], r[48]); r[10] = spu_a(r[10], r[49]);
            if (r[14]._u32[0] <= 0x7ff00) r[17] = r[10];
        }
        r[6] = spu_ai(r[14], 255); r[10] = spu_clgti(r[14], 256); r[56] = spu_rotmi(r[6], -8);
        r[55] = spu_cwd(r[17], 0); r[6] = spu_ai(r[17], 4); r[9] = spu_shli(r[56], 18); r[54] = spu_or(r[9], r[50]);
        r[7] = geometry_command_word(ctx, r[17]._u32[0], r[54]._u32[0], &r[8]);
        if (r[14]._u32[0] > 256) {
            r[7] = spu_ilhu(0xff00); const unsigned full = (r[14]._u32[0] - 1) / 256;
            for (unsigned i = 0; i < full; ++i) {
                r[60] = spu_or(r[16], r[7]); r[14] = spu_ai(r[14], -256); r[61] = spu_cwd(r[6], 0);
                r[16] = spu_ai(r[16], 256); r[57] = spu_clgti(r[14], 256);
                r[58] = geometry_command_word(ctx, r[6]._u32[0], r[60]._u32[0], &r[59]); r[6] = spu_ai(r[6], 4);
            }
        }
        if (r[14]._u32[0]) {
            r[66] = spu_ai(r[14], -1); r[65] = spu_shli(r[66], 24); r[64] = spu_cwd(r[6], 0); r[63] = spu_or(r[65], r[16]);
            r[14] = geometry_command_word(ctx, r[6]._u32[0], r[63]._u32[0], &r[62]); r[6] = spu_ai(r[6], 4);
        }
        r[16] = spu_il(4); r[15] = spu_ai(r[6], 8); r[70] = spu_cwd(r[6], 0); r[13] = spu_cwd(r[6], 4);
        r[68] = geometry_command_word(ctx, r[6]._u32[0], 0x41808, &r[69]);
        r[67] = geometry_command_word(ctx, r[6]._u32[0] + 4, 0, &r[5]);
        if (pass + 1 < p.passes.size()) r[51] = spu_ai(r[51], 64);
    }
    r[5] = r[89]; r[44] = spu_cwd(r[1], 8); r[3] = r[84]; r[4] = spu_ai(r[1], 32);
    r[42] = geometry_command_word(ctx, r[1]._u32[0] + 0x28, r[15]._u32[0], &r[43]);
    geometry_command_call(ctx, functions.reset, 0x1a50);
    r[4] = spu_ai(r[1], 32); r[3] = r[84]; r[5] = r[85]; r[6] = r[86]; r[7] = spu_il(1);
    geometry_command_call(ctx, functions.publish, 0x1a68);
    r[0] = spu_ls_read128(&ctx, p.stack + 16); r[1] = spu_ai(r[1], 240);
    for (unsigned reg = 80; reg <= 89; ++reg) r[reg] = spu_ls_read128(&ctx, p.stack - (reg - 79) * 16);
    r[126] = spu_ls_read128(&ctx, p.stack - 0xb0);
}
} // namespace buzz
