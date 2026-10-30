/* Full-state tests for SPU float-to-half packing, including non-IEEE edges. */
#define SPU_LS_DEBUG_HOOKS 0
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include "../port/native_function_check.h"
#include "../port/native_geometry_shuffle.h"
#include "../port/native_geometry_ls.h"
#include <cassert>
#include <chrono>
#include <cstdio>

extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
int g_spu_ls_watch_n = 0, buzz_geometry_debug = 0;
void buzz_job_6eba_spu_func_00004A9C(spu_context*) {}
}
#include "../analysis/native-geometry/phase2/test-half-kernels.inc"

static uint32_t random_state = 0x4ab00041;
static uint32_t word() { random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }

static void compare(spu_context* ref, spu_context* nat)
{
    buzz_job_6eba_spu_func_00004AB0_reference(ref);
    auto continuation = g_spu_trampoline_fn;
    g_spu_trampoline_fn = nullptr;
    assert(buzz_job_6eba_spu_func_00004AB0_native(nat));
    auto difference = buzz::compare_function_state(*ref, *nat);
    if (!difference.exact() || continuation != g_spu_trampoline_fn) {
        std::fprintf(stderr, "4AB0 DIFF r%d LS=%05X (%u bytes) state=%s\n", difference.first_register,
            difference.first_ls, difference.ls_bytes, difference.first_state ? difference.first_state : "equal");
        std::abort();
    }
}

int main(int argc, char** argv)
{
    auto* seed = new spu_context{}; seed->ls = seed->ls_store;
    constexpr uint32_t edges[] = {0, 0x80000000, 0x37ffffff, 0x38000000, 0x38001fff,
        0x38002000, 0x477fffff, 0x47800000, 0x7f7fffff, 0x7f800000, 0x7fffffff, 0xff800000, 0xffffffff};
    for (unsigned test = 0; test < 440; ++test) {
        for (auto& r : seed->gpr) for (auto& w : r._u32) w = word();
        for (unsigned i = 0; i < SPU_LS_SIZE; i += 4) { auto w = word(); std::memcpy(seed->ls + i, &w, 4); }
        half_seed_constants(seed);
        const unsigned iterations = test < 400 ? 2 + test % 15 : 512;
        const unsigned stride = test < 400 ? 64*(1 + test % 4) : 64;
        seed->gpr[22]._u32[0] = 0x10000 + test % 16;
        seed->gpr[32]._u32[0] = seed->gpr[22]._u32[0] + (iterations - 2)*stride + test % stride;
        for (unsigned j = 0; j < 4; ++j) {
            seed->gpr[29]._u32[j] = stride; seed->gpr[31]._u32[j] = test < 400 ? 320 : 64;
            seed->gpr[24]._u32[j] = test < 400 ? 16 + j*80 : j*16;
            seed->gpr[30]._u32[j] = 0x16000;
        }
        seed->gpr[19]._u32[0] = 0x20000;
        seed->gpr[28]._u32[0] = 16; seed->gpr[27]._u32[0] = 32; seed->gpr[26]._u32[0] = 48;
        if (test % 4 == 0) for (unsigned address = 0x10000; address < 0x10000 + iterations*stride + 64; address += 4) {
            const uint32_t value = SPU_BSWAP32(edges[(address/4 + test) % (sizeof(edges)/sizeof(*edges))]);
            std::memcpy(seed->ls + address, &value, 4);
        }
        auto* ref = buzz::clone_function_state(seed); auto* nat = buzz::clone_function_state(seed);
        compare(ref, nat); buzz::release_function_state(ref); buzz::release_function_state(nat);
    }
    for (unsigned variant = 0; variant < 11; ++variant) {
        auto* rejected = buzz::clone_function_state(seed);
        if (variant == 0) rejected->gpr[29]._u32[0] = 0;
        if (variant == 1) rejected->gpr[22]._u32[0] = rejected->gpr[32]._u32[0] + 16;
        if (variant == 2) rejected->gpr[33]._u32[0] ^= 1;
        if (variant == 3) ++rejected->gpr[28]._u32[0];
        if (variant == 4) rejected->gpr[22]._u32[0] = SPU_LS_SIZE - 16;
        if (variant == 5) { rejected->gpr[19]._u32[0] = 0x10000; rejected->gpr[24] = spu_il(0); }
        if (variant == 6) { rejected->int_enable = 1; rejected->event_mask = rejected->event_status = 1; }
        if (variant == 7) rejected->ls = seed->ls;
        if (variant == 8) buzz_geometry_debug = 1;
        if (variant == 9) rejected->gpr[19]._u32[0] = 0xfffffff0;
        if (variant == 10) rejected->gpr[22]._u32[0] = 0x100;
        auto* saved = buzz::clone_function_state(rejected);
        auto continuation = g_spu_trampoline_fn;
        assert(!buzz_job_6eba_spu_func_00004AB0_native(rejected));
        assert(buzz::compare_function_state(*saved, *rejected).exact() && continuation == g_spu_trampoline_fn);
        buzz_geometry_debug = 0;
        buzz::release_function_state(saved); buzz::release_function_state(rejected);
    }
    std::puts("4AB0: 440 strict states, FP edges, 2..512 iterations, eleven unchanged-state rejections PASS");
    if (argc > 1) {
        FILE* f = std::fopen(argv[1], "rb"); assert(f);
        assert(std::fread(seed->gpr, sizeof(seed->gpr), 1, f) == 1);
        assert(std::fread(seed->ls, SPU_LS_SIZE, 1, f) == 1); std::fclose(f);
        auto* ref = buzz::clone_function_state(seed); auto* nat = buzz::clone_function_state(seed);
        compare(ref, nat); buzz::release_function_state(ref); buzz::release_function_state(nat);
        for (unsigned round = 0; round < 4; ++round) for (unsigned pass = 0; pass < 2; ++pass) {
            const bool native = (pass ^ (round & 1)) != 0;
            auto* state = buzz::clone_function_state(seed);
            auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < 100000; ++i) {
                std::memcpy(state->gpr, seed->gpr, sizeof(seed->gpr));
                if (native) buzz_job_6eba_spu_func_00004AB0_native(state);
                else buzz_job_6eba_spu_func_00004AB0_reference(state);
            }
            double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count()/100000;
            std::printf("4AB0 micro round=%u %s ns=%.1f\n", round, native ? "native" : "reference", ns);
            buzz::release_function_state(state);
        }
    }
    delete seed;
}
