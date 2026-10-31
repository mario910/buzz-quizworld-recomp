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
static void drain(spu_context* ctx, uint32_t stop);
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
#include "../analysis/native-geometry/phase3/test-output-reference.inc"
extern "C" void spu_indirect_branch(spu_context* ctx)
{
    auto fn = output_reference_lookup(ctx->pc); assert(fn); fn(ctx);
}
static void drain(spu_context* ctx, uint32_t stop)
{
    unsigned steps = 0;
    while (g_spu_trampoline_fn) {
        assert(++steps < 100); auto fn = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; fn(ctx);
    }
    assert(ctx->pc == stop);
}
static uint32_t random_word(uint32_t& word) { word ^= word << 13; word ^= word >> 17; word ^= word << 5; return word; }
static void seed(spu_context& ctx, unsigned trial, bool publication)
{
    memset(&ctx, 0, sizeof ctx); ctx.ls = ctx.ls_store; ctx.image_id = 41; ctx.pc = publication ? 0x35f0 : 0x2b80; ctx.host_depth = 1;
    uint32_t random = trial + 1;
    for (unsigned at = 0; at < SPU_LS_SIZE; ++at) ctx.ls[at] = uint8_t(random_word(random));
    for (auto& reg : ctx.gpr) for (auto& word : reg._u32) word = random_word(random);
    ctx.gpr[0]._u32[0] = publication ? 0x2258 : 0x21b0;
    ctx.gpr[1]._u32[0] = 0x3c000; ctx.gpr[3]._u32[0] = 0x3b000;
    static const unsigned formats[] = {0, 1, 2, 3, 4, 0xff};
    const unsigned format = formats[trial % 6];
    const unsigned descriptor = trial & 256 ? 0 : 0x3b300 + (trial & 15);
    static const unsigned custom_strides[] = {0, 1, 12, 20, 24, 255};
    const unsigned stride = format < 4 ? (format == 0 ? 12 : format == 1 ? 20 : 24) : descriptor ? custom_strides[(trial / 6) % 6] : 0;
    const unsigned boundary = stride ? 16384 / stride : 65535;
    const unsigned counts[] = {0, 1, 7, boundary ? boundary - 1 : 0, boundary, boundary + 1, 2 * boundary, 3 * boundary};
    const unsigned vertices = counts[(trial / 36) % 8] & 0xffff;
    ctx.ls[0x3b004] = uint8_t(format); ctx.ls[0x3b008] = uint8_t(vertices >> 8); ctx.ls[0x3b009] = uint8_t(vertices);
    geometry_job_put32(ctx.ls + 0x3b074, descriptor); geometry_job_put32(ctx.ls + 0x3b088, 0);
    if (descriptor) ctx.ls[descriptor + 1] = uint8_t(stride);
    const uint32_t offsets[] = {0x68, 0x60, 0x58, 0x58};
    for (unsigned i = 0; i < 4; ++i) geometry_job_put32(ctx.ls + 0x4104 + 4 * i, offsets[i]);
    if (publication) {
        ctx.gpr[4]._u32[0] = 0x3b200; ctx.gpr[5]._u32[0] = 0x3b240;
        const unsigned bytes = (vertices * stride + 15) & ~15u;
        const uint32_t cpu = trial & 128 ? 0xfffffff0u : 0x100000 + trial * 128;
        geometry_job_put32(ctx.ls + 0x3b200, cpu); geometry_job_put32(ctx.ls + 0x3b20c, cpu + bytes);
        geometry_job_put32(ctx.ls + 0x3b054, 0x10000); geometry_job_put32(ctx.ls + 0x3b150, trial % 32);
    } else {
        static const unsigned indices[] = {0, 1, 7, 8, 8191, 8192, 8193, 0x80000000u, 0xffffffffu};
        ctx.gpr[4]._u32[0] = indices[(trial / 6) % 9]; ctx.int_enable = trial & 1;
    }
    ctx.mfc_eah = trial * 0x109u; ctx.mfc_tag_status = trial * 0x127u;
    ctx.resv_ea = trial * 128; ctx.resv_valid = trial & 1;
}
static void compare(spu_context& native, spu_context& reference, unsigned trial, bool publication)
{
    GeometryOutputLayout cached;
    assert(read_geometry_output_layout(native, native.gpr[3]._u32[0], cached));
    const bool stale = (trial & 8) != 0;
    if (stale) {
        // Independent source changes: even padding/other lanes affect the
        // strict getter state and cannot be hidden by a cached host layout.
        if (cached.format < 4) native.ls[0x3b00f] ^= 1;
        else switch ((trial >> 4) & 3) {
        case 0: native.ls[0x3b00f] ^= 1; break;
        case 1: native.ls[0x3b080] ^= 1; break;
        case 2: native.ls[cached.descriptor_lsa + ((cached.custom._u32[1] + 8) & 15)] ^= 1; break;
        case 3: native.ls[cached.dispatch_lsa + 15] ^= 1; break;
        }
    }
    memcpy(&reference, &native, sizeof native); reference.ls = reference.ls_store;
    GeometryOutputSize size; GeometryVertexPublication publish;
    operations.clear();
    assert(publication ? read_geometry_vertex_publication(native, publish, &cached) : read_geometry_output_size(native, size));
    assert(operations.empty() && !memcmp(native.ls, reference.ls, SPU_LS_SIZE));
    if (publication) assert(publish.reused_layout == !stale);
    operations.clear(); g_spu_trampoline_fn = nullptr;
    if (publication) buzz_job_6eba_spu_func_000035F0(&reference);
    else buzz_job_6eba_spu_func_00002B80(&reference);
    drain(&reference, native.gpr[0]._u32[0]);
    const auto expected = operations;
    operations.clear();
    if (publication) publish_geometry_vertices(native, publish);
    else measure_geometry_output(native, size);
    native.pc = native.gpr[0]._u32[0];
    const auto diff = compare_function_state(native, reference);
    if (!diff.exact()) {
        fprintf(stderr, "%s case%u differs: r%d byte%d LS=%X/%u controls=%s\n", publication ? "publish" : "size", trial,
            diff.first_register, diff.first_register_byte, diff.first_ls, diff.ls_bytes,
            diff.first_state ? diff.first_state : "equal"); abort();
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
    for (bool publication : {false, true}) for (unsigned trial = 0; trial < 2048; ++trial) {
        seed(*native, trial, publication); compare(*native, *reference, trial, publication);
    }
    unsigned rejections = 0;
    for (bool publication : {false, true}) for (unsigned rejection = 0; rejection < (publication ? 12 : 8); ++rejection) {
        seed(*native, 5, publication);
        switch (rejection) {
        case 0: native->ls = nullptr; break;
        case 1: native->gpr[1]._u32[0] = 0x3ffff; break;
        case 2: native->gpr[1]._u32[0] = 0x20; break;
        case 3: native->gpr[3]._u32[0] = 0x3fff0; break;
        case 4: geometry_job_put32(native->ls + 0x3b088, 0x1234); break;
        case 5: geometry_job_put32(native->ls + 0x3b074, 0x3ffff); break;
        case 6: native->gpr[1]._u32[0] = 0x3b080; break;
        case 7: native->ls[0x3b004] = 0; geometry_job_put32(native->ls + 0x4104, 0); break;
        case 8: native->int_enable = 1; break;
        case 9: native->gpr[5]._u32[0] = 0x3b200; break;
        case 10: geometry_job_put32(native->ls + 0x3b054, 0x3ffff); break;
        case 11: geometry_job_put32(native->ls + 0x3b20c, 0); break;
        }
        memcpy(reference.get(), native.get(), sizeof(*native)); operations.clear();
        GeometryOutputSize size, size_before; GeometryVertexPublication publish, publish_before;
        memset(&size, 0xa5, sizeof size); memcpy(&size_before, &size, sizeof size);
        memset(&publish, 0xa5, sizeof publish); memcpy(&publish_before, &publish, sizeof publish);
        assert(!(publication ? read_geometry_vertex_publication(*native, publish) : read_geometry_output_size(*native, size)));
        assert(operations.empty() && !memcmp(native.get(), reference.get(), sizeof(*native)));
        assert(!memcmp(&size, &size_before, sizeof size) && !memcmp(&publish, &publish_before, sizeof publish));
        ++rejections;
    }
    printf("Native output layout: 2048 size + 2048 vertex publication full-state/channel/payload cases, cache invalidation, %u unchanged-state/no-I/O rejections PASS\n", rejections);
}
