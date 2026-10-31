/* Real controller + root replay: a root difference must fail even when its
 * caller overwrites the offending state before the whole-job comparison. */
#include "../port/native_geometry_job.h"
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

static const char* scenario;
static unsigned real_channels, reference_roots, native_roots;
static bool selected(const char* name) { return !strcmp(scenario, name); }
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
int buzz_geometry_debug = 0;
void spu_indirect_branch(spu_context*) { assert(false); }
void spu_indirect_branch_mt(spu_context*) { assert(false); }
buzz_geometry_job_fn spu_lookup(uint32_t, int) { return nullptr; }
void spu_drain_call(spu_context* ctx, uint32_t pc)
{
    assert(ctx->pc == pc && g_spu_trampoline_fn == spu_indirect_branch);
    g_spu_trampoline_fn = nullptr;
}
void spu_wrch(spu_context* ctx, uint32_t channel, u128 argument)
{
    assert(channel == 21); ++real_channels;
    if (argument._u32[0] == 0x44) {
        memset(ctx->ls + 0xe800, 0x17, 16);
        buzz_geometry_job_observe(ctx, 1, 0xe800, 0x123000, 16, ctx->ls + 0xe800);
    } else {
        assert(argument._u32[0] == 0x20);
        buzz_geometry_job_observe(ctx, 2, 0xe800, 0x123000, 16, ctx->ls + 0xe800);
    }
}
u128 spu_rdch(spu_context*, uint32_t channel)
{
    assert(channel == 24); ++real_channels; u128 value{}; value._u32[0] = 42; return value;
}
uint32_t spu_rchcnt(spu_context*, uint32_t channel)
{
    assert(channel == 24); ++real_channels; return 3;
}
}
static void write(spu_context* ctx, uint32_t argument)
{
    u128 value{}; value._u32[0] = argument; buzz_geometry_job_wrch(ctx, 21, value);
}
static void tail(spu_context*) {}
static void root(spu_context* ctx, bool native)
{
    assert(g_spu_trampoline_fn == tail);
    if (!selected("empty")) {
        write(ctx, 0x44);
        assert(buzz_geometry_job_rdch(ctx, 24)._u32[0] == 42);
        assert(buzz_geometry_job_rchcnt(ctx, 24) == 3);
        ctx->ls[0xe800] ^= 0x91;
        write(ctx, 0x20);
    }
    ctx->gpr[127]._u32[3] ^= 0x1234;
    ctx->ls[0x3ffff] ^= 0x42;
    ctx->status = 7;
    ctx->pc = 0xbeef;
    g_spu_trampoline_fn = tail;
    if (native) {
        if (selected("register")) ctx->gpr[127]._u32[3] ^= 1;
        if (selected("ls")) ctx->ls[0x3ffff] ^= 1;
        if (selected("control")) ctx->status ^= 1;
        if (selected("pc")) ctx->pc ^= 4;
        if (selected("image")) ctx->image_id = 40;
        if (selected("depth")) ++ctx->host_depth;
        if (selected("policy")) ctx->policy_mode = 1;
        if (selected("overlay")) ctx->resident_ovl = 1;
        if (selected("continuation")) g_spu_trampoline_fn = nullptr;
    }
}
static void root_reference(spu_context* ctx)
{
    ++reference_roots;
    if (selected("missing")) return;
    if (selected("input")) ctx->ls[0xf000] ^= 1;
    root(ctx, false);
    if (selected("nested")) root(ctx, false);
    if (selected("extra")) write(ctx, 0x20);
}
static void child_reference(spu_context* ctx) { ++reference_roots; root(ctx, false); }
static void caller(spu_context* ctx, bool native)
{
    write(ctx, 0x20); // Before the root; a slice must not replay this.
    g_spu_trampoline_fn = tail; // Check restoration of a non-null entry continuation.
    auto* check = native ? buzz_geometry_job_io_root_begin(ctx) : nullptr;
    if (native) ++native_roots; else ++reference_roots;
    root(ctx, native);
    if (selected("nested")) {
        auto* child = native ? buzz_geometry_job_io_root_begin(ctx) : nullptr;
        root(ctx, native);
        if (native) buzz_geometry_job_io_root_end(ctx, child, child_reference);
    }
    if (native) {
        if (selected("foreign")) {
            auto other = std::make_unique<spu_context>();
            buzz_geometry_job_io_root_end(other.get(), check, root_reference);
        }
        buzz_geometry_job_io_root_end(ctx, check, selected("null_runner") ? nullptr : root_reference);
    }
    // Intentionally erase every local difference; root checking must happen first.
    ctx->gpr[127]._u32[3] = 0;
    ctx->ls[0x3ffff] = 0;
    ctx->status = 0;
    ctx->image_id = 41;
    ctx->host_depth = 0;
    ctx->policy_mode = ctx->resident_ovl = 0;
    write(ctx, 0x20); // After the root; parent cursor/shadow must be restored.
    ctx->pc = ctx->gpr[0]._u32[0];
    g_spu_trampoline_fn = spu_indirect_branch;
}
static void reference(spu_context* ctx) { caller(ctx, false); }
static void native(spu_context* ctx) { caller(ctx, true); }
static int eligible(const spu_context*) { return 1; }
static void expected_abort(int)
{
    // All six real operations execute only on the retained whole-job reference.
    std::_Exit(real_channels == 6 && native_roots == 1 ? 73 : 74);
}
int main(int argc, char** argv)
{
    assert(argc == 2); scenario = argv[1]; std::signal(SIGABRT, expected_abort);
    auto ctx = std::make_unique<spu_context>();
    memset(ctx.get(), 0, sizeof(*ctx)); ctx->ls = ctx->ls_store;
    ctx->image_id = 41; ctx->pc = 0x1c18; ctx->gpr[0]._u32[0] = 0x3ff00;
    ctx->mfc_eal = 0xf000; ctx->mfc_size = 8;
    assert(!buzz_geometry_job_io_root_begin(ctx.get()));
    buzz_geometry_job_io_root_end(ctx.get(), nullptr, nullptr);
    buzz_geometry_job_run(ctx.get(), reference, native, eligible);
    assert(!buzz_geometry_job_audit_failures());
    assert(real_channels == (selected("empty") ? 2 : selected("nested") ? 10 : 6));
    assert(g_spu_trampoline_fn == spu_indirect_branch);
    const int mode = buzz_geometry_job_mode();
    assert(native_roots == (mode ? 1 : 0));
    assert(reference_roots == (mode == 2 ? (selected("nested") ? 3 : 2) : mode == 0 ? 1 : 0));
    printf("I/O root %s: real I/O once, exact root and restored caller PASS\n", scenario);
}
