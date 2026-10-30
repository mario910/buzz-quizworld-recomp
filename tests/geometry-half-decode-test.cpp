/* Exact halfword column decoding, including the lift's unused output bits. */
#define SPU_LS_DEBUG_HOOKS 0
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include "../port/native_function_check.h"
#include "../port/native_geometry_ls.h"
#include <cassert>
#include <chrono>
#include <cstdio>

extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
int g_spu_ls_watch_n = 0, buzz_geometry_debug = 0;
void buzz_job_6eba_spu_func_0000CAD8(spu_context*) {}
void buzz_job_6eba_spu_func_0000CB78(spu_context*) {}
void spu_indirect_branch(spu_context*) {}
}
#include "../analysis/native-geometry/phase2/test-dehalf-kernels.inc"

static uint32_t random_state = 0xcb780041;
static uint32_t word() { random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }
static void compare(spu_context* ref, spu_context* nat)
{
    buzz_job_6eba_spu_func_0000CAD8_reference(ref);
    assert(ref->pc == ref->gpr[0]._u32[0]);
    assert(g_spu_trampoline_fn == (ref->host_depth ? nullptr : spu_indirect_branch));
    auto continuation = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr;
    assert(buzz_job_6eba_spu_func_0000CAD8_native(nat));
    auto difference = buzz::compare_function_state(*ref, *nat);
    if (!difference.exact() || continuation != g_spu_trampoline_fn) {
        std::fprintf(stderr, "CB78 DIFF r%d LS=%05X (%u bytes) state=%s\n", difference.first_register,
            difference.first_ls, difference.ls_bytes, difference.first_state ? difference.first_state : "equal");
        std::abort();
    }
}

int main(int argc, char** argv)
{
    auto* seed = new spu_context{}; seed->ls = seed->ls_store;
    const uint8_t edges[16] = {0, 0, 0x80, 0, 0, 1, 0x7c, 0, 0xfc, 0, 0x7f, 0xff, 0xff, 0xff, 0x80, 0};
    for (unsigned test = 0; test < 440; ++test) {
        for (auto& r : seed->gpr) for (auto& w : r._u32) w = word();
        for (unsigned i = 0; i < SPU_LS_SIZE; i += 4) { auto w = word(); std::memcpy(seed->ls + i, &w, 4); }
        dehalf_seed_constants(seed);
        const unsigned column = test < 400 ? test % 15 : 0;
        const unsigned remaining = test < 400 ? 2 + test % (15 - column) : 16;
        seed->gpr[44]._u32[0] = column; seed->gpr[3]._u32[0] = remaining;
        seed->gpr[5]._u32[0] = 1 + test % 32; seed->gpr[6]._u32[0] = test % 33;
        seed->gpr[0]._u32[0] = 0x1000; seed->host_depth = test % 2;
        for (unsigned j = 0; j < 4; ++j) {
            seed->gpr[7 + j*2]._u32[0] = 0x10000 + j*1024;
            seed->gpr[8 + j*2]._u32[0] = 0x10010 + j*1024;
            seed->gpr[4]._u32[j] = 0x20000 + j*4096 + 1 + test % 15;
            seed->gpr[17]._u32[j] = test < 400 ? 0x20800 : 0x20000 + (test % 7)*512 + 255;
            seed->gpr[19]._u32[j] = test < 400 ? 1024 : 512;
            seed->gpr[20]._u32[j] = 32*(1 + test % 4); seed->gpr[18]._u32[j] = 0x3e000;
        }
        if (test % 4 == 0) for (unsigned i = 0x10000; i < 0x13000; i += 16) std::memcpy(seed->ls + i, edges, 16);
        auto* ref = buzz::clone_function_state(seed); auto* nat = buzz::clone_function_state(seed);
        compare(ref, nat); buzz::release_function_state(ref); buzz::release_function_state(nat);
    }
    for (unsigned variant = 0; variant < 14; ++variant) {
        auto* rejected = buzz::clone_function_state(seed);
        if (variant == 0) rejected->gpr[3]._u32[0] = 1;
        if (variant == 1) rejected->gpr[44]._u32[0] = 0xffffffff;
        if (variant == 2) rejected->gpr[44]._u32[0] = 15;
        if (variant == 3) rejected->gpr[5]._u32[0] = 0;
        if (variant == 4) ++rejected->gpr[7]._u32[0];
        if (variant == 5) rejected->gpr[2]._u32[0] ^= 1;
        if (variant == 6) rejected->gpr[4]._u32[0] = 0x10000;
        if (variant == 7) rejected->gpr[18]._u32[0] = SPU_LS_SIZE - 8;
        if (variant == 8) rejected->gpr[20]._u32[3] += 16;
        if (variant == 9) { rejected->int_enable = 1; rejected->event_mask = rejected->event_status = 1; }
        if (variant == 10) rejected->ls = seed->ls;
        if (variant == 11) { rejected->gpr[7]._u32[0] = 0xfffffff0; rejected->gpr[8]._u32[0] = 0; }
        if (variant == 12) rejected->gpr[4]._u32[0] = 0xfffffff0;
        if (variant == 13) rejected->gpr[17] = spu_il(0x20010);
        auto* saved = buzz::clone_function_state(rejected);
        auto continuation = g_spu_trampoline_fn;
        assert(!buzz_job_6eba_spu_func_0000CAD8_native(rejected));
        assert(buzz::compare_function_state(*saved, *rejected).exact() && continuation == g_spu_trampoline_fn);
        buzz::release_function_state(saved); buzz::release_function_state(rejected);
    }
    std::puts("CAD8/CB78: 440 strict states, half edges, host returns, fourteen unchanged-state rejections PASS");
    if (argc > 1) {
        FILE* f = std::fopen(argv[1], "rb"); assert(f);
        assert(std::fread(seed->gpr, sizeof(seed->gpr), 1, f) == 1);
        assert(std::fread(seed->ls, SPU_LS_SIZE, 1, f) == 1); std::fclose(f);
        auto* ref = buzz::clone_function_state(seed); auto* nat = buzz::clone_function_state(seed);
        compare(ref, nat); buzz::release_function_state(ref); buzz::release_function_state(nat);
        for (unsigned round = 0; round < 4; ++round) for (unsigned pass = 0; pass < 2; ++pass) {
            const bool native = (pass ^ (round & 1)) != 0;
            auto* state = buzz::clone_function_state(seed); auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < 100000; ++i) {
                std::memcpy(state->gpr, seed->gpr, sizeof(seed->gpr));
                if (native) buzz_job_6eba_spu_func_0000CAD8_native(state);
                else buzz_job_6eba_spu_func_0000CAD8_reference(state);
            }
            double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count()/100000;
            std::printf("CB78 micro round=%u %s ns=%.1f\n", round, native ? "native" : "reference", ns);
            buzz::release_function_state(state);
        }
    }
    delete seed;
}
