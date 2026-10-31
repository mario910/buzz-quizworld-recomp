/* The checked controller must preserve entry eligibility before cloning LS. */
#define SPU_LS_DEBUG_HOOKS 0
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include <cassert>
#include <cstdio>
extern "C"
{
    SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context *);
    void buzz_geometry_run(spu_context *, void (*)(spu_context *), int (*)(spu_context *), unsigned);
    int buzz_geometry_check_failures();
    void buzz_geometry_report();
}
static unsigned reference_calls = 0, native_calls = 0;
static void continuation(spu_context *) {}
static void result(spu_context *ctx)
{
    ++ctx->gpr[0]._u32[0];
    ctx->ls[0xf000] = 0xa5;
    ctx->pc = 0x100;
    g_spu_trampoline_fn = continuation;
}
static void reference(spu_context *ctx)
{
    ++reference_calls;
    result(ctx);
}
static int native(spu_context *ctx)
{
    ++native_calls;
    result(ctx);
    return 1;
}
int main()
{
    auto *external = new spu_context{};
    auto *memory = new uint8_t[SPU_LS_SIZE]{};
    external->ls = memory;
    buzz_geometry_run(external, reference, native, 0);
    assert(reference_calls == 1 && native_calls == 0);
    assert(memory[0xf000] == 0xa5 && external->gpr[0]._u32[0] == 1 && external->pc == 0x100);
    assert(g_spu_trampoline_fn == continuation && buzz_geometry_check_failures());
    auto *owned = new spu_context{};
    owned->ls = owned->ls_store;
    buzz_geometry_run(owned, reference, native, 0);
    assert(reference_calls == 2 && native_calls == 1 && owned->gpr[0]._u32[0] == 1);
    assert(owned->ls[0xf000] == 0xa5 && owned->pc == 0x100 && g_spu_trampoline_fn == continuation);
    assert(!buzz_geometry_check_failures());
    buzz_geometry_report();
    delete[] memory;
    delete external;
    delete owned;
    puts("Geometry controller: private entry compared; external entry retained reference without native "
         "invocation PASS");
}
