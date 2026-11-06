/* Synthetic states plus optional private block-entry snapshot microbenchmark. */
#define SPU_LS_DEBUG_HOOKS 0
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include "../port/native_function_check.h"
#if defined(__aarch64__)
#include "../port/spu_arm_fp_state.h"
#endif
#include "../port/native_geometry_shuffle.h"
#include "../port/native_geometry_ls.h"
#include <cassert>
#include <chrono>
#include <cstdio>

extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
int g_spu_ls_watch_n = 0;
int buzz_geometry_debug = 0;
void buzz_job_6eba_spu_func_00009148(spu_context*) {}
void buzz_job_6eba_spu_func_000092A0(spu_context*) {}
void buzz_job_6eba_spu_func_000083C8(spu_context*) {}
}
#include "../analysis/native-geometry/phase2/test-kernels.inc"

static uint32_t random_state = 0x1892a041;
static uint32_t word() { random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }

static void compare(spu_context* ref, spu_context* nat)
{
    buzz_job_6eba_spu_func_00009148_reference(ref);
    auto continuation = g_spu_trampoline_fn;
    g_spu_trampoline_fn = nullptr;
    assert(buzz_job_6eba_spu_func_00009148_native(nat));
    auto difference = buzz::compare_function_state(*ref, *nat);
    if (!difference.exact() || continuation != g_spu_trampoline_fn) {
        std::fprintf(stderr, "92A0 DIFF r%d LS=%05X (%u bytes) state=%s\n", difference.first_register,
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
    buzz_arm_fp_enter_rtz(buzz_arm_fp_save());
#else
    _mm_setcsr((_mm_getcsr() & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO | 0x8040);
#endif
    auto* seed = new spu_context{};
    seed->ls = seed->ls_store;
    for (unsigned n = 0; n < 400; ++n) {
        for (auto& r : seed->gpr) for (auto& w : r._u32) w = word();
        for (unsigned i = 0; i < SPU_LS_SIZE; i += 4) { uint32_t v = word(); std::memcpy(seed->ls + i, &v, 4); }
        unit24_seed_constants(seed);
        seed->gpr[43]._u32[0] = n % 15;
        seed->gpr[44]._u32[0] = n % 15 * 16;
        constexpr unsigned strides[] = {3, 6, 12, 16};
        const unsigned stride = strides[n % 4];
        seed->gpr[42]._u32[0] = n % stride;
        seed->gpr[25]._u32[0] = 0xddf0;
        const unsigned bases[4] = {36,35,34,33}, second[4] = {40,39,38,37};
        for (unsigned i = 0; i < 4; ++i) {
            seed->gpr[bases[i]]._u32[0] = 0x10000 + i * 16 * stride;
            seed->gpr[second[i]]._u32[0] = 0x10010 + i * 16 * stride;
            seed->gpr[41]._u32[i] = 0x20000 + n % 16 + i * 256;
            seed->gpr[80]._u32[i] = 0x20000 + (4 + n % 12) * 1024 + 512;
            seed->gpr[81]._u32[i] = 1024;
            seed->gpr[82]._u32[i] = 0x3e000;
            seed->gpr[83]._u32[i] = 64 * stride;
            seed->gpr[106]._u32[i] = stride;
            seed->gpr[107]._u32[i] = 16;
        }
        for (unsigned i = 0x10000; i < 0x18000; ++i) seed->ls[i] = (seed->ls[i] & 0xf0) | (seed->ls[i] & 15) % 12;
        for (unsigned i = 0; i < 12; ++i) buzz_geometry_ls_write(seed->ls, 0xddf0 + 16*i, buzz_unit24_row(i));
        u128 gather;
        gather._u32[0] = 0x80800280; gather._u32[1] = 0x80800001;
        gather._u32[2] = 0x80800102; gather._u32[3] = 0x80808080;
        buzz_geometry_ls_write(seed->ls, 0xd8b0, gather);
        auto* ref = buzz::clone_function_state(seed);
        auto* nat = buzz::clone_function_state(seed);
        compare(ref, nat);
        buzz::release_function_state(ref); buzz::release_function_state(nat);
    }
    // Rejection must leave every byte and the continuation unchanged.
    auto* nat = buzz::clone_function_state(seed);
    nat->ls = seed->ls;
    assert(!buzz_job_6eba_spu_func_00009148_native(nat));
    assert(buzz::compare_function_state(*seed, *nat).exact());
    buzz::release_function_state(nat);
    for (unsigned variant = 0; variant < 7; ++variant) {
        auto* rejected = buzz::clone_function_state(seed);
        if (variant == 0) rejected->gpr[36]._u32[0] += 1;
        if (variant == 1) rejected->gpr[41] = rejected->gpr[36];
        if (variant == 2) rejected->ls[0xddf0] ^= 1;
        if (variant == 3) rejected->gpr[109]._u32[0] ^= 1;
        if (variant == 4) { rejected->int_enable = 1; rejected->event_mask = rejected->event_status = 1; }
        if (variant == 5) rejected->gpr[43]._u32[0] = 15;
        if (variant == 6) rejected->gpr[36]._u32[0] = SPU_LS_SIZE - 16;
        auto* saved = buzz::clone_function_state(rejected);
        auto continuation = g_spu_trampoline_fn;
        assert(!buzz_job_6eba_spu_func_00009148_native(rejected));
        assert(buzz::compare_function_state(*saved, *rejected).exact() && continuation == g_spu_trampoline_fn);
        buzz::release_function_state(saved); buzz::release_function_state(rejected);
    }
    std::puts("9148/92A0: 400 synthetic strict states, padded tail and external-LS rejection PASS");
    if (argc > 1) {
        FILE* f = std::fopen(argv[1], "rb"); assert(f);
        assert(std::fread(seed->gpr, sizeof(seed->gpr), 1, f) == 1);
        assert(std::fread(seed->ls, SPU_LS_SIZE, 1, f) == 1); std::fclose(f);
        auto* ref = buzz::clone_function_state(seed); nat = buzz::clone_function_state(seed);
        compare(ref, nat); buzz::release_function_state(ref); buzz::release_function_state(nat);
        for (unsigned round = 0; round < 4; ++round) {
            for (unsigned pass = 0; pass < 2; ++pass) {
                bool native = (pass ^ (round & 1)) != 0;
                auto* state = buzz::clone_function_state(seed);
                auto start = std::chrono::steady_clock::now();
                for (unsigned i = 0; i < 100000; ++i) {
                    std::memcpy(state->gpr, seed->gpr, sizeof(seed->gpr));
                    if (native) buzz_job_6eba_spu_func_00009148_native(state);
                    else buzz_job_6eba_spu_func_00009148_reference(state);
                }
                double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 100000;
                std::printf("92A0 micro round=%u %s ns=%.1f\n", round, native ? "native" : "reference", ns);
                buzz::release_function_state(state);
            }
        }
    }
    delete seed;
}
