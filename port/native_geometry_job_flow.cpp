/* Native outer job flow, compiled with the reviewed SPU integer/RTZ flags.
 * Keep this separate from the baseline controller's pre-main constructors. */
#define SPU_LS_DEBUG_HOOKS 0
#include "native_geometry_job.h"
#include "native_geometry_job_call.h"
#include "native_geometry_pipeline.h"
#include "native_geometry_commands.h"
#include "native_geometry_command_publish.h"
#include "native_geometry_position_commands.h"
#include "native_geometry_full_commands.h"
#include "native_geometry_attributes.h"
#include "native_geometry_color.h"
#include "native_geometry_unit_vectors.h"
#include "native_geometry_half_stream.h"
#include "native_geometry_float32_stream.h"
#include "native_geometry_fixed_stream.h"
#include "native_geometry_i16_stream.h"
#include "native_geometry_cmp_stream.h"
#include "native_geometry_u8_integer_stream.h"
#include "native_geometry_snorm_stream.h"
#include "native_geometry_blend_scatter.h"
#include "native_geometry_blend_lists.h"
#include "native_geometry_descriptors.h"
#include "native_function_check.h"
#include <atomic>
#include <cstdio>

namespace {
thread_local spu_context* native_pipeline_context = nullptr;
struct NativePipelineScope {
    spu_context* previous;
    explicit NativePipelineScope(spu_context* ctx) : previous(native_pipeline_context) { native_pipeline_context = ctx; }
    ~NativePipelineScope() { native_pipeline_context = previous; }
};
struct BlendListReference {
    buzz_geometry_job_fn entry, tail;
};
thread_local const BlendListReference* blend_list_reference = nullptr;
void run_blend_list_reference(spu_context* ctx)
{
    const auto* original = blend_list_reference;
    if (!original) buzz_geometry_job_native_fail(ctx, "missing list/blend reference scope");
    g_spu_trampoline_fn = nullptr; original->entry(ctx);
    // Return by identity: r0 may itself equal D04 or another interior PC.
    for (unsigned steps = 0; g_spu_trampoline_fn != original->tail; ++steps) {
        if (!g_spu_trampoline_fn || steps >= 1000000)
            buzz_geometry_job_native_fail(ctx, "list/blend reference escaped root tail");
        auto next = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; next(ctx);
    }
    g_spu_trampoline_fn = nullptr; original->tail(ctx);
}
}

extern "C" void buzz_geometry_job_blend_lists(spu_context* ctx, buzz_geometry_job_fn reference,
                                               const buzz_geometry_blend_list_functions* functions)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_blend_lists_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryBlendLists plan;
    if (!functions || !functions->unpack || !functions->attributes || !functions->normal_pointer ||
        !functions->normalize || !functions->tail || !buzz::read_geometry_blend_lists(*ctx, plan)) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_BLEND_LISTS, 0, 0); reference(ctx); return;
    }
    auto* check = buzz_geometry_job_io_root_begin(ctx);
    buzz::blend_geometry_lists(*ctx, plan, *functions);
    const BlendListReference original{reference, functions->tail};
    const auto* previous = blend_list_reference; blend_list_reference = &original;
    buzz_geometry_job_io_root_end(ctx, check, run_blend_list_reference);
    blend_list_reference = previous;
    if (check) buzz_geometry_job_blend_lists_checked_result();
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_BLEND_LISTS, 1, 0);
}

extern "C" void buzz_geometry_job_prepare_commands(spu_context* ctx, buzz_geometry_job_fn reference)
{
    // In JOB=2 the retained reference must stay original, even when it calls
    // this same library entry. A foreign comparison clone is not this owner.
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_commands_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryCommandBuffer plan;
    const bool supported = buzz::read_geometry_command_buffer(*ctx, plan);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_COMMAND_BUFFER, supported, 0);
    if (!supported) { reference(ctx); return; }
    buzz::prepare_geometry_command_buffer(*ctx, plan);
    SPU_RET(ctx);
}

extern "C" void buzz_geometry_job_publish_commands(spu_context* ctx, buzz_geometry_job_fn reference)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_command_publish_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryCommandPublication plan;
    const char* reason = nullptr;
    const bool supported = buzz::read_geometry_command_publication(*ctx, plan, &reason);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_COMMAND_PUBLISH, supported, 0);
    if (!supported) {
        static std::atomic_flag reported = ATOMIC_FLAG_INIT;
        if (!reported.test_and_set()) std::fprintf(stderr, "[geometry-job/command-publish] first fallback: %s\n", reason);
        reference(ctx); return;
    }
    buzz::publish_geometry_commands(*ctx, plan);
    SPU_RET(ctx);
}

extern "C" void buzz_geometry_job_command_bindings(spu_context* ctx, buzz_geometry_job_fn reference, int reset)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_emit_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryCommandBindings plan;
    const bool supported = buzz::read_geometry_command_bindings(*ctx, reset != 0, plan);
    buzz_geometry_job_output_result(reset ? BUZZ_GEOMETRY_JOB_COMMAND_RESET : BUZZ_GEOMETRY_JOB_COMMAND_BIND, supported, 0);
    if (!supported) { reference(ctx); return; }
    if (reset) buzz::reset_geometry_command_bindings(*ctx, plan);
    else buzz::bind_geometry_command_attributes(*ctx, plan);
    SPU_RET(ctx);
}

extern "C" void buzz_geometry_job_emit_position(spu_context* ctx, buzz_geometry_job_fn reference,
                                               const buzz_geometry_command_functions* functions)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_emit_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryPositionCommands plan;
    const bool supported = buzz::read_geometry_position_commands(*ctx, plan);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_POSITION_COMMANDS, supported, 0);
    if (!supported) { reference(ctx); return; }
    buzz::emit_geometry_position_commands(*ctx, plan, *functions);
    SPU_RET(ctx);
}

extern "C" void buzz_geometry_job_emit_full(spu_context* ctx, buzz_geometry_job_fn reference,
                                           const buzz_geometry_command_functions* functions)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_full_commands_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryFullCommands plan;
    const bool supported = buzz::read_geometry_full_commands(*ctx, plan);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_FULL_COMMANDS, supported, 0);
    if (!supported) { reference(ctx); return; }
    buzz::emit_geometry_full_commands(*ctx, plan, *functions);
    SPU_RET(ctx);
}

extern "C" void buzz_geometry_job_decode_attributes(spu_context* ctx, buzz_geometry_job_fn reference,
                                                     const buzz_geometry_attribute_functions* functions)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_attributes_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryAttributes plan;
    const bool supported = functions && functions->stream && functions->stride && buzz::read_geometry_attributes(*ctx, plan);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_ATTRIBUTES, supported, 0);
    if (!supported) { reference(ctx); return; }
    buzz::decode_geometry_attributes(*ctx, plan, *functions);
    SPU_RET(ctx);
}

extern "C" void buzz_geometry_job_decode_stream(spu_context* ctx, buzz_geometry_job_fn reference)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_attributes_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryRawStream plan;
    const bool supported = buzz::read_geometry_raw_stream(*ctx, plan);
    // Non-raw descriptors deliberately retain the original decoder. They are
    // reported as fallback here even when the enclosing AC68 control is native.
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_RAW_STREAM, supported, 0);
    if (!supported) { reference(ctx); return; }
    buzz::decode_geometry_raw_stream(*ctx, plan);
    SPU_RET(ctx);
}

extern "C" void buzz_geometry_job_decode_color(spu_context* ctx, buzz_geometry_job_fn reference,
                                               buzz_geometry_job_fn continuation)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_codecs_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryColorStream plan;
    const bool supported = continuation && buzz::read_geometry_color(*ctx,plan);
    if (!supported) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_COLOR_CODEC,0,0);
        reference(ctx); return;
    }
    spu_context* comparison = nullptr;
    if (buzz_geometry_job_mode() == 2) {
        // The codec is DMA-free. Compare its full region exit as well as the
        // enclosing job, before common descriptor control can overwrite ABI.
        comparison = buzz::clone_function_state(ctx);
        auto scheduled = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr;
        reference(comparison);
        unsigned steps = 0;
        while (comparison->pc != 0x83c8) {
            if (!g_spu_trampoline_fn || ++steps > 65536)
                buzz_geometry_job_native_fail(ctx,"color reference continuation escaped region");
            auto next = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; next(comparison);
        }
        if (g_spu_trampoline_fn != continuation)
            buzz_geometry_job_native_fail(ctx,"color reference continuation differs");
        g_spu_trampoline_fn = scheduled;
    }
    buzz::decode_geometry_color(*ctx,plan);
    // This codec is a region inside 7DF8, not a callable SPU root. Schedule
    // its exact original continuation; changing r0/host depth would be wrong.
    ctx->pc = 0x83c8; g_spu_trampoline_fn = continuation;
    if (comparison) {
        const auto diff = buzz::compare_function_state(*ctx,*comparison);
        const bool controls = ctx->host_depth == comparison->host_depth && ctx->image_id == comparison->image_id;
        buzz::release_function_state(comparison);
        if (!diff.exact() || !controls) {
            std::fprintf(stderr,"[geometry-job/color-codec] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"color codec strict state/continuation differs");
        }
        buzz_geometry_job_color_checked_result();
    }
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_COLOR_CODEC,1,0);
}

extern "C" void buzz_geometry_job_decode_unit_vectors(spu_context* ctx, buzz_geometry_job_fn reference,
                                               buzz_geometry_job_fn continuation)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_unit_vectors_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryUnitStream plan;
    const char* reason=continuation?nullptr:"null continuation";
    const bool supported = continuation && buzz::read_geometry_unit_vectors(*ctx,plan,&reason);
    if (!supported) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_UNIT_VECTORS,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/unit-vectors] first fallback: %s\n",reason);
        reference(ctx); return;
    }
    spu_context* comparison = nullptr;
    if (buzz_geometry_job_mode() == 2) {
        // The codec is DMA-free. Compare its full region exit as well as the
        // enclosing job, before common descriptor control can overwrite ABI.
        comparison = buzz::clone_function_state(ctx);
        auto scheduled = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr;
        reference(comparison);
        unsigned steps = 0;
        while (comparison->pc != 0x83c8) {
            if (!g_spu_trampoline_fn || ++steps > 65536)
                buzz_geometry_job_native_fail(ctx,"unit-vector reference continuation escaped region");
            auto next = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; next(comparison);
        }
        if (g_spu_trampoline_fn != continuation)
            buzz_geometry_job_native_fail(ctx,"unit-vector reference continuation differs");
        g_spu_trampoline_fn = scheduled;
    }
    buzz::decode_geometry_unit_vectors(*ctx,plan);
    // This codec is a region inside 7DF8, not a callable SPU root. Schedule
    // its exact original continuation; changing r0/host depth would be wrong.
    ctx->pc = 0x83c8; g_spu_trampoline_fn = continuation;
    if (comparison) {
        const auto diff = buzz::compare_function_state(*ctx,*comparison);
        const bool controls = ctx->host_depth == comparison->host_depth && ctx->image_id == comparison->image_id;
        buzz::release_function_state(comparison);
        if (!diff.exact() || !controls) {
            std::fprintf(stderr,"[geometry-job/unit-vectors] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"unit-vector codec strict state/continuation differs");
        }
        buzz_geometry_job_unit_vectors_checked_result();
    }
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_UNIT_VECTORS,1,0);
}

extern "C" void buzz_geometry_job_decode_half_stream(spu_context* ctx, buzz_geometry_job_fn reference,
                                               buzz_geometry_job_fn continuation)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_half_stream_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryHalfStream plan;
    const char* reason=continuation?nullptr:"null continuation";
    const bool supported = continuation && buzz::read_geometry_half_stream(*ctx,plan,&reason);
    if (!supported) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_HALF_STREAM,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/half-stream] first fallback: %s\n",reason);
        reference(ctx); return;
    }
    spu_context* comparison = nullptr;
    if (buzz_geometry_job_mode() == 2) {
        // The codec is DMA-free. Compare its full region exit as well as the
        // enclosing job, before common descriptor control can overwrite ABI.
        comparison = buzz::clone_function_state(ctx);
        auto scheduled = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr;
        reference(comparison);
        unsigned steps = 0;
        while (comparison->pc != 0x83c8) {
            if (!g_spu_trampoline_fn || ++steps > 65536)
                buzz_geometry_job_native_fail(ctx,"half-stream reference continuation escaped region");
            auto next = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; next(comparison);
        }
        if (g_spu_trampoline_fn != continuation)
            buzz_geometry_job_native_fail(ctx,"half-stream reference continuation differs");
        g_spu_trampoline_fn = scheduled;
    }
    buzz::decode_geometry_half_stream(*ctx,plan);
    // This codec is a region inside 7DF8, not a callable SPU root. Schedule
    // its exact original continuation; changing r0/host depth would be wrong.
    ctx->pc = 0x83c8; g_spu_trampoline_fn = continuation;
    if (comparison) {
        const auto diff = buzz::compare_function_state(*ctx,*comparison);
        const bool controls = ctx->host_depth == comparison->host_depth && ctx->image_id == comparison->image_id;
        buzz::release_function_state(comparison);
        if (!diff.exact() || !controls) {
            std::fprintf(stderr,"[geometry-job/half-stream] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"half-stream codec strict state/continuation differs");
        }
        buzz_geometry_job_half_stream_checked_result();
    }
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_HALF_STREAM,1,0);
}

extern "C" void buzz_geometry_job_decode_float32_stream(spu_context* ctx, buzz_geometry_job_fn reference,
                                               buzz_geometry_job_fn continuation)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_float32_stream_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryFloat32Stream plan;
    const char* reason=continuation?nullptr:"null continuation";
    const bool supported = continuation && buzz::read_geometry_float32_stream(*ctx,plan,&reason);
    if (!supported) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_FLOAT32_STREAM,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/float32-stream] first fallback: %s\n",reason);
        reference(ctx); return;
    }
    spu_context* comparison = nullptr;
    if (buzz_geometry_job_mode() == 2 || buzz_geometry_job_float32_stream_enabled() == 2) {
        // The codec is DMA-free. Compare its full region exit as well as the
        // enclosing job, before common descriptor control can overwrite ABI.
        comparison = buzz::clone_function_state(ctx);
        auto scheduled = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr;
        reference(comparison);
        unsigned steps = 0;
        while (comparison->pc != 0x83c8) {
            if (!g_spu_trampoline_fn || ++steps > 65536)
                buzz_geometry_job_native_fail(ctx,"float32-stream reference continuation escaped region");
            auto next = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; next(comparison);
        }
        if (g_spu_trampoline_fn != continuation)
            buzz_geometry_job_native_fail(ctx,"float32-stream reference continuation differs");
        g_spu_trampoline_fn = scheduled;
    }
    buzz::decode_geometry_float32_stream(*ctx,plan);
    // This codec is a region inside 7DF8, not a callable SPU root. Schedule
    // its exact original continuation; changing r0/host depth would be wrong.
    ctx->pc = 0x83c8; g_spu_trampoline_fn = continuation;
    if (comparison) {
        const auto diff = buzz::compare_function_state(*ctx,*comparison);
        const bool controls = ctx->host_depth == comparison->host_depth && ctx->image_id == comparison->image_id;
        buzz::release_function_state(comparison);
        if (!diff.exact() || !controls) {
            std::fprintf(stderr,"[geometry-job/float32-stream] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"float32-stream codec strict state/continuation differs");
        }
        buzz_geometry_job_float32_stream_checked_result();
    }
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_FLOAT32_STREAM,1,0);
}

extern "C" void buzz_geometry_job_decode_fixed_stream(spu_context* ctx, buzz_geometry_job_fn reference,
                                               buzz_geometry_job_fn continuation)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_fixed_stream_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryFixedStream plan;
    const char* reason=continuation?nullptr:"null continuation";
    const bool supported = continuation && buzz::read_geometry_fixed_stream(*ctx,plan,&reason);
    if (!supported) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_FIXED_STREAM,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/fixed-stream] first fallback: %s\n",reason);
        reference(ctx); return;
    }
    spu_context* comparison = nullptr;
    if (buzz_geometry_job_mode() == 2 || buzz_geometry_job_fixed_stream_enabled() == 2) {
        // The codec is DMA-free. Compare its full region exit as well as the
        // enclosing job, before common descriptor control can overwrite ABI.
        comparison = buzz::clone_function_state(ctx);
        auto scheduled = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr;
        reference(comparison);
        unsigned steps = 0;
        while (comparison->pc != 0x83c8) {
            if (!g_spu_trampoline_fn || ++steps > 65536)
                buzz_geometry_job_native_fail(ctx,"fixed-stream reference continuation escaped region");
            auto next = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; next(comparison);
        }
        if (g_spu_trampoline_fn != continuation)
            buzz_geometry_job_native_fail(ctx,"fixed-stream reference continuation differs");
        g_spu_trampoline_fn = scheduled;
    }
    buzz::decode_geometry_fixed_stream(*ctx,plan);
    // This codec is a region inside 7DF8, not a callable SPU root. Schedule
    // its exact original continuation; changing r0/host depth would be wrong.
    ctx->pc = 0x83c8; g_spu_trampoline_fn = continuation;
    if (comparison) {
        const auto diff = buzz::compare_function_state(*ctx,*comparison);
        const bool controls = ctx->host_depth == comparison->host_depth && ctx->image_id == comparison->image_id;
        buzz::release_function_state(comparison);
        if (!diff.exact() || !controls) {
            std::fprintf(stderr,"[geometry-job/fixed-stream] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"fixed-stream codec strict state/continuation differs");
        }
        buzz_geometry_job_fixed_stream_checked_result();
    }
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_FIXED_STREAM,1,0);
}

extern "C" void buzz_geometry_job_decode_snorm_stream(spu_context* ctx, buzz_geometry_job_fn reference,
                                               buzz_geometry_job_fn continuation)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_snorm_stream_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometrySnormStream plan;
    const char* reason=continuation?nullptr:"null continuation";
    const bool supported = continuation && buzz::read_geometry_snorm_stream(*ctx,plan,&reason);
    if (!supported) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SNORM_STREAM,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/snorm-stream] first fallback: %s\n",reason);
        reference(ctx); return;
    }
    spu_context* comparison = nullptr;
    if (buzz_geometry_job_mode() == 2 || buzz_geometry_job_snorm_stream_enabled() == 2) {
        // The codec is DMA-free. Compare its full region exit as well as the
        // enclosing job, before common descriptor control can overwrite ABI.
        comparison = buzz::clone_function_state(ctx);
        auto scheduled = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr;
        reference(comparison);
        unsigned steps = 0;
        while (comparison->pc != 0x83c8) {
            if (!g_spu_trampoline_fn || ++steps > 65536)
                buzz_geometry_job_native_fail(ctx,"snorm-stream reference continuation escaped region");
            auto next = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; next(comparison);
        }
        if (g_spu_trampoline_fn != continuation)
            buzz_geometry_job_native_fail(ctx,"snorm-stream reference continuation differs");
        g_spu_trampoline_fn = scheduled;
    }
    buzz::decode_geometry_snorm_stream(*ctx,plan);
    // This codec is a region inside 7DF8, not a callable SPU root. Schedule
    // its exact original continuation; changing r0/host depth would be wrong.
    ctx->pc = 0x83c8; g_spu_trampoline_fn = continuation;
    if (comparison) {
        const auto diff = buzz::compare_function_state(*ctx,*comparison);
        const bool controls = ctx->host_depth == comparison->host_depth && ctx->image_id == comparison->image_id;
        buzz::release_function_state(comparison);
        if (!diff.exact() || !controls) {
            std::fprintf(stderr,"[geometry-job/snorm-stream] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"snorm-stream codec strict state/continuation differs");
        }
        buzz_geometry_job_snorm_stream_checked_result();
    }
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SNORM_STREAM,1,0);
}

extern "C" void buzz_geometry_job_decode_i16_stream(spu_context* ctx, buzz_geometry_job_fn reference,
                                               buzz_geometry_job_fn continuation)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_i16_stream_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryI16Stream plan;
    const char* reason=continuation?nullptr:"null continuation";
    const bool supported = continuation && buzz::read_geometry_i16_stream(*ctx,plan,&reason);
    if (!supported) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_I16_STREAM,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/i16-stream] first fallback: %s\n",reason);
        reference(ctx); return;
    }
    spu_context* comparison = nullptr;
    if (buzz_geometry_job_mode() == 2 || buzz_geometry_job_i16_stream_enabled() == 2) {
        // The codec is DMA-free. Compare its full region exit as well as the
        // enclosing job, before common descriptor control can overwrite ABI.
        comparison = buzz::clone_function_state(ctx);
        auto scheduled = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr;
        reference(comparison);
        unsigned steps = 0;
        while (comparison->pc != 0x83c8) {
            if (!g_spu_trampoline_fn || ++steps > 65536)
                buzz_geometry_job_native_fail(ctx,"i16-stream reference continuation escaped region");
            auto next = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; next(comparison);
        }
        if (g_spu_trampoline_fn != continuation)
            buzz_geometry_job_native_fail(ctx,"i16-stream reference continuation differs");
        g_spu_trampoline_fn = scheduled;
    }
    buzz::decode_geometry_i16_stream(*ctx,plan);
    // This codec is a region inside 7DF8, not a callable SPU root. Schedule
    // its exact original continuation; changing r0/host depth would be wrong.
    ctx->pc = 0x83c8; g_spu_trampoline_fn = continuation;
    if (comparison) {
        const auto diff = buzz::compare_function_state(*ctx,*comparison);
        const bool controls = ctx->host_depth == comparison->host_depth && ctx->image_id == comparison->image_id;
        buzz::release_function_state(comparison);
        if (!diff.exact() || !controls) {
            std::fprintf(stderr,"[geometry-job/i16-stream] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"i16-stream codec strict state/continuation differs");
        }
        buzz_geometry_job_i16_stream_checked_result();
    }
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_I16_STREAM,1,0);
}

extern "C" void buzz_geometry_job_decode_cmp_stream(spu_context* ctx, buzz_geometry_job_fn reference,
                                               buzz_geometry_job_fn continuation)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_cmp_stream_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryCmpStream plan;
    const char* reason=continuation?nullptr:"null continuation";
    const bool supported = continuation && buzz::read_geometry_cmp_stream(*ctx,plan,&reason);
    if (!supported) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_CMP_STREAM,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/cmp-stream] first fallback: %s\n",reason);
        reference(ctx); return;
    }
    spu_context* comparison = nullptr;
    if (buzz_geometry_job_mode() == 2 || buzz_geometry_job_cmp_stream_enabled() == 2) {
        // The codec is DMA-free. Compare its full region exit as well as the
        // enclosing job, before common descriptor control can overwrite ABI.
        comparison = buzz::clone_function_state(ctx);
        auto scheduled = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr;
        reference(comparison);
        unsigned steps = 0;
        while (comparison->pc != 0x83c8) {
            if (!g_spu_trampoline_fn || ++steps > 65536)
                buzz_geometry_job_native_fail(ctx,"cmp-stream reference continuation escaped region");
            auto next = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; next(comparison);
        }
        if (g_spu_trampoline_fn != continuation)
            buzz_geometry_job_native_fail(ctx,"cmp-stream reference continuation differs");
        g_spu_trampoline_fn = scheduled;
    }
    buzz::decode_geometry_cmp_stream(*ctx,plan);
    // This codec is a region inside 7DF8, not a callable SPU root. Schedule
    // its exact original continuation; changing r0/host depth would be wrong.
    ctx->pc = 0x83c8; g_spu_trampoline_fn = continuation;
    if (comparison) {
        const auto diff = buzz::compare_function_state(*ctx,*comparison);
        const bool controls = ctx->host_depth == comparison->host_depth && ctx->image_id == comparison->image_id;
        buzz::release_function_state(comparison);
        if (!diff.exact() || !controls) {
            std::fprintf(stderr,"[geometry-job/cmp-stream] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"cmp-stream codec strict state/continuation differs");
        }
        buzz_geometry_job_cmp_stream_checked_result();
    }
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_CMP_STREAM,1,0);
}

extern "C" void buzz_geometry_job_decode_u8_integer_stream(spu_context* ctx, buzz_geometry_job_fn reference,
                                               buzz_geometry_job_fn continuation)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_u8_integer_stream_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryU8IntegerStream plan;
    const char* reason=continuation?nullptr:"null continuation";
    const bool supported = continuation && buzz::read_geometry_u8_integer_stream(*ctx,plan,&reason);
    if (!supported) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_U8_INTEGER_STREAM,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/u8-integer-stream] first fallback: %s\n",reason);
        reference(ctx); return;
    }
    spu_context* comparison = nullptr;
    if (buzz_geometry_job_mode() == 2 || buzz_geometry_job_u8_integer_stream_enabled() == 2) {
        // The codec is DMA-free. Compare its full region exit as well as the
        // enclosing job, before common descriptor control can overwrite ABI.
        comparison = buzz::clone_function_state(ctx);
        auto scheduled = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr;
        reference(comparison);
        unsigned steps = 0;
        while (comparison->pc != 0x83c8) {
            if (!g_spu_trampoline_fn || ++steps > 65536)
                buzz_geometry_job_native_fail(ctx,"u8-integer-stream reference continuation escaped region");
            auto next = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; next(comparison);
        }
        if (g_spu_trampoline_fn != continuation)
            buzz_geometry_job_native_fail(ctx,"u8-integer-stream reference continuation differs");
        g_spu_trampoline_fn = scheduled;
    }
    buzz::decode_geometry_u8_integer_stream(*ctx,plan);
    // This codec is a region inside 7DF8, not a callable SPU root. Schedule
    // its exact original continuation; changing r0/host depth would be wrong.
    ctx->pc = 0x83c8; g_spu_trampoline_fn = continuation;
    if (comparison) {
        const auto diff = buzz::compare_function_state(*ctx,*comparison);
        const bool controls = ctx->host_depth == comparison->host_depth && ctx->image_id == comparison->image_id;
        buzz::release_function_state(comparison);
        if (!diff.exact() || !controls) {
            std::fprintf(stderr,"[geometry-job/u8-integer-stream] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"u8-integer-stream codec strict state/continuation differs");
        }
        buzz_geometry_job_u8_integer_stream_checked_result();
    }
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_U8_INTEGER_STREAM,1,0);
}

extern "C" void buzz_geometry_job_blend_scatter(spu_context* ctx,buzz_geometry_job_fn reference,buzz_geometry_job_fn tail)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_blend_scatter_enabled()){
        reference(ctx);return;
    }
    buzz::GeometryBlendScatter plan;const char* reason=tail?nullptr:"null tail";
    if(!tail || !buzz::read_geometry_blend_scatter(*ctx,plan,&reason)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_BLEND_SCATTER,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/blend-scatter] first fallback: %s\n",reason);
        reference(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected_next=nullptr;
    if(buzz_geometry_job_mode()==2 || buzz_geometry_job_blend_scatter_enabled()==2){
        // Pure LS/FMA root: run the two original blocks on a private clone.
        // Drain by tail identity, even if r0 itself equals the tail PC.
        comparison=buzz::clone_function_state(ctx);
        auto scheduled=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;reference(comparison);
        unsigned steps=0;
        while(g_spu_trampoline_fn==tail){
            if(comparison->pc!=0xc7a8 || ++steps>65536)
                buzz_geometry_job_native_fail(ctx,"blend-scatter reference tail escaped");
            auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected_next=comparison->host_depth?nullptr:spu_indirect_branch;
        if(comparison->pc!=comparison->gpr[0]._u32[0] || g_spu_trampoline_fn!=expected_next)
            buzz_geometry_job_native_fail(ctx,"blend-scatter reference root return differs");
        g_spu_trampoline_fn=scheduled;
    }
    buzz::blend_geometry_scatter(*ctx,plan);
    ctx->pc=ctx->gpr[0]._u32[0];g_spu_trampoline_fn=ctx->host_depth?nullptr:spu_indirect_branch;
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool controls=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id && g_spu_trampoline_fn==expected_next;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !controls){
            std::fprintf(stderr,"[geometry-job/blend-scatter] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"blend-scatter strict root state/return differs");
        }
        buzz_geometry_job_blend_scatter_checked_result();
    }
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_BLEND_SCATTER,1,0);
}

extern "C" void buzz_geometry_job_decode_descriptors(spu_context* ctx,buzz_geometry_job_fn reference,
                                                      const buzz_geometry_descriptor_functions* functions)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_descriptors_enabled()){
        reference(ctx);return;
    }
    buzz::GeometryDescriptorStream plan;
    bool supported=functions && functions->blend && functions->continuation;
    if(supported)for(auto fn:functions->codec)if(!fn)supported=false;
    supported=supported && buzz::read_geometry_descriptor_stream(*ctx,plan);
    if(!supported){buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_DESCRIPTORS,0,0);reference(ctx);return;}
    spu_context* comparison=nullptr;
    if(buzz_geometry_job_mode()==2){
        comparison=buzz::clone_function_state(ctx);auto scheduled=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;
        const uint32_t link=comparison->gpr[0]._u32[0];reference(comparison);spu_drain_call(comparison,link);
        if(g_spu_trampoline_fn)buzz_geometry_job_native_fail(ctx,"descriptor reference continuation remains pending");
        g_spu_trampoline_fn=scheduled;
    }
    buzz::decode_geometry_descriptor_stream(*ctx,plan,*functions);
    ctx->pc=ctx->gpr[0]._u32[0];g_spu_trampoline_fn=nullptr;
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool controls=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !controls){
            std::fprintf(stderr,"[geometry-job/descriptor-control] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"descriptor control strict state/return differs");
        }
        buzz_geometry_job_descriptor_checked_result();
    }
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_DESCRIPTORS,1,0);
    SPU_RET(ctx);
}

extern "C" int buzz_geometry_job_native_eligible(const spu_context* ctx)
{
    buzz::GeometryJobSetup setup;
    return buzz::read_geometry_job_setup(*ctx, setup);
}

extern "C" void buzz_geometry_job_native_start(spu_context* ctx, const buzz_geometry_job_functions* functions)
{
    buzz::GeometryJobSetup setup;
    buzz::GeometryContextState state;
    const char* reason = nullptr;
    if (!buzz::prepare_geometry_job(*ctx, setup, &reason)) buzz_geometry_job_native_fail(ctx, reason);
    buzz::materialize_geometry_job_initial_call(*ctx, setup);
    if (!buzz::initialize_geometry_context(*ctx, setup.initialization, state, &reason))
        buzz_geometry_job_native_fail(ctx, reason);
    NativePipelineScope scope(ctx);
    buzz::GeometryPipeline pipeline(*ctx, *functions, setup);
    pipeline.run();
    SPU_RET(ctx);
}
