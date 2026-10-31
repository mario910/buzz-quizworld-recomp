#pragma once
#include "native_geometry_ls.h"
#include "native_geometry_shuffle.h"
typedef struct
{
    u128 raw;
    BuzzGeometryShuffle mask;
} BuzzTriangleControl;
static inline __attribute__((always_inline)) void
buzz_triangles_prepare(spu_context *ctx, BuzzTriangleControl *a, BuzzTriangleControl *b, unsigned *ready,
                       unsigned index, unsigned ta, unsigned tb)
{
    if (*ready & (1u << index))
        return;
    a[index].raw = buzz_geometry_ls_read(ctx->ls, ta + index * 16);
    a[index].mask = buzz_geometry_shuffle_prepare(a[index].raw);
    b[index].raw = buzz_geometry_ls_read(ctx->ls, tb + index * 16);
    b[index].mask = buzz_geometry_shuffle_prepare(b[index].raw);
    *ready |= 1u << index;
}
static int buzz_triangles_native(spu_context *ctx, void (*last)(spu_context *))
{
    if (ctx->ls != ctx->ls_store || buzz_geometry_debug || g_spu_ls_probe > 0 || g_spu_ls_watch_n > 0 ||
        g_spu_smc_watch > 0 || (ctx->int_enable && (ctx->event_status & ctx->event_mask)) ||
        !buzz_triangles_constants_match(ctx))
        return 0;
    unsigned count = ctx->gpr[22]._u32[0], output = ctx->gpr[18]._u32[0], first_step = ctx->gpr[5]._u32[0];
    unsigned table_a = ctx->gpr[24]._u32[0], table_b = ctx->gpr[27]._u32[0], code_base = ctx->gpr[31]._u32[0];
    if (count < 4 || count > 4096 || output < 0xe680 || (output & 15) ||
        (first_step != 0 && first_step != 48) ||
        (uint64_t)output + first_step + (count - 1ull) * 48 > SPU_LS_SIZE || table_a >= SPU_LS_SIZE ||
        table_b >= SPU_LS_SIZE || (table_a & ~15u) + 256 > 0xe680 || (table_b & ~15u) + 256 > 0xe680 ||
        ctx->gpr[12]._u32[0] > 8 || ctx->gpr[21]._u32[0] > 16)
        return 0;
    const unsigned input_indices[3] = {3, 7, 8};
    for (unsigned n = 0; n < 3; ++n)
    {
        unsigned r = input_indices[n];
        unsigned index = ctx->gpr[r]._u32[0];
        if (index > 240 || index & 15)
            return 0;
    }
    const unsigned code_start = code_base + ctx->gpr[28]._u32[0];
    const uint64_t code_end = ((uint64_t)code_start + (count - 1ull) * 2) & ~15ull;
    const uint64_t output_end = (uint64_t)output + first_step + (count - 1ull) * 48;
    // After the initial prefetch, 6*population+2*(8-population) is at most48.
    // Bound the whole immutable code stream and every speculative input load.
    if (code_start < 0xe680 || code_end + 16 > SPU_LS_SIZE ||
        ((code_start & ~15u) < output_end && output < code_end + 16) || ctx->gpr[20]._u32[0] < 0xe680 ||
        ctx->gpr[20]._u32[0] + 6ull * ctx->gpr[12]._u32[0] + ctx->gpr[21]._u32[0] + (count - 1ull) * 48 + 64 >
            SPU_LS_SIZE)
        return 0;
    BuzzTriangleControl a[16], b[16];
    unsigned ready = 0;
    const BuzzGeometryShuffle p33 = buzz_geometry_shuffle_prepare(ctx->gpr[33]),
                              p34 = buzz_geometry_shuffle_prepare(ctx->gpr[34]),
                              p35 = buzz_geometry_shuffle_prepare(ctx->gpr[35]);
    u128 q0 = ctx->gpr[32], q1 = ctx->gpr[26], q2 = ctx->gpr[25], q3 = ctx->gpr[30], previous = ctx->gpr[23];
    u128 in = ctx->gpr[20], add = ctx->gpr[21], population = ctx->gpr[12], out = ctx->gpr[18],
         step = ctx->gpr[5], bits = ctx->gpr[28], remaining = ctx->gpr[22];
    u128 idx0 = ctx->gpr[3], idx1 = ctx->gpr[7], idx2 = ctx->gpr[8], control = ctx->gpr[10],
         control19 = ctx->gpr[19], control29 = ctx->gpr[29];
    BuzzGeometryShuffle p19 = buzz_geometry_shuffle_prepare(control19),
                        p29 = buzz_geometry_shuffle_prepare(control29);
    for (unsigned i = 0; i < count - 1; ++i)
    {
        const BuzzGeometryShuffle aligned = buzz_geometry_shuffle_prepare(control);
        buzz_triangles_prepare(ctx, a, b, &ready, idx0._u32[0] / 16, table_a, table_b);
        buzz_triangles_prepare(ctx, a, b, &ready, idx1._u32[0] / 16, table_a, table_b);
        buzz_triangles_prepare(ctx, a, b, &ready, idx2._u32[0] / 16, table_a, table_b);
        const BuzzGeometryShuffle m9 = a[idx0._u32[0] / 16].mask, m14 = b[idx0._u32[0] / 16].mask,
                                  m78 = a[idx2._u32[0] / 16].mask, m82 = b[idx2._u32[0] / 16].mask,
                                  m76 = a[idx1._u32[0] / 16].mask;
        u128 code = spu_rotqby(buzz_geometry_ls_read(ctx->ls, code_base + bits._u32[0]), bits);
        u128 next_input = spu_a(spu_a(spu_mpyi(population, 6), add), in);
        u128 s7 = buzz_geometry_shuffle_apply(q1, q2, aligned),
             s13 = buzz_geometry_shuffle_apply(q2, q3, aligned),
             s90 = buzz_geometry_shuffle_apply(q0, q1, aligned);
        u128 s93 = buzz_geometry_shuffle_apply(s7, s13, p19), s5 = buzz_geometry_shuffle_apply(s90, s7, p19),
             s88 = buzz_geometry_shuffle_apply(previous, s90, p29),
             s11 = buzz_geometry_shuffle_apply(s13, s13, p19);
        u128 s91 = buzz_geometry_shuffle_apply(s88, s5, m9), s77 = buzz_geometry_shuffle_apply(s5, s93, m14),
             s97 = buzz_geometry_shuffle_apply(s93, s11, m14);
        u128 s40 = buzz_geometry_shuffle_apply(s88, s91, p35),
             s74 = buzz_geometry_shuffle_apply(s91, s77, m78),
             s89 = buzz_geometry_shuffle_apply(s77, s97, m82);
        previous = buzz_geometry_shuffle_apply(s74, s89, m76);
        u128 s49 = buzz_geometry_shuffle_apply(s91, s74, p34),
             s47 = buzz_geometry_shuffle_apply(s74, previous, p33);
        q3 = buzz_geometry_ls_read(ctx->ls, next_input._u32[0] + 48);
        q0 = buzz_geometry_ls_read(ctx->ls, next_input._u32[0]);
        q1 = buzz_geometry_ls_read(ctx->ls, next_input._u32[0] + 16);
        q2 = buzz_geometry_ls_read(ctx->ls, next_input._u32[0] + 32);
        buzz_geometry_ls_write(ctx->ls, out._u32[0], s40);
        buzz_geometry_ls_write(ctx->ls, out._u32[0] + 16, s49);
        buzz_geometry_ls_write(ctx->ls, out._u32[0] + 32, s47);
        out = spu_a(out, step);
        step = spu_il(48);
        bits = spu_ai(bits, 2);
        remaining = spu_ai(remaining, -1);
        u128 high = spu_rotmi(spu_and(code, ctx->gpr[36]), -8);
        idx0 = spu_rotmi(spu_and(code, ctx->gpr[37]), -4);
        idx1 = spu_shli(spu_andi(code, 15), 4);
        idx2 = spu_andi(code, 240);
        u128 byte_counts = spu_cntb(spu_and(spu_and(code, spu_a(code, code)), ctx->gpr[38]));
        population = spu_a(spu_rotmi(byte_counts, -8), spu_andi(byte_counts, 255));
        add = spu_sfi(population, 8);
        add = spu_a(add, add);
        unsigned index = high._u32[0] / 16;
        buzz_triangles_prepare(ctx, a, b, &ready, index, table_a, table_b);
        control19 = b[index].raw;
        p19 = b[index].mask;
        control29 = a[index].raw;
        p29 = a[index].mask;
        in = next_input;
        u128 low = spu_andi(in, 15);
        control = spu_a(spu_shufb(low, low, ctx->gpr[85]), ctx->gpr[83]);
    }
    ctx->gpr[32] = q0;
    ctx->gpr[26] = q1;
    ctx->gpr[25] = q2;
    ctx->gpr[30] = q3;
    ctx->gpr[23] = previous;
    ctx->gpr[20] = in;
    ctx->gpr[21] = add;
    ctx->gpr[12] = population;
    ctx->gpr[18] = out;
    ctx->gpr[5] = step;
    ctx->gpr[28] = bits;
    ctx->gpr[22] = remaining;
    ctx->gpr[3] = idx0;
    ctx->gpr[7] = idx1;
    ctx->gpr[8] = idx2;
    ctx->gpr[10] = control;
    ctx->gpr[19] = control19;
    ctx->gpr[29] = control29;
    last(ctx);
    return 1;
}
