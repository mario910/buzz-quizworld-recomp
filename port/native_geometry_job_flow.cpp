/* Native outer job flow, compiled with the reviewed SPU integer/RTZ flags.
 * Keep this separate from the baseline controller's pre-main constructors. */
#define SPU_LS_DEBUG_HOOKS 0
#include "native_geometry_job.h"
#include "native_geometry_job_call.h"
#include "native_geometry_pipeline.h"

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
    buzz::GeometryPipeline pipeline(*ctx, *functions, setup);
    pipeline.run();
    SPU_RET(ctx);
}
