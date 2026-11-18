/* Native image 38 (buzz_job_39d9): the serial chain's small gather jobs.
 *
 * About 240 of these run back to back on the chain in every answer-reveal
 * frame and the frame time follows their cost 1:1 (BUZZ_CAUSAL_DELAY), so the
 * lifted program's fixed per-element overhead is on the critical path.
 *
 * This is the same program written against the host: identical LS reads and
 * writes of everything the job's DMA/PUT can observe, and the identical
 * channel sequence (list GET per group, tag waits, chunked PUT), issued
 * through the same spu_wrch/spu_rdch so the MFC model, coherency, tracing and
 * job capture see exactly what the lifted code produces. Every condition
 * under which the original halts, stops or loops degenerately is checked
 * before the first channel write; such jobs run the retained lifted entry.
 *
 * BUZZ_IMAGE38=0 selects the lifted program for every job.
 * The build pins the lifted source this mirrors (cmake/BuzzNativeImage38.cmake). */
#include "spu_context.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
void spu_wrch(spu_context* ctx, uint32_t channel, u128 value);
u128 spu_rdch(spu_context* ctx, uint32_t channel);
void spu_indirect_branch(spu_context* ctx);
void buzz_job_39d9_spu_func_00000010_retained(spu_context* ctx);
void buzz_job_39d9_spu_func_00000010(spu_context* ctx);
}

namespace {
constexpr uint32_t kMask = SPU_LS_MASK;

struct Ls {
    uint8_t* ls;
    const uint8_t* quad(uint32_t a) const { return ls + (a & kMask & ~15u); }
    uint8_t* quad_w(uint32_t a) const { return ls + (a & kMask & ~15u); }
    // word 0 of rotqby(read128(read), rot)
    uint32_t wrot(uint32_t read, uint32_t rot) const {
        const uint8_t* q = quad(read);
        rot &= 15;
        return uint32_t(q[rot]) << 24 | uint32_t(q[(rot + 1) & 15]) << 16 |
               uint32_t(q[(rot + 2) & 15]) << 8 | q[(rot + 3) & 15];
    }
    uint32_t w(uint32_t a) const { return wrot(a, a); }
    void put_word(uint32_t a, uint32_t v) const {   // shufb/cwd word insert
        uint8_t* q = quad_w(a) + (a & 12);
        q[0] = uint8_t(v >> 24); q[1] = uint8_t(v >> 16); q[2] = uint8_t(v >> 8); q[3] = uint8_t(v);
    }
};

inline u128 word0(uint32_t v) { u128 r{}; r._u32[0] = v; return r; }
inline void wr(spu_context* ctx, uint32_t ch, uint32_t v) { spu_wrch(ctx, ch, word0(v)); }

int mode()
{
    static int m = -1;
    if (m < 0) {
        const char* e = getenv("BUZZ_IMAGE38");
        m = e && *e ? atoi(e) : 1;
        if (m != 0) m = 1;
        fprintf(stderr, "[image38] mode=%d (0 lifted, 1 native)\n", m);
    }
    return m;
}
std::atomic<unsigned long> native_jobs{0}, fallback_jobs{0};

struct Group {
    uint32_t lsa, list, buf;
};

bool run_native(spu_context* ctx)
{
    const Ls L{ctx->ls};
    const uint32_t jctx = ctx->gpr[3]._u32[0], desc = ctx->gpr[4]._u32[0];
    if (!jctx || !desc || (jctx & 15) || (desc & 15)) return false;
    const uint32_t sp = ctx->gpr[1]._u32[0] - 3616u;

    // ---- parameters (0x3E0 / 0x498) ---------------------------------------
    const uint32_t count = L.w(desc + 0xF4);
    if (count > 4) return false;
    if (L.w(desc + 0xF8) || L.w(desc + 0xFC)) return false;
    const unsigned groups = count ? count : 1;   // do-while loops
    if (((sp + 112) & 15) != 0) return false;

    // Dry run of everything control flow depends on, on the staged LS. The
    // pointer table is computed exactly as the job writes it.
    const uint8_t* q10 = L.quad(desc + 10);
    const uint32_t rot10 = (desc + 24) & 15;
    const uint32_t n_dma = ((uint32_t(q10[(rot10 + 2) & 15]) << 8 | q10[(rot10 + 3) & 15]) >> 3) & 0x1FFF;
    if (!n_dma) return false;   // the job skips its table; keep the lifted path
    const uint32_t size_mask = L.w(0x9C4);
    uint32_t table[8];
    for (unsigned i = 0; i < 8; ++i) table[i] = L.w(sp + 80 + 4 * i);   // stale stack beyond n_dma
    uint32_t entries[8];
    {
        uint32_t io = L.w(jctx);
        for (uint32_t i = 0; i < n_dma; ++i) {
            const uint32_t at = desc + 8 * i;
            const uint8_t* q = L.quad(at + 0x30);
            const uint32_t rot = at & 15;
            const uint32_t size = uint32_t(q[rot]) << 24 | uint32_t(q[(rot + 1) & 15]) << 16 |
                                  uint32_t(q[(rot + 2) & 15]) << 8 | q[(rot + 3) & 15];
            const uint32_t eal = uint32_t(q[(rot + 4) & 15]) << 24 | uint32_t(q[(rot + 5) & 15]) << 16 |
                                 uint32_t(q[(rot + 6) & 15]) << 8 | q[(rot + 7) & 15];
            const uint32_t entry = (eal & 15) | io;
            if (i < 8) entries[i] = entry;
            // A table entry the memset below clears, or one overlapping the
            // quads we read back, would need the exact interleaving: keep the
            // simple shape only.
            if (i >= 8) return false;
            io += ((size & size_mask) + 15) & ~15u;
        }
        for (uint32_t i = 0; i < n_dma; ++i) table[i] = entries[i];
    }
    const uint32_t g1_0 = L.w(desc + 0xB4);
    const uint32_t scratch = L.w(jctx + 0x20);
    const uint32_t end = scratch + ((L.w(desc + 0x1C) & 0xFFFF) << 4);
    const uint32_t outbase = L.w(jctx + 0x1C);
    Group grp[4];
    {
        uint32_t cur = scratch;
        for (unsigned g = 0; g < groups; ++g) {
            const uint32_t base = table[4] + g * 2816u;
            const uint32_t target = L.w(base + 772);
            const uint32_t list = base + 768;
            const uint32_t lsa = cur + ((target - cur) & 127);
            if ((list & 7) || (lsa & 15)) return false;
            cur = lsa + 3264;
            if (cur > end) return false;
            grp[g].lsa = lsa;
            grp[g].list = table[4 + g] + 768;
            grp[g].buf = sp + 112 + 816 * g;
        }
    }
    // Per-group element counts come from the staged inputs (P = table[g]).
    for (unsigned g = 0; g < groups; ++g) {
        const uint32_t p = table[g];
        const uint32_t r7 = p + L.wrot(p + 20, p + 20);
        if ((L.wrot(r7 + 6, r7 + 20) & 0xFFFF) == 0) return false;
    }
    const uint32_t put_tag = L.wrot(jctx + 36, jctx + 36);
    const uint32_t put_ea = L.w(desc + 0xDC), put_size = L.w(desc + 0xD4);
    if ((outbase & 15) || (put_ea & 15) || put_tag > 31) return false;

    // ---- execution (side effects from here on) ---------------------------
    for (uint32_t i = 0; i < n_dma; ++i) L.put_word(sp + 80 + 4 * i, table[i]);
    {   // 0x3E0: the job stores its descriptor pointer register at 0xA00
        uint8_t* q = L.quad_w(0xA00);
        for (unsigned i = 0; i < 4; ++i) {
            const uint32_t v = ctx->gpr[4]._u32[i];
            q[4 * i] = uint8_t(v >> 24); q[4 * i + 1] = uint8_t(v >> 16);
            q[4 * i + 2] = uint8_t(v >> 8); q[4 * i + 3] = uint8_t(v);
        }
    }
    // Parameter words the job parks on its stack (sp+0x20 / sp+0x30).
    for (unsigned g = 0; g < 4; ++g) {
        L.put_word(sp + 0x30 + 4 * g, L.w(desc + 0xB0 + 8 * g));
        L.put_word(sp + 0x20 + 4 * g, L.w(desc + 0xB4 + 8 * g));
    }
    std::memset(L.quad_w(sp + 112), 0, 3264);
    for (unsigned g = 0; g < groups; ++g) {
        const uint32_t base = table[4] + g * 2816u;
        wr(ctx, MFC_LSA, grp[g].lsa);
        wr(ctx, MFC_EAH, 0);
        wr(ctx, MFC_EAL, base + 768);
        wr(ctx, MFC_Size, 408);
        wr(ctx, MFC_TagID, g + 2);
        wr(ctx, MFC_Cmd, 0x44);
        L.put_word(sp + 64 + 4 * g, grp[g].lsa);
    }
    // 0x5F8 / func 0x20: running sums of the list words, 51 quads per group.
    for (unsigned g = 0; g < groups; ++g) {
        const uint32_t lst = grp[g].list;
        uint32_t acc[4];
        {
            const uint32_t t = sp + 64 + 4 * g;
            for (unsigned i = 0; i < 4; ++i) acc[i] = L.wrot(t, (t & 15) + 4 * i);
        }
        const uint8_t* cur = L.quad(lst);
        uint32_t off = 8, shift = 8;
        alignas(16) uint8_t pad[32] = {};
        for (unsigned k = 0; k < 51; ++k) {
            uint32_t add[4];
            std::memcpy(pad, cur, 16);   // shlqby: bytes past the quad read as zero
            for (unsigned i = 0; i < 4; ++i) {
                uint32_t v;
                std::memcpy(&v, pad + shift + 4 * i, 4);
                add[i] = __builtin_bswap32(v);
            }
            cur = L.quad(lst + off);
            uint8_t* o = L.quad_w(grp[g].buf + 16 * k);
            for (unsigned i = 0; i < 4; ++i) {
                o[4 * i] = uint8_t(acc[i] >> 24); o[4 * i + 1] = uint8_t(acc[i] >> 16);
                o[4 * i + 2] = uint8_t(acc[i] >> 8); o[4 * i + 3] = uint8_t(acc[i]);
                acc[i] += add[i];
            }
            off += 8;
            shift = off & 15;
        }
    }
    // 0x670 / 0x1E0 / 0x138: per group, wait for its list and gather.
    for (unsigned g = 0; g < groups; ++g) {
        wr(ctx, MFC_WrTagMask, 1u << (g + 2));
        wr(ctx, MFC_WrTagUpdate, 2);
        (void)spu_rdch(ctx, MFC_RdTagStat);
        const uint32_t out = outbase + (L.w(desc + 0xB4 + 8 * g) - g1_0);
        const uint32_t a = L.w(desc + 0xB0 + 8 * g);
        const uint32_t p = table[g], qb = table[g + 4];
        const uint32_t r7 = p + L.wrot(p + 20, p + 20);
        const uint32_t entries_at = p + (L.wrot(p + 12, p + 26) & 0xFFFF);
        const uint32_t stream = r7 + L.wrot(r7 + 8, r7 + 8);
        const uint32_t n = L.wrot(r7 + 6, r7 + 20) & 0xFFFF;
        const uint32_t direct = r7 + L.wrot(r7 + 12, r7 + 12);
        const uint32_t codes = qb + (a & 0xFFFF) * 96u + 1232u;
        const uint32_t buf = grp[g].buf;
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t s = stream + 2 * k;
            const uint8_t* sq = L.quad(s);
            const uint32_t so = s & 15;
            const uint32_t hw = uint32_t(sq[so]) << 8 | (so < 15 ? sq[so + 1] : 0u);
            const uint32_t idx = hw & 0x0FFF;
            const uint8_t* src;
            uint8_t* dst = L.quad_w(out + 16 * k);
            if (hw & 0x8000) {
                std::memcpy(dst, L.quad(direct + 16 * idx), 16);
                continue;
            }
            const uint32_t e8 = idx * 8;
            const uint32_t b4 = L.quad(entries_at + e8)[(e8 & 15) + 4];
            if (b4 == 255) {
                const uint32_t c = L.quad(codes + idx)[idx & 15];
                src = L.quad(qb + 32 * c);
            } else {
                const uint8_t* bq = L.quad(buf + 16 * b4);
                const uint32_t rec = uint32_t(bq[0]) << 24 | uint32_t(bq[1]) << 16 | uint32_t(bq[2]) << 8 | bq[3];
                src = L.quad(rec);
            }
            uint8_t tmp[16];
            for (unsigned i = 0; i < 16; i += 4) {
                tmp[i] = src[i + 2]; tmp[i + 1] = src[i + 3];
                tmp[i + 2] = src[i]; tmp[i + 3] = src[i + 1];
            }
            std::memcpy(dst, tmp, 16);
        }
    }
    // 0x780: PUT the output, chunked as the job does.
    {
        uint32_t lsa = outbase, ea_lo = put_ea, ea_hi = 0, rem = put_size;
        while (rem) {
            const uint32_t chunk = rem > 16383 ? 16384u : rem > 15 ? (rem & ~15u)
                                 : rem > 7 ? 8u : rem > 3 ? 4u : rem > 1 ? 2u : 1u;
            wr(ctx, MFC_LSA, lsa);
            wr(ctx, MFC_EAH, ea_hi);
            wr(ctx, MFC_EAL, ea_lo);
            wr(ctx, MFC_Size, chunk);
            wr(ctx, MFC_TagID, put_tag);
            wr(ctx, MFC_Cmd, 32);
            rem -= chunk;
            lsa += chunk;
            const uint64_t ea = (uint64_t(ea_hi) << 32 | ea_lo) + chunk;
            ea_hi = uint32_t(ea >> 32); ea_lo = uint32_t(ea);
        }
    }
    return true;
}
} // namespace

extern "C" void buzz_job_39d9_spu_func_00000010(spu_context* ctx)
{
    if (mode() && run_native(ctx)) {
        const unsigned long n = ++native_jobs;
        if (n == 1 || n % 100000 == 0)
            fprintf(stderr, "[image38] native %lu, lifted fallback %lu\n", n, fallback_jobs.load());
        // SPU_RET: back to the job manager's return address in r0.
        ctx->pc = ctx->gpr[0]._u32[0];
        if (ctx->host_depth == 0) g_spu_trampoline_fn = spu_indirect_branch;
        return;
    }
    if (mode()) ++fallback_jobs;
    buzz_job_39d9_spu_func_00000010_retained(ctx);
}
