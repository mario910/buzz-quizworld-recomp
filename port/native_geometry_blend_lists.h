/* Image-41 additional streams: native reverse selection and double-buffered
 * DMA orchestration. The four library data operations remain explicit calls.
 * ABI vectors below preserve all observable spills and child-call inputs. */
#pragma once
#include "native_geometry_commands.h"

namespace buzz {
struct GeometryBlendLists { uint32_t manager = 0, stack = 0, count = 0, begin = 0, end = 0; };
using GeometryBlendListFunctions = buzz_geometry_blend_list_functions;

inline bool read_geometry_blend_lists(const spu_context& ctx, GeometryBlendLists& output)
{
    GeometryBlendLists p;
    p.manager = ctx.gpr[3]._u32[0]; p.stack = ctx.gpr[1]._u32[0]; p.count = ctx.gpr[5]._u32[0];
    if (ctx.ls != ctx.ls_store || (p.stack & 15) || p.stack < 0xe680 + 448 ||
        p.stack > SPU_LS_SIZE - 32 || p.count > 128) return false;
    if (!p.count) { output = p; return true; }
    if (!geometry_output_range(p.manager, 0x180)) return false;
    p.begin = geometry_job_be32(ctx.ls + p.manager + 0x58);
    p.end = geometry_job_be32(ctx.ls + p.manager + 0x17c);
    if ((p.begin | p.end) & 15 || p.begin < 0xe680 || p.begin >= p.end || p.end > SPU_LS_SIZE ||
        geometry_output_overlap(p.begin, p.end - p.begin, p.manager, 0x180) ||
        geometry_output_overlap(p.begin, p.end - p.begin, p.stack - 448, 480) ||
        geometry_output_overlap(p.manager, 0x180, p.stack - 448, 480)) return false;
    output = p; return true;
}

class NativeGeometryBlendLists {
    spu_context& c;
    const GeometryBlendListFunctions& library;
    u128 get(uint32_t at) { return spu_ls_read128(&c, at); }
    void put(uint32_t at, u128 value) { spu_ls_write128(&c, at, value); }
    void channel(uint32_t id, u128 value) { buzz_geometry_job_wrch(&c, id, value); }
    void disable(uint32_t link) { c.gpr[2] = spu_ai(spu_link(link), 8); c.int_enable = 0; }
    void restore_irq(uint32_t link, u128 saved) {
        c.gpr[2] = spu_ai(spu_link(link), 8);
        if (!saved._u16[0]) c.int_enable = 1;
    }
    void save()
    {
        auto& r = c.gpr; const uint32_t sp = r[1]._u32[0];
        r[9] = r[5]; put(sp - 0x180, r[126]);
        r[2] = spu_ila(0xc08); r[126] = spu_link(0xc08);
        put(sp - 0xa0, r[89]); r[89] = r[3];
        put(sp - 0xb0, r[90]); r[90] = r[4];
        put(sp - 0xe0, r[93]); r[126] = spu_sf(r[2], r[126]);
        put(sp + 16, r[0]); r[93] = r[8];
        for (unsigned reg = 80; reg <= 102; ++reg)
            if (reg != 89 && reg != 90 && reg != 93) put(sp - (reg - 79) * 16, r[reg]);
        put(sp - 448, r[1]); r[1] = spu_ai(r[1], -448);
    }
    void finish(bool processed)
    {
        auto& r = c.gpr;
        r[3] = processed ? r[102] : spu_il(0);
        r[0] = get(r[1]._u32[0] + 0x1d0); r[1] = spu_ai(r[1], 448);
        for (unsigned reg = 80; reg <= 102; ++reg) r[reg] = get(r[1]._u32[0] - (reg - 79) * 16);
        r[126] = get(r[1]._u32[0] - 0x180);
        c.pc = r[0]._u32[0]; g_spu_trampoline_fn = c.host_depth ? nullptr : spu_indirect_branch;
    }
    void fetch_tables()
    {
        auto& r = c.gpr;
        r[101] = spu_ila(0xdf00); r[31] = get(r[3]._u32[0] + 0x50);
        r[96] = spu_ila(0xe100); r[30] = get(r[3]._u32[0]);
        r[101] = spu_a(r[101], r[126]); r[32] = buzz_geometry_job_rdch(&c, SPU_RdMachStat);
        r[96] = spu_a(r[96], r[126]); r[27] = spu_andi(r[32], 1);
        r[12] = spu_rotqbyi(r[31], 8); r[13] = spu_rotqbyi(r[30], 6); disable(0xd38);
        r[16] = spu_il(64); r[8] = spu_il(1); r[11] = spu_shli(r[5], 2);
        r[26] = get(r[3]._u32[0] + 0x150); r[29] = r[101]; r[28] = r[7];
        r[10] = spu_shli(r[5], 3); r[21] = r[96]; r[20] = r[6];
        channel(MFC_LSA, r[29]); r[25] = spu_ai(r[11], 15); channel(MFC_EAL, r[28]);
        r[6] = spu_il(2); r[24] = spu_andi(r[25], -16); r[22] = spu_rotqbyi(r[26], 4);
        r[23] = spu_ai(r[10], 15); r[95] = spu_ceqbi(r[27], 0);
        channel(MFC_Size, r[24]); r[19] = spu_andi(r[23], -16); r[4] = spu_xsbh(r[95]);
        channel(MFC_TagID, r[22]); channel(MFC_Cmd, r[16]);
        channel(MFC_LSA, r[21]); channel(MFC_EAL, r[20]); channel(MFC_Size, r[19]);
        r[18] = get(r[3]._u32[0] + 0x150); r[17] = spu_rotqbyi(r[18], 4);
        channel(MFC_TagID, r[17]); channel(MFC_Cmd, r[16]);
        r[15] = get(r[3]._u32[0] + 0x150); r[14] = spu_rotqbyi(r[15], 4); r[7] = spu_shl(r[8], r[14]);
        channel(MFC_WrTagMask, r[7]); channel(MFC_WrTagUpdate, r[6]); r[2] = buzz_geometry_job_rdch(&c, MFC_RdTagStat);
        restore_irq(0xde0, r[4]);
        r[6] = spu_ilhu(0x3ccc); r[8] = spu_fsmbi(0); r[83] = spu_ai(r[9], -1);
        r[5] = get(r[3]._u32[0] + 0x170); r[100] = spu_ai(r[101], -4); r[99] = spu_ai(r[96], -8);
        r[3] = spu_cgti(r[83], -1); r[4] = spu_a(r[11], r[100]); r[7] = spu_a(r[10], r[99]);
        r[6] = spu_iohl(r[6], 0xcccc); r[94] = spu_rotqbyi(r[5], 12);
    }
    bool select_first()
    {
        auto& r = c.gpr;
        if (!r[3]._u32[0]) return false;
        for (;;) {
            r[42] = get(r[4]._u32[0]); r[40] = get(r[7]._u32[0]);
            r[41] = spu_rotqby(r[42], r[4]); r[4] = spu_ai(r[4], -4); r[39] = spu_rotqby(r[40], r[7]);
            r[36] = spu_fm(r[90], r[41]); r[38] = spu_ceq(r[39], r[8]); r[37] = spu_gb(r[38]);
            r[34] = spu_fcgt(r[6], r[36]); r[35] = spu_cgti(r[37], 11); r[33] = spu_or(r[34], r[35]);
            if (!r[33]._u32[0]) return true;
            r[83] = spu_ai(r[83], -1); r[7] = spu_ai(r[7], -8); r[9] = spu_cgti(r[83], -1);
            if (!r[9]._u32[0]) return false;
        }
    }
    void initial_buffer()
    {
        auto& r = c.gpr; const auto sp = r[1]._u32[0];
        r[63] = spu_il(0); r[65] = spu_cwd(r[1], 0); r[64] = spu_il(1); r[62] = spu_cwd(r[1], 8);
        r[97] = spu_sf(r[12], r[94]); r[59] = spu_cwd(r[1], 12);
        r[61] = spu_shufb(r[64], r[63], r[65]); r[60] = spu_shufb(r[12], r[63], r[62]);
        r[58] = spu_shufb(r[12], r[61], r[62]); r[57] = spu_shufb(r[97], r[60], r[59]);
        r[56] = spu_shufb(r[97], r[58], r[59]); put(sp + 32, r[57]); put(sp + 48, r[56]); disable(0xea0);
        r[52] = spu_il(8); r[49] = spu_il(68); r[53] = r[7]; r[55] = get(sp + 32);
        r[48] = spu_xsbh(r[95]); r[51] = get(r[89]._u32[0] + 0x150);
        r[54] = spu_rotqbyi(r[55], 8); r[50] = spu_rotqbyi(r[51], 4);
        channel(MFC_LSA, r[54]); channel(MFC_EAL, r[53]); channel(MFC_Size, r[52]);
        channel(MFC_TagID, r[50]); channel(MFC_Cmd, r[49]); restore_irq(0xee0, r[48]);
        r[47] = spu_ila(0xffff); r[102] = spu_fsmbi(0); r[46] = spu_and(r[13], r[47]);
        r[88] = spu_ai(r[1], 32); r[45] = spu_ai(r[46], 3); r[91] = spu_ai(r[1], 48);
        r[44] = spu_andi(r[45], -4); r[43] = spu_a(r[44], r[44]); r[98] = spu_ai(r[43], 15);
    }
    void wait_and_size()
    {
        auto& r = c.gpr; disable(0xf10);
        r[11] = spu_il(1); r[85] = spu_il(2); r[80] = spu_xsbh(r[95]);
        r[8] = get(r[89]._u32[0] + 0x150); r[7] = spu_rotqbyi(r[8], 4); r[5] = spu_shl(r[11], r[7]);
        channel(MFC_WrTagMask, r[5]); channel(MFC_WrTagUpdate, r[85]); r[2] = buzz_geometry_job_rdch(&c, MFC_RdTagStat);
        restore_irq(0xf40, r[80]);
        r[81] = spu_il(4); r[12] = spu_ilhu(0x3ccc); r[3] = spu_shli(r[83], 3);
        r[84] = get(r[88]._u32[0] + r[81]._u32[0]); r[80] = spu_ai(r[83], -1);
        r[87] = spu_cwd(r[88], 4); r[10] = spu_shli(r[83], 2); r[70] = spu_cwd(r[1], 12);
        r[85] = spu_cgti(r[80], -1); r[6] = spu_a(r[3], r[96]); r[4] = get(r[3]._u32[0] + r[96]._u32[0]);
        r[11] = spu_a(r[3], r[99]); r[10] = spu_a(r[10], r[100]); r[12] = spu_iohl(r[12], 0xcccc);
        r[2] = spu_rotqby(r[4], r[6]); r[6] = spu_il(0); r[92] = spu_a(r[2], r[98]);
        r[86] = spu_andi(r[92], -16); r[82] = spu_shufb(r[86], r[84], r[87]);
        put(r[88]._u32[0] + r[81]._u32[0], r[82]);
        r[79] = get(r[1]._u32[0] + 48); r[78] = get(r[1]._u32[0] + 32);
        r[77] = spu_rotqbyi(r[79], 8); r[72] = spu_rotqbyi(r[78], 8); r[76] = spu_sf(r[72], r[77]);
        r[74] = spu_shufb(r[76], r[78], r[70]); r[73] = spu_rotqbyi(r[74], 4); put(r[1]._u32[0] + 32, r[74]);
        r[71] = spu_a(r[72], r[73]); r[69] = spu_sf(r[71], r[94]);
        r[67] = spu_shufb(r[69], r[79], r[70]); put(r[1]._u32[0] + 48, r[67]);
    }
    bool select_next()
    {
        auto& r = c.gpr;
        if (!r[85]._u32[0]) return false;
        for (;;) {
            r[23] = get(r[10]._u32[0]); r[21] = get(r[11]._u32[0]);
            r[22] = spu_rotqby(r[23], r[10]); r[10] = spu_ai(r[10], -4); r[5] = spu_rotqby(r[21], r[11]);
            r[18] = spu_fm(r[90], r[22]); r[20] = spu_ceq(r[5], r[6]); r[19] = spu_gb(r[20]);
            r[16] = spu_fcgt(r[12], r[18]); r[17] = spu_cgti(r[19], 11); r[15] = spu_or(r[16], r[17]);
            if (!r[15]._u32[0]) return true;
            r[80] = spu_ai(r[80], -1); r[11] = spu_ai(r[11], -8); r[14] = spu_ceqi(r[80], -1);
            if (r[14]._u32[0]) { r[85] = spu_il(0); return false; }
        }
    }
    void prefetch_next()
    {
        auto& r = c.gpr;
        r[7] = spu_il(4); r[8] = spu_cwd(r[91], 4); r[30] = spu_a(r[5], r[98]); r[29] = spu_il(12);
        r[13] = get(r[91]._u32[0] + r[7]._u32[0]); r[25] = spu_andi(r[30], -16); r[28] = spu_ai(r[91], 12);
        r[6] = spu_shufb(r[25], r[13], r[8]); put(r[91]._u32[0] + r[7]._u32[0], r[6]);
        r[27] = get(r[91]._u32[0] + r[29]._u32[0]); r[26] = spu_rotqby(r[27], r[28]); r[24] = spu_clgt(r[25], r[26]);
        if (r[24]._u32[0]) {
            r[53] = spu_il(0); r[49] = spu_cwd(r[1], 8); r[85] = spu_cgti(r[80], -1);
            r[52] = spu_shufb(r[53], r[6], r[8]); put(r[91]._u32[0] + r[7]._u32[0], r[52]);
            r[51] = get(r[1]._u32[0] + 48); r[50] = spu_rotqbyi(r[51], 4); r[48] = spu_sf(r[50], r[94]);
            r[46] = spu_shufb(r[48], r[51], r[49]); put(r[1]._u32[0] + 48, r[46]);
            return; // Candidate is retained for the later buffer reset.
        }
        r[45] = get(r[1]._u32[0] + 48); r[43] = spu_cwd(r[1], 8); r[44] = spu_rotqbyi(r[45], 4);
        r[42] = spu_sf(r[44], r[94]); r[40] = spu_shufb(r[42], r[45], r[43]); put(r[1]._u32[0] + 48, r[40]);
        disable(0x1264);
        r[39] = spu_il(8); r[34] = spu_il(8); r[38] = spu_ai(r[91], 8); r[37] = get(r[91]._u32[0] + r[39]._u32[0]);
        r[36] = r[11]; r[33] = get(r[89]._u32[0] + 0x150); r[32] = spu_il(68);
        r[31] = spu_xsbh(r[95]); r[85] = spu_cgti(r[80], -1); r[35] = spu_rotqby(r[37], r[38]); r[9] = spu_rotqbyi(r[33], 4);
        channel(MFC_LSA, r[35]); channel(MFC_EAL, r[36]); channel(MFC_Size, r[34]); channel(MFC_TagID, r[9]); channel(MFC_Cmd, r[32]);
        if (!r[31]._u16[0]) { r[2] = spu_ai(spu_link(0x12b8), 8); c.int_enable = 1; }
    }
    void process()
    {
        auto& r = c.gpr;
        r[70] = spu_il(8); r[72] = spu_ai(r[88], 8); r[65] = get(r[89]._u32[0] + 0x70);
        r[87] = spu_il(4); r[71] = get(r[88]._u32[0] + r[70]._u32[0]); r[69] = spu_il(12); r[92] = spu_cwd(r[1], 8);
        r[57] = spu_rotqby(r[71], r[72]); r[64] = spu_ai(r[57], 4); r[68] = get(r[57]._u32[0]);
        r[66] = spu_ai(r[57], 32); r[63] = get(r[57]._u32[0] + r[87]._u32[0]); r[61] = spu_ai(r[57], 8);
        r[60] = get(r[57]._u32[0] + r[70]._u32[0]); r[55] = spu_ai(r[57], 12);
        r[56] = get(r[57]._u32[0] + 16); r[54] = get(r[57]._u32[0] + r[69]._u32[0]);
        r[59] = spu_shufb(r[66], r[65], r[92]); r[67] = spu_rotqby(r[68], r[57]); r[62] = spu_rotqby(r[63], r[64]);
        r[58] = spu_rotqby(r[60], r[61]); r[81] = spu_rotqby(r[56], r[57]); r[86] = spu_a(r[66], r[67]);
        r[4] = spu_rotqby(r[54], r[55]); put(r[89]._u32[0] + 0x70, r[59]);
        r[82] = spu_a(r[86], r[62]); r[5] = r[81]; r[84] = spu_a(r[82], r[58]); r[3] = r[84];
        c.pc = 0x1018; geometry_command_call(c, library.unpack, 0x1098);
        if (r[81]._u32[0]) {
            r[75] = spu_shli(r[83], 2); r[102] = spu_ai(r[102], 1); r[74] = get(r[89]._u32[0] + 0x70);
            r[4] = r[82]; r[5] = r[86]; r[7] = r[81]; r[9] = r[84]; r[73] = spu_a(r[75], r[101]);
            r[83] = get(r[75]._u32[0] + r[101]._u32[0]); r[3] = r[89]; r[6] = spu_rotqbyi(r[74], 8);
            r[8] = spu_rotqby(r[83], r[73]); c.pc = 0x11f8; geometry_command_call(c, library.attributes, 0x1230);
        }
        r[82] = spu_il(0); r[79] = get(r[88]._u32[0] + r[87]._u32[0]); r[77] = spu_ai(r[91], 4);
        r[84] = spu_cwd(r[88], 4); r[78] = spu_shufb(r[82], r[79], r[84]); put(r[88]._u32[0] + r[87]._u32[0], r[78]);
        r[76] = get(r[91]._u32[0] + r[87]._u32[0]); r[6] = spu_rotqby(r[76], r[77]);
    }
    void restart()
    {
        auto& r = c.gpr;
        if (r[93]._u32[0]) {
            r[4] = get(r[93]._u32[0]); r[87] = spu_cwd(r[93], 0); r[2] = spu_rotqby(r[4], r[93]);
            r[86] = spu_ai(r[2], 1); r[88] = spu_shufb(r[86], r[4], r[87]); put(r[93]._u32[0], r[88]);
        }
        r[23] = get(r[1]._u32[0] + 48); r[22] = spu_cwd(r[1], 4); r[21] = get(r[1]._u32[0] + 32); r[20] = spu_cwd(r[1], 12);
        r[5] = spu_shufb(r[6], r[23], r[22]); r[19] = spu_shufb(r[6], r[21], r[22]);
        r[17] = spu_shufb(r[97], r[5], r[20]); r[18] = spu_shufb(r[97], r[19], r[20]); r[16] = spu_shufb(r[94], r[17], r[92]);
        put(r[1]._u32[0] + 32, r[18]); put(r[1]._u32[0] + 48, r[16]); disable(0x1114);
        r[11] = spu_il(8); r[10] = spu_il(68); r[15] = spu_shli(r[80], 3); r[14] = get(r[1]._u32[0] + 32);
        r[92] = spu_xsbh(r[95]); r[85] = get(r[89]._u32[0] + 0x150); r[7] = spu_a(r[96], r[15]);
        r[8] = spu_rotqbyi(r[14], 8); r[3] = spu_rotqbyi(r[85], 4);
        channel(MFC_LSA, r[8]); channel(MFC_EAL, r[7]); channel(MFC_Size, r[11]); channel(MFC_TagID, r[3]); channel(MFC_Cmd, r[10]);
        if (!r[92]._u16[0]) { r[2] = spu_ai(spu_link(0x1238), 8); c.int_enable = 1; }
        r[88] = spu_ai(r[1], 32); r[83] = r[80]; r[91] = spu_ai(r[1], 48);
    }
public:
    NativeGeometryBlendLists(spu_context& ctx, const GeometryBlendListFunctions& fn) : c(ctx), library(fn) {}
    void run()
    {
        auto& r = c.gpr; const bool nonempty = r[5]._u32[0] != 0; save();
        if (!nonempty) { finish(false); return; }
        fetch_tables();
        if (!select_first()) { finish(false); return; }
        initial_buffer();
        for (;;) {
            wait_and_size();
            if (select_next()) prefetch_next();
            process();
            if (!r[85]._u32[0]) break;
            if (r[6]._u32[0]) {
                r[66] = r[91]; r[83] = r[80]; r[91] = r[88]; r[88] = r[66];
            } else restart();
        }
        r[3] = r[89]; c.pc = 0x12c8; geometry_command_call(c, library.normal_pointer, 0x12d0);
        if (r[3]._u32[0]) {
            r[93] = get(r[89]._u32[0]); r[89] = spu_ila(0xffff); r[90] = spu_rotqbyi(r[93], 6);
            r[4] = spu_and(r[89], r[90]); geometry_command_call(c, library.normalize, 0x12e8);
        }
        finish(true);
    }
};
inline void blend_geometry_lists(spu_context& ctx, const GeometryBlendLists&, const GeometryBlendListFunctions& fn)
{
    NativeGeometryBlendLists(ctx, fn).run();
}
} // namespace buzz
