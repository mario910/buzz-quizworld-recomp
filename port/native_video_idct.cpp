#include "spu_context.h"
#include "spu_scoped_rtz.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

extern "C" void buzz_video_idct_run(spu_context* ctx, void (*reference)(spu_context*), void (*native)(spu_context*))
{
    static const int mode = [] { const char* s = std::getenv("BUZZ_VIDEO_IDCT"); return s ? std::atoi(s) : 1; }();
    if (!mode) { reference(ctx); return; }
    if (mode != 2) { buzz_spu_scoped_rtz(ctx, native); return; }
    // Allocate only in diagnostic mode. A full context in static TLS consumes
    // about 266 KiB of every Linux pthread's requested stack, including the
    // 1 MiB SPURS workers, even when comparison is disabled.
    static thread_local std::unique_ptr<spu_context> shadow;
    if (!shadow) shadow.reset(new (std::nothrow) spu_context);
    if (!shadow) { reference(ctx); return; }
    auto* copy = shadow.get();
    std::memcpy(copy, ctx, sizeof(*copy));
    copy->ls = copy->ls_store;
    std::memcpy(copy->ls, ctx->ls, SPU_LS_SIZE);
    auto incoming = g_spu_trampoline_fn;
    buzz_spu_scoped_rtz(copy, native);
    auto next = g_spu_trampoline_fn;
    g_spu_trampoline_fn = incoming;
    reference(ctx);
    const bool same = !std::memcmp(ctx->gpr, copy->gpr, sizeof(ctx->gpr)) &&
        !std::memcmp(ctx->ls, copy->ls, SPU_LS_SIZE) && ctx->pc == copy->pc && next == g_spu_trampoline_fn;
    static std::atomic<unsigned> calls{0}, mismatches{0};
    if (!same && ++mismatches <= 8) std::fprintf(stderr, "[video-idct] MISMATCH\n");
    const unsigned n = ++calls;
    if (n % 10000 == 0) std::fprintf(stderr, "[video-idct] compared=%u mismatches=%u\n", n, mismatches.load());
}
