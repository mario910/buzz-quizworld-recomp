#pragma once
#include "native_geometry_ls.h"
#include "native_geometry_shuffle.h"
/* Input reads deliberately preserve SPU wrap/alignment semantics. Only the
 * output span is restricted to private, non-wrapping data. Keep the final
 * lifted iteration for exact temporary registers and both exit branches. */
static int buzz_index_native(spu_context *ctx, void (*last)(spu_context *))
{
    if (ctx->ls != ctx->ls_store || buzz_geometry_debug || g_spu_ls_probe > 0 || g_spu_ls_watch_n > 0 ||
        g_spu_smc_watch > 0 || (ctx->int_enable && (ctx->event_status & ctx->event_mask)))
        return 0;
    const unsigned count = ctx->gpr[18]._u32[0], output = ctx->gpr[17]._u32[0], step0 = ctx->gpr[23]._u32[0];
    if (count < 4 || count > 4096 || output < 0xe680 || (output & 15) || (step0 != 0 && step0 != 32) ||
        output + (uint64_t)step0 + (count - 1ull) * 32 > SPU_LS_SIZE)
        return 0;
    const BuzzGeometryShuffle p20 = buzz_geometry_shuffle_prepare(ctx->gpr[20]),
                              p25 = buzz_geometry_shuffle_prepare(ctx->gpr[25]),
                              p30 = buzz_geometry_shuffle_prepare(ctx->gpr[30]);
    const BuzzGeometryShuffle p31 = buzz_geometry_shuffle_prepare(ctx->gpr[31]),
                              p32 = buzz_geometry_shuffle_prepare(ctx->gpr[32]),
                              p85 = buzz_geometry_shuffle_prepare(ctx->gpr[85]);
    u128 v3 = ctx->gpr[3], v6 = ctx->gpr[6], v7 = ctx->gpr[7], v9 = ctx->gpr[9], v10 = ctx->gpr[10],
         v11 = ctx->gpr[11], v17 = ctx->gpr[17];
    u128 v18 = ctx->gpr[18], v19 = ctx->gpr[19], v21 = ctx->gpr[21], v22 = ctx->gpr[22], v23 = ctx->gpr[23],
         v24 = ctx->gpr[24];
    for (unsigned i = 0; i < count - 1; ++i)
    {
        const u128 code = spu_rotqby(buzz_geometry_ls_read(ctx->ls, ctx->gpr[26]._u32[0] + v21._u32[0]), v21);
        const u128 next0 = buzz_geometry_ls_read(ctx->ls, v3._u32[0]),
                   next1 = buzz_geometry_ls_read(ctx->ls, v3._u32[0] + 16);
        const u128 offset_input = spu_a(spu_a(v7, v7), v3), next_input = spu_a(offset_input, v22);
        const u128 extra0 = buzz_geometry_ls_read(ctx->ls, offset_input._u32[0]),
                   extra1 = buzz_geometry_ls_read(ctx->ls, offset_input._u32[0] + 16);
        const u128 picked = spu_selb(v11, v10, v24), twice = spu_a(picked, picked);
        const u128 low0 = spu_andi(v3, 15), low1 = spu_andi(offset_input, 15);
        const u128 align0 = spu_a(ctx->gpr[83], buzz_geometry_shuffle_apply(low0, low0, p85));
        const u128 align1 = spu_a(ctx->gpr[83], buzz_geometry_shuffle_apply(low1, low1, p85));
        const u128 control0 = spu_ahi(buzz_geometry_shuffle_apply(twice, twice, p30), 1);
        const u128 control1 = spu_ahi(buzz_geometry_shuffle_apply(twice, twice, p25), 1);
        const u128 initial = spu_shufb(next0, next1, align0), extra = spu_shufb(extra0, extra1, align1);
        const u128 old6 = buzz_geometry_shuffle_apply(v6, v6, p20),
                   old9 = buzz_geometry_shuffle_apply(v9, v9, p20);
        const u128 carry = spu_a(v19, old6);
        const u128 output0 = spu_shufb(initial, v19, control0), output1 = spu_shufb(extra, carry, control1);
        const u128 counts = spu_cntb(code), lowcounts = spu_andi(counts, 15),
                   code_control = buzz_geometry_shuffle_apply(code, code, p20);
        v24 = spu_fsmb(code);
        v24 = buzz_geometry_shuffle_apply(v24, v24, p32);
        v10 = spu_cntb(spu_and(code_control, ctx->gpr[28]));
        v11 = spu_ah(spu_sf(v10, ctx->gpr[27]), ctx->gpr[29]);
        // r10 carries the count, not the code-control temporary, into the next selb.
        v7 = buzz_geometry_shuffle_apply(counts, counts, p31);
        v6 = spu_sfi(v7, 8);
        v9 = spu_sfi(lowcounts, 8);
        v22 = spu_a(lowcounts, lowcounts);
        v19 = spu_a(carry, old9);
        v21 = spu_ai(v21, 2);
        v18 = spu_ai(v18, -1);
        buzz_geometry_ls_write(ctx->ls, v17._u32[0] + 16, output1);
        buzz_geometry_ls_write(ctx->ls, v17._u32[0], output0);
        v3 = next_input;
        v17 = spu_a(v17, v23);
        v23 = spu_il(32);
    }
    ctx->gpr[3] = v3;
    ctx->gpr[6] = v6;
    ctx->gpr[7] = v7;
    ctx->gpr[9] = v9;
    ctx->gpr[10] = v10;
    ctx->gpr[11] = v11;
    ctx->gpr[17] = v17;
    ctx->gpr[18] = v18;
    ctx->gpr[19] = v19;
    ctx->gpr[21] = v21;
    ctx->gpr[22] = v22;
    ctx->gpr[23] = v23;
    ctx->gpr[24] = v24;
    last(ctx);
    return 1;
}
