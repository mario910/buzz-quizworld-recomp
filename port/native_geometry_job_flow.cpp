/* Native outer job flow, compiled with the reviewed SPU integer/RTZ flags.
 * Keep this separate from the baseline controller's pre-main constructors. */
#define SPU_LS_DEBUG_HOOKS 0
#include "native_geometry_job.h"
#include "native_geometry_job_call.h"
#include "native_geometry_pipeline.h"
#include "native_geometry_commands.h"

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
