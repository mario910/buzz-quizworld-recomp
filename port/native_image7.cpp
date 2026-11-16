/* Image 7 (Sofdec YUV->RGB consumer) conversion loop 0x8638, native float.
 * The generated wrapper (generate-image7-native.py, build-windows.ps1
 * -NativeImage7) hands every call here.
 *
 *   BUZZ_IMAGE7=0  lifted reference with the exact scalar helpers
 *   BUZZ_IMAGE7=1  the same body built with the native RTZ helpers, run with
 *                  MXCSR RZ+FTZ+DAZ for exactly this call (default)
 *   BUZZ_IMAGE7=2  both, compared (registers, local store, pc, next block);
 *                  the reference result is kept
 *   BUZZ_IMAGE7_SIMD=0 disables the exact AVX2 bulk path for qualified layouts
 *
 * The scope is the whole call: the conversion leaf has no channel access, stop or nested
 * call, so nothing can leave it by longjmp with the environment changed. */
#include "spu_context.h"

#include "spu_scoped_rtz.h"
#include <atomic>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <malloc.h>
#include <windows.h>
#endif

extern "C" {
#ifdef BUZZ_IMAGE7_COMPARE_DEFAULT
int buzz_image7_mode = 2;
#else
int buzz_image7_mode = 1;
#endif
void buzz_image7_run(spu_context* ctx, void (*reference)(spu_context*), void (*native)(spu_context*));
void buzz_image7_run_layout(spu_context* ctx, void (*reference)(spu_context*),
                           void (*native)(spu_context*), unsigned layout_pc);
#ifdef BUZZ_IMAGE7_SIMD_AVX2
void buzz_image7_8638_native(spu_context*);
#endif
}

#ifdef BUZZ_IMAGE7_SIMD_AVX2
bool buzz_image7_try_simd(spu_context*);
#endif

namespace {

std::atomic<long> g_compared{0}, g_mismatched{0};
#ifdef BUZZ_IMAGE7_SIMD_AVX2
const bool g_simd = !getenv("BUZZ_IMAGE7_SIMD") || strcmp(getenv("BUZZ_IMAGE7_SIMD"), "0");

void run_simd(spu_context* ctx)
{
    const bool optimized = buzz_image7_try_simd(ctx);
    if (!optimized) buzz_image7_8638_native(ctx);
    if (buzz_image7_mode == 2) {
        static std::atomic<unsigned> hits{0}, misses{0};
        if (optimized) hits.fetch_add(1, std::memory_order_relaxed);
        else misses.fetch_add(1, std::memory_order_relaxed);
        const unsigned a = hits.load(std::memory_order_relaxed);
        const unsigned b = misses.load(std::memory_order_relaxed);
        if ((a + b) % 20000 == 0)
            fprintf(stderr, "[image7-simd] optimized=%u fallback=%u\n", a, b);
    }
}
#endif

struct Image7Init {
    Image7Init()
    {
#ifdef BUZZ_IMAGE7_SIMD_AVX2
        fprintf(stderr, "[image7-simd] enabled=%d (qualified layout 0x86B8 only)\n", g_simd);
#endif
        if (const char* e = getenv("BUZZ_IMAGE7")) buzz_image7_mode = atoi(e);
        fprintf(stderr, "[image7] conversion loop 0x8638: mode=%d (0 lift, 1 native RTZ, 2 compare)\n",
                buzz_image7_mode);
    }
} g_init;

/* Same environment as spurs_job_enter gives the native RTZ images. */
inline void run_native(spu_context* ctx, void (*native)(spu_context*), unsigned layout_pc)
{
#ifdef BUZZ_IMAGE7_SIMD_AVX2
    // Older generated wrappers carry no layout proof and keep the original path.
    if (g_simd && layout_pc == 0x86b8 && native == buzz_image7_8638_native)
        native = run_simd;
#else
    (void)layout_pc;
#endif
    buzz_spu_scoped_rtz(ctx, native);
}

} // namespace

void buzz_image7_run(spu_context* ctx, void (*reference)(spu_context*), void (*native)(spu_context*))
{
    buzz_image7_run_layout(ctx, reference, native, 0);
}

void buzz_image7_run_layout(spu_context* ctx, void (*reference)(spu_context*),
                           void (*native)(spu_context*), unsigned layout_pc)
{
    if (buzz_image7_mode != 2) {
        run_native(ctx, native, layout_pc);
        return;
    }
#ifdef _WIN32
    auto* copy = static_cast<spu_context*>(_aligned_malloc(sizeof(spu_context), 16));
#else
    auto* copy = static_cast<spu_context*>(aligned_alloc(16, sizeof(spu_context)));
#endif
    if (!copy) {
        fprintf(stderr, "[image7] compare allocation failed; using reference\n");
        reference(ctx);
        return;
    }
    memcpy(copy, ctx, sizeof(spu_context));
    copy->ls = copy->ls_store;
    memcpy(copy->ls, ctx->ls, SPU_LS_SIZE);
    void (*const incoming)(spu_context*) = g_spu_trampoline_fn;
    run_native(copy, native, layout_pc);
    void (*const native_next)(spu_context*) = g_spu_trampoline_fn;
    g_spu_trampoline_fn = incoming;
    reference(ctx);
    const long n = ++g_compared;
    const bool same = !memcmp(ctx->gpr, copy->gpr, sizeof(ctx->gpr)) &&
                      !memcmp(ctx->ls, copy->ls, SPU_LS_SIZE) && ctx->pc == copy->pc &&
                      native_next == g_spu_trampoline_fn;
    if (!same && ++g_mismatched <= 16) {
        int reg = -1;
        for (int r = 0; r < 128 && reg < 0; r++)
            if (memcmp(&ctx->gpr[r], &copy->gpr[r], 16)) reg = r;
        uint32_t byte = 0;
        while (byte < SPU_LS_SIZE && ctx->ls[byte] == copy->ls[byte]) byte++;
        fprintf(stderr, "[image7] MISMATCH call %ld: first register r%d, first LS byte %05X\n", n, reg, byte);
    }
    if (n % 20000 == 0)
        fprintf(stderr, "[image7] compared %ld calls, %ld mismatched\n", n, g_mismatched.load());
#ifdef _WIN32
    _aligned_free(copy);
#else
    free(copy);
#endif
}
