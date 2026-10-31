/* Native RSX command finalization (image 41, 27A0).
 * Host packet traversal and bulk shifts own the data path. Vector arithmetic
 * below materializes the full observable SPU ABI, including dead lanes. */
#pragma once
#include "native_geometry_commands.h"
#include <algorithm>
#include <vector>

namespace buzz {
struct GeometryCommandPublication {
    uint32_t context = 0, record = 0, destination = 0, records = 0, count = 0;
    uint32_t start = 0, cursor = 0, limit = 0, bytes = 0, final_cursor = 0;
    uint32_t splits = 0, padding_words = 0;
};

// Simulate only command bytes, before any register/LS/channel mutation. This
// bounds data-dependent packet expansion and also proves the terminating
// look-ahead load, which can read beyond the last logical command.
inline bool read_geometry_command_publication(const spu_context& ctx, GeometryCommandPublication& output, const char** reason = nullptr)
{
    auto reject = [&](const char* message) { if (reason) *reason = message; return false; };
    GeometryCommandPublication p;
    p.context = ctx.gpr[3]._u32[0]; p.record = ctx.gpr[4]._u32[0];
    p.destination = ctx.gpr[5]._u32[0]; p.records = ctx.gpr[6]._u32[0]; p.count = ctx.gpr[7]._u32[0];
    if (!p.destination) { output = p; return true; }
    if (ctx.ls != ctx.ls_store || ctx.int_enable || (p.destination & 3) ||
        !geometry_output_range(p.context, 0x160) || !geometry_output_range(p.record, 16) ||
        p.count > SPU_LS_SIZE / 32 || (p.count && !geometry_output_range(p.records, p.count * 32))) return reject("context/record/list/interrupt ABI");
    p.start = geometry_job_be32(ctx.ls + p.record); p.limit = geometry_job_be32(ctx.ls + p.record + 4);
    p.cursor = geometry_job_be32(ctx.ls + p.record + 8);
    p.bytes = ctx.ls[p.context + 1] ? unsigned(ctx.ls[p.context + 1]) * 16 : 4;
    if (((p.start | p.cursor | p.limit) & 3) || p.start < 0xe680 || p.start > p.cursor || p.cursor > p.limit ||
        p.limit > SPU_LS_SIZE || p.bytes > SPU_LS_SIZE - p.start) return reject("logical buffer range");
    const uint32_t first = (p.start & ~127u) - 16;
    const uint32_t last = std::min<uint32_t>(SPU_LS_SIZE, ((std::max(p.limit, p.start + p.bytes) + 127) & ~127u) + 128);
    const uint32_t begins[] = {p.context, p.record, p.records, first};
    const uint32_t sizes[] = {0x160, 16, p.count * 32, last - first};
    for (unsigned a = 0; a < 4; ++a) for (unsigned b = a + 1; b < 4; ++b)
        if (sizes[a] && sizes[b] && geometry_output_overlap(begins[a], sizes[a], begins[b], sizes[b])) return reject("overlapping command/state spans");
    static constexpr uint8_t shift[] = {12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27};
    if (memcmp(ctx.ls + 0xcea0, shift, 16)) return reject("unreviewed shift mask");
    std::vector<uint8_t> data(ctx.ls + first, ctx.ls + last);
    auto range = [&](uint32_t at, uint32_t bytes) { return at >= first && at <= last && bytes <= last - at; };
    auto put = [&](uint32_t at, uint32_t value) { geometry_job_put32(data.data() + at - first, value); };
    uint32_t cursor = p.cursor;
    for (unsigned i = 0; i < p.count; ++i) {
        const auto* record = ctx.ls + p.records + i * 32;
        const uint32_t semaphore = geometry_job_be32(record + 16);
        if (semaphore) {
            if (!range(cursor, 16)) return reject("semaphore span");
            put(cursor, 0x00041d6c); put(cursor + 4, semaphore & 4080);
            put(cursor + 8, 0x00041d74); put(cursor + 12, geometry_job_be32(record + 12)); cursor += 16;
        }
    }
    uint32_t end = cursor, next = p.start, previous = 0, header = 0, payload = 0, start_block = 0, end_block = 0;
    if (end != p.start) for (unsigned steps = 0; ; ++steps) {
        if (steps > (last - first) / 4) return reject("nonterminating packet traversal");
        // The terminating look-ahead is an unconditional LS load. Its address
        // can lie outside the command allocation and wraps at the LS boundary.
        // Read modified command bytes from the private plan, and untouched LS
        // elsewhere; the record cursor already reflects appended semaphores.
        const uint32_t address = next & (SPU_LS_SIZE - 4);
        const uint32_t word = address == p.record + 8 ? cursor :
            geometry_job_be32(range(address, 4) ? data.data() + address - first : ctx.ls + address);
        const bool more = end > previous, same_block = start_block == end_block;
        start_block = next & ~127u;
        if (!more) break;
        const uint32_t old_header = header, old_address = previous;
        header = word; previous = next;
        const uint32_t boundary = (old_address + 128) & ~127u, gap = boundary - old_address - 4;
        const uint32_t remainder = payload - gap;
        payload = (word >> 16) & 8188; next += payload + 4; end_block = (next - 4) & ~127u;
        if (!same_block) {
            const uint32_t move_end = (end & ~127u) + 128;
            if (!range(old_address, 4) || boundary < 4 || move_end < boundary ||
                !range(boundary - 4, move_end - boundary + 4) || end > last - 4) return reject("packet expansion span");
            put(old_address, gap ? (old_header & 0xe003ffffu) | (gap << 16) : 0);
            memmove(data.data() + boundary - first, data.data() + boundary - first - 4, move_end - boundary);
            const uint32_t relocated = ((old_header & 0xe003ffffu) | (remainder << 16)) +
                ((old_header & 0x40000000u) ? 0 : gap);
            put(boundary, relocated); next = previous = boundary; end += 4; end_block = start_block;
            ++p.splits;
        }
    }
    if (end > p.limit) return reject("packet expansion exceeds logical limit"); // Original padding loop would not terminate within the buffer.
    p.final_cursor = p.limit; p.padding_words = (p.limit - end) / 4;
    output = p; return true;
}

inline void publish_geometry_commands(spu_context& ctx, const GeometryCommandPublication& p)
{
    auto& r = ctx.gpr;
    r[29] = r[5]; r[30] = r[3]; r[18] = r[4];
    if (!p.destination) return;
    if (p.count) {
        r[4] = spu_il(8); r[16] = spu_il(0); r[20] = spu_splat_u32(0x41d6c);
        r[2] = spu_ai(r[18], 8); r[3] = geometry_output_quad(ctx, p.record);
        r[19] = spu_splat_u32(0x41d74); r[21] = spu_il(4080); r[8] = spu_rotqby(r[3], r[2]);
        for (unsigned i = 0; i < p.count; ++i) {
            r[13] = spu_il(4); r[5] = geometry_output_quad(ctx, p.records + i * 32 + 16);
            r[11] = spu_il(8); r[12] = spu_cwd(r[8], 0); r[10] = spu_ai(r[6], 12);
            r[14] = spu_cwd(r[8], 4); r[16] = spu_ai(r[16], 1); r[17] = spu_cwd(r[18], 8);
            r[15] = spu_cwd(r[8], 8); r[22] = spu_rotqby(r[5], r[6]); r[5] = spu_and(r[22], r[21]);
            if (geometry_job_be32(ctx.ls + p.records + i * 32 + 16)) {
                const uint32_t cursor = r[8]._u32[0];
                r[36] = spu_il(12); r[34] = geometry_output_quad(ctx, (p.records + i * 32 + 12) & ~15u);
                r[12] = spu_ai(r[8], 8); r[23] = spu_rotqby(r[34], r[10]); r[24] = spu_cwd(r[12], 4);
                r[33] = geometry_command_word(ctx, cursor, 0x41d6c, &r[35]);
                r[31] = geometry_command_word(ctx, cursor + 4, r[5]._u32[0], &r[32]);
                r[27] = geometry_command_word(ctx, p.record + 8, cursor + 8, &r[28]);
                r[25] = geometry_command_word(ctx, cursor + 8, 0x41d74, &r[26]);
                r[8] = spu_ai(r[8], 16);
                r[14] = geometry_command_word(ctx, cursor + 12, r[23]._u32[0], &r[15]);
                r[9] = geometry_command_word(ctx, p.record + 8, cursor + 16, &r[10]);
            }
            r[11] = spu_ceq(r[7], r[16]); r[6] = spu_ai(r[6], 32);
        }
    } else {
        r[13] = spu_il(8); r[7] = spu_ai(r[4], 8); r[6] = geometry_output_quad(ctx, p.record);
        r[8] = spu_rotqby(r[6], r[7]);
    }
    r[7] = spu_il(0); r[16] = geometry_output_quad(ctx, p.record); r[5] = spu_rotqby(r[16], r[18]);
    r[27] = spu_sf(r[5], r[8]);
    if (r[27]._u32[0]) {
        r[17] = spu_il(0); r[20] = spu_andi(r[27], -4); r[24] = geometry_output_quad(ctx, 0xcea0);
        r[37] = spu_ila(0x10203); r[21] = spu_fsmbi(0xfff); r[31] = spu_ilhu(0xe003);
        r[9] = r[5]; r[28] = r[27]; r[12] = r[17]; r[31] = spu_iohl(r[31], 0xffff);
        r[11] = r[17]; r[25] = spu_il(8188); r[16] = r[17];
        r[32] = spu_selb(r[37], r[24], r[21]); r[15] = r[17]; r[19] = spu_a(r[5], r[20]);
        while (true) {
            const bool more = r[19]._u32[0] > r[12]._u32[0];
            const bool same_block = r[17]._u32[0] == r[11]._u32[0];
            r[38] = spu_clgt(r[19], r[12]); r[6] = geometry_output_quad(ctx, r[9]._u32[0] & (SPU_LS_SIZE - 16));
            r[3] = spu_ceq(r[17], r[11]); r[17] = spu_andi(r[9], -128);
            if (!more) break;
            r[14] = r[15]; r[10] = r[12]; r[15] = spu_rotqby(r[6], r[9]); r[12] = r[9];
            r[47] = spu_rotqmbyi(r[14], -3); r[48] = spu_ai(r[10], 128); r[46] = spu_ai(r[10], 4);
            r[13] = spu_andi(r[48], -128); r[43] = spu_rotqmbyi(r[15], -2); r[45] = spu_rotqmbii(r[47], -4);
            r[8] = spu_sf(r[46], r[13]); r[20] = spu_sf(r[8], r[16]); r[42] = spu_rotqmbii(r[8], -2);
            r[44] = spu_andi(r[45], 4); r[16] = spu_and(r[25], r[43]); r[41] = spu_xori(r[44], 4);
            r[40] = spu_a(r[9], r[16]); r[22] = spu_mpyu(r[41], r[42]); r[2] = spu_shli(r[8], 16);
            r[11] = spu_andi(r[40], -128); r[9] = spu_ai(r[40], 4);
            if (same_block) continue;
            r[57] = spu_ceqi(r[8], 0); r[56] = spu_selb(r[2], r[14], r[31]);
            r[55] = spu_sf(r[13], r[19]); r[54] = spu_cwd(r[10], 0); r[52] = spu_andc(r[56], r[57]);
            r[51] = spu_andi(r[55], -128); r[26] = spu_ai(r[13], -128); r[21] = spu_a(r[51], r[13]);
            r[50] = geometry_command_word(ctx, r[10]._u32[0], r[52]._u32[0], &r[53]);
            // Backwards 128-byte iterations shift the complete physical tail,
            // including padding. Only the final iteration's vectors survive.
            const uint32_t boundary = r[13]._u32[0], last_block = r[21]._u32[0];
            const unsigned blocks = (last_block - boundary) / 128 + 1;
            r[59] = spu_ai(r[21], -int((blocks - 1) * 128)); r[74] = spu_ai(r[59], -128);
            r[58] = spu_ceq(r[74], r[26]);
            r[23] = geometry_output_quad(ctx, boundary); r[64] = geometry_output_quad(ctx, boundary + 16);
            r[76] = geometry_output_quad(ctx, boundary + 32); r[75] = geometry_output_quad(ctx, boundary + 48);
            r[73] = geometry_output_quad(ctx, boundary + 64); r[65] = geometry_output_quad(ctx, boundary + 80);
            r[66] = geometry_output_quad(ctx, boundary + 96); r[68] = geometry_output_quad(ctx, boundary + 112);
            r[67] = geometry_output_quad(ctx, boundary - 16); r[21] = r[74];
            r[72] = spu_shufb(r[64], r[76], r[24]); r[71] = spu_shufb(r[76], r[75], r[24]);
            r[70] = spu_shufb(r[75], r[73], r[24]); r[69] = spu_shufb(r[73], r[65], r[24]);
            r[63] = spu_shufb(r[66], r[68], r[24]); r[62] = spu_shufb(r[67], r[23], r[24]);
            r[61] = spu_shufb(r[65], r[66], r[24]); r[60] = spu_shufb(r[23], r[64], r[24]);
            memmove(ctx.ls + boundary, ctx.ls + boundary - 4, blocks * 128);
            r[2] = spu_shli(r[20], 16); r[33] = spu_ai(r[19], 4); r[9] = r[13];
            r[4] = spu_ai(r[28], 4); r[12] = r[13]; r[19] = r[33]; r[11] = r[17];
            r[3] = spu_selb(r[2], r[14], r[31]); r[28] = r[4]; r[79] = spu_a(r[3], r[22]);
            r[78] = spu_shufb(r[79], r[23], r[32]); spu_ls_write128(&ctx, boundary, r[78]);
        }
        r[24] = spu_il(8); r[7] = r[28]; r[23] = spu_ai(r[18], 8);
        r[17] = geometry_output_quad(ctx, p.record); r[8] = spu_rotqby(r[17], r[23]);
    }
    r[34] = spu_il(8); r[11] = spu_cwd(r[18], 8); r[13] = spu_sf(r[27], r[7]); r[32] = spu_il(4);
    r[6] = spu_andi(r[13], -4); r[31] = spu_ai(r[18], 4); r[8] = spu_a(r[6], r[8]);
    r[35] = geometry_command_word(ctx, p.record + 8, r[8]._u32[0], &r[36]);
    r[28] = geometry_output_quad(ctx, p.record); r[27] = spu_rotqby(r[28], r[31]);
    r[25] = spu_sf(r[8], r[27]); r[7] = spu_rotmai(r[25], -2);
    const unsigned padding_words = r[7]._u32[0];
    if (padding_words) {
        r[6] = spu_il(int(padding_words)); r[9] = spu_il(0); r[5] = spu_ai(r[8], int((padding_words - 1) * 4));
        const uint32_t final_word = r[5]._u32[0];
        memset(ctx.ls + r[8]._u32[0], 0, (padding_words - 1) * 4);
        r[38] = spu_cwd(r[5], 0); r[16] = spu_ceq(r[7], r[6]);
        r[19] = geometry_command_word(ctx, final_word, 0, &r[37]); r[5] = spu_ai(r[5], 4);
    }
    r[10] = spu_il(8); r[50] = spu_cwd(r[18], 8); r[21] = spu_shli(r[7], 2); r[41] = spu_il(4);
    r[49] = spu_a(r[8], r[21]); r[14] = geometry_command_word(ctx, p.record + 8, r[49]._u32[0], &r[15]);
    r[48] = geometry_output_quad(ctx, p.context); r[12] = spu_rotqbyi(r[48], 14);
    r[47] = spu_ceqbi(r[12], 0); r[46] = spu_andi(r[12], 255); r[45] = spu_xsbh(r[47]);
    r[43] = spu_shli(r[46], 4); r[44] = spu_ceqhi(r[45], 0); r[42] = spu_fsm(r[44]);
    r[9] = spu_selb(r[41], r[43], r[42]); r[7] = buzz_geometry_job_rdch(&ctx, SPU_RdMachStat);
    ctx.int_enable = 0; r[2] = spu_ai(spu_link(0x2960), 8);
    r[22] = geometry_output_quad(ctx, p.record); r[20] = spu_rotqby(r[22], r[18]);
    buzz_geometry_job_wrch(&ctx, MFC_LSA, r[20]); r[40] = r[29]; buzz_geometry_job_wrch(&ctx, MFC_EAL, r[40]);
    buzz_geometry_job_wrch(&ctx, MFC_Size, r[9]); r[39] = geometry_output_quad(ctx, p.context + 0x150);
    buzz_geometry_job_wrch(&ctx, MFC_TagID, r[39]); r[5] = spu_il(34); buzz_geometry_job_wrch(&ctx, MFC_Cmd, r[5]);
    r[30] = spu_andi(r[7], 1); r[29] = spu_ceqbi(r[30], 0); r[18] = spu_xsbh(r[29]); r[2] = spu_ai(spu_link(0x29a0), 8);
}
} // namespace buzz
