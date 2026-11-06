/* Complete descriptor U8 normalized-color decoder (89B0..8B90).
 * Host loops own columns and batches; only their final ABI is materialized. */
#pragma once
#include "native_geometry_output.h"
#include "native_stagef_avx2.h"

namespace buzz {
struct GeometryColorColumn {
    uint32_t offset = 0, groups = 0;
    bool crosses = false;
};
struct GeometryColorStream {
    uint32_t columns = 0, input_step = 0, output_step = 0, limit = 0;
    GeometryColorColumn column[16]{}; // bounded by admission; no per-call allocation
};
inline bool read_geometry_color(const spu_context& ctx, GeometryColorStream& output)
{
    if (ctx.ls != ctx.ls_store) return false;
    GeometryColorStream p;
    if (!ctx.gpr[121]._u32[0]) { output = p; return true; }
    p.columns = ctx.gpr[107]._u32[0]; p.input_step = ctx.gpr[83]._u32[0];
    p.output_step = ctx.gpr[81]._u32[0]; p.limit = ctx.gpr[80]._u32[0];
    const uint32_t stride = ctx.gpr[106]._u32[0], attribute = ctx.gpr[42]._u32[0], bytes = ctx.gpr[25]._u32[0];
    const uint8_t selector[] = {255,0,255,255,255,1,255,255,255,2,255,255,255,3,255,255};
    if (!p.columns || p.columns > 16 || !stride || stride > 255 || attribute > 255 ||
        !bytes || bytes > 4 || !p.input_step || (p.input_step & 15) ||
        !p.output_step || (p.output_step & 15) || p.limit >= SPU_LS_SIZE ||
        memcmp(ctx.ls + 0xd8c0,selector,16)) return false;
    for (unsigned byte = 0; byte < 16; ++byte)
        if (ctx.gpr[112]._u8[byte ^ 3] != byte || ctx.gpr[108]._u8[byte] != 3) return false;
    uint64_t input_begin = SPU_LS_SIZE, input_end = 0, output_begin = SPU_LS_SIZE, output_end = 0;
    const uint32_t stack = ctx.gpr[1]._u32[0];
    if (!geometry_output_range(stack,0x80)) return false;
    const unsigned bases[] = {118,117,116,124};
    for (unsigned row = 0; row < 4; ++row) {
        const uint32_t base = ctx.gpr[bases[row]]._u32[0];
        if ((base & 15) || geometry_job_be32(ctx.ls+stack+0x70-row*16) != base + 16 ||
            ctx.gpr[81]._u32[row] != p.output_step || ctx.gpr[80]._u32[row] != p.limit ||
            !geometry_output_range(ctx.gpr[82]._u32[row],16)) return false;
        output_begin = std::min(output_begin,uint64_t(ctx.gpr[82]._u32[row]));
        output_end = std::max(output_end,uint64_t(ctx.gpr[82]._u32[row])+16);
    }
    for (unsigned column = 0; column < p.columns; ++column) {
        GeometryColorColumn c; c.offset = column * stride + attribute;
        c.crosses = (c.offset & 15) + bytes > 16;
        const uint64_t first = uint64_t(ctx.gpr[41]._u32[0]) + column * 16;
        if (first > INT32_MAX) return false;
        c.groups = first > p.limit ? 1 : (p.limit - uint32_t(first)) / p.output_step + 2;
        if (c.groups > 4096) return false;
        for (unsigned row = 0; row < 4; ++row) {
            const uint64_t begin = uint64_t(ctx.gpr[bases[row]]._u32[0]) + c.offset;
            const uint64_t end = begin + uint64_t(c.groups) * p.input_step + 32;
            input_begin = std::min(input_begin,begin & ~uint64_t{15}); input_end = std::max(input_end,end);
            const uint64_t destination = uint64_t(ctx.gpr[41]._u32[row]) + column * 16;
            const uint64_t last = destination + uint64_t(c.groups - 1) * p.output_step;
            if (last > INT32_MAX) return false;
            if (destination <= p.limit) {
                // Generic descriptors bias the logical cursor by one byte;
                // STQD still writes its enclosing aligned physical quad.
                output_begin = std::min(output_begin,destination & ~uint64_t{15});
                output_end = std::max(output_end,(std::min(last,uint64_t(p.limit)) & ~uint64_t{15}) + 16);
            }
        }
        p.column[column]=c;
    }
    if (input_begin < 0xe680 || input_end > SPU_LS_SIZE || output_begin < 0xe680 || output_end > SPU_LS_SIZE ||
        (input_begin < output_end && output_begin < input_end)) return false;
    output = p; return true;
}

inline u128 geometry_color_input(const spu_context& ctx, unsigned row, unsigned offset, bool crosses)
{
    const unsigned bases[] = {36,35,34,33};
    const unsigned base = ctx.gpr[bases[row]]._u32[0] + (offset & ~15u);
    u128 value{};
    for (unsigned component = 0; component < 4; ++component) {
        const unsigned byte = (offset & 15) + component;
        value._u32[component] = ctx.ls[base + (crosses ? byte : byte & 15)];
    }
    return value;
}

inline void decode_geometry_color(spu_context& ctx, const GeometryColorStream& p)
{
    auto& r = ctx.gpr;
    if (!p.columns) return;
    r[36] = r[118]; r[40] = geometry_output_quad(ctx,r[1]._u32[0] + 0x70);
    r[35] = r[117]; r[39] = geometry_output_quad(ctx,r[1]._u32[0] + 0x60);
    r[34] = r[116]; r[38] = geometry_output_quad(ctx,r[1]._u32[0] + 0x50);
    r[33] = r[124]; r[37] = geometry_output_quad(ctx,r[1]._u32[0] + 0x40);
    r[17] = spu_il(0); r[18] = spu_fsmbi(0); r[19] = spu_ila(0x10203);
    const unsigned bases[] = {36,35,34,33}, next[] = {40,39,38,37};
    const v8 scale = v8_bcast(r[91]), bias = v8_bcast(r[94]);
    for (unsigned column = 0; column < p.columns; ++column) {
        const auto& c = p.column[column];
        // The original performs one final group past the preferred output
        // limit. Its four writes alias scratch in row order, which is retained.
        for (unsigned group = 0; group + 1 < c.groups; ++group) {
            const unsigned offset = c.offset + group * p.input_step;
            u128 values[4];
            for (unsigned pair = 0; pair < 2; ++pair) {
                // Initial preloads always use the following quad. Later
                // within-quad iterations wrap an unused fourth byte instead.
                const bool crosses = !group || c.crosses;
                const v8 integers = v8_pair(geometry_color_input(ctx,pair*2,offset,crosses),
                                            geometry_color_input(ctx,pair*2+1,offset,crosses));
                const v8 decoded = v8_fma(_mm256_castps_si256(_mm256_cvtepi32_ps(integers)),scale,bias);
                const __m128i lo = _mm256_castsi256_si128(decoded), hi = _mm256_extracti128_si256(decoded,1);
                memcpy(&values[pair*2],&lo,16); memcpy(&values[pair*2+1],&hi,16);
            }
            for (unsigned row = 0; row < 4; ++row) {
                uint32_t at = r[41]._u32[row] + column * 16 + group * p.output_step;
                if (at > p.limit) at = r[82]._u32[row];
                spu_ls_write128(&ctx,at,values[row]);
            }
        }
        // Original per-column setup and only the final iteration reproduce
        // volatile vectors and speculative lookahead, without an original tail.
        r[52] = r[17]; r[48] = geometry_output_quad(ctx,0xd8c0); r[21] = spu_mpya(r[52],r[106],r[42]);
        r[50] = spu_shufb(r[18],r[18],r[19]); r[15] = spu_a(r[50],r[41]); r[49] = spu_andi(r[21],15);
        r[30] = spu_ls_read128(&ctx,r[37]._u32[0]+r[21]._u32[0]); r[16] = spu_a(r[21],r[83]);
        r[45] = spu_ls_read128(&ctx,r[36]._u32[0]+r[21]._u32[0]); r[13] = spu_a(r[49],r[25]);
        r[20] = spu_shufb(r[49],r[21],r[108]); r[46] = spu_ls_read128(&ctx,r[40]._u32[0]+r[21]._u32[0]);
        r[29] = spu_cgti(r[13],16); r[44] = spu_ls_read128(&ctx,r[35]._u32[0]+r[21]._u32[0]);
        r[43] = spu_ls_read128(&ctx,r[39]._u32[0]+r[21]._u32[0]); r[47] = spu_a(r[112],r[20]);
        r[32] = spu_ls_read128(&ctx,r[34]._u32[0]+r[21]._u32[0]); r[6] = spu_ls_read128(&ctx,r[38]._u32[0]+r[21]._u32[0]);
        r[31] = spu_ls_read128(&ctx,r[33]._u32[0]+r[21]._u32[0]); r[14] = spu_shufb(r[47],r[47],r[48]);
        const unsigned prefix = c.groups - 1;
        u128 pending[4];
        for (unsigned row = 0; row < 4; ++row) {
            const unsigned offset = c.offset + prefix * p.input_step;
            const u128 a = spu_ls_read128(&ctx,r[bases[row]]._u32[0]+offset);
            const u128 b = (!prefix || c.crosses) ? spu_ls_read128(&ctx,r[next[row]]._u32[0]+offset) : a;
            pending[row] = spu_shufb(a,b,r[14]);
        }
        r[12] = pending[0]; r[8] = pending[1]; r[2] = pending[2]; r[13] = pending[3];
        r[15] = spu_ai(r[15],prefix*p.output_step);
        for (unsigned lane = 0; lane < 4; ++lane) r[16]._u32[lane] += prefix*r[83]._u32[lane];
        if (c.crosses) {
            r[72] = spu_cuflt(r[12],139); r[23] = r[16]; r[71] = spu_cuflt(r[8],139);
            r[70] = spu_cuflt(r[2],139); r[69] = spu_cuflt(r[13],139); r[57] = spu_cgt(r[15],r[80]);
            r[73] = spu_a(r[16],r[83]); r[24] = spu_selb(r[15],r[82],r[57]); r[68] = spu_fma(r[72],r[91],r[94]);
            r[16] = r[73]; r[65] = spu_fma(r[71],r[91],r[94]); r[64] = spu_rotqbyi(r[24],4);
            r[63] = spu_fma(r[70],r[91],r[94]); r[26] = spu_rotqbyi(r[24],8); r[11] = spu_fma(r[69],r[91],r[94]);
            r[8] = spu_rotqbyi(r[24],12); r[66] = spu_a(r[15],r[81]); r[15] = r[66];
            const unsigned dest[] = {24,64,26,8}, val[] = {68,65,63,11};
            for (unsigned row = 0; row < 4; ++row) spu_ls_write128(&ctx,r[dest[row]]._u32[0],r[val[row]]);
            const unsigned load[] = {61,59,58,54}, load_next[] = {62,60,56,55};
            for (unsigned row = 0; row < 4; ++row) {
                r[load[row]] = spu_ls_read128(&ctx,r[bases[row]]._u32[0]+r[23]._u32[0]);
                r[load_next[row]] = spu_ls_read128(&ctx,r[next[row]]._u32[0]+r[23]._u32[0]);
            }
            r[12] = spu_shufb(r[61],r[62],r[14]); r[8] = spu_shufb(r[59],r[60],r[14]);
            r[2] = spu_shufb(r[58],r[56],r[14]); r[13] = spu_shufb(r[54],r[55],r[14]);
        } else {
            r[32] = spu_cuflt(r[12],139); r[31] = spu_cuflt(r[8],139); r[9] = r[16];
            r[7] = spu_cuflt(r[2],139); r[30] = spu_cuflt(r[13],139); r[3] = spu_cgt(r[15],r[80]);
            r[6] = spu_a(r[16],r[83]); r[10] = spu_selb(r[15],r[82],r[3]); r[4] = spu_fma(r[32],r[91],r[94]);
            r[16] = r[6]; r[22] = spu_fma(r[31],r[91],r[94]); r[2] = spu_rotqbyi(r[10],4);
            r[28] = spu_fma(r[7],r[91],r[94]); r[27] = spu_rotqbyi(r[10],8); r[79] = spu_fma(r[30],r[91],r[94]);
            r[78] = spu_rotqbyi(r[10],12); r[5] = spu_a(r[15],r[81]); r[15] = r[5];
            const unsigned dest[] = {10,2,27,78}, val[] = {4,22,28,79};
            for (unsigned row = 0; row < 4; ++row) spu_ls_write128(&ctx,r[dest[row]]._u32[0],r[val[row]]);
            r[77] = spu_ls_read128(&ctx,r[33]._u32[0]+r[9]._u32[0]);
            r[12] = spu_ls_read128(&ctx,r[36]._u32[0]+r[9]._u32[0]);
            r[76] = spu_ls_read128(&ctx,r[35]._u32[0]+r[9]._u32[0]);
            r[75] = spu_ls_read128(&ctx,r[34]._u32[0]+r[9]._u32[0]);
            r[13] = spu_shufb(r[77],r[77],r[14]); r[12] = spu_shufb(r[12],r[12],r[14]);
            r[8] = spu_shufb(r[76],r[76],r[14]); r[2] = spu_shufb(r[75],r[75],r[14]);
        }
        r[17] = spu_ai(r[17],1); r[18] = spu_ai(r[18],16); r[14] = spu_clgt(r[107],r[17]);
    }
}
} // namespace buzz
