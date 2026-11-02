/* Complete 7730/7770 delta reconstruction, including the warmup, lookahead
 * and final SPU ABI. Four vector lanes of words contain eight u16 chains.
 * No original setup, step or final iteration is called. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"

namespace buzz {
struct GeometryIndexDeltas {
    unsigned groups = 0;
    bool aliases = false;
};

inline bool read_geometry_index_deltas(const spu_context& c, GeometryIndexDeltas& out,
                                       const char** reason = nullptr)
{
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    if (c.ls != c.ls_store) return reject("foreign LS");
    const unsigned count = c.gpr[96]._u32[0];
    if (count > 65535) return reject("delta count");
    GeometryIndexDeltas p; p.groups = (count + 31) / 32;
    const uint64_t raw[] = {c.gpr[31]._u32[0], uint64_t(c.gpr[88]._u32[0]) + 16,
                           uint64_t(c.gpr[88]._u32[0]) + 32, uint64_t(c.gpr[88]._u32[0]) + 48};
    const unsigned last = p.groups ? (p.groups - 1) * 64 : 0;
    for (auto base : raw)
        if (base >= SPU_LS_SIZE || (base & ~15ull) < 0xe680 ||
            (base & ~15ull) + last + 16 > SPU_LS_SIZE) return reject("delta output span");
    // Each writer advances by 64 bytes. Overlapping bounding intervals do
    // not imply aliasing: the ordinary four interleaved writers are disjoint.
    for (unsigned i = 0; i < 4; ++i) for (unsigned j = i + 1; j < 4; ++j) {
        const unsigned a = unsigned(raw[i]) & ~15u, b = unsigned(raw[j]) & ~15u;
        const unsigned distance = a > b ? a - b : b - a;
        p.aliases |= !(distance & 63) && distance <= last;
    }
    out = p; return true;
}

inline void reconstruct_geometry_index_deltas(spu_context& ctx, const GeometryIndexDeltas& p,
                                               buzz_geometry_job_fn tail)
{
    auto& r = ctx.gpr;
    const u128 original_count = r[96], bias = r[10], scratch = r[88];
    const u128 bases[] = {r[31], spu_ai(scratch, 16), spu_ai(scratch, 32), spu_ai(scratch, 48)};
    u128 cached[4]{}, next[4], values[4], carry{};
    unsigned write_offset = 0;
    for (unsigned group = 0; group <= p.groups; ++group) {
        // All reads precede every store, including when bases alias or the
        // final extra group wraps LS. Cached values belong to the prior group.
        for (unsigned i = 0; i < 4; ++i)
            next[i] = spu_ls_read128(&ctx, bases[i]._u32[0] + group * 64);
        for (unsigned i = 0; i < 4; ++i) {
            carry = spu_ah(cached[i], carry);
            values[i] = carry;
        }
        for (unsigned i = 0; i < 4; ++i)
            spu_ls_write128(&ctx, bases[i]._u32[0] + write_offset, values[i]);
        for (unsigned i = 0; i < 4; ++i) cached[i] = spu_sfh(bias, next[i]);
        if (group) write_offset += 64;
    }
    // Materialize every changed temporary once. The counter's nonpreferred
    // words derive from the caller's entire r96, not the preferred count.
    r[84] = spu_ai(original_count, 31);
    r[45] = spu_rotmi(r[84], -5);
    r[12] = spu_ai(spu_ai(r[45], 1), -int(p.groups + 1));
    r[17] = bases[1]; r[16] = bases[2]; r[15] = bases[3];
    r[9] = spu_il(64);
    r[14] = r[74] = spu_il(int((p.groups + 1) * 64));
    r[13] = r[99] = spu_il(int(p.groups * 64));
    r[96] = spu_il(int(p.groups ? (p.groups - 1) * 64 : 0));
    r[75] = values[0]; r[28] = values[1]; r[47] = values[2]; r[11] = values[3];
    r[49] = next[0]; r[48] = next[1]; r[22] = next[2]; r[25] = next[3];
    r[19] = cached[0]; r[18] = cached[1]; r[20] = cached[2]; r[21] = cached[3];
    ctx.pc = 0x77f8; g_spu_trampoline_fn = tail;
}
}
