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
    static constexpr unsigned entries[] = {0x2FF18, 0x301D8, 0xB078, 0xC758, 0xC8D4, 0x4E00,
        0x4F24, 0x513C, 0x51F8, 0x36538, 0x36710, 0x4A4C, 0x47B4, 0xAC1C, 0x8400, 0x5524,
        0x34770, 0x34840, 0x344E0, 0x363E0, 0x53DC, 0xA290, 0xA214,
        0x34B58, 0x34930, 0x34608, 0x342E8, 0x35600, 0x5048,
        0xA504, 0xC65C, 0x59C0, 0x8578, 0x2FCD8, 0x358B0, 0x33DE8,
        0x34410, 0x352D8, 0x341E0, 0x35030, 0x34F38, 0x55DC, 0x57EC, 0x36370, 0x368B8,
        0x3614C, 0x37AE8, 0x36EB0, 0x37750, 0x37148, 0x37590};
    constexpr unsigned size = sizeof(entries) / sizeof(entries[0]);
    static std::atomic<unsigned> count[size]{};
    unsigned index = 0;
    while (index < size && entries[index] != entry) ++index;
    if (index == size) { reference(ctx); return; }
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
