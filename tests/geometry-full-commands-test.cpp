#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_command_publish.h"
#include "../port/native_geometry_full_commands.h"
#if defined(BUZZ_TEST_HOST_COMMANDS)
#include "../port/native_geometry_host_commands.h"
#endif
#include "../port/native_geometry_job_io.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace buzz;
static std::vector<GeometryJobIoOperation> operations;
static unsigned prepare_reused=0, publish_reused=0;

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
        assert((op.argument == 34 || op.argument == 66) && ctx->mfc_lsa + ctx->mfc_size <= SPU_LS_SIZE);
        std::vector<uint8_t> bytes;
        if (op.argument == 66) {
            assert(ctx->mfc_size == 4); bytes.resize(4); geometry_job_put32(bytes.data(), ctx->mfc_eal ^ 0x75815731);
            memcpy(ctx->ls + ctx->mfc_lsa, bytes.data(), 4);
        } else bytes.assign(ctx->ls + ctx->mfc_lsa, ctx->ls + ctx->mfc_lsa + ctx->mfc_size);
        op.transfers.push_back({op.argument == 66 ? 1u : 2u, ctx->mfc_lsa, ctx->mfc_eal, bytes});
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
#include "../analysis/native-geometry/phase3/test-full-commands-reference.inc"
extern "C" void spu_indirect_branch(spu_context* ctx) { auto fn = full_commands_reference_lookup(ctx->pc); assert(fn); fn(ctx); }
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
    ctx.gpr[4]._u32[0] = 0x100000 + (trial % 32) * 4; ctx.gpr[5]._u32[0] = 0x28000;
    ctx.gpr[7]._u32[1] = (random_word(random) & ~127u) + (trial % 128);
    const unsigned counts[] = {0,1,2,63,64,255,256,257,512,513,1024,4096,4095,8192};
    ctx.gpr[8]._u32[0] = counts[(trial / 32) % 14];
    ctx.gpr[9]._u32[0] = trial % 8 ? 0x2a000 + (trial % 4) * 4 : 0;
    const unsigned metadata = ctx.gpr[9]._u32[0] ? ctx.gpr[9]._u32[0] : 0x2a000;
    geometry_job_put32(ctx.ls + metadata + 32, trial & 64 ? 8 : 1);
    const unsigned modes[] = {0, 1, 2305, 9}; geometry_job_put32(ctx.ls + metadata + 36, modes[(trial / 4) % 4]);
    ctx.ls[0x3b001] = uint8_t(ctx.gpr[8]._u32[0] > 4096 ? 255 : 32 + trial % 64); geometry_job_put32(ctx.ls + 0x3b058, 0x10000 + (trial % 32) * 4);
    geometry_job_put32(ctx.ls + 0x3b074, 0x2c000); ctx.ls[0x2c000] = uint8_t((trial / 128) % 9);
    for (unsigned i = 0; i < ctx.ls[0x2c000]; ++i) ctx.ls[0x2c00d + i * 8] = uint8_t((trial + i * 3) % 16);
    geometry_job_put32(ctx.ls + 0x28000, 0x200000 + trial * 128);
    geometry_job_put32(ctx.ls + 0x28008, trial & 1 ? 0 : 1);
    geometry_job_put32(ctx.ls + 0x28010, trial & 2 ? 0 : 0x200000 + trial * 16);
    ctx.ls[0x28014] = uint8_t(trial & 4 ? 0 : 1);
    static constexpr uint8_t shift[] = {12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27};
    memcpy(ctx.ls + 0xcea0, shift, 16);
    if (trial & 2048) ctx.gpr[4]._u32[0] = 0;
    if (ctx.gpr[9]._u32[0]) {
        const unsigned passes = 1 + (trial / 64) % 3;
        geometry_job_put32(ctx.ls + metadata, passes);
        for (unsigned pass = 0; pass < passes; ++pass) {
            const unsigned at = metadata + 16 + pass * 64;
            for (unsigned field : {0u,2u,4u,6u,8u,12u,14u})
                geometry_job_put32(ctx.ls + at + field * 4, ((trial >> (field % 7)) + pass + field) & 1 ? 8 : 1);
            geometry_job_put32(ctx.ls + at + 20, modes[(trial + pass) % 4]);
        }
    }
    ctx.gpr[11]._u32[1] = (ctx.gpr[11]._u32[1] & ~15u) | ((trial / 32) % 16);
    ctx.ls[0x3b001] = 255;
    ctx.pc = 0x12f0;
    ctx.mfc_eah = trial * 0x109u; ctx.mfc_tag_status = trial * 0x127u;
    ctx.resv_ea = trial * 128; ctx.resv_valid = trial & 1;
}
static void native_prepare(spu_context* ctx) {
    GeometryCommandBuffer p; assert(read_geometry_command_buffer(*ctx,p));
    const auto* cached=geometry_command_admission ? geometry_command_admission->take_prepare(*ctx) : nullptr;
    assert(cached); ++prepare_reused;
    // Compare initialized members; padding before the aligned vector fields
    // is not part of the reservation contract.
    assert(!memcmp(&p,cached,11*sizeof(uint32_t)));
    assert(!memcmp(&p.context_arg,&cached->context_arg,8*sizeof(u128)));
    prepare_geometry_command_buffer(*ctx,*cached); SPU_RET(ctx);
}
static void native_bind(spu_context* ctx) { GeometryCommandBindings p; assert(read_geometry_command_bindings(*ctx,false,p)); bind_geometry_command_attributes(*ctx,p); SPU_RET(ctx); }
static void native_reset(spu_context* ctx) { GeometryCommandBindings p; assert(read_geometry_command_bindings(*ctx,true,p)); reset_geometry_command_bindings(*ctx,p); SPU_RET(ctx); }
static void native_publish(spu_context* ctx) {
    GeometryCommandPublication p,expected; assert(read_geometry_command_publication(*ctx,expected));
    const auto* forecast=geometry_command_admission ? geometry_command_admission->take_publish(*ctx) : nullptr;
    assert(forecast); ++publish_reused;
    assert(read_geometry_command_publication(*ctx,p,nullptr,forecast));
    assert(!memcmp(&p,&expected,sizeof p));
    publish_geometry_commands(*ctx,p); SPU_RET(ctx);
}
static void compare(spu_context& native, spu_context& reference, unsigned trial)
{
    memcpy(&reference, &native, sizeof native); reference.ls = reference.ls_store;
    GeometryFullCommands plan; operations.clear();
    assert(read_geometry_full_commands(native, plan) && operations.empty());
    buzz_job_6eba_spu_func_000012F0(&reference); drain(&reference, native.gpr[0]._u32[0]);
    const auto expected = operations;
    operations.clear();
    const buzz_geometry_command_functions original{buzz_job_6eba_spu_func_000032C8,buzz_job_6eba_spu_func_00000100,buzz_job_6eba_spu_func_00000020,buzz_job_6eba_spu_func_000027A0};
    const buzz_geometry_command_functions combined{native_prepare,native_bind,native_reset,native_publish};
    const auto& callbacks=(trial < 8192 ? trial & 4096 : trial & 1) ? combined : original;
#if defined(BUZZ_TEST_HOST_COMMANDS)
    emit_geometry_full_commands_host(native,plan,callbacks);
#else
    emit_geometry_full_commands(native,plan,callbacks);
#endif
    native.pc = native.gpr[0]._u32[0];
    assert(!geometry_command_admission);
#if defined(BUZZ_TEST_HOST_COMMANDS)
    // The whole-job caller owns volatile temporaries. r3, the caller's link,
    // stack, nonvolatile registers, every LS byte and all controls stay exact.
    assert(native.host_depth==reference.host_depth && native.image_id==reference.image_id &&
           native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl &&
           native.irq_frame==reference.irq_frame && native.ls==native.ls_store);
    auto observed=std::unique_ptr<spu_context,decltype(&release_function_state)>(clone_function_state(&reference),release_function_state);
    for(unsigned reg=2;reg<80;++reg)if(reg!=3)observed->gpr[reg]=native.gpr[reg];
    const auto diff=compare_function_state(native,*observed);
#else
    const auto diff = compare_function_state(native, reference);
#endif
    if (!diff.exact()) {
        fprintf(stderr, "full command case%u indices=%u start=%X expectedend=%X differs: r%d byte%d LS=%X/%u controls=%s\n", trial,
            plan.indices, plan.reservation.start, plan.relocated_end, diff.first_register, diff.first_register_byte, diff.first_ls, diff.ls_bytes,
            diff.first_state ? diff.first_state : "equal");
        if (diff.first_register >= 0) for (unsigned lane = 0; lane < 4; ++lane)
            fprintf(stderr, " lane%u native=%08X reference=%08X\n", lane, native.gpr[diff.first_register]._u32[lane], reference.gpr[diff.first_register]._u32[lane]);
        abort();
    }
    if (!plan.reservation.destination._u32[0]) {
        assert(geometry_job_be32(native.ls + plan.stack - 240 + 40) == plan.raw_end);
    } else {
        unsigned puts = 0;
        for (const auto& operation : operations) for (const auto& transfer : operation.transfers) if (transfer.kind == 2) {
            unsigned at = 0, last_packet = 0;
            while (at < transfer.bytes.size()) {
                const uint32_t header = geometry_job_be32(transfer.bytes.data() + at);
                const unsigned bytes = 4 * (1 + ((header >> 18) & 2047));
                assert(bytes <= transfer.bytes.size() - at);
                at += bytes; if (header) last_packet = at;
            }
            assert(plan.reservation.start + last_packet == plan.relocated_end); ++puts;
        }
        assert(puts == 1);
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
static void admission_guards()
{
    auto c=std::make_unique<spu_context>(), before=std::make_unique<spu_context>();
    unsigned rejected=0;
    for (unsigned publication=0;publication<2;++publication) for (unsigned fault=0;fault<18;++fault) {
        seed(*c,33); GeometryFullCommands p; assert(read_geometry_full_commands(*c,p));
        const auto& r=p.reservation;
        GeometryCommandAdmissionScope scope(*c,r,p.raw_end,p.relocated_end,0x1380,0x1a68);
        c->gpr[1]._u32[0]=r.stack;
        c->gpr[3]=r.context_arg; c->gpr[4]=r.output_arg; c->gpr[5]=r.destination;
        c->gpr[6]=r.records_arg; c->gpr[7]=r.count_arg; c->gpr[0]=spu_link(0x1380);
        if(publication) {
            assert(scope.take_prepare(*c)); prepare_geometry_command_buffer(*c,r);
            c->gpr[3]=r.context_arg; c->gpr[4]=r.output_arg; c->gpr[5]=r.destination;
            c->gpr[6]=r.records_arg; c->gpr[7]=r.count_arg; c->gpr[0]=spu_link(0x1a68);
            geometry_job_put32(c->ls+r.output+8,p.raw_end-scope.semaphore_bytes);
        }
        switch(fault) {
        case 0: c->gpr[1]._u32[0]+=16; break;
        case 1: c->gpr[0]._u32[0]+=4; break;
        case 2: case 3: case 4: case 5: case 6: c->gpr[3+fault-2]._u32[0]^=16; break;
        case 7: c->gpr[3]._u32[3]^=1; break;
        case 8: c->ls[r.records+16]^=1; break;
        case 9: c->int_enable=1; break;
        case 10: c->ls=before->ls_store; break;
        case 11: scope.owner=before.get(); break;
        case 12:
            if(publication) ++c->ls[r.output+3]; else ++c->ls[r.context]; break;
        case 13:
            if(publication) ++c->ls[r.output+7]; else ++c->ls[r.context+0x50]; break;
        case 14:
            if(publication) ++c->ls[r.output+11]; else ++c->ls[r.context+0x150]; break;
        case 15:
            if(publication) scope.prepared=false; else scope.prepared=true; break;
        case 16:
            if(publication) scope.published=true; else scope.prepare_link=0; break;
        case 17:
            if(publication) scope.publish_link=0; else scope.owner=nullptr; break;
        }
        memcpy(before.get(),c.get(),sizeof(*c)); operations.clear();
        if(publication) assert(!scope.take_publish(*c)); else assert(!scope.take_prepare(*c));
        assert(operations.empty() && !memcmp(before.get(),c.get(),sizeof(*c))); ++rejected;
    }
    assert(!geometry_command_admission);
    seed(*c,33); GeometryFullCommands p; assert(read_geometry_full_commands(*c,p));
    {
        GeometryCommandAdmissionScope outer(*c,p.reservation,p.raw_end,p.relocated_end,0x1380,0x1a68);
        {
            GeometryCommandAdmissionScope inner(*before,p.reservation,p.raw_end,p.relocated_end,0x1380,0x1a68);
            assert(geometry_command_admission==&inner && !inner.take_prepare(*c));
        }
        assert(geometry_command_admission==&outer);
    }
    assert(!geometry_command_admission);
    printf("Command proof guards: %u unchanged-context/no-I/O rejections and nested/foreign scopes PASS\n",rejected);
}
int main()
{
    admission_guards();
    auto native = std::make_unique<spu_context>(), reference = std::make_unique<spu_context>();
    for (unsigned trial = 0; trial < 8192; ++trial) { seed(*native, trial); compare(*native,*reference,trial); }
    for (unsigned trial = 0; trial < 64; ++trial) {
        seed(*native, 256 + trial); native->gpr[4]._u32[0] = 0; native->ls[0x3b001] = 0;
        compare(*native,*reference,8192 + trial);
    }
    for (unsigned trial = 0; trial < 64; ++trial) {
        seed(*native, 256 + trial); native->gpr[4]._u32[0] = 0;
        native->gpr[8]._u32[0] = (trial & 1 ? 0x100000 : 0x7ff01) + trial * 256;
        compare(*native,*reference,8256 + trial);
    }
    for (unsigned trial = 0; trial < 128; ++trial) {
        seed(*native, trial);
        GeometryFullCommands plan; assert(read_geometry_full_commands(*native,plan));
        const unsigned bytes = plan.relocated_end - plan.reservation.start;
        if (native->gpr[4]._u32[0]) native->ls[0x3b001] = uint8_t((bytes+15)/16);
        compare(*native,*reference,8320 + trial);
    }
    for (unsigned trial = 0; trial < 64; ++trial) {
        seed(*native,trial); native->gpr[4]._u32[0] = 0; native->gpr[9]._u32[0] = 0x2a000;
        native->gpr[8]._u32[0] = (trial*17)%1024;
        geometry_job_put32(native->ls + 0x2a000,16);
        compare(*native,*reference,8448 + trial);
    }
    for (unsigned rejection = 0; rejection < 32; ++rejection) {
        seed(*native,33);
        switch (rejection) {
        case 0: native->ls = nullptr; break;
        case 1: native->int_enable = 1; break;
        case 2: native->gpr[1]._u32[0] = 0x3bfff; break;
        case 3: native->gpr[1]._u32[0] = 0x170; break;
        case 4: native->gpr[3]._u32[0] = 0x3fff0; break;
        case 5: native->gpr[4]._u32[0] |= 1; break;
        case 6: native->gpr[5]._u32[0] = 0x3ffff; break;
        case 7: geometry_job_put32(native->ls + 0x3b058,0x1000); break;
        case 8: geometry_job_put32(native->ls + 0x3b074,0x3ffff); break;
        case 9: native->gpr[9]._u32[0] |= 1; break;
        case 10: native->gpr[9]._u32[0] = 0x3fff0; geometry_job_put32(native->ls+0x3fff0,1); break;
        case 11: native->ls[0x3b001] = 1; break;
        case 12: geometry_job_put32(native->ls+0x28000,0x200001); break;
        case 13: native->gpr[5]._u32[0] = 0x3b000; break;
        case 14: geometry_job_put32(native->ls+0x3b074,0x3bff0); native->ls[0x3bff0]=1; break;
        case 15: geometry_job_put32(native->ls+0x3b058,0x2a000); break;
        case 16: geometry_job_put32(native->ls+0x3b058,0x2c000); break;
        case 17: native->gpr[1]._u32[0] = 0x2c100; break;
        case 18: native->gpr[9]._u32[0] = 0x3b000; geometry_job_put32(native->ls+0x3b000,1); break;
        case 19: native->gpr[8]._u32[0] = 0x80000; break;
        case 20: native->ls[0x2c000] = 255; native->gpr[12]._u32[0]=65535; for(unsigned i=0;i<255;++i)native->ls[0x2c00d+i*8]=0; break;
        case 21: geometry_job_put32(native->ls+0x3b058,0x3bfe0); break;
        case 22: native->gpr[5]._u32[0] = 0x3fff0; break;
        case 23: native->gpr[1]._u32[0] = SPU_LS_SIZE; break;
        case 24: native->gpr[8]._u32[0] = 0xffffffff; break;
        case 25: native->gpr[4]._u32[0] = 0; native->gpr[8]._u32[0] = 0xffffffff; break;
        case 26: geometry_job_put32(native->ls+native->gpr[9]._u32[0],0); break;
        case 27: geometry_job_put32(native->ls+native->gpr[9]._u32[0],0xffffffff); break;
        case 28: geometry_job_put32(native->ls+native->gpr[9]._u32[0],4096); break;
        case 29: native->gpr[9]._u32[0] = 0x3bf00; geometry_job_put32(native->ls+0x3bf00,1); break;
        case 30: native->gpr[9]._u32[0] = 0x28000; geometry_job_put32(native->ls+0x28000,1); break;
        case 31: native->gpr[9]._u32[0] = 0x10000; geometry_job_put32(native->ls+0x10000,1); break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native)); operations.clear();
        GeometryFullCommands plan; plan.stack=0xa5a5a5a5; plan.pass_count=0x12345678;
        const auto before = plan;
        if(read_geometry_full_commands(*native,plan)) { fprintf(stderr,"full command rejection %u unexpectedly accepted\n",rejection); abort(); }
        assert(operations.empty() && !memcmp(native.get(),reference.get(),sizeof(*native)));
        assert(plan.stack==before.stack && !memcmp(&plan.reservation,&before.reservation,sizeof plan.reservation) &&
            !memcmp(&plan.format,&before.format,sizeof plan.format) && plan.pass_count==before.pass_count);
    }
#if defined(BUZZ_TEST_HOST_COMMANDS)
    assert(prepare_reused>4000 && !publish_reused);
    puts("Host full commands: live ABI, complete LS/control/channel/packet comparisons PASS; volatile temporaries excluded");
#else
    assert(prepare_reused>4000 && publish_reused==prepare_reused);
    printf("Command proof reuse: %u preparation / %u publication matches independent readers PASS\n",prepare_reused,publish_reused);
    puts("Native full commands: 8512 strict full-state/channel + independent packet-size cases, original/native callees, 32 unchanged-state/no-I/O rejections PASS");
#endif
}
