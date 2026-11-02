/* Host command-buffer reservation and synchronization (image 41, 32C8).
 * Native control owns the scans and packet; ABI vectors/spills stay exact. */
#pragma once
#include "native_geometry_output.h"
#include "native_geometry_job.h"

namespace buzz {
inline void geometry_command_call(spu_context& ctx, buzz_geometry_job_fn fn, uint32_t link)
{
    ctx.gpr[0] = spu_link(link); const int32_t image = ctx.image_id;
    ++ctx.host_depth; fn(&ctx); spu_drain_call(&ctx, link); --ctx.host_depth; spu_img_restore(&ctx, image);
}

// Accumulate packet footprints as they are described, without a packet list.
struct GeometryCommandFootprint {
    uint32_t raw,relocated;bool publish;
    GeometryCommandFootprint(uint32_t start,bool enabled):raw(start),relocated(start),publish(enabled){}
    void add(unsigned words) {
        raw+=4*(words+1);
        if(!publish){relocated=raw;return;}
        unsigned bytes=words*4;
        while(bytes>124-(relocated&127)){
            const unsigned available=124-(relocated&127);
            bytes-=available;relocated+=available+4;
        }
        relocated+=bytes+4;
    }
};

struct GeometryCommandBuffer {
    uint32_t context = 0, output = 0, stack = 0, workspace = 0, start = 0, limit = 0, end = 0;
    uint32_t records = 0, count = 0, first_sync = 0, first_ready = 0;
    u128 context_arg{}, output_arg{}, destination{}, records_arg{}, count_arg{}, context50{}, configuration{}, tag{};
};
inline bool read_geometry_command_buffer(const spu_context& ctx, GeometryCommandBuffer& output, const u128* arguments = nullptr)
{
    if (ctx.ls != ctx.ls_store) return false;
    const u128* r = arguments ? arguments : ctx.gpr;
    GeometryCommandBuffer plan;
    plan.context_arg = r[3]; plan.output_arg = r[4]; plan.destination = r[5];
    plan.records_arg = r[6]; plan.count_arg = r[7];
    plan.context = r[3]._u32[0]; plan.output = r[4]._u32[0]; plan.stack = r[1]._u32[0];
    plan.count = r[7]._u32[0]; plan.records = r[6]._u32[0];
    if (!geometry_output_range(plan.context, 0x160) || !geometry_output_range(plan.output, 16) ||
        (plan.stack & 15) || plan.stack < 0x90 || plan.stack > SPU_LS_SIZE - 32 ||
        (plan.destination._u32[0] & 3) || plan.count > SPU_LS_SIZE / 32) return false;
    plan.context50 = geometry_output_quad(ctx, plan.context + 0x50);
    plan.configuration = geometry_output_quad(ctx, plan.context);
    plan.tag = geometry_output_quad(ctx, plan.context + 0x150);
    plan.workspace = plan.context50._u32[2];
    if (plan.workspace < 0xe680 || plan.workspace > SPU_LS_SIZE - 16) return false;
    plan.start = ((plan.workspace + 16 + 127) & ~127u) + (plan.destination._u32[0] & 127);
    const unsigned words = ctx.ls[plan.context + 1];
    plan.limit = plan.start + (words ? words * 16 : 4);
    plan.end = (plan.limit + 127) & ~127u;
    if (plan.end > SPU_LS_SIZE || plan.start >= plan.end) return false;
    if (plan.count && !geometry_output_range(plan.records, plan.count * 32)) return false;
    plan.first_sync = plan.first_ready = plan.count;
    for (unsigned i = 0; i < plan.count; ++i) {
        const auto* record = ctx.ls + plan.records + i * 32;
        if (plan.first_sync == plan.count && !geometry_job_be32(record + 8)) {
            if ((geometry_job_be32(record) & 3) || (plan.workspace & 3) || ctx.int_enable) return false;
            plan.first_sync = i;
        }
        if (plan.first_ready == plan.count && geometry_job_be32(record + 16) && record[20]) plan.first_ready = i;
    }
    // The original can emit a 32-byte packet past the record's logical limit.
    // Preserve that behavior; validate the actual touched LS span separately
    // from the cursor reservation rather than imposing a new capacity rule.
    const uint32_t touched_end = plan.first_ready < plan.count ? (plan.start + 32 > plan.end ? plan.start + 32 : plan.end) : plan.end;
    if (touched_end > SPU_LS_SIZE) return false;
    const uint32_t starts[] = {plan.context, plan.output, plan.stack - 0x90, plan.workspace, plan.records};
    const uint32_t sizes[] = {0x160, 16, 0xb0, touched_end - plan.workspace, plan.count * 32};
    const unsigned regions = plan.count ? 5 : 4;
    for (unsigned a = 0; a < regions; ++a) for (unsigned b = a + 1; b < regions; ++b)
        if (geometry_output_overlap(starts[a], sizes[a], starts[b], sizes[b])) return false;
    output = plan;
    return true;
}

inline u128 geometry_command_word(spu_context& ctx, uint32_t at, uint32_t value, u128* old = nullptr)
{
    u128 quad = geometry_output_quad(ctx, at & ~15u);
    if (old) *old = quad;
    quad._u32[(at & 15) / 4] = value;
    geometry_job_put32(ctx.ls + at, value);
    return quad;
}

inline void prepare_geometry_command_buffer(spu_context& ctx, const GeometryCommandBuffer& plan)
{
    const u128 stack = ctx.gpr[1];
    spu_ls_write128(&ctx, plan.stack + 16, ctx.gpr[0]);
    for (unsigned reg = 80; reg <= 85; ++reg)
        spu_ls_write128(&ctx, plan.stack - (reg - 79) * 16, ctx.gpr[reg]);
    spu_ls_write128(&ctx, plan.stack - 0x70, ctx.gpr[126]);
    spu_ls_write128(&ctx, plan.stack - 0x90, stack);
    ctx.gpr[1] = spu_ai(stack, -144);
    const u128 temporary = spu_rotqbyi(plan.context50, 8);
    geometry_job_put32(ctx.ls + plan.context + 0x58, plan.workspace + 16);
    const u128 after_temporary = geometry_output_quad(ctx, plan.context + 0x50);
    ctx.gpr[32] = spu_ai(spu_rotqbyi(after_temporary, 8), 127);
    ctx.gpr[31] = spu_andi(ctx.gpr[32], -128);
    ctx.gpr[17] = spu_a(ctx.gpr[31], spu_andi(plan.destination, 127));
    ctx.gpr[30] = geometry_output_quad(ctx, plan.output);
    ctx.gpr[29] = geometry_command_word(ctx, plan.output, plan.start);
    ctx.gpr[25] = spu_il(8); ctx.gpr[7] = spu_il(4); ctx.gpr[9] = spu_il(12);
    ctx.gpr[28] = spu_cwd(plan.output_arg, 8); ctx.gpr[10] = spu_cwd(plan.output_arg, 4);
    ctx.gpr[11] = spu_cwd(plan.output_arg, 12); ctx.gpr[27] = ctx.gpr[29];
    ctx.gpr[26] = geometry_command_word(ctx, plan.output + 8, plan.start);
    ctx.gpr[24] = plan.configuration; ctx.gpr[12] = ctx.gpr[26];
    ctx.gpr[23] = spu_rotqbyi(ctx.gpr[24], 14); ctx.gpr[22] = spu_ceqbi(ctx.gpr[23], 0);
    ctx.gpr[21] = spu_andi(ctx.gpr[23], 255); ctx.gpr[20] = spu_xsbh(ctx.gpr[22]);
    ctx.gpr[18] = spu_shli(ctx.gpr[21], 4); ctx.gpr[19] = spu_ceqhi(ctx.gpr[20], 0);
    ctx.gpr[16] = spu_fsm(ctx.gpr[19]); ctx.gpr[15] = spu_a(ctx.gpr[17], ctx.gpr[18]);
    ctx.gpr[14] = spu_andc(ctx.gpr[7], ctx.gpr[16]); ctx.gpr[13] = spu_a(ctx.gpr[14], ctx.gpr[15]);
    ctx.gpr[4] = spu_andi(spu_ai(ctx.gpr[13], 127), -128);
    ctx.gpr[8] = geometry_command_word(ctx, plan.output + 4, plan.limit);
    ctx.gpr[2] = geometry_command_word(ctx, plan.output + 12, 0);
    ctx.gpr[6] = geometry_output_quad(ctx, plan.context + 0x50); ctx.gpr[5] = spu_cwd(ctx.gpr[1], 8);
    ctx.gpr[2] = geometry_command_word(ctx, plan.context + 0x58, plan.end);
    ctx.gpr[3] = plan.context_arg;
    if (plan.count) {
        ctx.gpr[7] = plan.records_arg;
        ctx.gpr[34] = geometry_output_quad(ctx, plan.records); ctx.gpr[33] = spu_rotqbyi(ctx.gpr[34], 8);
        if (plan.first_sync != 0) {
            ctx.gpr[4] = spu_ai(plan.records_arg, 32); ctx.gpr[6] = spu_il(0);
            for (unsigned index = 1; ; ++index) {
                ctx.gpr[6] = spu_ai(ctx.gpr[6], 1); ctx.gpr[7] = ctx.gpr[4];
                ctx.gpr[5] = spu_ai(ctx.gpr[4], 8); ctx.gpr[47] = spu_ceq(plan.count_arg, ctx.gpr[6]);
                if (index == plan.count) break;
                ctx.gpr[37] = spu_il(8); ctx.gpr[36] = geometry_output_quad(ctx, plan.records + index * 32);
                ctx.gpr[4] = spu_ai(ctx.gpr[4], 32); ctx.gpr[35] = spu_rotqby(ctx.gpr[36], ctx.gpr[5]);
                if (index == plan.first_sync) break;
            }
        }
        if (plan.first_sync < plan.count) {
            ctx.gpr[41] = buzz_geometry_job_rdch(&ctx, SPU_RdMachStat); ctx.int_enable = 0;
            ctx.gpr[2] = spu_ai(spu_link(0x35a0), 8);
            ctx.gpr[46] = temporary; buzz_geometry_job_wrch(&ctx, MFC_LSA, temporary);
            ctx.gpr[45] = geometry_output_quad(ctx, plan.records + plan.first_sync * 32);
            buzz_geometry_job_wrch(&ctx, MFC_EAL, ctx.gpr[45]);
            ctx.gpr[44] = spu_il(4); buzz_geometry_job_wrch(&ctx, MFC_Size, ctx.gpr[44]);
            ctx.gpr[43] = plan.tag; buzz_geometry_job_wrch(&ctx, MFC_TagID, plan.tag);
            ctx.gpr[42] = spu_il(66); buzz_geometry_job_wrch(&ctx, MFC_Cmd, ctx.gpr[42]);
            ctx.gpr[40] = spu_andi(ctx.gpr[41], 1); ctx.gpr[39] = spu_ceqbi(ctx.gpr[40], 0);
            ctx.gpr[38] = spu_xsbh(ctx.gpr[39]);
        }
        ctx.gpr[6] = plan.records_arg; ctx.gpr[8] = spu_il(0);
        for (unsigned i = 0; i < plan.count; ++i) {
            if (i <= plan.first_ready) {
                ctx.gpr[62] = spu_il(20); ctx.gpr[61] = geometry_output_quad(ctx, plan.records + i * 32 + 16);
                ctx.gpr[59] = spu_ai(ctx.gpr[6], 33); ctx.gpr[48] = spu_il(0);
                ctx.gpr[58] = ctx.gpr[61]; ctx.gpr[50] = spu_il(-1);
                ctx.gpr[60] = spu_rotqby(ctx.gpr[61], ctx.gpr[6]);
                ctx.gpr[57] = spu_rotqby(ctx.gpr[58], ctx.gpr[59]); ctx.gpr[53] = spu_ceqi(ctx.gpr[60], 0);
                ctx.gpr[56] = spu_ceqbi(ctx.gpr[57], 0); ctx.gpr[55] = spu_xsbh(ctx.gpr[56]);
                ctx.gpr[54] = spu_ceqhi(ctx.gpr[55], 0); ctx.gpr[52] = spu_xshw(ctx.gpr[54]);
                ctx.gpr[51] = spu_andc(ctx.gpr[52], ctx.gpr[53]); ctx.gpr[49] = spu_ceqi(ctx.gpr[51], 0);
                ctx.gpr[7] = spu_selb(ctx.gpr[48], ctx.gpr[50], ctx.gpr[49]);
            }
            ctx.gpr[8] = spu_ai(ctx.gpr[8], 1); ctx.gpr[3] = spu_xsbh(ctx.gpr[7]);
            ctx.gpr[63] = spu_ceq(plan.count_arg, ctx.gpr[8]); ctx.gpr[6] = spu_ai(ctx.gpr[6], 32);
        }
        if (plan.first_ready < plan.count) {
            const u128 cursor = spu_rotqbyi(geometry_output_quad(ctx, plan.output), 8);
            ctx.gpr[65] = spu_il(8); ctx.gpr[74] = spu_il(0); ctx.gpr[21] = spu_ai(plan.output_arg, 8);
            ctx.gpr[69] = spu_cwd(plan.output_arg, 8); ctx.gpr[20] = spu_ila(0x1710); ctx.gpr[20] = spu_or(ctx.gpr[20], spu_ilhu(4));
            ctx.gpr[4] = geometry_output_quad(ctx, plan.output); ctx.gpr[70] = spu_il(4); ctx.gpr[76] = spu_il(24);
            ctx.gpr[77] = spu_rotqby(ctx.gpr[4], ctx.gpr[21]);
            ctx.gpr[7] = spu_ai(cursor, 8);
            ctx.gpr[11] = spu_cwd(cursor, 0); ctx.gpr[71] = spu_ai(cursor, 24); ctx.gpr[17] = spu_cwd(cursor, 4);
            ctx.gpr[68] = spu_ai(cursor, 32); ctx.gpr[13] = spu_cwd(ctx.gpr[7], 4);
            ctx.gpr[5] = spu_cwd(spu_ai(cursor, 16), 4); ctx.gpr[75] = spu_cwd(ctx.gpr[71], 4);
            ctx.gpr[18] = geometry_command_word(ctx, plan.start, 0x00041710, &ctx.gpr[19]);
            ctx.gpr[15] = geometry_command_word(ctx, plan.start + 4, 0, &ctx.gpr[16]);
            ctx.gpr[10] = geometry_command_word(ctx, plan.start + 8, 0x00041714, &ctx.gpr[14]);
            ctx.gpr[8] = geometry_command_word(ctx, plan.start + 12, 0, &ctx.gpr[12]);
            ctx.gpr[2] = geometry_command_word(ctx, plan.start + 16, 0x00041714, &ctx.gpr[6]);
            geometry_command_word(ctx, plan.start + 20, 0, &ctx.gpr[9]);
            ctx.gpr[78] = geometry_command_word(ctx, plan.start + 24, 0x00041714, &ctx.gpr[79]);
            ctx.gpr[72] = geometry_command_word(ctx, plan.start + 28, 0, &ctx.gpr[73]);
            ctx.gpr[67] = geometry_output_quad(ctx, plan.output);
            ctx.gpr[66] = geometry_command_word(ctx, plan.output + 8, plan.start + 32);
        }
    }
    ctx.gpr[1] = stack; ctx.gpr[0] = spu_ls_read128(&ctx, plan.stack + 16);
    for (unsigned reg = 80; reg <= 85; ++reg)
        ctx.gpr[reg] = spu_ls_read128(&ctx, plan.stack - (reg - 79) * 16);
    ctx.gpr[126] = spu_ls_read128(&ctx, plan.stack - 0x70);
}
} // namespace buzz
