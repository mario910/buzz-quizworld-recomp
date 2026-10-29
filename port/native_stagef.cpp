/* Image 41 stage F: the vertex loop 0x6338/0x63E8 (setup 0x60E8 stays lifted).
 * The generated wrapper (generate-geometry-stagef.py, build-windows.ps1
 * -StageF) hands every call here.
 *
 *   BUZZ_STAGEF=0  lifted reference (default; the wrapper does not call in)
 *   BUZZ_STAGEF=1  native implementation, reference for what it does not cover
 *   BUZZ_STAGEF=2  run both and compare (reference result is kept)
 *   BUZZ_STAGEF=3  probe: write the entry/exit state of the first
 *                  BUZZ_STAGEF_DUMP_MAX calls to BUZZ_STAGEF_DUMP (a directory)
 *   BUZZ_STAGEF=4  diagnostic: reference, then overwrite the volatile registers
 *                  r2..r79 with garbage (proves the caller never reads them)
 */
#include "spu_context.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <malloc.h>
#include <windows.h>
#endif

extern "C" {
int buzz_stagef_mode = 0;
int buzz_stagef_width = 8;   /* BUZZ_STAGEF_WIDTH=4: the u128 helper path */
int buzz_stagef_probe_on = 0;
void buzz_stagef_probe(spu_context* ctx, unsigned pc);
void buzz_stagef_cull_run(spu_context* ctx, void (*reference)(spu_context*),
                          int (*native)(spu_context*), void (*exit_fn)(spu_context*), unsigned base_reg, unsigned kept_reg);
int buzz_stagef_last_fallback(void);
void buzz_stagef_report(void);
void buzz_stagef_run(spu_context* ctx, void (*reference)(spu_context*),
                     int (*native)(spu_context*), int kernel);
}

// A reference-only build has no Stage-F kernel TU. This diagnostic hook does
// not execute/complete a job; the real Stage-F TU overrides it when present.
extern "C" __attribute__((weak)) int buzz_stagef_last_fallback(void) { return 0; }

namespace {

std::string g_dump_dir, g_probe_dir;
long g_dump_max = 8;
volatile long g_dumped = 0;
/* Leaf kernels (generate-geometry-stagef.py KERNELS): 0 vertex loop 0x6338,
 * 1 matrix x float4 array 0xC3A0, 2 normalize 0x2C10. BUZZ_STAGEF_KERNELS is
 * a bit mask (default all) of the ones BUZZ_STAGEF applies to; the others
 * run the lift. */
constexpr int kKernels = 4;   /* 3: image 39 0x05D0 (generate-image39-kernels.py) */
const char* const kKernelNames[kKernels] = {"vertex 6338", "xform C3A0", "normalize 2C10", "img39 05D0"};
unsigned g_kernel_mask = ~0u;
volatile long g_compared[kKernels], g_mismatched[kKernels], g_native[kKernels], g_fallback[kKernels];
volatile long g_reasons[4];
volatile long g_restored = 0;

struct StageFInit {
    StageFInit()
    {
        if (const char* e = getenv("BUZZ_STAGEF")) buzz_stagef_mode = atoi(e);
        if (const char* e = getenv("BUZZ_STAGEF_WIDTH")) buzz_stagef_width = atoi(e) == 4 ? 4 : 8;
        if (const char* e = getenv("BUZZ_STAGEF_KERNELS")) g_kernel_mask = (unsigned)strtoul(e, nullptr, 0);
        if (const char* e = getenv("BUZZ_STAGEF_DUMP")) g_dump_dir = e;
        if (const char* e = getenv("BUZZ_STAGEF_DUMP_MAX")) g_dump_max = atol(e);
        if (const char* e = getenv("BUZZ_STAGEF_PROBE")) { g_probe_dir = e; buzz_stagef_probe_on = 1; }
        if (getenv("BUZZ_STAGEF_CULLTEST")) buzz_stagef_probe_on = 1;
        if (buzz_stagef_mode)
            fprintf(stderr, "[stagef] mode=%d (1 native, 2 compare, 3 probe) width=%d\n", buzz_stagef_mode,
                    buzz_stagef_width);
    }
} g_init;

/* Run the lifted function to its return, exactly as the brsl caller would. */
void run_reference(spu_context* ctx, void (*reference)(spu_context*))
{
    const uint32_t ret = ctx->gpr[0]._u32[0];
    reference(ctx);
    spu_drain_call(ctx, ret);
    g_spu_trampoline_fn = nullptr;
    ctx->pc = ret;
}

spu_context* clone(const spu_context* ctx)
{
#ifdef _WIN32
    auto* copy = static_cast<spu_context*>(_aligned_malloc(sizeof(spu_context), 16));
#else
    auto* copy = static_cast<spu_context*>(aligned_alloc(16, sizeof(spu_context)));
#endif
    memcpy(copy, ctx, sizeof(spu_context));
    copy->ls = copy->ls_store;
    return copy;
}

void release(spu_context* copy)
{
#ifdef _WIN32
    _aligned_free(copy);
#else
    free(copy);
#endif
}

void dump(const spu_context* before, const spu_context* after, int kernel)
{
    long n = InterlockedIncrement(&g_dumped);
    if (n > g_dump_max || g_dump_dir.empty()) return;
    char name[64];
    snprintf(name, sizeof name, "/stagef_k%d_%03ld.bin", kernel, n - 1);
    FILE* f = fopen((g_dump_dir + name).c_str(), "wb");
    if (!f) return;
    fwrite(before->gpr, sizeof(before->gpr), 1, f);
    fwrite(before->ls, SPU_LS_SIZE, 1, f);
    fwrite(after->gpr, sizeof(after->gpr), 1, f);
    fwrite(after->ls, SPU_LS_SIZE, 1, f);
    fclose(f);
}

void compare(const spu_context* reference, const spu_context* native, int kernel)
{
    InterlockedIncrement(&g_compared[kernel]);
    unsigned ls_diff = 0, first = 0;
    for (uint32_t i = SPU_LS_SIZE; i-- > 0;)
        if (reference->ls[i] != native->ls[i]) { ls_diff++; first = i; }
    uint64_t regs = 0, regs_high = 0;
    /* The contract: r2..r79 are volatile and dead after the return (proven by
     * BUZZ_STAGEF=4 on the corpus); everything else must match. */
    for (int r = 0; r < 128; r++)
        if ((r < 2 || r >= 80) && memcmp(&reference->gpr[r], &native->gpr[r], 16))
            (r < 64 ? regs : regs_high) |= 1ull << (r & 63);
    if (!ls_diff && !regs && !regs_high && reference->pc == native->pc) return;
    if (InterlockedIncrement(&g_mismatched[kernel]) <= 16)
        fprintf(stderr, "[stagef] MISMATCH %s: %u LS bytes (first %05X), registers %016llX:%016llX,"
                        " pc %05X/%05X\n", kKernelNames[kernel], ls_diff, first, (unsigned long long)regs_high,
                (unsigned long long)regs, reference->pc, native->pc);
}

} // namespace

void buzz_stagef_run(spu_context* ctx, void (*reference)(spu_context*),
                     int (*native)(spu_context*), int kernel)
{
    if (kernel < 0 || kernel >= kKernels || !((g_kernel_mask >> kernel) & 1)) {
        run_reference(ctx, reference);
        return;
    }
    auto run_native = [native](spu_context* c) { return native(c) != 0; };
    if (buzz_stagef_mode == 1) {
        if (run_native(ctx)) {
            InterlockedIncrement(&g_native[kernel]);
            return;
        }
        InterlockedIncrement(&g_fallback[kernel]);
        InterlockedIncrement(&g_reasons[buzz_stagef_last_fallback() & 3]);
        run_reference(ctx, reference);
        return;
    }
    if (buzz_stagef_mode == 2) {
        spu_context* copy = clone(ctx);
        run_reference(ctx, reference);
        if (run_native(copy)) {
            compare(ctx, copy, kernel);
            if (g_compared[kernel] % 50000 == 0) buzz_stagef_report();
        }
        else InterlockedIncrement(&g_fallback[kernel]);
        release(copy);
        return;
    }
    if (buzz_stagef_mode == 3) {
        spu_context* before = clone(ctx);
        run_reference(ctx, reference);
        dump(before, ctx, kernel);
        release(before);
        return;
    }
    run_reference(ctx, reference);
    if (buzz_stagef_mode == 4) {
        /* BUZZ_STAGEF_POISON_KEEP=3,4: registers the kernel returns (left alone) */
        static uint64_t keep = UINT64_MAX;
        if (keep == UINT64_MAX) {
            keep = 0;
            for (const char* e = getenv("BUZZ_STAGEF_POISON_KEEP"); e && *e;) {
                char* end; long r = strtol(e, &end, 10);
                if (end == e) break;
                if (r >= 0 && r < 64) keep |= 1ull << r;
                e = *end == ',' ? end + 1 : end;
            }
        }
        for (int r = 2; r < 80; r++)
            if (r >= 64 || !((keep >> r) & 1))
                for (int w = 0; w < 4; w++) ctx->gpr[r]._u32[w] = 0xDEAD0000u ^ (uint32_t)(r * 4 + w) * 0x9E3779B1u;
    }
}

/* BUZZ_STAGEF_PROBE=<dir>: registers + LS at the cull loop's entry (0x6D28)
 * and exit (0x6888); files pair up by number (one job thread at a time in
 * replay). First BUZZ_STAGEF_DUMP_MAX pairs. */
/* BUZZ_STAGEF_CULLTEST=1 (diagnostic): at the cull loop's exit, put back the
 * pre-loop value of every LS byte the loop changed at or past
 * align16(index base + 6 * kept). If every corpus transfer stays identical,
 * those bytes (shuffle spill, scratch redirects) are dead. */
static int g_culltest = -1;
static thread_local uint8_t* t_cull_before;
static thread_local uint32_t t_cull_base;
static bool culltest(spu_context* ctx, unsigned pc)
{
    if (g_culltest < 0) { const char* e = getenv("BUZZ_STAGEF_CULLTEST"); g_culltest = e ? atoi(e) : 0; }
    if (!g_culltest) return false;
    if (pc == 0x6D28) {
        if (!t_cull_before) t_cull_before = static_cast<uint8_t*>(malloc(SPU_LS_SIZE));
        memcpy(t_cull_before, ctx->ls, SPU_LS_SIZE);
        t_cull_base = ctx->gpr[13]._u32[0];
        return true;
    }
    if (!t_cull_before || !t_cull_base) return true;
    const uint32_t end = (t_cull_base + 6u * ctx->gpr[51]._u32[0] + 15u) & ~15u;
    for (uint32_t i = 0; i < SPU_LS_SIZE; i++)
        if (i >= end && ctx->ls[i] != t_cull_before[i]) {
            InterlockedIncrement(&g_restored);
            ctx->ls[i] = t_cull_before[i];
        }
    t_cull_base = 0;
    if (g_culltest == 2)   /* also: only r1, r51, r81 are live at 0x6888 */
        for (int r = 2; r <= 102; r++)
            if (r != 51 && r != 81)
                for (int w = 0; w < 4; w++) ctx->gpr[r]._u32[w] = 0xBAD00000u ^ (uint32_t)(r * 4 + w) * 0x9E3779B1u;
    return true;
}

void buzz_stagef_probe(spu_context* ctx, unsigned pc)
{
    if (culltest(ctx, pc)) return;
    static long pairs;
    static thread_local bool pending;
    if (pairs >= g_dump_max) return;
    /* entry -> exit pairs: cull 0x6D28 -> 0x6888, normal decode 0x8FD4 -> 0x82B8 */
    static thread_local unsigned pending_entry;
    const bool is_entry = pc == 0x6D28 || pc == 0x8FD4;
    const unsigned exit_of_pending = pending_entry == 0x6D28 ? 0x6888u : pending_entry == 0x8FD4 ? 0x82B8u : 0u;
    if (!is_entry && (!pending || pc != exit_of_pending)) return;   /* reached without the loop */
    pending = is_entry;
    if (is_entry) pending_entry = pc;
    char name[64];
    snprintf(name, sizeof name, "/probe_%03ld_%04X.bin", pairs, pc);
    if (!is_entry) pairs++;
    FILE* f = fopen((g_probe_dir + name).c_str(), "wb");
    if (!f) return;
    fwrite(ctx->gpr, sizeof(ctx->gpr), 1, f);
    fwrite(ctx->ls, SPU_LS_SIZE, 1, f);
    fclose(f);
}

/* Cull loop: the reference runs from 0x6D28 until it hands over to 0x6888
 * (the loop only trampolines between its own blocks). The contract compared
 * is r51 (kept count) and the index bytes up to the 16-byte boundary after
 * the last kept triangle; the trampoline is left at 0x6888 either way. */
static volatile long g_cull_native = 0, g_cull_fallback = 0, g_cull_compared = 0, g_cull_mismatched = 0;

static void run_reference_until(spu_context* ctx, void (*reference)(spu_context*),
                                void (*exit_fn)(spu_context*))
{
    reference(ctx);
    while (g_spu_trampoline_fn && g_spu_trampoline_fn != exit_fn) {
        void (*fn)(spu_context*) = g_spu_trampoline_fn;
        g_spu_trampoline_fn = nullptr;
        fn(ctx);
    }
}

void buzz_stagef_cull_run(spu_context* ctx, void (*reference)(spu_context*),
                          int (*native)(spu_context*), void (*exit_fn)(spu_context*), unsigned base_reg, unsigned kept_reg)
{
    if (buzz_stagef_mode == 1) {
        if (native(ctx)) { InterlockedIncrement(&g_cull_native); return; }
        InterlockedIncrement(&g_cull_fallback);
        reference(ctx);
        return;
    }
    if (buzz_stagef_mode != 2) { reference(ctx); return; }
    spu_context* copy = clone(ctx);
    const uint32_t base = ctx->gpr[base_reg]._u32[0];
    run_reference_until(ctx, reference, exit_fn);
    void (*after)(spu_context*) = g_spu_trampoline_fn;
    if (!native(copy)) { InterlockedIncrement(&g_cull_fallback); release(copy); return; }
    g_spu_trampoline_fn = after;   /* keep the reference's continuation */
    if (InterlockedIncrement(&g_cull_compared) % 50000 == 0) buzz_stagef_report();
    const uint32_t kept = ctx->gpr[kept_reg]._u32[0];
    const uint32_t end = (base + 6u * kept + 15u) & ~15u;
    bool same = !memcmp(&ctx->gpr[kept_reg], &copy->gpr[kept_reg], 16) && ctx->pc == copy->pc;
    for (uint32_t a = base; a < end && same; a++) same = ctx->ls[a & 0x3FFFF] == copy->ls[a & 0x3FFFF];
    if (!same && InterlockedIncrement(&g_cull_mismatched) <= 16) {
        uint32_t first = base;
        while (first < end && ctx->ls[first & 0x3FFFF] == copy->ls[first & 0x3FFFF]) first++;
        fprintf(stderr, "[stagef] CULL MISMATCH: kept %u/%u, first byte %05X of %05X-%05X\n",
                kept, copy->gpr[kept_reg]._u32[0], first, base, end);
    }
    release(copy);
}

/* Summary for the replay harness and logs. */
extern "C" void buzz_stagef_report(void)
{
    if (g_restored) fprintf(stderr, "[stagef] culltest: restored %ld junk bytes\n", g_restored);
    if (buzz_stagef_mode)
        fprintf(stderr, "[stagef] cull: native %ld, fallback %ld, compared %ld, mismatched %ld\n",
                g_cull_native, g_cull_fallback, g_cull_compared, g_cull_mismatched);
    if (buzz_stagef_mode)
        for (int k = 0; k < kKernels; k++)
            fprintf(stderr, "[stagef] %s: native %ld, fallback %ld, compared %ld, mismatched %ld\n",
                    kKernelNames[k], g_native[k], g_fallback[k], g_compared[k], g_mismatched[k]);
    if (buzz_stagef_mode)
        fprintf(stderr, "[stagef] vertex fallback reasons: count %ld, empty %ld, overlap %ld\n",
                g_reasons[1], g_reasons[2], g_reasons[3]);
}
