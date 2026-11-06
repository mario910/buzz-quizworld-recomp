#pragma once
#include "native_geometry_ls.h"

static inline u128 stagef_word_splat(uint32_t value)
{
    u128 result;
    vst1q_u32(result._u32, vdupq_n_u32(value));
    return result;
}

static inline int buzz_stagef_arm_admit(const spu_context* ctx)
{
    extern int buzz_geometry_debug;
    return ctx->ls == ctx->ls_store && !buzz_geometry_debug && g_spu_ls_probe <= 0 &&
           g_spu_ls_watch_n <= 0 && g_spu_smc_watch <= 0 &&
           !(ctx->int_enable && (ctx->event_status & ctx->event_mask));
}

static inline void stagef_transpose4(u128 words[4])
{
    const uint32x4x2_t ab = vzipq_u32(vld1q_u32(words[0]._u32), vld1q_u32(words[1]._u32));
    const uint32x4x2_t cd = vzipq_u32(vld1q_u32(words[2]._u32), vld1q_u32(words[3]._u32));
    for (unsigned pair = 0; pair < 2; ++pair) {
        const uint64x2_t x = vreinterpretq_u64_u32(ab.val[pair]);
        const uint64x2_t y = vreinterpretq_u64_u32(cd.val[pair]);
        vst1q_u32(words[2*pair]._u32, vreinterpretq_u32_u64(vzip1q_u64(x, y)));
        vst1q_u32(words[2*pair + 1]._u32, vreinterpretq_u32_u64(vzip2q_u64(x, y)));
    }
}

static inline void stagef_load4(const uint8_t* ls, uint32_t address, u128 columns[4])
{
    for (unsigned lane = 0; lane < 4; ++lane)
        columns[lane] = buzz_geometry_ls_read(ls, address + 16*lane);
    stagef_transpose4(columns);
}

static inline void stagef_store4(uint8_t* ls, uint32_t address, u128 columns[4])
{
    stagef_transpose4(columns);
    for (unsigned lane = 0; lane < 4; ++lane)
        buzz_geometry_ls_write(ls, address + 16*lane, columns[lane]);
}

static int buzz_stagef_native_xform(spu_context* ctx)
{
    const uint32_t count = ctx->gpr[3]._u32[0];
    const uint32_t in = ctx->gpr[4]._u32[0] & 0x3fff0;
    const uint32_t out = ctx->gpr[5]._u32[0] & 0x3fff0;
    const uint32_t matrix = ctx->gpr[6]._u32[0];
    if (!buzz_stagef_arm_admit(ctx) || !count || count > 0x2000) return 0;
    const uint32_t padded = (count + 7u) & ~7u, bytes = padded*16;
    if (in + bytes > SPU_LS_SIZE || out + bytes > SPU_LS_SIZE ||
        (in != out && in < out + bytes && out < in + bytes)) return 0;

    u128 coefficients[4][4];
    for (unsigned row = 0; row < 4; ++row) {
        const u128 values = buzz_geometry_ls_read(ctx->ls, matrix + 16*row);
        for (unsigned col = 0; col < 4; ++col)
            coefficients[row][col] = stagef_word_splat(values._u32[col]);
    }
    for (unsigned offset = 0; offset < bytes; offset += 64) {
        u128 columns[4], result[4];
        stagef_load4(ctx->ls, in + offset, columns);
        // Each SPU output row starts with its diagonal product, then wraps.
        for (unsigned row = 0; row < 4; ++row) {
            u128 sum = spu_fm(columns[row], coefficients[row][row]);
            for (unsigned term = 1; term < 4; ++term) {
                const unsigned col = (row + term) & 3;
                sum = spu_fma(columns[col], coefficients[row][col], sum);
            }
            result[row] = sum;
        }
        stagef_store4(ctx->ls, out + offset, result);
    }
    ctx->pc = ctx->gpr[0]._u32[0];
    g_spu_trampoline_fn = 0;
    return 1;
}

static int buzz_stagef_native_normalize(spu_context* ctx)
{
    const uint32_t count = ctx->gpr[4]._u32[0];
    const uint32_t base = ctx->gpr[3]._u32[0] & 0x3fff0;
    if (!buzz_stagef_arm_admit(ctx) || !count || count > 0x4000) return 0;
    const uint32_t bytes = ((count + 3u) & ~3u)*16;
    if (base + bytes > SPU_LS_SIZE) return 0;

    const u128 one = stagef_word_splat(0x3f800000u);
    for (unsigned offset = 0; offset < bytes; offset += 64) {
        u128 columns[4];
        stagef_load4(ctx->ls, base + offset, columns);
        const u128 x = columns[0], y = columns[1], z = columns[2];
        const u128 sq = spu_fma(z, z, spu_fma(y, y, spu_fm(x, x)));
        const u128 reciprocal = spu_fi(sq, spu_frsqest(sq));
        for (unsigned lane = 0; lane < 3; ++lane)
            columns[lane] = spu_fm(columns[lane], reciprocal);
        columns[3] = spu_fm(columns[3], one);
        stagef_store4(ctx->ls, base + offset, columns);
    }
    ctx->pc = ctx->gpr[0]._u32[0];
    g_spu_trampoline_fn = 0;
    return 1;
}
