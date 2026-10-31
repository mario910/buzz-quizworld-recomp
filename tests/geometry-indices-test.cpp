#define SPU_LS_DEBUG_HOOKS 0
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <chrono>
extern "C"
{
    SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context *);
    int g_spu_ls_watch_n = 0, buzz_geometry_debug = 0;
    void buzz_job_6eba_spu_func_00007DCC(spu_context *) {}
    void buzz_job_6eba_spu_func_000079A8(spu_context *) {}
}
#include "../analysis/native-geometry/phase2/test-indices-kernels.inc"
static uint32_t rng = 0x78a80041;
static uint32_t word()
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static void compare(spu_context *a, spu_context *b)
{
    buzz_job_6eba_spu_func_000078A8_reference(a);
    assert((a->pc == 0x79a8 && g_spu_trampoline_fn == buzz_job_6eba_spu_func_000079A8) ||
           (a->pc == 0x7dcc && g_spu_trampoline_fn == buzz_job_6eba_spu_func_00007DCC));
    auto next = g_spu_trampoline_fn;
    g_spu_trampoline_fn = nullptr;
    assert(buzz_job_6eba_spu_func_000078A8_native(b));
    auto diff = buzz::compare_function_state(*a, *b);
    if (!diff.exact() || next != g_spu_trampoline_fn)
    {
        fprintf(stderr, "INDEX DIFF r%d LS%05X %u bytes state%s\n", diff.first_register, diff.first_ls,
                diff.ls_bytes, diff.first_state ? diff.first_state : "equal");
        for (unsigned r = 0; r < 128; ++r)
            if (memcmp(&a->gpr[r], &b->gpr[r], 16))
            {
                fprintf(stderr, "r%u ref", r);
                for (auto w : a->gpr[r]._u32)
                    fprintf(stderr, " %08X", w);
                fprintf(stderr, " native");
                for (auto w : b->gpr[r]._u32)
                    fprintf(stderr, " %08X", w);
                fprintf(stderr, "\n");
            }
        abort();
    }
}
int main(int argc, char **argv)
{
    auto *seed = new spu_context{};
    seed->ls = seed->ls_store;
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
        seed->gpr[18]._u32[0] = test < 400 ? 4 + test % 61 : 256;
        seed->gpr[17]._u32[0] = 0x12000;
        seed->gpr[23]._u32[0] = test % 2 ? 32 : 0;
        auto *a = buzz::clone_function_state(seed);
        auto *b = buzz::clone_function_state(seed);
        compare(a, b);
        buzz::release_function_state(a);
        buzz::release_function_state(b);
    }
    for (unsigned variant = 0; variant < 11; ++variant)
    {
        auto *rejected = buzz::clone_function_state(seed);
        if (variant == 0)
            rejected->gpr[18]._u32[0] = 0;
        if (variant == 1)
            rejected->gpr[18]._u32[0] = 3;
        if (variant == 2)
            rejected->gpr[18]._u32[0] = 4097;
        if (variant == 3)
            ++rejected->gpr[17]._u32[0];
        if (variant == 4)
            rejected->gpr[17]._u32[0] = 0xe670;
        if (variant == 5)
            rejected->gpr[23]._u32[0] = 16;
        if (variant == 6)
            rejected->gpr[17]._u32[0] = SPU_LS_SIZE - 16;
        if (variant == 7)
            rejected->gpr[23]._u32[0] = 0xffffffff;
        if (variant == 8)
        {
            rejected->int_enable = 1;
            rejected->event_mask = rejected->event_status = 1;
        }
        if (variant == 9)
            rejected->ls = seed->ls;
        if (variant == 10)
            buzz_geometry_debug = 1;
        auto *saved = buzz::clone_function_state(rejected);
        auto continuation = g_spu_trampoline_fn;
        assert(!buzz_job_6eba_spu_func_000078A8_native(rejected));
        assert(buzz::compare_function_state(*saved, *rejected).exact() &&
               continuation == g_spu_trampoline_fn);
        buzz_geometry_debug = 0;
        buzz::release_function_state(saved);
        buzz::release_function_state(rejected);
    }
    puts("78A8: 440 strict states, 4..256 iterations, wrapping input/arbitrary selectors, eleven "
         "unchanged-state rejections PASS");
    if (argc > 1)
    {
        FILE *f = fopen(argv[1], "rb");
        assert(f);
        assert(fread(seed->gpr, sizeof(seed->gpr), 1, f) == 1);
        assert(fread(seed->ls, SPU_LS_SIZE, 1, f) == 1);
        fclose(f);
        auto *a = buzz::clone_function_state(seed);
        auto *b = buzz::clone_function_state(seed);
        compare(a, b);
        buzz::release_function_state(a);
        buzz::release_function_state(b);
        for (unsigned round = 0; round < 4; ++round)
            for (unsigned pass = 0; pass < 2; ++pass)
            {
                bool native = (pass ^ (round & 1)) != 0;
                auto *c = buzz::clone_function_state(seed);
                auto start = std::chrono::steady_clock::now();
                for (unsigned i = 0; i < 100000; ++i)
                {
                    memcpy(c->gpr, seed->gpr, sizeof(seed->gpr));
                    memcpy(c->ls + seed->gpr[17]._u32[0], seed->ls + seed->gpr[17]._u32[0],
                           seed->gpr[23]._u32[0] + (seed->gpr[18]._u32[0] - 1) * 32);
                    if (native)
                        buzz_job_6eba_spu_func_000078A8_native(c);
                    else
                        buzz_job_6eba_spu_func_000078A8_reference(c);
                }
                printf("INDEX micro %u %s %.1f ns\n", round, native ? "native" : "reference",
                       std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start)
                               .count() /
                           100000);
                buzz::release_function_state(c);
            }
    }
    delete seed;
}
