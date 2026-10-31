/* Native index publication: host allocation/transfer state, with the original
 * LS spill footprint and complete exit vectors at the SPU-ABI boundary. */
#pragma once
#include "native_geometry_job.h"
#include "native_geometry_context.h"
#include "spu_helpers.h"

namespace buzz {
struct GeometryIndexPublication {
    uint32_t stack = 0, context = 0, allocation_lsa = 0, record_lsa = 0;
    uint32_t bytes = 0;
    u128 allocation{}, source{}, count{}, tag{};
};

inline bool read_geometry_index_publication(const spu_context& ctx, GeometryIndexPublication& output)
{
    if (ctx.ls != ctx.ls_store) return false;
    GeometryIndexPublication plan;
    plan.stack = ctx.gpr[1]._u32[0];
    plan.count = ctx.gpr[4];
    if ((plan.stack & 15) || plan.stack < 0xb0 || plan.stack > SPU_LS_SIZE - 32) return false;
    if (plan.count._u32[0]) {
        // The enabled-interrupt indirect continuation remains in the lift.
        // Reject before any spill/channel, rather than changing its dispatch.
        if (ctx.int_enable) return false;
        plan.context = ctx.gpr[3]._u32[0];
        plan.allocation_lsa = ctx.gpr[5]._u32[0];
        plan.record_lsa = ctx.gpr[6]._u32[0];
        auto range = [](uint32_t at, uint32_t bytes) {
            return !(at & 15) && at <= SPU_LS_SIZE && bytes <= SPU_LS_SIZE - at;
        };
        if (!range(plan.context, 0x160) || !range(plan.allocation_lsa, 32) || !range(plan.record_lsa, 16)) return false;
        const uint32_t starts[] = {plan.context, plan.allocation_lsa, plan.record_lsa, plan.stack - 0xb0};
        const uint32_t sizes[] = {0x160, 32, 16, 0xd0};
        for (unsigned a = 0; a < 4; ++a) for (unsigned b = a + 1; b < 4; ++b)
            if (starts[a] < starts[b] + sizes[b] && starts[b] < starts[a] + sizes[a]) return false;
        plan.bytes = ((plan.count._u32[0] + 7u) & ~7u) * 2u;
        auto read = [&](uint32_t at) {
            u128 value;
            for (unsigned lane = 0; lane < 4; ++lane) value._u32[lane] = geometry_job_be32(ctx.ls + at + lane * 4);
            return value;
        };
        plan.allocation = read(plan.allocation_lsa);
        plan.source = read(plan.context + 0x50);
        plan.tag = read(plan.context + 0x150);
        const uint32_t source = plan.source._u32[0];
        if (!range(source, plan.bytes) || uint32_t(plan.allocation._u32[0] + plan.bytes) > plan.allocation._u32[3]) return false;
    }
    output = plan;
    return true;
}

inline void publish_geometry_indices(spu_context& ctx, const GeometryIndexPublication& plan)
{
    const u128 stack = ctx.gpr[1];
    spu_ls_write128(&ctx, plan.stack - 0x90, ctx.gpr[126]);
    for (unsigned reg = 80; reg <= 87; ++reg)
        spu_ls_write128(&ctx, plan.stack - (reg - 79) * 16, ctx.gpr[reg]);
    spu_ls_write128(&ctx, plan.stack + 16, ctx.gpr[0]);
    spu_ls_write128(&ctx, plan.stack - 0xb0, stack);
    ctx.gpr[2] = spu_ila(0x2dd0);
    ctx.gpr[1] = spu_ai(stack, -176);
    if (plan.count._u32[0]) {
        const u128 machine = buzz_geometry_job_rdch(&ctx, SPU_RdMachStat);
        ctx.int_enable = 0;
        u128 remaining = spu_a(spu_andi(spu_ai(plan.count, 7), -8), spu_andi(spu_ai(plan.count, 7), -8));
        u128 source = plan.source, cpu = plan.allocation, gpu = spu_rotqbyi(plan.allocation, 4);
        ctx.gpr[6] = plan.allocation;
        ctx.gpr[5] = spu_a(remaining, cpu);
        ctx.gpr[4] = spu_rotqbyi(plan.allocation, 12);
        ctx.gpr[3] = spu_clgt(ctx.gpr[5], ctx.gpr[4]);
        const u128 old_record = spu_ls_read128(&ctx, plan.record_lsa);
        ctx.gpr[19] = spu_cwd(ctx.gpr[1], 0); ctx.gpr[17] = spu_cwd(ctx.gpr[1], 4);
        ctx.gpr[14] = spu_cwd(ctx.gpr[1], 8); ctx.gpr[18] = old_record;
        ctx.gpr[16] = old_record; ctx.gpr[16]._u32[0] = cpu._u32[0];
        ctx.gpr[13] = ctx.gpr[16]; ctx.gpr[13]._u32[1] = gpu._u32[0];
        ctx.gpr[15] = plan.allocation; ctx.gpr[12] = spu_rotqbyi(plan.allocation, 8);
        ctx.gpr[11] = ctx.gpr[13]; ctx.gpr[11]._u32[2] = plan.allocation._u32[2];
        spu_ls_write128(&ctx, plan.record_lsa, ctx.gpr[11]);
        ctx.gpr[8] = spu_il(16384); ctx.gpr[7] = spu_il(32);
        do {
            // The four-lane values below also reconstruct the final volatile
            // ABI state; only the preferred words feed the DMA engine.
            ctx.gpr[25] = spu_clgt(remaining, ctx.gpr[8]);
            const u128 chunk = spu_selb(remaining, ctx.gpr[8], ctx.gpr[25]);
            ctx.gpr[24] = source; ctx.gpr[23] = cpu;
            ctx.gpr[20] = ctx.gpr[22] = chunk; ctx.gpr[21] = plan.tag;
            buzz_geometry_job_wrch(&ctx, MFC_LSA, source);
            buzz_geometry_job_wrch(&ctx, MFC_EAL, cpu);
            buzz_geometry_job_wrch(&ctx, MFC_Size, chunk);
            buzz_geometry_job_wrch(&ctx, MFC_TagID, plan.tag);
            buzz_geometry_job_wrch(&ctx, MFC_Cmd, ctx.gpr[7]);
            remaining = spu_sf(chunk, remaining); source = spu_a(source, chunk);
            cpu = spu_a(cpu, chunk); gpu = spu_a(gpu, chunk);
        } while (remaining._u32[0]);
        ctx.gpr[37] = spu_andi(machine, 1); ctx.gpr[36] = spu_ceqbi(ctx.gpr[37], 0);
        ctx.gpr[35] = spu_cwd(ctx.gpr[1], 0); ctx.gpr[33] = spu_cwd(ctx.gpr[1], 4);
        ctx.gpr[30] = spu_xsbh(ctx.gpr[36]);
        ctx.gpr[34] = spu_ls_read128(&ctx, plan.allocation_lsa);
        ctx.gpr[32] = ctx.gpr[34]; ctx.gpr[32]._u32[0] = cpu._u32[0];
        ctx.gpr[31] = ctx.gpr[32]; ctx.gpr[31]._u32[1] = gpu._u32[0];
        spu_ls_write128(&ctx, plan.allocation_lsa, ctx.gpr[31]);
        ctx.gpr[2] = spu_ai(spu_link(0x2f38), 8);
        ctx.gpr[28] = spu_il(1); ctx.gpr[29] = spu_cbd(ctx.gpr[1], 4);
        ctx.gpr[27] = spu_ls_read128(&ctx, plan.allocation_lsa + 16);
        ctx.gpr[26] = spu_shufb(ctx.gpr[28], ctx.gpr[27], ctx.gpr[29]);
        spu_ls_write128(&ctx, plan.allocation_lsa + 16, ctx.gpr[26]);
    }
    ctx.gpr[1] = stack;
    ctx.gpr[0] = spu_ls_read128(&ctx, plan.stack + 16);
    for (unsigned reg = 80; reg <= 87; ++reg)
        ctx.gpr[reg] = spu_ls_read128(&ctx, plan.stack - (reg - 79) * 16);
    ctx.gpr[126] = spu_ls_read128(&ctx, plan.stack - 0x90);
}
} // namespace buzz
