/* Image 7 (Sofdec YUV->RGB consumer) conversion loop 0x8638, native float.
 * The generated wrapper (generate-image7-native.py, build-windows.ps1
 * -NativeImage7) hands every call here.
 *
 *   BUZZ_IMAGE7=0  lifted reference with the exact scalar helpers
 *   BUZZ_IMAGE7=1  the same body built with the native RTZ helpers, run with
 *                  MXCSR RZ+FTZ+DAZ for exactly this call (default)
 *   BUZZ_IMAGE7=2  both, compared (registers, local store, pc, next block);
 *                  the reference result is kept
 *
 * The scope is the whole call: 0x8638 has no channel access, stop or nested
 * call, so nothing can leave it by longjmp with the environment changed. */
#include "spu_context.h"

#include <immintrin.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <malloc.h>
#include <windows.h>
#endif

extern "C" {
int buzz_image7_mode = 1;
void buzz_image7_run(spu_context* ctx, void (*reference)(spu_context*), void (*native)(spu_context*));
}

namespace {

volatile long g_compared = 0, g_mismatched = 0;

struct Image7Init {
    Image7Init()
    {
        if (const char* e = getenv("BUZZ_IMAGE7")) buzz_image7_mode = atoi(e);
        fprintf(stderr, "[image7] conversion loop 0x8638: mode=%d (0 lift, 1 native RTZ, 2 compare)\n",
                buzz_image7_mode);
    }
} g_init;

/* Same environment as spurs_job_enter gives the native RTZ images. */
inline void run_native(spu_context* ctx, void (*native)(spu_context*))
{
    const unsigned saved = _mm_getcsr();
    _mm_setcsr((saved & ~0x6000u) | 0x6000u | 0x8040u | 0x1f80u);
    native(ctx);
    _mm_setcsr(saved);
}

} // namespace

void buzz_image7_run(spu_context* ctx, void (*reference)(spu_context*), void (*native)(spu_context*))
{
    if (buzz_image7_mode != 2) {
        run_native(ctx, native);
        return;
    }
#ifdef _WIN32
    auto* copy = static_cast<spu_context*>(_aligned_malloc(sizeof(spu_context), 16));
#else
    auto* copy = static_cast<spu_context*>(aligned_alloc(16, sizeof(spu_context)));
#endif
    memcpy(copy, ctx, sizeof(spu_context));
    copy->ls = copy->ls_store;
    void (*const incoming)(spu_context*) = g_spu_trampoline_fn;
    run_native(copy, native);
    void (*const native_next)(spu_context*) = g_spu_trampoline_fn;
    g_spu_trampoline_fn = incoming;
    reference(ctx);
    const long n = InterlockedIncrement(&g_compared);
    const bool same = !memcmp(ctx->gpr, copy->gpr, sizeof(ctx->gpr)) &&
                      !memcmp(ctx->ls, copy->ls, SPU_LS_SIZE) && ctx->pc == copy->pc &&
                      native_next == g_spu_trampoline_fn;
    if (!same && InterlockedIncrement(&g_mismatched) <= 16) {
        int reg = -1;
        for (int r = 0; r < 128 && reg < 0; r++)
            if (memcmp(&ctx->gpr[r], &copy->gpr[r], 16)) reg = r;
        uint32_t byte = 0;
        while (byte < SPU_LS_SIZE && ctx->ls[byte] == copy->ls[byte]) byte++;
        fprintf(stderr, "[image7] MISMATCH call %ld: first register r%d, first LS byte %05X\n", n, reg, byte);
    }
    if (n % 20000 == 0)
        fprintf(stderr, "[image7] compared %ld calls, %ld mismatched\n", n, g_mismatched);
#ifdef _WIN32
    _aligned_free(copy);
#else
    free(copy);
#endif
}
