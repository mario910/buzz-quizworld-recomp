#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_publish.h"
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
    case MFC_Cmd:
        assert(op.argument == 32 && ctx->mfc_size <= 16384 && ctx->mfc_lsa + ctx->mfc_size <= SPU_LS_SIZE);
        op.transfers.push_back({2, ctx->mfc_lsa, ctx->mfc_eal,
            {ctx->ls + ctx->mfc_lsa, ctx->ls + ctx->mfc_lsa + ctx->mfc_size}});
        ctx->mfc_tag_status |= 1u << ctx->mfc_tag;
        break;
    default: abort();
    }
    op.after = GeometryJobControls::capture(*ctx); operations.push_back(op);
}
static u128 channel_read(spu_context* ctx, uint32_t channel)
{
    assert(channel == SPU_RdMachStat);
    GeometryJobIoOperation op; op.kind = GeometryJobIoKind::read; op.channel = channel;
    op.before = GeometryJobControls::capture(*ctx); op.after = op.before;
    op.result._u32[0] = ctx->int_enable;
    operations.push_back(op); return op.result;
}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*);
void spu_drain_call(spu_context*, uint32_t) { abort(); }
void spu_img_restore(spu_context*, int32_t) { abort(); }
void spu_stop(spu_context*) { abort(); }
void spu_wrch(spu_context* ctx, uint32_t channel, u128 value) { channel_write(ctx, channel, value); }
u128 spu_rdch(spu_context* ctx, uint32_t channel) { return channel_read(ctx, channel); }
void buzz_geometry_job_wrch(spu_context* ctx, uint32_t channel, u128 value) { channel_write(ctx, channel, value); }
u128 buzz_geometry_job_rdch(spu_context* ctx, uint32_t channel) { return channel_read(ctx, channel); }
}
#include "../analysis/native-geometry/phase3/test-publish-reference.inc"
extern "C" void spu_indirect_branch(spu_context* ctx)
{
    assert(ctx->pc == 0x2e60);
    buzz_job_6eba_spu_func_00002E60(ctx);
}
static uint32_t random_word(uint32_t& word) { word ^= word << 13; word ^= word >> 17; word ^= word << 5; return word; }
static void seed(spu_context& ctx, unsigned trial)
{
    memset(&ctx, 0, sizeof ctx); ctx.ls = ctx.ls_store; ctx.image_id = 41; ctx.pc = 0x2dc0; ctx.host_depth = 1;
    uint32_t random = trial + 1;
    for (unsigned at = 0; at < SPU_LS_SIZE; ++at) ctx.ls[at] = uint8_t(random_word(random));
    for (auto& reg : ctx.gpr) for (auto& word : reg._u32) word = random_word(random);
    ctx.gpr[0]._u32[0] = 0x2240; ctx.gpr[1]._u32[0] = 0x3c000;
    ctx.gpr[3]._u32[0] = 0x3b000; ctx.gpr[5]._u32[0] = 0x3b200; ctx.gpr[6]._u32[0] = 0x3b240;
    static const unsigned counts[] = {0, 1, 7, 8, 9, 8191, 8192, 8193, 16383, 16384, 16385, 32768,
        0x80000000u, 0xfffffff9u, 0xffffffffu};
    const unsigned count = counts[trial % (sizeof counts / sizeof *counts)];
    ctx.gpr[4]._u32[0] = count;
    const unsigned bytes = ((count + 7u) & ~7u) * 2u;
    const uint32_t cpu = trial & 128 ? 0xfffffff0u : 0x100000 + trial * 128;
    geometry_job_put32(ctx.ls + 0x3b200, cpu);
    geometry_job_put32(ctx.ls + 0x3b20c, cpu + bytes + (trial & 128 ? 0 : (trial % 7) * 128));
    geometry_job_put32(ctx.ls + 0x3b050, 0x10000);
    geometry_job_put32(ctx.ls + 0x3b150, trial % 32);
    ctx.mfc_eah = trial * 0x109u; ctx.mfc_tag_status = trial * 0x127u;
    ctx.resv_ea = trial * 128; ctx.resv_valid = trial & 1;
    ctx.int_enable = count ? 0 : trial & 1;
}
static void compare(spu_context& native, spu_context& reference, unsigned trial)
{
    memcpy(&reference, &native, sizeof native); reference.ls = reference.ls_store;
    GeometryIndexPublication plan;
    operations.clear();
    assert(read_geometry_index_publication(native, plan));
    assert(operations.empty());
    operations.clear(); g_spu_trampoline_fn = nullptr;
    buzz_job_6eba_spu_func_00002DC0(&reference);
    unsigned steps = 0;
    while (g_spu_trampoline_fn) {
        assert(++steps < 100); auto fn = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; fn(&reference);
    }
    const auto expected = operations;
    operations.clear(); publish_geometry_indices(native, plan); native.pc = native.gpr[0]._u32[0];
    const auto diff = compare_function_state(native, reference);
    if (!diff.exact()) {
        fprintf(stderr, "case%u differs: r%d byte%d LS=%X/%u controls=%s\n", trial,
            diff.first_register, diff.first_register_byte, diff.first_ls, diff.ls_bytes,
            diff.first_state ? diff.first_state : "equal"); abort();
    }
    assert(!g_spu_trampoline_fn && expected.size() == operations.size());
    for (unsigned i = 0; i < expected.size(); ++i) {
        const auto& a = expected[i]; const auto& b = operations[i];
        assert(a.kind == b.kind && a.channel == b.channel && a.argument == b.argument);
        assert(!memcmp(&a.result, &b.result, 16));
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
    for (unsigned trial = 0; trial < 2048; ++trial) { seed(*native, trial); compare(*native, *reference, trial); }
    for (unsigned rejection = 0; rejection < 9; ++rejection) {
        seed(*native, 1);
        switch (rejection) {
        case 0: native->ls = nullptr; break;
        case 1: native->gpr[1]._u32[0] = 0x3ffff; break;
        case 2: native->gpr[1]._u32[0] = 0x80; break;
        case 3: native->int_enable = 1; break;
        case 4: native->gpr[3]._u32[0] = 0x3fff0; break;
        case 5: native->gpr[5]._u32[0] = 0x3b050; break;
        case 6: native->gpr[6]._u32[0] = 0x3b200; break;
        case 7: geometry_job_put32(native->ls + 0x3b050, 0x3fff0); native->gpr[4]._u32[0] = 9; break;
        case 8: geometry_job_put32(native->ls + 0x3b20c, 0); break;
        }
        memcpy(reference.get(), native.get(), sizeof(*native));
        GeometryIndexPublication plan, before; memset(&plan, 0xa5, sizeof plan); memcpy(&before, &plan, sizeof plan);
        operations.clear();
        assert(!read_geometry_index_publication(*native, plan));
        assert(operations.empty());
        assert(!memcmp(native.get(), reference.get(), sizeof(*native)) && !memcmp(&plan, &before, sizeof plan));
    }
    puts("Native index publication: 2048 full-state/channel/payload cases, 9 unchanged-state rejections PASS");
}
