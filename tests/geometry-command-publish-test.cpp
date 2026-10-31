#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_command_publish.h"
#include "../port/native_geometry_job_io.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace buzz;
static std::vector<GeometryJobIoOperation> operations;
static unsigned split_cases = 0, multiple_splits = 0, no_padding = 0, no_destination = 0;
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
        assert(op.argument == 34 && ctx->mfc_lsa + ctx->mfc_size <= SPU_LS_SIZE);
        std::vector<uint8_t> bytes(ctx->ls + ctx->mfc_lsa, ctx->ls + ctx->mfc_lsa + ctx->mfc_size);
        op.transfers.push_back({2, ctx->mfc_lsa, ctx->mfc_eal, bytes});
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
#include "../analysis/native-geometry/phase3/test-command-publish-reference.inc"
extern "C" void spu_indirect_branch(spu_context* ctx) { auto fn = command_publish_reference_lookup(ctx->pc); assert(fn); fn(ctx); }
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
    const unsigned counts[] = {0, 1, 2, 3, 8, 16, 64, 65};
    unsigned count = counts[(trial / 32) % 8];
    ctx.ls[0x3b001] = 255;
    geometry_job_put32(ctx.ls + 0x3b150, trial % 32);
    const uint32_t start = 0x10000 + (trial % 32) * 4;
    unsigned cursor = start;
    // Keep physical tail/padding random; only actual command words are initialized.
    const unsigned lengths[] = {0, 1, 3, 20, 30, 31, 32, 33, 80, 127};
    for (unsigned i = 0; i < (trial / 256) % 7; ++i) {
        unsigned words = lengths[(trial + i * 7) % 10];
        geometry_job_put32(ctx.ls + cursor, 0x1700 | (words << 18) | (trial & 1024 ? 0x40000000 : 0));
        cursor += 4;
        for (unsigned j = 0; j < words; ++j, cursor += 4) geometry_job_put32(ctx.ls + cursor, random_word(random));
    }
    // A random-looking word just beyond the logical commands makes the final
    // unconditional look-ahead reach far outside the command allocation.
    if ((trial & 256) && cursor != start) geometry_job_put32(ctx.ls + cursor, 0x1ffc1700);
    if (!(trial % 8)) { count = 0; cursor = start; ctx.ls[0x3b001] = trial & 256 ? 1 : 0; }
    ctx.gpr[7]._u32[0] = count;
    geometry_job_put32(ctx.ls + 0x3b200, start);
    geometry_job_put32(ctx.ls + 0x3b204, start + (ctx.ls[0x3b001] ? ctx.ls[0x3b001] * 16 : 4));
    geometry_job_put32(ctx.ls + 0x3b208, cursor);
    for (unsigned i = 0; i < count; ++i) {
        auto* record = ctx.ls + 0x28000 + i * 32;
        geometry_job_put32(record + 12, random_word(random));
        geometry_job_put32(record + 16, (trial & 512) || ((i + trial) % 3) ? 0 : 0x200000 + i * 128);
    }
    static constexpr uint8_t shift[] = {12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27};
    memcpy(ctx.ls + 0xcea0, shift, 16);
    ctx.mfc_eah = trial * 0x109u; ctx.mfc_tag_status = trial * 0x127u;
    ctx.resv_ea = trial * 128; ctx.resv_valid = trial & 1;
    ctx.pc = 0x27a0;
}

static void compare(spu_context& native, spu_context& reference, unsigned trial)
{
    memcpy(&reference, &native, sizeof native); reference.ls = reference.ls_store;
    GeometryCommandPublication plan; operations.clear();
    assert(read_geometry_command_publication(native, plan) && operations.empty());
    split_cases += plan.splits != 0; multiple_splits += plan.splits > 1;
    no_padding += plan.destination && !plan.padding_words; no_destination += !plan.destination;
    buzz_job_6eba_spu_func_000027A0(&reference); drain(&reference, native.gpr[0]._u32[0]);
    const auto expected = operations;
    operations.clear(); publish_geometry_commands(native, plan); native.pc = native.gpr[0]._u32[0];
    const auto diff = compare_function_state(native, reference);
    if (!diff.exact()) {
        fprintf(stderr, "command publication case%u count=%u start=%X cursor=%X differs: r%d byte%d LS=%X/%u controls=%s\n", trial,
            plan.count, plan.start, plan.cursor, diff.first_register, diff.first_register_byte, diff.first_ls, diff.ls_bytes,
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
    for (unsigned trial = 0; trial < 8192; ++trial) {
        seed(*native, trial);
        if (trial & 4096) {
            GeometryCommandPublication plan; assert(read_geometry_command_publication(*native, plan));
            // Exercise exact fit independently of the configured DMA size.
            if (trial & 1) geometry_job_put32(native->ls + 0x3b204, plan.limit - plan.padding_words * 4);
            if (trial & 2) { native->gpr[5] = spu_il(0); native->int_enable = 1; }
        }
        compare(*native, *reference, trial);
    }
    assert(split_cases > 100 && multiple_splits > 100 && no_padding > 100 && no_destination > 100);
    for (unsigned trial = 0; trial < 64; ++trial) {
        seed(*native, trial);
        const uint32_t start = 0x3e800 + trial * 4;
        native->gpr[5]._u32[0] = 0x100000 + trial * 4; native->gpr[7] = spu_il(0);
        native->ls[0x3b001] = 255;
        geometry_job_put32(native->ls + 0x3b200, start);
        geometry_job_put32(native->ls + 0x3b204, start + 4);
        geometry_job_put32(native->ls + 0x3b208, start + 4);
        geometry_job_put32(native->ls + start, 0x1700);
        geometry_job_put32(native->ls + start + 4, 0x1ffc1700);
        compare(*native, *reference, 8192 + trial);
    }
    for (unsigned rejection = 0; rejection < 20; ++rejection) {
        seed(*native, 33);
        switch (rejection) {
        case 0: native->ls = nullptr; break;
        case 1: native->int_enable = 1; break;
        case 2: native->gpr[3]._u32[0] = 0x3fff0; break;
        case 3: native->gpr[4]._u32[0] = 0x3ffff; break;
        case 4: native->gpr[5]._u32[0] |= 1; break;
        case 5: native->gpr[7]._u32[0] = SPU_LS_SIZE / 32 + 1; break;
        case 6: native->gpr[6]._u32[0] = 0x3ffff; break;
        case 7: geometry_job_put32(native->ls + 0x3b200, 0x10001); break;
        case 8: geometry_job_put32(native->ls + 0x3b208, 0x10000); break;
        case 9: geometry_job_put32(native->ls + 0x3b204, 0x10080); geometry_job_put32(native->ls + 0x3b208, 0x10100); break;
        case 10: geometry_job_put32(native->ls + 0x3b204, SPU_LS_SIZE + 4); break;
        case 11: geometry_job_put32(native->ls + 0x3b200, 0x1000); break;
        case 12: native->gpr[4]._u32[0] = 0x3b050; break;
        case 13: native->gpr[6]._u32[0] = 0x3b200; break;
        case 14: geometry_job_put32(native->ls + 0x3b200, 0x28000); geometry_job_put32(native->ls + 0x3b204, 0x29000); geometry_job_put32(native->ls + 0x3b208, 0x28000); break;
        case 15: native->ls[0xcea0] ^= 1; break;
        case 16: geometry_job_put32(native->ls + 0x3b204, 0x10084); geometry_job_put32(native->ls + 0x3b208, 0x10084); geometry_job_put32(native->ls + 0x28010, 1); break;
        case 17: geometry_job_put32(native->ls + 0x3b208, 0x10085); break;
        case 18: geometry_job_put32(native->ls + 0x3b204, 0x10005); break;
        case 19: native->gpr[3]._u32[0] = 0x10000; break;
        }
        memcpy(reference.get(), native.get(), sizeof(*native)); operations.clear();
        GeometryCommandPublication plan, before; memset(&plan, 0xa5, sizeof plan); memcpy(&before, &plan, sizeof plan);
        if (read_geometry_command_publication(*native, plan)) { fprintf(stderr, "rejection %u unexpectedly accepted\n", rejection); abort(); }
        assert(operations.empty() && !memcmp(native.get(), reference.get(), sizeof(*native)) && !memcmp(&plan, &before, sizeof plan));
    }
    printf("Native command publication: 8256 strict full-state/channel/fenced-PUT cases (64 wrapped look-aheads; %u split/%u multiple/%u exact fit/%u no destination), 20 unchanged-state/no-I/O rejections PASS\n",
        split_cases, multiple_splits, no_padding, no_destination);
}
