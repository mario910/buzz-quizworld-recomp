/* Whole-driver shadow I/O and incremental native flow. Real I/O executes once.
 * The native outer driver calls the existing SPU-ABI library wrappers. */
#include "native_geometry_job.h"
#include "native_geometry_ledger.h"
#include "native_geometry_job_io.h"
#include "spu_dma.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
// Private cache-line slots keep the d0a688b sharding. ++ reports a writer's
// local total (an overflow-bin total after the inventory fills); quiescent
// load() sums every owner and stays exact across thread creation/exit.
using Counter=GeometryLedgerCounter;
// Environment options are immutable once read. A function-local `static const`
// pays the thread-safe-static guard (a TLS epoch check) on every hot call;
// this constant-initialized cache is a plain relaxed load after the first read.
class OptionCache {
    std::atomic<int> value{-1};
public:
    template<class Read> int get(Read read)
    {
        int v = value.load(std::memory_order_relaxed);
        if (v < 0) { v = read(); value.store(v, std::memory_order_relaxed); }
        return v;
    }
};
std::atomic<unsigned long long> attempted{0}, checked{0}, skipped{0}, failures{0}, drawn{0}, culled{0};
Counter native_attempted, native_executed, native_compared, native_fallback;
Counter publication_native, publication_fallback;
Counter color_checked,unit_vectors_checked,half_stream_checked,float32_stream_checked,fixed_stream_checked,snorm_stream_checked,i16_stream_checked,cmp_stream_checked,u8_integer_stream_checked,blend_scatter_checked,blend_lists_checked;
Counter descriptor_checked,descriptor_dispatches[9]{};
Counter custom_stream_checked;
Counter skin_normalized_counts[2][3]{},skin_normalized_aliases[2][3]{};
Counter index_deltas_counts[2][3]{}, index_deltas_aliases[2]{};
Counter index_unpack_counts[2][2]{};
Counter index_expand_counts[2][3]{}, index_expand_capacity[2]{};
Counter index_relocate_counts[2][3]{};
Counter index_triangles_counts[2][3]{};
Counter index_control_counts[2][5]{};
Counter transform_finish_counts[2][2]{};
Counter vertex_prepare_counts[2][4]{};
Counter transform_control_counts[2][7]{};
Counter alternate_cull_counts[2][3]{};
Counter ring_counts[2][3]{};
Counter float_output_counts[2][4]{};
Counter half_output_counts[2][4]{};
Counter cmp_output_counts[2][4]{};
Counter snorm_output_counts[2][4]{};
Counter unorm_output_counts[2][4]{};
Counter u8_integer_output_counts[2][4]{};
Counter i16_output_counts[2][4]{};
Counter formatter_counts[2][4]{},formatter_types[2][7]{};
Counter formatter_registers_roots[2]{},formatter_registers_children[2]{};
Counter conversion_control_counts[2][7]{};
Counter builtin_output_counts[2][4]{};
Counter builtin_input_counts[2][5]{},input_control_counts[2][7]{};
Counter skin_cofactor_counts[2][2]{},skin_cofactor_aliases[2]{};
Counter rigid_skin_counts[2][2]{},rigid_skin_aliases[2]{};
Counter skin_flow_counts[2][2]{};
Counter skin_dispatch_counts[2][2]{};
Counter weighted_skin_counts[2][2]{},weighted_skin_alias_counts[2]{};
Counter skin_directions_counts[2][3]{},skin_directions_aliases[2][3]{};
Counter run_lists_checked,run_lists_expanded_native,run_lists_empty_native,run_lists_expanded_checked,run_lists_empty_checked;
struct OutputCounters { Counter native, fallback, reused; } output_counters[61];
constexpr const char* output_labels[] = {"output-size", "vertex-publish", "command-buffer", "command-publish", "command-bind", "command-reset", "position-commands", "full-commands", "attributes", "raw-stream", "color-codec", "descriptor-control", "unit-vectors", "half-stream", "float32-stream", "fixed-stream", "snorm-stream", "i16-stream", "cmp-stream", "u8-integer-stream", "blend-scatter", "blend-lists", "run-lists", "custom-stream", "skin-dispatch", "weighted-skin", "skin-directions", "skin-flow", "rigid-skin", "skin-cofactor", "skin-normalized", "index-deltas", "index-unpack", "index-expand", "index-relocate", "index-triangles", "index-control", "transform-finish", "vertex-prepare", "transform-control", "alternate-cull", "ring", "float-output", "half-output", "cmp-output", "snorm-output", "unorm-output", "i16-output", "u8-integer-output", "formatter", "conversion-control", "builtin-float3-output", "builtin20-output", "builtin24-output", "builtin24-cmp-output", "input-control", "builtin20-input", "builtin24-input", "builtin24-cmp-input", "builtin12-input", "builtin15-input"};
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
thread_local const spu_context* admission_owner=nullptr;
thread_local const char* replay_case=nullptr;
struct AdmissionOwnerScope {
    const spu_context* previous;
    explicit AdmissionOwnerScope(const spu_context* c):previous(admission_owner){admission_owner=c;}
    ~AdmissionOwnerScope(){admission_owner=previous;}
};
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
    if(replay_case)std::fprintf(stderr,"[geometry-job-audit] corpus case=%s\n",replay_case);
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
        if(replay)if(const auto* expected=replay->expected_operation())
            std::fprintf(stderr,"[geometry-job-audit] expected channel=%u argument=%08X\n",expected->channel,expected->argument);
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
            std::fprintf(stderr, "[geometry-job] mode=%d: native job driver, experimental/default off\n",
                         buzz_geometry_job_mode());
            std::fprintf(stderr,"[geometry-job] profile=%s publication=%d commands=%d (JOB default off)\n",
                buzz_geometry_job_legacy_enabled()?"legacy-debug":"host-groups",
                buzz_geometry_job_publication_enabled(),buzz_geometry_job_commands_enabled());
            std::fprintf(stderr,"[geometry-job] host pipeline=%d (persistent host SoA;whole-job admission;JOB default off)\n",buzz_geometry_job_host_pipeline_enabled());
            if (buzz_geometry_job_legacy_enabled()) {
                std::fprintf(stderr, "[geometry-job] native index publication=%d (default off)\n",
                             buzz_geometry_job_publish_enabled());
                std::fprintf(stderr, "[geometry-job] native output sizing=%d, vertex publication=%d (default off)\n",
                             buzz_geometry_job_layout_enabled(), buzz_geometry_job_vertex_enabled());
                std::fprintf(stderr, "[geometry-job] native command publication=%d (default off)\n", buzz_geometry_job_command_publish_enabled());
                std::fprintf(stderr, "[geometry-job] native position commands/bindings=%d (default off)\n", buzz_geometry_job_emit_enabled());
                std::fprintf(stderr, "[geometry-job] native full commands=%d (default off)\n", buzz_geometry_job_full_commands_enabled());
                std::fprintf(stderr, "[geometry-job] native attribute control/raw float3=%d (default off; descriptor codecs retained)\n", buzz_geometry_job_attributes_enabled());
                std::fprintf(stderr, "[geometry-job] native blend scatter=%d (0 lift, 1 native, 2 strict root; default off)\n", buzz_geometry_job_blend_scatter_enabled());
                std::fprintf(stderr, "[geometry-job] native blend lists=%d (default off; JOB=2 strict I/O root)\n", buzz_geometry_job_blend_lists_enabled());
                std::fprintf(stderr, "[geometry-job] native run lists=%d (0 lift, 1 native, 2 strict root; default off)\n", buzz_geometry_job_run_lists_enabled());
                std::fprintf(stderr, "[geometry-job] native normalized skin=%d (0 lift, 1 native, 2 strict state/boundary; default off)\n", buzz_geometry_job_skin_normalized_enabled());
                std::fprintf(stderr, "[geometry-job] native index deltas=%d (0 lift, 1 native, 2 strict data/state; default off)\n", buzz_geometry_job_index_deltas_enabled());
                std::fprintf(stderr, "[geometry-job] native index unpack=%d (0 lift, 1 native, 2 strict data/state; default off)\n", buzz_geometry_job_index_unpack_enabled());
                std::fprintf(stderr, "[geometry-job] native index expansion=%d (0 lift, 1 native, 2 strict data/state; default off)\n", buzz_geometry_job_index_expand_enabled());
                std::fprintf(stderr, "[geometry-job] native index relocation=%d (0 lift, 1 native, 2 strict data/state; default off)\n", buzz_geometry_job_index_relocate_enabled());
                std::fprintf(stderr, "[geometry-job] native index triangles=%d (0 lift, 1 native, 2 strict data/state; default off)\n", buzz_geometry_job_index_triangles_enabled());
                std::fprintf(stderr, "[geometry-job] native index control=%d (0 lift, 1 native, 2 strict data/state; default off)\n", buzz_geometry_job_index_control_enabled());
                std::fprintf(stderr,"[geometry-job] native input control=%d (0 lift,1 native,2 strict full state;default off)\n",buzz_geometry_job_input_control_enabled());
                for(unsigned format=1;format<=5;++format)std::fprintf(stderr,"[geometry-job] native builtin input%u=%d (requires input control;0 lift,1 native,2 strict data;default off)\n",format,buzz_geometry_job_builtin_input_enabled(format));
                for(unsigned format=0;format<4;++format)std::fprintf(stderr,"[geometry-job] native builtin output%u=%d (0 lift, 1 native, 2 strict full-state; default off)\n",format,buzz_geometry_job_builtin_output_enabled(format));
                std::fprintf(stderr,"[geometry-job] native conversion control=%d (0 lift, 1 native, 2 strict full-state; default off)\n",buzz_geometry_job_conversion_control_enabled());
                std::fprintf(stderr,"[geometry-job] native formatter=%d (0 lift, 1 native, 2 strict full-state; default off)\n",buzz_geometry_job_formatter_enabled());
                std::fprintf(stderr,"[geometry-job] formatter column spans=%d (0 exhaustive,1 proven spans;default off)\n",buzz_geometry_job_formatter_spans_enabled());
                std::fprintf(stderr,"[geometry-job] formatter registers=%d (0 ABI callbacks,1 shared host bank;default off)\n",buzz_geometry_job_formatter_registers_enabled());
                std::fprintf(stderr,"[geometry-job] host output=%d (8-lane SoA float/half/CMP;whole-job fallback;default off)\n",buzz_geometry_host_output_enabled());
                std::fprintf(stderr,"[geometry-job] native I16 output=%d (0 lift, 1 native, 2 strict full-state; default off)\n",buzz_geometry_job_i16_output_enabled());
                std::fprintf(stderr,"[geometry-job] native U8 integer output=%d (0 lift, 1 native, 2 strict full-state; default off)\n",buzz_geometry_job_u8_integer_output_enabled());
                std::fprintf(stderr,"[geometry-job] native UNORM output=%d (0 lift, 1 native, 2 strict full-state; default off)\n",buzz_geometry_job_unorm_output_enabled());
                std::fprintf(stderr,"[geometry-job] native SNORM output=%d (0 lift, 1 native, 2 strict full-state; default off)\n",buzz_geometry_job_snorm_output_enabled());
                std::fprintf(stderr,"[geometry-job] native CMP output=%d (0 lift, 1 native, 2 strict full-state; default off)\n",buzz_geometry_job_cmp_output_enabled());
                std::fprintf(stderr,"[geometry-job] native half output=%d (0 lift, 1 native, 2 strict full-state; default off)\n",buzz_geometry_job_half_output_enabled());
                std::fprintf(stderr,"[geometry-job] native float output=%d (0 lift, 1 native, 2 strict full-state; default off)\n",buzz_geometry_job_float_output_enabled());
                std::fprintf(stderr,"[geometry-job] native ring=%d (0 lift, 1 native; JOB=2 strict state/I/O; default off)\n",buzz_geometry_job_ring_enabled());
                std::fprintf(stderr, "[geometry-job] native alternate cull=%d (0 lift, 1 native, 2 strict state/handoff; default off)\n", buzz_geometry_job_alternate_cull_enabled());
                std::fprintf(stderr, "[geometry-job] native transform control=%d (0 lift, 1 native, 2 strict state/return; default off)\n", buzz_geometry_job_transform_control_enabled());
                std::fprintf(stderr, "[geometry-job] native vertex prepare=%d (0 lift, 1 native, 2 strict state/handoff; default off)\n", buzz_geometry_job_vertex_prepare_enabled());
                std::fprintf(stderr, "[geometry-job] native transform finish=%d (0 lift, 1 native, 2 strict data/state; default off)\n", buzz_geometry_job_transform_finish_enabled());
                std::fprintf(stderr, "[geometry-job] native skin cofactor=%d (0 lift, 1 native, 2 strict state/boundary; default off)\n", buzz_geometry_job_skin_cofactor_enabled());
                std::fprintf(stderr, "[geometry-job] native rigid skin=%d (0 lift, 1 native, 2 strict state/boundary; default off)\n", buzz_geometry_job_rigid_skin_enabled());
                std::fprintf(stderr, "[geometry-job] native skin flow=%d (0 lift, 1 native, 2 strict state/return; default off)\n", buzz_geometry_job_skin_flow_enabled());
                std::fprintf(stderr, "[geometry-job] native skin directions=%d (0 lift, 1 native, 2 strict data/state; default off)\n", buzz_geometry_job_skin_directions_enabled());
                std::fprintf(stderr, "[geometry-job] complete weighted skin=%d (0 previous path, 1 native, 2 strict data/state; default off)\n", buzz_geometry_job_weighted_skin_enabled());
                std::fprintf(stderr, "[geometry-job] native skin dispatch=%d (0 lift, 1 native, 2 strict controller boundaries; default off)\n", buzz_geometry_job_skin_dispatch_enabled());
                std::fprintf(stderr, "[geometry-job] native custom stream=%d (0 lift, 1 native, 2 strict 7DF8 handoff; default off)\n", buzz_geometry_job_custom_stream_enabled());
                std::fprintf(stderr, "[geometry-job] native I16 stream=%d (0 lift, 1 native, 2 strict region; default off)\n", buzz_geometry_job_i16_stream_enabled());
                std::fprintf(stderr, "[geometry-job] native CMP stream=%d (0 lift, 1 native, 2 strict region; default off)\n", buzz_geometry_job_cmp_stream_enabled());
                std::fprintf(stderr, "[geometry-job] native U8_INTEGER stream=%d (0 lift, 1 native, 2 strict region; default off)\n", buzz_geometry_job_u8_integer_stream_enabled());
                std::fprintf(stderr, "[geometry-job] native SNORM16 stream=%d (0 lift, 1 native, 2 strict region; default off)\n", buzz_geometry_job_snorm_stream_enabled());
                std::fprintf(stderr, "[geometry-job] native fixed stream=%d (0 lift, 1 native, 2 strict region; default off)\n", buzz_geometry_job_fixed_stream_enabled());
                std::fprintf(stderr, "[geometry-job] native float32 stream=%d (0 lift, 1 native, 2 strict region; default off)\n", buzz_geometry_job_float32_stream_enabled());
                std::fprintf(stderr, "[geometry-job] native half stream=%d (default off; complete storage3 region)\n", buzz_geometry_job_half_stream_enabled());
                std::fprintf(stderr, "[geometry-job] native unit vectors=%d (default off; complete storage9 region)\n", buzz_geometry_job_unit_vectors_enabled());
                std::fprintf(stderr, "[geometry-job] native stream codecs=%d (default off; U8 color only)\n", buzz_geometry_job_codecs_enabled());
                std::fprintf(stderr, "[geometry-job] native descriptor control=%d (default off; typed callbacks tracked separately)\n", buzz_geometry_job_descriptors_enabled());
                std::fprintf(stderr, "[geometry-job] native command buffer=%d (default off)\n", buzz_geometry_job_commands_enabled());
            }
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

static int raw_output_option(const char* name, int maximum_value = 1, int fallback = 0)
{
    const char* value = std::getenv(name);
    if (!value) return fallback;
    if (value[0] < '0' || value[0] > '0' + maximum_value || value[1]) std::abort();
    return value[0] - '0';
}
extern "C" int buzz_geometry_job_legacy_enabled()
{
    static OptionCache enabled;
    return enabled.get([] { return raw_output_option("BUZZ_GEOMETRY_JOB_LEGACY"); });
}
static int host_pipeline_group()
{
    static OptionCache enabled;
    return enabled.get([] { return raw_output_option("BUZZ_GEOMETRY_JOB_HOST_PIPELINE", 1,
        BUZZ_GEOMETRY_JOB_NATIVE_SETUP_AVAILABLE && !buzz_geometry_job_legacy_enabled()); });
}
extern "C" int buzz_geometry_job_publication_enabled()
{
    static OptionCache enabled;
    return enabled.get([] { return buzz_geometry_job_legacy_enabled() ? 0 :
        raw_output_option("BUZZ_GEOMETRY_JOB_PUBLICATION", 1, host_pipeline_group()); });
}
static int commands_group()
{
    static OptionCache enabled;
    return enabled.get([] { return raw_output_option("BUZZ_GEOMETRY_JOB_COMMANDS", 1, host_pipeline_group()); });
}
static int output_option(const char* name, int maximum_value = 1)
{
    // Each public getter caches this decision. Hot calls still pay one relaxed
    // load, with no new parent checks. Old stage switches are diagnostic-only.
    if (!std::strcmp(name, "BUZZ_GEOMETRY_DEBUG_STAGES")) return raw_output_option(name, maximum_value);
    if (!std::strcmp(name, "BUZZ_GEOMETRY_JOB_HOST_PIPELINE")) return host_pipeline_group();
    if (buzz_geometry_job_legacy_enabled()) return raw_output_option(name, maximum_value);
    if (!std::strcmp(name, "BUZZ_GEOMETRY_JOB_PUBLISH") || !std::strcmp(name, "BUZZ_GEOMETRY_JOB_LAYOUT") ||
        !std::strcmp(name, "BUZZ_GEOMETRY_JOB_VERTEX") || !std::strcmp(name, "BUZZ_GEOMETRY_JOB_RING"))
        return buzz_geometry_job_publication_enabled();
    if (!std::strcmp(name, "BUZZ_GEOMETRY_JOB_COMMANDS") || !std::strcmp(name, "BUZZ_GEOMETRY_JOB_COMMAND_PUBLISH") ||
        !std::strcmp(name, "BUZZ_GEOMETRY_JOB_EMIT") || !std::strcmp(name, "BUZZ_GEOMETRY_JOB_FULL_COMMANDS"))
        return commands_group();
    return 0;
}
extern "C" int buzz_geometry_job_publish_enabled()
{
    static OptionCache enabled;
    return enabled.get([] { return output_option("BUZZ_GEOMETRY_JOB_PUBLISH"); });
}

extern "C" void buzz_geometry_job_publish_result(int native)
{
    if (native) {
        const auto total = ++publication_native;
        if (total % 5000 == 0) std::fprintf(stderr, "[geometry-job/index-publish] native=%llu\n", total);
    } else ++publication_fallback;
}

extern "C" int buzz_geometry_job_debug_stages()
{
    static OptionCache enabled;
    return enabled.get([] { return output_option("BUZZ_GEOMETRY_DEBUG_STAGES"); });
}
extern "C" int buzz_geometry_host_output_enabled()
{
    static OptionCache enabled;
    return enabled.get([] { return output_option("BUZZ_GEOMETRY_HOST_OUTPUT"); });
}
extern "C" int buzz_geometry_job_host_pipeline_enabled()
{
    static OptionCache enabled;
    return enabled.get([] { return output_option("BUZZ_GEOMETRY_JOB_HOST_PIPELINE"); });
}
extern "C" void buzz_geometry_job_host_pipeline_result()
{
    static Counter completed;
    const auto count=++completed;
    if(count<=4 || count%25000==0)
        std::fprintf(stderr,"[geometry-job/host-pipeline] native %llu (owner count;direct host data)\n",count);
}
extern "C" void buzz_geometry_host_output_result()
{
    static Counter completed;
    const auto count = ++completed;
    if (count <= 4 || count % 25000 == 0)
        std::fprintf(stderr, "[geometry-job/host-output] native %llu (owner count)\n", count);
}
extern "C" int buzz_geometry_job_layout_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_LAYOUT");});
}
extern "C" int buzz_geometry_job_vertex_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_VERTEX");});
}
extern "C" int buzz_geometry_job_commands_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_COMMANDS");});
}
extern "C" int buzz_geometry_job_command_publish_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_COMMAND_PUBLISH");});
}
extern "C" int buzz_geometry_job_emit_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_EMIT");});
}
extern "C" int buzz_geometry_job_full_commands_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_FULL_COMMANDS");});
}
extern "C" int buzz_geometry_job_attributes_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_ATTRIBUTES");});
}
extern "C" int buzz_geometry_job_codecs_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_CODECS");});
}
extern "C" void buzz_geometry_job_output_result(unsigned stage, int native, int reused_layout)
{
    if (stage >= 61) std::abort();
    auto& counters = output_counters[stage];
    if (native) {
        if (reused_layout) ++counters.reused;
        const auto total = ++counters.native;
        if (((stage >= BUZZ_GEOMETRY_JOB_FLOAT32_STREAM) && total == 1) || total == 256 || total % 5000 == 0) std::fprintf(stderr, "[geometry-job/%s] native=%llu reused-layout=%llu\n",
            output_labels[stage], total, counters.reused.load());
    } else {
        const auto total=++counters.fallback;
        // A scene run may be force-killed without atexit summaries. Persist
        // its first unsupported owned call immediately, even without success.
        if(total==1){
            std::fprintf(stderr,"[geometry-job/%s] first-fallback=%llu native=%llu reused-layout=%llu\n",output_labels[stage],total,counters.native.load(),counters.reused.load());
            std::fflush(stderr);
        }
    }
}

extern "C" void buzz_geometry_job_color_checked_result()
{
    const auto total = ++color_checked;
    if (total == 256 || total % 5000 == 0)
        std::fprintf(stderr,"[geometry-job/color-codec] strict full-state %llu PASS\n",total);
}

extern "C" int buzz_geometry_job_unit_vectors_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_UNIT_VECTORS");});
}
extern "C" void buzz_geometry_job_unit_vectors_checked_result()
{
    const auto total=++unit_vectors_checked;
    if(total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/unit-vectors] strict full-state %llu PASS\n",total);
}

extern "C" int buzz_geometry_job_half_stream_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_HALF_STREAM");});
}
extern "C" void buzz_geometry_job_half_stream_checked_result()
{
    const auto total=++half_stream_checked;
    if(total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/half-stream] strict full-state %llu PASS\n",total);
}

extern "C" int buzz_geometry_job_float32_stream_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_FLOAT32_STREAM", 2);});
}
extern "C" void buzz_geometry_job_float32_stream_checked_result()
{
    const auto total=++float32_stream_checked;
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/float32-stream] strict full-state %llu PASS\n",total);
}

extern "C" int buzz_geometry_job_fixed_stream_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_FIXED_STREAM", 2);});
}
extern "C" void buzz_geometry_job_fixed_stream_checked_result()
{
    const auto total=++fixed_stream_checked;
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/fixed-stream] strict full-state %llu PASS\n",total);
}

extern "C" int buzz_geometry_job_blend_scatter_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_BLEND_SCATTER", 2);});
}
extern "C" int buzz_geometry_job_blend_lists_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_BLEND_LISTS");});
}
extern "C" int buzz_geometry_job_run_lists_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_RUN_LISTS",2);});
}
extern "C" int buzz_geometry_job_input_control_enabled(){static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_INPUT_CONTROL",2);});}
extern "C" int buzz_geometry_job_builtin_input_enabled(unsigned format){
    switch(format){
    case 1:{static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_BUILTIN20_INPUT",2);});}
    case 2:{static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_BUILTIN24_INPUT",2);});}
    case 3:{static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_BUILTIN24_CMP_INPUT",2);});}
    case 4:{static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_BUILTIN12_INPUT",2);});}
    case 5:{static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_BUILTIN15_INPUT",2);});}
    default:std::abort();}
}
extern "C" void buzz_geometry_job_builtin_input_result(unsigned format,int checked){
    if(format<1 || format>5)std::abort();const auto total=++builtin_input_counts[!!checked][format-1];
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/builtin-input%u] %s=%llu PASS\n",format,checked?"strict-full-state-handoff":"native",total);
}
extern "C" void buzz_geometry_job_input_control_result(unsigned path,int checked){
    if(path>=7)std::abort();const auto total=++input_control_counts[!!checked][path];const char* names[]={"empty","raw0","builtin1","builtin2","builtin3","builtin4","builtin5"};
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/input-control/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-return":"native",total);
}
extern "C" int buzz_geometry_job_builtin_output_enabled(unsigned format)
{
    switch(format){
    case 0:{static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_BUILTIN_FLOAT3_OUTPUT",2);});}
    case 1:{static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_BUILTIN20_OUTPUT",2);});}
    case 2:{static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_BUILTIN24_OUTPUT",2);});}
    case 3:{static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_BUILTIN24_CMP_OUTPUT",2);});}
    default:std::abort();
    }
}
extern "C" void buzz_geometry_job_builtin_output_result(unsigned format,int checked)
{
    if(format>=4)std::abort();const auto total=++builtin_output_counts[!!checked][format];
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/builtin-output%u] %s=%llu PASS\n",format,checked?"strict-full-state-handoff":"native",total);
}
extern "C" int buzz_geometry_job_conversion_control_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_CONVERSION_CONTROL",2);});
}
extern "C" void buzz_geometry_job_conversion_control_result(unsigned path,int checked)
{
    if(path>=7)std::abort();const auto total=++conversion_control_counts[!!checked][path];
    const char* names[]={"existing","empty","descriptor","builtin0","builtin1","builtin2","builtin3"};
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/conversion-control/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-return":"native",total);
}
extern "C" int buzz_geometry_job_formatter_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_FORMATTER",2);});
}
extern "C" int buzz_geometry_job_formatter_spans_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_FORMATTER_SPANS",1);});
}
extern "C" void buzz_geometry_job_formatter_result(unsigned fields,unsigned known,unsigned skipped,unsigned mask,int checked)
{
    if(fields!=known+skipped || mask>127)std::abort();
    const unsigned path=!fields?0:!known?2:skipped?3:1;
    const auto total=++formatter_counts[!!checked][path];
    for(unsigned i=0;i<7;++i)if(mask&(1u<<i))++formatter_types[!!checked][i];
    const char* names[]={"empty","known","skipped","mixed"};
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/formatter/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-return":"native",total);
}
extern "C" int buzz_geometry_job_formatter_registers_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_FORMATTER_REGISTERS",1);});
}
extern "C" void buzz_geometry_job_formatter_registers_result(unsigned children,unsigned strict_children,int checked)
{
    if(strict_children>children)std::abort();
    const auto total=++formatter_registers_roots[0];
    if(checked)++formatter_registers_roots[1];
    formatter_registers_children[0]+=children;formatter_registers_children[1]+=strict_children;
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/formatter-registers] native=%llu strict-parent=%llu children=%llu strict-children=%llu PASS\n",total,formatter_registers_roots[1].load(),formatter_registers_children[0].load(),formatter_registers_children[1].load());
}
extern "C" int buzz_geometry_job_i16_output_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_I16_OUTPUT",2);});
}
extern "C" void buzz_geometry_job_i16_output_result(unsigned columns,unsigned cross_columns,int checked)
{
    if(cross_columns>columns)std::abort();
    const unsigned path=!columns?0:!cross_columns?1:cross_columns==columns?2:3;const auto total=++i16_output_counts[!!checked][path];
    const char* names[]={"skip","single-quad","double-quad","mixed"};
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/i16-output/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-handoff":"native",total);
}
extern "C" int buzz_geometry_job_u8_integer_output_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_U8_INTEGER_OUTPUT",2);});
}
extern "C" void buzz_geometry_job_u8_integer_output_result(unsigned columns,unsigned cross_columns,int checked)
{
    if(cross_columns>columns)std::abort();
    const unsigned path=!columns?0:!cross_columns?1:cross_columns==columns?2:3;const auto total=++u8_integer_output_counts[!!checked][path];
    const char* names[]={"skip","single-quad","double-quad","mixed"};
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/u8-integer-output/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-handoff":"native",total);
}
extern "C" int buzz_geometry_job_unorm_output_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_UNORM_OUTPUT",2);});
}
extern "C" void buzz_geometry_job_unorm_output_result(unsigned columns,unsigned cross_columns,int checked)
{
    if(cross_columns>columns)std::abort();
    const unsigned path=!columns?0:!cross_columns?1:cross_columns==columns?2:3;const auto total=++unorm_output_counts[!!checked][path];
    const char* names[]={"skip","single-quad","double-quad","mixed"};
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/unorm-output/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-handoff":"native",total);
}
extern "C" int buzz_geometry_job_snorm_output_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_SNORM_OUTPUT",2);});
}
extern "C" void buzz_geometry_job_snorm_output_result(unsigned columns,unsigned cross_columns,int checked)
{
    if(cross_columns>columns)std::abort();
    const unsigned path=!columns?0:!cross_columns?1:cross_columns==columns?2:3;const auto total=++snorm_output_counts[!!checked][path];
    const char* names[]={"skip","single-quad","double-quad","mixed"};
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/snorm-output/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-handoff":"native",total);
}
extern "C" int buzz_geometry_job_cmp_output_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_CMP_OUTPUT",2);});
}
extern "C" void buzz_geometry_job_cmp_output_result(unsigned columns,unsigned cross_columns,int checked)
{
    if(cross_columns>columns)std::abort();
    const unsigned path=!columns?0:!cross_columns?1:cross_columns==columns?2:3;const auto total=++cmp_output_counts[!!checked][path];
    const char* names[]={"skip","single-quad","double-quad","mixed"};
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/cmp-output/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-handoff":"native",total);
}
extern "C" int buzz_geometry_job_half_output_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_HALF_OUTPUT",2);});
}
extern "C" void buzz_geometry_job_half_output_result(unsigned columns,unsigned cross_columns,int checked)
{
    if(cross_columns>columns)std::abort();
    const unsigned path=!columns?0:!cross_columns?1:cross_columns==columns?2:3;const auto total=++half_output_counts[!!checked][path];
    const char* names[]={"skip","single-quad","double-quad","mixed"};
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/half-output/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-handoff":"native",total);
}
extern "C" int buzz_geometry_job_float_output_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_FLOAT_OUTPUT",2);});
}
extern "C" void buzz_geometry_job_float_output_result(unsigned columns,unsigned cross_columns,int checked)
{
    if(cross_columns>columns)std::abort();
    const unsigned path=!columns?0:!cross_columns?1:cross_columns==columns?2:3;const auto total=++float_output_counts[!!checked][path];
    const char* names[]={"skip","single-quad","double-quad","mixed"};
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/float-output/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-handoff":"native",total);
}
extern "C" int buzz_geometry_job_ring_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_RING");});
}
extern "C" void buzz_geometry_job_ring_result(unsigned frontier,int checked)
{
    const unsigned path=frontier==0?0:frontier==0x3dd0?1:frontier==0x4078?2:3;
    if(path==3)std::abort();const auto total=++ring_counts[!!checked][path];
    const char* names[]={"return","retained-initial-report","retained-shared-report"};
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/ring/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-io":"native-prefix",total);
}
extern "C" int buzz_geometry_job_alternate_cull_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_ALTERNATE_CULL",2);});
}
extern "C" void buzz_geometry_job_alternate_cull_result(unsigned iterations,int checked)
{
    const unsigned path=iterations==1?0:iterations<=3?1:2;const auto total=++alternate_cull_counts[!!checked][path];
    const char* names[]={"warmup","short","bulk"};
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/alternate-cull/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-handoff":"native",total);
}
extern "C" int buzz_geometry_job_transform_control_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_TRANSFORM_CONTROL",2);});
}
extern "C" void buzz_geometry_job_transform_control_result(unsigned path,int checked)
{
    if(path>=7)std::abort();const auto total=++transform_control_counts[!!checked][path];
    const char* names[]={"bypass-skip","bypass-publish","idle","one","two","three","alternate"};
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/transform-control/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-return":"native",total);
}
extern "C" int buzz_geometry_job_vertex_prepare_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_VERTEX_PREPARE",2);});
}
extern "C" void buzz_geometry_job_vertex_prepare_result(unsigned path,int checked)
{
    if(path>=4)std::abort();
    const auto total=++vertex_prepare_counts[!!checked][path];
    const char* names[]={"base","middle","five","other"};
    if(total==1 || total==256 || total%5000==0)
        std::fprintf(stderr,"[geometry-job/vertex-prepare/%s] %s=%llu PASS\n",names[path],checked?"strict-full-state-handoff":"native",total);
}
extern "C" int buzz_geometry_job_transform_finish_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_TRANSFORM_FINISH",2);});
}
extern "C" void buzz_geometry_job_transform_finish_result(int publish,int checked)
{
    const auto total=++transform_finish_counts[!!checked][!!publish];
    if(total==1 || total==256 || total%5000==0)
        std::fprintf(stderr,"[geometry-job/transform-finish/%s] %s=%llu PASS\n",publish?"publish":"count-only",checked?"strict-full-state-return":"native",total);
}
extern "C" int buzz_geometry_job_index_control_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_INDEX_CONTROL",2);});
}
extern "C" void buzz_geometry_job_index_control_result(unsigned path,int checked)
{
    if(path>=5)std::abort();const char* labels[]={"empty","raw","zero","low","high"};
    const auto total=++index_control_counts[!!checked][path];
    if(total==1 || total==256 || total%5000==0)
        std::fprintf(stderr,"[geometry-job/index-control/%s] %s=%llu PASS\n",labels[path],checked?"strict-full-state-return":"native",total);
}
extern "C" int buzz_geometry_job_index_triangles_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_INDEX_TRIANGLES", 2);});
}
extern "C" void buzz_geometry_job_index_triangles_result(unsigned iterations, int checked)
{
    const unsigned shape = iterations == 1 ? 0 : iterations <= 3 ? 1 : 2;
    const auto total = ++index_triangles_counts[!!checked][shape];
    if (total == 1 || total == 256 || total % 5000 == 0)
        std::fprintf(stderr,"[geometry-job/index-triangles/%s] %s=%llu PASS\n",shape == 0 ? "warmup" : shape == 1 ? "short" : "bulk",
            checked ? "strict-full-state-boundary" : "native",total);
}
extern "C" int buzz_geometry_job_index_relocate_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_INDEX_RELOCATE", 2);});
}
extern "C" void buzz_geometry_job_index_relocate_result(unsigned iterations, int checked)
{
    const unsigned shape = iterations == 1 ? 0 : iterations == 2 ? 1 : 2;
    const auto total = ++index_relocate_counts[!!checked][shape];
    if (total == 1 || total == 256 || total % 5000 == 0)
        std::fprintf(stderr,"[geometry-job/index-relocate/%s] %s=%llu PASS\n",shape == 0 ? "warmup" : shape == 1 ? "short" : "bulk",
            checked ? "strict-full-state-boundary" : "native",total);
}
extern "C" int buzz_geometry_job_index_expand_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_INDEX_EXPAND", 2);});
}
extern "C" void buzz_geometry_job_index_expand_result(unsigned iterations, int checked, int capacity_error)
{
    const unsigned shape = iterations == 1 ? 0 : iterations <= 3 ? 1 : 2;
    const auto total = ++index_expand_counts[!!checked][shape]; if (capacity_error) ++index_expand_capacity[!!checked];
    if (total == 1 || total == 256 || total % 5000 == 0)
        std::fprintf(stderr,"[geometry-job/index-expand/%s] %s=%llu capacity-exits=%llu PASS\n",shape == 0 ? "warmup" : shape == 1 ? "short" : "bulk",
            checked ? "strict-full-state-boundary" : "native",total,index_expand_capacity[!!checked].load());
}
extern "C" int buzz_geometry_job_index_unpack_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_INDEX_UNPACK", 2);});
}
extern "C" void buzz_geometry_job_index_unpack_result(unsigned width, int checked)
{
    const unsigned high = width > 8; const auto total = ++index_unpack_counts[!!checked][high];
    if (total == 1 || total == 256 || total % 5000 == 0)
        std::fprintf(stderr,"[geometry-job/index-unpack/%s] %s=%llu PASS\n",high ? "high" : "low",
            checked ? "strict-full-state-boundary" : "native",total);
}
extern "C" int buzz_geometry_job_index_deltas_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_INDEX_DELTAS", 2);});
}
extern "C" void buzz_geometry_job_index_deltas_result(unsigned groups, int checked, int aliases)
{
    const unsigned shape = groups == 0 ? 0 : groups == 1 ? 1 : 2;
    const auto total = ++index_deltas_counts[!!checked][shape]; if (aliases) ++index_deltas_aliases[!!checked];
    if (total == 1 || total == 256 || total % 5000 == 0)
        std::fprintf(stderr, "[geometry-job/index-deltas/%s] %s=%llu aliases=%llu PASS\n", shape == 0 ? "empty" : shape == 1 ? "one-group" : "bulk",
            checked ? "strict-full-state-boundary" : "native", total, index_deltas_aliases[!!checked].load());
}
extern "C" int buzz_geometry_job_skin_normalized_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_SKIN_NORMALIZED",2);});
}
extern "C" void buzz_geometry_job_skin_normalized_result(unsigned streams,int checked,int aliases)
{
    if(streams<1 || streams>3)std::abort();
    const auto total=++skin_normalized_counts[!!checked][streams-1];if(aliases)++skin_normalized_aliases[!!checked][streams-1];
    if(total==1 || total==256 || total%5000==0)
        std::fprintf(stderr,"[geometry-job/skin-normalized/%u] %s=%llu aliases=%llu PASS\n",streams,checked?"strict-full-state-boundary":"native",total,skin_normalized_aliases[!!checked][streams-1].load());
}
extern "C" int buzz_geometry_job_skin_cofactor_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_SKIN_COFACTOR",2);});
}
extern "C" void buzz_geometry_job_skin_cofactor_result(unsigned count,int checked,int aliases)
{
    const auto total=++skin_cofactor_counts[!!checked][count==4];if(aliases)++skin_cofactor_aliases[!!checked];
    if(total==1 || total==256 || total%5000==0)
        std::fprintf(stderr,"[geometry-job/skin-cofactor/%s] %s=%llu aliases=%llu PASS\n",count==4?"one-group":"bulk",checked?"strict-full-state-boundary":"native",total,skin_cofactor_aliases[!!checked].load());
}
extern "C" int buzz_geometry_job_rigid_skin_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_RIGID_SKIN",2);});
}
extern "C" void buzz_geometry_job_rigid_skin_result(unsigned count,int checked,int aliases)
{
    const auto total=++rigid_skin_counts[!!checked][count==2];if(aliases)++rigid_skin_aliases[!!checked];
    if(total==1 || total==256 || total%5000==0)
        std::fprintf(stderr,"[geometry-job/rigid-skin/%s] %s=%llu potential-aliases=%llu PASS\n",count==2?"one-pair":"bulk",checked?"strict-full-state-boundary":"native",total,rigid_skin_aliases[!!checked].load());
}
extern "C" int buzz_geometry_job_skin_flow_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_SKIN_FLOW",2);});
}
extern "C" void buzz_geometry_job_skin_flow_result(int work,int checked)
{
    const auto total=++skin_flow_counts[!!checked][!!work];
    if(total==1 || total==256 || total%5000==0)
        std::fprintf(stderr,"[geometry-job/skin-flow/%s] %s=%llu PASS\n",work?"work":"empty",checked?"strict-full-state-return":"native",total);
}
extern "C" int buzz_geometry_job_skin_directions_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_SKIN_DIRECTIONS",2);});
}
extern "C" void buzz_geometry_job_skin_directions_result(unsigned streams,int checked,int aliases)
{
    if(streams<1 || streams>3)std::abort();
    const auto total=++skin_directions_counts[!!checked][streams-1];
    if(aliases)++skin_directions_aliases[!!checked][streams-1];
    if(total==1 || total==256 || total%5000==0)
        std::fprintf(stderr,"[geometry-job/skin-directions/%u] %s=%llu aliases=%llu PASS\n",streams,checked?"strict-full-state-boundary":"native",total,skin_directions_aliases[!!checked][streams-1].load());
}
extern "C" int buzz_geometry_job_weighted_skin_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_WEIGHTED_SKIN",2);});
}
extern "C" void buzz_geometry_job_weighted_skin_result(unsigned count,int checked,int palette_alias)
{
    if(palette_alias){
        const auto aliases=++weighted_skin_alias_counts[!!checked];
        if(aliases==1 || aliases==256 || aliases%5000==0)
            std::fprintf(stderr,"[geometry-job/weighted-skin/palette-alias] %s=%llu PASS\n",checked?"strict-full-state-boundary":"native",aliases);
    }
    const auto total=++weighted_skin_counts[!!checked][count==2];
    if(total==1 || total==256 || total%5000==0)
        std::fprintf(stderr,"[geometry-job/weighted-skin/%s] %s=%llu PASS\n",count==2?"one-pair":"bulk",checked?"strict-full-state-boundary":"native",total);
}
extern "C" int buzz_geometry_job_skin_dispatch_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_SKIN_DISPATCH",2);});
}
extern "C" void buzz_geometry_job_skin_dispatch_result(int weighted,int checked)
{
    const auto total=++skin_dispatch_counts[!!checked][!!weighted];
    if(total==1 || total==256 || total%5000==0)
        std::fprintf(stderr,"[geometry-job/skin-dispatch/%s] %s=%llu PASS\n",weighted?"weighted-handoff":"rigid",checked?"strict-full-state-boundary":"native",total);
}
extern "C" int buzz_geometry_job_custom_stream_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_CUSTOM_STREAM",2);});
}
extern "C" void buzz_geometry_job_custom_stream_checked_result()
{
    const auto total=++custom_stream_checked;
    if(total==1 || total==256 || total%5000==0)
        std::fprintf(stderr,"[geometry-job/custom-stream] strict full-state + handoff %llu PASS\n",total);
}
extern "C" void buzz_geometry_job_run_lists_native_result(int nonempty)
{
    const auto count=nonempty?++run_lists_expanded_native:++run_lists_empty_native;
    if(count==1 || count==256 || count%5000==0)
        std::fprintf(stderr,"[geometry-job/run-lists/data] native-expanded=%llu native-empty=%llu\n",run_lists_expanded_native.load(),run_lists_empty_native.load());
}
extern "C" void buzz_geometry_job_run_lists_checked_result(int nonempty)
{
    const auto total=++run_lists_checked;
    const auto count=nonempty?++run_lists_expanded_checked:++run_lists_empty_checked;
    if(count==1 || count==256 || count%5000==0)
        std::fprintf(stderr,"[geometry-job/run-lists] strict full-state %llu PASS; strict-expanded=%llu strict-empty=%llu\n",total,run_lists_expanded_checked.load(),run_lists_empty_checked.load());
}
extern "C" void buzz_geometry_job_blend_lists_checked_result()
{
    const auto total=++blend_lists_checked;
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/blend-lists] strict full-state + I/O root %llu PASS\n",total);
}
extern "C" void buzz_geometry_job_blend_scatter_checked_result()
{
    const auto total=++blend_scatter_checked;
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/blend-scatter] strict full-state %llu PASS\n",total);
}

extern "C" int buzz_geometry_job_snorm_stream_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_SNORM_STREAM", 2);});
}
extern "C" void buzz_geometry_job_snorm_stream_checked_result()
{
    const auto total=++snorm_stream_checked;
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/snorm-stream] strict full-state %llu PASS\n",total);
}

extern "C" int buzz_geometry_job_i16_stream_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_I16_STREAM", 2);});
}
extern "C" void buzz_geometry_job_i16_stream_checked_result()
{
    const auto total=++i16_stream_checked;
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/i16-stream] strict full-state %llu PASS\n",total);
}

extern "C" int buzz_geometry_job_cmp_stream_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_CMP_STREAM", 2);});
}
extern "C" void buzz_geometry_job_cmp_stream_checked_result()
{
    const auto total=++cmp_stream_checked;
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/cmp-stream] strict full-state %llu PASS\n",total);
}

extern "C" int buzz_geometry_job_u8_integer_stream_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_U8_INTEGER_STREAM", 2);});
}
extern "C" void buzz_geometry_job_u8_integer_stream_checked_result()
{
    const auto total=++u8_integer_stream_checked;
    if(total==1 || total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/u8-integer-stream] strict full-state %llu PASS\n",total);
}

extern "C" int buzz_geometry_job_descriptors_enabled()
{
    static OptionCache enabled;return enabled.get([]{return output_option("BUZZ_GEOMETRY_JOB_DESCRIPTORS");});
}
extern "C" void buzz_geometry_job_descriptor_checked_result()
{
    const auto total=++descriptor_checked;
    if(total==256 || total%5000==0)std::fprintf(stderr,"[geometry-job/descriptor-control] strict full-state %llu PASS\n",total);
}
extern "C" void buzz_geometry_job_descriptor_dispatched(unsigned storage)
{
    if(storage<1 || storage>9)std::abort();++descriptor_dispatches[storage-1];
}

extern "C" int buzz_geometry_job_mode()
{
    static OptionCache mode;
    return mode.get([] {
        const char* value = std::getenv("BUZZ_GEOMETRY_JOB");
        if (!value) return 0;
        if (value[0] < '0' || value[0] > '2' || value[1]) std::abort();
        return value[0] - '0';
    });
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

extern "C" void buzz_geometry_job_callback_boundary(spu_context* ctx)
{
    if(is_shadow(ctx))mismatch("unaudited custom callback reached shadow",ctx);
    if(auto* r=recording_for(ctx))unsupported(*r,"custom callback has no audited contract");
}

extern "C" void buzz_geometry_job_indirect_call(spu_context* ctx)
{
    buzz_geometry_job_callback_boundary(ctx);
    // Keep the original full resolver, including IRQ/HLE/interpreter handling.
    // Even a registered function is not an independently audited callback.
    spu_indirect_branch(ctx);
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
        const auto* exit_difference = buzz::geometry_job_exit_difference(*expected, *copy);
        if (exit_difference || continuation != g_spu_trampoline_fn) {
            std::fprintf(stderr, "[geometry-job-audit] job exit controls=%s\n",
                         exit_difference ? exit_difference : "continuation");
            mismatch("job exit state/continuation", ctx);
        }
        const bool debug_stages = buzz_geometry_job_debug_stages() != 0;
        const auto difference = debug_stages ? buzz::compare_function_state(*expected, *copy) : buzz::FunctionDifference{};
        if (debug_stages && !difference.exact()) {
            std::fprintf(stderr, "[geometry-job-audit] r%d byte%d LS=%05X bytes=%u controls=%s\n",
                         difference.first_register, difference.first_register_byte, difference.first_ls,
                         difference.ls_bytes, difference.first_state ? difference.first_state : "equal");
            mismatch("debug full driver exit state", ctx);
        }
        (r->transfers > 1 ? drawn : culled).fetch_add(1);
        const auto n = checked.fetch_add(1) + 1;
        if (n == 256 || n % 5000 == 0)
            std::fprintf(stderr, "[%s] %s + I/O %llu PASS\n",
                         native_trial ? "geometry-job/native-outer" : "geometry-job-audit",
                         debug_stages ? "debug full-state" : "job-boundary", n);
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

struct buzz_geometry_io_root_check {
    spu_context* input;
    spu_context* owner;
    buzz::GeometryJobIoReplay* parent;
    size_t first;
    buzz_geometry_job_fn continuation;
};

extern "C" buzz_geometry_io_root_check* buzz_geometry_job_io_root_begin(spu_context* ctx)
{
    if (!is_shadow(ctx) || !buzz_geometry_job_debug_stages()) return nullptr;
    if (!replay || replay->error()) mismatch("I/O root without valid parent transcript", ctx);
    return new buzz_geometry_io_root_check{buzz::clone_function_state(ctx), ctx, replay,
                                          replay->consumed(), g_spu_trampoline_fn};
}

extern "C" void buzz_geometry_job_io_root_end(spu_context* ctx, buzz_geometry_io_root_check* check,
                                               buzz_geometry_job_fn reference_runner)
{
    if (!check) return; // JOB=1 and original/reference contexts have no transcript.
    if (!is_shadow(ctx) || replay != check->parent || ctx != check->owner || !reference_runner)
        mismatch("I/O root checkpoint owner/runner", ctx);
    auto io = replay->slice(check->first, replay->consumed());
    if (io.error()) mismatch(io.error(), ctx);
    const auto continuation = g_spu_trampoline_fn;
    shadow = check->input;
    replay = &io;
    g_spu_trampoline_fn = check->continuation;
    reference_runner(check->input);
    if (!io.finish()) mismatch(io.error(), check->input);
    const auto difference = buzz::compare_function_state(*check->input, *ctx);
    if (!difference.exact() || check->input->image_id != ctx->image_id ||
        check->input->host_depth != ctx->host_depth || check->input->policy_mode != ctx->policy_mode ||
        check->input->resident_ovl != ctx->resident_ovl || continuation != g_spu_trampoline_fn) {
        std::fprintf(stderr, "[geometry-job/io-root] r%d byte%d LS=%05X bytes=%u controls=%s\n",
                     difference.first_register, difference.first_register_byte, difference.first_ls,
                     difference.ls_bytes, difference.first_state ? difference.first_state : "equal");
        mismatch("strict I/O root exit state/continuation", ctx);
    }
    shadow = ctx;
    replay = check->parent;
    g_spu_trampoline_fn = continuation;
    buzz::release_function_state(check->input);
    delete check;
}

extern "C" int buzz_geometry_job_owns_admission(const spu_context* ctx)
{
    return ctx && admission_owner==ctx;
}
extern "C" void buzz_geometry_job_replay_case(const char* path)
{
    replay_case=path;
}

extern "C" void buzz_geometry_job_run(spu_context* ctx, buzz_geometry_job_fn reference,
                                      buzz_geometry_job_fn native, buzz_geometry_job_eligibility_fn supports)
{
    const int mode = buzz_geometry_job_mode();
    if (!mode) { buzz_geometry_job_audit_run(ctx, reference); return; }
    ++native_attempted;
    // Only JOB1 calls supports/native contiguously on the same context.
    // JOB2 clones and original/reference execution never inherit this proof.
    AdmissionOwnerScope admission(mode==1?ctx:nullptr);
    // Always test the original entry. Cloning an external LS or letting the
    // preamble mutate before this decision cannot turn a fallback into native.
    if (!eligible(ctx) || !supports(ctx)) { admission_owner=nullptr; ++native_fallback; reference(ctx); return; }
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
    if(buzz_geometry_job_mode() && buzz_geometry_job_descriptors_enabled()){
        std::fprintf(stderr,"[geometry-job/descriptor-control] strict-checked=%llu\n",descriptor_checked.load());
        for(unsigned storage=1;storage<=9;++storage)
            std::fprintf(stderr,"[geometry-job/descriptor-dispatch] storage=%u calls=%llu (callback selection; not native-codec coverage)\n",storage,descriptor_dispatches[storage-1].load());
    }
    if (buzz_geometry_job_mode() && buzz_geometry_job_codecs_enabled())
        std::fprintf(stderr,"[geometry-job/color-codec] strict-checked=%llu\n",color_checked.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_unit_vectors_enabled())
        std::fprintf(stderr,"[geometry-job/unit-vectors] strict-checked=%llu\n",unit_vectors_checked.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_half_stream_enabled())
        std::fprintf(stderr,"[geometry-job/half-stream] strict-checked=%llu\n",half_stream_checked.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_float32_stream_enabled())
        std::fprintf(stderr,"[geometry-job/float32-stream] strict-checked=%llu\n",float32_stream_checked.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_fixed_stream_enabled())
        std::fprintf(stderr,"[geometry-job/fixed-stream] strict-checked=%llu\n",fixed_stream_checked.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_snorm_stream_enabled())
        std::fprintf(stderr,"[geometry-job/snorm-stream] strict-checked=%llu\n",snorm_stream_checked.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_i16_stream_enabled())
        std::fprintf(stderr,"[geometry-job/i16-stream] strict-checked=%llu\n",i16_stream_checked.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_cmp_stream_enabled())
        std::fprintf(stderr,"[geometry-job/cmp-stream] strict-checked=%llu\n",cmp_stream_checked.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_u8_integer_stream_enabled())
        std::fprintf(stderr,"[geometry-job/u8-integer-stream] strict-checked=%llu\n",u8_integer_stream_checked.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_blend_scatter_enabled())
        std::fprintf(stderr,"[geometry-job/blend-scatter] strict-checked=%llu\n",blend_scatter_checked.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_custom_stream_enabled())
        std::fprintf(stderr,"[geometry-job/custom-stream] strict-checked=%llu\n",custom_stream_checked.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_run_lists_enabled())
        std::fprintf(stderr,"[geometry-job/run-lists] strict-checked=%llu native-expanded=%llu native-empty=%llu strict-expanded=%llu strict-empty=%llu\n",run_lists_checked.load(),run_lists_expanded_native.load(),run_lists_empty_native.load(),run_lists_expanded_checked.load(),run_lists_empty_checked.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_blend_lists_enabled())
        std::fprintf(stderr,"[geometry-job/blend-lists] strict-checked=%llu\n",blend_lists_checked.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_skin_dispatch_enabled())
        std::fprintf(stderr,"[geometry-job/skin-dispatch] native-weighted=%llu native-rigid=%llu strict-weighted=%llu strict-rigid=%llu\n",skin_dispatch_counts[0][1].load(),skin_dispatch_counts[0][0].load(),skin_dispatch_counts[1][1].load(),skin_dispatch_counts[1][0].load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_weighted_skin_enabled())
        std::fprintf(stderr,"[geometry-job/weighted-skin] native-bulk=%llu native-one-pair=%llu strict-bulk=%llu strict-one-pair=%llu native-palette-alias=%llu strict-palette-alias=%llu\n",weighted_skin_counts[0][0].load(),weighted_skin_counts[0][1].load(),weighted_skin_counts[1][0].load(),weighted_skin_counts[1][1].load(),weighted_skin_alias_counts[0].load(),weighted_skin_alias_counts[1].load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_skin_directions_enabled())for(unsigned stream=0;stream<3;++stream)
        std::fprintf(stderr,"[geometry-job/skin-directions/%u] native=%llu strict=%llu native-aliases=%llu strict-aliases=%llu\n",stream+1,skin_directions_counts[0][stream].load(),skin_directions_counts[1][stream].load(),skin_directions_aliases[0][stream].load(),skin_directions_aliases[1][stream].load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_skin_flow_enabled())
        std::fprintf(stderr,"[geometry-job/skin-flow] native-work=%llu native-empty=%llu strict-work=%llu strict-empty=%llu\n",skin_flow_counts[0][1].load(),skin_flow_counts[0][0].load(),skin_flow_counts[1][1].load(),skin_flow_counts[1][0].load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_rigid_skin_enabled())
        std::fprintf(stderr,"[geometry-job/rigid-skin] native-bulk=%llu native-one-pair=%llu strict-bulk=%llu strict-one-pair=%llu native-potential-aliases=%llu strict-potential-aliases=%llu\n",rigid_skin_counts[0][0].load(),rigid_skin_counts[0][1].load(),rigid_skin_counts[1][0].load(),rigid_skin_counts[1][1].load(),rigid_skin_aliases[0].load(),rigid_skin_aliases[1].load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_skin_cofactor_enabled())
        std::fprintf(stderr,"[geometry-job/skin-cofactor] native-bulk=%llu native-one-group=%llu strict-bulk=%llu strict-one-group=%llu native-aliases=%llu strict-aliases=%llu\n",skin_cofactor_counts[0][0].load(),skin_cofactor_counts[0][1].load(),skin_cofactor_counts[1][0].load(),skin_cofactor_counts[1][1].load(),skin_cofactor_aliases[0].load(),skin_cofactor_aliases[1].load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_skin_normalized_enabled())for(unsigned stream=0;stream<3;++stream)
        std::fprintf(stderr,"[geometry-job/skin-normalized/%u] native=%llu strict=%llu native-aliases=%llu strict-aliases=%llu\n",stream+1,skin_normalized_counts[0][stream].load(),skin_normalized_counts[1][stream].load(),skin_normalized_aliases[0][stream].load(),skin_normalized_aliases[1][stream].load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_index_deltas_enabled())
        std::fprintf(stderr,"[geometry-job/index-deltas] native-empty=%llu native-one-group=%llu native-bulk=%llu strict-empty=%llu strict-one-group=%llu strict-bulk=%llu native-aliases=%llu strict-aliases=%llu\n",index_deltas_counts[0][0].load(),index_deltas_counts[0][1].load(),index_deltas_counts[0][2].load(),index_deltas_counts[1][0].load(),index_deltas_counts[1][1].load(),index_deltas_counts[1][2].load(),index_deltas_aliases[0].load(),index_deltas_aliases[1].load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_index_unpack_enabled())
        std::fprintf(stderr,"[geometry-job/index-unpack] native-low=%llu native-high=%llu strict-low=%llu strict-high=%llu\n",index_unpack_counts[0][0].load(),index_unpack_counts[0][1].load(),index_unpack_counts[1][0].load(),index_unpack_counts[1][1].load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_index_expand_enabled())
        std::fprintf(stderr,"[geometry-job/index-expand] native-warmup=%llu native-short=%llu native-bulk=%llu strict-warmup=%llu strict-short=%llu strict-bulk=%llu native-capacity-exits=%llu strict-capacity-exits=%llu\n",index_expand_counts[0][0].load(),index_expand_counts[0][1].load(),index_expand_counts[0][2].load(),index_expand_counts[1][0].load(),index_expand_counts[1][1].load(),index_expand_counts[1][2].load(),index_expand_capacity[0].load(),index_expand_capacity[1].load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_index_relocate_enabled())
        std::fprintf(stderr,"[geometry-job/index-relocate] native-warmup=%llu native-short=%llu native-bulk=%llu strict-warmup=%llu strict-short=%llu strict-bulk=%llu\n",index_relocate_counts[0][0].load(),index_relocate_counts[0][1].load(),index_relocate_counts[0][2].load(),index_relocate_counts[1][0].load(),index_relocate_counts[1][1].load(),index_relocate_counts[1][2].load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_index_triangles_enabled())
        std::fprintf(stderr,"[geometry-job/index-triangles] native-warmup=%llu native-short=%llu native-bulk=%llu strict-warmup=%llu strict-short=%llu strict-bulk=%llu\n",index_triangles_counts[0][0].load(),index_triangles_counts[0][1].load(),index_triangles_counts[0][2].load(),index_triangles_counts[1][0].load(),index_triangles_counts[1][1].load(),index_triangles_counts[1][2].load());
    if(buzz_geometry_job_mode() && buzz_geometry_job_index_control_enabled()){
        const char* paths[]={"empty","raw","zero","low","high"};
        for(unsigned path=0;path<5;++path)std::fprintf(stderr,"[geometry-job/index-control/%s] native=%llu strict-full-state-return=%llu\n",paths[path],index_control_counts[0][path].load(),index_control_counts[1][path].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_input_control_enabled()){
        const char* names[]={"empty","raw0","builtin1","builtin2","builtin3","builtin4","builtin5"};
        for(unsigned path=0;path<7;++path)std::fprintf(stderr,"[geometry-job/input-control/%s] native=%llu strict-full-state-return=%llu\n",names[path],input_control_counts[0][path].load(),input_control_counts[1][path].load());
        for(unsigned format=1;format<=5;++format)if(buzz_geometry_job_builtin_input_enabled(format))std::fprintf(stderr,"[geometry-job/builtin-input%u] native=%llu strict-full-state-handoff=%llu\n",format,builtin_input_counts[0][format-1].load(),builtin_input_counts[1][format-1].load());
    }
    if(buzz_geometry_job_mode())for(unsigned format=0;format<4;++format)if(buzz_geometry_job_builtin_output_enabled(format))
        std::fprintf(stderr,"[geometry-job/builtin-output%u] native=%llu strict-full-state-handoff=%llu\n",format,builtin_output_counts[0][format].load(),builtin_output_counts[1][format].load());
    if(buzz_geometry_job_mode() && buzz_geometry_job_conversion_control_enabled()){
        const char* names[]={"existing","empty","descriptor","builtin0","builtin1","builtin2","builtin3"};
        for(unsigned path=0;path<7;++path)std::fprintf(stderr,"[geometry-job/conversion-control/%s] native=%llu strict-full-state-return=%llu\n",names[path],conversion_control_counts[0][path].load(),conversion_control_counts[1][path].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_formatter_enabled()){
        const char* names[]={"empty","known","skipped","mixed"};
        for(unsigned path=0;path<4;++path)std::fprintf(stderr,"[geometry-job/formatter/%s] native=%llu strict-full-state-return=%llu\n",names[path],formatter_counts[0][path].load(),formatter_counts[1][path].load());
        for(unsigned i=0;i<7;++i)std::fprintf(stderr,"[geometry-job/formatter/storage%u] native-roots=%llu strict-roots=%llu\n",i+1,formatter_types[0][i].load(),formatter_types[1][i].load());
        if(buzz_geometry_job_formatter_registers_enabled())std::fprintf(stderr,"[geometry-job/formatter-registers] native=%llu strict-parent=%llu children=%llu strict-children=%llu\n",formatter_registers_roots[0].load(),formatter_registers_roots[1].load(),formatter_registers_children[0].load(),formatter_registers_children[1].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_i16_output_enabled()){
        const char* names[]={"skip","single-quad","double-quad","mixed"};
        for(unsigned path=0;path<4;++path)std::fprintf(stderr,"[geometry-job/i16-output/%s] native=%llu strict-full-state-handoff=%llu\n",names[path],i16_output_counts[0][path].load(),i16_output_counts[1][path].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_u8_integer_output_enabled()){
        const char* names[]={"skip","single-quad","double-quad","mixed"};
        for(unsigned path=0;path<4;++path)std::fprintf(stderr,"[geometry-job/u8-integer-output/%s] native=%llu strict-full-state-handoff=%llu\n",names[path],u8_integer_output_counts[0][path].load(),u8_integer_output_counts[1][path].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_unorm_output_enabled()){
        const char* names[]={"skip","single-quad","double-quad","mixed"};
        for(unsigned path=0;path<4;++path)std::fprintf(stderr,"[geometry-job/unorm-output/%s] native=%llu strict-full-state-handoff=%llu\n",names[path],unorm_output_counts[0][path].load(),unorm_output_counts[1][path].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_snorm_output_enabled()){
        const char* names[]={"skip","single-quad","double-quad","mixed"};
        for(unsigned path=0;path<4;++path)std::fprintf(stderr,"[geometry-job/snorm-output/%s] native=%llu strict-full-state-handoff=%llu\n",names[path],snorm_output_counts[0][path].load(),snorm_output_counts[1][path].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_cmp_output_enabled()){
        const char* names[]={"skip","single-quad","double-quad","mixed"};
        for(unsigned path=0;path<4;++path)std::fprintf(stderr,"[geometry-job/cmp-output/%s] native=%llu strict-full-state-handoff=%llu\n",names[path],cmp_output_counts[0][path].load(),cmp_output_counts[1][path].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_half_output_enabled()){
        const char* names[]={"skip","single-quad","double-quad","mixed"};
        for(unsigned path=0;path<4;++path)std::fprintf(stderr,"[geometry-job/half-output/%s] native=%llu strict-full-state-handoff=%llu\n",names[path],half_output_counts[0][path].load(),half_output_counts[1][path].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_float_output_enabled()){
        const char* names[]={"skip","single-quad","double-quad","mixed"};
        for(unsigned path=0;path<4;++path)std::fprintf(stderr,"[geometry-job/float-output/%s] native=%llu strict-full-state-handoff=%llu\n",names[path],float_output_counts[0][path].load(),float_output_counts[1][path].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_ring_enabled()){
        const char* names[]={"return","retained-initial-report","retained-shared-report"};
        for(unsigned path=0;path<3;++path)std::fprintf(stderr,"[geometry-job/ring/%s] native-prefix=%llu strict-full-state-io=%llu\n",names[path],ring_counts[0][path].load(),ring_counts[1][path].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_alternate_cull_enabled()){
        const char* names[]={"warmup","short","bulk"};
        for(unsigned path=0;path<3;++path)std::fprintf(stderr,"[geometry-job/alternate-cull/%s] native=%llu strict-full-state-handoff=%llu\n",names[path],alternate_cull_counts[0][path].load(),alternate_cull_counts[1][path].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_transform_control_enabled()){
        const char* names[]={"bypass-skip","bypass-publish","idle","one","two","three","alternate"};
        for(unsigned path=0;path<7;++path)std::fprintf(stderr,"[geometry-job/transform-control/%s] native=%llu strict-full-state-return=%llu\n",names[path],transform_control_counts[0][path].load(),transform_control_counts[1][path].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_vertex_prepare_enabled()){
        const char* names[]={"base","middle","five","other"};
        for(unsigned path=0;path<4;++path)std::fprintf(stderr,"[geometry-job/vertex-prepare/%s] native=%llu strict-full-state-handoff=%llu\n",names[path],vertex_prepare_counts[0][path].load(),vertex_prepare_counts[1][path].load());
    }
    if(buzz_geometry_job_mode() && buzz_geometry_job_transform_finish_enabled())
        for(unsigned path=0;path<2;++path)std::fprintf(stderr,"[geometry-job/transform-finish/%s] native=%llu strict-full-state-return=%llu\n",path?"publish":"count-only",transform_finish_counts[0][path].load(),transform_finish_counts[1][path].load());
    if (buzz_geometry_job_mode()) for (unsigned stage = 0; stage < 61; ++stage)
        if (stage >= 56 ? buzz_geometry_job_builtin_input_enabled(stage-55) : stage == 55 ? buzz_geometry_job_input_control_enabled() : stage >= 51 ? buzz_geometry_job_builtin_output_enabled(stage-51) : stage == 50 ? buzz_geometry_job_conversion_control_enabled() : stage == 49 ? buzz_geometry_job_formatter_enabled() : stage == 48 ? buzz_geometry_job_u8_integer_output_enabled() : stage == 47 ? buzz_geometry_job_i16_output_enabled() : stage == 46 ? buzz_geometry_job_unorm_output_enabled() : stage == 45 ? buzz_geometry_job_snorm_output_enabled() : stage == 44 ? buzz_geometry_job_cmp_output_enabled() : stage == 43 ? buzz_geometry_job_half_output_enabled() : stage == 42 ? buzz_geometry_job_float_output_enabled() : stage == 41 ? buzz_geometry_job_ring_enabled() : stage == 40 ? buzz_geometry_job_alternate_cull_enabled() : stage == 39 ? buzz_geometry_job_transform_control_enabled() : stage == 38 ? buzz_geometry_job_vertex_prepare_enabled() : stage == 37 ? buzz_geometry_job_transform_finish_enabled() : stage == 36 ? buzz_geometry_job_index_control_enabled() : stage == 35 ? buzz_geometry_job_index_triangles_enabled() : stage == 34 ? buzz_geometry_job_index_relocate_enabled() : stage == 33 ? buzz_geometry_job_index_expand_enabled() : stage == 32 ? buzz_geometry_job_index_unpack_enabled() : stage == 31 ? buzz_geometry_job_index_deltas_enabled() : stage == 30 ? buzz_geometry_job_skin_normalized_enabled() : stage == 29 ? buzz_geometry_job_skin_cofactor_enabled() : stage == 28 ? buzz_geometry_job_rigid_skin_enabled() : stage == 27 ? buzz_geometry_job_skin_flow_enabled() : stage == 26 ? buzz_geometry_job_skin_directions_enabled() : stage == 25 ? buzz_geometry_job_weighted_skin_enabled() : stage == 24 ? buzz_geometry_job_skin_dispatch_enabled() : stage == 23 ? buzz_geometry_job_custom_stream_enabled() : stage == 22 ? buzz_geometry_job_run_lists_enabled() : stage == 21 ? buzz_geometry_job_blend_lists_enabled() : stage == 20 ? buzz_geometry_job_blend_scatter_enabled() : stage == 19 ? buzz_geometry_job_u8_integer_stream_enabled() : stage == 18 ? buzz_geometry_job_cmp_stream_enabled() : stage == 17 ? buzz_geometry_job_i16_stream_enabled() : stage == 16 ? buzz_geometry_job_snorm_stream_enabled() : stage == 15 ? buzz_geometry_job_fixed_stream_enabled() : stage == 14 ? buzz_geometry_job_float32_stream_enabled() : stage == 13 ? buzz_geometry_job_half_stream_enabled() : stage == 12 ? buzz_geometry_job_unit_vectors_enabled() : stage == 11 ? buzz_geometry_job_descriptors_enabled() : stage == 10 ? buzz_geometry_job_codecs_enabled() : stage >= 8 ? buzz_geometry_job_attributes_enabled() : stage == 0 ? buzz_geometry_job_layout_enabled() : stage == 1 ? buzz_geometry_job_vertex_enabled() : stage == 2 ? buzz_geometry_job_commands_enabled() : stage == 3 ? buzz_geometry_job_command_publish_enabled() : stage == 7 ? buzz_geometry_job_full_commands_enabled() : buzz_geometry_job_emit_enabled())
            std::fprintf(stderr, "[geometry-job/%s] native=%llu fallback=%llu reused-layout=%llu\n", output_labels[stage],
                output_counters[stage].native.load(), output_counters[stage].fallback.load(), output_counters[stage].reused.load());
    if (buzz_geometry_job_mode() && buzz_geometry_job_publish_enabled())
        std::fprintf(stderr, "[geometry-job/index-publish] native=%llu fallback=%llu\n",
                     publication_native.load(), publication_fallback.load());
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
    if (buzz_geometry_job_mode()) {
        const bool disabled = !buzz_geometry_job_legacy_enabled() &&
            (!buzz_geometry_job_host_pipeline_enabled() || !buzz_geometry_job_publication_enabled() ||
             !buzz_geometry_job_commands_enabled());
        return failures.load() || (!disabled &&
            (buzz_geometry_job_mode() == 2 ? !native_compared.load() : !native_executed.load()));
    }
    return buzz_geometry_job_audit_enabled() && (!checked.load() || failures.load());
}
