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
#include <atomic>
#include <cstdio>

namespace {
thread_local spu_context* native_pipeline_context = nullptr;
struct NativePipelineScope {
    spu_context* previous;
    explicit NativePipelineScope(spu_context* ctx) : previous(native_pipeline_context) { native_pipeline_context = ctx; }
    ~NativePipelineScope() { native_pipeline_context = previous; }
};
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
