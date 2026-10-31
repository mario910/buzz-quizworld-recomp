/* Full-state fill comparison, including counter wrap and partial quads. */
#define SPU_LS_DEBUG_HOOKS 0
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
int g_spu_ls_watch_n = 0, buzz_geometry_debug = 0;
void buzz_job_6eba_spu_func_00002910(spu_context*) {}
}
#include "../analysis/native-geometry/phase2/test-fill-kernels.inc"
static uint32_t rng = 0x28f00041;
static uint32_t word() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static void compare(spu_context* seed)
{
    auto* ref = buzz::clone_function_state(seed);
    auto* nat = buzz::clone_function_state(seed);
    buzz_job_6eba_spu_func_000028F0_reference(ref);
    assert(ref->pc == 0x2910 && g_spu_trampoline_fn == buzz_job_6eba_spu_func_00002910);
    auto continuation = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr;
    assert(buzz_job_6eba_spu_func_000028F0_native(nat));
    assert(buzz::compare_function_state(*ref, *nat).exact() && continuation == g_spu_trampoline_fn);
    buzz::release_function_state(ref); buzz::release_function_state(nat);
}
int main(int argc, char** argv)
{
    auto* seed = new spu_context{}; seed->ls = seed->ls_store;
    for (unsigned test = 0; test < 440; ++test) {
        for (auto& r : seed->gpr) for (auto& w : r._u32) w = word();
        for (unsigned i = 0; i < SPU_LS_SIZE; i += 4) { auto w = word(); memcpy(seed->ls + i, &w, 4); }
        const unsigned count = test < 400 ? 4 + test % 127 : 8192;
        seed->gpr[5]._u32[0] = test % 7 ? 0x12000 + (test % 4)*4 : SPU_LS_SIZE - count*4;
        if (test % 3 == 0) seed->gpr[6]._u32[0] = 0xfffffffe;
        seed->gpr[7]._u32[0] = seed->gpr[6]._u32[0] + count;
        if (test % 5 == 0) seed->gpr[9]._u32[0] = test % 10 ? 0xffffffff : 0;
        compare(seed);
    }
    for (unsigned variant = 0; variant < 11; ++variant) {
        auto* rejected = buzz::clone_function_state(seed);
        if (variant < 4) rejected->gpr[7]._u32[0] = rejected->gpr[6]._u32[0] + variant;
        if (variant == 4) ++rejected->gpr[5]._u32[0];
        if (variant == 5) rejected->gpr[5]._u32[0] = 0xe67c;
        if (variant == 6) rejected->gpr[5]._u32[0] = SPU_LS_SIZE - 4;
        if (variant == 7) rejected->gpr[7]._u32[0] = rejected->gpr[6]._u32[0] + 0x40000000;
        if (variant == 8) { rejected->int_enable = 1; rejected->event_mask = rejected->event_status = 1; }
        if (variant == 9) rejected->ls = seed->ls;
        if (variant == 10) buzz_geometry_debug = 1;
        auto* saved = buzz::clone_function_state(rejected); auto continuation = g_spu_trampoline_fn;
        assert(!buzz_job_6eba_spu_func_000028F0_native(rejected));
        assert(buzz::compare_function_state(*saved, *rejected).exact() && continuation == g_spu_trampoline_fn);
        buzz_geometry_debug = 0;
        buzz::release_function_state(saved); buzz::release_function_state(rejected);
    }
    if (argc > 1) {
        FILE* f = fopen(argv[1], "rb"); assert(f);
        assert(fread(seed->gpr, sizeof(seed->gpr), 1, f) == 1);
        assert(fread(seed->ls, SPU_LS_SIZE, 1, f) == 1); fclose(f); compare(seed);
    }
    puts("28F0: 440 strict states, 4..8192 words, padding, wrap, eleven unchanged-state rejections PASS");
    delete seed;
}
