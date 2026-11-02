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

static void job_exit_contract()
{
    auto expected=context(), actual=context();
    assert(!geometry_job_exit_difference(*expected,*actual));
    actual->gpr[2]._u64[0]^=1; actual->gpr[127]._u64[1]^=1;
    actual->ls[0xe800]^=1; actual->ls[SPU_LS_SIZE-1]^=1;
    assert(!geometry_job_exit_difference(*expected,*actual)); // Not published by this job.
    assert(!compare_function_state(*expected,*actual).exact()); // Optional debug still sees them.
    const char* fields[]={"pc","status","stop_code","list_stall_stat","host_depth","image_id","policy_mode","resident_ovl"};
    for(unsigned i=0;i<8;++i){
        auto changed=context();
        switch(i){
        case 0: changed->pc^=4; break;
        case 1: changed->status^=1; break;
        case 2: changed->stop_code^=1; break;
        case 3: changed->list_stall_stat^=1; break;
        case 4: changed->host_depth^=1; break;
        case 5: changed->image_id^=1; break;
        case 6: changed->policy_mode^=1; break;
        case 7: changed->resident_ovl^=1; break;
        }
        const auto* difference=geometry_job_exit_difference(*expected,*changed);
        assert(difference && !strcmp(difference,fields[i]));
    }
    actual->ch_out_mbox.count=1;actual->ch_out_mbox.value=actual->ch_out_mbox.q[0]=1;
    assert(!strcmp(geometry_job_exit_difference(*expected,*actual),"ch_out_mbox"));
    actual->ch_out_mbox={}; actual->ls=expected->ls_store;
    assert(!strcmp(geometry_job_exit_difference(*expected,*actual),"local-store ownership"));
    actual->ls=actual->ls_store;
    actual->irq_frame=reinterpret_cast<decltype(actual->irq_frame)>(uintptr_t(1));
    assert(!strcmp(geometry_job_exit_difference(*expected,*actual),"irq_frame"));
    puts("Job exit contract: internal GPR/LS differences allowed; PC/control/channel/host-state/LS-ownership faults rejected PASS");
}
static void prepared_input_contract()
{
    std::vector<GeometryJobIoOperation> ops(3);
    // Lookups are by EA/range, never by channel position or LS alias.
    ops[0].transfers.push_back({1,0x100,0x3000,{1,2,3,4}});
    ops[1].transfers.push_back({2,0x100,0x1000,{7,7,7,7}}); // PUT is not input.
    ops[1].transfers.push_back({3,0x100,0x2000,{8,8,8,8}}); // Neither is GETLLAR.
    ops[2].transfers.push_back({1,0x100,0x1000,{9,10,11,12}});
    GeometryJobIoReplay view(ops);
    assert(view.input(0x1001,2) && view.input(0x1001,2)[0]==10 && view.input(0x1001,2)[1]==11);
    assert(view.input(0x3000,4) && view.input(0x3000,4)[3]==4);
    assert(!view.input(0x2000,4) && !view.input(0x1003,2) && !view.input(0x1000,0));
    assert(!view.input(UINT32_MAX,2) && !view.input(0x1000,SIZE_MAX));
    assert(view.consumed()==0 && !view.error());
    ops[0].transfers.push_back({1,0x200,0x1000,{9,10,11,12}});
    assert(view.input(0x1000,4)); // Identical repeated GET is safe.
    ops[0].transfers.back().bytes[2]^=1;
    assert(!view.input(0x1000,4)); // Ambiguous full snapshot.
    ops[0].transfers.pop_back();
    ops[0].transfers.push_back({1,0x200,0x1002,{99}});
    assert(!view.input(0x1000,4)); // A partial overlapping GET differs too.
    assert(view.consumed()==0 && !view.error());
    ops[0].transfers.back().bytes[0]=11;
    assert(view.input(0x1000,4));
    puts("Prepared input: future GET lookup, exact bounded spans, repeated/partial ambiguity rejection, no channel consumption PASS");
}
int main()
{
    job_exit_contract();
    prepared_input_contract();
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
    {
        auto changed = context();
        std::vector<GeometryJobIoOperation> simple(3);
        for (unsigned i = 0; i < 3; ++i) {
            simple[i].channel = i;
            simple[i].before = simple[i].after = GeometryJobControls::capture(*changed);
        }
        GeometryJobIoReplay parent(simple);
        assert(parent.consume(*changed, GeometryJobIoKind::write, 0, 0, result));
        assert(parent.consume(*changed, GeometryJobIoKind::write, 1, 0, result));
        auto slice = parent.slice(1, 2);
        assert(slice.consumed() == 1 && !slice.error());
        assert(slice.consume(*changed, GeometryJobIoKind::write, 1, 0, result));
        assert(slice.finish() && parent.consumed() == 2);
        assert(!slice.consume(*changed, GeometryJobIoKind::write, 2, 0, result));
        assert(!slice.finish()); // Cannot consume the caller's following operation.
        auto short_slice = parent.slice(0, 2);
        assert(!short_slice.finish());
        auto empty = parent.slice(2, 2);
        assert(empty.finish());
        auto future = parent.slice(1, 3);
        auto reversed = parent.slice(2, 1);
        auto outside = parent.slice(0, SIZE_MAX);
        auto earlier = empty.slice(1, 2);
        assert(future.error() && reversed.error() && outside.error() && earlier.error());
        assert(!future.consume(*changed, GeometryJobIoKind::write, 1, 0, result));
        assert(parent.consume(*changed, GeometryJobIoKind::write, 2, 0, result) && parent.finish());
        assert(parent.consumed() == 3 && commits == 1 && guest == published);
    }
    printf("Geometry job I/O: bounded root slices, full exit state, ordered/overlapping DMA, six atomic/transfer kinds, "
           "no repeated guest commit, %u rejected corruptions, missing/extra channels and read/count PASS\n",
           rejected);
}
