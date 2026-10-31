/* Whole-driver shadow I/O and incremental native flow. Real I/O executes once.
 * The native outer driver calls the existing SPU-ABI library wrappers. */
#include "native_geometry_job.h"
#include "native_geometry_job_io.h"
#include "spu_dma.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>

extern "C" {
void spu_wrch(spu_context*, uint32_t, u128);
u128 spu_rdch(spu_context*, uint32_t);
uint32_t spu_rchcnt(spu_context*, uint32_t);
void spu_indirect_branch_mt(spu_context*);
buzz_geometry_job_fn spu_lookup(uint32_t, int);
extern int buzz_geometry_debug;
}

#ifndef BUZZ_GEOMETRY_JOB_AUDIT_AVAILABLE
#define BUZZ_GEOMETRY_JOB_AUDIT_AVAILABLE 0
#endif
#ifndef BUZZ_GEOMETRY_JOB_NATIVE_SETUP_AVAILABLE
#define BUZZ_GEOMETRY_JOB_NATIVE_SETUP_AVAILABLE 0
#endif

namespace {
using namespace buzz;
std::atomic<unsigned long long> attempted{0}, checked{0}, skipped{0}, failures{0}, drawn{0}, culled{0};
std::atomic<unsigned long long> native_attempted{0}, native_executed{0}, native_compared{0}, native_fallback{0};
unsigned long long maximum = 512;

struct Recording {
    std::atomic<bool> busy{false};
    std::atomic<spu_context*> context{nullptr};
    std::atomic<const char*> unsupported{nullptr};
    std::atomic<const void*> owner{nullptr};
    GeometryJobIoOperation* current = nullptr;
    GeometryJobControls entry;
    std::vector<GeometryJobIoOperation> operations;
    size_t transfers = 0;
};
Recording recordings[8]; // Persistent storage; foreign observers never touch vectors.
thread_local Recording* recording = nullptr;
thread_local unsigned owner_token = 0;
thread_local spu_context* shadow = nullptr;
thread_local GeometryJobIoReplay* replay = nullptr;

void unsupported(Recording& r, const char* reason)
{
    const char* empty = nullptr;
    r.unsupported.compare_exchange_strong(empty, reason);
}

[[noreturn]] void mismatch(const char* reason, const spu_context* ctx)
{
    ++failures;
    std::fprintf(stderr, "[geometry-job-audit] DIFF pc=%05X: %s\n", ctx->pc, reason);
    std::fflush(stderr); // abort does not flush the normal buffered live log.
    std::abort();
}

bool is_shadow(const spu_context* ctx) { return ctx == shadow; }
Recording* recording_for(const spu_context* ctx)
{
    return recording && recording->context.load(std::memory_order_acquire) == ctx ? recording : nullptr;
}

template<class Current>
bool external_change(const GeometryJobControls& previous, const Current& ctx)
{
    return previous.event_status != ctx.event_status || previous.resv_valid != ctx.resv_valid ||
           previous.resv_ea != ctx.resv_ea || memcmp(previous.resv_line, ctx.resv_line, sizeof(ctx.resv_line)) ||
           !previous.ch_out_mbox.matches(ctx.ch_out_mbox) || !previous.ch_in_mbox.matches(ctx.ch_in_mbox) ||
           !previous.ch_out_intr_mbox.matches(ctx.ch_out_intr_mbox) ||
           !previous.ch_sig_notify[0].matches(ctx.ch_sig_notify[0]) ||
           !previous.ch_sig_notify[1].matches(ctx.ch_sig_notify[1]);
}

GeometryJobIoOperation* begin(spu_context* ctx, GeometryJobIoKind kind, uint32_t channel,
                              uint32_t argument = 0)
{
    auto* r = recording_for(ctx);
    if (!r || r->unsupported.load()) return nullptr;
    if (r->current || r->operations.size() >= 65536) {
        unsupported(*r, "nested/oversized channel transcript");
        return nullptr;
    }
    const auto before = GeometryJobControls::capture(*ctx);
    const auto& previous = r->operations.empty() ? r->entry : r->operations.back().after;
    // Check the exact snapshot that is recorded, including before the first
    // channel. A second read here would race peer reservation invalidation
    // between admission and capture, then incorrectly fail an exact shadow.
    if (external_change(previous, before)) {
        unsupported(*r, "concurrent external control/channel input");
        return nullptr;
    }
    r->operations.emplace_back();
    auto& op = r->operations.back();
    op.kind = kind;
    op.channel = channel;
    op.argument = argument;
    op.before = before;
    if (kind == GeometryJobIoKind::write && channel == MFC_Cmd && mfc_is_list(argument)) {
        const uint32_t lsa = ctx->mfc_eal & SPU_LS_MASK;
        const uint32_t size = (ctx->mfc_size / 8) * 8;
        if (size > SPU_LS_SIZE - lsa) {
            unsupported(*r, "wrapping list descriptor range");
            return nullptr;
        }
        op.inputs.push_back({lsa, {ctx->ls + lsa, ctx->ls + lsa + size}});
    }
    r->current = &op;
    return &op;
}

void end(spu_context* ctx, GeometryJobIoOperation* op, u128 result = {})
{
    if (!op) return;
    op->result = result;
    op->after = GeometryJobControls::capture(*ctx);
    recording_for(ctx)->current = nullptr;
}

u128 consume(spu_context* ctx, GeometryJobIoKind kind, uint32_t channel, uint32_t argument = 0)
{
    u128 result{};
    if (!replay || !replay->consume(*ctx, kind, channel, argument, result)) {
        std::fprintf(stderr, "[geometry-job-audit] channel operation=%zu channel=%u argument=%08X\n",
                     replay ? replay->consumed() : 0, channel, argument);
        mismatch(replay && replay->error() ? replay->error() : "missing shadow transcript", ctx);
    }
    return result;
}

void run_group(spu_context* ctx, buzz_geometry_job_fn reference, uint32_t return_pc)
{
    // The driver enters at depth zero. Its ordinary SPU_RET queues the saved
    // link; drain_call recognizes that link without executing the parent.
    // An artificial extra frame changes live scheduler behavior in the menu.
    reference(ctx);
    spu_drain_call(ctx, return_pc);
}

bool eligible(const spu_context* ctx)
{
    return !recording && !shadow && !buzz_geometry_debug && ctx->image_id == 41 &&
           ctx->pc == 0x1c18 && ctx->ls == ctx->ls_store && ctx->host_depth == 0 &&
           !ctx->policy_mode && !ctx->resident_ovl && !ctx->irq_saved && !ctx->irq_frame &&
           !ctx->event_mask && !ctx->list_stall_mask && !g_spu_trampoline_fn;
}

struct Init {
    Init()
    {
        const char* option = std::getenv("BUZZ_GEOMETRY_JOB_AUDIT");
        if (option && ((option[0] != '0' && option[0] != '1') || option[1])) std::abort();
        if (buzz_geometry_job_mode()) {
            if (!BUZZ_GEOMETRY_JOB_NATIVE_SETUP_AVAILABLE || buzz_geometry_job_audit_enabled()) {
                std::fprintf(stderr, "[geometry-job] native setup adapter unavailable, or AUDIT conflicts with JOB mode\n");
                std::abort();
            }
            std::fprintf(stderr, "[geometry-job] mode=%d: native outer driver + SPU-ABI library, experimental/default off\n",
                         buzz_geometry_job_mode());
        }
        if (!buzz_geometry_job_audit_enabled()) return;
        if (!BUZZ_GEOMETRY_JOB_AUDIT_AVAILABLE) {
            std::fprintf(stderr, "[geometry-job-audit] unavailable: build the phase-3 adapter\n");
            std::abort();
        }
        if (const char* value = std::getenv("BUZZ_GEOMETRY_JOB_AUDIT_MAX")) {
            char* stop = nullptr;
            maximum = std::strtoull(value, &stop, 10);
            if (!*value || *stop || *value == '-') std::abort();
        }
        std::fprintf(stderr, "[geometry-job-audit] reference/shadow reference, max=%llu (0 unlimited)\n", maximum);
    }
} init;
} // namespace

extern "C" int buzz_geometry_job_mode()
{
    static const int mode = [] {
        const char* value = std::getenv("BUZZ_GEOMETRY_JOB");
        if (!value) return 0;
        if (value[0] < '0' || value[0] > '2' || value[1]) std::abort();
        return value[0] - '0';
    }();
    return mode;
}

extern "C" int buzz_geometry_job_audit_enabled()
{
    const char* value = std::getenv("BUZZ_GEOMETRY_JOB_AUDIT");
    return value && value[0] == '1' && !value[1]; // Safe even during another TU's constructor.
}

extern "C" void buzz_geometry_job_wrch(spu_context* ctx, uint32_t channel, u128 value)
{
    if (is_shadow(ctx)) { consume(ctx, GeometryJobIoKind::write, channel, value._u32[0]); return; }
    auto* op = begin(ctx, GeometryJobIoKind::write, channel, value._u32[0]);
    spu_wrch(ctx, channel, value);
    end(ctx, op);
}

extern "C" u128 buzz_geometry_job_rdch(spu_context* ctx, uint32_t channel)
{
    if (is_shadow(ctx)) return consume(ctx, GeometryJobIoKind::read, channel);
    auto* op = begin(ctx, GeometryJobIoKind::read, channel);
    auto result = spu_rdch(ctx, channel);
    end(ctx, op, result);
    return result;
}

extern "C" uint32_t buzz_geometry_job_rchcnt(spu_context* ctx, uint32_t channel)
{
    if (is_shadow(ctx)) return consume(ctx, GeometryJobIoKind::count, channel)._u32[0];
    auto* op = begin(ctx, GeometryJobIoKind::count, channel);
    u128 result{};
    result._u32[0] = spu_rchcnt(ctx, channel);
    end(ctx, op, result);
    return result._u32[0];
}

extern "C" void buzz_geometry_job_observe(spu_context* ctx, uint32_t kind, uint32_t lsa,
                                           uint32_t ea, uint32_t size, const void* bytes)
{
    for (auto& r : recordings) {
        if (r.context.load(std::memory_order_acquire) != ctx) continue;
        if (r.owner.load() != &owner_token) { unsupported(r, "asynchronous DMA observer"); return; }
        if (!r.current) { unsupported(r, "DMA outside recorded channel call"); return; }
        if (kind < 1 || kind > 6 || lsa > SPU_LS_SIZE || size > SPU_LS_SIZE - lsa) {
            unsupported(r, "unsupported DMA kind/range"); return;
        }
        const auto* data = static_cast<const uint8_t*>(bytes);
        r.current->transfers.push_back({kind, lsa, ea, {data, data + size}});
        ++r.transfers;
        return;
    }
}

extern "C" void buzz_geometry_job_indirect(spu_context* ctx)
{
    const uint32_t pc = ctx->pc & SPU_LS_MASK;
    if (is_shadow(ctx)) {
        if (pc == ctx->drain_ret_pc) { g_spu_trampoline_fn = nullptr; return; }
        auto fn = ctx->image_id == 41 && !ctx->resident_ovl ? spu_lookup(pc, 41) : nullptr;
        if (!fn) mismatch("shadow computed branch would leave the audited image", ctx);
        ctx->pc = pc;
        [[clang::musttail]] return fn(ctx);
    }
    if (auto* r = recording_for(ctx))
        if (pc != ctx->drain_ret_pc && !spu_lookup(pc, 41))
            unsupported(*r, "reference computed branch outside compiled image");
    [[clang::musttail]] return spu_indirect_branch_mt(ctx);
}

extern "C" buzz_geometry_job_fn buzz_geometry_job_indirect_target(spu_context* ctx)
{
    if (is_shadow(ctx)) return buzz_geometry_job_indirect;
    if (auto* r = recording_for(ctx))
        if ((ctx->pc & SPU_LS_MASK) != ctx->drain_ret_pc && !spu_lookup(ctx->pc & SPU_LS_MASK, 41))
            unsupported(*r, "reference computed branch outside compiled image");
    return spu_indirect_branch;
}

extern "C" void buzz_geometry_job_step(spu_context* ctx, uint32_t pc)
{
    pc &= SPU_LS_MASK;
    if (is_shadow(ctx)) {
        if (ctx->image_id != 41 || ctx->resident_ovl ||
            (pc != ctx->drain_ret_pc && !spu_lookup(pc, 41)))
            mismatch("shadow step would enter unwrapped/interpreted code", ctx);
    } else if (auto* r = recording_for(ctx)) {
        if (ctx->image_id != 41 || ctx->resident_ovl ||
            (pc != ctx->drain_ret_pc && !spu_lookup(pc, 41)))
            unsupported(*r, "reference step outside compiled image");
    }
}

static bool audit_pair(spu_context* ctx, buzz_geometry_job_fn reference,
                        buzz_geometry_job_fn trial, bool native_trial)
{
    const auto attempt = attempted.fetch_add(1);
    if ((!native_trial && maximum && attempt >= maximum) || !eligible(ctx)) {
        ++skipped; reference(ctx); return false;
    }
    Recording* r = nullptr;
    for (auto& slot : recordings) {
        bool idle = false;
        if (slot.busy.compare_exchange_strong(idle, true)) { r = &slot; break; }
    }
    if (!r) { ++skipped; reference(ctx); return false; }
    auto* copy = buzz::clone_function_state(ctx);
    copy->mfc_private = nullptr;
    copy->ch_out_mbox.lock = copy->ch_in_mbox.lock = copy->ch_out_intr_mbox.lock = 0;
    copy->ch_sig_notify[0].lock = copy->ch_sig_notify[1].lock = 0;
    const uint32_t return_pc = ctx->gpr[0]._u32[0];
    r->owner.store(&owner_token);
    r->unsupported.store(nullptr);
    r->current = nullptr;
    r->entry = GeometryJobControls::capture(*copy);
    r->transfers = 0;
    r->operations.clear();
    r->context.store(ctx, std::memory_order_release);
    recording = r;
    run_group(ctx, reference, return_pc);
    auto continuation = g_spu_trampoline_fn;
    r->context.store(nullptr, std::memory_order_release);
    recording = nullptr;
    // Freeze the reference at its exit boundary. Peers can invalidate its
    // reservation while the shadow runs; comparing against the still-live
    // context later would compare two different times. Never restore over
    // those later external changes on the retained context.
    auto* expected = buzz::clone_function_state(ctx);
    if (!r->operations.empty() && external_change(r->operations.back().after, *expected))
        unsupported(*r, "concurrent external control/channel input at driver exit");
    bool matched = false;
    if (!r->unsupported.load() && !r->operations.empty()) {
        GeometryJobIoReplay io(r->operations);
        shadow = copy;
        replay = &io;
        g_spu_trampoline_fn = nullptr;
        run_group(copy, trial, return_pc);
        shadow = nullptr;
        replay = nullptr;
        if (!io.finish()) mismatch(io.error(), ctx);
        const auto difference = buzz::compare_function_state(*expected, *copy);
        if (!difference.exact() || continuation != g_spu_trampoline_fn) {
            std::fprintf(stderr, "[geometry-job-audit] r%d byte%d LS=%05X bytes=%u controls=%s\n",
                         difference.first_register, difference.first_register_byte, difference.first_ls,
                         difference.ls_bytes, difference.first_state ? difference.first_state : "equal");
            mismatch("full driver exit state/continuation", ctx);
        }
        (r->transfers > 1 ? drawn : culled).fetch_add(1);
        const auto n = checked.fetch_add(1) + 1;
        if (n == 256 || n % 5000 == 0)
            std::fprintf(stderr, "[%s] strict full-state + I/O %llu PASS\n",
                         native_trial ? "geometry-job/native-outer" : "geometry-job-audit", n);
        matched = true;
    } else {
        const auto n = skipped.fetch_add(1);
        if (n < 8) std::fprintf(stderr, "[geometry-job-audit] unrecordable: %s\n",
                               r->unsupported.load() ? r->unsupported.load() : "no channel operations");
    }
    buzz::release_function_state(expected);
    buzz::release_function_state(copy);
    r->busy.store(false);
    g_spu_trampoline_fn = continuation ? continuation : spu_indirect_branch;
    return matched;
}

extern "C" void buzz_geometry_job_audit_run(spu_context* ctx, buzz_geometry_job_fn reference)
{
    if (!buzz_geometry_job_audit_enabled()) { reference(ctx); return; }
    audit_pair(ctx, reference, reference, false);
}

extern "C" void buzz_geometry_job_native_fail(spu_context* ctx, const char* reason)
{
    mismatch(reason, ctx);
}

extern "C" void buzz_geometry_job_run(spu_context* ctx, buzz_geometry_job_fn reference,
                                      buzz_geometry_job_fn native, buzz_geometry_job_eligibility_fn supports)
{
    const int mode = buzz_geometry_job_mode();
    if (!mode) { buzz_geometry_job_audit_run(ctx, reference); return; }
    ++native_attempted;
    // Always test the original entry. Cloning an external LS or letting the
    // preamble mutate before this decision cannot turn a fallback into native.
    if (!eligible(ctx) || !supports(ctx)) { ++native_fallback; reference(ctx); return; }
    if (mode == 2) {
        if (audit_pair(ctx, reference, native, true)) { ++native_compared; ++native_executed; }
        else ++native_fallback;
        return;
    }
    // Keep the normal entry's depth and trampoline scheduling in actual
    // execution. Only paired checking needs to synchronously drain a job.
    native(ctx);
    ++native_executed;
}

extern "C" void buzz_geometry_job_audit_report()
{
    if (buzz_geometry_job_mode())
        std::fprintf(stderr, "[geometry-job/native-outer] mode=%d attempted=%llu executed=%llu checked=%llu fallback=%llu failed=%llu\n",
                     buzz_geometry_job_mode(), native_attempted.load(), native_executed.load(),
                     native_compared.load(), native_fallback.load(), failures.load());
    if (buzz_geometry_job_audit_enabled())
        std::fprintf(stderr, "[geometry-job-audit] checked=%llu drawn=%llu culled=%llu skipped=%llu failed=%llu\n",
                     checked.load(), drawn.load(), culled.load(), skipped.load(), failures.load());
}
extern "C" int buzz_geometry_job_audit_failures()
{
    if (buzz_geometry_job_mode())
        return failures.load() || (buzz_geometry_job_mode() == 2 ? !native_compared.load() : !native_executed.load());
    return buzz_geometry_job_audit_enabled() && (!checked.load() || failures.load());
}
