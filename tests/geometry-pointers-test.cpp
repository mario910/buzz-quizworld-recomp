/* Staged-list pointer generation: full-state comparison and safe rejection. */
#define SPU_LS_DEBUG_HOOKS 0
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <chrono>
#include <cstdio>

extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
int g_spu_ls_watch_n = 0, buzz_geometry_debug = 0;
void buzz_job_6eba_spu_func_00000988(spu_context*) {}
void buzz_job_6eba_spu_func_000009E0(spu_context*) {}
void buzz_job_6eba_spu_func_000008F0(spu_context*) {}
void buzz_job_6eba_spu_func_000009F8(spu_context*) {}
}
#include "../analysis/native-geometry/phase2/test-pointers-kernels.inc"

static uint32_t random_state = 0x09880041;
static uint32_t word() { random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }
static void compare(spu_context* ref, spu_context* nat)
{
    buzz_job_6eba_spu_func_00000988_reference(ref);
    assert(ref->pc == 0x8f0 && g_spu_trampoline_fn == buzz_job_6eba_spu_func_000008F0);
    auto continuation = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr;
    assert(buzz_job_6eba_spu_func_00000988_native(nat));
    auto difference = buzz::compare_function_state(*ref, *nat);
    if (!difference.exact() || continuation != g_spu_trampoline_fn) {
        std::fprintf(stderr, "0988 DIFF r%d LS=%05X (%u bytes) state=%s\n", difference.first_register,
            difference.first_ls, difference.ls_bytes, difference.first_state ? difference.first_state : "equal");
        std::abort();
    }
}

int main(int argc, char** argv)
{
    auto* seed = new spu_context{}; seed->ls = seed->ls_store;
    for (unsigned test = 0; test < 440; ++test) {
        for (auto& r : seed->gpr) for (auto& w : r._u32) w = word();
        for (unsigned i = 0; i < SPU_LS_SIZE; i += 4) { auto w = word(); std::memcpy(seed->ls + i, &w, 4); }
        pointers_seed_constants(seed);
        const unsigned count = test < 400 ? 2 + test % 20 : 128;
        if (test % 6 == 0) seed->gpr[13]._u32[0] = 0xfffffff0;
        seed->gpr[15]._u32[0] = seed->gpr[13]._u32[0] + count;
        seed->gpr[10]._u32[0] = 0x12000 + (test % 4)*4;
        seed->gpr[12]._u32[0] = (test % 2 ? 0x20000 : 0xe500) + (test % 4)*4;
        if (test % 4 == 0) seed->gpr[11]._u32[0] = 0xfffffff0;
        if (test % 10 < 2) for (unsigned i = 0; i < count; ++i) {
            const uint32_t size = test % 10 ? 0xffffffff : 0;
            const uint32_t address = test % 10 ? 0xaaaaaaaa : 0;
            std::memcpy(seed->ls + seed->gpr[10]._u32[0] + 48 + i*8, &size, 4);
            std::memcpy(seed->ls + seed->gpr[10]._u32[0] + 52 + i*8, &address, 4);
        }
        auto* ref = buzz::clone_function_state(seed); auto* nat = buzz::clone_function_state(seed);
        compare(ref, nat); buzz::release_function_state(ref); buzz::release_function_state(nat);
    }
    for (unsigned variant = 0; variant < 15; ++variant) {
        auto* rejected = buzz::clone_function_state(seed);
        if (variant == 0) rejected->gpr[15]._u32[0] = rejected->gpr[13]._u32[0];
        if (variant == 1) rejected->gpr[15]._u32[0] = rejected->gpr[13]._u32[0] + 1;
        if (variant == 2) rejected->gpr[15]._u32[0] = rejected->gpr[13]._u32[0] + 129;
        if (variant == 3) ++rejected->gpr[10]._u32[0];
        if (variant == 4) ++rejected->gpr[12]._u32[0];
        if (variant == 5) rejected->gpr[14]._u32[0] ^= 1;
        if (variant == 6) rejected->gpr[16]._u32[0] ^= 1;
        if (variant == 7) rejected->gpr[17]._u32[0] ^= 1;
        if (variant == 8) rejected->gpr[10]._u32[0] = 0xfffffff0;
        if (variant == 9) rejected->gpr[12]._u32[0] = SPU_LS_SIZE - 4;
        if (variant == 10) rejected->gpr[12]._u32[0] = rejected->gpr[10]._u32[0] + 48;
        if (variant == 11) rejected->gpr[12]._u32[0] = 0xe4fc;
        if (variant == 12) { rejected->int_enable = 1; rejected->event_mask = rejected->event_status = 1; }
        if (variant == 13) rejected->ls = seed->ls;
        if (variant == 14) buzz_geometry_debug = 1;
        auto* saved = buzz::clone_function_state(rejected);
        auto continuation = g_spu_trampoline_fn;
        assert(!buzz_job_6eba_spu_func_00000988_native(rejected));
        assert(buzz::compare_function_state(*saved, *rejected).exact() && continuation == g_spu_trampoline_fn);
        buzz_geometry_debug = 0;
        buzz::release_function_state(saved); buzz::release_function_state(rejected);
    }
    std::puts("0988/09E0: 440 strict states, word/counter wrap, zero/max sizes, fifteen unchanged-state rejections PASS");
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
                if (native) buzz_job_6eba_spu_func_00000988_native(state);
                else buzz_job_6eba_spu_func_00000988_reference(state);
            }
            double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count()/100000;
            std::printf("0988 micro round=%u %s ns=%.1f\n", round, native ? "native" : "reference", ns);
            buzz::release_function_state(state);
        }
    }
    delete seed;
}
