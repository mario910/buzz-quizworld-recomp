/* Additional weighted blend streams for the host geometry pipeline.
 *
 * The lifted job applies these morph-target streams (0x0BF8/0x0C08) after
 * decoding its base attributes into LS float4 tables and before skinning.
 * The host pipeline keeps attributes in SoA and never builds those tables, so
 * blend jobs used to take the whole lifted job (~4x the native cost).
 *
 * Here the host pipeline reproduces the exact LS preconditions of that call:
 * the geometry context as it stands at 0x0C08 (native initialization plus the
 * base decode's slot routing, routing pointers and workspace cursor), the
 * float4 tables holding the captured attributes, and the call ABI. It then runs
 * the original routine, whose DMA orchestration, run-list expansion, decode,
 * weighted scatter and normal renormalization are the reviewed lift and its
 * existing native regions, and reads the tables back into SoA. Its GETs are
 * the job's first transfers, as in the original order.
 *
 * Everything that can reject the job is decided after this runs, still before
 * any guest-visible write. A rejection restores the complete LS and register
 * state, so the lifted job then starts from its untouched entry state (it only
 * repeats the blend GETs, which are reads).
 */
#pragma once
#include "native_geometry_context.h"
#include "native_geometry_commands.h"
#include "native_geometry_host_input_job.h"
#include "native_geometry_job_setup.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

extern "C" __attribute__((weak)) void buzz_job_6eba_spu_func_00000BF8(spu_context*);
#include "native_geometry_host_blend_native.h"

namespace buzz {

inline int geometry_host_blend_enabled()
{
    static const int enabled = [] {
        const char* e = std::getenv("BUZZ_GEOMETRY_HOST_BLEND");
        const int on = !(e && e[0] == '0');
        std::fprintf(stderr, "[geometry-job/host-blend] enabled=%d\n", on);
        return on;
    }();
    return enabled;
}

/* Entry state of everything the blend call changes: registers plus the LS
 * ranges it writes (context, stack frame, list buffers, work buffers). The
 * float4 tables live in scratch, which the host pipeline never reads.
 * BUZZ_GEOMETRY_HOST_BLEND_DIFF=1 lists every changed range for review. */
struct GeometryHostBlendSnapshot {
    u128 gpr[128];
    uint32_t pc = 0, int_enable = 0;
    int32_t image = 0, depth = 0;
    std::unique_ptr<uint8_t[]> ls;
    struct Range { uint32_t begin = 0, end = 0; } range[8];
    unsigned ranges = 0;
    bool full = false;
    void take_registers(const spu_context& c)
    {
        memcpy(gpr, c.gpr, sizeof gpr);
        pc = c.pc; int_enable = c.int_enable; image = c.image_id; depth = c.host_depth;
        ranges = 0; full = false;
        if (!ls) ls.reset(new uint8_t[SPU_LS_SIZE]);
    }
    void take_full(const spu_context& c) { memcpy(ls.get(), c.ls, SPU_LS_SIZE); full = true; }
    void take(const spu_context& c, uint32_t begin, uint32_t end)
    {
        if (begin > end || end > SPU_LS_SIZE) return;
        if (ranges == 8) std::abort();   // a range that is not saved cannot be restored
        range[ranges++] = {begin, end};
        if (!full) memcpy(ls.get() + begin, c.ls + begin, end - begin);
    }
    void restore(spu_context& c) const
    {
        memcpy(c.gpr, gpr, sizeof gpr); c.pc = pc; c.int_enable = int_enable;
        c.image_id = image; c.host_depth = depth;
        for (unsigned i = 0; i < ranges; ++i)
            memcpy(c.ls + range[i].begin, ls.get() + range[i].begin, range[i].end - range[i].begin);
    }
};
inline GeometryHostBlendSnapshot& geometry_host_blend_snapshot()
{
    static thread_local GeometryHostBlendSnapshot snapshot;
    return snapshot;
}

inline unsigned geometry_host_blend_count(const GeometryJobEntry& e)
{
    return geometry_job_be32(e.user.data() + 8) & 0xffff;
}

/* Optional diagnostic: BUZZ_GEOMETRY_HOST_BLEND_CHECK=<file> names a replay
 * state dump taken at 0x0C08 (BUZZ_REPLAY_DUMP_PC=C08) for the single job
 * being replayed. Prints every difference in the constructed preconditions. */
inline void geometry_host_blend_check(const spu_context& c, uint32_t context,
    const GeometryContextState& state, const GeometryHostInputRecipe& recipe, const GeometryJobEntry& e)
{
    const char* path = std::getenv("BUZZ_GEOMETRY_HOST_BLEND_CHECK");
    if (!path || !*path) return;
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "[host-blend-check] cannot open %s\n", path); return; }
    std::unique_ptr<uint8_t[]> dump(new uint8_t[sizeof c.gpr + SPU_LS_SIZE]);
    const size_t got = std::fread(dump.get(), 1, sizeof c.gpr + SPU_LS_SIZE, f); std::fclose(f);
    if (got != sizeof c.gpr + SPU_LS_SIZE) { std::fprintf(stderr, "[host-blend-check] short dump\n"); return; }
    const u128* g = reinterpret_cast<const u128*>(dump.get());
    const uint8_t* ls = dump.get() + sizeof c.gpr;
    for (unsigned r : {0u, 1u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 126u})
        if (g[r]._u32[0] != c.gpr[r]._u32[0])
            std::fprintf(stderr, "[host-blend-check] r%u lifted %08X native %08X\n", r, g[r]._u32[0], c.gpr[r]._u32[0]);
    for (unsigned o = 0; o < 0x190; o += 4)
        if (memcmp(ls + context + o, c.ls + context + o, 4))
            std::fprintf(stderr, "[host-blend-check] ctx+%03X lifted %08X native %08X\n", o,
                geometry_job_be32(ls + context + o), geometry_job_be32(c.ls + context + o));
    for (unsigned field = 0; field < recipe.fields; ++field) {
        unsigned diffs = 0, first = ~0u;
        for (unsigned v = 0; v < recipe.vertices; ++v)
            if (memcmp(ls + state.tables[field] + v * 16, c.ls + state.tables[field] + v * 16, 16)) {
                if (first == ~0u) first = v;
                ++diffs;
            }
        if (diffs) {
            const uint8_t* a = ls + state.tables[field] + first * 16;
            const uint8_t* b = c.ls + state.tables[field] + first * 16;
            std::fprintf(stderr, "[host-blend-check] field %u (semantic %u) %u vertices differ, first %u: "
                "lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n", field, recipe.field[field].semantic,
                diffs, first, geometry_job_be32(a), geometry_job_be32(a + 4), geometry_job_be32(a + 8),
                geometry_job_be32(a + 12), geometry_job_be32(b), geometry_job_be32(b + 4),
                geometry_job_be32(b + 8), geometry_job_be32(b + 12));
        }
    }
    std::fprintf(stderr, "[host-blend-check] entry io=%05X+%X out=%05X+%X scratch=%05X+%X ws=%05X+%X stack_top=%05X frame=%05X input_end=%05X idx=%u verts=%u\n",
        e.io_lsa, e.io_bytes, e.output_lsa, e.output_bytes, e.scratch_lsa, e.scratch_bytes, e.workspace_lsa,
        e.workspace_bytes, e.stack_top, e.frame_lsa, e.input_end, e.index_count, e.vertices);
    for (unsigned i = 0; i < e.input.size(); ++i)
        if (e.input[i].size || e.input[i].lsa)
            std::fprintf(stderr, "[host-blend-check] input[%u] lsa=%05X size=%X end=%05X\n", i, e.input[i].lsa, e.input[i].size,
                e.input[i].lsa + ((e.input[i].size + 15) & ~15u));
    std::fprintf(stderr, "[host-blend-check] compared %s\n", path);
}

/* Structural admission only: no LS, register or DMA effects. */
inline bool admit_geometry_host_blend(const GeometryJobSetup& setup, const GeometryHostInputRecipe& recipe,
    const char** reason)
{
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    const GeometryJobEntry& e = setup.entry;
    const unsigned count = geometry_host_blend_count(e);
    if (!geometry_host_blend_enabled() || !buzz_job_6eba_spu_func_00000BF8)
        return reject("host job additional blend streams");
    if (count > 128) return reject("host blend stream count");
    // The blend decodes into the extra table the configuration reserves.
    if (!(e.config[0] & 0x80) || recipe.fields >= e.uniform_tables() || recipe.fields > 4)
        return reject("host blend table layout");
    return true;
}

/* SoA (four component vectors, lane = vertex) <-> BE float4 quads, eight
 * vertices at a time: the same bytes as the per-lane byte-swapped copies. */
inline void geometry_blend_soa_to_quads(const GeometryHostSoA8& block, uint8_t* table, unsigned count)
{
#if defined(__aarch64__)
    uint32x4x4_t low, high;
    for (unsigned k = 0; k < 4; ++k) {
        const v8 be = v8_bswap(block.component[k]);
        low.val[k] = be.lo;
        high.val[k] = be.hi;
    }
    alignas(16) uint32_t tail[32];
    auto* output = count == 8 ? reinterpret_cast<uint32_t*>(table) : tail;
    vst4q_u32(output, low);
    vst4q_u32(output + 16, high);
    if (count < 8) memcpy(table, tail, count * 16);
#else
    const __m256i swap = _mm256_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12,
                                          3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12);
    const __m256i t0 = _mm256_unpacklo_epi32(block.component[0], block.component[1]);
    const __m256i t1 = _mm256_unpackhi_epi32(block.component[0], block.component[1]);
    const __m256i t2 = _mm256_unpacklo_epi32(block.component[2], block.component[3]);
    const __m256i t3 = _mm256_unpackhi_epi32(block.component[2], block.component[3]);
    // Per 128-bit half: vertices {0,1,2,3} low, {4,5,6,7} high.
    const __m256i v01 = _mm256_shuffle_epi8(_mm256_unpacklo_epi64(t0, t2), swap); // v0 | v4
    const __m256i v11 = _mm256_shuffle_epi8(_mm256_unpackhi_epi64(t0, t2), swap); // v1 | v5
    const __m256i v21 = _mm256_shuffle_epi8(_mm256_unpacklo_epi64(t1, t3), swap); // v2 | v6
    const __m256i v31 = _mm256_shuffle_epi8(_mm256_unpackhi_epi64(t1, t3), swap); // v3 | v7
    alignas(32) uint8_t quads[8][16];
    _mm256_store_si256(reinterpret_cast<__m256i*>(quads[0]), _mm256_permute2x128_si256(v01, v11, 0x20));
    _mm256_store_si256(reinterpret_cast<__m256i*>(quads[2]), _mm256_permute2x128_si256(v21, v31, 0x20));
    _mm256_store_si256(reinterpret_cast<__m256i*>(quads[4]), _mm256_permute2x128_si256(v01, v11, 0x31));
    _mm256_store_si256(reinterpret_cast<__m256i*>(quads[6]), _mm256_permute2x128_si256(v21, v31, 0x31));
    memcpy(table, quads, count * 16);
#endif
}
inline void geometry_blend_quads_to_soa(const uint8_t* table, GeometryHostSoA8& block, unsigned count)
{
#if defined(__aarch64__)
    alignas(16) uint32_t tail[32];
    if (count < 8) {
        // Preserve the inactive lanes without reading beyond the guest table.
        geometry_blend_soa_to_quads(block, reinterpret_cast<uint8_t*>(tail), 8);
        memcpy(tail, table, count * 16);
        table = reinterpret_cast<const uint8_t*>(tail);
    }
    const auto* words = reinterpret_cast<const uint32_t*>(table);
    const auto low = vld4q_u32(words);
    const auto high = vld4q_u32(words + 16);
    for (unsigned k = 0; k < 4; ++k)
        block.component[k] = v8_bswap({low.val[k], high.val[k]});
#else
    const __m256i swap = _mm256_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12,
                                          3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12);
    alignas(32) uint8_t quads[8][16];
    if (count < 8) {
        // Lanes past the copied vertices keep their SoA values.
        geometry_blend_soa_to_quads(block, quads[0], 8);   // stored (BE) order
    }
    memcpy(quads, table, count * 16);
    const __m256i q04 = _mm256_shuffle_epi8(_mm256_permute2x128_si256(
        _mm256_load_si256(reinterpret_cast<const __m256i*>(quads[0])), _mm256_load_si256(reinterpret_cast<const __m256i*>(quads[4])), 0x20), swap); // v0 | v4
    const __m256i q15 = _mm256_shuffle_epi8(_mm256_permute2x128_si256(
        _mm256_load_si256(reinterpret_cast<const __m256i*>(quads[0])), _mm256_load_si256(reinterpret_cast<const __m256i*>(quads[4])), 0x31), swap); // v1 | v5
    const __m256i q26 = _mm256_shuffle_epi8(_mm256_permute2x128_si256(
        _mm256_load_si256(reinterpret_cast<const __m256i*>(quads[2])), _mm256_load_si256(reinterpret_cast<const __m256i*>(quads[6])), 0x20), swap); // v2 | v6
    const __m256i q37 = _mm256_shuffle_epi8(_mm256_permute2x128_si256(
        _mm256_load_si256(reinterpret_cast<const __m256i*>(quads[2])), _mm256_load_si256(reinterpret_cast<const __m256i*>(quads[6])), 0x31), swap); // v3 | v7
    const __m256i t0 = _mm256_unpacklo_epi32(q04, q15), t1 = _mm256_unpackhi_epi32(q04, q15);
    const __m256i t2 = _mm256_unpacklo_epi32(q26, q37), t3 = _mm256_unpackhi_epi32(q26, q37);
    // t0 = [v0c0 v1c0 v0c1 v1c1 | v4c0 v5c0 v4c1 v5c1], t2 the same for v2,v3 | v6,v7:
    // the 64-bit unpacks give each component in lane order.
    block.component[0] = _mm256_unpacklo_epi64(t0, t2);
    block.component[1] = _mm256_unpackhi_epi64(t0, t2);
    block.component[2] = _mm256_unpacklo_epi64(t1, t3);
    block.component[3] = _mm256_unpackhi_epi64(t1, t3);
#endif
}

/* Runs the original blend routine on materialized tables. On success the SoA
 * holds the blended attributes, registers are back at their entry values and
 * the snapshot stays armed until the caller commits or rolls back. */
inline bool run_geometry_host_blend(spu_context& c, const GeometryJobSetup& setup,
    const GeometryHostInputRecipe& recipe, GeometryHostInputData& data, const char** reason)
{
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    const GeometryJobEntry& e = setup.entry;
    GeometryHostBlendSnapshot& snapshot = geometry_host_blend_snapshot();
    snapshot.take_registers(c);
    const char* diff = std::getenv("BUZZ_GEOMETRY_HOST_BLEND_DIFF");
    const bool review = diff && diff[0] == '1';
    if (review) snapshot.take_full(c);

    // The original's entry SP and its own context; nothing is written before
    // all ranges are known and saved.
    const uint32_t sp = snapshot.gpr[1]._u32[0] - 0x520;
    const uint32_t context = setup.initialization.context;
    const uint32_t work_begin = e.input[GeometryJobEntry::stream_a_first].lsa;
    const uint32_t work_end = setup.initialization.workspace + setup.initialization.workspace_bytes;
    if ((sp & 15) || sp < 0xe680 + 0x800 || sp + 0x40 > SPU_LS_SIZE || context & 15 ||
        uint64_t(context) + 0x190 > SPU_LS_SIZE || work_begin > work_end || work_end > SPU_LS_SIZE)
        return reject("host blend LS layout");
    snapshot.take(c, context, context + 0x190);
    snapshot.take(c, sp - 0x800, sp + 0x40);
    snapshot.take(c, 0xdf00, 0xe500);

    // Context initialization writes only the context, so the work range is
    // saved after it. The host blend never writes the attribute tables: they
    // are saved only if a job switches to the original (materialize below).
    GeometryContextState state;
    if (!initialize_geometry_context(c, setup.initialization, state, reason)) {
        snapshot.restore(c); return false;
    }
    uint8_t* ctx = c.ls + context;
    if (state.table_end > SPU_LS_SIZE || geometry_job_be32(ctx + 0x17c) != work_end) {
        snapshot.restore(c); return reject("host blend table span");
    }
    const int native_mode = geometry_host_blend_native_mode();
    const uint32_t tables_begin = state.tables[0], tables_end = state.table_end;
    const bool skip_tables = native_mode == 1 && work_begin <= tables_begin && tables_begin <= tables_end &&
                             tables_end <= work_end;
    if (skip_tables) {
        snapshot.take(c, work_begin, tables_begin);
        snapshot.take(c, tables_end, work_end);
    } else snapshot.take(c, work_begin, work_end);

    // Base decode effects at 0x0C08: slot routing IDs, routing pointers 1..4.
    memset(ctx + 0x160, 0, 16);
    for (unsigned field = 0; field < recipe.fields; ++field) ctx[0x160 + field] = uint8_t(recipe.field[field].semantic);
    for (unsigned routing = 1; routing <= 4; ++routing) {
        uint32_t pointer = 0;
        for (unsigned field = 0; field < recipe.fields; ++field)
            if (recipe.field[field].semantic == routing) { pointer = state.tables[field]; break; }
        geometry_job_put32(ctx + 0x58 + routing * 4, pointer);
    }
    // Allocator state left by the base flow: the workspace cursor reaches the
    // first vertex stream (already decoded into the tables), the custom words
    // point past the configuration input and at the workspace.
    geometry_job_put32(ctx + 0x54, e.input[GeometryJobEntry::stream_a_first].lsa);
    geometry_job_put32(ctx + 0x58, e.input[GeometryJobEntry::stream_a_first].lsa);
    geometry_job_put32(ctx + 0x6c, e.input[GeometryJobEntry::fixed_a].lsa);
    geometry_job_put32(ctx + 0x70, e.input[GeometryJobEntry::fixed_a].lsa);
    geometry_job_put32(ctx + 0x74, e.workspace_lsa);

    // Captured attributes into their float4 tables (BE words, four lanes).
    struct Tables { spu_context* c; const GeometryHostInputRecipe* recipe; GeometryHostInputData* data; const GeometryContextState* state;
                    GeometryHostBlendSnapshot* snapshot; bool save; };
    Tables tables{&c, &recipe, &data, &state, &snapshot, skip_tables};
    auto materialize = [](spu_context&, void* arg) {
        const auto& t = *static_cast<Tables*>(arg);
        if (t.save) t.snapshot->take(*t.c, t.state->tables[0], t.state->table_end);
        for (unsigned field = 0; field < t.recipe->fields; ++field) {
            uint8_t* table = t.c->ls + t.state->tables[field];
            for (unsigned first = 0; first < t.recipe->vertices; first += 8) {
                const unsigned count = t.recipe->vertices - first < 8 ? t.recipe->vertices - first : 8;
                geometry_blend_soa_to_quads(t.data->at(*t.recipe, field, first / 8), table + first * 16, count);
            }
        }
    };
    if (native_mode != 1) materialize(c, &tables);

    // Call ABI of 0x2578 -> 0x0BF8.
    const uint32_t user8 = geometry_job_be32(e.user.data() + 8);
    auto& r = c.gpr;
    r[1] = spu_il(0); r[1]._u32[0] = sp;
    r[3] = spu_il(0); r[3]._u32[0] = context;
    {
        // Scale = (user8 >> 16) / 32767 with the original reciprocal sequence.
        u128 divisor = spu_ilhu(0x46ff); divisor = spu_iohl(divisor, 0xfe00);
        const u128 estimate = spu_frest(divisor), reciprocal = spu_fi(divisor, estimate);
        u128 raw = spu_il(0); raw._u32[0] = user8 >> 16;
        const u128 numerator = spu_csflt(raw, 155);
        const u128 quotient = spu_fm(numerator, reciprocal);
        const u128 residual = spu_fnms(quotient, divisor, numerator);
        r[4] = spu_fma(residual, reciprocal, quotient);
    }
    r[5] = spu_il(0); r[5]._u32[0] = user8 & 0xffff;
    r[6] = spu_il(0); r[6]._u32[0] = geometry_job_be32(e.user.data() + 12);
    r[7] = spu_il(0); r[7]._u32[0] = geometry_job_be32(e.user.data() + 16);
    r[8] = spu_il(0); r[8]._u32[0] = sp + 32;
    r[9] = r[5];
    geometry_host_blend_check(c, context, state, recipe, e);
    c.pc = 0xbf8;
    // BUZZ_GEOMETRY_HOST_BLEND_NATIVE=2: also compute the blend on a host
    // copy of the tables and compare it with the original after read-back.
    GeometryHostBlendNative native;
    for (unsigned field = 0; field < recipe.fields; ++field) native.tables[field] = state.tables[field];
    native.recipe = &recipe;
    const bool compare = native_mode == 2;
    if (native_mode == 1) {
        // SoA tables throughout; the original only after an unsupported stream.
        native.data = &data;
        geometry_host_blend_switch() = {materialize, &tables};
        geometry_host_blend_native() = &native;
        geometry_command_call(c, geometry_host_blend_native_entry, 0x25c4);
        geometry_host_blend_native() = nullptr;
        geometry_host_blend_note(native);
    } else if (compare) {
        static thread_local std::unique_ptr<GeometryHostInputData> copy(new GeometryHostInputData);
        for (unsigned field = 0; field < recipe.fields; ++field) {
            native.tables[field] = state.tables[field];
            for (unsigned b = 0; b < recipe.blocks_per_field; ++b) copy->at(recipe, field, b) = data.at(recipe, field, b);
        }
        native.recipe = &recipe; native.data = copy.get();
        geometry_host_blend_native() = &native;
        geometry_command_call(c, geometry_host_blend_compare_entry, 0x25c4);
        geometry_host_blend_native() = nullptr;
    } else
    geometry_command_call(c, buzz_job_6eba_spu_func_00000BF8, 0x25c4);

    if (review) {
        // Diagnostic: every LS range the call changed, relative to its anchors.
        const uint8_t* before = snapshot.ls.get();
        auto saved = [&](uint32_t at) {
            for (unsigned i = 0; i < snapshot.ranges; ++i)
                if (snapshot.range[i].begin <= at && at < snapshot.range[i].end) return true;
            return at >= state.tables[0] && at < state.table_end;
        };
        for (uint32_t at = 0; at < SPU_LS_SIZE;) {
            if (before[at] == c.ls[at]) { ++at; continue; }
            if (!saved(at)) std::fprintf(stderr, "[host-blend-diff] UNSAVED %05X\n", at);
            uint32_t end = at;
            while (end < SPU_LS_SIZE && (before[end] != c.ls[end] || (end + 64 < SPU_LS_SIZE &&
                   memcmp(before + end, c.ls + end, 64)))) ++end;
            std::fprintf(stderr, "[host-blend-diff] %05X..%05X ctx=%05X sp=%05X tables=%05X..%05X work=%05X..%05X\n",
                at, end, context, sp, state.tables[0], state.table_end,
                geometry_job_be32(ctx + 0x58), geometry_job_be32(ctx + 0x17c));
            at = end;
        }
    }
    // Blended (and renormalized) attributes back into SoA.
    if (native_mode != 1 || native.lifted)
    for (unsigned field = 0; field < recipe.fields; ++field) {
        const uint8_t* table = c.ls + state.tables[field];
        for (unsigned first = 0; first < recipe.vertices; first += 8) {
            const unsigned count = recipe.vertices - first < 8 ? recipe.vertices - first : 8;
            geometry_blend_quads_to_soa(table + first * 16, data.at(recipe, field, first / 8), count);
        }
    }
    if (compare) geometry_host_blend_compare(recipe, data, native);
    // The rest of the host pipeline reads the job's entry LS (raw streams,
    // indices, palettes); the blend only lives on in the SoA.
    snapshot.restore(c);
    if (reason) *reason = nullptr;
    return true;
}
} // namespace buzz
