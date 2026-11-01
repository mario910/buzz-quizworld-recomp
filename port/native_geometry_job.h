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
typedef struct buzz_geometry_attribute_functions {
    buzz_geometry_job_fn stream, stride;
} buzz_geometry_attribute_functions;
typedef struct buzz_geometry_blend_list_functions {
    buzz_geometry_job_fn unpack, attributes, normal_pointer, normalize, tail;
} buzz_geometry_blend_list_functions;

typedef struct buzz_geometry_skin_dispatch_functions {
    buzz_geometry_job_fn position_pointer, normal_pointer, tangent_pointer, transform, normalize, weighted;
} buzz_geometry_skin_dispatch_functions;

typedef struct buzz_geometry_skin_flow_functions {
    buzz_geometry_job_fn position_pointer, normal_pointer, tangent_pointer, aux_pointer, publish_palette;
    buzz_geometry_job_fn weighted, rigid, directions[6], direction_entry, batch_entry;
} buzz_geometry_skin_flow_functions;
typedef struct buzz_geometry_descriptor_functions {
    buzz_geometry_job_fn codec[9], blend, continuation;
    void (*dispatched)(unsigned storage);
} buzz_geometry_descriptor_functions;
int buzz_geometry_job_audit_enabled(void);
int buzz_geometry_job_mode(void);
int buzz_geometry_job_publish_enabled(void);
void buzz_geometry_job_publish_result(int native);
enum { BUZZ_GEOMETRY_JOB_OUTPUT_SIZE = 0, BUZZ_GEOMETRY_JOB_VERTEX_PUBLISH = 1,
       BUZZ_GEOMETRY_JOB_COMMAND_BUFFER = 2, BUZZ_GEOMETRY_JOB_COMMAND_PUBLISH = 3,
       BUZZ_GEOMETRY_JOB_COMMAND_BIND = 4, BUZZ_GEOMETRY_JOB_COMMAND_RESET = 5,
       BUZZ_GEOMETRY_JOB_POSITION_COMMANDS = 6, BUZZ_GEOMETRY_JOB_FULL_COMMANDS = 7,
       BUZZ_GEOMETRY_JOB_ATTRIBUTES = 8, BUZZ_GEOMETRY_JOB_RAW_STREAM = 9,
       BUZZ_GEOMETRY_JOB_COLOR_CODEC = 10, BUZZ_GEOMETRY_JOB_DESCRIPTORS = 11, BUZZ_GEOMETRY_JOB_UNIT_VECTORS = 12, BUZZ_GEOMETRY_JOB_HALF_STREAM = 13, BUZZ_GEOMETRY_JOB_FLOAT32_STREAM = 14, BUZZ_GEOMETRY_JOB_FIXED_STREAM = 15, BUZZ_GEOMETRY_JOB_SNORM_STREAM = 16, BUZZ_GEOMETRY_JOB_I16_STREAM = 17, BUZZ_GEOMETRY_JOB_CMP_STREAM = 18, BUZZ_GEOMETRY_JOB_U8_INTEGER_STREAM = 19, BUZZ_GEOMETRY_JOB_BLEND_SCATTER = 20, BUZZ_GEOMETRY_JOB_BLEND_LISTS = 21, BUZZ_GEOMETRY_JOB_RUN_LISTS = 22, BUZZ_GEOMETRY_JOB_CUSTOM_STREAM = 23, BUZZ_GEOMETRY_JOB_SKIN_DISPATCH = 24, BUZZ_GEOMETRY_JOB_WEIGHTED_SKIN = 25, BUZZ_GEOMETRY_JOB_SKIN_DIRECTIONS = 26, BUZZ_GEOMETRY_JOB_SKIN_FLOW = 27, BUZZ_GEOMETRY_JOB_RIGID_SKIN = 28, BUZZ_GEOMETRY_JOB_SKIN_COFACTOR = 29, BUZZ_GEOMETRY_JOB_SKIN_NORMALIZED = 30 };
enum { BUZZ_GEOMETRY_JOB_INDEX_DELTAS = 31 };
enum { BUZZ_GEOMETRY_JOB_INDEX_UNPACK = 32 };
enum { BUZZ_GEOMETRY_JOB_INDEX_EXPAND = 33 };
enum { BUZZ_GEOMETRY_JOB_INDEX_RELOCATE = 34 };
enum { BUZZ_GEOMETRY_JOB_INDEX_TRIANGLES = 35 };
typedef struct buzz_geometry_index_functions {
    buzz_geometry_job_fn unpack,deltas,expansion_setup,expand,relocate,triangles,publish;
} buzz_geometry_index_functions;
enum { BUZZ_GEOMETRY_JOB_INDEX_CONTROL = 36 };
enum { BUZZ_GEOMETRY_JOB_TRANSFORM_FINISH = 37 };
enum { BUZZ_GEOMETRY_JOB_VERTEX_PREPARE = 38 };
typedef struct buzz_geometry_transform_functions {
    buzz_geometry_job_fn vertex,cull,alternate,finish,restore;
} buzz_geometry_transform_functions;
enum { BUZZ_GEOMETRY_JOB_TRANSFORM_CONTROL = 39 };
enum { BUZZ_GEOMETRY_JOB_ALTERNATE_CULL = 40 };
int buzz_geometry_job_alternate_cull_enabled(void);
void buzz_geometry_job_alternate_cull_result(unsigned iterations,int checked);
void buzz_geometry_job_alternate_cull(spu_context*,buzz_geometry_job_fn reference,buzz_geometry_job_fn finish);
int buzz_geometry_job_transform_control_enabled(void);
void buzz_geometry_job_transform_control_result(unsigned path,int checked);
void buzz_geometry_job_transform_control(spu_context*,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,const buzz_geometry_transform_functions*);
void buzz_geometry_job_transform_reference_cull(spu_context*,buzz_geometry_job_fn root,buzz_geometry_job_fn boundary,buzz_geometry_job_fn replacement);

int buzz_geometry_job_vertex_prepare_enabled(void);
void buzz_geometry_job_vertex_prepare_result(unsigned path,int checked);
void buzz_geometry_job_vertex_prepare(spu_context*,buzz_geometry_job_fn reference,buzz_geometry_job_fn vertex);
int buzz_geometry_job_transform_finish_enabled(void);
void buzz_geometry_job_transform_finish_result(int publish,int checked);
void buzz_geometry_job_transform_finish(spu_context*,buzz_geometry_job_fn reference);
int buzz_geometry_job_index_control_enabled(void);
void buzz_geometry_job_index_control_result(unsigned path, int checked);
void buzz_geometry_job_index_control(spu_context*, buzz_geometry_job_fn fallback, buzz_geometry_job_fn reference, const buzz_geometry_index_functions*);
int buzz_geometry_job_index_triangles_enabled(void);
void buzz_geometry_job_index_triangles_result(unsigned iterations, int checked);
void buzz_geometry_job_index_triangles(spu_context*, buzz_geometry_job_fn fallback, buzz_geometry_job_fn reference, buzz_geometry_job_fn publish);
int buzz_geometry_job_index_relocate_enabled(void);
void buzz_geometry_job_index_relocate_result(unsigned iterations, int checked);
void buzz_geometry_job_index_relocate(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn triangles);
int buzz_geometry_job_index_expand_enabled(void);
void buzz_geometry_job_index_expand_result(unsigned iterations, int checked, int capacity_error);
void buzz_geometry_job_index_expand(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn commands, buzz_geometry_job_fn capacity_error);
int buzz_geometry_job_index_unpack_enabled(void);
void buzz_geometry_job_index_unpack_result(unsigned width, int checked);
void buzz_geometry_job_index_unpack(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn deltas, buzz_geometry_job_fn expansion);
int buzz_geometry_job_index_deltas_enabled(void);
void buzz_geometry_job_index_deltas_result(unsigned groups, int checked, int aliases);
void buzz_geometry_job_index_deltas(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn tail);
int buzz_geometry_job_skin_normalized_enabled(void);
void buzz_geometry_job_skin_normalized_result(unsigned streams, int checked, int aliases);
void buzz_geometry_job_skin_normalized(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn tail, unsigned streams);
int buzz_geometry_job_skin_cofactor_enabled(void);
void buzz_geometry_job_skin_cofactor_result(unsigned count, int checked, int aliases);
void buzz_geometry_job_skin_cofactor(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn tail);
int buzz_geometry_job_rigid_skin_enabled(void);
void buzz_geometry_job_rigid_skin_result(unsigned count, int checked, int aliases);
void buzz_geometry_job_rigid_skin(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn directions, buzz_geometry_job_fn normals);
int buzz_geometry_job_skin_flow_enabled(void);
void buzz_geometry_job_skin_flow_result(int work, int checked);
void buzz_geometry_job_skin_flow(spu_context*, buzz_geometry_job_fn reference, const buzz_geometry_skin_flow_functions*);
int buzz_geometry_job_skin_directions_enabled(void);
void buzz_geometry_job_skin_directions_result(unsigned streams, int checked, int aliases);
void buzz_geometry_job_skin_directions(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn tail, unsigned streams);
int buzz_geometry_job_weighted_skin_enabled(void);
void buzz_geometry_job_weighted_skin_result(unsigned count, int checked, int palette_alias);
void buzz_geometry_job_weighted_skin(spu_context*, buzz_geometry_job_fn fallback, buzz_geometry_job_fn reference,
                                    buzz_geometry_job_fn directions, buzz_geometry_job_fn normals);
int buzz_geometry_job_skin_dispatch_enabled(void);
void buzz_geometry_job_skin_dispatch_result(int weighted, int checked);
void buzz_geometry_job_skin_dispatch(spu_context*, buzz_geometry_job_fn reference, const buzz_geometry_skin_dispatch_functions*);
int buzz_geometry_job_blend_lists_enabled(void);
void buzz_geometry_job_blend_lists_checked_result(void);
void buzz_geometry_job_blend_lists(spu_context*, buzz_geometry_job_fn reference, const buzz_geometry_blend_list_functions*);
int buzz_geometry_job_run_lists_enabled(void);
int buzz_geometry_job_custom_stream_enabled(void);
void buzz_geometry_job_custom_stream_checked_result(void);
void buzz_geometry_job_decode_custom_stream(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn descriptors);
void buzz_geometry_job_run_lists_native_result(int nonempty);
void buzz_geometry_job_run_lists_checked_result(int nonempty);
void buzz_geometry_job_run_lists(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn tail);
int buzz_geometry_job_layout_enabled(void);
int buzz_geometry_job_vertex_enabled(void);
int buzz_geometry_job_commands_enabled(void);
int buzz_geometry_job_command_publish_enabled(void);
int buzz_geometry_job_emit_enabled(void);
int buzz_geometry_job_full_commands_enabled(void);
int buzz_geometry_job_attributes_enabled(void);
int buzz_geometry_job_codecs_enabled(void);
int buzz_geometry_job_descriptors_enabled(void);
int buzz_geometry_job_unit_vectors_enabled(void);
int buzz_geometry_job_half_stream_enabled(void);
/* Weighted scatter is a complete synchronous root with the original r0 return. */
int buzz_geometry_job_blend_scatter_enabled(void);
void buzz_geometry_job_blend_scatter_checked_result(void);
void buzz_geometry_job_blend_scatter(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn tail);
/* Typed decoders: 0 lift, 1 native, 2 strict region (also with native JOB=1). */
int buzz_geometry_job_i16_stream_enabled(void);
void buzz_geometry_job_i16_stream_checked_result(void);
void buzz_geometry_job_decode_i16_stream(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn continuation);
int buzz_geometry_job_cmp_stream_enabled(void);
void buzz_geometry_job_cmp_stream_checked_result(void);
void buzz_geometry_job_decode_cmp_stream(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn continuation);
int buzz_geometry_job_u8_integer_stream_enabled(void);
void buzz_geometry_job_u8_integer_stream_checked_result(void);
void buzz_geometry_job_decode_u8_integer_stream(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn continuation);
int buzz_geometry_job_snorm_stream_enabled(void);
void buzz_geometry_job_snorm_stream_checked_result(void);
void buzz_geometry_job_decode_snorm_stream(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn continuation);
int buzz_geometry_job_fixed_stream_enabled(void);
void buzz_geometry_job_fixed_stream_checked_result(void);
void buzz_geometry_job_decode_fixed_stream(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn continuation);
int buzz_geometry_job_float32_stream_enabled(void);
void buzz_geometry_job_float32_stream_checked_result(void);
void buzz_geometry_job_decode_float32_stream(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn continuation);
void buzz_geometry_job_half_stream_checked_result(void);
void buzz_geometry_job_decode_half_stream(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn continuation);
void buzz_geometry_job_unit_vectors_checked_result(void);
void buzz_geometry_job_decode_unit_vectors(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn continuation);
void buzz_geometry_job_output_result(unsigned stage, int native, int reused_layout);
void buzz_geometry_job_prepare_commands(spu_context*, buzz_geometry_job_fn reference);
void buzz_geometry_job_publish_commands(spu_context*, buzz_geometry_job_fn reference);
void buzz_geometry_job_command_bindings(spu_context*, buzz_geometry_job_fn reference, int reset);
void buzz_geometry_job_emit_position(spu_context*, buzz_geometry_job_fn reference, const buzz_geometry_command_functions*);
void buzz_geometry_job_emit_full(spu_context*, buzz_geometry_job_fn reference, const buzz_geometry_command_functions*);
void buzz_geometry_job_decode_attributes(spu_context*, buzz_geometry_job_fn reference, const buzz_geometry_attribute_functions*);
void buzz_geometry_job_decode_stream(spu_context*, buzz_geometry_job_fn reference);
void buzz_geometry_job_decode_color(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn continuation);
void buzz_geometry_job_color_checked_result(void);
void buzz_geometry_job_decode_descriptors(spu_context*, buzz_geometry_job_fn reference, const buzz_geometry_descriptor_functions*);
void buzz_geometry_job_descriptor_checked_result(void);
void buzz_geometry_job_descriptor_dispatched(unsigned storage);
void buzz_geometry_job_audit_run(spu_context*, buzz_geometry_job_fn reference);
void buzz_geometry_job_run(spu_context*, buzz_geometry_job_fn reference, buzz_geometry_job_fn native,
                           buzz_geometry_job_eligibility_fn);
int buzz_geometry_job_native_eligible(const spu_context*);
void buzz_geometry_job_native_start(spu_context*, const buzz_geometry_job_functions*);
void buzz_geometry_job_native_fail(spu_context*, const char* reason);
/* Strict synchronous I/O-root checkpoint inside JOB=2's native shadow.
 * Outside a replay shadow begin returns NULL (no comparison was performed).
 * The reference runner must stop exactly at the root's return boundary,
 * including its original return trampoline; it must use audited I/O hooks. */
typedef struct buzz_geometry_io_root_check buzz_geometry_io_root_check;
buzz_geometry_io_root_check* buzz_geometry_job_io_root_begin(spu_context*);
void buzz_geometry_job_io_root_end(spu_context*, buzz_geometry_io_root_check*, buzz_geometry_job_fn reference_runner);
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
