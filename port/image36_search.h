#ifndef BUZZ_IMAGE36_SEARCH_H
#define BUZZ_IMAGE36_SEARCH_H
/* Exact integer search for the pinned EU 1.00 image36 15BC/15A8 loop.
 * Caller must exclude observers, interrupts, foreign LS and active LS probes.
 * Return 0 without mutation for unsupported input; otherwise the boundary PC.
 * skipped is the alternating 15A8/15BC steps after the entry's first 15BC. */
#include "spu_helpers.h"
#include <string.h>
#if defined(__AVX2__)
#include <immintrin.h>
#endif

static inline void image36_search_advance(spu_context* ctx)
{
    ctx->gpr[5] = spu_ahi(ctx->gpr[5], 1);
    ctx->gpr[44] = spu_and(ctx->gpr[6], ctx->gpr[5]);
    ctx->gpr[3] = spu_shli(ctx->gpr[44], 2);
    ctx->gpr[43] = spu_rotqmbyi(ctx->gpr[44], -4);
    ctx->gpr[42] = spu_clgt(ctx->gpr[11], ctx->gpr[43]);
    ctx->gpr[41] = spu_ceq(ctx->gpr[11], ctx->gpr[43]);
    ctx->gpr[89] = spu_xswd(ctx->gpr[42]);
    ctx->gpr[4] = spu_a(ctx->gpr[3], ctx->gpr[9]);
    ctx->gpr[40] = spu_selb(ctx->gpr[42], ctx->gpr[89], ctx->gpr[41]);
}

static inline uint32_t image36_search(spu_context* ctx, uint32_t* skipped)
{
    const uint32_t start = ctx->gpr[5]._u32[0] & 65535u;
    const uint32_t limit = ctx->gpr[11]._u32[1];
    const uint32_t base = ctx->gpr[9]._u32[0];
    if (ctx->ls != ctx->ls_store || ctx->gpr[6]._u32[0] != 65535u ||
        ctx->gpr[11]._u32[0] != 0 || limit > 65535u || start == 65535u ||
        (base & 3u) || base > SPU_LS_SIZE || limit > (SPU_LS_SIZE-base)/4u)
        return 0;
    uint32_t i = start + 1u;
    if (i >= limit) {
        image36_search_advance(ctx);
        *skipped = 0;
        return 0x15E8u;
    }
    const uint32_t target = SPU_BSWAP32(ctx->gpr[7]._u32[0]);
#if defined(__AVX2__)
    const __m256i needle = _mm256_set1_epi32((int)target);
    while (limit-i >= 8u) {
        __m256i words = _mm256_loadu_si256((const __m256i*)(ctx->ls+base+4u*i));
        unsigned mask = (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(words, needle)));
        if (mask) { i += (unsigned)__builtin_ctz(mask); goto found; }
        i += 8u;
    }
#endif
    for (; i < limit; ++i) {
        uint32_t word;
        memcpy(&word, ctx->ls+base+4u*i, 4);
        if (word == target) goto found;
    }
found:;
    const int hit = i < limit;
    const uint32_t last = hit ? i : limit-1u;
    /* Addition is per halfword, exactly like repeated ahi; no word carry. */
    ctx->gpr[5] = spu_ahi(ctx->gpr[5], (int32_t)(last-start-1u));
    image36_search_advance(ctx);
    ctx->gpr[39] = spu_ls_read128(ctx, ctx->gpr[3]._u32[0] + ctx->gpr[9]._u32[0]);
    ctx->gpr[38] = spu_rotqby(ctx->gpr[39], ctx->gpr[4]);
    ctx->gpr[37] = spu_ceq(ctx->gpr[38], ctx->gpr[7]);
    *skipped = 2u*(last-start) - (uint32_t)hit;
    if (!hit) image36_search_advance(ctx);
    return hit ? 0x1628u : 0x15E8u;
}

#endif
