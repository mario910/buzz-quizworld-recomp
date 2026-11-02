/* Exercise the real whole-driver controller with synthetic channel producers. */
#include "../port/native_geometry_job.h"
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

static const char* scenario = "pass";
static unsigned calls = 0, guest_writes = 0;
static unsigned direct_effects = 0;
static bool expected_config_reject=false;
static unsigned reference_calls = 0, native_calls = 0, eligibility_calls = 0;
static spu_context* retained = nullptr;
static bool selected(const char* name) { return !strcmp(scenario, name); }
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
int buzz_geometry_debug = 0;
void spu_wrch(spu_context* ctx, uint32_t channel, u128 argument)
{
    assert(channel == 21 && argument._u32[0] == 0x20);
    ++guest_writes;
    auto observe = [&] {
        buzz_geometry_job_observe(ctx, 2, 0xe800, 0x123000, 16, ctx->ls + 0xe800);
    };
    if (selected("async")) { std::thread worker(observe); worker.join(); }
    else observe();
    ctx->mfc_tag_status = 8;
}
u128 spu_rdch(spu_context*, uint32_t) { return {}; }
uint32_t spu_rchcnt(spu_context*, uint32_t) { return 0; }
void spu_drain_call(spu_context* ctx, uint32_t pc)
{
    assert(ctx->host_depth == 0 && ctx->pc == pc && g_spu_trampoline_fn == spu_indirect_branch);
    g_spu_trampoline_fn = nullptr;
}
void spu_indirect_branch(spu_context* ctx) {
    assert((selected("direct_reference") || selected("callback_reference")) && ctx==retained && calls==1);
    ++direct_effects;ctx->ls[0xe80e]^=0x91;
}
void spu_indirect_branch_mt(spu_context*) { assert(false); }
buzz_geometry_job_fn spu_lookup(uint32_t, int) { return nullptr; }
}

static void producer(spu_context* ctx)
{
    ++calls;
    if (calls == 1) retained = ctx;
    if (calls == 1 && selected("unknown_reference")) buzz_geometry_job_step(ctx, 0xbeef);
    if (calls == 2 && selected("unknown_shadow")) buzz_geometry_job_step(ctx, 0xbeef);
    if(calls==1 && selected("direct_reference")){
        const auto pc=ctx->pc;ctx->pc=0xbeef;buzz_geometry_job_indirect_call(ctx);ctx->pc=pc;
    }
    if(calls==1 && selected("callback_reference")){
        auto before=std::make_unique<spu_context>();memcpy(before.get(),ctx,sizeof(*ctx));const auto scheduled=g_spu_trampoline_fn;
        buzz_geometry_job_callback_boundary(ctx);
        assert(!memcmp(before.get(),ctx,sizeof(*ctx)) && scheduled==g_spu_trampoline_fn);
        spu_indirect_branch(ctx); // Caller keeps its original dispatch semantics.
    }
    if (calls == 2 && selected("bad_write")) ctx->ls[0xe80f] ^= 1;
    if (calls == 1 && selected("first_event")) ctx->event_status = 0x80;
    if (calls == 1 && selected("first_reservation")) ctx->resv_valid = 0;
    if (calls == 1 && selected("first_queue")) {
        ctx->ch_in_mbox.count = 1;
        ctx->ch_in_mbox.q[0] = ctx->ch_in_mbox.value = 0xcafe;
    }
    if (calls == 2 && selected("bad_event_before")) ctx->event_status = 0x80;
    if (calls == 2 && selected("bad_reservation_before")) ctx->resv_valid ^= 1;
    u128 argument{};
    argument._u32[0] = 0x20;
    if (!(calls == 2 && selected("missing_channel"))) buzz_geometry_job_wrch(ctx, 21, argument);
    if (calls == 2 && selected("extra_channel")) buzz_geometry_job_wrch(ctx, 21, argument);
    if(calls==2 && selected("direct_shadow")){
        ctx->pc=0xbeef;buzz_geometry_job_indirect_call(ctx);
    }
    if(calls==2 && selected("callback_shadow"))buzz_geometry_job_callback_boundary(ctx);
    if (calls == 2 && selected("bad_exit")) ctx->ls[0x3ffff] ^= 1;
    if (calls == 2 && selected("bad_register")) ctx->gpr[127]._u32[3] ^= 1;
    if (calls == 2 && selected("bad_control")) ctx->list_stall_stat ^= 1;
    if (calls == 2 && selected("bad_image")) ctx->image_id ^= 1;
    if (calls == 2 && selected("bad_policy")) ctx->policy_mode ^= 1;
    if (calls == 2 && selected("bad_overlay")) ctx->resident_ovl ^= 1;
    if (calls == 1 && selected("unstable_exit")) ctx->event_status = 0x80;
    if (calls == 2 && selected("post_exit_input")) retained->event_status = 0x80;
    if (calls == 1 && selected("unstable_queue")) {
        ctx->ch_in_mbox.count = 1;
        ctx->ch_in_mbox.q[0] = ctx->ch_in_mbox.value = 0xcafe;
    }
    if (calls == 2 && selected("post_exit_queue")) {
        retained->ch_in_mbox.count = 1;
        retained->ch_in_mbox.q[0] = retained->ch_in_mbox.value = 0xcafe;
    }
    ctx->pc = ctx->gpr[0]._u32[0];
    if (!ctx->host_depth) g_spu_trampoline_fn = spu_indirect_branch; // Ordinary SPU_RET.
}
static void reference(spu_context* ctx) { assert(!buzz_geometry_job_owns_admission(ctx)); ++reference_calls; producer(ctx); }
static void native(spu_context* ctx)
{
    if (buzz_geometry_job_mode() == 1) assert(ctx->host_depth == 0);
    assert(bool(buzz_geometry_job_owns_admission(ctx))==(buzz_geometry_job_mode()==1));
    ++native_calls; producer(ctx);
}
static int supports(const spu_context* ctx)
{
    assert(bool(buzz_geometry_job_owns_admission(ctx))==(buzz_geometry_job_mode()==1));
    ++eligibility_calls;
    assert(ctx->ls[0xe80f] == 0xa5 && ctx->pc == 0x1c18 && calls == 0);
    return !selected("unsupported");
}

static void expected_abort(int)
{
    if(expected_config_reject){std::fputs("[config] rejected\n",stderr);std::fflush(stderr);std::_Exit(75);}
    // Even a failed shadow must not publish another real output.
    std::_Exit(guest_writes == 1 && calls == 2 && direct_effects==0 ? 73 : 74);
}

static int builtin_input1_option(){return buzz_geometry_job_builtin_input_enabled(1);}
static int builtin_input2_option(){return buzz_geometry_job_builtin_input_enabled(2);}
static int builtin_input3_option(){return buzz_geometry_job_builtin_input_enabled(3);}
static int builtin_input4_option(){return buzz_geometry_job_builtin_input_enabled(4);}
static int builtin_input5_option(){return buzz_geometry_job_builtin_input_enabled(5);}
static int builtin0_option(){return buzz_geometry_job_builtin_output_enabled(0);}
static int builtin1_option(){return buzz_geometry_job_builtin_output_enabled(1);}
static int builtin2_option(){return buzz_geometry_job_builtin_output_enabled(2);}
static int builtin3_option(){return buzz_geometry_job_builtin_output_enabled(3);}
int main(int argc, char** argv)
{
    if (argc > 1) scenario = argv[1];
    std::signal(SIGABRT, expected_abort);
    if(selected("fallback_flush")){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_RIGID_SKIN,0,0);
        std::_Exit(0); // Deliberately bypass final summaries and CRT flushing.
    }
    if(selected("profile-coverage")){
        assert(argc==3 && buzz_geometry_job_mode() && !buzz_geometry_job_legacy_enabled());
        assert(buzz_geometry_job_audit_failures()==atoi(argv[2]));
        puts("[profile] deliberate fallback/missing coverage distinction PASS");return 0;
    }
    if(selected("profile")){
        assert(argc==5);
        const int host=atoi(argv[2]),publication=atoi(argv[3]),commands=atoi(argv[4]);
        assert(!buzz_geometry_job_mode()); // Explicit JOB opt-in remains required.
        assert(!buzz_geometry_job_legacy_enabled());
        assert(buzz_geometry_job_host_pipeline_enabled()==host);
        assert(buzz_geometry_job_publication_enabled()==publication);
        assert(buzz_geometry_job_publish_enabled()==publication);
        assert(buzz_geometry_job_layout_enabled()==publication);
        assert(buzz_geometry_job_vertex_enabled()==publication);
        assert(buzz_geometry_job_ring_enabled()==publication);
        assert(buzz_geometry_job_commands_enabled()==commands);
        assert(buzz_geometry_job_command_publish_enabled()==commands);
        assert(buzz_geometry_job_emit_enabled()==commands);
        assert(buzz_geometry_job_full_commands_enabled()==commands);
        assert(!buzz_geometry_host_output_enabled());
        assert(!buzz_geometry_job_attributes_enabled());
        assert(!buzz_geometry_job_unit_vectors_enabled());
        assert(!buzz_geometry_job_float_output_enabled());
        assert(!buzz_geometry_job_formatter_registers_enabled());
        // Later children must inherit the already-read groups, even if a
        // diagnostic caller changes its process environment after startup.
#ifdef _WIN32
        _putenv_s("BUZZ_GEOMETRY_JOB_PUBLICATION",publication?"0":"1");
        _putenv_s("BUZZ_GEOMETRY_JOB_COMMANDS",commands?"0":"1");
        _putenv_s("BUZZ_GEOMETRY_JOB_LEGACY","1");
#endif
        assert(buzz_geometry_job_publish_enabled()==publication);
        assert(buzz_geometry_job_commands_enabled()==commands);
        assert(!buzz_geometry_job_weighted_skin_enabled());
        puts("[profile] groups/legacy isolation/cache PASS");return 0;
    }
    if(selected("option")){
        assert(argc==4);
        struct Option {const char* name;int(*get)();};
        const Option options[]={
            {"LEGACY",buzz_geometry_job_legacy_enabled},{"PUBLICATION",buzz_geometry_job_publication_enabled},
            {"HOST_PIPELINE",buzz_geometry_job_host_pipeline_enabled},
            {"DEBUG_STAGES",buzz_geometry_job_debug_stages},{"PUBLISH",buzz_geometry_job_publish_enabled},{"LAYOUT",buzz_geometry_job_layout_enabled},
            {"VERTEX",buzz_geometry_job_vertex_enabled},{"COMMANDS",buzz_geometry_job_commands_enabled},
            {"COMMAND_PUBLISH",buzz_geometry_job_command_publish_enabled},{"EMIT",buzz_geometry_job_emit_enabled},
            {"FULL_COMMANDS",buzz_geometry_job_full_commands_enabled},{"ATTRIBUTES",buzz_geometry_job_attributes_enabled},
            {"CODECS",buzz_geometry_job_codecs_enabled},{"DESCRIPTORS",buzz_geometry_job_descriptors_enabled},
            {"BLEND_LISTS",buzz_geometry_job_blend_lists_enabled},
            {"RUN_LISTS",buzz_geometry_job_run_lists_enabled},
            {"CUSTOM_STREAM",buzz_geometry_job_custom_stream_enabled},
            {"SKIN_DISPATCH",buzz_geometry_job_skin_dispatch_enabled},
            {"WEIGHTED_SKIN",buzz_geometry_job_weighted_skin_enabled},
            {"SKIN_DIRECTIONS",buzz_geometry_job_skin_directions_enabled},
            {"SKIN_FLOW",buzz_geometry_job_skin_flow_enabled},
            {"RIGID_SKIN",buzz_geometry_job_rigid_skin_enabled},
            {"SKIN_COFACTOR",buzz_geometry_job_skin_cofactor_enabled},
            {"SKIN_NORMALIZED",buzz_geometry_job_skin_normalized_enabled},
            {"INDEX_DELTAS",buzz_geometry_job_index_deltas_enabled},
            {"INDEX_UNPACK",buzz_geometry_job_index_unpack_enabled},
            {"INDEX_EXPAND",buzz_geometry_job_index_expand_enabled},
            {"INDEX_RELOCATE",buzz_geometry_job_index_relocate_enabled},
            {"INDEX_TRIANGLES",buzz_geometry_job_index_triangles_enabled},
            {"INDEX_CONTROL",buzz_geometry_job_index_control_enabled},
            {"TRANSFORM_FINISH",buzz_geometry_job_transform_finish_enabled},
            {"VERTEX_PREPARE",buzz_geometry_job_vertex_prepare_enabled},
            {"TRANSFORM_CONTROL",buzz_geometry_job_transform_control_enabled},
            {"ALTERNATE_CULL",buzz_geometry_job_alternate_cull_enabled},
            {"RING",buzz_geometry_job_ring_enabled},
            {"FLOAT_OUTPUT",buzz_geometry_job_float_output_enabled},
            {"HALF_OUTPUT",buzz_geometry_job_half_output_enabled},
            {"CMP_OUTPUT",buzz_geometry_job_cmp_output_enabled},
            {"SNORM_OUTPUT",buzz_geometry_job_snorm_output_enabled},
            {"UNORM_OUTPUT",buzz_geometry_job_unorm_output_enabled},
            {"U8_INTEGER_OUTPUT",buzz_geometry_job_u8_integer_output_enabled},
            {"I16_OUTPUT",buzz_geometry_job_i16_output_enabled},
            {"FORMATTER",buzz_geometry_job_formatter_enabled},
            {"FORMATTER_SPANS",buzz_geometry_job_formatter_spans_enabled},
            {"FORMATTER_REGISTERS",buzz_geometry_job_formatter_registers_enabled},
            {"HOST_OUTPUT",buzz_geometry_host_output_enabled},
            {"CONVERSION_CONTROL",buzz_geometry_job_conversion_control_enabled},
            {"INPUT_CONTROL",buzz_geometry_job_input_control_enabled},
            {"BUILTIN20_INPUT",builtin_input1_option},
            {"BUILTIN24_INPUT",builtin_input2_option},
            {"BUILTIN24_CMP_INPUT",builtin_input3_option},
            {"BUILTIN12_INPUT",builtin_input4_option},
            {"BUILTIN15_INPUT",builtin_input5_option},

            {"BUILTIN_FLOAT3_OUTPUT",builtin0_option},{"BUILTIN20_OUTPUT",builtin1_option},
            {"BUILTIN24_OUTPUT",builtin2_option},{"BUILTIN24_CMP_OUTPUT",builtin3_option},
            {"UNIT_VECTORS",buzz_geometry_job_unit_vectors_enabled},{"HALF_STREAM",buzz_geometry_job_half_stream_enabled},
            {"FLOAT32_STREAM",buzz_geometry_job_float32_stream_enabled},{"FIXED_STREAM",buzz_geometry_job_fixed_stream_enabled},{"SNORM_STREAM",buzz_geometry_job_snorm_stream_enabled},{"I16_STREAM",buzz_geometry_job_i16_stream_enabled},{"CMP_STREAM",buzz_geometry_job_cmp_stream_enabled},{"U8_INTEGER_STREAM",buzz_geometry_job_u8_integer_stream_enabled},{"BLEND_SCATTER",buzz_geometry_job_blend_scatter_enabled}};
        for(const auto& option:options)if(!strcmp(argv[2],option.name)){
            expected_config_reject=!strcmp(argv[3],"reject");
            const int actual=option.get();
            if(expected_config_reject)return 76;
            assert(actual==std::atoi(argv[3]));puts("[config] accepted PASS");return 0;
        }
        return 77;
    }

    auto ctx = std::make_unique<spu_context>();
    memset(ctx.get(), 0, sizeof(*ctx));
    ctx->ls = ctx->ls_store;
    ctx->image_id = 41;
    ctx->pc = 0x1c18;
    ctx->gpr[0]._u32[0] = 0x3ff00;
    if (selected("first_reservation")) ctx->resv_valid = 1;
    ctx->ls[0xe80f] = 0xa5;
    std::unique_ptr<uint8_t[]> external;
    if (selected("external_ls")) {
        external = std::make_unique<uint8_t[]>(SPU_LS_SIZE);
        memcpy(external.get(), ctx->ls, SPU_LS_SIZE);
        ctx->ls = external.get();
    }
    const int mode = buzz_geometry_job_mode();
    assert(!buzz_geometry_job_owns_admission(ctx.get()));
    buzz_geometry_job_run(ctx.get(), reference, native, supports);
    assert(!buzz_geometry_job_owns_admission(ctx.get()));
    buzz_geometry_job_audit_report();
    assert(guest_writes == 1 && ctx->ls[0xe80f] == 0xa5 && ctx->host_depth == 0);
    assert(direct_effects==(selected("direct_reference") || selected("callback_reference")?1u:0u));
    if (mode == 1) {
        assert(calls == 1 && native_calls == 1 && !reference_calls && eligibility_calls == 1);
        assert(!buzz_geometry_job_audit_failures() && g_spu_trampoline_fn == spu_indirect_branch);
    } else if (selected("off")) {
        assert(calls == 1 && reference_calls == 1 && !native_calls && !eligibility_calls);
        assert(!buzz_geometry_job_audit_failures());
    } else if (selected("pass") || selected("post_exit_input") || selected("post_exit_queue") ||
               (!buzz_geometry_job_debug_stages() && (selected("bad_exit") || selected("bad_register")))) {
        assert(calls == 2 && !buzz_geometry_job_audit_failures());
        assert(g_spu_trampoline_fn == spu_indirect_branch);
        assert(native_calls == (mode == 2 ? 1 : 0));
        assert(reference_calls == (mode == 2 ? 1 : 2));
        if (selected("post_exit_input")) assert(ctx->event_status == 0x80);
        if (selected("post_exit_queue")) assert(ctx->ch_in_mbox.count == 1 && ctx->ch_in_mbox.value == 0xcafe);
    } else if (selected("first_event") || selected("first_reservation") || selected("first_queue") ||
               selected("unsupported") || selected("external_ls") || selected("async") || selected("unknown_reference") || selected("direct_reference") || selected("callback_reference") ||
               selected("unstable_exit") || selected("unstable_queue")) {
        assert(calls == 1 && buzz_geometry_job_audit_failures());
        assert(!native_calls && reference_calls == 1);
    } else {
        return 75; // A deliberately wrong shadow must have aborted.
    }
    printf("Whole-driver controller %s: real I/O once, reference/shadow/fallback accounting PASS\n", scenario);
}
