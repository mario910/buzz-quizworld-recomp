/* Synthetic channel/DMA transactions: no game bytes or guest RAM mapping. */
#include "../port/native_geometry_job_io.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <memory>

using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
}

static std::unique_ptr<spu_context> context()
{
    auto ctx = std::make_unique<spu_context>();
    memset(ctx.get(), 0, sizeof(*ctx));
    ctx->ls = ctx->ls_store;
    ctx->mfc_lsa = 0xe800;
    ctx->mfc_eal = 0xf000;
    ctx->mfc_size = 8;
    ctx->mfc_tag = 3;
    for (unsigned i = 0; i < SPU_LS_SIZE; ++i) ctx->ls[i] = uint8_t(i * 71 + 13);
    return ctx;
}

static std::vector<GeometryJobIoOperation> record(spu_context& ctx,
                                                std::array<uint8_t, 128>& guest,
                                                unsigned& commits)
{
    std::vector<GeometryJobIoOperation> ops(2);
    auto& get = ops[0];
    get.channel = 21; // MFC_Cmd
    get.argument = 0x44; // GETL
    get.before = GeometryJobControls::capture(ctx);
    get.inputs.push_back({0xf000, {ctx.ls + 0xf000, ctx.ls + 0xf008}});
    get.transfers.push_back({1, 0xe800, 0x1000, std::vector<uint8_t>(16, 0x17)});
    get.transfers.push_back({1, 0xe808, 0x2000, std::vector<uint8_t>(16, 0x29)});
    for (const auto& transfer : get.transfers)
        memcpy(ctx.ls + transfer.lsa, transfer.bytes.data(), transfer.bytes.size());
    ctx.mfc_tag_status = 8;
    get.after = GeometryJobControls::capture(ctx);

    for (unsigned i = 0; i < 128; ++i) ctx.ls[0xe800 + i] ^= 0xa5;
    ctx.gpr[31]._u32[0] = 0xc001d00d;
    auto& put = ops[1];
    put.channel = 21;
    put.argument = 0xb0; // PUTLLUC
    put.before = GeometryJobControls::capture(ctx);
    put.transfers.push_back({6, 0xe800, 0x3000, {ctx.ls + 0xe800, ctx.ls + 0xe880}});
    memcpy(guest.data(), ctx.ls + 0xe800, guest.size());
    ++commits;
    ctx.atomic_stat = 2;
    ctx.resv_valid = 0;
    put.after = GeometryJobControls::capture(ctx);
    ctx.pc = 0x3ff00;
    return ops;
}

int main()
{
    auto reference = context();
    std::array<uint8_t, 128> guest{};
    unsigned commits = 0;
    const auto ops = record(*reference, guest, commits);
    const auto published = guest;
    auto shadow = context();
    GeometryJobIoReplay replay(ops);
    u128 result{};
    assert(replay.consume(*shadow, GeometryJobIoKind::write, 21, 0x44, result));
    // Ordered overlapping GETs must preserve the second element's bytes.
    assert(shadow->ls[0xe807] == 0x17 && shadow->ls[0xe808] == 0x29);
    for (unsigned i = 0; i < 128; ++i) shadow->ls[0xe800 + i] ^= 0xa5;
    shadow->gpr[31]._u32[0] = 0xc001d00d;
    assert(replay.consume(*shadow, GeometryJobIoKind::write, 21, 0xb0, result));
    shadow->pc = 0x3ff00;
    assert(replay.finish() && replay.consumed() == 2);
    assert(compare_function_state(*reference, *shadow).exact());
    assert(commits == 1 && guest == published);
    // A complete I/O trace is not sufficient for a full-job pass.
    shadow->gpr[127]._u32[3] ^= 1;
    assert(!compare_function_state(*reference, *shadow).exact());
    shadow->gpr[127]._u32[3] ^= 1;
    shadow->ls[0x3ffff] ^= 1;
    assert(!compare_function_state(*reference, *shadow).exact());
    shadow->ls[0x3ffff] ^= 1;
    ++shadow->pc;
    assert(!compare_function_state(*reference, *shadow).exact());
    --shadow->pc;
    assert(!replay.consume(*shadow, GeometryJobIoKind::read, 24, 0, result));
    assert(!replay.finish());

    unsigned rejected = 0;
    for (unsigned fault = 0; fault < 7; ++fault) {
        auto changed = context();
        GeometryJobIoReplay check(ops);
        auto kind = GeometryJobIoKind::write;
        unsigned channel = 21, argument = 0x44;
        switch (fault) {
        case 0: kind = GeometryJobIoKind::read; break;
        case 1: channel = 16; break;
        case 2: argument = 0x40; break;
        case 3: ++changed->mfc_lsa; break;
        case 4: changed->ls[0xf003] ^= 1; break;
        case 5: changed->irq_gpr[127]._u32[3] = 1; break;
        case 6: ++changed->event_status; break;
        }
        assert(!check.consume(*changed, kind, channel, argument, result));
        assert(check.error() && !check.finish() && check.consumed() == 0);
        ++rejected;
    }
    {
        auto changed = context();
        GeometryJobIoReplay check(ops);
        assert(check.consume(*changed, GeometryJobIoKind::write, 21, 0x44, result));
        assert(!check.finish()); // Missing output cannot count as a match.
    }
    {
        auto changed = context();
        GeometryJobIoReplay check(ops);
        assert(check.consume(*changed, GeometryJobIoKind::write, 21, 0x44, result));
        for (unsigned i = 0; i < 128; ++i) changed->ls[0xe800 + i] ^= 0xa5;
        changed->ls[0xe87f] ^= 1; // Padding/output tail is covered too.
        assert(!check.consume(*changed, GeometryJobIoKind::write, 21, 0xb0, result));
        assert(!check.finish() && commits == 1 && guest == published);
    }
    for (unsigned kind = 1; kind <= 6; ++kind) {
        auto changed = context();
        std::vector<GeometryJobIoOperation> single(1);
        auto& op = single[0];
        op.before = op.after = GeometryJobControls::capture(*changed);
        op.transfers.push_back({kind, SPU_LS_SIZE - 2, 0x3000,
                               {changed->ls[SPU_LS_SIZE - 2], changed->ls[SPU_LS_SIZE - 1]}});
        GeometryJobIoReplay check(single);
        assert(check.consume(*changed, GeometryJobIoKind::write, 0, 0, result) && check.finish());
        assert(commits == 1 && guest == published); // No atomic kind publishes twice.
    }
    for (unsigned fault = 0; fault < 4; ++fault) {
        auto changed = context();
        auto bad = ops;
        switch (fault) {
        case 0: bad[0].inputs[0].lsa = UINT32_MAX; break;
        case 1: bad[0].transfers[0].kind = 7; break;
        case 2: bad[0].transfers[0].lsa = SPU_LS_SIZE - 15; break;
        case 3: bad[0].transfers[0].lsa = UINT32_MAX; break;
        }
        GeometryJobIoReplay check(bad);
        assert(!check.consume(*changed, GeometryJobIoKind::write, 21, 0x44, result));
        assert(!check.finish());
        ++rejected;
    }
    for (auto kind : {GeometryJobIoKind::read, GeometryJobIoKind::count}) {
        auto changed = context();
        changed->ch_in_mbox.count = 2;
        changed->ch_in_mbox.q[0] = 0x1234;
        changed->ch_in_mbox.q[1] = 0x5678;
        std::vector<GeometryJobIoOperation> single(1);
        auto& op = single[0];
        op.kind = kind;
        op.channel = 29;
        op.before = GeometryJobControls::capture(*changed);
        op.after = op.before;
        op.result._u32[0] = 0xabcdef12;
        op.result._u32[3] = 0x13579bdf;
        // Same logical queue, different physical rotation.
        changed->ch_in_mbox.head = 5;
        changed->ch_in_mbox.q[5] = 0x1234;
        changed->ch_in_mbox.q[6] = 0x5678;
        GeometryJobIoReplay check(single);
        assert(check.consume(*changed, kind, 29, 0, result) && check.finish());
        assert(result._u32[0] == 0xabcdef12 && result._u32[3] == 0x13579bdf);
    }
    {
        auto changed = context();
        changed->pc = 0x1122;
        changed->gpr[80]._u32[2] = 0xaabbccdd;
        changed->mfc_private = reinterpret_cast<void*>(uintptr_t(0x1234));
        const auto memory = std::vector<uint8_t>(changed->ls, changed->ls + SPU_LS_SIZE);
        std::vector<GeometryJobIoOperation> single(1);
        single[0].before = single[0].after = GeometryJobControls::capture(*changed);
        single[0].after.mfc_tag_status = 8;
        // Lock values differ from the recording and must survive application.
        changed->ch_out_mbox.lock = 11;
        changed->ch_in_mbox.lock = 12;
        changed->ch_out_intr_mbox.lock = 13;
        changed->ch_sig_notify[0].lock = 14;
        changed->ch_sig_notify[1].lock = 15;
        GeometryJobIoReplay check(single);
        assert(check.consume(*changed, GeometryJobIoKind::write, 0, 0, result) && check.finish());
        assert(changed->mfc_tag_status == 8 && changed->pc == 0x1122);
        assert(changed->gpr[80]._u32[2] == 0xaabbccdd);
        assert(changed->mfc_private == reinterpret_cast<void*>(uintptr_t(0x1234)));
        assert(!memcmp(changed->ls, memory.data(), memory.size()));
        assert(changed->ch_out_mbox.lock == 11 && changed->ch_in_mbox.lock == 12);
        assert(changed->ch_out_intr_mbox.lock == 13);
        assert(changed->ch_sig_notify[0].lock == 14 && changed->ch_sig_notify[1].lock == 15);
    }
    printf("Geometry job I/O: full exit state, ordered/overlapping DMA, six atomic/transfer kinds, "
           "no repeated guest commit, %u rejected corruptions, missing/extra channels and read/count PASS\n",
           rejected);
}
