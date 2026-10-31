#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_command_bindings.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_stop(spu_context*) { abort(); }
void spu_indirect_branch(spu_context*) { abort(); }
}
#include "../analysis/native-geometry/phase3/test-command-bindings-reference.inc"
static uint32_t random_word(uint32_t& x) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }
static void seed(spu_context& ctx, unsigned trial, bool reset)
{
    memset(&ctx, 0, sizeof ctx); ctx.ls = ctx.ls_store; ctx.image_id = 41; ctx.pc = reset ? 0x20 : 0x100; ctx.host_depth = 1;
    uint32_t random = trial + 1;
    for (unsigned i = 0; i < SPU_LS_SIZE; ++i) ctx.ls[i] = uint8_t(random_word(random));
    for (auto& reg : ctx.gpr) for (auto& word : reg._u32) word = random_word(random);
    ctx.gpr[0]._u32[0] = 0x1380; ctx.gpr[3]._u32[0] = 0x3b000; ctx.gpr[4]._u32[0] = 0x3b200;
    if (!reset) ctx.gpr[5]._u32[0] = 0x3b400;
    geometry_job_put32(ctx.ls + 0x3b074, 0x2c000);
    geometry_job_put32(ctx.ls + 0x3b208, 0x10000 + (trial % 32) * 4);
    const unsigned counts[] = {0, 1, 2, 3, 8, 16, 65, 255};
    const unsigned slots[] = {0,1,3,7,15,16,31,32,63,64,65,127,128,255};
    ctx.ls[0x2c000] = uint8_t(counts[(trial / 32) % 8]);
    for (unsigned i = 0; i < ctx.ls[0x2c000]; ++i) ctx.ls[0x2c00d + i * 8] = uint8_t(slots[(trial + i * 7) % 14]);
    // Neither empty nor fully masked lists may dereference the record pointer.
    if (trial & 1024) ctx.gpr[reset ? 5 : 6]._u32[0] &= ~65535u;
    if (trial & 1024) ctx.gpr[4]._u32[0] = 0xdeadbeef;
    ctx.int_enable = trial & 1;
}
static void compare(spu_context& native, spu_context& reference, unsigned trial, bool reset)
{
    memcpy(&reference, &native, sizeof native); reference.ls = reference.ls_store;
    GeometryCommandBindings plan; assert(read_geometry_command_bindings(native, reset, plan));
    auto fn = reset ? buzz_job_6eba_spu_func_00000020 : buzz_job_6eba_spu_func_00000100;
    fn(&reference);
    unsigned steps = 0;
    while (g_spu_trampoline_fn) {
        assert(++steps < 8192); fn = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; fn(&reference);
    }
    if (reset) reset_geometry_command_bindings(native, plan); else bind_geometry_command_attributes(native, plan);
    native.pc = native.gpr[0]._u32[0];
    const auto diff = compare_function_state(native, reference);
    if (!diff.exact()) {
        fprintf(stderr, "bindings case%u reset%d count%u enabled%u: r%d byte%d LS%X/%u control%s\n", trial, reset,
            plan.format.count, plan.enabled, diff.first_register, diff.first_register_byte, diff.first_ls, diff.ls_bytes,
            diff.first_state ? diff.first_state : "equal");
        if (diff.first_register >= 0) for (unsigned lane = 0; lane < 4; ++lane)
            fprintf(stderr, " lane%u native%08X reference%08X\n", lane, native.gpr[diff.first_register]._u32[lane], reference.gpr[diff.first_register]._u32[lane]);
        abort();
    }
}
int main()
{
    auto native = std::make_unique<spu_context>(), reference = std::make_unique<spu_context>();
    for (bool reset : {false, true}) {
        for (unsigned trial = 0; trial < 4096; ++trial) { seed(*native, trial, reset); compare(*native, *reference, trial, reset); }
        for (unsigned rejection = 0; rejection < 14; ++rejection) {
            seed(*native, 0, reset); native->ls[0x2c000] = 1; native->ls[0x2c00d] = 0; native->gpr[reset ? 5 : 6]._u32[0] = 1;
            switch (rejection) {
            case 0: native->ls = nullptr; break;
            case 1: native->gpr[3]._u32[0] = 0x3ffff; break;
            case 2: geometry_job_put32(native->ls + 0x3b074, 0x3ffff); break;
            case 3: native->gpr[4]._u32[0] = 0x3ffff; break;
            case 4: geometry_job_put32(native->ls + 0x3b208, 0x10001); break;
            case 5: geometry_job_put32(native->ls + 0x3b208, 0x1000); break;
            case 6: geometry_job_put32(native->ls + 0x3b208, SPU_LS_SIZE); break;
            case 7: geometry_job_put32(native->ls + 0x3b208, 0x3b000); break;
            case 8: geometry_job_put32(native->ls + 0x3b208, 0x2c000); break;
            case 9: geometry_job_put32(native->ls + 0x3b208, 0x3b200); break;
            case 10: native->gpr[4]._u32[0] = 0x3b070; break;
            case 11: geometry_job_put32(native->ls + 0x3b074, 0x3fff0); native->ls[0x3fff0] = 255; break;
            case 12: geometry_job_put32(native->ls + 0x3b074, 0x2c001); break;
            case 13: geometry_job_put32(native->ls + 0x3b208, 0x2c004); break;
            }
            memcpy(reference.get(), native.get(), sizeof(*native));
            GeometryCommandBindings plan, before; memset(&plan, 0xa5, sizeof plan); memcpy(&before, &plan, sizeof plan);
            assert(!read_geometry_command_bindings(*native, reset, plan));
            assert(!memcmp(native.get(), reference.get(), sizeof(*native)) && !memcmp(&plan, &before, sizeof plan));
        }
    }
    puts("Native command binding/reset: 8192 strict full-state cases, 28 unchanged-state rejections PASS");
}
