/* Quantized output: full state, dispatcher boundary, FP edges and rejection. */
#define SPU_LS_DEBUG_HOOKS 0
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#if defined(__aarch64__)
#include "../port/spu_arm_fp_state.h"
#endif
#include "../port/native_function_check.h"
#include <cassert>
#include <chrono>
#include <cstdio>
extern "C"
{
    SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context *);
    int g_spu_ls_watch_n = 0, buzz_geometry_debug = 0;
    void buzz_job_6eba_spu_func_00005008(spu_context *);
    void buzz_job_6eba_spu_func_000051E0(spu_context *);
    void buzz_job_6eba_spu_func_000050C8(spu_context *);
    void buzz_job_6eba_spu_func_000051C8(spu_context *);
    void buzz_job_6eba_spu_func_00004610(spu_context *) {}
}
#include "../analysis/native-geometry/phase2/test-compact-kernels.inc"
extern "C"
{
    void buzz_job_6eba_spu_func_00005008(spu_context *ctx) { buzz_job_6eba_spu_func_00005008_setup(ctx); }
    void buzz_job_6eba_spu_func_000051E0(spu_context *ctx) { buzz_job_6eba_spu_func_000051E0_reference(ctx); }
    void buzz_job_6eba_spu_func_000050C8(spu_context *ctx) { buzz_job_6eba_spu_func_000050C8_reference(ctx); }
    void buzz_job_6eba_spu_func_000051C8(spu_context *ctx) { buzz_job_6eba_spu_func_000051C8_reference(ctx); }
}
static uint32_t rng = 0x51e00041;
static uint32_t word()
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static void dispatch_group(spu_context *ctx)
{
    g_spu_trampoline_fn = buzz_job_6eba_spu_func_00005008;
    do
    {
        auto next = g_spu_trampoline_fn;
        assert(next);
        g_spu_trampoline_fn = nullptr;
        next(ctx);
    } while (ctx->pc != 0x4610);
    assert(g_spu_trampoline_fn == buzz_job_6eba_spu_func_00004610);
}
static void compare(spu_context *seed, bool native)
{
    auto *ref = buzz::clone_function_state(seed);
    auto *check = buzz::clone_function_state(seed);
    buzz_job_6eba_spu_func_00005008_reference(ref);
    assert(ref->pc == 0x4610 && g_spu_trampoline_fn == buzz_job_6eba_spu_func_00004610);
    auto continuation = g_spu_trampoline_fn;
    g_spu_trampoline_fn = nullptr;
    if (native)
        assert(buzz_job_6eba_spu_func_00005008_native(check));
    else
        dispatch_group(check);
    auto diff = buzz::compare_function_state(*ref, *check);
    if (!diff.exact() || continuation != g_spu_trampoline_fn)
    {
        fprintf(stderr, "COMPACT DIFF r%d LS=%05X (%u bytes), state=%s\n", diff.first_register, diff.first_ls,
                diff.ls_bytes, diff.first_state ? diff.first_state : "equal");
        abort();
    }
    buzz::release_function_state(ref);
    buzz::release_function_state(check);
}
int main(int argc, char **argv)
{
#if defined(__aarch64__)
    const auto previous = buzz_arm_fp_save();
    buzz_arm_fp_enter_rtz(previous);
#else
    _mm_setcsr((_mm_getcsr() & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO | 0x8040);
#endif
    auto *seed = new spu_context{};
    seed->ls = seed->ls_store;
    const uint32_t edges[] = {0,          0x80000000, 1,          0x007fffff, 0x00800000, 0x3f800000,
                              0xbf800000, 0x3f7fffff, 0x4f000000, 0xcf000000, 0x7f800000, 0xff800000,
                              0x7fffffff, 0xffffffff, 0x7fc00000, 0x3a800000};
    for (unsigned test = 0; test < 440; ++test)
    {
        for (auto &r : seed->gpr)
            for (auto &w : r._u32)
                w = word();
        for (unsigned i = 0; i < SPU_LS_SIZE; i += 4)
        {
            auto w = word();
            memcpy(seed->ls + i, &w, 4);
        }
        compact_seed_constants(seed);
        unsigned columns = 2 + test % 15, column = test % (columns - 1), stride = 4 + 4 * (test % 16),
                 groups = 1 + test % 6;
        seed->gpr[44]._u32[0] = column;
        seed->gpr[60]._u32[0] = columns;
        seed->gpr[47]._u32[0] = column * 16;
        seed->gpr[19]._u32[0] = 0x20000;
        seed->gpr[28]._u32[0] = 256;
        seed->gpr[27]._u32[0] = 512;
        seed->gpr[26]._u32[0] = 768;
        for (unsigned j = 0; j < 4; ++j)
        {
            seed->gpr[52]._u32[j] = 0x10000 + test % 16 + j * 256;
            seed->gpr[32]._u32[j] = 0x10000 + test % 16 + (groups - 1) * 1024 + 255;
            seed->gpr[55]._u32[j] = stride;
            seed->gpr[53]._u32[j] = 4 * (test % 17);
            seed->gpr[29]._u32[j] = 1024;
            seed->gpr[31]._u32[j] = test % 3 ? 64 * stride : 16;
            seed->gpr[30]._u32[j] = 0x16000 + (test % 4) * 4;
        }
        if (test >= 400)
        {
            const uint32_t scale[4] = {0x3f7fe000, 0x3f7fe000, 0x3f7fc000, 0};
            const uint32_t bias[4] = {0xba000000, 0xba000000, 0xba800000, 0};
            memcpy(&seed->gpr[35], scale, 16);
            memcpy(&seed->gpr[34], bias, 16);
            for (unsigned i = 0; i < 0x3000; i += 4)
            {
                uint32_t value = SPU_BSWAP32(edges[(i / 4 + test) % 16]);
                memcpy(seed->ls + 0x10000 + i, &value, 4);
            }
        }
        compare(seed, true);
        compare(seed, false);
    }
    for (unsigned offset = 13; offset <= 15; ++offset)
    {
        for (unsigned lane = 0; lane < 4; ++lane)
            seed->gpr[53]._u32[lane] = offset;
        compare(seed, false); // Retained straddled path agrees with normal dispatch.
    }
    for (unsigned lane = 0; lane < 4; ++lane)
        seed->gpr[53]._u32[lane] = 4;
    for (unsigned variant = 0; variant < 22; ++variant)
    {
        auto *rejected = buzz::clone_function_state(seed);
        if (variant == 0)
            rejected->gpr[60]._u32[0] = 0;
        if (variant == 1)
            rejected->gpr[44]._u32[0] = rejected->gpr[60]._u32[0] - 1;
        if (variant == 2)
            ++rejected->gpr[47]._u32[0];
        if (variant == 3)
            ++rejected->gpr[19]._u32[0];
        if (variant == 4)
            rejected->gpr[19]._u32[0] = 0xe670;
        if (variant == 5)
            rejected->gpr[55]._u32[0] = 0;
        if (variant == 6)
            rejected->gpr[55]._u32[0] = 3;
        if (variant == 7)
            rejected->gpr[53]._u32[0] = 13;
        if (variant == 8)
            rejected->gpr[29]._u32[0] = 0;
        if (variant == 9)
            ++rejected->gpr[31]._u32[0];
        if (variant == 10)
            rejected->gpr[36]._u32[0] ^= 1;
        if (variant == 11)
            rejected->gpr[49]._u32[0] ^= 1;
        if (variant == 12)
            rejected->gpr[32]._u32[0] = SPU_LS_SIZE;
        if (variant == 13)
            rejected->gpr[52]._u32[0] = SPU_LS_SIZE - 16;
        if (variant == 14)
            ++rejected->gpr[28]._u32[0];
        if (variant == 15)
            ++rejected->gpr[55]._u32[3];
        if (variant == 16)
            rejected->gpr[19]._u32[0] = rejected->gpr[52]._u32[0] & ~15u;
        if (variant == 17)
            for (unsigned j = 0; j < 4; ++j)
                rejected->gpr[30]._u32[j] =
                    rejected->gpr[55]._u32[0] * rejected->gpr[44]._u32[0] + rejected->gpr[53]._u32[0];
        if (variant == 18)
        {
            rejected->int_enable = 1;
            rejected->event_mask = rejected->event_status = 1;
        }
        if (variant == 19)
            rejected->ls = seed->ls;
        if (variant == 20)
            buzz_geometry_debug = 1;
        if (variant == 21)
            rejected->gpr[52]._u32[3] = 0xffffffff;
        auto *saved = buzz::clone_function_state(rejected);
        auto continuation = g_spu_trampoline_fn;
        assert(!buzz_job_6eba_spu_func_00005008_native(rejected));
        assert(buzz::compare_function_state(*saved, *rejected).exact() &&
               continuation == g_spu_trampoline_fn);
        buzz_geometry_debug = 0;
        buzz::release_function_state(saved);
        buzz::release_function_state(rejected);
    }
    puts("5008/51E0: 440 strict/dispatch states, FP edges, straddled fallback dispatch and 22 "
         "unchanged-state rejections PASS");
    if (argc > 1)
    {
        FILE *f = fopen(argv[1], "rb");
        assert(f);
        assert(fread(seed->gpr, sizeof(seed->gpr), 1, f) == 1);
        assert(fread(seed->ls, SPU_LS_SIZE, 1, f) == 1);
        fclose(f);
        compare(seed, true);
        compare(seed, false);
        for (unsigned round = 0; round < 4; ++round)
            for (unsigned pass = 0; pass < 2; ++pass)
            {
                bool native = (pass ^ (round & 1)) != 0;
                auto *state = buzz::clone_function_state(seed);
                auto start = std::chrono::steady_clock::now();
                for (unsigned i = 0; i < 100000; ++i)
                {
                    memcpy(state->gpr, seed->gpr, sizeof(seed->gpr));
                    if (native)
                        buzz_job_6eba_spu_func_00005008_native(state);
                    else
                        buzz_job_6eba_spu_func_00005008_reference(state);
                }
                printf("COMPACT micro %u %s %.1f ns\n", round, native ? "native" : "reference",
                       std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start)
                               .count() /
                           100000);
                buzz::release_function_state(state);
            }
    }
    delete seed;
}
