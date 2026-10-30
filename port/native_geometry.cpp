/* Runtime gates and strict paired checks for the phase-2 image-41 kernels. */
#include "spu_context.h"
#include "native_function_check.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>

extern "C" { int buzz_geometry_debug = 0; unsigned buzz_geometry_active = 0; }

namespace {
constexpr unsigned pcs[] = {0x92A0, 0xB940, 0x4AB0, 0x7A90, 0x51E0, 0xCB78,
                           0x78A8, 0x9148, 0x97A8, 0x4750, 0x0988, 0x4808, 0x28F0};
constexpr unsigned count = sizeof(pcs) / sizeof(*pcs);
#ifndef BUZZ_GEOMETRY_AVAILABLE
#define BUZZ_GEOMETRY_AVAILABLE 0
#endif
constexpr unsigned available = BUZZ_GEOMETRY_AVAILABLE;
struct Counters { std::atomic<unsigned long long> native{0}, fallback{0}, checked{0}, failed{0}; } counters[count];
int mode = 1;
unsigned kernels = 1u & available; // Bit 0 passed corpus/live/render and both scene reversals.

struct Init {
    Init() {
        buzz_geometry_debug = std::getenv("SPU_LS_LOWREAD") || std::getenv("SPU_LS_WATCH") || std::getenv("SPU_SMC_WATCH");
        if (const char* e = std::getenv("BUZZ_GEOMETRY")) {
            if (e[0] < '0' || e[0] > '2' || e[1]) std::abort();
            mode = e[0] - '0';
        }
        if (const char* e = std::getenv("BUZZ_GEOMETRY_KERNELS")) {
            char* end;
            unsigned long value = std::strtoul(e, &end, 0);
            if (!*e || *end || value >= (1u << count) || (value & ~available)) {
                std::fprintf(stderr, "[geometry-native] invalid/unavailable mask %s; built mask=0x%X\n", e, available);
                std::abort();
            }
            kernels = (unsigned)value;
        }
        std::fprintf(stderr, "[geometry-native] mode=%d kernels=0x%X\n", mode, kernels);
        buzz_geometry_active = mode ? kernels : 0;
    }
} init;
}

extern "C" void buzz_geometry_report()
{
    if (!mode) return;
    for (unsigned k = 0; k < count; ++k) if (kernels & (1u << k))
        std::fprintf(stderr, "[geometry-native] %04X native=%llu fallback=%llu checked=%llu failed=%llu\n",
            pcs[k], counters[k].native.load(), counters[k].fallback.load(),
            counters[k].checked.load(), counters[k].failed.load());
}

extern "C" int buzz_geometry_check_failures()
{
    for (unsigned k = 0; k < count; ++k) if ((kernels & (1u << k)) && mode == 2)
        if (counters[k].failed.load() || !counters[k].checked.load()) return 1;
    return 0;
}

extern "C" void buzz_geometry_run(spu_context* ctx, void (*reference)(spu_context*),
                                  int (*native)(spu_context*), unsigned kernel)
{
    if (kernel >= count) std::abort();
    if (!mode || !(kernels & (1u << kernel))) { reference(ctx); return; }
    auto& c = counters[kernel];
    if (mode == 1) {
        if (native(ctx)) ++c.native;
        else { ++c.fallback; reference(ctx); }
        return;
    }
    bool handled = buzz::check_function(ctx, reference, native,
        [&](const spu_context& ref, const spu_context& nat, bool continuation) {
            auto d = buzz::compare_function_state(ref, nat);
            const auto n = ++c.checked;
            if (!d.exact() || !continuation) {
                ++c.failed;
                std::fprintf(stderr, "[geometry-native] %04X DIFF: r%d byte %d (%u bytes), LS %05X (%u bytes), state=%s continuation=%s\n",
                    pcs[kernel], d.first_register, d.first_register_byte, d.register_bytes,
                    d.first_ls, d.ls_bytes, d.first_state ? d.first_state : "equal", continuation ? "equal" : "DIFF");
                std::abort();
            }
            if (n == 256 || n % 100000 == 0)
                std::fprintf(stderr, "[geometry-native] %04X strict full-state %llu PASS\n", pcs[kernel], n);
        });
    if (!handled) ++c.fallback;
}
