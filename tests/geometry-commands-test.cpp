#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_commands.h"
#include "../port/native_geometry_job_io.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace buzz;
static std::vector<GeometryJobIoOperation> operations;
static void channel_write(spu_context* ctx, uint32_t channel, u128 value)
{
    GeometryJobIoOperation op;
    op.channel = channel; op.argument = value._u32[0]; op.before = GeometryJobControls::capture(*ctx);
    switch (channel) {
    case MFC_LSA: ctx->mfc_lsa = op.argument; break;
    case MFC_EAL: ctx->mfc_eal = op.argument; break;
    case MFC_Size: ctx->mfc_size = op.argument; break;
    case MFC_TagID: ctx->mfc_tag = op.argument & 31; break;
    case MFC_Cmd: {
        assert(op.argument == 66 && ctx->mfc_size == 4 && ctx->mfc_lsa + 4 <= SPU_LS_SIZE);
        const uint32_t payload = ctx->mfc_eal ^ ctx->mfc_eah ^ 0x75815731u;
        std::vector<uint8_t> bytes(4); geometry_job_put32(bytes.data(), payload);
        op.transfers.push_back({1, ctx->mfc_lsa, ctx->mfc_eal, bytes});
        memcpy(ctx->ls + ctx->mfc_lsa, bytes.data(), 4);
        ctx->mfc_tag_status |= 1u << ctx->mfc_tag;
        break;
    }
    default: abort();
    }
    op.after = GeometryJobControls::capture(*ctx); operations.push_back(op);
}
static u128 channel_read(spu_context* ctx, uint32_t channel)
{
    assert(channel == SPU_RdMachStat);
    GeometryJobIoOperation op; op.kind = GeometryJobIoKind::read; op.channel = channel;
    op.before = GeometryJobControls::capture(*ctx); op.after = op.before;
    op.result._u32[0] = ctx->int_enable; operations.push_back(op); return op.result;
}
static void drain(spu_context*, uint32_t);
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*);
void spu_drain_call(spu_context* ctx, uint32_t stop) { drain(ctx, stop); }
void spu_img_restore(spu_context* ctx, int32_t image) { assert(ctx->image_id == image); }
void spu_stop(spu_context*) { abort(); }
void spu_wrch(spu_context* ctx, uint32_t channel, u128 value) { channel_write(ctx, channel, value); }
u128 spu_rdch(spu_context* ctx, uint32_t channel) { return channel_read(ctx, channel); }
void buzz_geometry_job_wrch(spu_context* ctx, uint32_t channel, u128 value) { channel_write(ctx, channel, value); }
u128 buzz_geometry_job_rdch(spu_context* ctx, uint32_t channel) { return channel_read(ctx, channel); }
}
#include "../analysis/native-geometry/phase3/test-commands-reference.inc"
extern "C" void spu_indirect_branch(spu_context* ctx) { auto fn = commands_reference_lookup(ctx->pc); assert(fn); fn(ctx); }
static void drain(spu_context* ctx, uint32_t stop)
{
    unsigned steps = 0;
    while (g_spu_trampoline_fn) {
        assert(++steps < 65536); auto fn = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; fn(ctx);
    }
    assert(ctx->pc == stop);
}
static uint32_t random_word(uint32_t& word) { word ^= word << 13; word ^= word >> 17; word ^= word << 5; return word; }
static void seed(spu_context& ctx, unsigned trial)
{
    memset(&ctx, 0, sizeof ctx); ctx.ls = ctx.ls_store; ctx.image_id = 41; ctx.pc = 0x32c8; ctx.host_depth = 1;
    uint32_t random = trial + 1;
    for (unsigned at = 0; at < SPU_LS_SIZE; ++at) ctx.ls[at] = uint8_t(random_word(random));
    for (auto& reg : ctx.gpr) for (auto& word : reg._u32) word = random_word(random);
    ctx.gpr[0]._u32[0] = 0x1380; ctx.gpr[1]._u32[0] = 0x3c000; ctx.gpr[3]._u32[0] = 0x3b000;
    ctx.gpr[4]._u32[0] = 0x3b200; ctx.gpr[5]._u32[0] = 0x100000 + (trial % 32) * 4;
    ctx.gpr[6]._u32[0] = 0x28000;
    const unsigned counts[] = {0, 1, 2, 3, 8, 16, 64, 65, 1024};
    const unsigned count = counts[(trial / 32) % 9]; ctx.gpr[7]._u32[0] = count;
    ctx.ls[0x3b001] = uint8_t(trial & 256 ? 0 : trial & 512 ? 255 : 1 + trial % 15);
    geometry_job_put32(ctx.ls + 0x3b150, trial % 32);
    const unsigned sync = count ? (trial & 2048 ? count : (trial * 13 + trial / 32) % (count + 1)) : 0;
    const unsigned bad = count ? (trial * 17 + trial / 32) % (count + 1) : 0;
    const unsigned workspace = 0x10000 + (trial % 256);
    geometry_job_put32(ctx.ls + 0x3b058, sync < count ? workspace & ~3u : workspace);
    for (unsigned i = 0; i < count; ++i) {
        auto* record = ctx.ls + 0x28000 + i * 32;
        geometry_job_put32(record, 0x200000 + i * 128);
        geometry_job_put32(record + 8, i == sync ? 0 : i + 1);
        geometry_job_put32(record + 16, (i < bad && (trial & 1)) || (trial & 1024) ? 0 : 1 + i);
        record[20] = uint8_t(i < bad && !(trial & 1) ? 0 : 1 + i);
    }
    ctx.int_enable = sync < count ? 0 : trial & 1;
    ctx.mfc_eah = trial * 0x109u; ctx.mfc_tag_status = trial * 0x127u;
    ctx.resv_ea = trial * 128; ctx.resv_valid = trial & 1;
}
static void compare(spu_context& native, spu_context& reference, unsigned trial)
{
    memcpy(&reference, &native, sizeof native); reference.ls = reference.ls_store;
    GeometryCommandBuffer plan; operations.clear();
    assert(read_geometry_command_buffer(native, plan) && operations.empty());
    buzz_job_6eba_spu_func_000032C8(&reference); drain(&reference, native.gpr[0]._u32[0]);
    const auto expected = operations;
    operations.clear(); prepare_geometry_command_buffer(native, plan); native.pc = native.gpr[0]._u32[0];
    const auto diff = compare_function_state(native, reference);
    if (!diff.exact()) {
        fprintf(stderr, "command-buffer case%u count=%u sync=%u ready=%u differs: r%d byte%d LS=%X/%u controls=%s\n", trial,
            plan.count, plan.first_sync, plan.first_ready, diff.first_register, diff.first_register_byte, diff.first_ls, diff.ls_bytes,
            diff.first_state ? diff.first_state : "equal");
        if (diff.first_register >= 0) for (unsigned lane = 0; lane < 4; ++lane)
            fprintf(stderr, " lane%u native=%08X reference=%08X\n", lane, native.gpr[diff.first_register]._u32[lane], reference.gpr[diff.first_register]._u32[lane]);
        abort();
    }
    assert(!g_spu_trampoline_fn && expected.size() == operations.size());
    for (unsigned i = 0; i < expected.size(); ++i) {
        const auto& a = expected[i]; const auto& b = operations[i];
        assert(a.kind == b.kind && a.channel == b.channel && a.argument == b.argument && !memcmp(&a.result, &b.result, 16));
        a.before.apply(reference); assert(!b.before.difference(reference));
        a.after.apply(reference); assert(!b.after.difference(reference));
        assert(a.transfers.size() == b.transfers.size());
        for (unsigned j = 0; j < a.transfers.size(); ++j) {
            const auto& x = a.transfers[j]; const auto& y = b.transfers[j];
            assert(x.kind == y.kind && x.lsa == y.lsa && x.ea == y.ea && x.bytes == y.bytes);
        }
    }
}
int main()
{
    auto native = std::make_unique<spu_context>(), reference = std::make_unique<spu_context>();
    for (unsigned trial = 0; trial < 4096; ++trial) { seed(*native, trial); compare(*native, *reference, trial); }
    for (unsigned rejection = 0; rejection < 16; ++rejection) {
        seed(*native, 32); // One record needing a fenced read.
        geometry_job_put32(native->ls + 0x28008, 0); native->int_enable = 0;
        switch (rejection) {
        case 0: native->ls = nullptr; break;
        case 1: native->gpr[1]._u32[0] = 0x3ffff; break;
        case 2: native->gpr[1]._u32[0] = 0x80; break;
        case 3: native->gpr[3]._u32[0] = 0x3fff0; break;
        case 4: native->gpr[4]._u32[0] = 0x3b050; break;
        case 5: native->gpr[5]._u32[0] |= 1; break;
        case 6: native->gpr[7]._u32[0] = SPU_LS_SIZE / 32 + 1; break;
        case 7: native->gpr[6]._u32[0] = 0x3ffff; break;
        case 8: geometry_job_put32(native->ls + 0x3b058, 0x3fff0); break;
        case 9: native->int_enable = 1; break;
        case 10: geometry_job_put32(native->ls + 0x28000, 0x200001); break;
        case 11: geometry_job_put32(native->ls + 0x3b058, 0x1000); break;
        case 12: geometry_job_put32(native->ls + 0x3b058, 0x3b000); break;
        case 13: native->gpr[6]._u32[0] = 0x3b200; break;
        case 14: native->ls[0x3b001] = 0; native->gpr[5]._u32[0] = 124; geometry_job_put32(native->ls + 0x3b058, 0x3ff00); geometry_job_put32(native->ls + 0x28010, 1); native->ls[0x28014] = 1; break;
        case 15: geometry_job_put32(native->ls + 0x3b058, 0x10002); break;
        }
        memcpy(reference.get(), native.get(), sizeof(*native)); operations.clear();
        GeometryCommandBuffer plan, before; memset(&plan, 0xa5, sizeof plan); memcpy(&before, &plan, sizeof plan);
        assert(!read_geometry_command_buffer(*native, plan));
        assert(operations.empty() && !memcmp(native.get(), reference.get(), sizeof(*native)) && !memcmp(&plan, &before, sizeof plan));
    }
    puts("Native command buffer: 4096 full-state/channel/fenced-GET cases, 16 unchanged-state/no-I/O rejections PASS");
}
