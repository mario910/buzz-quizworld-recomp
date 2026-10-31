/* Exercise the real whole-driver controller with synthetic channel producers. */
#include "../port/native_geometry_job.h"
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

static const char* scenario = "pass";
static unsigned calls = 0, guest_writes = 0;
static spu_context* retained = nullptr;
static bool selected(const char* name) { return !strcmp(scenario, name); }
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
int buzz_geometry_debug = 0;
void spu_wrch(spu_context* ctx, uint32_t channel, u128 argument)
{
    assert(channel == 21 && argument._u32[0] == 0x20);
    ++guest_writes;
    auto observe = [&] {
        buzz_geometry_job_observe(ctx, 2, 0xe800, 0x123000, 16, ctx->ls + 0xe800);
    };
    if (selected("async")) { std::thread worker(observe); worker.join(); }
    else observe();
    ctx->mfc_tag_status = 8;
}
u128 spu_rdch(spu_context*, uint32_t) { return {}; }
uint32_t spu_rchcnt(spu_context*, uint32_t) { return 0; }
void spu_drain_call(spu_context* ctx, uint32_t pc)
{
    assert(ctx->host_depth == 1 && ctx->pc == pc && !g_spu_trampoline_fn);
}
void spu_indirect_branch(spu_context*) { assert(false); }
void spu_indirect_branch_mt(spu_context*) { assert(false); }
buzz_geometry_job_fn spu_lookup(uint32_t, int) { return nullptr; }
}

static void reference(spu_context* ctx)
{
    ++calls;
    if (calls == 1) retained = ctx;
    if (calls == 1 && selected("unknown_reference")) buzz_geometry_job_step(ctx, 0xbeef);
    if (calls == 2 && selected("unknown_shadow")) buzz_geometry_job_step(ctx, 0xbeef);
    if (calls == 2 && selected("bad_write")) ctx->ls[0xe80f] ^= 1;
    u128 argument{};
    argument._u32[0] = 0x20;
    if (!(calls == 2 && selected("missing_channel"))) buzz_geometry_job_wrch(ctx, 21, argument);
    if (calls == 2 && selected("extra_channel")) buzz_geometry_job_wrch(ctx, 21, argument);
    if (calls == 2 && selected("bad_exit")) ctx->ls[0x3ffff] ^= 1;
    if (calls == 2 && selected("bad_register")) ctx->gpr[127]._u32[3] ^= 1;
    if (calls == 2 && selected("bad_control")) ctx->list_stall_stat ^= 1;
    if (calls == 1 && selected("unstable_exit")) ctx->event_status = 0x80;
    if (calls == 2 && selected("post_exit_input")) retained->event_status = 0x80;
    if (calls == 1 && selected("unstable_queue")) {
        ctx->ch_in_mbox.count = 1;
        ctx->ch_in_mbox.q[0] = ctx->ch_in_mbox.value = 0xcafe;
    }
    if (calls == 2 && selected("post_exit_queue")) {
        retained->ch_in_mbox.count = 1;
        retained->ch_in_mbox.q[0] = retained->ch_in_mbox.value = 0xcafe;
    }
    ctx->pc = ctx->gpr[0]._u32[0];
}

static void expected_abort(int)
{
    // Even a failed shadow must not publish another real output.
    std::_Exit(guest_writes == 1 && calls == 2 ? 73 : 74);
}

int main(int argc, char** argv)
{
    if (argc > 1) scenario = argv[1];
    std::signal(SIGABRT, expected_abort);
    auto ctx = std::make_unique<spu_context>();
    memset(ctx.get(), 0, sizeof(*ctx));
    ctx->ls = ctx->ls_store;
    ctx->image_id = 41;
    ctx->pc = 0x1c18;
    ctx->gpr[0]._u32[0] = 0x3ff00;
    ctx->ls[0xe80f] = 0xa5;
    std::unique_ptr<uint8_t[]> external;
    if (selected("external_ls")) {
        external = std::make_unique<uint8_t[]>(SPU_LS_SIZE);
        memcpy(external.get(), ctx->ls, SPU_LS_SIZE);
        ctx->ls = external.get();
    }
    buzz_geometry_job_audit_run(ctx.get(), reference);
    buzz_geometry_job_audit_report();
    assert(guest_writes == 1 && ctx->ls[0xe80f] == 0xa5 && ctx->host_depth == 0);
    if (selected("pass") || selected("post_exit_input") || selected("post_exit_queue")) {
        assert(calls == 2 && !buzz_geometry_job_audit_failures());
        assert(g_spu_trampoline_fn == spu_indirect_branch);
        if (selected("post_exit_input")) assert(ctx->event_status == 0x80);
        if (selected("post_exit_queue")) assert(ctx->ch_in_mbox.count == 1 && ctx->ch_in_mbox.value == 0xcafe);
    } else if (selected("external_ls") || selected("async") || selected("unknown_reference") ||
               selected("unstable_exit") || selected("unstable_queue")) {
        assert(calls == 1 && buzz_geometry_job_audit_failures());
    } else {
        return 75; // A deliberately wrong shadow must have aborted.
    }
    printf("Whole-driver controller %s: real I/O once, reference/shadow/fallback accounting PASS\n", scenario);
}
