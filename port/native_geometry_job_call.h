/* SPU-ABI call boundary for the semantic native driver setup. Preserve the
 * complete nonvolatile vectors: downstream callees spill them into LS even
 * when their non-preferred words are otherwise dead. This is not a full-state
 * replacement for an independently callable lifted preamble. */
#pragma once
#include "native_geometry_job_setup.h"

namespace buzz {
inline void materialize_geometry_job_initial_call(spu_context& ctx, const GeometryJobSetup& setup)
{
    const auto& entry = setup.entry;
    const u128 manager = ctx.gpr[3], descriptor = ctx.gpr[4];
    const u128 stack = spu_ai(ctx.gpr[1], -0x520);
    auto parameter = [&](unsigned offset) {
        return spu_rotqby(spu_ls_read128(&ctx, entry.descriptor_lsa + offset), spu_ai(descriptor, offset));
    };
    auto manager_parameter = [&](unsigned offset) {
        return spu_rotqby(spu_ls_read128(&ctx, entry.context_lsa + offset), spu_ai(manager, offset));
    };
    const u128 inputs0 = spu_ls_read128(&ctx, 0xe500);
    const u128 inputs4 = spu_ls_read128(&ctx, 0xe510);
    const u128 inputs8 = spu_ls_read128(&ctx, 0xe520);
    const u128 inputs12 = spu_ls_read128(&ctx, 0xe530);
    const u128 inputs16 = spu_ls_read128(&ctx, 0xe540);
    const u128 prefix = parameter(0x30);
    const u128 flags = spu_rotqmbyi(parameter(0xec), -4);
    const u128 blend = spu_andi(spu_shlqbii(flags, 4), 1);
    const u128 position_only = spu_andi(spu_shlqbyi(spu_shlqbii(flags, 1), 3), 1);
    const u128 no_secondary = spu_ceqi(parameter(0x88), 0);
    const u128 command_pointer = spu_andc(inputs0, spu_ceqi(prefix, 0));

    // The pipeline selector is a host value. The command header can override
    // it only on the flags&8==0 path; loads retain SPU quadword-wrap semantics.
    unsigned pipeline = 1;
    if (entry.flags & 8) {
        if (entry.flags & 32) pipeline = entry.flags & 0x100 ? 3 : 2;
        else if (entry.flags & 64) pipeline = entry.flags & 0x100 ? 2 : 3;
    } else if (command_pointer._u32[0]) {
        const u128 command = spu_rotqby(spu_ls_read128(&ctx, command_pointer._u32[0]), command_pointer);
        if (command._u32[0] == 1) {
            const u128 address = spu_ai(command_pointer, 36);
            const u128 mode = spu_rotqby(spu_ls_read128(&ctx, address._u32[0]), address);
            if (mode._u32[0] == 2305) pipeline = entry.flags & 0x100 ? 3 : 2;
            else if (mode._u32[0] == 2304) pipeline = entry.flags & 0x100 ? 2 : 3;
        }
    }

    ctx.gpr[80] = spu_ai(manager, 36);
    ctx.gpr[81] = spu_il(36);
    ctx.gpr[82] = spu_ai(stack, 0x1a0);
    ctx.gpr[83] = manager;
    ctx.gpr[84] = spu_il(0);
    ctx.gpr[85] = spu_cwd(stack, 0);
    ctx.gpr[86] = spu_ila(0xffff);
    ctx.gpr[87] = spu_selb(spu_rotqbyi(inputs8, 12), spu_il(0), no_secondary);
    ctx.gpr[88] = spu_selb(spu_rotqbyi(inputs16, 8), spu_il(0), no_secondary);
    ctx.gpr[89] = flags;
    ctx.gpr[90] = parameter(0xf4);
    const u128 range = spu_rotqby(spu_ls_read128(&ctx, entry.descriptor_lsa + 0xf8), spu_ai(descriptor, 0x106));
    ctx.gpr[91] = spu_or(range, spu_ceqhi(range, 0));
    ctx.gpr[92] = command_pointer;
    ctx.gpr[93] = parameter(0xe0);
    ctx.gpr[94] = parameter(0xd8);
    ctx.gpr[95] = spu_ceqbi(position_only, 0);
    ctx.gpr[96] = parameter(0xdc);
    ctx.gpr[97] = blend;
    ctx.gpr[98] = position_only;
    ctx.gpr[99] = entry.flags & 0x10000000 ? spu_il(0) : spu_rotqbyi(inputs4, 8);
    ctx.gpr[100] = spu_ai(stack, 0x50);
    ctx.gpr[101] = entry.flags & 0x10000000 ? spu_il(0) : inputs4;
    ctx.gpr[102] = spu_ai(stack, 0x40);
    ctx.gpr[103] = spu_il(pipeline);
    ctx.gpr[104] = inputs8;
    ctx.gpr[105] = parameter(0x50);
    ctx.gpr[106] = parameter(0x58);
    ctx.gpr[107] = spu_rotqbyi(inputs16, 4);
    ctx.gpr[108] = spu_rotqbyi(inputs0, 8);
    ctx.gpr[109] = parameter(0xe8);

    ctx.gpr[0] = spu_link(0x2088);
    ctx.gpr[1] = stack;
    ctx.gpr[3] = ctx.gpr[82];
    ctx.gpr[4] = inputs16;
    ctx.gpr[5] = manager_parameter(0x20);
    ctx.gpr[6] = spu_shli(spu_and(spu_rotqby(spu_ls_read128(&ctx, entry.descriptor_lsa + 0x1e),
                                         spu_ai(descriptor, 44)), spu_ila(0xffff)), 4);
    ctx.gpr[7] = spu_a(prefix, manager_parameter(0));
    ctx.gpr[8] = spu_sf(prefix, parameter(0x14));
    ctx.gpr[9] = manager_parameter(0x24);
    ctx.gpr[10] = spu_rotqbyi(inputs12, 8);
    ctx.gpr[11] = spu_rotqbyi(inputs12, 12);
    ctx.gpr[12] = spu_ai(stack, 0xb0);
    ctx.pc = 0x2f80;
}
} // namespace buzz
