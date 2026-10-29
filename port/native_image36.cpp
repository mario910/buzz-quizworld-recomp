/* Scoped RTZ SIMD for image36 0x06D8. Mode 0: reference,
 * 1: native (default), 2: full-state comparison retaining the reference result.
 * The generator rejects channels/calls/stops/double arithmetic in the leaf:
 * native execution cannot longjmp out with the host FP environment changed. */
#include "native_image36.h"
#include <immintrin.h>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>

extern "C" { int buzz_image36_mode = 1; }
namespace {
std::atomic<unsigned> compared{0}, mismatched{0}, seen{0}, dumped{0};
std::string dump_dir;
unsigned dump_every = 1000, dump_max = 32;
struct Init {
    Init() {
        if (const char* value = getenv("BUZZ_IMAGE36")) buzz_image36_mode = atoi(value);
        if (buzz_image36_mode < 0 || buzz_image36_mode > 2) buzz_image36_mode = 0;
        if (const char* value = getenv("BUZZ_IMAGE36_DUMP")) dump_dir = value;
        if (const char* value = getenv("BUZZ_IMAGE36_DUMP_EVERY")) dump_every = (unsigned)strtoul(value, nullptr, 10);
        if (const char* value = getenv("BUZZ_IMAGE36_DUMP_MAX")) dump_max = (unsigned)strtoul(value, nullptr, 10);
        if (!dump_every) dump_every = 1;
        fprintf(stderr, "[image36] loop 0x06D8: mode=%d (0 reference, 1 native RTZ, 2 full compare)\n", buzz_image36_mode);
    }
} init;

void native_scoped(spu_context* ctx, void (*native)(spu_context*))
{
    const unsigned saved = _mm_getcsr();
    _mm_setcsr((saved & ~0x6000u) | 0x6000u | 0x8040u | 0x1f80u);
    native(ctx);
    _mm_setcsr(saved);
}

void state(FILE* file, const spu_context* ctx)
{
    fwrite(ctx->gpr, sizeof(ctx->gpr), 1, file);
    fwrite(ctx->ls, SPU_LS_SIZE, 1, file);
}

FILE* capture_before(const spu_context* ctx)
{
    if (dump_dir.empty() || seen.fetch_add(1, std::memory_order_relaxed) % dump_every) return nullptr;
    unsigned slot = dumped.fetch_add(1, std::memory_order_relaxed);
    if (slot >= dump_max) return nullptr;
    char name[64];
    snprintf(name, sizeof name, "/image36_06d8_%06u.bin", slot);
    FILE* file = fopen((dump_dir + name).c_str(), "wb");
    if (file) state(file, ctx);
    return file;
}

bool same_state(const spu_context* a, const spu_context* b)
{
    // Every GPR, every active LS byte and ALL context metadata. Only the LS
    // pointer/private backing store are excluded: the clone owns its own LS.
    constexpr size_t metadata = offsetof(spu_context, mfc_private);
    return !memcmp(a, b, offsetof(spu_context, ls_store)) &&
           !memcmp(a->ls, b->ls, SPU_LS_SIZE) &&
           !memcmp(reinterpret_cast<const char*>(a) + metadata,
                   reinterpret_cast<const char*>(b) + metadata, sizeof(*a) - metadata);
}
} // namespace

void buzz_image36_run(spu_context* ctx, void (*reference)(spu_context*), void (*native)(spu_context*))
{
    if (buzz_image36_mode == 0) { reference(ctx); return; }
    if (buzz_image36_mode == 1 && dump_dir.empty()) { native_scoped(ctx, native); return; }
    FILE* file = capture_before(ctx);
    if (buzz_image36_mode != 2) {
        native_scoped(ctx, native);
    } else {
        static thread_local std::unique_ptr<spu_context> copy;
        if (!copy) copy.reset(new (std::nothrow) spu_context);
        if (!copy) {
            mismatched.fetch_add(1, std::memory_order_relaxed);
            fprintf(stderr, "[image36] compare allocation failed; using reference\n");
            reference(ctx);
        } else {
            memcpy(copy.get(), ctx, sizeof(*ctx));
            copy->ls = copy->ls_store;
            memcpy(copy->ls, ctx->ls, SPU_LS_SIZE);
            auto incoming = g_spu_trampoline_fn;
            native_scoped(copy.get(), native);
            auto native_next = g_spu_trampoline_fn;
            g_spu_trampoline_fn = incoming;
            reference(ctx);
            unsigned count = compared.fetch_add(1, std::memory_order_relaxed) + 1;
            if (!same_state(ctx, copy.get()) || native_next != g_spu_trampoline_fn) {
                unsigned failures = mismatched.fetch_add(1, std::memory_order_relaxed) + 1;
                if (failures <= 16) {
                    int reg = -1;
                    for (int r = 0; r < 128 && reg < 0; ++r)
                        if (memcmp(&ctx->gpr[r], &copy->gpr[r], 16)) reg = r;
                    unsigned byte = 0;
                    while (byte < SPU_LS_SIZE && ctx->ls[byte] == copy->ls[byte]) ++byte;
                    fprintf(stderr, "[image36] MISMATCH call=%u first-reg=r%d first-LS=%05X pc=%05X/%05X\n",
                            count, reg, byte, ctx->pc, copy->pc);
                }
            }
            if (!(count % 4096)) buzz_image36_report();
        }
    }
    if (file) { state(file, ctx); fclose(file); }
}

unsigned buzz_image36_mismatches(void) { return mismatched.load(std::memory_order_relaxed); }
void buzz_image36_report(void)
{
    fprintf(stderr, "[image36] compared=%u mismatched=%u\n",
            compared.load(std::memory_order_relaxed), buzz_image36_mismatches());
}
