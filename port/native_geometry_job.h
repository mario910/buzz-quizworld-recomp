/* Port-only image-41 whole-job checks and incremental native flow hooks. */
#pragma once
#include "spu_context.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*buzz_geometry_job_fn)(spu_context*);
typedef int (*buzz_geometry_job_eligibility_fn)(const spu_context*);
typedef struct buzz_geometry_job_functions {
    buzz_geometry_job_fn attributes, blend, skin, indices, transform;
    buzz_geometry_job_fn vertex_bytes, allocate_output, put_indices, emit_vertices, put_vertices;
    buzz_geometry_job_fn commands, position_commands, empty_convert, empty_publish;
} buzz_geometry_job_functions;
typedef struct buzz_geometry_command_functions {
    buzz_geometry_job_fn prepare, bind, reset, publish;
} buzz_geometry_command_functions;
int buzz_geometry_job_audit_enabled(void);
int buzz_geometry_job_mode(void);
int buzz_geometry_job_publish_enabled(void);
void buzz_geometry_job_publish_result(int native);
enum { BUZZ_GEOMETRY_JOB_OUTPUT_SIZE = 0, BUZZ_GEOMETRY_JOB_VERTEX_PUBLISH = 1,
       BUZZ_GEOMETRY_JOB_COMMAND_BUFFER = 2, BUZZ_GEOMETRY_JOB_COMMAND_PUBLISH = 3,
       BUZZ_GEOMETRY_JOB_COMMAND_BIND = 4, BUZZ_GEOMETRY_JOB_COMMAND_RESET = 5,
       BUZZ_GEOMETRY_JOB_POSITION_COMMANDS = 6, BUZZ_GEOMETRY_JOB_FULL_COMMANDS = 7 };
int buzz_geometry_job_layout_enabled(void);
int buzz_geometry_job_vertex_enabled(void);
int buzz_geometry_job_commands_enabled(void);
int buzz_geometry_job_command_publish_enabled(void);
int buzz_geometry_job_emit_enabled(void);
int buzz_geometry_job_full_commands_enabled(void);
void buzz_geometry_job_output_result(unsigned stage, int native, int reused_layout);
void buzz_geometry_job_prepare_commands(spu_context*, buzz_geometry_job_fn reference);
void buzz_geometry_job_publish_commands(spu_context*, buzz_geometry_job_fn reference);
void buzz_geometry_job_command_bindings(spu_context*, buzz_geometry_job_fn reference, int reset);
void buzz_geometry_job_emit_position(spu_context*, buzz_geometry_job_fn reference, const buzz_geometry_command_functions*);
void buzz_geometry_job_emit_full(spu_context*, buzz_geometry_job_fn reference, const buzz_geometry_command_functions*);
void buzz_geometry_job_audit_run(spu_context*, buzz_geometry_job_fn reference);
void buzz_geometry_job_run(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn native,
                           buzz_geometry_job_eligibility_fn);
int buzz_geometry_job_native_eligible(const spu_context*);
void buzz_geometry_job_native_start(spu_context*, const buzz_geometry_job_functions*);
void buzz_geometry_job_native_fail(spu_context*, const char* reason);
void buzz_geometry_job_wrch(spu_context*, uint32_t, u128);
u128 buzz_geometry_job_rdch(spu_context*, uint32_t);
uint32_t buzz_geometry_job_rchcnt(spu_context*, uint32_t);
void buzz_geometry_job_indirect(spu_context*);
void buzz_geometry_job_step(spu_context*, uint32_t);
buzz_geometry_job_fn buzz_geometry_job_indirect_target(spu_context*);
void buzz_geometry_job_observe(spu_context*, uint32_t, uint32_t, uint32_t, uint32_t, const void*);
void buzz_geometry_job_audit_report(void);
int buzz_geometry_job_audit_failures(void);
#ifdef __cplusplus
}
#endif
