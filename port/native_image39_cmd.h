/* Not lifted fragments: spu_direct_hops.h redefines `return` for those. */
#pragma push_macro("return")
#undef return
/* Image 39 (EU 1.00 C0BC) native command writers.
 *
 * Image 39 builds per-object RSX command buffers in local store: method
 * headers and field values packed from byte records, appended at a cursor
 * word, with a few cached state words that suppress redundant commands.
 * The lifted code spends ~10 SPU operations per scalar field store (quadword
 * load, generate-control, shuffle, quadword store); these routines write the
 * same words directly.
 *
 * Contract (checked by the job corpus replay): the same LS bytes outside the
 * routine's own dead stack frame, the same cached state quadwords, the same
 * child calls with the same arguments, r1 and the callee-saved registers
 * unchanged at return. Volatile registers are dead at return (SPU ABI).
 * Scalar accesses reproduce the lifted addressing exactly: a quadword load
 * from (a & ~15) rotated by a separate byte amount, and a word store into
 * slot (pos & 12) of the quadword at (q & ~15).
 *
 * BUZZ_IMAGE39_CMD=0 runs the lifted routines (default 1).
 */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int buzz_i39_cmd_mode_value = -1;
static inline int buzz_i39_cmd_mode(void)
{
    int mode = __atomic_load_n(&buzz_i39_cmd_mode_value, __ATOMIC_RELAXED);
    if (mode < 0) {
        const char* e = getenv("BUZZ_IMAGE39_CMD");
        mode = e ? atoi(e) : 1;
        if (mode != 0) mode = 1;
        __atomic_store_n(&buzz_i39_cmd_mode_value, mode, __ATOMIC_RELAXED);
        fprintf(stderr, "[image39-cmd] mode=%d\n", mode);
    }
    return mode;
}

static inline int buzz_i39_cmd_admit(const spu_context* ctx)
{
#if SPU_LS_DEBUG_HOOKS
    (void)ctx;
    return 0;
#else
    return buzz_i39_cmd_mode() && ctx->image_id == 39 && ctx->ls == ctx->ls_store &&
           ctx->host_depth && !ctx->policy_mode && !ctx->resident_ovl && !ctx->irq_saved &&
           !ctx->irq_frame && !ctx->event_mask && !ctx->int_enable;
#endif
}

static inline uint8_t* i39_q(spu_context* ctx, uint32_t a) { return ctx->ls + (a & SPU_LS_MASK & ~15u); }
static inline uint32_t i39_be32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return __builtin_bswap32(v); }
/* rotqby(LD(qa), rot) preferred word. */
static inline uint32_t i39_rot32(spu_context* ctx, uint32_t qa, uint32_t rot)
{
    const uint8_t* q = i39_q(ctx, qa);
    rot &= 15;
    if (rot <= 12) return i39_be32(q + rot);
    uint32_t v = 0;
    for (uint32_t k = 0; k < 4; ++k) v = v << 8 | q[(rot + k) & 15];
    return v;
}
/* Low byte of rotqby(LD(qa), rot). */
static inline uint32_t i39_rotb(spu_context* ctx, uint32_t qa, uint32_t rot)
{
    return i39_q(ctx, qa)[(rot + 3) & 15];
}
/* ST(qa, shufb(v, LD(qa), cwd(pos))) */
static inline void i39_wr(spu_context* ctx, uint32_t qa, uint32_t pos, uint32_t v)
{
    const uint32_t b = __builtin_bswap32(v);
    memcpy(i39_q(ctx, qa) + (pos & 12), &b, 4);
}
static inline u128 i39_splat(uint32_t v) { u128 r; r._u32[0] = r._u32[1] = r._u32[2] = r._u32[3] = v; return r; }
/* Word k / big-endian halfword k of the quadword at qa. */
static inline uint32_t i39_w(spu_context* ctx, uint32_t qa, unsigned k) { return i39_be32(i39_q(ctx, qa) + 4 * k); }
static inline uint32_t i39_h(spu_context* ctx, uint32_t qa, unsigned k)
{
    const uint8_t* q = i39_q(ctx, qa) + 2 * k;
    return (uint32_t)q[0] << 8 | q[1];
}
/* ST(qa, shufb(v, LD(qa), cdd(pos))): doubleword (hi, lo) into slot pos & 8. */
static inline void i39_wr_dw(spu_context* ctx, uint32_t qa, uint32_t pos, uint32_t hi, uint32_t lo)
{
    uint8_t* q = i39_q(ctx, qa) + (pos & 8);
    const uint32_t a = __builtin_bswap32(hi), b = __builtin_bswap32(lo);
    memcpy(q, &a, 4); memcpy(q + 4, &b, 4);
}
/* ST(qa, shufb(v, LD(qa), chd(pos))): halfword into slot pos & 14. */
static inline void i39_wr_hw(spu_context* ctx, uint32_t qa, uint32_t pos, uint32_t v)
{
    uint8_t* q = i39_q(ctx, qa) + (pos & 14);
    q[0] = (uint8_t)(v >> 8); q[1] = (uint8_t)v;
}

/* The lifted brsl: link, host call, drain to the return point. */
static inline void i39_call(spu_context* ctx, void (*fn)(spu_context*), uint32_t ret)
{
    ctx->gpr[0] = spu_link(ret);
    const int32_t saved = (int32_t)ctx->image_id;
    ctx->host_depth++;
    fn(ctx);
    spu_drain_call(ctx, ret);
    ctx->host_depth--;
    spu_img_restore(ctx, saved);
}

static inline void i39_return(spu_context* ctx, u128 r0, u128 r1)
{
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->pc = r0._u32[0];
    if (ctx->host_depth == 0) g_spu_trampoline_fn = spu_indirect_branch;
}

/* 0x4C68 (leaf): q = (W(*a4) - W(*p3)) / W(p3+0x20); bump the counter quad at
 * p3+0x50 and store its word into slot (W(entry)+4) of entry q of the table
 * at W(p3+0x10). The entry quad is loaded before the counter store, as in the
 * original. A zero divisor halts: left to the lifted body. */
static void i39_4c68(spu_context* ctx, u128 r3, u128 r4)
{
    const uint32_t p3 = r3._u32[0];
    const uint32_t d = spu_ls_read128(ctx, p3 + 0x20)._u32[0];
    if (!d) {
        ctx->gpr[3] = r3; ctx->gpr[4] = r4;
        i39_call(ctx, buzz_job_c0bc_spu_func_00004C68, 0x4378);
        return;
    }
    const uint32_t n = spu_ls_read128(ctx, r4._u32[0])._u32[0] - spu_ls_read128(ctx, p3)._u32[0];
    const uint32_t base = spu_ls_read128(ctx, p3 + 0x10)._u32[0];
    const u128 count = spu_ai(spu_ls_read128(ctx, p3 + 0x50), 1);
    const uint32_t at = base + ((n / d) << 4);
    const u128 entry = spu_ls_read128(ctx, at);
    spu_ls_write128(ctx, p3 + 0x50, count);
    spu_ls_write128(ctx, at, spu_shufb(count, entry, spu_cwd(entry, 4)));
}

/* 0x3BA8 (leaf, no frame): for each flagged 8-byte entry, find its key in the
 * 32-byte records at p84 and emit method 0x141EFC with the record (24 bytes). */
static void i39_3ba8(spu_context* ctx, uint32_t a3, uint32_t p84, uint32_t a6)
{
    const uint32_t x = i39_rot32(ctx, a6 + 10, a6 + 23);
    if (!(x & 0xFFu)) return;
    const uint32_t bound = ((x - 1u) & 0xFFFFu) + 1u;
    const uint32_t T = a6 + (i39_rot32(ctx, a6 + 12, a6 + 26) & 0xFFFFu);
    const uint32_t H = i39_rot32(ctx, p84 + 2740, p84 + 2754) & 0xFFFFu;
    const uint32_t cur_at = a3 + 8;
    uint32_t i = 0;
    do {
        const uint32_t E = T + ((i & 0xFFFFu) << 3);
        if (i39_rotb(ctx, E + 7, E + 20) == 0xFFu) {
            const uint32_t K = i39_rot32(ctx, E, E);
            uint32_t R = 0;
            if (H) {
                uint32_t j = 0;
                do {
                    const uint32_t rec = p84 + ((j & 0xFFFFu) << 5);
                    j = (j + 1) & 0xFFFFu;
                    if (i39_rot32(ctx, rec + 16, rec) == 0 && i39_rot32(ctx, rec + 16, rec + 4) == K) { R = rec; break; }
                } while (j != H);
            }
            const u128 r76 = spu_ls_read128(ctx, R);
            const uint32_t C = i39_rot32(ctx, cur_at, cur_at);
            const uint32_t v4 = i39_rot32(ctx, E + 4, E + 18) & 0xFFFFu;
            i39_wr(ctx, C + 4, C + 4, v4);
            i39_wr(ctx, C + 8, C + 8, r76._u32[0]);
            i39_wr(ctx, C + 12, C + 12, r76._u32[1]);
            i39_wr(ctx, C + 16, C, r76._u32[2]);
            i39_wr(ctx, C, C, 0x00141EFCu);
            i39_wr(ctx, C + 20, C + 4, r76._u32[3]);
            i39_wr(ctx, cur_at, cur_at, C + 24);
        }
        ++i;
    } while (i != bound);
}

/* Descriptor request 0x3A10 at a reviewed return point: the native request
 * driver (native_image39_request.h, later in this translation unit), else
 * the lifted routine. The driver rejects before any mutation. Returns r3. */
static int buzz_image39_native_request(spu_context* ctx);
static inline u128 i39_request(spu_context* ctx, u128 desc, u128 key, u128 tag, u128 out, uint32_t ret)
{
    ctx->gpr[3] = desc; ctx->gpr[4] = key; ctx->gpr[5] = tag; ctx->gpr[6] = out;
    ctx->gpr[0] = spu_link(ret);
    if (!buzz_image39_native_request(ctx)) i39_call(ctx, buzz_job_c0bc_spu_func_00003A10, ret);
    return ctx->gpr[3];
}

/* 0x3470: two cache requests per key (descriptors at 0x60B0 / 0x60A0, tag 9),
 * returns the OR of their pending tag masks. */
static int buzz_i39_native_3470(spu_context* ctx)
{
    if (!buzz_i39_cmd_admit(ctx)) return 0;
    const u128 r0 = ctx->gpr[0], r1 = ctx->gpr[1];
    u128 out1 = ctx->gpr[3], out2 = ctx->gpr[4], count = ctx->gpr[5], keys = ctx->gpr[6];
    spu_ls_write128(ctx, r1._u32[0] + 0x10, r0);
    ctx->gpr[1] = spu_ai(r1, -112);
    u128 pending = spu_il(0);
    do {
        const u128 o1 = out1;
        count = spu_ai(count, -1);
        out1 = spu_ai(out1, 16);
        pending = spu_or(pending, i39_request(ctx, spu_ls_read128(ctx, 0x60B0), spu_ls_read128(ctx, keys._u32[0]),
                                              spu_il(9), o1, 0x34C4));
        const u128 key2 = spu_rotqbyi(spu_ls_read128(ctx, keys._u32[0]), 4);
        const u128 o2 = out2;
        out2 = spu_ai(out2, 16);
        keys = spu_ai(keys, 32);
        pending = spu_or(pending, i39_request(ctx, spu_ls_read128(ctx, 0x60A0), key2, spu_il(9), o2, 0x34F0));
    } while (count._u32[0] != 0);
    ctx->gpr[3] = pending;
    i39_return(ctx, r0, r1);
    return 1;
}

/* 0x3768: per 128-byte record, a tag-3 request from the 0x60C0 cache for its
 * key word, or a zero quad output when the key is 0. */
static int buzz_i39_native_3768(spu_context* ctx)
{
    if (!buzz_i39_cmd_admit(ctx)) return 0;
    const u128 r0 = ctx->gpr[0], r1 = ctx->gpr[1];
    u128 n = ctx->gpr[3], src = ctx->gpr[4], out = ctx->gpr[5];
    spu_ls_write128(ctx, r1._u32[0] + 0x10, r0);
    ctx->gpr[1] = spu_ai(r1, -128);
    u128 pending = spu_il(0);
    do {
        const uint32_t at = src._u32[0] + 4;
        const u128 key = spu_rotqbyi(spu_ls_read128(ctx, at), (int)(at & 15));
        const u128 o = out;
        n = spu_ai(n, -1);
        out = spu_ai(out, 16);
        src = spu_ai(src, 128);
        if (key._u32[0] != 0)
            pending = spu_or(pending, i39_request(ctx, spu_ls_read128(ctx, 0x60C0), key, spu_il(3), o, 0x37C0));
        else
            spu_ls_write128(ctx, o._u32[0], spu_il(0));
    } while (n._u32[0] != 0);
    ctx->gpr[3] = pending;
    i39_return(ctx, r0, r1);
    return 1;
}

/* 0x3838: per object (or the shared one at 0x6110), unless its slot byte is
 * 0xFF: two tag-3 requests (0x6070 / 0x6080 caches) keyed by its table entry,
 * then up to four 0x6090 requests for the nonzero words below O+2836. */
static int buzz_i39_native_3838(spu_context* ctx)
{
    if (!buzz_i39_cmd_admit(ctx)) return 0;
    const u128 r0 = ctx->gpr[0], r1 = ctx->gpr[1];
    u128 list = ctx->gpr[3], out1 = ctx->gpr[5], out2 = ctx->gpr[6];
    u128 base4 = spu_ai(ctx->gpr[7], 48);
    u128 shared = spu_ls_read128(ctx, 0x6110);
    u128 left = spu_selb(spu_il(1), ctx->gpr[4], spu_ceqi(shared, 0));
    spu_ls_write128(ctx, r1._u32[0] + 0x10, r0);
    ctx->gpr[1] = spu_ai(r1, -256);
    u128 pending = spu_il(0);
    for (;;) {
        const u128 obj = spu_selb(shared, spu_ls_read128(ctx, list._u32[0]), spu_ceqi(shared, 0));
        const uint32_t O = obj._u32[0];
        left = spu_ai(left, -1);
        const uint32_t slot = i39_rotb(ctx, O + 1198, O + 1211);
        if (slot != 0xFFu) {
            const u128 entry = spu_ls_read128(ctx, O + slot * 96u + 1216u);
            const u128 key2 = spu_rotqbyi(entry, 12);
            u128 got = i39_request(ctx, spu_ls_read128(ctx, 0x6070), spu_rotqbyi(entry, 8), spu_il(3), out1, 0x3950);
            got = spu_or(got, i39_request(ctx, spu_ls_read128(ctx, 0x6080), key2, spu_il(3), out2, 0x396C));
            pending = spu_or(pending, got);
            uint32_t at = O + 2836;
            u128 o = base4;
            for (int k = 0; k < 4; ++k) {
                at -= 4;
                const u128 key = spu_rotqbyi(spu_ls_read128(ctx, at), (int)(at & 15));
                const u128 here = o;
                o = spu_ai(o, -16);
                if (key._u32[0] != 0)
                    pending = spu_or(pending, i39_request(ctx, spu_ls_read128(ctx, 0x6090), key, spu_il(3), here, 0x39AC));
            }
        }
        base4 = spu_ai(base4, 64);
        if (left._u32[0] == 0) break;
        list = spu_ai(list, 16);
        shared = spu_ls_read128(ctx, 0x6110);
        out1 = spu_ai(out1, 16);
        out2 = spu_ai(out2, 16);
    }
    ctx->gpr[3] = pending;
    i39_return(ctx, r0, r1);
    return 1;
}

/* One list element of 0x35D8 / 0x3720: size word from the record, then the
 * (masked) doubleword at e rewritten. */
static inline void i39_list_element(spu_context* ctx, uint32_t e, uint32_t rec, u128 pattern, u128 mask)
{
    i39_wr(ctx, e + 4, e + 4, spu_ls_read128(ctx, rec + 0x10)._u32[0]);
    const u128 x = spu_selb(spu_rotqbyi(spu_ls_read128(ctx, e), (int)(e & 15)), pattern, mask);
    i39_wr_dw(ctx, e, e, x._u32[0], x._u32[1]);
}

/* 0x3530: clear the 128-byte list at a4, build one element per record (with
 * a tag-2 cache request each, or one shared request when 0x6100 is set), then
 * issue the list GET (lsa a3, list a4, a6 elements, tag 2). */
static int buzz_i39_native_3530(spu_context* ctx)
{
    if (!buzz_i39_cmd_admit(ctx)) return 0;
    const u128 r0 = ctx->gpr[0], r1 = ctx->gpr[1];
    const u128 a3q = ctx->gpr[3], a4q = ctx->gpr[4], a5q = ctx->gpr[5], a6q = ctx->gpr[6], a7q = ctx->gpr[7];
    const uint32_t a3 = a3q._u32[0], a4 = a4q._u32[0];
    spu_ls_write128(ctx, r1._u32[0] + 0x10, r0);
    ctx->gpr[1] = spu_ai(r1, -224);
    const u128 zero = spu_il(0);
    spu_ls_write128(ctx, a4 + 0x70, zero);
    for (uint32_t o = 0; o < 0x70; o += 0x10) spu_ls_write128(ctx, a4 + o, zero);
    const u128 key0 = spu_ls_read128(ctx, 0x6100);
    const u128 pattern = spu_andbi(spu_fsmbi(0x1010), -128);
    u128 pending;
    if (key0._u32[0] == 0) {
        const u128 mask = spu_ls_read128(ctx, 0x5DF0);
        u128 e = a4q, rec = a7q, n = a6q, out = a5q;
        pending = spu_il(0);
        do {
            const u128 o = out;
            n = spu_ai(n, -1);
            out = spu_ai(out, 16);
            i39_list_element(ctx, e._u32[0], rec._u32[0], pattern, mask);
            e = spu_ai(e, 8);
            const u128 key = spu_rotqbyi(spu_ls_read128(ctx, rec._u32[0] + 0x10), 4);
            rec = spu_ai(rec, 32);
            pending = spu_or(pending, i39_request(ctx, spu_ls_read128(ctx, 0x6060), key, spu_il(2), o, 0x3630));
        } while (n._u32[0] != 0);
    } else {
        pending = i39_request(ctx, spu_ls_read128(ctx, 0x6060), key0, spu_il(2), a5q, 0x36F8);
        spu_ls_write128(ctx, 0x6110, spu_ls_read128(ctx, a5q._u32[0]));
        const u128 mask = spu_ls_read128(ctx, 0x5DF0);
        u128 e = a4q, rec = a7q, n = a6q;
        do {
            n = spu_ai(n, -1);
            i39_list_element(ctx, e._u32[0], rec._u32[0], pattern, mask);
            rec = spu_ai(rec, 32);
            e = spu_ai(e, 8);
        } while (n._u32[0] != 0);
    }
    /* 0x3638 */
    const u128 size = spu_shli(a6q, 3);
    if (!((a3 & 15) == 0 && (a4 & 7) == 0 && size._u32[0] <= 16384)) {
        ctx->gpr[1] = spu_ai(r1, -224);
        ctx->pc = 0x3638;
        extern void spu_halt(spu_context*);
        fprintf(stderr, "[spu] HALT-ASSERT heqi pc=0x0366C img=%d\n", ctx->image_id);
        ctx->status = SPU_STATUS_STOPPED_BY_HALT;
        spu_halt(ctx);
        return 1;
    }
    spu_wrch(ctx, MFC_LSA, a3q);
    spu_wrch(ctx, MFC_EAH, spu_il(0));
    spu_wrch(ctx, MFC_EAL, a4q);
    spu_wrch(ctx, MFC_Size, size);
    spu_wrch(ctx, MFC_TagID, spu_il(2));
    spu_wrch(ctx, MFC_Cmd, spu_il(68));
    ctx->gpr[3] = pending;
    i39_return(ctx, r0, r1);
    return 1;
}

/* 0x0E10: DMA list/descriptor block at a3 from the records at a4/a5 and fixed
 * LS state; 0x0B48 (issues the transfer) keeps its lifted body. */
static int buzz_i39_native_0e10(spu_context* ctx)
{
    if (!buzz_i39_cmd_admit(ctx)) return 0;
    const u128 r0 = ctx->gpr[0], r1 = ctx->gpr[1];
    const u128 a3q = ctx->gpr[3], a4q = ctx->gpr[4], a5q = ctx->gpr[5];
    const uint32_t a3 = a3q._u32[0], a4 = a4q._u32[0], a5 = a5q._u32[0];
    spu_ls_write128(ctx, r1._u32[0] + 0x10, r0);
    ctx->gpr[1] = spu_ai(r1, -96);

    /* 0x0E10 */
    {
        const uint32_t h70_6 = i39_h(ctx, a4 + 0x70, 6), w70_0 = i39_w(ctx, a4 + 0x70, 0);
        i39_wr_dw(ctx, a3 + 56, a3 + 8, h70_6, w70_0);
        const uint32_t w10_0 = i39_w(ctx, a4 + 0x10, 0);
        const uint32_t h10_2 = i39_h(ctx, a4 + 0x10, 2), h10_3 = i39_h(ctx, a4 + 0x10, 3);
        i39_wr_dw(ctx, a3 + 0x40, a3, h10_2, w10_0);
        i39_wr_dw(ctx, a3 + 72, a3 + 8, h10_3, w10_0 + h10_2);
    }
    const uint32_t w40_0 = i39_w(ctx, a4 + 0x40, 0);
    uint32_t r30;
    if (w40_0 == 0) {                                  /* 0x13B8 */
        r30 = 0x10000000u;
        i39_wr_dw(ctx, a3 + 0x50, a3, 0, 0);
        i39_wr_dw(ctx, a3 + 88, a3 + 8, 0, 0);
        i39_wr_dw(ctx, a3 + 0x60, a3, 0, 0);
        i39_wr_dw(ctx, a3 + 104, a3 + 8, 0, 0);
    } else {
        r30 = 0x20000000u;
        const uint32_t h2 = i39_h(ctx, a4 + 0x30, 2), h3 = i39_h(ctx, a4 + 0x30, 3);
        const uint32_t h4 = i39_h(ctx, a4 + 0x30, 4), h5 = i39_h(ctx, a4 + 0x30, 5);
        i39_wr_dw(ctx, a3 + 0x50, a3, h4, h2);
        i39_wr_dw(ctx, a3 + 88, a3 + 8, h5, h3);
        const uint32_t h6 = i39_h(ctx, a4 + 0x30, 6), h7 = i39_h(ctx, a4 + 0x30, 7);
        i39_wr_dw(ctx, a3 + 0x60, a3, h6, w40_0);
        i39_wr_dw(ctx, a3 + 104, a3 + 8, h7, w40_0 + h6);
    }

    /* 0x0FB0 */
    {
        const uint32_t g0 = i39_h(ctx, a4 + 0x20, 0), g1 = i39_h(ctx, a4 + 0x20, 1), g2 = i39_h(ctx, a4 + 0x20, 2);
        const uint32_t w10_2 = i39_w(ctx, a4 + 0x10, 2);
        i39_wr_dw(ctx, a3 + 0x70, a3, g0, w10_2);
        i39_wr_dw(ctx, a3 + 120, a3 + 8, g1, g0 + w10_2);
        i39_wr_dw(ctx, a3 + 0x80, a3, g2, g0 + w10_2 + g1);
        const int m = i39_q(ctx, 0x60F0)[0] != 0;
        const uint32_t g3 = i39_h(ctx, a4 + 0x20, 3), g4 = i39_h(ctx, a4 + 0x20, 4), g5 = i39_h(ctx, a4 + 0x20, 5);
        const uint32_t w10_3 = i39_w(ctx, a4 + 0x10, 3);
        i39_wr_dw(ctx, a3 + 136, a3 + 8, m ? 0 : g3, w10_3);
        i39_wr_dw(ctx, a3 + 0x90, a3, m ? 0 : g4, g3 + w10_3);
        i39_wr_dw(ctx, a3 + 152, a3 + 8, m ? 0 : g5, g3 + w10_3 + g4);
        i39_wr_dw(ctx, a3 + 0xB0, a3, 0x10, i39_w(ctx, a5 + 0x10, 2));
        i39_wr_dw(ctx, a3 + 184, a3 + 8, i39_w(ctx, a4 + 0x50, 2), i39_w(ctx, a4 + 0x60, 0));
        i39_wr_dw(ctx, a3 + 0xC0, a3, i39_w(ctx, a4 + 0x50, 3), i39_w(ctx, a4 + 0x60, 1));
        i39_wr_dw(ctx, a3 + 200, a3 + 8, i39_h(ctx, a4 + 0x70, 4), i39_w(ctx, a4 + 0x60, 2));
        i39_wr_dw(ctx, a3 + 0xD0, a3, i39_h(ctx, a4 + 0x70, 5), i39_w(ctx, a4 + 0x60, 3));
        i39_wr_dw(ctx, a3 + 0xA0, a3, i39_w(ctx, 0x61A0, 0), i39_w(ctx, 0x61A0, 1));
        i39_wr(ctx, a3 + 220, a3 + 12, (uint32_t)i39_q(ctx, a4)[1] << 4);
        i39_wr(ctx, a3 + 236, a3 + 12, r30 | i39_w(ctx, 0x61D0, 0));
        i39_wr(ctx, a3 + 216, a3 + 8, i39_w(ctx, 0x61C0, 1));
        if (i39_w(ctx, a5 + 0x10, 3) & 4u) {
            ctx->gpr[3] = a3q; ctx->gpr[4] = a5q; ctx->gpr[5] = a4q;
            i39_call(ctx, buzz_job_c0bc_spu_func_00000B48, 0x12C8);
        }
    }

    /* 0x12C8 */
    {
        i39_wr_dw(ctx, a3 + 0xE0, a3, 0, i39_w(ctx, a4 + 0x40, 3));
        i39_wr_hw(ctx, a3 + 248, a3 + 8, i39_h(ctx, 0x6230, 0));
        const uint32_t p = i39_w(ctx, 0x6120, 0);
        i39_wr_dw(ctx, a3, a3, 0, i39_w(ctx, p + 0x60, 0));
        i39_wr_hw(ctx, a3 + 8, a3 + 8, i39_h(ctx, p + 0x60, 3));
        i39_wr_hw(ctx, a3 + 10, a3 + 10, 168);
        i39_wr(ctx, a3 + 0x10, a3, 1);
        i39_wr(ctx, a3 + 20, a3 + 4, i39_w(ctx, a4 + 0x40, 1) + 208u);
        i39_wr_hw(ctx, a3 + 30, a3 + 14, (i39_w(ctx, a4 + 0x40, 2) + 128u) & 0xFFFFu);
    }
    i39_return(ctx, r0, r1);
    return 1;
}

/* 0x4308: per-object state/constant command writer. Children 0x4C68 and
 * 0x3BA8 run natively; 0x3EF8 and 0x0150 keep their lifted bodies. */
static int buzz_i39_native_4308(spu_context* ctx)
{
    if (!buzz_i39_cmd_admit(ctx)) return 0;
    const u128 r0 = ctx->gpr[0], r1 = ctx->gpr[1];
    const u128 a3q = ctx->gpr[3], a4q = ctx->gpr[4], a6q = ctx->gpr[6], a8q = ctx->gpr[8], a9q = ctx->gpr[9];
    const uint32_t a3 = a3q._u32[0], a4 = a4q._u32[0], a5 = ctx->gpr[5]._u32[0];
    const uint32_t a6 = a6q._u32[0], a7 = ctx->gpr[7]._u32[0];
    const uint32_t sp = r1._u32[0];
    const uint32_t cur_at = a3 + 8;

    spu_ls_write128(ctx, sp + 0x10, r0);          /* link save in the caller's frame */
    ctx->gpr[1] = spu_ai(r1, -192);               /* this routine's frame, for the children */

    i39_4c68(ctx, spu_ls_read128(ctx, 0x6060), a4q);

    const u128 q5 = spu_ls_read128(ctx, a5);
    const u128 r84q = spu_ls_read128(ctx, a4);
    const uint32_t p84 = r84q._u32[0];
    const u128 q6190 = spu_ls_read128(ctx, 0x6190), q6180 = spu_ls_read128(ctx, 0x6180);
    const u128 q6170 = spu_ls_read128(ctx, 0x6170);
    const uint32_t b8 = i39_rotb(ctx, a5, 5);
    const u128 r23 = spu_rotqbyi(q5, 4);
    const uint32_t P = p84 + b8 * 96u + 1152u;
    const u128 r80 = spu_rotqbyi(spu_ls_read128(ctx, p84 + 2836u), (int)((p84 + 2836u) & 15));
    const u128 r24 = spu_rotqbyi(spu_ls_read128(ctx, P + 0x30), 12);

    uint32_t cur;
    if (r23._u32[0] != q6190._u32[0]) {
        const uint32_t w0 = i39_rotb(ctx, a7, a7 + 13) << 24 | i39_rotb(ctx, a7 + 1, a7 + 14) << 16 |
                            i39_rotb(ctx, a7 + 2, a7 + 15) << 8 | i39_rotb(ctx, a7 + 3, a7 + 16);
        const uint32_t C = i39_rot32(ctx, cur_at, cur_at);
        i39_wr(ctx, C, C, 0x000408E4u);
        i39_wr(ctx, C + 4, C + 4, (w0 & 0x1FFFFFFFu) | 1u);
        i39_wr(ctx, cur_at, cur_at, C + 8);
        i39_wr(ctx, C + 8, C + 8, 0x00041FF4u);
        const uint32_t w1 = i39_rotb(ctx, a7 + 4, a7 + 17) << 24 | i39_rotb(ctx, a7 + 5, a7 + 18) << 16 |
                            i39_rotb(ctx, a7 + 6, a7 + 19) << 8 | i39_rotb(ctx, a7 + 7, a7 + 20);
        i39_wr(ctx, C + 12, C + 12, w1);
        cur = C + 16;
        i39_wr(ctx, cur_at, cur_at, cur);
        uint32_t m5 = i39_rotb(ctx, a7 + 8, a7 + 21) << 8 | i39_rotb(ctx, a7 + 9, a7 + 22);
        uint32_t m6 = i39_rotb(ctx, a7 + 10, a7 + 23) << 8 | i39_rotb(ctx, a7 + 11, a7 + 24);
        uint32_t m7 = i39_rotb(ctx, a7 + 12, a7 + 25) << 8 | i39_rotb(ctx, a7 + 13, a7 + 26);
        if (m5) {
            uint32_t method = 2880;
            do {
                const uint32_t value = (m6 & 1u) | (m7 & 1u) << 4, bit = m5 & 1u;
                m7 >>= 1; m6 >>= 1; m5 >>= 1;
                const uint32_t header = method | 0x40000u;
                method += 4;
                if (bit) {
                    i39_wr(ctx, cur, cur, header);
                    i39_wr(ctx, cur + 4, cur + 4, value);
                    cur += 8;
                }
            } while (m5);
            i39_wr(ctx, cur_at, cur_at, cur);
        }
        const uint32_t b20 = i39_rotb(ctx, a7 + 20, a7 + 33);
        const uint32_t w2 = i39_rotb(ctx, a7 + 16, a7 + 13) << 24 | i39_rotb(ctx, a7 + 17, a7 + 30) << 16 |
                            i39_rotb(ctx, a7 + 18, a7 + 31) << 8 | i39_rotb(ctx, a7 + 19, a7 + 32);
        const uint32_t top = b20 > 1 ? b20 << 24 : 0x02000000u;
        i39_wr(ctx, cur, cur, 0x00041D60u);
        i39_wr(ctx, cur + 4, cur + 4, top | w2);
        cur += 8;
        i39_wr(ctx, cur_at, cur_at, cur);
        i39_wr(ctx, cur, cur, 0x000408E4u);
        i39_wr(ctx, cur + 4, cur + 4, r23._u32[0] | 1u);
        cur += 8;
        i39_wr(ctx, cur_at, cur_at, cur);
        spu_ls_write128(ctx, 0x6190, r23);
    } else {
        cur = i39_rot32(ctx, cur_at, cur_at);
    }

    /* 0x4790: three NOP words, align to 16, optional copied constant block. */
    {
        const uint32_t c0 = cur;
        i39_wr(ctx, c0, c0, 0);
        i39_wr(ctx, c0 + 4, c0 + 4, 0);
        i39_wr(ctx, c0 + 8, c0 + 8, 0);
        const uint32_t pad = (0u - c0) & 12u;
        cur = c0 + pad;
        i39_wr(ctx, cur_at, cur_at, cur);
        if (r24._u32[0] != q6180._u32[0]) {
            const uint32_t D = i39_rot32(ctx, a6 + 20, a6 + 20);
            const uint32_t srcq = a6 + D;
            const u128 r16 = spu_rotqbyi(spu_ls_read128(ctx, srcq), (int)(srcq & 15));
            i39_wr(ctx, c0 + pad, c0 + pad, 0);
            i39_wr(ctx, cur + 4, cur + 4, 0);
            i39_wr(ctx, cur + 8, cur + 8, 0);
            const uint32_t dst = cur + ((0u - cur) & 12u);
            const uint32_t tail = r16._u32[0] & 15u;
            const u128 mask = spu_cgt(i39_splat(tail), spu_ls_read128(ctx, 0x5F20));
            i39_wr(ctx, cur_at, cur_at, dst);
            const uint32_t src = srcq + 16, n = r16._u32[0] >> 4;
            u128 q = spu_ls_read128(ctx, src);
            uint32_t off = 0;
            for (uint32_t k = 0; k < n; ++k, off += 16) {
                const u128 next = spu_ls_read128(ctx, src + off + 16);
                spu_ls_write128(ctx, dst + off, q);
                q = next;
            }
            spu_ls_write128(ctx, dst + off, spu_selb(spu_ls_read128(ctx, dst + off), q, mask));
            const uint32_t c2 = i39_rot32(ctx, cur_at, cur_at);
            i39_wr(ctx, cur_at, cur_at, c2 + (r16._u32[0] & ~3u));
            spu_ls_write128(ctx, 0x6180, r24);
        }
    }

    /* 0x490C */
    if (r80._u32[0] != 0 && r80._u32[0] != q6170._u32[0]) {
        ctx->gpr[3] = a3q; ctx->gpr[4] = r84q; ctx->gpr[5] = a8q;
        i39_call(ctx, buzz_job_c0bc_spu_func_00003EF8, 0x4938);
        spu_ls_write128(ctx, 0x6170, r80);
    }

    /* 0x4940 / 0x4C34 / 0x4968 */
    {
        const u128 q6220 = spu_ls_read128(ctx, 0x6220);
        int emit = 0;
        u128 r3v = q6220, r9v = spu_il(0);
        if (a9q._u32[0] != 0) {
            const u128 r9q = spu_ls_read128(ctx, a9q._u32[0]);
            const u128 shifted = spu_shlqbyi(r9q, 4);
            if (shifted._u32[0] != spu_ls_read128(ctx, 0x76B0)._u32[0]) {
                spu_ls_write128(ctx, 0x76B0, shifted);
                r3v = r9q; r9v = spu_fsmbi(0xFFFF);
                spu_ls_write128(ctx, 0x6250, spu_il(2));
                emit = 1;
            }
        } else if (spu_ls_read128(ctx, 0x6250)._u32[0] != 1) {
            spu_ls_write128(ctx, 0x76B0, a9q);
            spu_ls_write128(ctx, 0x6250, spu_il(1));
            emit = 1;
        }
        if (emit) {
            const u128 r47 = spu_rotqbyi(spu_ls_read128(ctx, cur_at), (int)(cur_at & 15));
            const uint32_t c = r47._u32[0];
            i39_wr(ctx, c, c, 0);
            i39_wr(ctx, c + 4, c + 4, 0);
            i39_wr(ctx, c + 8, c + 8, 0);
            const u128 r46 = spu_a(r47, spu_andi(spu_sfi(r47, 0), 12));
            i39_wr(ctx, cur_at, cur_at, r46._u32[0]);
            ctx->gpr[3] = r3v; ctx->gpr[4] = q6220; ctx->gpr[5] = r46;
            ctx->gpr[6] = spu_il(70); ctx->gpr[7] = i39_splat(0x00541EFCu);
            ctx->gpr[8] = i39_splat(0x00441EFCu); ctx->gpr[9] = r9v;
            i39_call(ctx, buzz_job_c0bc_spu_func_00000150, 0x49F4);
            i39_wr(ctx, cur_at, cur_at, ctx->gpr[3]._u32[0]);
        }
    }

    /* 0x4A00 / 0x4A60: per-entry 44-byte records. */
    i39_3ba8(ctx, a3, p84, a6);
    {
        const uint32_t P48 = P + 48;
        uint32_t count = i39_rotb(ctx, P48 + 0x50, 11);
        if (count) {
            uint32_t C = i39_rot32(ctx, cur_at, cur_at), k = 0, off = 80;
            do {
                const uint32_t bA = i39_rotb(ctx, P48 + off, off + 13);
                const uint32_t bB = i39_rotb(ctx, P48 + off + 1, P48 + off + 14);
                const uint32_t r42 = p84 + (bB << 5) + 2816u;
                ++k;
                i39_wr(ctx, C, C, ((bA << 5) + 6656u) | 0x00200000u);
                i39_wr(ctx, C + 4, C + 4, i39_rot32(ctx, r42 + 32, r42));
                i39_wr(ctx, C + 8, C + 8, i39_rot32(ctx, r42 + 32, 4));
                i39_wr(ctx, C + 12, C + 12, i39_rot32(ctx, r42 + 32, 8));
                i39_wr(ctx, C + 16, C, i39_rot32(ctx, r42 + 32, 12));
                i39_wr(ctx, C + 20, C + 4, i39_rot32(ctx, r42 + 48, 0));
                i39_wr(ctx, C + 24, C + 8, i39_rot32(ctx, r42 + 48, 4));
                i39_wr(ctx, C + 28, C + 12, i39_rot32(ctx, r42 + 48, 8));
                i39_wr(ctx, C + 32, C, 0);
                const uint32_t bA2 = i39_rotb(ctx, P48 + off, off + 13);
                off += 2;
                i39_wr(ctx, C + 36, C + 4, ((bA2 << 2) + 6208u) | 0x40000u);
                i39_wr(ctx, C + 40, C + 8, i39_rot32(ctx, r42 + 48, 12));
                C += 44;
                i39_wr(ctx, cur_at, cur_at, C);
                count = i39_rotb(ctx, P48 + 0x50, 11);
            } while (count > k);
        }
    }

    /* 0x4BDC */
    spu_ls_write128(ctx, 0x6160, spu_shlqbyi(spu_ls_read128(ctx, a4), 4));
    i39_return(ctx, r0, r1);
    return 1;
}
#pragma pop_macro("return")
