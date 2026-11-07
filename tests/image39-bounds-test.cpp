// Differential tests against unchanged, separately compiled pinned SPU code.
#ifdef NDEBUG
#error Assertions must be enabled for this test
#endif
#include "spu_context.h"
#include "spu_pure_loop.h"
#include "spu_helpers.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <chrono>
#include <cfenv>
#include <vector>
static bool native_guard = true;
extern "C"
{
    extern int buzz_image39_bounds_mode;
    SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context *) = nullptr;
    int buzz_spu_native_request_allowed(const spu_context *)
    {
        return native_guard;
    }
    void spu_indirect_branch(spu_context *)
    {
        std::abort();
    }
    int buzz_image39_bounds(spu_context *);
    void bounds_original(spu_context *);
}
static unsigned seed = 0x91ab37cd;
static unsigned rnd()
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
struct Case
{
    u128 before[128];
    unsigned char input[SPU_LS_SIZE];
    u128 after[128];
    unsigned char output[SPU_LS_SIZE];
};
static void setup(spu_context &c, const Case &k)
{
    memset(&c, 0, sizeof c);
    c.ls = c.ls_store;
    c.image_id = 39;
    c.host_depth = 1;
    c.pc = 0x5c0;
    memcpy(c.gpr, k.before, sizeof c.gpr);
    memcpy(c.ls, k.input, SPU_LS_SIZE);
}
static void same(const spu_context &a, const spu_context &b, const char *stage, unsigned n)
{
    if (memcmp(a.gpr, b.gpr, 32) || memcmp(&a.gpr[3], &b.gpr[3], 16) ||
        memcmp(&a.gpr[80], &b.gpr[80], 48 * 16) || memcmp(a.ls, b.ls, SPU_LS_SIZE) || a.pc != b.pc)
    {
        for (unsigned r = 0; r < 128; ++r)
            if ((r < 2 || r == 3 || r >= 80) && memcmp(&a.gpr[r], &b.gpr[r], 16))
            {
                fprintf(stderr, "%s case%u register%u got%08X expected%08X\n", stage, n, r,
                        b.gpr[r]._u32[0], a.gpr[r]._u32[0]);
                break;
            }
        for (unsigned at = 0; at < SPU_LS_SIZE; ++at)
            if (a.ls[at] != b.ls[at])
            {
                fprintf(stderr, "LS %05X got%02X expected%02X\n", at, b.ls[at], a.ls[at]);
                break;
            }
        abort();
    }
    assert(a.image_id == b.image_id && a.host_depth == b.host_depth && a.status == b.status &&
           a.stop_code == b.stop_code && a.int_enable == b.int_enable);
    // Compare every remaining context byte after removing architectural output
    // and rebasing the private LS pointer. Channels, events and IRQ state stay exact.
    auto metadata = std::make_unique<spu_context>();
    memcpy(metadata.get(), &b, sizeof b);
    memcpy(metadata->gpr, a.gpr, sizeof a.gpr);
    memcpy(metadata->ls_store, a.ls_store, sizeof a.ls_store);
    metadata->ls = a.ls;
    metadata->pc = a.pc;
    assert(!memcmp(metadata.get(), &a, sizeof a));
}
int main(int argc, char **argv)
{
    auto a = std::make_unique<spu_context>();
    auto b = std::make_unique<spu_context>();
    std::vector<std::unique_ptr<Case>> cases;
    bool bench = false;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--bench"))
        {
            bench = true;
            continue;
        }
        auto k = std::make_unique<Case>();
        FILE *f = fopen(argv[i], "rb");
        assert(f);
        assert(fread(k.get(), sizeof(Case), 1, f) == 1 && fgetc(f) == EOF);
        fclose(f);
        setup(*a, *k);
        bounds_original(a.get());
        assert(!memcmp(a->gpr, k->after, sizeof a->gpr) && !memcmp(a->ls, k->output, SPU_LS_SIZE));
        assert(k->before[3]._u32[0] > 0 && k->before[3]._u32[0] <= 4096);
        cases.push_back(std::move(k));
    }
    const size_t captured = cases.size();
    if (cases.empty())
    {
        auto k = std::make_unique<Case>();
        memset(k.get(), 0, sizeof *k);
        const unsigned addresses[] = {0x5c30, 0x5c40, 0x5c80, 0x5d00,
                                      0x5d10, 0x5d20, 0x5d40, 0x5d50};
        const unsigned char masks[][16] = {
            {8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11},
            {0, 1, 2, 3, 16, 17, 18, 19, 128, 128, 128, 128, 128, 128, 128, 128},
            {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3},
            {12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15},
            {8, 9, 10, 11, 24, 25, 26, 27, 12, 13, 14, 15, 28, 29, 30, 31},
            {4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7},
            {0, 1, 2, 3, 16, 17, 18, 19, 4, 5, 6, 7, 20, 21, 22, 23},
            {0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 128, 128, 128, 128}};
        for (unsigned i = 0; i < 8; ++i)
            memcpy(k->input + addresses[i], masks[i], 16);
        // One valid synthetic seed for admission/fallback checks without captures.
        k->before[0]._u32[0] = 0x2178;
        k->before[3]._u32[0] = 8;
        k->before[5]._u32[0] = 0x10000;
        k->before[6]._u32[0] = 0x11000;
        k->before[13]._u32[0] = 0x20000;
        for (unsigned i = 0; i < 10; ++i)
        {
            k->input[0x10000 + i * 16 + 1] = 0x01;
            k->input[0x10000 + i * 16 + 2] = 0x40;
            k->input[0x11000 + i * 16 + 1] = 0x01;
            k->input[0x11000 + i * 16 + 2] = 0x80;
        }
        cases.push_back(std::move(k));
    }
    const int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    assert(!buzz_image39_bounds(nullptr));
    for (unsigned n = 0; n < captured; ++n)
        for (int mode : modes)
        {
            setup(*a, *cases[n]);
            setup(*b, *cases[n]);
            bounds_original(a.get());
            assert(!fesetround(mode));
            feraiseexcept(FE_INEXACT);
            const int flags = fetestexcept(FE_ALL_EXCEPT);
            g_spu_trampoline_fn = spu_indirect_branch;
            assert(buzz_image39_bounds(b.get()));
            assert(fegetround() == mode && fetestexcept(FE_ALL_EXCEPT) == flags);
            same(*a, *b, "captured", n);
            assert(!g_spu_trampoline_fn);
        }

    for (unsigned n = 0; n < 1200; ++n)
    {
        auto k = std::make_unique<Case>();
        for (auto &reg : k->before)
            for (auto &word : reg._u32)
                word = rnd();
        for (unsigned at = 0; at < SPU_LS_SIZE; at += 4)
        {
            unsigned v = rnd();
            memcpy(k->input + at, &v, 4);
        }
        for (unsigned at : {0x5c30u, 0x5c40u, 0x5c80u, 0x5d00u, 0x5d10u, 0x5d20u, 0x5d40u, 0x5d50u})
            memcpy(k->input + at, cases[0]->input + at, 16);
        const unsigned count = 1 + n % 33;
        k->before[0]._u32[0] = 0x2178;
        k->before[3]._u32[0] = count;
        k->before[5]._u32[0] = 0x10000;
        k->before[6]._u32[0] = 0x11000;
        k->before[13]._u32[0] = 0x20000 + 4 * (n % 4);
        auto store = [&](unsigned at, unsigned v)
        {
            k->input[at] = v >> 24;
            k->input[at + 1] = v >> 16;
            k->input[at + 2] = v >> 8;
            k->input[at + 3] = v;
        };
        for (unsigned i = 0; i < count + 2; ++i)
        {
            store(0x10000 + i * 16, 0x14000 + i * 32);
            store(0x11000 + i * 16, 0x18000 + i * 64);
        }
        if (n % 3 == 0)
        {
            // Finite ordinary-scale inputs, mixed signs and translations, not only
            // random extended-exponent values that often reject everything.
            auto value = [&]()
            {
                float f = (int)(rnd() % 20001) / 100.0f - 100.f;
                unsigned v;
                memcpy(&v, &f, 4);
                return v;
            };
            for (unsigned i = 7; i <= 12; ++i)
                for (auto &w : k->before[i]._u32)
                    w = value();
            for (unsigned i = 0; i < count + 2; ++i)
                for (unsigned j = 0; j < 8; ++j)
                    store(0x14000 + i * 32 + j * 4, value());
            for (unsigned i = 0; i < count + 2; ++i)
                for (unsigned j = 0; j < 16; ++j)
                    store(0x18000 + i * 64 + j * 4, value());
        }
        if (n % 3 == 1)
        {
            // Equality and the adjacent floats on either side of -radius. Candidate
            // indices must stay in order for fully visible, culled and mixed lists.
            for (unsigned r = 7; r <= 11; ++r)
                k->before[r] = spu_zero(), k->before[r]._u32[0] = 0x3f800000;
            k->before[12] = spu_il(0x3f800000);
            for (unsigned i = 0; i < count + 2; ++i)
            {
                for (unsigned j = 0; j < 8; ++j)
                    store(0x14000 + i * 32 + j * 4, 0);
                store(0x14000 + i * 32,
                      (i + n) % 3 == 0 ? 0xbf800001 : ((i + n) % 3 == 1 ? 0xbf800000 : 0xbf7fffff));
                store(0x14000 + i * 32 + 16, 0x3f800000);
                for (unsigned row = 0; row < 4; ++row)
                    for (unsigned col = 0; col < 4; ++col)
                        store(0x18000 + i * 64 + row * 16 + col * 4, row == col ? 0x3f800000 : 0);
            }
        }
        setup(*a, *k);
        setup(*b, *k);
        bounds_original(a.get());
        const int mode = modes[n % 4];
        assert(!fesetround(mode));
        feraiseexcept(FE_INEXACT);
        const int flags = fetestexcept(FE_ALL_EXCEPT);
        assert(buzz_image39_bounds(b.get()));
        assert(fegetround() == mode && fetestexcept(FE_ALL_EXCEPT) == flags);
        same(*a, *b, "synthetic finite/extended", n);
    }
    for (unsigned count : {4095u, 4096u})
    {
        auto k = std::make_unique<Case>();
        memset(k.get(), 0, sizeof *k);
        for (unsigned at : {0x5c30u, 0x5c40u, 0x5c80u, 0x5d00u, 0x5d10u, 0x5d20u, 0x5d40u, 0x5d50u})
            memcpy(k->input + at, cases[0]->input + at, 16);
        k->before[0]._u32[0] = 0x2178;
        k->before[3]._u32[0] = count;
        k->before[5]._u32[0] = 0x10000;
        k->before[6]._u32[0] = 0x20000;
        k->before[13]._u32[0] = 0x30000;
        for (unsigned i = 0; i < count; ++i)
        {
            k->input[0x10000 + i * 16 + 2] = 0x30;
            k->input[0x20000 + i * 16 + 2] = 0x30;
            k->input[0x20000 + i * 16 + 3] = 0x40;
        }
        setup(*a, *k);
        setup(*b, *k);
        bounds_original(a.get());
        assert(buzz_image39_bounds(b.get()));
        same(*a, *b, "maximum admitted list", count);
        assert(b->gpr[3]._u32[0] == count);
    }
    for (unsigned kind = 0; kind < 28; ++kind)
    {
        setup(*a, *cases[0]);
        if (kind == 0)
            a->gpr[3]._u32[0] = 0;
        if (kind == 1)
            a->gpr[3]._u32[0] = 4097;
        if (kind == 2)
            a->image_id = 40;
        if (kind == 3)
            a->int_enable = 1;
        if (kind == 4)
            a->host_depth = 0;
        if (kind == 5)
            a->gpr[0]._u32[0] = 0x2000;
        if (kind == 6)
            a->ls = a->ls_store + 16;
        if (kind == 7)
            a->gpr[13]._u32[0] = a->gpr[5]._u32[0];
        if (kind == 8)
            a->gpr[13]._u32[0] = a->gpr[6]._u32[0];
        if (kind == 9)
            a->gpr[13]._u32[0] = spu_ls_read128(a.get(), a->gpr[5]._u32[0])._u32[0];
        if (kind == 10)
            a->gpr[13]._u32[0] = spu_ls_read128(a.get(), a->gpr[6]._u32[0])._u32[0];
        if (kind == 11)
            a->gpr[13]._u32[0] = 0x5d00;
        if (kind == 12)
        {
            a->gpr[13]._u32[0] = SPU_LS_SIZE - 4;
            a->gpr[3]._u32[0] = 2;
        }
        if (kind == 13)
            a->gpr[13]._u32[0] |= 1;
        if (kind == 14)
            a->ls[0x5d10] ^= 1;
        if (kind == 15)
            a->event_mask = 1;
        if (kind == 16)
            a->policy_mode = 1;
        if (kind == 17)
            a->resident_ovl = 1;
        if (kind == 18)
            a->irq_saved = 1;
        if (kind == 19)
            a->irq_frame = reinterpret_cast<void *>(1);
        if (kind == 20)
            a->list_stall_mask = 1;
        if (kind == 21)
            a->gpr[5]._u32[0] = SPU_LS_SIZE;
        if (kind == 22)
            a->gpr[6]._u32[0] |= 1;
        if (kind == 23)
            memset(a->ls + a->gpr[5]._u32[0], 0xff, 4);
        if (kind == 24)
            memset(a->ls + a->gpr[6]._u32[0], 0xff, 4);
        if (kind == 25)
            buzz_image39_bounds_mode = 0;
        if (kind == 26)
            native_guard = false;
        if (kind == 27)
            a->gpr[13]._u32[0] = 0xfffffffcu;

        memcpy(b.get(), a.get(), sizeof *a);
        assert(!buzz_image39_bounds(a.get()));
        assert(!memcmp(a.get(), b.get(), sizeof *a));
        buzz_image39_bounds_mode = 1;
        native_guard = true;
    }
    fesetround(FE_TONEAREST);
    printf("PASS image39 bounds: %zu captured original returns, full LS/live ABI/PC/controls, four "
           "incoming FP states,1200 finite/extended/boundary states,4095/4096 items,28 fallback "
           "gates\n",
           captured);
    if (!bench)
        return 0;
    double ref_ns = 0, native_ns = 0;
    for (unsigned repeat = 0; repeat < 256; ++repeat)
        for (const auto &k : cases)
        {
            setup(*a, *k);
            auto start = std::chrono::steady_clock::now();
            bounds_original(a.get());
            auto end = std::chrono::steady_clock::now();
            ref_ns += std::chrono::duration<double, std::nano>(end - start).count();
            setup(*b, *k);
            start = std::chrono::steady_clock::now();
            assert(buzz_image39_bounds(b.get()));
            end = std::chrono::steady_clock::now();
            native_ns += std::chrono::duration<double, std::nano>(end - start).count();
        }
    printf(
        "Kernel-only reference/native ns %.0f/%.0f; not game FPS or hardware evidence under QEMU\n",
        ref_ns, native_ns);
}
