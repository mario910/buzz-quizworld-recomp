/* Optional image-40 whole-job entry; qualified Stage F remains the fallback. */
#pragma once
#include "spu_context.h"
#ifdef __cplusplus
extern "C" {
#endif
void buzz_animation_job_run(spu_context*, void (*reference)(spu_context*));
int buzz_animation_job_mode(void);
void buzz_animation_job_report(void);
#ifdef __cplusplus
}
#endif
