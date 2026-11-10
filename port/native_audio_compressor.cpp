#include "spu_context.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

extern "C" void buzz_audio_compressor_run(spu_context* ctx, void (*reference)(spu_context*),
                                         void (*native)(spu_context*), unsigned entry)
{
    static const int mode = [] { const char* s = std::getenv("BUZZ_AUDIO_COMPRESSOR"); return s ? std::atoi(s) : 1; }();
    if (mode == 0) { reference(ctx); return; }
    if (mode != 2) { native(ctx); return; }
    // Diagnostic mode retains original results. Bound costly complete-state
    // comparisons; this mode is not a performance measurement.
    static std::atomic<unsigned> count[11]{};
    const unsigned index = entry == 0x2FF18 ? 0 : entry == 0x301D8 ? 1 :
                           entry == 0xB078 ? 2 : entry == 0xC758 ? 3 : entry == 0xC8D4 ? 4 :
                           entry == 0x4E00 ? 5 : entry == 0x4F24 ? 6 : entry == 0x513C ? 7 : entry == 0x51F8 ? 8 :
                           entry == 0x36538 ? 9 : entry == 0x36710 ? 10 : 11;
    if (index == 11) { reference(ctx); return; }
    const unsigned call = count[index].fetch_add(1, std::memory_order_relaxed);
    if (call >= 16 || ctx->ls != ctx->ls_store) { reference(ctx); return; }
    // Keep the large diagnostic context OUT of every ordinary worker's TLS
    // block. Only a thread actually running comparisons allocates one.
    static thread_local std::unique_ptr<spu_context> storage;
    if (!storage) storage.reset(new (std::nothrow) spu_context);
    if (!storage) { reference(ctx); return; }
    auto* shadow = storage.get();
    std::memcpy(shadow, ctx, sizeof *shadow);
    shadow->ls = shadow->ls_store;
    const auto incoming = g_spu_trampoline_fn;
    native(shadow);
    const auto next = g_spu_trampoline_fn;
    g_spu_trampoline_fn = incoming;
    reference(ctx);
    shadow->ls = ctx->ls;
    const bool same = !std::memcmp(shadow, ctx, sizeof *shadow) && next == g_spu_trampoline_fn;
    std::fprintf(stderr, "[audio-compressor] entry=%05X compare=%u %s\n", entry, call + 1, same ? "PASS" : "MISMATCH");
}
