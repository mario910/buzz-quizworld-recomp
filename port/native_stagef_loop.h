/* Native image 41 stage-F vertex loop (SPU 0x6338 + 0x63E8).
 *
 * Included into the generated geometry translation unit
 * (generate-geometry-stagef.py), so every float operation below is the SAME
 * helper the lift uses (RTZ FMA with exact fallback, the frest table, the fi
 * step, cflts) with the same compiler flags: results are bit-identical lane
 * by lane. What changes is the structure: the SPU loop is software-pipelined
 * three groups deep and shuffles AoS<->SoA; here each group of four vertices
 * is loaded, transformed and stored once.
 *
 * Contract (verified on the job corpus, see docs/job41-edge-geometry.md):
 * inputs are the registers 0x60E8 leaves at 0x6338's entry; outputs are the
 * 4*G 16-byte records and the return through r0. r2..r79 are dead after the
 * return (poisoning them changes no job output).
 *
 * Per vertex (x, y, z from the input record; w ignored):
 *   W = z*m65 + (y*m66 + (x*m68 + m67))      (fma chain, same order)
 *   r = fi(W, frest(W)); R = (1 - r*W)*r + r
 *   X, Y, Z = rows (m61,m60,m58 / m56,m55,m54 / m53,m52,m51 + m64,m63,m62)
 *   out = { sign(W)^X*R, sign(W)^Y*R, flags << 24,
 *           (cflts(X',3) & mask) << 16 | (cflts(Y',3) & mask) & 0xFFFF, bit 31 = W > 0 }
 *   flags = zmin>Z' | xmin>X' | ymin>Y' | X'>xmax | Y'>ymax | Z'>zmax  (bits 7..2)
 */
#include <immintrin.h>

// Canonical algorithm inputs -> reviewed compiler register allocation. Only
// vertex-input reads use this map; architectural state is never renumbered.
static inline unsigned stagef_vertex_reg(unsigned reg)
{
#ifdef BUZZ_STAGEF_DISC100
    switch (reg) {
    case 4: return 11; case 7: return 8; case 5: return 7;
    case 17: return 4; case 10: return 12; case 9: return 10; case 8: return 9;
    case 50: return 51; case 51: return 52; case 52: return 53;
    case 53: return 54; case 54: return 55; case 55: return 57;
    case 56: return 59; case 58: return 60; case 60: return 61;
    case 61: return 63; case 62: return 62; case 63: return 64;
    case 64: return 65; case 65: return 66; case 66: return 67;
    case 67: return 68; case 68: return 69; case 69: return 70;
    }
#endif
    return reg;
}
static inline uint32_t stagef_vertex_word(const spu_context* ctx, unsigned reg)
{
    return ctx->gpr[stagef_vertex_reg(reg)]._u32[0];
}

static inline __m128i stagef_ld(const uint8_t* ls, uint32_t a)
{
    const __m128i swap = _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
    return _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(ls + (a & 0x3FFF0))), swap);
}
static inline void stagef_st(uint8_t* ls, uint32_t a, __m128i v)
{
    const __m128i swap = _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
    _mm_storeu_si128((__m128i*)(ls + (a & 0x3FFF0)), _mm_shuffle_epi8(v, swap));
}
static inline u128 stagef_u(__m128i v) { u128 r; memcpy(&r, &v, 16); return r; }
static inline __m128i stagef_m(u128 v) { __m128i r; memcpy(&r, &v, 16); return r; }
static inline u128 stagef_splat(const spu_context* ctx, int reg, int lane)
{
    return stagef_u(_mm_set1_epi32((int)ctx->gpr[stagef_vertex_reg(reg)]._u32[lane]));
}
static inline u128 stagef_row(u128 x, u128 y, u128 z, u128 mx, u128 my, u128 mz, u128 t)
{
    return spu_fma(z, mz, spu_fma(y, my, spu_fma(x, mx, t)));
}

#include "native_stagef_avx2.h"

/* 8 = two groups per AVX2 step (default), 4 = the u128 helper path (A/B). */
extern int buzz_stagef_width;

static inline v8 stagef_row8(v8 x, v8 y, v8 z, v8 mx, v8 my, v8 mz, v8 t)
{
    return v8_fma(z, mz, v8_fma(y, my, v8_fma(x, mx, t)));
}
static inline v8 stagef_bcast(const spu_context* ctx, int reg, int lane) { return v8_set(ctx->gpr[stagef_vertex_reg(reg)]._u32[lane]); }

/* Groups g and g+1 of the vertex loop at once (same operations per lane). */
static void stagef_vertex_pair(spu_context* ctx, const uint32_t in[4], const uint32_t out[4], uint32_t off,
                               uint32_t g, __m128i mask4)
{
    uint8_t* ls = ctx->ls;
    const v8 m51 = stagef_bcast(ctx, 51, 0), m52 = stagef_bcast(ctx, 52, 0), m53 = stagef_bcast(ctx, 53, 0),
             m54 = stagef_bcast(ctx, 54, 0), m55 = stagef_bcast(ctx, 55, 0), m56 = stagef_bcast(ctx, 56, 0),
             m58 = stagef_bcast(ctx, 58, 0), m60 = stagef_bcast(ctx, 60, 0), m61 = stagef_bcast(ctx, 61, 0),
             m62 = stagef_bcast(ctx, 62, 0), m63 = stagef_bcast(ctx, 63, 0), m64 = stagef_bcast(ctx, 64, 0),
             m65 = stagef_bcast(ctx, 65, 0), m66 = stagef_bcast(ctx, 66, 0), m67 = stagef_bcast(ctx, 67, 0),
             m68 = stagef_bcast(ctx, 68, 0);
    const v8 xmax = stagef_bcast(ctx, 26, 0), ymax = stagef_bcast(ctx, 26, 1), zmax = stagef_bcast(ctx, 26, 2);
    const v8 xmin = stagef_bcast(ctx, 27, 0), ymin = stagef_bcast(ctx, 27, 1), zmin = stagef_bcast(ctx, 27, 2);
    const v8 sign = stagef_bcast(ctx, 50, 0), one = stagef_bcast(ctx, 71, 0), zero = stagef_bcast(ctx, 69, 0);
    const v8 mask = _mm256_broadcastsi128_si256(mask4);
    const uint32_t o0 = off + 64u * g, o1 = o0 + 64u;
    const v8 v0 = v8_ld2(ls, in[0] + o0, in[0] + o1), v1 = v8_ld2(ls, in[1] + o0, in[1] + o1);
    const v8 v2 = v8_ld2(ls, in[2] + o0, in[2] + o1), v3 = v8_ld2(ls, in[3] + o0, in[3] + o1);
    const v8 t0 = _mm256_unpacklo_epi32(v0, v1), t1 = _mm256_unpacklo_epi32(v2, v3);
    const v8 t2 = _mm256_unpackhi_epi32(v0, v1), t3 = _mm256_unpackhi_epi32(v2, v3);
    const v8 x = _mm256_unpacklo_epi64(t0, t1), y = _mm256_unpackhi_epi64(t0, t1), z = _mm256_unpacklo_epi64(t2, t3);
    const v8 W = stagef_row8(x, y, z, m68, m66, m65, m67);
    const v8 s = _mm256_and_si256(W, sign);
    const v8 r = v8_fi(W, v8_frest(W));
    const v8 R = v8_fma(v8_fnms(r, W, one), r, r);
    const v8 X = _mm256_xor_si256(s, v8_fm(stagef_row8(x, y, z, m61, m56, m53, m64), R));
    const v8 Y = _mm256_xor_si256(s, v8_fm(stagef_row8(x, y, z, m60, m55, m52, m63), R));
    const v8 Z = _mm256_xor_si256(s, v8_fm(stagef_row8(x, y, z, m58, m54, m51, m62), R));
    v8 flags = _mm256_and_si256(v8_fcgt(zmin, Z), v8_set(0x80));
    flags = _mm256_or_si256(flags, _mm256_and_si256(v8_fcgt(xmin, X), v8_set(0x40)));
    flags = _mm256_or_si256(flags, _mm256_and_si256(v8_fcgt(ymin, Y), v8_set(0x20)));
    flags = _mm256_or_si256(flags, _mm256_and_si256(v8_fcgt(X, xmax), v8_set(0x10)));
    flags = _mm256_or_si256(flags, _mm256_and_si256(v8_fcgt(Y, ymax), v8_set(0x08)));
    flags = _mm256_or_si256(flags, _mm256_and_si256(v8_fcgt(Z, zmax), v8_set(0x04)));
    const v8 w2 = _mm256_slli_epi32(flags, 24);
    const v8 cx = _mm256_and_si256(v8_cflts3(X), mask), cy = _mm256_and_si256(v8_cflts3(Y), mask);
    v8 w3 = _mm256_or_si256(_mm256_slli_epi32(cx, 16), _mm256_and_si256(cy, v8_set(0xFFFF)));
    w3 = _mm256_or_si256(_mm256_and_si256(w3, v8_set(0x7FFFFFFF)),
                         _mm256_and_si256(v8_fcgt(W, zero), v8_set(0x80000000u)));
    const v8 a0 = _mm256_unpacklo_epi32(X, Y), a1 = _mm256_unpacklo_epi32(w2, w3);
    const v8 a2 = _mm256_unpackhi_epi32(X, Y), a3 = _mm256_unpackhi_epi32(w2, w3);
    const uint32_t p0 = 64u * g, p1 = p0 + 64u;
    v8_st2(ls, out[0] + p0, out[0] + p1, _mm256_unpacklo_epi64(a0, a1));
    v8_st2(ls, out[1] + p0, out[1] + p1, _mm256_unpackhi_epi64(a0, a1));
    v8_st2(ls, out[2] + p0, out[2] + p1, _mm256_unpacklo_epi64(a2, a3));
    v8_st2(ls, out[3] + p0, out[3] + p1, _mm256_unpackhi_epi64(a2, a3));
}

/* Why the last call fell back: 1 odd count, 2 no full group, 3 overlap. */
static _Thread_local int buzz_stagef_fallback_reason;
int buzz_stagef_last_fallback(void) { return buzz_stagef_fallback_reason; }

/* Returns 1 when it produced the result, 0 to let the reference run. */
static int buzz_stagef_native_loop(spu_context* ctx)
{
    const int32_t start = (int32_t)ctx->gpr[30]._u32[0];
    if (start >= 0 || (start & 63)) { buzz_stagef_fallback_reason = 1; return 0; }
    const int groups = -start / 64 - 3;
    if (groups < 1) { buzz_stagef_fallback_reason = 2; return 0; }   /* prologue junk would survive: keep the reference */
    const uint32_t mode = ctx->gpr[6]._u32[0];
    const __m128i mask = _mm_set1_epi32(mode == 0 ? (int)0xFFFFFFFCu : mode == 1 ? (int)0xFFFFFFFEu : -1);
    const uint32_t in[4] = { stagef_vertex_word(ctx,4), stagef_vertex_word(ctx,7), stagef_vertex_word(ctx,5), stagef_vertex_word(ctx,3) };
    const uint32_t out[4] = { stagef_vertex_word(ctx,17), stagef_vertex_word(ctx,10), stagef_vertex_word(ctx,9), stagef_vertex_word(ctx,8) };
    /* The SPU reads group g+3 before it writes group g; keep that order out of
     * the question by requiring the record arrays not to overlap the input
     * groups that are used. The three groups it prefetches past the end are
     * only read for the pipeline and never stored, so the output may start
     * there (the common layout: records right after the vertices). */
    for (int j = 0; j < 4; j++)
        for (int k = 0; k < 4; k++) {
            const uint32_t i0 = in[j] + (uint32_t)start, i1 = i0 + 64u * (uint32_t)(groups - 1);
            const uint32_t o0 = out[k], o1 = out[k] + 64u * (uint32_t)groups;
            if (o0 < i1 + 16 && i0 < o1) {
                static volatile long shown;
                if (getenv("BUZZ_STAGEF_OVERLAP_LOG") && _InterlockedIncrement(&shown) <= 6)
                    fprintf(stderr, "[stagef] overlap: groups=%d start=%d in=%05X,%05X,%05X,%05X out=%05X,%05X,%05X,%05X\n",
                            groups, start, in[0], in[1], in[2], in[3], out[0], out[1], out[2], out[3]);
                buzz_stagef_fallback_reason = 3; return 0;
            }
        }
    const u128 m51 = stagef_splat(ctx, 51, 0), m52 = stagef_splat(ctx, 52, 0), m53 = stagef_splat(ctx, 53, 0),
               m54 = stagef_splat(ctx, 54, 0), m55 = stagef_splat(ctx, 55, 0), m56 = stagef_splat(ctx, 56, 0),
               m58 = stagef_splat(ctx, 58, 0), m60 = stagef_splat(ctx, 60, 0), m61 = stagef_splat(ctx, 61, 0),
               m62 = stagef_splat(ctx, 62, 0), m63 = stagef_splat(ctx, 63, 0), m64 = stagef_splat(ctx, 64, 0),
               m65 = stagef_splat(ctx, 65, 0), m66 = stagef_splat(ctx, 66, 0), m67 = stagef_splat(ctx, 67, 0),
               m68 = stagef_splat(ctx, 68, 0);
    const u128 xmax = stagef_splat(ctx, 26, 0), ymax = stagef_splat(ctx, 26, 1), zmax = stagef_splat(ctx, 26, 2);
    const u128 xmin = stagef_splat(ctx, 27, 0), ymin = stagef_splat(ctx, 27, 1), zmin = stagef_splat(ctx, 27, 2);
    const u128 sign = stagef_splat(ctx, 50, 0), one = stagef_splat(ctx, 71, 0), zero = stagef_splat(ctx, 69, 0);
    uint8_t* ls = ctx->ls;
    int first = 0;
    if (buzz_stagef_width == 8)
        for (; first + 1 < groups; first += 2)
            stagef_vertex_pair(ctx, in, out, (uint32_t)start, (uint32_t)first, mask);
    for (int g = first; g < groups; g++) {
        const uint32_t off = (uint32_t)start + 64u * (uint32_t)g;
        __m128i v0 = stagef_ld(ls, in[0] + off), v1 = stagef_ld(ls, in[1] + off);
        __m128i v2 = stagef_ld(ls, in[2] + off), v3 = stagef_ld(ls, in[3] + off);
        /* AoS (x y z w per vertex) -> SoA (one component of 4 vertices) */
        __m128i t0 = _mm_unpacklo_epi32(v0, v1), t1 = _mm_unpacklo_epi32(v2, v3);
        __m128i t2 = _mm_unpackhi_epi32(v0, v1), t3 = _mm_unpackhi_epi32(v2, v3);
        const u128 x = stagef_u(_mm_unpacklo_epi64(t0, t1)), y = stagef_u(_mm_unpackhi_epi64(t0, t1));
        const u128 z = stagef_u(_mm_unpacklo_epi64(t2, t3));
        const u128 W = stagef_row(x, y, z, m68, m66, m65, m67);
        const u128 s = spu_and(W, sign);
        const u128 r = spu_fi(W, spu_frest(W));
        const u128 R = spu_fma(spu_fnms(r, W, one), r, r);
        const u128 X = spu_xor(s, spu_fm(stagef_row(x, y, z, m61, m56, m53, m64), R));
        const u128 Y = spu_xor(s, spu_fm(stagef_row(x, y, z, m60, m55, m52, m63), R));
        const u128 Z = spu_xor(s, spu_fm(stagef_row(x, y, z, m58, m54, m51, m62), R));
        __m128i flags = _mm_and_si128(stagef_m(spu_fcgt(zmin, Z)), _mm_set1_epi32(0x80));
        flags = _mm_or_si128(flags, _mm_and_si128(stagef_m(spu_fcgt(xmin, X)), _mm_set1_epi32(0x40)));
        flags = _mm_or_si128(flags, _mm_and_si128(stagef_m(spu_fcgt(ymin, Y)), _mm_set1_epi32(0x20)));
        flags = _mm_or_si128(flags, _mm_and_si128(stagef_m(spu_fcgt(X, xmax)), _mm_set1_epi32(0x10)));
        flags = _mm_or_si128(flags, _mm_and_si128(stagef_m(spu_fcgt(Y, ymax)), _mm_set1_epi32(0x08)));
        flags = _mm_or_si128(flags, _mm_and_si128(stagef_m(spu_fcgt(Z, zmax)), _mm_set1_epi32(0x04)));
        const __m128i w2 = _mm_slli_epi32(flags, 24);
        const __m128i cx = _mm_and_si128(stagef_m(spu_cflts(X, 170)), mask);
        const __m128i cy = _mm_and_si128(stagef_m(spu_cflts(Y, 170)), mask);
        __m128i w3 = _mm_or_si128(_mm_slli_epi32(cx, 16), _mm_and_si128(cy, _mm_set1_epi32(0xFFFF)));
        w3 = _mm_or_si128(_mm_and_si128(w3, _mm_set1_epi32(0x7FFFFFFF)),
                          _mm_and_si128(stagef_m(spu_fcgt(W, zero)), _mm_set1_epi32((int)0x80000000u)));
        /* SoA -> one record (X Y w2 w3) per vertex */
        const __m128i xo = stagef_m(X), yo = stagef_m(Y);
        __m128i a0 = _mm_unpacklo_epi32(xo, yo), a1 = _mm_unpacklo_epi32(w2, w3);
        __m128i a2 = _mm_unpackhi_epi32(xo, yo), a3 = _mm_unpackhi_epi32(w2, w3);
        stagef_st(ls, out[0] + 64u * (uint32_t)g, _mm_unpacklo_epi64(a0, a1));
        stagef_st(ls, out[1] + 64u * (uint32_t)g, _mm_unpackhi_epi64(a0, a1));
        stagef_st(ls, out[2] + 64u * (uint32_t)g, _mm_unpacklo_epi64(a2, a3));
        stagef_st(ls, out[3] + 64u * (uint32_t)g, _mm_unpackhi_epi64(a2, a3));
    }
    ctx->pc = ctx->gpr[0]._u32[0];
    g_spu_trampoline_fn = 0;
    return 1;
}

/* ---- Native triangle cull (SPU 0x6D28 + loop 0x6E00/0x6DF8, exit 0x6888) ----
 *
 * Per triangle (v0, v1, v2) = u16 indices, records V = {X, Y, F, P} from the
 * vertex loop above (F = outcode byte << 24, P = snapped x|y, bit 31 = W > 0),
 * lane L of the group of four using lane L of the mode registers:
 *   c    = (X1-X0)*(Y2-Y0) - (Y1-Y0)*(X2-X0)                       (fs/fm)
 *   zero = fcmeq(c, r72) | (P0==P1 && P0==P2 in either halfword)
 *   back = fcgt(c ^ r48, r72) & (r82 >u (F0|F1|F2)) & sra(P0&P1&P2, 31)
 *   clip = (F0 & F1 & F2) != 0
 *   culled = bit 0 of zero | fnms(back, r83, clip)     (r83: 0 off, -1 on)
 * The last group's padding lanes are culled by r84 (gb bit order).
 * Kept triangles are compacted in place exactly as the SPU does it: per group
 * three quadword stores at the aligned cursor, shuffled through the job's own
 * tables (kept-mask tables at r8/r7, alignment table at r6), the second and
 * third redirected to scratch past the r66 limits. That reproduces the bytes
 * up to the 16-byte boundary after the last kept triangle, which the index
 * PUT sends. Beyond that boundary nothing reads the LS (corpus check
 * BUZZ_STAGEF_CULLTEST), nor r2..r102 except r51/r81 at 0x6888; the loop is
 * de-pipelined (tools/cull_model.py is the checked Python form). */
#ifdef BUZZ_STAGEF_DISC100
#define STAGEF_CULL_EXIT buzz_job_6eba_spu_func_00006958
#define STAGEF_CULL_PC 0x6958
#define STAGEF_CULL_SEL 0xD980
#define STAGEF_CULL_TOGGLE 0xCFF0
#else
#define STAGEF_CULL_EXIT buzz_job_6eba_spu_func_00006888
#define STAGEF_CULL_PC 0x6888
#define STAGEF_CULL_SEL 0xD800
#define STAGEF_CULL_TOGGLE 0xCEB0
#endif
void STAGEF_CULL_EXIT(spu_context* ctx);
static inline unsigned stagef_cull_reg(unsigned reg)
{
#ifdef BUZZ_STAGEF_DISC100
    switch(reg) {
    case 6: return 3; case 39: return 37; case 50: return 48;
    case 51: return 50; case 59: return 57; case 66: return 64;
    case 72: return 70; case 82: return 80; case 83: return 82;
    }
#endif
    return reg;
}
#define CULL_GPR(r) ctx->gpr[stagef_cull_reg(r)]

static inline u128 stagef_lane_gather(const uint8_t* ls, uint32_t base, const uint16_t* idx, int word)
{
    u128 r;
    for (int l = 0; l < 4; l++) {
        const uint8_t* q = ls + ((base + 16u * idx[l]) & 0x3FFF0) + 4 * word;
        r._u32[l] = ((uint32_t)q[0] << 24) | ((uint32_t)q[1] << 16) | ((uint32_t)q[2] << 8) | q[3];
    }
    return r;
}

#define STAGEF_MAX_GROUPS 8192u

static int buzz_stagef_native_cull(spu_context* ctx)
{
    const uint32_t groups = ctx->gpr[15]._u32[0];
    const uint32_t in = CULL_GPR(13)._u32[0] + CULL_GPR(50)._u32[0];
    const uint32_t verts = ctx->gpr[10]._u32[0];
    uint32_t cursor = CULL_GPR(39)._u32[0];
    if (groups == 0 || groups > STAGEF_MAX_GROUPS || cursor != in || CULL_GPR(51)._u32[0] != 0 ||
        ctx->gpr[41]._u32[0] != 0 || ctx->gpr[43]._u32[0] != 0 || ctx->gpr[84]._u32[0] > 15) {
        buzz_stagef_fallback_reason = 1;
        return 0;
    }
    /* r48: the winding flip 0x6D28 derives from the job configuration. */
    const u128 cfg = spu_ls_read128(ctx, ctx->gpr[81]._u32[0]);
    const u128 mode = spu_rothmi(spu_andi(spu_rotqbyi(cfg, 3), 255), -4);
    const u128 pick = spu_or(spu_ceqi(mode, 2), spu_ceqi(mode, 0));
    const u128 flip = spu_and(spu_shufb(pick, pick, ctx->gpr[12]), stagef_u(_mm_set1_epi32((int)0x80000000u)));
    const u128 r48 = spu_xor(ctx->gpr[11], flip);
    const u128 r72 = CULL_GPR(72), r82 = CULL_GPR(82), r83 = CULL_GPR(83);
    const uint32_t pad = ctx->gpr[84]._u32[0];
    const u128 sel0 = spu_ls_read128(ctx, STAGEF_CULL_SEL), toggle = spu_ls_read128(ctx, STAGEF_CULL_TOGGLE);
    const u128 sel1 = spu_xor(sel0, toggle);
    const uint32_t t_mask1 = CULL_GPR(8)._u32[0], t_mask2 = CULL_GPR(7)._u32[0], t_align = CULL_GPR(6)._u32[0];
    const uint32_t lim0 = CULL_GPR(66)._u32[0], lim1 = CULL_GPR(66)._u32[1], scratch = CULL_GPR(59)._u32[0];
    /* per-thread work buffers, allocated once (not TLS-sized arrays: every
     * host thread, PPU ones included, would carry them) */
    static _Thread_local u128 (*part)[2];
    static _Thread_local uint8_t* masks;
    static _Thread_local uint32_t* vidx;   /* [3][4 * STAGEF_MAX_GROUPS] vertex indices */
    if (!part) {
        part = (u128 (*)[2])malloc(sizeof(u128) * 2 * STAGEF_MAX_GROUPS);
        masks = (uint8_t*)malloc(STAGEF_MAX_GROUPS);
        vidx = (uint32_t*)malloc(sizeof(uint32_t) * 3 * 4 * STAGEF_MAX_GROUPS);
        if (!part || !masks || !vidx) { buzz_stagef_fallback_reason = 1; return 0; }
    }
    const uint8_t* ls = ctx->ls;
    const uint32_t stride = 4u * STAGEF_MAX_GROUPS;
    /* Pass 1: every group's indices and cull mask, before any store (the SPU
     * has read a group before its compaction can overwrite it). */
    for (uint32_t g = 0; g < groups; g++) {
        const uint32_t o = in + 24u * g;
        const u128 qa = spu_ls_read128(ctx, o), qb = spu_ls_read128(ctx, o + 16);
        const u128 sel = (g & 1) ? sel1 : sel0;
        const u128 d0 = spu_shufb(qa, qb, sel), d1 = spu_shufb(qb, qb, sel);
        part[g][0] = d0; part[g][1] = d1;
        for (int l = 0; l < 4; l++)
            for (int v = 0; v < 3; v++) {
                const int b = 6 * l + 2 * v;   /* byte b of d0||d1, SPU order */
                const u128* src = b < 16 ? &d0 : &d1;
                const int k = b & 15;
                const uint32_t w = src->_u32[k >> 2];
                vidx[v * stride + 4u * g + (uint32_t)l] = (uint16_t)(k & 2 ? w : w >> 16);
            }
    }
    uint32_t g8 = 0;
    if (buzz_stagef_width == 8) {
        const v8 R48 = v8_bcast(r48), R72 = v8_bcast(r72), R82 = v8_bcast(r82), R83 = v8_bcast(r83);
        const v8 vbase = v8_set(verts), lsmask = v8_set(0x3FFF0);
        for (; g8 + 1 < groups; g8 += 2) {
            v8 a[3];
            for (int v = 0; v < 3; v++) {
                const v8 i = _mm256_loadu_si256((const __m256i*)(vidx + v * stride + 4u * g8));
                a[v] = _mm256_and_si256(_mm256_add_epi32(vbase, _mm256_slli_epi32(i, 4)), lsmask);
            }
#define STAGEF_G(v, w) v8_bswap(_mm256_i32gather_epi32((const int*)(ls + 4 * (w)), a[v], 1))
            const v8 X0 = STAGEF_G(0, 0), Y0 = STAGEF_G(0, 1), F0 = STAGEF_G(0, 2), P0 = STAGEF_G(0, 3);
            const v8 X1 = STAGEF_G(1, 0), Y1 = STAGEF_G(1, 1), F1 = STAGEF_G(1, 2), P1 = STAGEF_G(1, 3);
            const v8 X2 = STAGEF_G(2, 0), Y2 = STAGEF_G(2, 1), F2 = STAGEF_G(2, 2), P2 = STAGEF_G(2, 3);
#undef STAGEF_G
            const v8 c = v8_fs(v8_fm(v8_fs(X1, X0), v8_fs(Y2, Y0)), v8_fm(v8_fs(Y1, Y0), v8_fs(X2, X0)));
            const v8 same = _mm256_and_si256(_mm256_cmpeq_epi16(P0, P1), _mm256_cmpeq_epi16(P0, P2));
            const v8 zero = _mm256_or_si256(v8_fcmeq(c, R72), v8_nonzero(same));
            const v8 back = _mm256_and_si256(
                _mm256_and_si256(v8_fcgt(_mm256_xor_si256(c, R48), R72),
                                 v8_clgt(R82, _mm256_or_si256(_mm256_or_si256(F0, F1), F2))),
                _mm256_srai_epi32(_mm256_and_si256(P2, _mm256_and_si256(P1, P0)), 31));
            const v8 clip = v8_nonzero(_mm256_and_si256(_mm256_and_si256(F0, F1), F2));
            const v8 lane = _mm256_or_si256(zero, v8_fnms_masks(back, R83, clip));
            uint32_t bits[8];
            _mm256_storeu_si256((__m256i*)bits, lane);
            for (uint32_t h = 0; h < 2; h++) {
                uint32_t mask = 0;
                for (int l = 0; l < 4; l++) mask |= (bits[4 * h + (uint32_t)l] & 1u) << (3 - l);
                if (g8 + h + 1 == groups) mask |= pad;
                masks[g8 + h] = (uint8_t)mask;
            }
        }
    }
    for (uint32_t g = g8; g < groups; g++) {
        uint16_t idx[3][4];
        for (int v = 0; v < 3; v++)
            for (int l = 0; l < 4; l++) idx[v][l] = (uint16_t)vidx[v * stride + 4u * g + (uint32_t)l];
        const u128 X0 = stagef_lane_gather(ls, verts, idx[0], 0), Y0 = stagef_lane_gather(ls, verts, idx[0], 1);
        const u128 X1 = stagef_lane_gather(ls, verts, idx[1], 0), Y1 = stagef_lane_gather(ls, verts, idx[1], 1);
        const u128 X2 = stagef_lane_gather(ls, verts, idx[2], 0), Y2 = stagef_lane_gather(ls, verts, idx[2], 1);
        const u128 F0 = stagef_lane_gather(ls, verts, idx[0], 2), P0 = stagef_lane_gather(ls, verts, idx[0], 3);
        const u128 F1 = stagef_lane_gather(ls, verts, idx[1], 2), P1 = stagef_lane_gather(ls, verts, idx[1], 3);
        const u128 F2 = stagef_lane_gather(ls, verts, idx[2], 2), P2 = stagef_lane_gather(ls, verts, idx[2], 3);
        const u128 c = spu_fs(spu_fm(spu_fs(X1, X0), spu_fs(Y2, Y0)), spu_fm(spu_fs(Y1, Y0), spu_fs(X2, X0)));
        const u128 same = spu_and(spu_ceqh(P0, P1), spu_ceqh(P0, P2));
        const u128 zero = spu_or(spu_fcmeq(c, r72), spu_clgti(same, 0));
        const u128 back = spu_and(spu_and(spu_fcgt(spu_xor(c, r48), r72), spu_clgt(r82, spu_or(spu_or(F0, F1), F2))),
                                  spu_rotmai(spu_and(P2, spu_and(P1, P0)), -31));
        const u128 clip = spu_clgti(spu_and(spu_and(F0, F1), F2), 0);
        const u128 lane = spu_or(zero, spu_fnms(back, r83, clip));
        uint32_t mask = 0;
        for (int l = 0; l < 4; l++) mask |= (lane._u32[l] & 1u) << (3 - l);
        if (g + 1 == groups) mask |= pad;
        masks[g] = (uint8_t)mask;
    }
    /* Pass 2: the SPU's three table-driven quadword stores per group. */
    const u128 ones = stagef_u(_mm_set1_epi32(-1));
    uint32_t kept_total = 0;
    for (uint32_t g = 0; g < groups; g++) {
        const uint32_t mask = masks[g];
        const u128 d0 = part[g][0], d1 = part[g][1];
        const u128 t1 = spu_ls_read128(ctx, t_mask1 + 16u * mask);
        const u128 t2 = spu_ls_read128(ctx, t_mask2 + 16u * mask);
        const u128 t3 = spu_ls_read128(ctx, t_align + 16u * (cursor & 15u));
        const u128 p0 = spu_shufb(d0, d1, t1), p1 = spu_shufb(d1, d1, t2);
        u128 shift; shift._u32[0] = shift._u32[1] = shift._u32[2] = shift._u32[3] = 0u - (cursor & 15u);
        const u128 keep = spu_rotqmby(ones, shift);
        const u128 q0 = spu_shufb(spu_ls_read128(ctx, cursor), p0, spu_selb(sel0, t3, keep));
        spu_ls_write128(ctx, cursor, q0);
        spu_ls_write128(ctx, (cursor > lim0 ? scratch : cursor) + 0x10, spu_shufb(p0, p1, t3));
        spu_ls_write128(ctx, (cursor > lim1 ? scratch : cursor) + 0x20, spu_shufb(p1, p1, t3));
        const uint32_t kept = 4u - (uint32_t)__builtin_popcount(mask);
        cursor += 6u * kept;
        kept_total += kept;
    }
    for (int w = 0; w < 4; w++) CULL_GPR(51)._u32[w] = kept_total;
    ctx->pc = STAGEF_CULL_PC;
    g_spu_trampoline_fn = STAGEF_CULL_EXIT;
    return 1;
}

#undef CULL_GPR
/* ---- Matrix x float4 array (SPU 0xC3A0, loop 0xC488) ----
 * r3 = count, r4 = input, r5 = output (in place in every corpus call),
 * r6 = 4x4 row-major matrix. Per vector v, row k in the SPU's order:
 *   out_k = fma(v[k+3], M[k][k+3], fma(v[k+2], M[k][k+2], fma(v[k+1], M[k][k+1], fm(v[k], M[k][k]))))
 * (indices mod 4; derived with tools/stagef_sym.py). The SPU loop is pipelined
 * one group deep and stores groups 0..ceil(n/8)-1 of eight vectors, the last
 * one padded with whatever follows the array -- the same here. r2..r79 are dead
 * after the return (BUZZ_STAGEF=4 with BUZZ_STAGEF_KERNELS=2 on the corpus). */
static int buzz_stagef_native_xform(spu_context* ctx)
{
    const uint32_t n = ctx->gpr[3]._u32[0];
    const uint32_t in = ctx->gpr[4]._u32[0] & 0x3FFF0, out = ctx->gpr[5]._u32[0] & 0x3FFF0;
    const uint32_t mat = ctx->gpr[6]._u32[0];
    const uint32_t groups = (n + 7u) >> 3;
    if (!groups || groups > 0x400) return 0;   /* n == 0 leaves the pipeline's first store */
    const uint32_t bytes = groups * 128u;
    if (in != out && in < out + bytes && out < in + bytes) return 0;   /* shifted overlap */
    uint8_t* ls = ctx->ls;
    const u128 r0 = spu_ls_read128(ctx, mat), r1 = spu_ls_read128(ctx, mat + 16);
    const u128 r2 = spu_ls_read128(ctx, mat + 32), r3 = spu_ls_read128(ctx, mat + 48);
#define M(row, col) v8_set(row._u32[col])
    const v8 m00 = M(r0, 0), m01 = M(r0, 1), m02 = M(r0, 2), m03 = M(r0, 3);
    const v8 m10 = M(r1, 0), m11 = M(r1, 1), m12 = M(r1, 2), m13 = M(r1, 3);
    const v8 m20 = M(r2, 0), m21 = M(r2, 1), m22 = M(r2, 2), m23 = M(r2, 3);
    const v8 m30 = M(r3, 0), m31 = M(r3, 1), m32 = M(r3, 2), m33 = M(r3, 3);
#undef M
    for (uint32_t g = 0; g < groups; g++) {
        const uint32_t a = in + 128u * g, b = out + 128u * g;
        const v8 A = v8_ld2(ls, a, a + 64), B = v8_ld2(ls, a + 16, a + 80);
        const v8 C = v8_ld2(ls, a + 32, a + 96), D = v8_ld2(ls, a + 48, a + 112);
        const v8 t0 = _mm256_unpacklo_epi32(A, B), t1 = _mm256_unpacklo_epi32(C, D);
        const v8 t2 = _mm256_unpackhi_epi32(A, B), t3 = _mm256_unpackhi_epi32(C, D);
        const v8 X = _mm256_unpacklo_epi64(t0, t1), Y = _mm256_unpackhi_epi64(t0, t1);
        const v8 Z = _mm256_unpacklo_epi64(t2, t3), W = _mm256_unpackhi_epi64(t2, t3);
        const v8 x = v8_fma(W, m03, v8_fma(Z, m02, v8_fma(Y, m01, v8_fm(X, m00))));
        const v8 y = v8_fma(X, m10, v8_fma(W, m13, v8_fma(Z, m12, v8_fm(Y, m11))));
        const v8 z = v8_fma(Y, m21, v8_fma(X, m20, v8_fma(W, m23, v8_fm(Z, m22))));
        const v8 w = v8_fma(Z, m32, v8_fma(Y, m31, v8_fma(X, m30, v8_fm(W, m33))));
        const v8 a0 = _mm256_unpacklo_epi32(x, y), a1 = _mm256_unpacklo_epi32(z, w);
        const v8 a2 = _mm256_unpackhi_epi32(x, y), a3 = _mm256_unpackhi_epi32(z, w);
        v8_st2(ls, b, b + 64, _mm256_unpacklo_epi64(a0, a1));
        v8_st2(ls, b + 16, b + 80, _mm256_unpackhi_epi64(a0, a1));
        v8_st2(ls, b + 32, b + 96, _mm256_unpacklo_epi64(a2, a3));
        v8_st2(ls, b + 48, b + 112, _mm256_unpackhi_epi64(a2, a3));
    }
    ctx->pc = ctx->gpr[0]._u32[0];
    g_spu_trampoline_fn = 0;
    return 1;
}

/* ---- Normalize an array of vectors (SPU 0x2C10, loop 0x2D00) ----
 * r3 = array (in place), r4 = count. Per vector (x, y, z, w), as the SPU does:
 *   s = fma(z, z, fma(y, y, fm(x, x))),  r = fi(s, frsqest(s))
 *   out = (x*r, y*r, z*r, fm(w, 1.0))
 * over ceil(n/4) groups of four (the last padded with what follows), no
 * prologue stores. r2..r79 are dead after the return (corpus poison test). */
static inline void stagef_normalize8(uint8_t* ls, uint32_t a, v8 one)
{
    const v8 A = v8_ld2(ls, a, a + 64), B = v8_ld2(ls, a + 16, a + 80);
    const v8 C = v8_ld2(ls, a + 32, a + 96), D = v8_ld2(ls, a + 48, a + 112);
    const v8 t0 = _mm256_unpacklo_epi32(A, B), t1 = _mm256_unpacklo_epi32(C, D);
    const v8 t2 = _mm256_unpackhi_epi32(A, B), t3 = _mm256_unpackhi_epi32(C, D);
    const v8 X = _mm256_unpacklo_epi64(t0, t1), Y = _mm256_unpackhi_epi64(t0, t1);
    const v8 Z = _mm256_unpacklo_epi64(t2, t3), W = _mm256_unpackhi_epi64(t2, t3);
    const v8 sq = v8_fma(Z, Z, v8_fma(Y, Y, v8_fm(X, X)));
    const v8 r = v8_fi(sq, v8_frsqest(sq));
    const v8 x = v8_fm(X, r), y = v8_fm(Y, r), z = v8_fm(Z, r), w = v8_fm(W, one);
    const v8 a0 = _mm256_unpacklo_epi32(x, y), a1 = _mm256_unpacklo_epi32(z, w);
    const v8 a2 = _mm256_unpackhi_epi32(x, y), a3 = _mm256_unpackhi_epi32(z, w);
    v8_st2(ls, a, a + 64, _mm256_unpacklo_epi64(a0, a1));
    v8_st2(ls, a + 16, a + 80, _mm256_unpackhi_epi64(a0, a1));
    v8_st2(ls, a + 32, a + 96, _mm256_unpacklo_epi64(a2, a3));
    v8_st2(ls, a + 48, a + 112, _mm256_unpackhi_epi64(a2, a3));
}
static int buzz_stagef_native_normalize(spu_context* ctx)
{
    const uint32_t n = ctx->gpr[4]._u32[0];
    const uint32_t base = ctx->gpr[3]._u32[0] & 0x3FFF0;
    const uint32_t groups = (n + 3u) >> 2;
    if (!groups || groups > 0x1000) return 0;
    uint8_t* ls = ctx->ls;
    const v8 one = v8_set(0x3F800000u);
    uint32_t g = 0;
    for (; g + 1 < groups; g += 2) stagef_normalize8(ls, base + 64u * g, one);
    if (g < groups) {   /* last odd group: the same operations on four lanes */
        const uint32_t a = base + 64u * g;
        u128 v[4], o[4];
        for (int k = 0; k < 4; k++) v[k] = spu_ls_read128(ctx, a + 16u * k);
        u128 X, Y, Z, W;
        for (int k = 0; k < 4; k++) { X._u32[k] = v[k]._u32[0]; Y._u32[k] = v[k]._u32[1]; Z._u32[k] = v[k]._u32[2]; W._u32[k] = v[k]._u32[3]; }
        const u128 sq = spu_fma(Z, Z, spu_fma(Y, Y, spu_fm(X, X)));
        const u128 r = spu_fi(sq, spu_frsqest(sq));
        const u128 x = spu_fm(X, r), y = spu_fm(Y, r), z = spu_fm(Z, r), w = spu_fm(W, stagef_u(_mm_set1_epi32(0x3F800000)));
        for (int k = 0; k < 4; k++) {
            o[k]._u32[0] = x._u32[k]; o[k]._u32[1] = y._u32[k]; o[k]._u32[2] = z._u32[k]; o[k]._u32[3] = w._u32[k];
            spu_ls_write128(ctx, a + 16u * k, o[k]);
        }
    }
    ctx->pc = ctx->gpr[0]._u32[0];
    g_spu_trampoline_fn = 0;
    return 1;
}
