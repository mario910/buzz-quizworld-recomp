#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_blend_lists.h"
#include "../port/native_function_check.h"
#include "../port/native_geometry_job_io.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_stop(spu_context*) { abort(); }
void spu_img_restore(spu_context* ctx, int32_t image) { assert(ctx->image_id == image); }
}
#include "../analysis/native-geometry/phase3/test-blend-lists-reference.inc"

struct Operation { uint32_t channel, argument; GeometryJobControls before, after; };
struct Call { unsigned which; std::vector<uint8_t> registers, stack; GeometryJobControls controls; uint32_t pc, depth; int image; };
static std::vector<uint8_t> guest(1 << 20);
static std::vector<Operation> expected_io;
static std::vector<Call> expected_calls;
static bool trial_native, with_normal;
static unsigned io_cursor, call_cursor, list_gets, restarts, filtered;
static uint32_t random_word(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

static void run_until(spu_context* ctx, buzz_geometry_job_fn first, buzz_geometry_job_fn tail)
{
    g_spu_trampoline_fn = nullptr; first(ctx);
    for (unsigned steps = 0; g_spu_trampoline_fn != tail; ++steps) {
        if (steps > 200000 || !g_spu_trampoline_fn) { fprintf(stderr, "escaped list oracle pc%X\n", ctx->pc); abort(); }
        auto next = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; next(ctx);
    }
    g_spu_trampoline_fn = nullptr; tail(ctx);
}
extern "C" void spu_indirect_branch(spu_context* ctx)
{
    auto fn = list_reference_lookup(ctx->pc); assert(fn); fn(ctx);
}
extern "C" void spu_drain_call(spu_context* ctx, uint32_t link)
{
    // The four bounded callees below return synchronously, just like an SPU
    // leaf at positive host depth. Never consume a parent continuation.
    assert(ctx->pc == link && !g_spu_trampoline_fn);
}
static void child(spu_context* ctx, unsigned which)
{
    Call call{which, std::vector<uint8_t>(reinterpret_cast<uint8_t*>(ctx->gpr), reinterpret_cast<uint8_t*>(ctx->gpr) + sizeof(ctx->gpr)),
              std::vector<uint8_t>(ctx->ls + ctx->gpr[1]._u32[0], ctx->ls + ctx->gpr[1]._u32[0] + 448),
              GeometryJobControls::capture(*ctx), ctx->pc, ctx->host_depth, ctx->image_id};
    if (!trial_native) expected_calls.push_back(call);
    else {
        assert(call_cursor < expected_calls.size());
        const auto& expected = expected_calls[call_cursor++];
        if (call.which != expected.which || call.registers != expected.registers || call.stack != expected.stack ||
            call.pc != expected.pc || call.depth != expected.depth || call.image != expected.image || expected.controls.difference(*ctx)) {
            fprintf(stderr, "child%u call%u ABI differs pc%X\n", which, call_cursor, ctx->pc);
            for(unsigned r=0;r<128;++r)if(memcmp(call.registers.data()+r*16, expected.registers.data()+r*16,16)) fprintf(stderr,"r%u\n",r);
            abort();
        }
    }
    // Observable child spills depend on ALL live high registers, not just
    // documented input words. Also clobber volatile registers deterministically.
    for (unsigned reg = 80; reg < 128; ++reg)
        spu_ls_write128(ctx, ctx->gpr[1]._u32[0] - 1024 + (reg - 80) * 16, ctx->gpr[reg]);
    for (unsigned reg = 2; reg < 80; ++reg)
        ctx->gpr[reg] = spu_xori(ctx->gpr[reg], int(which * 17));
    // r87 must be callee-saved; the enclosing function uses it after each call.
    if (which == 2) ctx->gpr[3] = spu_il(with_normal ? 0x28000 : 0);
    ctx->pc = ctx->gpr[0]._u32[0]; g_spu_trampoline_fn = nullptr;
}
static void buzz_job_6eba_spu_func_00007118(spu_context* ctx) { child(ctx, 0); }
static void buzz_job_6eba_spu_func_00007DF8(spu_context* ctx) { child(ctx, 1); }
static void buzz_job_6eba_spu_func_00002780(spu_context* ctx) { child(ctx, 2); }
static void buzz_job_6eba_spu_func_00002C00(spu_context* ctx) { child(ctx, 3); }

static void io_check(spu_context* ctx, uint32_t channel, uint32_t argument, bool before)
{
    if (!trial_native) {
        if (before) expected_io.push_back({channel, argument, GeometryJobControls::capture(*ctx), {}});
        else expected_io.back().after = GeometryJobControls::capture(*ctx);
    } else {
        assert(io_cursor < expected_io.size()); const auto& expected = expected_io[io_cursor];
        const auto* difference = (before ? expected.before : expected.after).difference(*ctx);
        if (expected.channel != channel || expected.argument != argument || difference) {
            fprintf(stderr, "channel%u op%u arg%X/%X state%s\n", channel, io_cursor, argument, expected.argument, difference ? difference : "equal"); abort();
        }
        if (!before) ++io_cursor;
    }
}
extern "C" void buzz_geometry_job_wrch(spu_context* ctx, uint32_t channel, u128 value)
{
    const auto arg = value._u32[0]; io_check(ctx, channel, arg, true);
    switch (channel) {
    case MFC_LSA: ctx->mfc_lsa = arg; break;
    case MFC_EAL: ctx->mfc_eal = arg; break;
    case MFC_Size: ctx->mfc_size = arg; break;
    case MFC_TagID: ctx->mfc_tag = arg; break;
    case MFC_WrTagMask: ctx->mfc_tag_mask = arg; break;
    case MFC_WrTagUpdate: break;
    case MFC_Cmd:
        if (arg == 64) {
            assert(ctx->mfc_eal + ctx->mfc_size <= guest.size());
            memcpy(ctx->ls + ctx->mfc_lsa, guest.data() + ctx->mfc_eal, ctx->mfc_size);
        } else {
            assert(arg == 68 && ctx->mfc_size == 8);
            const auto list = ctx->ls + (ctx->mfc_eal & SPU_LS_MASK);
            const auto size = geometry_job_be32(list) & 0x7fff;
            const auto ea = geometry_job_be32(list + 4);
            assert(ea + size <= guest.size() && ctx->mfc_lsa + size <= SPU_LS_SIZE);
            memcpy(ctx->ls + ctx->mfc_lsa, guest.data() + ea, size);
            if (trial_native) ++list_gets;
        }
        ctx->mfc_tag_status |= 1u << (ctx->mfc_tag & 31); break;
    default: abort();
    }
    io_check(ctx, channel, arg, false);
}
extern "C" u128 buzz_geometry_job_rdch(spu_context* ctx, uint32_t channel)
{
    io_check(ctx, channel, UINT32_MAX, true);
    u128 value{};
    if (channel == SPU_RdMachStat) value._u32[0] = ctx->int_enable;
    else { assert(channel == MFC_RdTagStat); value._u32[0] = ctx->mfc_tag_status & ctx->mfc_tag_mask; }
    io_check(ctx, channel, UINT32_MAX, false); return value;
}
static void seed(spu_context& ctx, unsigned trial)
{
    memset(&ctx, 0, sizeof ctx); ctx.ls = ctx.ls_store; ctx.image_id = 41; ctx.pc = 0xbf8;
    uint32_t state = trial + 1;
    for (auto& r : ctx.gpr) for (auto& word : r._u32) word = random_word(state);
    for (unsigned i = 0; i < SPU_LS_SIZE; ++i) ctx.ls[i] = uint8_t(random_word(state));
    const unsigned counts[] = {0,1,2,3,4,5,7,8,9,15,16,17,31,32,63,64,127,128};
    const unsigned count = counts[(trial / 8) % 18];
    ctx.gpr[0]._u32[0] = trial & 2 ? 0xd04 : 0x25c4; ctx.host_depth = trial & 1; ctx.int_enable = (trial / 2) & 1;
    ctx.gpr[1]._u32[0] = 0x3f000; ctx.gpr[3]._u32[0] = 0xe800;
    ctx.gpr[4] = spu_splat_u32(0x3f800000); ctx.gpr[5]._u32[0] = count;
    ctx.gpr[6]._u32[0] = 0x20000; ctx.gpr[7]._u32[0] = 0x10000;
    ctx.gpr[8]._u32[0] = trial & 4 ? 0x25004 : 0;
    geometry_job_put32(ctx.ls + 0xe858, 0x10000);
    geometry_job_put32(ctx.ls + 0xe97c, 0x10000 + ((trial / 144) % 3 == 0 ? 176 : 512));
    geometry_job_put32(ctx.ls + 0xe954, 3);
    ctx.ls[0xe806] = ctx.ls[0xe807] = ctx.ls[0xe808] = 0; ctx.ls[0xe809] = 16;
    geometry_job_put32(ctx.ls + 0x25004, 0);
    for (unsigned i = 0; i < count; ++i) {
        const unsigned mode = (trial / 432) % 5;
        geometry_job_put32(guest.data() + 0x10000 + i * 4, mode == 1 || (mode == 2 && i % 2) ? 0 : mode == 4 && i % 3 ? 0x3ca3d70a : 0x3f800000);
        geometry_job_put32(guest.data() + 0x20000 + i * 8, mode == 3 && i % 2 ? 0 : 128);
        geometry_job_put32(guest.data() + 0x20004 + i * 8, mode == 3 && i % 2 ? 0 : 0x30000 + i * 1024);
        auto* payload = guest.data() + 0x30000 + i * 1024;
        for (unsigned j = 0; j < 128; ++j) payload[j] = uint8_t(random_word(state));
        geometry_job_put32(payload, 16); geometry_job_put32(payload + 4, 16); geometry_job_put32(payload + 8, 16);
        geometry_job_put32(payload + 12, 8); geometry_job_put32(payload + 16, trial % 5 ? 4 : 0);
    }
    with_normal = trial % 3 != 0;
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR); _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    auto native = std::make_unique<spu_context>(), reference = std::make_unique<spu_context>();
    GeometryBlendListFunctions library{buzz_job_6eba_spu_func_00007118, buzz_job_6eba_spu_func_00007DF8, buzz_job_6eba_spu_func_00002780, buzz_job_6eba_spu_func_00002C00};
    for (unsigned trial = 0; trial < 2160 + 512; ++trial) {
        seed(*native, trial); memcpy(reference.get(), native.get(), sizeof(*native)); reference->ls = reference->ls_store;
        if (trial >= 2160) {
            const uint32_t bits[] = {0,0x80000000,1,0x007fffff,0x00800000,0x3f800000,0xbf800000,
                0x3ccccccb,0x3ccccccc,0x3ccccccd,0x7f800000,0xff800000,0x7fc00001,0x7fffffff,0xffffffff,0x12345678};
            native->gpr[4] = spu_splat_u32(bits[trial % 16]);
            for (unsigned i=0;i<128;++i) geometry_job_put32(guest.data()+0x10000+i*4,bits[(trial/16+i)%16]);
            memcpy(reference.get(), native.get(), sizeof(*native)); reference->ls = reference->ls_store;
        }
        GeometryBlendLists plan; assert(read_geometry_blend_lists(*native, plan));
        expected_io.clear(); expected_calls.clear(); trial_native = false; io_cursor = call_cursor = 0;
        run_until(reference.get(), buzz_job_6eba_spu_func_00000BF8, buzz_job_6eba_spu_func_00000D04);
        const auto expected_next = g_spu_trampoline_fn;
        trial_native = true; g_spu_trampoline_fn = nullptr; blend_geometry_lists(*native, plan, library);
        const auto difference = compare_function_state(*native, *reference);
        if (!difference.exact() || expected_next != g_spu_trampoline_fn) {
            fprintf(stderr, "list trial%u count%u:r%d byte%d LS%X/%u state%s\n", trial, plan.count, difference.first_register, difference.first_register_byte,
                    difference.first_ls, difference.ls_bytes, difference.first_state ? difference.first_state : "equal"); abort();
        }
        assert(io_cursor == expected_io.size() && call_cursor == expected_calls.size());
        assert(native->host_depth == reference->host_depth && native->image_id == reference->image_id);
        restarts += geometry_job_be32(native->ls + 0x25004); filtered += plan.count && expected_calls.empty();
    }
    assert(list_gets && restarts && filtered);
    for (unsigned fault=0;fault<15;++fault) {
        seed(*native,8);
        switch(fault) {
        case 0: native->gpr[1]._u32[0] |= 1; break;
        case 1: native->gpr[1]._u32[0] = 0xe680; break;
        case 2: native->gpr[1]._u32[0] = SPU_LS_SIZE-16; break;
        case 3: native->gpr[5]._u32[0] = 129; break;
        case 4: native->gpr[3]._u32[0] |= 4; break;
        case 5: native->gpr[3]._u32[0] = SPU_LS_SIZE-16; break;
        case 6: geometry_job_put32(native->ls+0xe858,0x10004); break;
        case 7: geometry_job_put32(native->ls+0xe97c,0x10204); break;
        case 8: geometry_job_put32(native->ls+0xe858,0x100); break;
        case 9: geometry_job_put32(native->ls+0xe97c,0x10000); break;
        case 10: geometry_job_put32(native->ls+0xe97c,SPU_LS_SIZE+16); break;
        case 11: geometry_job_put32(native->ls+0xe858,0xe800); geometry_job_put32(native->ls+0xe97c,0xea00); break;
        case 12: geometry_job_put32(native->ls+0xe858,0x3ed00); geometry_job_put32(native->ls+0xe97c,0x3ef00); break;
        case 13: native->gpr[3]._u32[0]=0x3ef00; geometry_job_put32(native->ls+0x3ef58,0x10000); geometry_job_put32(native->ls+0x3f07c,0x10200); break;
        case 14: native->ls=reference->ls_store; break;
        }
        auto* before=clone_function_state(native.get()); const auto* local=native->ls;
        GeometryBlendLists plan; g_spu_trampoline_fn=nullptr;
        assert(!read_geometry_blend_lists(*native,plan));
        assert(compare_function_state(*native,*before).exact() && native->ls==local && !g_spu_trampoline_fn);
        release_function_state(before); native->ls=native->ls_store;
    }
    printf("Native blend lists: 2672 strict root/call-ABI/I/O +15 unchanged/no-I/O rejects PASS; GETL%u restarts%u filtered%u\n", list_gets, restarts, filtered);
}
