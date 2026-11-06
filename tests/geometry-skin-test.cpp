/* Strict skinning checks; the pinned reference is generated into ignored analysis/. */
#define SPU_LS_DEBUG_HOOKS 0
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#if defined(__aarch64__)
#include "../port/spu_arm_fp_state.h"
#endif
#include "../port/native_function_check.h"
#include "../port/native_geometry_ls.h"
#include <cassert>
#include <chrono>
#include <cstdio>

extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
int g_spu_ls_watch_n = 0, buzz_geometry_debug = 0;
void buzz_job_6eba_spu_func_0000B940(spu_context*) {}
void buzz_job_6eba_spu_func_0000B934(spu_context*) {}
void buzz_job_6eba_spu_func_0000B4C0(spu_context*) {}
void buzz_job_6eba_spu_func_0000BB58(spu_context*) {}
}
#include "../analysis/native-geometry/phase2/test-skin-kernels.inc"

static uint32_t random_state = 0xb9400041;
static unsigned exits_seen = 0;
static uint32_t word() { random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }

static void compare(spu_context* ref, spu_context* nat)
{
    buzz_job_6eba_spu_func_0000B880_reference(ref);
    assert(ref->pc == 0xB4C0 || ref->pc == 0xBB58);
    exits_seen |= ref->pc == 0xB4C0 ? 1 : 2;
    auto continuation = g_spu_trampoline_fn;
    g_spu_trampoline_fn = nullptr;
    assert(buzz_job_6eba_spu_func_0000B880_native(nat));
    auto difference = buzz::compare_function_state(*ref, *nat);
    if (!difference.exact() || continuation != g_spu_trampoline_fn) {
        std::fprintf(stderr, "B940 DIFF r%d LS=%05X (%u bytes) state=%s\n", difference.first_register,
            difference.first_ls, difference.ls_bytes, difference.first_state ? difference.first_state : "equal");
        for (unsigned r = 0; r < 128; ++r) if (std::memcmp(&ref->gpr[r], &nat->gpr[r], 16)) {
            std::fprintf(stderr, "r%u ref", r);
            for (auto w : ref->gpr[r]._u32) std::fprintf(stderr, " %08X", w);
            std::fprintf(stderr, " native");
            for (auto w : nat->gpr[r]._u32) std::fprintf(stderr, " %08X", w);
            std::fprintf(stderr, "\n");
        }
        std::abort();
    }
}

int main(int argc, char** argv)
{
#if defined(__aarch64__)
    const auto previous = buzz_arm_fp_save();
    buzz_arm_fp_enter_rtz(previous);
#else
    _mm_setcsr((_mm_getcsr() & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO | 0x8040);
#endif
    auto* seed = new spu_context{};
    seed->ls = seed->ls_store;
    constexpr unsigned large[] = {128, 256, 512, 1024, 2048};
    for (unsigned test = 0; test < 440; ++test) {
        for (auto& r : seed->gpr) for (auto& w : r._u32) w = word();
        for (unsigned i = 0; i < SPU_LS_SIZE; i += 4) { auto w = word(); std::memcpy(seed->ls + i, &w, 4); }
        skin_seed_constants(seed);
        const unsigned count = test < 400 ? 4 + 2*(test % 15) : large[test % 5];
        seed->gpr[84]._u32[0] = count;
        seed->gpr[102]._u32[0] = test % 2; // Exercise both exit continuations.
        seed->gpr[80]._u32[0] = 0x13000; seed->gpr[85]._u32[0] = 0xec00;
        seed->gpr[96]._u32[0] = 0x1c000;
        seed->gpr[54]._u32[0] = 0xe800; seed->gpr[53]._u32[0] = 0xe810; seed->gpr[52]._u32[0] = 0xe820;
        for (unsigned i = 0; i < (count + 2)*4; ++i) seed->ls[0xec00 + i*2 + 1] %= 16;
        // All-zero, one-hot in each influence, mixed sparse and dense weights.
        if (test % 8 < 5) for (unsigned v = 0; v < count; ++v) {
            for (unsigned j = 0; j < 4; ++j) seed->ls[0xec00 + v*8 + j*2] = 0;
            if (test % 8) seed->ls[0xec00 + v*8 + (test % 8 - 1)*2] = 255;
        }
        auto* ref = buzz::clone_function_state(seed); auto* nat = buzz::clone_function_state(seed);
        compare(ref, nat);
        buzz::release_function_state(ref); buzz::release_function_state(nat);
    }
    assert(exits_seen == 3);
    for (unsigned variant = 0; variant < 10; ++variant) {
        auto* rejected = buzz::clone_function_state(seed);
        if (variant == 0) ++rejected->gpr[84]._u32[0];
        if (variant == 1) rejected->gpr[96]._u32[0] = rejected->gpr[80]._u32[0];
        if (variant == 2) rejected->gpr[39]._u32[0] ^= 1;
        if (variant == 3) ++rejected->gpr[80]._u32[0];
        if (variant == 4) rejected->gpr[85]._u32[0] = 0xfffffff0;
        if (variant == 5) { rejected->int_enable = 1; rejected->event_mask = rejected->event_status = 1; }
        if (variant == 6) rejected->ls = seed->ls;
        if (variant == 7) rejected->gpr[53]._u32[0] += 16;
        if (variant == 8) rejected->gpr[84]._u32[0] = 2;
        if (variant == 9) rejected->gpr[85]._u32[0] += 8;
        auto* saved = buzz::clone_function_state(rejected);
        auto continuation = g_spu_trampoline_fn;
        assert(!buzz_job_6eba_spu_func_0000B880_native(rejected));
        assert(buzz::compare_function_state(*saved, *rejected).exact() && continuation == g_spu_trampoline_fn);
        buzz::release_function_state(saved); buzz::release_function_state(rejected);
    }
    std::puts("B880/B940: 440 synthetic strict states (4..2048 vertices), ten unchanged-state rejections PASS");
    if (argc > 1) {
        FILE* f = std::fopen(argv[1], "rb"); assert(f);
        assert(std::fread(seed->gpr, sizeof(seed->gpr), 1, f) == 1);
        assert(std::fread(seed->ls, SPU_LS_SIZE, 1, f) == 1); std::fclose(f);
        auto* ref = buzz::clone_function_state(seed); auto* nat = buzz::clone_function_state(seed);
        compare(ref, nat); buzz::release_function_state(ref); buzz::release_function_state(nat);
        const unsigned position = seed->gpr[80]._u32[0], bytes = (seed->gpr[84]._u32[0] + 2)*16;
        for (unsigned round = 0; round < 4; ++round) for (unsigned pass = 0; pass < 2; ++pass) {
            const bool native = (pass ^ (round & 1)) != 0;
            auto* state = buzz::clone_function_state(seed);
            auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < 100000; ++i) {
                std::memcpy(state->gpr, seed->gpr, sizeof(seed->gpr));
                std::memcpy(state->ls + position, seed->ls + position, bytes);
                if (native) buzz_job_6eba_spu_func_0000B880_native(state);
                else buzz_job_6eba_spu_func_0000B880_reference(state);
            }
            double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 100000;
            std::printf("B940 micro round=%u %s ns=%.1f\n", round, native ? "native" : "reference", ns);
            buzz::release_function_state(state);
        }
    }
    delete seed;
}
