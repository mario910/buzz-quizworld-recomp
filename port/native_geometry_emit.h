#pragma once
#include "native_geometry_ls.h"
/* Raw float3 output columns: 46FC -> {4750,4808} -> 47F0. Both alignment
 * arms share one operation and switch. Preserve the last lifted column. */
static int buzz_emit_native(spu_context* ctx, void (*last)(spu_context*))
{
    if (ctx->ls != ctx->ls_store || buzz_geometry_debug || g_spu_ls_probe > 0 ||
        g_spu_ls_watch_n > 0 || g_spu_smc_watch > 0 ||
        (ctx->int_enable && (ctx->event_status & ctx->event_mask)) ||
        !buzz_emit_constants_match(ctx)) return 0;
    const unsigned column = ctx->gpr[32]._u32[0], columns = ctx->gpr[60]._u32[0];
    const unsigned base = ctx->gpr[19]._u32[0], stride = ctx->gpr[55]._u32[0];
    const unsigned attribute = ctx->gpr[53]._u32[0], in_step = ctx->gpr[29]._u32[0];
    const unsigned out_step = ctx->gpr[31]._u32[0], source = ctx->gpr[52]._u32[0];
    const unsigned limit = ctx->gpr[25]._u32[0], sentinel = ctx->gpr[30]._u32[0];
    if (columns > 16 || column >= columns || columns - column < 2 ||
        ctx->gpr[44]._u32[0] != column*16 || (base & 15) || base < 0xe680 ||
        ctx->gpr[22]._u32[0] != base + 16 || stride < 12 || stride > 64 || (stride & 3) ||
        attribute > 64 || (attribute & 3) || !in_step || (in_step & 15) ||
        !out_step || (out_step & 15) || source < 0xe680 || limit >= SPU_LS_SIZE) return 0;
    const uint64_t first = (uint64_t)source + column*16;
    const uint64_t final = (uint64_t)source + (columns - 1)*16;
    if (first > limit || final > limit) return 0;
    const unsigned groups = (unsigned)((limit - first)/in_step + 2);
    if (groups > 4096 || groups != (limit - final)/in_step + 2) return 0;
    const unsigned offsets[4] = {0, ctx->gpr[28]._u32[0], ctx->gpr[27]._u32[0], ctx->gpr[26]._u32[0]};
    unsigned max_offset = 0;
    for (unsigned j = 0; j < 4; ++j) {
        if ((offsets[j] & 15) || ctx->gpr[55]._u32[j] != stride ||
            ctx->gpr[53]._u32[j] != attribute || ctx->gpr[29]._u32[j] != in_step ||
            ctx->gpr[31]._u32[j] != out_step || ctx->gpr[25]._u32[j] != limit ||
            ctx->gpr[30]._u32[j] != sentinel ||
            ctx->gpr[52]._u32[j] + (columns - 1)*16ull + (groups - 1ull)*in_step >= SPU_LS_SIZE) return 0;
        if (offsets[j] > max_offset) max_offset = offsets[j];
    }
    const uint64_t input_first = first & ~15ull;
    const uint64_t input_end = final + (groups - 1ull)*in_step + max_offset + 16;
    const uint64_t output_first = ((uint64_t)base + stride*column + attribute) & ~15ull;
    const uint64_t output_end = (uint64_t)base + stride*(columns - 1 + 12) + attribute + (groups - 1ull)*out_step + 32;
    const uint64_t sent_first = ((uint64_t)base + sentinel) & ~15ull, sent_end = sent_first + 32;
    if (input_end > SPU_LS_SIZE || output_end > SPU_LS_SIZE || sent_first < 0xe680 || sent_end > SPU_LS_SIZE ||
        (input_first < output_end && output_first < input_end) ||
        (input_first < sent_end && sent_first < input_end) ||
        (output_first < sent_end && sent_first < output_end)) return 0;
    for (unsigned col = column; col < columns - 1; ++col) {
        const unsigned within = (stride*col + attribute) & 15;
        for (unsigned group = 0; group < groups; ++group) {
            for (unsigned lane = 0; lane < 4; ++lane) {
                const unsigned cursor = ctx->gpr[52]._u32[lane] + col*16 + group*in_step;
                const unsigned route = cursor > limit ? sentinel : stride*(col + lane*4) + attribute + group*out_step;
                // The nominal field's low nibble remains active on a sentinel
                // route. Using the sentinel's nibble would corrupt padding.
                const unsigned output = ((base + route) & ~15u) + within;
                const unsigned input = (source + col*16 + group*in_step + offsets[lane]) & ~15u;
                memcpy(ctx->ls + output, ctx->ls + input, 12);
            }
        }
        // 4808 leaves r12 unchanged; carry the most recent straddled column's
        // padded input load into a possible non-straddled final column.
        if (within > 4)
            ctx->gpr[12] = buzz_geometry_ls_read(ctx->ls, source + col*16 + (groups - 1)*in_step + offsets[2]);
    }
    for (unsigned j = 0; j < 4; ++j) {
        ctx->gpr[32]._u32[j] += columns - column - 1;
        ctx->gpr[44]._u32[j] += (columns - column - 1)*16;
    }
    last(ctx);
    return 1;
}
