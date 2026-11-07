// Exercise the real Stage F paired runner: image39 returns a live count in r3.
#include "../port/native_stagef.cpp"
#include <cassert>

extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_drain_call(spu_context*, uint32_t) {}
}

static void reference_leaf(spu_context* ctx)
{
    ctx->gpr[3]._u32[0] = 7;
    ctx->gpr[42]._u32[0] = 123;
    ctx->pc = ctx->gpr[0]._u32[0];
}

static int correct_leaf(spu_context* ctx)
{
    ctx->gpr[3]._u32[0] = 7;
    ctx->gpr[42]._u32[0] = 999; // Dead scratch is deliberately different.
    ctx->pc = ctx->gpr[0]._u32[0];
    return 1;
}

static int wrong_leaf(spu_context* ctx)
{
    correct_leaf(ctx);
    ctx->gpr[3]._u32[0] = 8;
    return 1;
}

int main()
{
    auto* ctx = new spu_context{};
    ctx->ls = ctx->ls_store;
    ctx->gpr[0]._u32[0] = 0x2178;
    buzz_stagef_mode = 2;
    buzz_stagef_run(ctx, reference_leaf, correct_leaf, 3);
    assert(!buzz_stagef_check_failures());
    assert(ctx->gpr[3]._u32[0] == 7 && ctx->gpr[42]._u32[0] == 123);
    buzz_stagef_run(ctx, reference_leaf, wrong_leaf, 3);
    assert(buzz_stagef_check_failures());
    assert(ctx->gpr[3]._u32[0] == 7); // Paired mode retains reference output.
    buzz_stagef_mode = 4;
    buzz_stagef_run(ctx, reference_leaf, correct_leaf, 3);
    assert(ctx->gpr[3]._u32[0] == 7 && ctx->gpr[42]._u32[0] != 123);
    assert(g_poisoned[3].load() == 1 && g_poisoned[0].load() == 0);
    buzz_stagef_report(); // Replay automation must be able to prove poison coverage.
    delete ctx;
    puts("stagef-image39-return: PASS (live return, dead scratch, poison contract)");
}
