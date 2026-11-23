/* Host (SoA) implementation of the image-41 additional blend streams, built
 * against the original routines through a compare harness.
 *
 * Per selected stream the original 0x7DF8 (blend mode) decodes each
 * descriptor attribute that has a destination table into a temporary float4
 * table, zeroes the padding quads up to a multiple of four vertices, and calls
 * 0xC750 with: remap = the expanded run list (BE16 byte offsets), destination
 * = the attribute's routing table, mask = the first `components` lanes,
 * weight = the stream's raw weight word. 0xC750 applies
 *     dest[remap[v]] = fma(src[v] & mask, weight, dest[remap[v]])
 * for v < round4(count) with a two-pair software pipeline (next B is loaded
 * before the current stores), so repeated destinations keep that order here.
 * After all streams 0x2780/0x2C00 renormalize the normal table.
 *
 * BUZZ_GEOMETRY_HOST_BLEND_NATIVE=2 runs the original (through the exact
 * native orchestration in native_geometry_blend_lists.h) and this host
 * version side by side and compares every blended table lane. */
#pragma once
#include "native_geometry_blend_lists.h"
#include "native_geometry_run_lists.h"
#include "native_geometry_host_input_job.h"
#include "native_geometry_host_streams.h"
#include "native_geometry_host_unit_vectors.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>

extern "C" __attribute__((weak)) void buzz_job_6eba_spu_func_00007118(spu_context*);
extern "C" __attribute__((weak)) void buzz_job_6eba_spu_func_00007DF8(spu_context*);
extern "C" __attribute__((weak)) void buzz_job_6eba_spu_func_00002780(spu_context*);
extern "C" __attribute__((weak)) void buzz_job_6eba_spu_func_00002C00(spu_context*);
extern "C" __attribute__((weak)) void buzz_job_6eba_spu_func_00000D04(spu_context*);

namespace buzz {
inline int geometry_host_blend_native_mode()
{
    static const int mode = [] {
        // Default 1 (host SoA blend); 0 keeps the original, 2 compares.
        const char* e = std::getenv("BUZZ_GEOMETRY_HOST_BLEND_NATIVE");
        return e && e[0] >= '0' && e[0] <= '2' && !e[1] ? e[0] - '0' : 1;
    }();
    return mode;
}

struct GeometryHostBlendNative {
    const GeometryHostInputRecipe* recipe = nullptr;
    GeometryHostInputData* data = nullptr;      // host result tables
    uint32_t tables[16] = {};                   // LS table of each SoA field
    const char* failure = nullptr;
    unsigned streams = 0, attributes = 0;
    bool lifted = false;                        // native mode switched to the original
};
inline GeometryHostBlendNative*& geometry_host_blend_native()
{
    static thread_local GeometryHostBlendNative* native = nullptr;
    return native;
}

// One vertex of an SoA field as the float4 quad the original keeps in LS.
inline uint32_t* geometry_host_blend_lane(GeometryHostInputData& d, const GeometryHostInputRecipe& r,
                                          unsigned field, unsigned vertex)
{
    return reinterpret_cast<uint32_t*>(&d.at(r, field, vertex / 8)) + (vertex % 8);
}
inline u128 geometry_host_blend_get(GeometryHostInputData& d, const GeometryHostInputRecipe& r,
                                    unsigned field, unsigned vertex)
{
    const uint32_t* lane = geometry_host_blend_lane(d, r, field, vertex);
    u128 v; for (unsigned c = 0; c < 4; ++c) v._u32[c] = lane[c * 8]; return v;
}
inline void geometry_host_blend_set(GeometryHostInputData& d, const GeometryHostInputRecipe& r,
                                    unsigned field, unsigned vertex, u128 v)
{
    uint32_t* lane = geometry_host_blend_lane(d, r, field, vertex);
    for (unsigned c = 0; c < 4; ++c) lane[c * 8] = v._u32[c];
}

// Decode one attribute of a blend stream into 8-vertex SoA blocks covering
// round4(count) vertices: lanes past `count` and components past
// `components` are zero (the original zeroes its padding quads and masks).
inline bool geometry_host_blend_decode(const spu_context& c, uint32_t source, unsigned stride,
    unsigned storage, unsigned components, unsigned count, GeometryHostSoA8* out, const char** why)
{
    if (storage == 3) {
        decode_geometry_host_half_stream(c.ls, source, stride, components, count, out);
        return true;
    }
    for (unsigned first = 0; first < count; first += 8) {
        const unsigned active = count - first < 8 ? count - first : 8;
        const uint32_t at = source + first * stride;
        GeometryHostSoA8 block;
        if (storage == 2) block = decode_geometry_host_floats8(c.ls + at, stride, components, active);
        else if (storage == 9) {
            const v8 words = geometry_host_unit_words8(c.ls + at, stride, active);
            if (_mm256_movemask_epi8(_mm256_cmpgt_epi32(_mm256_and_si256(words, v8_set(15)), v8_set(11)))) {
                *why = "blend unit selector"; return false;
            }
            block = decode_geometry_host_unit8(words);
        } else { *why = storage == 8 ? "blend storage 8" : "blend storage"; return false; }
        const v8 lanes = _mm256_cmpgt_epi32(v8_set(active), _mm256_setr_epi32(0,1,2,3,4,5,6,7));
        for (unsigned k = 0; k < 4; ++k)
            out[first / 8].component[k] = k < components ? _mm256_and_si256(block.component[k], lanes) : _mm256_setzero_si256();
    }
    return true;
}

// Destination vertex of every remap entry (A = high halfword of each pair's
// word, B = the full word, as LQD/STQD form both addresses), and the independent prefix
// that can be scattered in parallel. Every table has the same capacity.
inline bool geometry_host_blend_destinations(const spu_context& c, uint32_t table, uint32_t remap,
    unsigned total, unsigned capacity, uint16_t* dest, unsigned* independent_prefix)
{
    uint64_t seen[(4096 + 64) / 64] = {};
    unsigned prefix = total;
    for (unsigned i = 0; i < total; i += 2) {
        const uint32_t word = geometry_job_be32(c.ls + remap + i * 2);
        const unsigned a = ((((table + (word >> 16)) & (SPU_LS_SIZE - 16)) - table) & (SPU_LS_SIZE - 1)) / 16;
        const unsigned b = ((((table + word) & (SPU_LS_SIZE - 16)) - table) & (SPU_LS_SIZE - 1)) / 16;
        if (a >= capacity || b >= capacity) return false;
        dest[i] = uint16_t(a);
        dest[i+1] = uint16_t(b);
        if (prefix == total) {
            for (const unsigned v : {a, b}) {
                const uint64_t bit = UINT64_C(1) << (v & 63);
                if (seen[v >> 6] & bit) prefix = i & ~7u;
                seen[v >> 6] |= bit;
            }
        }
    }
    // At a split the original has already loaded the next B before storing
    // either preceding result, and the next A before storing the previous A.
    // Keep these pairs together if reloading either destination could differ.
    if (prefix && prefix < total &&
        (dest[prefix] == dest[prefix-2] || dest[prefix+1] == dest[prefix-2] ||
         dest[prefix+1] == dest[prefix-1])) prefix -= 8;
    *independent_prefix = prefix;
    return true;
}

// All destinations distinct: the pipeline order cannot matter, eight entries
// at a time with the exact eight-lane FMA.
inline void geometry_host_blend_scatter_distinct(GeometryHostBlendNative& n, unsigned field,
    const uint16_t* dest, unsigned total, const GeometryHostSoA8* src, uint32_t weight)
{
    auto& d = *n.data; const auto& r = *n.recipe; const v8 w = v8_set(weight);
    for (unsigned first = 0; first < total; first += 8) {
        const unsigned active = total - first < 8 ? total - first : 8;
        const GeometryHostSoA8& s = src[first / 8];
        const unsigned d0 = dest[first];
#if defined(__AVX2__) && !defined(__aarch64__)
        bool run = false;
        if (active == 8) {
            const __m128i indices = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dest + first));
            const __m128i consecutive = _mm_add_epi16(_mm_set1_epi16(d0), _mm_setr_epi16(0,1,2,3,4,5,6,7));
            run = _mm_movemask_epi8(_mm_cmpeq_epi16(indices, consecutive)) == 0xffff;
        }
        if (run && (d0 & 7)) {
            // An unaligned run spans two SoA blocks. Rotate its eight source
            // lanes into destination order and retain the untouched neighbors.
            const v8 lanes = _mm256_setr_epi32(0,1,2,3,4,5,6,7);
            const v8 order = _mm256_sub_epi32(lanes, v8_set(d0 & 7));
            const v8 upper = _mm256_cmpgt_epi32(_mm256_setzero_si256(), order);
            auto& left = d.at(r, field, d0 / 8);
            auto& right = d.at(r, field, d0 / 8 + 1);
            for (unsigned k = 0; k < 4; ++k) {
                const v8 a = left.component[k], b = right.component[k];
                const v8 previous = _mm256_blendv_epi8(a, b, upper);
                const v8 source = _mm256_permutevar8x32_epi32(s.component[k], order);
                const v8 result = v8_fma(source, w, previous);
                left.component[k] = _mm256_blendv_epi8(result, a, upper);
                right.component[k] = _mm256_blendv_epi8(b, result, upper);
            }
            continue;
        }
#else
        bool run = active == 8 && (d0 & 7) == 0;
        for (unsigned j = 1; run && j < 8; ++j) run = dest[first + j] == d0 + j;
#endif
        if (run) {
            auto& block = d.at(r, field, d0 / 8);
            for (unsigned k = 0; k < 4; ++k) block.component[k] = v8_fma(s.component[k], w, block.component[k]);
            continue;
        }
        uint32_t* destinations[8];
        alignas(32) uint32_t lanes[4][8] = {};
        for (unsigned j = 0; j < active; ++j) {
            auto* p = geometry_host_blend_lane(d, r, field, dest[first + j]);
            destinations[j] = p;
            for (unsigned k = 0; k < 4; ++k) lanes[k][j] = p[k * 8];
        }
        for (unsigned k = 0; k < 4; ++k)
            _mm256_store_si256(reinterpret_cast<v8*>(lanes[k]),
                v8_fma(s.component[k], w, _mm256_load_si256(reinterpret_cast<const v8*>(lanes[k]))));
        for (unsigned j = 0; j < active; ++j) {
            uint32_t* p = destinations[j];
            for (unsigned k = 0; k < 4; ++k) p[k * 8] = lanes[k][j];
        }
    }
}

// Repeated destinations: the exact 0xC750 two-pair pipeline (next B is read
// before the current stores; next A after the current B store).
inline void geometry_host_blend_scatter_pipeline(GeometryHostBlendNative& n, unsigned field,
    const uint16_t* dest, unsigned total, const GeometryHostSoA8* src, uint32_t weight)
{
    auto& d = *n.data; const auto& r = *n.recipe;
    auto source = [&](unsigned v) {
        const uint32_t* lane = reinterpret_cast<const uint32_t*>(&src[v / 8]) + (v % 8);
        u128 s; for (unsigned k = 0; k < 4; ++k) s._u32[k] = lane[k * 8]; return s;
    };
    u128 w; for (unsigned k = 0; k < 4; ++k) w._u32[k] = weight;
    const unsigned pairs = total / 2;
    unsigned a = dest[0], b = dest[1];
    u128 loaded_a = geometry_host_blend_get(d, r, field, a);
    u128 pending_b = spu_fma(source(1), w, geometry_host_blend_get(d, r, field, b));
    u128 masked_a = source(0);
    for (unsigned i = 0; i + 1 < pairs; ++i) {
        const unsigned next_a = dest[2 * i + 2], next_b = dest[2 * i + 3];
        const u128 result_a = spu_fma(masked_a, w, loaded_a);
        const u128 loaded_next_b = geometry_host_blend_get(d, r, field, next_b);
        geometry_host_blend_set(d, r, field, b, pending_b);
        loaded_a = geometry_host_blend_get(d, r, field, next_a);
        geometry_host_blend_set(d, r, field, a, result_a);
        pending_b = spu_fma(source(2 * i + 3), w, loaded_next_b);
        masked_a = source(2 * i + 2);
        a = next_a; b = next_b;
    }
    const u128 result_a = spu_fma(masked_a, w, loaded_a);
    geometry_host_blend_set(d, r, field, b, pending_b);
    geometry_host_blend_set(d, r, field, a, result_a);
}

// One stream: the arguments of the original 0x7DF8 call (r3 context,
// r4 data, r6 descriptor, r7 count, r8 weight, r9 remap).
inline bool geometry_host_blend_stream(const spu_context& c, GeometryHostBlendNative& n, const char** why)
{
    const auto& g = c.gpr;
    const uint32_t context = g[3]._u32[0], data = g[4]._u32[0], descriptor = g[6]._u32[0];
    const unsigned count = g[7]._u32[0];
    const uint32_t weight = g[8]._u32[0], remap = g[9]._u32[0];
    if (!count) return true;
    if (!spu_fcgt(spu_splat_u32(weight), spu_il(0))._u32[0]) { *why = "blend non-positive weight"; return false; }
    if (count > 4096 || descriptor + 16 > SPU_LS_SIZE) { *why = "blend stream bounds"; return false; }
    const unsigned attributes = c.ls[descriptor], stride = c.ls[descriptor + 1];
    if (attributes > 16 || !stride) { *why = "blend descriptor"; return false; }
    // Heap, not static TLS: glibc carves static TLS out of every thread's
    // stack, and the SPURS chain threads recurse deeply (image 34).
    struct Buffers { GeometryHostSoA8 decoded[(4096 + 8) / 8]; uint16_t destinations[4096 + 8]; };
    static thread_local std::unique_ptr<Buffers> buffers;
    if (!buffers) buffers.reset(new Buffers);
    GeometryHostSoA8* const decoded = buffers->decoded;
    uint16_t* const destinations = buffers->destinations;
    // Validate the whole stream before changing any table, so an unsupported
    // stream can still be handed to the original intact.
    const unsigned total = (count + 3) & ~3u, capacity = n.recipe->blocks_per_field * 8;
    struct Attribute { const uint8_t* descriptor; unsigned field; };
    Attribute selected[16];
    unsigned selected_count = 0;
    for (unsigned i = 0; i < attributes; ++i) {
        const uint8_t* a = c.ls + descriptor + 8 + i * 8;
        if ((c.ls[context] & 16) && a[3] != 1) continue;
        unsigned field = 0;
        while (field < n.recipe->fields && n.recipe->field[field].semantic != a[3]) ++field;
        if (field == n.recipe->fields) continue;
        if (a[2] > 4) { *why = "blend components"; return false; }
        if (a[1] != 2 && a[1] != 3 && a[1] != 9) { *why = a[1] == 8 ? "blend storage 8" : "blend storage"; return false; }
        if (uint64_t(data) + a[0] + uint64_t(count) * stride + 64 > SPU_LS_SIZE) { *why = "blend data bounds"; return false; }
        if (a[1] == 9)
            for (unsigned first = 0; first < count; first += 8) {
                const unsigned active = count - first < 8 ? count - first : 8;
                const v8 words = geometry_host_unit_words8(c.ls + data + a[0] + first * stride, stride, active);
                if (_mm256_movemask_epi8(_mm256_cmpgt_epi32(_mm256_and_si256(words, v8_set(15)), v8_set(11)))) {
                    *why = "blend unit selector"; return false;
                }
            }
        selected[selected_count++] = {a, field};
    }
    unsigned prefix = 0;
    if (selected_count) {
        // initialize_geometry_context spaces every table by a multiple of
        // 16. The aligned LS address minus its table base is thus identical
        // for all attributes, even when a remap wraps around local store.
        if (uint64_t(remap) + total * 2 > SPU_LS_SIZE) { *why = "blend remap bounds"; return false; }
        if (!geometry_host_blend_destinations(c, n.tables[selected[0].field], remap, total,
                                              capacity, destinations, &prefix)) {
            *why = "blend remap"; return false;
        }
    }
    for (unsigned i = 0; i < selected_count; ++i) {
        const auto [a, field] = selected[i];
        if (!geometry_host_blend_decode(c, data + a[0], stride, a[1], a[2], count, decoded, why))
            return false;
        if (prefix) geometry_host_blend_scatter_distinct(n, field, destinations, prefix, decoded, weight);
        if (prefix < total) geometry_host_blend_scatter_pipeline(n, field, destinations + prefix,
            total - prefix, decoded + prefix / 8, weight);
        ++n.attributes;
    }
    ++n.streams;
    return true;
}

// 0x2C10 on the normal table, eight vertices at a time as its native kernel:
// (x, y, z) * fi(s, frsqest(s)), w * 1.0, over round4(count) vertices.
inline void geometry_host_blend_normalize(GeometryHostBlendNative& n, unsigned field, unsigned count)
{
    const unsigned total = (count + 3) & ~3u, capacity = n.recipe->blocks_per_field * 8;
    const v8 one = v8_set(0x3f800000u);
    for (unsigned first = 0; first < total && first + 4 <= capacity; first += 8) {
        auto& block = n.data->at(*n.recipe, field, first / 8);
        const v8 X = block.component[0], Y = block.component[1], Z = block.component[2], W = block.component[3];
        const v8 sq = v8_fma(Z, Z, v8_fma(Y, Y, v8_fm(X, X)));
        const v8 rs = v8_fi(sq, v8_frsqest(sq));
        const v8 out[4] = {v8_fm(X, rs), v8_fm(Y, rs), v8_fm(Z, rs), v8_fm(W, one)};
        const unsigned active = total - first < 8 ? total - first : 8;
        const v8 keep = _mm256_cmpgt_epi32(v8_set(active), _mm256_setr_epi32(0,1,2,3,4,5,6,7));
        for (unsigned k = 0; k < 4; ++k) block.component[k] = _mm256_blendv_epi8(block.component[k], out[k], keep);
    }
}

// Native mode (BUZZ_GEOMETRY_HOST_BLEND_NATIVE=1). The tables stay in SoA;
// a stream the host does not support switches the rest of the job to the
// original: the current tables are written to LS once, the original runs this
// and every later stream, and the caller reads the tables back.
struct GeometryHostBlendSwitch { void (*materialize)(spu_context&, void*) = nullptr; void* arg = nullptr; };
inline GeometryHostBlendSwitch& geometry_host_blend_switch()
{
    static thread_local GeometryHostBlendSwitch s; return s;
}
inline void geometry_host_blend_return(spu_context* c)
{
    c->pc = c->gpr[0]._u32[0]; g_spu_trampoline_fn = c->host_depth ? nullptr : spu_indirect_branch;
}
// 0x7118 run-list expansion (r3 base, r4 encoded bytes, r5 vertices), LS
// effects only: the bytes after the expansion in its last quad are cleared
// first, then each code (start << 5 | length - 1) expands in place, backwards,
// into BE16 byte offsets (start + j) * 16. No register result is used.
// BUZZ_GEOMETRY_HOST_BLEND_UNPACK_CHECK=1 compares every expansion with the original.
inline void geometry_host_blend_expand(spu_context& c, const GeometryRunList& p)
{
    if (!p.bytes) return;
    const uint32_t end = p.base + p.vertices * 2;
    if (end & 15) memset(c.ls + end, 0, 16 - (end & 15));
    uint32_t cursor = p.vertices * 2;
    for (unsigned i = p.bytes / 2; i > 0; --i) {
        const uint16_t code = uint16_t(geometry_job_be16(c.ls + p.base + (i - 1) * 2));
        const unsigned length = (code & 31) + 1;
        cursor -= length * 2;
        uint8_t* out = c.ls + p.base + cursor;
        for (unsigned j = 0; j < length; ++j) {
            const unsigned value = ((code >> 5) + j) * 16;
            out[j * 2] = uint8_t(value >> 8); out[j * 2 + 1] = uint8_t(value);
        }
    }
}
extern "C" inline void geometry_host_blend_native_unpack(spu_context* c)
{
    GeometryRunList p;
    if (!read_geometry_run_list(*c, p)) { buzz_job_6eba_spu_func_00007118(c); return; }
    static const bool check = [] { const char* e = std::getenv("BUZZ_GEOMETRY_HOST_BLEND_UNPACK_CHECK"); return e && e[0] == '1'; }();
    if (check && p.bytes) {
        const uint32_t span = ((p.base + p.vertices * 2 + 15) & ~15u) - p.base + 16;
        std::unique_ptr<uint8_t[]> before(new uint8_t[span]), host(new uint8_t[span]);
        memcpy(before.get(), c->ls + p.base, span);
        geometry_host_blend_expand(*c, p);
        memcpy(host.get(), c->ls + p.base, span);
        memcpy(c->ls + p.base, before.get(), span);
        const u128 saved[3] = {c->gpr[3], c->gpr[4], c->gpr[5]};
        geometry_command_call(*c, buzz_job_6eba_spu_func_00007118, c->gpr[0]._u32[0]);
        c->gpr[3] = saved[0]; c->gpr[4] = saved[1]; c->gpr[5] = saved[2];
        static std::atomic<unsigned long> checked{0}, mismatched{0};
        if (memcmp(host.get(), c->ls + p.base, span)) {
            if (mismatched++ < 8) std::fprintf(stderr, "[host-blend-native] UNPACK DIFF base=%05X bytes=%u vertices=%u\n", p.base, p.bytes, p.vertices);
        }
        if (++checked % 500 == 0) std::fprintf(stderr, "[host-blend-native] unpack checked=%lu mismatched=%lu\n", checked.load(), mismatched.load());
    } else geometry_host_blend_expand(*c, p);
    geometry_host_blend_return(c);
}
extern "C" inline void geometry_host_blend_native_attributes(spu_context* c)
{
    auto* n = geometry_host_blend_native();
    if (n && !n->lifted) {
        const char* why = nullptr;
        if (geometry_host_blend_stream(*c, *n, &why)) { geometry_host_blend_return(c); return; }
        n->failure = why; n->lifted = true;
        auto& s = geometry_host_blend_switch(); s.materialize(*c, s.arg);
    }
    buzz_job_6eba_spu_func_00007DF8(c);
}
extern "C" inline void geometry_host_blend_native_normalize(spu_context* c)
{
    auto* n = geometry_host_blend_native();
    if (n && !n->lifted) {
        const uint32_t table = c->gpr[3]._u32[0] & (SPU_LS_SIZE - 16);
        unsigned field = 0;
        while (field < n->recipe->fields && n->tables[field] != table) ++field;
        if (field < n->recipe->fields) {
            geometry_host_blend_normalize(*n, field, c->gpr[4]._u32[0]);
            geometry_host_blend_return(c); return;
        }
        n->failure = "blend normal table"; n->lifted = true;
        auto& s = geometry_host_blend_switch(); s.materialize(*c, s.arg);
    }
    buzz_job_6eba_spu_func_00002C00(c);
}
extern "C" inline void geometry_host_blend_native_entry(spu_context* c)
{
    static const buzz_geometry_blend_list_functions library = {
        geometry_host_blend_native_unpack, geometry_host_blend_native_attributes,
        buzz_job_6eba_spu_func_00002780, geometry_host_blend_native_normalize, buzz_job_6eba_spu_func_00000D04};
    GeometryBlendLists plan;
    if (!read_geometry_blend_lists(*c, plan)) {
        if (auto* n = geometry_host_blend_native(); n && !n->lifted) {
            n->failure = "blend lists admission"; n->lifted = true;
            auto& s = geometry_host_blend_switch(); s.materialize(*c, s.arg);
        }
        buzz_job_6eba_spu_func_00000BF8(c); return;
    }
    blend_geometry_lists(*c, plan, library);
}

// Library wrappers for the compare harness: the host step first, then the original.
extern "C" inline void geometry_host_blend_wrap_attributes(spu_context* c)
{
    if (auto* n = geometry_host_blend_native(); n && !n->failure) {
        const char* why = nullptr;
        if (!geometry_host_blend_stream(*c, *n, &why)) n->failure = why;
    }
    buzz_job_6eba_spu_func_00007DF8(c);
}
extern "C" inline void geometry_host_blend_wrap_normalize(spu_context* c)
{
    if (auto* n = geometry_host_blend_native(); n && !n->failure) {
        const uint32_t table = c->gpr[3]._u32[0] & (SPU_LS_SIZE - 16);
        unsigned field = 0;
        while (field < n->recipe->fields && n->tables[field] != table) ++field;
        if (field == n->recipe->fields) n->failure = "blend normal table";
        else geometry_host_blend_normalize(*n, field, c->gpr[4]._u32[0]);
    }
    buzz_job_6eba_spu_func_00002C00(c);
}
extern "C" inline void geometry_host_blend_compare_entry(spu_context* c)
{
    static const buzz_geometry_blend_list_functions library = {
        buzz_job_6eba_spu_func_00007118, geometry_host_blend_wrap_attributes,
        buzz_job_6eba_spu_func_00002780, geometry_host_blend_wrap_normalize, buzz_job_6eba_spu_func_00000D04};
    GeometryBlendLists plan;
    if (!read_geometry_blend_lists(*c, plan)) {
        if (auto* n = geometry_host_blend_native()) n->failure = "blend lists admission";
        buzz_job_6eba_spu_func_00000BF8(c); return;
    }
    blend_geometry_lists(*c, plan, library);
}

// Native-mode counters: jobs fully on the host vs switched to the original.
inline void geometry_host_blend_note(const GeometryHostBlendNative& n)
{
    static std::atomic<unsigned long> host{0}, switched{0};
    const unsigned long total = n.lifted ? ++switched + host.load() : ++host + switched.load();
    if (n.lifted) {
        static std::atomic<int> reported{0};
        if (reported++ < 4) std::fprintf(stderr, "[host-blend-native] original after: %s\n", n.failure ? n.failure : "?");
    }
    if (total % 5000 == 0) std::fprintf(stderr, "[host-blend-native] host=%lu original=%lu\n", host.load(), switched.load());
}

struct GeometryHostBlendCompareStats {
    std::atomic<unsigned long> jobs{0}, exact{0}, differ{0}, unsupported{0};
};
inline GeometryHostBlendCompareStats& geometry_host_blend_compare_stats()
{
    static GeometryHostBlendCompareStats stats; return stats;
}
// Compare the host tables with the original's (read back into `data`).
inline void geometry_host_blend_compare(const GeometryHostInputRecipe& r, GeometryHostInputData& reference,
                                        GeometryHostBlendNative& n)
{
    auto& s = geometry_host_blend_compare_stats();
    const unsigned long jobs = ++s.jobs;
    if (n.failure) {
        ++s.unsupported;
        static std::atomic<int> reported{0};
        if (reported++ < 8) std::fprintf(stderr, "[host-blend-native] unsupported: %s\n", n.failure);
    } else {
        bool same = true;
        for (unsigned field = 0; field < r.fields && same; ++field)
            for (unsigned v = 0; v < r.vertices && same; ++v) {
                const u128 a = geometry_host_blend_get(reference, r, field, v), b = geometry_host_blend_get(*n.data, r, field, v);
                for (unsigned k = 0; k < 4; ++k) if (a._u32[k] != b._u32[k]) {
                    same = false;
                    static std::atomic<int> reported{0};
                    if (reported++ < 16)
                        std::fprintf(stderr, "[host-blend-native] DIFF field %u (semantic %u) vertex %u lane %u: original %08X host %08X (streams %u)\n",
                                     field, r.field[field].semantic, v, k, a._u32[k], b._u32[k], n.streams);
                    break;
                }
            }
        ++(same ? s.exact : s.differ);
    }
    if (jobs % 50 == 0)
        std::fprintf(stderr, "[host-blend-native] compared=%lu exact=%lu differ=%lu unsupported=%lu\n",
                     jobs, s.exact.load(), s.differ.load(), s.unsupported.load());
}
} // namespace buzz
