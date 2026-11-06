#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_pipeline.h"
#include "../port/native_function_check.h"
#if defined(BUZZ_GEOMETRY_LS_DRIVER)
#include "../port/spu_arm_fp_state.h"
#else
#include "../port/native_geometry_conversion_control.h"
#include "../port/native_geometry_snorm_output.h"
#include "../port/native_geometry_float_output.h"
#include "../port/native_geometry_half_output.h"
#include "../port/native_geometry_unorm_output.h"
#include "../port/native_geometry_i16_output.h"
#include "../port/native_geometry_cmp_output.h"
#include "../port/native_geometry_u8_integer_output.h"
#include "zlib.h"
#include <immintrin.h>
#endif
#include "../ps3recomp/runtime/spu/spu_dma.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct Call {
    unsigned pc, link;
    std::array<u128, 10> arguments{};
    std::array<u128, 31> nonvolatile{};
};
static std::vector<Call> calls;
static unsigned scenario;
static const char* current_case = "synthetic";
static bool emit_enabled = false, full_enabled = false;
static bool attributes_enabled = false;
static int i16_stream_enabled=0;
static unsigned i16_references=0,i16_native=0,i16_fallback=0,i16_checked=0;
static int cmp_stream_enabled=0;
static unsigned cmp_references=0,cmp_native=0,cmp_fallback=0,cmp_checked=0;
static int u8_integer_stream_enabled=0;
static unsigned u8_integer_references=0,u8_integer_native=0,u8_integer_fallback=0,u8_integer_checked=0;
static int blend_scatter_enabled=0;
static unsigned blend_references=0,blend_native=0,blend_fallback=0,blend_checked=0;
static int snorm_stream_enabled=0;
static unsigned snorm_references=0,snorm_native=0,snorm_fallback=0,snorm_checked=0;
static int fixed_stream_enabled=0;
static unsigned fixed_references=0,fixed_native=0,fixed_fallback=0,fixed_checked=0;
static int float32_stream_enabled=0;
static unsigned float32_references=0,float32_native=0,float32_fallback=0,float32_checked=0;
static bool half_stream_enabled=false;
static unsigned half_references=0,half_native=0,half_fallback=0,half_checked=0;
static bool codecs_enabled = false,unit_vectors_enabled=false;
static unsigned unit_references=0,unit_native=0,unit_fallback=0,unit_checked=0;
static unsigned color_references = 0, color_native = 0, color_fallback = 0;
static int flow_mode = 1;
static unsigned color_region_checked = 0;
static bool descriptors_enabled=false;
static unsigned descriptor_references=0,descriptor_native=0,descriptor_fallback=0;
static unsigned attribute_references = 0, attribute_native[2]{}, attribute_fallback[2]{};
static unsigned emit_references = 0, emit_native[4]{}, emit_fallback[4]{};
static bool routing_probe = false, commands_enabled = false, command_publish_enabled = false;
static bool lists_enabled=false;
static int run_lists_mode=0;
static int custom_stream_mode=0;
static int skin_dispatch_mode=0;
static int weighted_skin_mode=0;
static int skin_directions_mode=0;
static int skin_flow_mode=0;
static int rigid_skin_mode=0;
static int skin_cofactor_mode=0;
static int index_expand_mode=0;
static unsigned index_expand_native=0,index_expand_fallback=0,index_expand_checked=0,index_expand_references=0;
static int index_relocate_mode=0;
static unsigned index_relocate_native=0,index_relocate_fallback=0,index_relocate_checked=0,index_relocate_references=0;
static int index_triangles_mode=0;
static unsigned index_triangles_native=0,index_triangles_fallback=0,index_triangles_checked=0,index_triangles_references=0,index_triangles_fallback_calls=0;
static int index_control_mode=0;
static unsigned index_control_native=0,index_control_fallback=0,index_control_checked=0,index_control_references=0,index_control_fallback_calls=0;
static int ring_mode=0;
static int float_output_mode=0;
static int half_output_mode=0;
static int cmp_output_mode=0;
static int snorm_output_mode=0;
static int unorm_output_mode=0;
static int u8_integer_output_mode=0;
static int i16_output_mode=0;
static int formatter_mode=0;
static int formatter_registers_mode=0;
static bool formatter_registers_tests_only=false;
static const char* formatter_registers_corruption=nullptr;
static unsigned formatter_registers_cross[7]{};
static unsigned formatter_registers_roots=0,formatter_registers_parents=0,formatter_registers_children=0,formatter_registers_strict_children=0;
static unsigned formatter_registers_cases=0,formatter_registers_references[7]{},formatter_registers_entries[7]{};
static int conversion_control_mode=0;
static int builtin_output_mode[4]{};
static int input_control_mode=0,builtin_input_mode[5]{};
static unsigned input_control_native=0,input_control_checked=0,input_control_fallback=0,input_control_references=0,input_control_fallback_calls=0;
static unsigned builtin_input_native[5]{},builtin_input_checked[5]{},builtin_input_references[5]{},builtin_input_fallback[5]{};
static unsigned builtin_output_native[4]{},builtin_output_checked[4]{},builtin_output_fallback[4]{},builtin_output_references[4]{},builtin_output_fallback_calls[4]{};
static unsigned builtin_output_current=0;
static const spu_context* builtin_output_unchanged=nullptr;
static buzz_geometry_job_fn builtin_output_queue=nullptr;
static unsigned conversion_control_native=0,conversion_control_checked=0,conversion_control_fallback=0,conversion_control_references=0,conversion_control_fallback_calls=0;
static unsigned formatter_native=0,formatter_checked=0,formatter_fallback=0,formatter_references=0,formatter_fallback_calls=0,formatter_children=0;
static unsigned float_output_native=0,float_output_fallback=0,float_output_checked=0,float_output_references=0,float_output_fallback_calls=0;
static unsigned half_output_native=0,half_output_fallback=0,half_output_checked=0,half_output_references=0,half_output_fallback_calls=0;
static unsigned cmp_output_native=0,cmp_output_fallback=0,cmp_output_checked=0,cmp_output_references=0,cmp_output_fallback_calls=0;
static unsigned snorm_output_native=0,snorm_output_fallback=0,snorm_output_checked=0,snorm_output_references=0,snorm_output_fallback_calls=0;
static unsigned unorm_output_native=0,unorm_output_fallback=0,unorm_output_checked=0,unorm_output_references=0,unorm_output_fallback_calls=0;
static unsigned u8_integer_output_native=0,u8_integer_output_fallback=0,u8_integer_output_checked=0,u8_integer_output_references=0,u8_integer_output_fallback_calls=0;
static unsigned i16_output_native=0,i16_output_fallback=0,i16_output_checked=0,i16_output_references=0,i16_output_fallback_calls=0;
static unsigned ring_native=0,ring_fallback=0,ring_checked=0,ring_references=0;
static int alternate_cull_mode=0;
static unsigned alternate_cull_native=0,alternate_cull_fallback=0,alternate_cull_checked=0,alternate_cull_references=0,alternate_cull_fallback_calls=0;
static int transform_control_mode=0;
static unsigned transform_control_native=0,transform_control_fallback=0,transform_control_checked=0,transform_control_references=0,transform_control_fallback_calls=0;
static int vertex_prepare_mode=0;
static unsigned vertex_prepare_native=0,vertex_prepare_fallback=0,vertex_prepare_checked=0,vertex_prepare_references=0,vertex_prepare_fallback_calls=0;
static int transform_finish_mode=0;
static unsigned transform_finish_native=0,transform_finish_fallback=0,transform_finish_checked=0,transform_finish_references=0,transform_finish_fallback_calls=0;
static int index_unpack_mode=0;
static unsigned index_unpack_native=0,index_unpack_fallback=0,index_unpack_checked=0,index_unpack_references=0;
static int index_deltas_mode=0;
static unsigned index_deltas_native=0,index_deltas_fallback=0,index_deltas_checked=0,index_deltas_references=0;
static int skin_normalized_mode=0;
static unsigned skin_normalized_native=0,skin_normalized_fallback=0,skin_normalized_checked=0,skin_normalized_references=0;
static unsigned skin_cofactor_native=0,skin_cofactor_fallback=0,skin_cofactor_checked=0,skin_cofactor_references=0;
static unsigned rigid_skin_native=0,rigid_skin_fallback=0,rigid_skin_checked=0,rigid_skin_references=0;
static unsigned skin_flow_native=0,skin_flow_fallback=0,skin_flow_checked=0,skin_flow_references=0;
static unsigned skin_directions_native=0,skin_directions_fallback=0,skin_directions_checked=0,skin_directions_references=0;
static unsigned weighted_skin_native=0,weighted_skin_fallback=0,weighted_skin_checked=0,weighted_skin_references=0;
static unsigned skin_dispatch_native=0,skin_dispatch_fallback=0,skin_dispatch_checked=0,skin_dispatch_references=0;
static unsigned custom_stream_native=0,custom_stream_fallback=0,custom_stream_checked=0,custom_stream_references=0;
static unsigned run_list_native=0,run_list_fallback=0,run_list_reference_count=0,run_list_checked=0,run_list_expanded=0,run_list_empty=0;
static unsigned list_native=0,list_fallback=0,list_references=0;
static unsigned publication_references = 0, publication_native = 0, publication_fallback = 0;
static unsigned routed_references = 0, routed_native = 0, routed_fallback = 0;
#if !defined(BUZZ_GEOMETRY_LS_DRIVER)
static void routing_inside(spu_context*);
#endif
static void drain(spu_context* ctx, uint32_t stop)
{
    unsigned steps = 0;
    while (g_spu_trampoline_fn) {
        assert(++steps < 4096);
        if (g_spu_trampoline_fn == spu_indirect_branch && ctx->pc == stop) {
            g_spu_trampoline_fn = nullptr; break;
        }
        auto fn = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; fn(ctx);
    }
    assert(ctx->pc == stop);
}
extern "C" {
#if defined(BUZZ_GEOMETRY_LS_DRIVER)
int buzz_geometry_fallback_reasons_enabled() { return 0; }
void buzz_geometry_job_fallback_reason(const char*) { abort(); }
#endif
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*) { abort(); }
void spu_drain_call(spu_context* ctx, uint32_t link) { drain(ctx, link); }
void spu_img_restore(spu_context* ctx, int32_t image) { assert(ctx->image_id == image); }
void spu_stop(spu_context*) { abort(); }
void spu_halt(spu_context*) { abort(); }
void spu_wrch(spu_context*, uint32_t, u128) { abort(); }
u128 spu_rdch(spu_context*, uint32_t) { abort(); }
void buzz_geometry_job_wrch(spu_context*, uint32_t, u128) { abort(); }
u128 buzz_geometry_job_rdch(spu_context*, uint32_t) { abort(); }
int buzz_geometry_job_publish_enabled() { return 0; }
void buzz_geometry_job_publish_result(int) { abort(); }
int buzz_geometry_job_layout_enabled() { return 0; }
int buzz_geometry_job_vertex_enabled() { return 0; }
int buzz_geometry_job_commands_enabled() { return commands_enabled; }
int buzz_geometry_job_full_commands_enabled() { return full_enabled; }
int buzz_geometry_job_attributes_enabled() { return attributes_enabled; }
int buzz_geometry_job_codecs_enabled() { return codecs_enabled; }
int buzz_geometry_job_i16_stream_enabled(){return i16_stream_enabled;}
void buzz_geometry_job_i16_stream_checked_result(){++i16_checked;}
int buzz_geometry_job_cmp_stream_enabled(){return cmp_stream_enabled;}
void buzz_geometry_job_cmp_stream_checked_result(){++cmp_checked;}
int buzz_geometry_job_u8_integer_stream_enabled(){return u8_integer_stream_enabled;}
void buzz_geometry_job_u8_integer_stream_checked_result(){++u8_integer_checked;}
int buzz_geometry_job_blend_scatter_enabled(){return blend_scatter_enabled;}
void buzz_geometry_job_blend_scatter_checked_result(){++blend_checked;}
int buzz_geometry_job_blend_lists_enabled(){return lists_enabled;}
int buzz_geometry_job_run_lists_enabled(){return run_lists_mode;}
int buzz_geometry_job_index_expand_enabled(){return index_expand_mode;}
void buzz_geometry_job_index_expand_result(unsigned,int checked,int){if(checked)++index_expand_checked;}
int buzz_geometry_job_index_relocate_enabled(){return index_relocate_mode;}
void buzz_geometry_job_index_relocate_result(unsigned,int checked){if(checked)++index_relocate_checked;}
int buzz_geometry_job_index_triangles_enabled(){return index_triangles_mode;}
void buzz_geometry_job_index_triangles_result(unsigned,int checked){if(checked)++index_triangles_checked;}
int buzz_geometry_job_index_control_enabled(){return index_control_mode;}
void buzz_geometry_job_index_control_result(unsigned,int checked){if(checked)++index_control_checked;}
int buzz_geometry_job_ring_enabled(){return ring_mode;}
int buzz_geometry_job_float_output_enabled(){return float_output_mode;}
int buzz_geometry_job_half_output_enabled(){return half_output_mode;}
int buzz_geometry_job_cmp_output_enabled(){return cmp_output_mode;}
int buzz_geometry_job_snorm_output_enabled(){return snorm_output_mode;}
int buzz_geometry_job_unorm_output_enabled(){return unorm_output_mode;}
int buzz_geometry_job_u8_integer_output_enabled(){return u8_integer_output_mode;}
int buzz_geometry_job_i16_output_enabled(){return i16_output_mode;}
int buzz_geometry_job_formatter_enabled(){return formatter_mode;}
int buzz_geometry_job_formatter_spans_enabled(){return 1;}
int buzz_geometry_job_formatter_registers_enabled(){return formatter_registers_mode;}
int buzz_geometry_host_output_enabled(){return 0;}
void buzz_geometry_host_output_result(){}
void buzz_geometry_job_formatter_registers_result(unsigned children,unsigned strict_children,int checked){
    assert(strict_children<=children);++formatter_registers_roots;
    formatter_registers_parents+=!!checked;formatter_registers_children+=children;formatter_registers_strict_children+=strict_children;
}
int buzz_geometry_job_conversion_control_enabled(){return conversion_control_mode;}
int buzz_geometry_job_input_control_enabled(){return input_control_mode;}
int buzz_geometry_job_builtin_input_enabled(unsigned format){assert(format>=1 && format<=5);return builtin_input_mode[format-1];}
void buzz_geometry_job_input_control_result(unsigned path,int checked){assert(path<7);if(checked)++input_control_checked;}
void buzz_geometry_job_builtin_input_result(unsigned format,int checked){assert(format>=1 && format<=5);if(checked)++builtin_input_checked[format-1];}
int buzz_geometry_job_builtin_output_enabled(unsigned format){assert(format<4);return builtin_output_mode[format];}
void buzz_geometry_job_builtin_output_result(unsigned format,int checked){assert(format<4);if(checked)++builtin_output_checked[format];}
void buzz_geometry_job_conversion_control_result(unsigned,int checked){if(checked)++conversion_control_checked;}
void buzz_geometry_job_formatter_result(unsigned,unsigned,unsigned,unsigned,int checked){if(checked)++formatter_checked;}
void buzz_geometry_job_float_output_result(unsigned,unsigned cross,int checked){if(checked)++float_output_checked;else if(formatter_registers_tests_only && formatter_registers_mode)formatter_registers_cross[1]+=cross;}
void buzz_geometry_job_half_output_result(unsigned,unsigned cross,int checked){if(checked)++half_output_checked;else if(formatter_registers_tests_only && formatter_registers_mode)formatter_registers_cross[2]+=cross;}
void buzz_geometry_job_cmp_output_result(unsigned,unsigned cross,int checked){if(checked)++cmp_output_checked;else if(formatter_registers_tests_only && formatter_registers_mode)formatter_registers_cross[5]+=cross;}
void buzz_geometry_job_snorm_output_result(unsigned,unsigned cross,int checked){if(checked)++snorm_output_checked;else if(formatter_registers_tests_only && formatter_registers_mode)formatter_registers_cross[0]+=cross;}
void buzz_geometry_job_unorm_output_result(unsigned,unsigned cross,int checked){if(checked)++unorm_output_checked;else if(formatter_registers_tests_only && formatter_registers_mode)formatter_registers_cross[3]+=cross;}
void buzz_geometry_job_u8_integer_output_result(unsigned,unsigned cross,int checked){if(checked)++u8_integer_output_checked;else if(formatter_registers_tests_only && formatter_registers_mode)formatter_registers_cross[6]+=cross;}
void buzz_geometry_job_i16_output_result(unsigned,unsigned cross,int checked){if(checked)++i16_output_checked;else if(formatter_registers_tests_only && formatter_registers_mode)formatter_registers_cross[4]+=cross;}
void buzz_geometry_job_ring_result(unsigned frontier,int checked){assert(!frontier);if(checked)++ring_checked;}
int buzz_geometry_job_alternate_cull_enabled(){return alternate_cull_mode;}
void buzz_geometry_job_alternate_cull_result(unsigned,int checked){if(checked)++alternate_cull_checked;}
int buzz_geometry_job_transform_control_enabled(){return transform_control_mode;}
void buzz_geometry_job_transform_control_result(unsigned,int checked){if(checked)++transform_control_checked;}
int buzz_geometry_job_vertex_prepare_enabled(){return vertex_prepare_mode;}
void buzz_geometry_job_vertex_prepare_result(unsigned,int checked){if(checked)++vertex_prepare_checked;}
int buzz_geometry_job_transform_finish_enabled(){return transform_finish_mode;}
void buzz_geometry_job_transform_finish_result(int,int checked){if(checked)++transform_finish_checked;}
int buzz_geometry_job_index_unpack_enabled(){return index_unpack_mode;}
void buzz_geometry_job_index_unpack_result(unsigned,int checked){if(checked)++index_unpack_checked;}
int buzz_geometry_job_index_deltas_enabled(){return index_deltas_mode;}
void buzz_geometry_job_index_deltas_result(unsigned,int checked,int){if(checked)++index_deltas_checked;}
int buzz_geometry_job_skin_normalized_enabled(){return skin_normalized_mode;}
void buzz_geometry_job_skin_normalized_result(unsigned,int checked,int){if(checked)++skin_normalized_checked;}
int buzz_geometry_job_skin_cofactor_enabled(){return skin_cofactor_mode;}
void buzz_geometry_job_skin_cofactor_result(unsigned,int checked,int){if(checked)++skin_cofactor_checked;}
int buzz_geometry_job_rigid_skin_enabled(){return rigid_skin_mode;}
void buzz_geometry_job_rigid_skin_result(unsigned,int checked,int){if(checked)++rigid_skin_checked;}
int buzz_geometry_job_skin_flow_enabled(){return skin_flow_mode;}
void buzz_geometry_job_skin_flow_result(int,int checked){if(checked)++skin_flow_checked;}
int buzz_geometry_job_skin_directions_enabled(){return skin_directions_mode;}
void buzz_geometry_job_skin_directions_result(unsigned,int checked,int){if(checked)++skin_directions_checked;}
int buzz_geometry_job_weighted_skin_enabled(){return weighted_skin_mode;}
void buzz_geometry_job_weighted_skin_result(unsigned,int checked,int){if(checked)++weighted_skin_checked;}
int buzz_geometry_job_skin_dispatch_enabled(){return skin_dispatch_mode;}
void buzz_geometry_job_skin_dispatch_result(int,int checked){if(checked)++skin_dispatch_checked;}
int buzz_geometry_job_custom_stream_enabled(){return custom_stream_mode;}
void buzz_geometry_job_custom_stream_checked_result(){++custom_stream_checked;}
void buzz_geometry_job_run_lists_checked_result(int){++run_list_checked;}
void buzz_geometry_job_run_lists_native_result(int nonempty){if(nonempty)++run_list_expanded;else ++run_list_empty;}
void buzz_geometry_job_blend_lists_checked_result(){abort();}
buzz_geometry_io_root_check* buzz_geometry_job_io_root_begin(spu_context*){return nullptr;}
void buzz_geometry_job_io_root_end(spu_context*,buzz_geometry_io_root_check* check,buzz_geometry_job_fn){assert(!check);}
int buzz_geometry_job_snorm_stream_enabled(){return snorm_stream_enabled;}
void buzz_geometry_job_snorm_stream_checked_result(){++snorm_checked;}
int buzz_geometry_job_fixed_stream_enabled(){return fixed_stream_enabled;}
void buzz_geometry_job_fixed_stream_checked_result(){++fixed_checked;}
int buzz_geometry_job_float32_stream_enabled(){return float32_stream_enabled;}
void buzz_geometry_job_float32_stream_checked_result(){++float32_checked;}
int buzz_geometry_job_half_stream_enabled(){return half_stream_enabled;}
void buzz_geometry_job_half_stream_checked_result(){++half_checked;}
int buzz_geometry_job_unit_vectors_enabled(){return unit_vectors_enabled;}
void buzz_geometry_job_unit_vectors_checked_result(){++unit_checked;}
int buzz_geometry_job_descriptors_enabled() { return descriptors_enabled; }
void buzz_geometry_job_descriptor_checked_result() { abort(); }
void buzz_geometry_job_descriptor_dispatched(unsigned) { abort(); }
int buzz_geometry_job_mode() { return flow_mode; }
int buzz_geometry_job_debug_stages() { return 1; } // Legacy stage-debug fixture.
int buzz_geometry_job_owns_admission(const spu_context*) { return 0; } // direct fixture calls have no controller scope
void buzz_geometry_job_color_checked_result() { ++color_region_checked; }
int buzz_geometry_job_emit_enabled() { return emit_enabled; }
int buzz_geometry_job_command_publish_enabled() { return command_publish_enabled; }
void buzz_geometry_job_output_result(unsigned stage, int native, int) {
    if(stage==BUZZ_GEOMETRY_JOB_INPUT_CONTROL){assert(routing_probe);if(native)++input_control_native;else ++input_control_fallback;return;}
    if(stage>=BUZZ_GEOMETRY_JOB_BUILTIN20_INPUT && stage<=BUZZ_GEOMETRY_JOB_BUILTIN15_INPUT){assert(routing_probe);const auto format=stage-BUZZ_GEOMETRY_JOB_BUILTIN20_INPUT;if(native)++builtin_input_native[format];else ++builtin_input_fallback[format];return;}
    if(stage>=BUZZ_GEOMETRY_JOB_BUILTIN_FLOAT3_OUTPUT && stage<=BUZZ_GEOMETRY_JOB_BUILTIN24_CMP_OUTPUT){assert(routing_probe);const auto f=stage-BUZZ_GEOMETRY_JOB_BUILTIN_FLOAT3_OUTPUT;if(native)++builtin_output_native[f];else ++builtin_output_fallback[f];return;}
    if(stage==BUZZ_GEOMETRY_JOB_CONVERSION_CONTROL){assert(routing_probe);if(native)++conversion_control_native;else ++conversion_control_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_FORMATTER){assert(routing_probe);if(native)++formatter_native;else ++formatter_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_RING){assert(routing_probe);if(native)++ring_native;else ++ring_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_FLOAT_OUTPUT){assert(routing_probe);if(native)++float_output_native;else ++float_output_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_HALF_OUTPUT){assert(routing_probe);if(native)++half_output_native;else ++half_output_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_CMP_OUTPUT){assert(routing_probe);if(native)++cmp_output_native;else ++cmp_output_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_SNORM_OUTPUT){assert(routing_probe);if(native)++snorm_output_native;else ++snorm_output_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_UNORM_OUTPUT){assert(routing_probe);if(native)++unorm_output_native;else ++unorm_output_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_U8_INTEGER_OUTPUT){assert(routing_probe);if(native)++u8_integer_output_native;else ++u8_integer_output_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_I16_OUTPUT){assert(routing_probe);if(native)++i16_output_native;else ++i16_output_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_ALTERNATE_CULL){assert(routing_probe);if(native)++alternate_cull_native;else ++alternate_cull_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_TRANSFORM_CONTROL){assert(routing_probe);if(native)++transform_control_native;else ++transform_control_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_VERTEX_PREPARE){assert(routing_probe);if(native)++vertex_prepare_native;else ++vertex_prepare_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_TRANSFORM_FINISH){assert(routing_probe);if(native)++transform_finish_native;else ++transform_finish_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_INDEX_CONTROL){assert(routing_probe);if(native)++index_control_native;else ++index_control_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_INDEX_TRIANGLES){assert(routing_probe);if(native)++index_triangles_native;else ++index_triangles_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_INDEX_RELOCATE){assert(routing_probe);if(native)++index_relocate_native;else ++index_relocate_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_INDEX_EXPAND){assert(routing_probe);if(native)++index_expand_native;else ++index_expand_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_INDEX_UNPACK){assert(routing_probe);if(native)++index_unpack_native;else ++index_unpack_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_INDEX_DELTAS){assert(routing_probe);if(native)++index_deltas_native;else ++index_deltas_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_SKIN_NORMALIZED){assert(routing_probe);if(native)++skin_normalized_native;else ++skin_normalized_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_SKIN_COFACTOR){assert(routing_probe);if(native)++skin_cofactor_native;else ++skin_cofactor_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_RIGID_SKIN){assert(routing_probe);if(native)++rigid_skin_native;else ++rigid_skin_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_SKIN_FLOW){assert(routing_probe);if(native)++skin_flow_native;else ++skin_flow_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_SKIN_DIRECTIONS){assert(routing_probe);if(native)++skin_directions_native;else ++skin_directions_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_WEIGHTED_SKIN){assert(routing_probe);if(native)++weighted_skin_native;else ++weighted_skin_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_SKIN_DISPATCH){assert(routing_probe);if(native)++skin_dispatch_native;else ++skin_dispatch_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_CUSTOM_STREAM){assert(routing_probe);if(native)++custom_stream_native;else ++custom_stream_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_RUN_LISTS){assert(routing_probe);if(native)++run_list_native;else ++run_list_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_BLEND_LISTS){assert(routing_probe);if(native)++list_native;else ++list_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_I16_STREAM){assert(routing_probe);if(native)++i16_native;else ++i16_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_CMP_STREAM){assert(routing_probe);if(native)++cmp_native;else ++cmp_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_U8_INTEGER_STREAM){assert(routing_probe);if(native)++u8_integer_native;else ++u8_integer_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_BLEND_SCATTER){assert(routing_probe);if(native)++blend_native;else ++blend_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_SNORM_STREAM){assert(routing_probe);if(native)++snorm_native;else ++snorm_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_FIXED_STREAM){assert(routing_probe);if(native)++fixed_native;else ++fixed_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_FLOAT32_STREAM){assert(routing_probe);if(native)++float32_native;else ++float32_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_HALF_STREAM){assert(routing_probe);if(native)++half_native;else ++half_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_UNIT_VECTORS){assert(routing_probe);if(native)++unit_native;else ++unit_fallback;return;}
    if(stage==BUZZ_GEOMETRY_JOB_DESCRIPTORS){
        assert(routing_probe);if(native)++descriptor_native;else ++descriptor_fallback;return;
    }
    if (stage == BUZZ_GEOMETRY_JOB_COLOR_CODEC) {
        assert(routing_probe);
        if (native) ++color_native; else ++color_fallback;
        return;
    }
    if (stage >= BUZZ_GEOMETRY_JOB_ATTRIBUTES) {
        assert(routing_probe && stage <= BUZZ_GEOMETRY_JOB_RAW_STREAM);
        if (native) ++attribute_native[stage - BUZZ_GEOMETRY_JOB_ATTRIBUTES];
        else ++attribute_fallback[stage - BUZZ_GEOMETRY_JOB_ATTRIBUTES];
        return;
    }
    if (stage >= BUZZ_GEOMETRY_JOB_COMMAND_BIND) {
        assert(routing_probe && stage <= BUZZ_GEOMETRY_JOB_FULL_COMMANDS);
        if (native) ++emit_native[stage - BUZZ_GEOMETRY_JOB_COMMAND_BIND];
        else ++emit_fallback[stage - BUZZ_GEOMETRY_JOB_COMMAND_BIND];
        return;
    }
    if (stage == BUZZ_GEOMETRY_JOB_COMMAND_PUBLISH) {
        assert(routing_probe);
        if (native) ++publication_native; else ++publication_fallback;
        return;
    }
    assert(routing_probe && stage == BUZZ_GEOMETRY_JOB_COMMAND_BUFFER);
    if (native) ++routed_native; else ++routed_fallback;
}
void buzz_geometry_job_native_fail(spu_context*, const char* reason) {
    fprintf(stderr, "%s: native rejection: %s\n", current_case, reason); abort();
}
}

static unsigned arguments_of(unsigned pc)
{
    switch (pc) {
    case 0xac68: case 0x37c0: case 0x32c8: case 0x27a0: return 7;
    case 0xbf8: return 8;
    case 0x9f8: return 10;
    case 0xaea0: case 0x6608: case 0x2b80: return 4;
    case 0x2dc0: return 6;
    case 0x54e8: return 3;
    case 0x35f0: return 5;
    case 0x12f0: case 0x2e8: return 12;
    default: abort();
    }
}
static void model_call(spu_context* ctx, unsigned pc)
{
    assert(ctx->host_depth == 1);
#if !defined(BUZZ_GEOMETRY_LS_DRIVER)
    if (routing_probe && pc == 0xac68) routing_inside(ctx);
#endif
    Call record{}; record.pc = pc; record.link = ctx->gpr[0]._u32[0];
    uint32_t hash = pc ^ uint32_t(calls.size() * 0x173b);
    for (unsigned reg = 3; reg <= arguments_of(pc); ++reg) {
        record.arguments[reg - 3] = ctx->gpr[reg];
        for (auto word : ctx->gpr[reg]._u32) hash = (hash ^ word) * 0x1000193u;
    }
    for (unsigned i = 0; i < 30; ++i) record.nonvolatile[i] = ctx->gpr[80 + i];
    record.nonvolatile[30] = ctx->gpr[126];
    calls.push_back(record);
    // A legal callee may spill these complete vectors. Exercise that contract
    // separately from argument observation and real-library corpus replay.
    for (unsigned i = 0; i < 30; ++i)
        spu_ls_write128(ctx, ctx->gpr[1]._u32[0] - 0x200 - i * 16, ctx->gpr[80 + i]);
    const unsigned frame = ctx->gpr[1]._u32[0];
    const u128 value = spu_ilhu(uint16_t(hash));
    if (pc == 0x2dc0) spu_ls_write128(ctx, frame + 0x50, value);
    if (pc == 0x35f0) spu_ls_write128(ctx, frame + 0x40, value);
    if (pc == 0x37c0) spu_ls_write128(ctx, frame + 0x90, value);
    if (pc == 0x32c8) spu_ls_write128(ctx, frame + 0x60, value);
    for (unsigned reg = 2; reg < 80; ++reg)
        for (unsigned lane = 0; lane < 4; ++lane) ctx->gpr[reg]._u32[lane] = hash ^ (reg * 0x11701u + lane * 0x137u);
    if (pc == 0x6608) ctx->gpr[3]._u32[0] = scenario % 3 ? 3 + (scenario % 97) * 3 : 0;
    if (pc == 0x2b80) ctx->gpr[3]._u32[0] = 16 * (1 + scenario % 59);
    if (pc == 0x37c0) ctx->gpr[3]._u32[0] = scenario & 1 ? 1 : 0x100;
    ctx->pc = record.link;
}
#include "../analysis/native-geometry/phase3/test-driver-reference.inc"
#if !defined(BUZZ_GEOMETRY_LS_DRIVER)
#include "../analysis/native-geometry/phase3/test-half-stream-reference.inc"
#define BUZZ_GEOMETRY_TEST_CONTINUATION_83C8_DEFINED 1
#include "../analysis/native-geometry/phase3/test-float32-stream-reference.inc"
#include "../analysis/native-geometry/phase3/test-fixed-stream-reference.inc"
#include "../analysis/native-geometry/phase3/test-snorm-stream-reference.inc"
#include "../analysis/native-geometry/phase3/test-blend-scatter-reference.inc"
#include "../analysis/native-geometry/phase3/test-u8-integer-stream-reference.inc"
#include "../analysis/native-geometry/phase3/test-cmp-stream-reference.inc"
#include "../analysis/native-geometry/phase3/test-i16-stream-reference.inc"
#include "../analysis/native-geometry/phase3/test-run-lists-reference.inc"
#include "../analysis/native-geometry/phase3/test-custom-stream-reference.inc"
static unsigned skin_dispatch_secondary=0;
static void skin_dispatch_model(spu_context* ctx,uint32_t pc)
{
    if(pc==0x2750)ctx->gpr[3]=spu_ila(0x18000);
    if(pc==0x2778)ctx->gpr[3]=spu_ila(skin_dispatch_secondary?0x19000:0);
    if(pc==0x2780)ctx->gpr[3]=spu_ila(skin_dispatch_secondary==2?0x1a000:0);
    ctx->pc=ctx->gpr[0]._u32[0];
}
#include "../analysis/native-geometry/phase3/test-skin-dispatch-reference.inc"
#include "../analysis/native-geometry/phase3/test-weighted-skin-reference.inc"
#include "../analysis/native-geometry/phase3/test-rigid-skin-reference.inc"
#include "../analysis/native-geometry/phase3/test-skin-cofactor-reference.inc"
#include "../analysis/native-geometry/phase3/test-skin-normalized-reference.inc"
#include "../analysis/native-geometry/phase3/test-index-deltas-reference.inc"
#include "../analysis/native-geometry/phase3/test-index-unpack-reference.inc"
#include "../analysis/native-geometry/phase3/test-index-expand-reference.inc"
#include "../analysis/native-geometry/phase3/test-index-relocate-reference.inc"
#include "../analysis/native-geometry/phase3/test-index-triangles-reference.inc"
#include "../analysis/native-geometry/phase3/test-index-control-reference.inc"
#include "../analysis/native-geometry/phase3/test-transform-finish-reference.inc"
#include "../analysis/native-geometry/phase3/test-vertex-prepare-reference.inc"
#include "../analysis/native-geometry/phase3/test-alternate-cull-reference.inc"
#include "../analysis/native-geometry/phase3/test-ring-reference.inc"
#include "../analysis/native-geometry/phase3/formatter-original.inc"
static void formatter_reference(spu_context* c){++formatter_references;buzz_formatter_original_run(c);}
static const spu_context* formatter_unchanged=nullptr;
static buzz_geometry_job_fn formatter_unchanged_queue=nullptr;
static void formatter_fallback_call(spu_context* c){
    if(formatter_unchanged)assert(!memcmp(c,formatter_unchanged,sizeof(*c)) && g_spu_trampoline_fn==formatter_unchanged_queue);
    ++formatter_fallback_calls;
}
static void formatter_data(spu_context* c,unsigned kind){
    const auto finish=buzz_formatter_original_00004610,skip=buzz_formatter_original_00004618;const char* why=nullptr;
    switch(kind){
    case 0:{buzz::GeometrySnormOutput p;assert(buzz::read_geometry_snorm_output(*c,p,&why));buzz::emit_geometry_snorm_output(*c,p,finish,skip);break;}
    case 1:{buzz::GeometryFloatOutput p;assert(buzz::read_geometry_float_output(*c,p,&why));buzz::emit_geometry_float_output(*c,p,finish,skip);break;}
    case 2:{buzz::GeometryHalfOutput p;assert(buzz::read_geometry_half_output(*c,p,&why));buzz::emit_geometry_half_output(*c,p,finish,skip);break;}
    case 3:{buzz::GeometryUnormOutput p;assert(buzz::read_geometry_unorm_output(*c,p,&why));buzz::emit_geometry_unorm_output(*c,p,finish,skip);break;}
    case 4:{buzz::GeometryI16Output p;assert(buzz::read_geometry_i16_output(*c,p,&why));buzz::emit_geometry_i16_output(*c,p,finish,skip);break;}
    case 5:{buzz::GeometryCmpOutput p;assert(buzz::read_geometry_cmp_output(*c,p,&why));buzz::emit_geometry_cmp_output(*c,p,finish,skip);break;}
    case 6:{buzz::GeometryU8IntegerOutput p;assert(buzz::read_geometry_u8_integer_output(*c,p,&why));buzz::emit_geometry_u8_integer_output(*c,p,finish,skip);break;}
    }++formatter_children;
}
static void formatter_d0(spu_context* c){formatter_data(c,0);}static void formatter_d1(spu_context* c){formatter_data(c,1);}
static void formatter_d2(spu_context* c){formatter_data(c,2);}static void formatter_d3(spu_context* c){formatter_data(c,3);}
static void formatter_d4(spu_context* c){formatter_data(c,4);}static void formatter_d5(spu_context* c){formatter_data(c,5);}
static void formatter_d6(spu_context* c){formatter_data(c,6);}
static const buzz_geometry_formatter_functions formatter_library{{formatter_d0,formatter_d1,formatter_d2,formatter_d3,formatter_d4,formatter_d5,formatter_d6},buzz_formatter_original_00004610,buzz_formatter_original_00004618};
#include "../analysis/native-geometry/phase3/test-i16-output-reference.inc"
static void i16_output_reference(spu_context* c){++i16_output_references;i16_output_oracle::buzz_job_6eba_spu_func_00004DD0(c);}
static void i16_output_fallback_call(spu_context* c){++i16_output_fallback_calls;c->pc=c->gpr[0]._u32[0];}
#include "../analysis/native-geometry/phase3/test-u8-integer-output-reference.inc"
static void u8_integer_output_reference(spu_context* c){++u8_integer_output_references;u8_integer_output_oracle::buzz_job_6eba_spu_func_000052B4(c);}
static void u8_integer_output_fallback_call(spu_context* c){++u8_integer_output_fallback_calls;c->pc=c->gpr[0]._u32[0];}
#include "../analysis/native-geometry/phase3/test-unorm-output-reference.inc"
static void unorm_output_reference(spu_context* c){++unorm_output_references;unorm_output_oracle::buzz_job_6eba_spu_func_00004B9C(c);}
static void unorm_output_fallback_call(spu_context* c){++unorm_output_fallback_calls;c->pc=c->gpr[0]._u32[0];}
#include "../analysis/native-geometry/phase3/test-snorm-output-reference.inc"
static void snorm_output_reference(spu_context* c){++snorm_output_references;snorm_output_oracle::buzz_job_6eba_spu_func_00004478(c);}
static void snorm_output_fallback_call(spu_context* c){++snorm_output_fallback_calls;c->pc=c->gpr[0]._u32[0];}
#include "../analysis/native-geometry/phase3/test-cmp-output-reference.inc"
static void cmp_output_reference(spu_context* c){++cmp_output_references;cmp_output_oracle::buzz_job_6eba_spu_func_00004FDC(c);}
static void cmp_output_fallback_call(spu_context* c){++cmp_output_fallback_calls;c->pc=c->gpr[0]._u32[0];}
#include "../analysis/native-geometry/phase3/test-half-output-reference.inc"
static void half_output_reference(spu_context* c){++half_output_references;half_output_oracle::buzz_job_6eba_spu_func_00004880(c);}
static void half_output_fallback_call(spu_context* c){++half_output_fallback_calls;c->pc=c->gpr[0]._u32[0];}
#include "../analysis/native-geometry/phase3/test-float-output-reference.inc"
static void float_output_reference(spu_context* c){++float_output_references;float_output_oracle::buzz_job_6eba_spu_func_000046D0(c);}
static void float_output_fallback_call(spu_context* c){++float_output_fallback_calls;c->pc=c->gpr[0]._u32[0];}
static void ring_reference_call(spu_context*){++ring_references;}
static void alternate_cull_reference(spu_context* c){++alternate_cull_references;alternate_cull_oracle::buzz_job_6eba_spu_func_000069E8(c);}
static void alternate_cull_fallback_call(spu_context* c){++alternate_cull_fallback_calls;c->pc=c->gpr[0]._u32[0];}

static void transform_control_test_bridge(spu_context* c,buzz_geometry_job_fn root,buzz_geometry_job_fn boundary,buzz_geometry_job_fn replacement){buzz_geometry_job_transform_reference_cull(c,root,boundary,replacement);}
#include "../analysis/native-geometry/phase3/test-transform-control-reference.inc"
static void transform_control_vertex(spu_context* c){assert(c->pc==0x6790);c->gpr[2]=spu_ila(0x12345);SPU_RET(c);}
static void transform_control_finish(spu_context* c){transform_control_oracle::buzz_job_6eba_spu_func_00006958(c);}
static unsigned transform_control_expected_cull_pc=0;
static void transform_control_cull(spu_context* c){
    if(c->pc!=transform_control_expected_cull_pc)fprintf(stderr,"shared callback PC %X expected %X mode %X JOB%d flag%d\n",c->pc,transform_control_expected_cull_pc,c->gpr[6]._u32[0],flow_mode,transform_control_mode);
    assert(c->pc==transform_control_expected_cull_pc);c->gpr[50]=spu_ila(17);u128 link=spu_ila(0x66c8);
    spu_ls_write128(c,c->gpr[1]._u32[0]+480+16,link);c->pc=0x6958;g_spu_trampoline_fn=transform_control_finish;
}
static const buzz_geometry_transform_functions transform_control_library={transform_control_vertex,transform_control_cull,transform_control_cull,transform_control_finish,transform_control_oracle::buzz_job_6eba_spu_func_000066C8};
static void transform_control_fallback_call(spu_context* c){++transform_control_fallback_calls;c->pc=c->gpr[0]._u32[0];}
static void transform_control_reference(spu_context* c){++transform_control_references;transform_control_oracle::children=&transform_control_library;transform_control_oracle::buzz_job_6eba_spu_func_00006608(c);}

#include "../analysis/native-geometry/phase3/test-skin-directions-reference.inc"
static void skin_flow_trace(spu_context*,uint32_t){}
static void skin_flow_model(spu_context*,uint32_t){abort();}
#include "../analysis/native-geometry/phase3/test-skin-flow-reference.inc"
static void index_expand_reference(spu_context* ctx){++index_expand_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void index_relocate_reference(spu_context* ctx){++index_relocate_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void index_triangles_fallback_call(spu_context* ctx){++index_triangles_fallback_calls;ctx->pc=ctx->gpr[0]._u32[0];}
static void index_triangles_reference(spu_context* ctx){++index_triangles_references;index_triangles_oracle::buzz_job_6eba_spu_func_00007A90(ctx);}
static void index_control_fallback_call(spu_context* ctx){++index_control_fallback_calls;ctx->pc=ctx->gpr[0]._u32[0];}
static void index_control_reference(spu_context* ctx){++index_control_references;index_control_oracle::buzz_job_6eba_spu_func_0000AEA0(ctx);}
static void vertex_prepare_fallback_call(spu_context* ctx){++vertex_prepare_fallback_calls;ctx->pc=ctx->gpr[0]._u32[0];}
static void vertex_prepare_reference(spu_context* ctx){++vertex_prepare_references;vertex_prepare_oracle::buzz_job_6eba_spu_func_00006188(ctx);}
static void transform_finish_fallback_call(spu_context* ctx){++transform_finish_fallback_calls;ctx->pc=ctx->gpr[0]._u32[0];}
static void transform_finish_reference(spu_context* ctx){++transform_finish_references;transform_finish_oracle::buzz_job_6eba_spu_func_00006958(ctx);}
static void index_unpack_reference(spu_context* ctx){++index_unpack_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void index_deltas_reference(spu_context* ctx){++index_deltas_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void skin_normalized_reference(spu_context* ctx){++skin_normalized_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void skin_cofactor_reference(spu_context* ctx){++skin_cofactor_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void rigid_skin_reference(spu_context* ctx){++rigid_skin_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void skin_flow_reference(spu_context* ctx){++skin_flow_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void skin_directions_reference(spu_context* ctx){++skin_directions_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void weighted_skin_fallback_call(spu_context* ctx){++weighted_skin_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void weighted_skin_reference(spu_context* ctx){
    weighted_skin_oracle::buzz_job_6eba_spu_func_0000B880(ctx);
    while(g_spu_trampoline_fn!=weighted_skin_oracle::buzz_job_6eba_spu_func_0000B4C0 && g_spu_trampoline_fn!=weighted_skin_oracle::buzz_job_6eba_spu_func_0000BB58){auto fn=g_spu_trampoline_fn;assert(fn);g_spu_trampoline_fn=nullptr;fn(ctx);}
}
static void skin_dispatch_reference(spu_context* ctx){++skin_dispatch_references;ctx->pc=ctx->gpr[0]._u32[0];}


static void routing_reference(spu_context* ctx)
{
    ++routed_references; ctx->gpr[2] = spu_il(-1); ctx->pc = ctx->gpr[0]._u32[0];
}
static void publication_reference(spu_context* ctx)
{
    ++publication_references; ctx->pc = ctx->gpr[0]._u32[0];
}
static void emit_reference(spu_context* ctx) { ++emit_references; ctx->pc = ctx->gpr[0]._u32[0]; }
static void attribute_reference(spu_context* ctx) { ++attribute_references; ctx->pc = ctx->gpr[0]._u32[0]; }
static void color_reference(spu_context* ctx) { ++color_references; ctx->pc = ctx->gpr[0]._u32[0]; }
static void color_zero_reference(spu_context* ctx) { ctx->pc = 0x83c8; g_spu_trampoline_fn = color_reference; }
static void i16_reference(spu_context* ctx){++i16_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void cmp_reference(spu_context* ctx){++cmp_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void u8_integer_reference(spu_context* ctx){++u8_integer_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void blend_reference(spu_context* ctx){++blend_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void list_reference(spu_context* ctx){++list_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void run_list_reference(spu_context* ctx){++run_list_reference_count;ctx->pc=ctx->gpr[0]._u32[0];}
static void custom_stream_reference(spu_context* ctx){++custom_stream_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void snorm_reference(spu_context* ctx){++snorm_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void fixed_reference(spu_context* ctx){++fixed_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void float32_reference(spu_context* ctx){++float32_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void half_reference(spu_context* ctx){++half_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void unit_reference(spu_context* ctx){++unit_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void unit_zero_reference(spu_context* ctx){ctx->pc=0x83c8;g_spu_trampoline_fn=unit_reference;}
static void descriptor_reference(spu_context* ctx){++descriptor_references;ctx->pc=ctx->gpr[0]._u32[0];}
static void prepare_stream_routing(spu_context* ctx)
{
    ctx->gpr[1]._u32[0]=0x3d000;ctx->gpr[107]._u32[0]=1;
    ctx->gpr[25]._u32[0]=16;ctx->gpr[121]._u32[0]=1;ctx->gpr[106]._u32[0]=8;ctx->gpr[42]._u32[0]=0;
    const unsigned half_bases[]={118,117,116,124};
    for(unsigned row=0;row<4;++row){
        ctx->gpr[half_bases[row]]._u32[0]=0x10000+row*0x4000;
        u128 pointer=ctx->gpr[row+4];pointer._u32[0]=ctx->gpr[half_bases[row]]._u32[0]+16;
        spu_ls_write128(ctx,0x3d070-row*16,pointer);
        ctx->gpr[41]._u32[row]=0x24000+row*256;ctx->gpr[80]._u32[row]=0x243f0;
        ctx->gpr[81]._u32[row]=1024;ctx->gpr[82]._u32[row]=0x3d020;ctx->gpr[83]._u32[row]=256;
    }
    const uint32_t half_tables[][4]={{0x03030303,0x03030303,0x03030303,0x03030303},
        {0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f},{0x00010203,0x00010203,0x00010203,0x00010203},
        {0x00018080,0x02038080,0x04058080,0x06078080}};
    for(unsigned i=0;i<4;++i){u128 q;memcpy(&q,half_tables[i],16);spu_ls_write128(ctx,0xd5d0+i*16,q);}
    for(unsigned byte=0;byte<16;++byte){ctx->gpr[112]._u8[byte^3]=uint8_t(byte);ctx->gpr[108]._u8[byte]=3;}
}
static void attribute_routing(spu_context* ctx)
{
    auto backup = std::make_unique<spu_context>(); memcpy(backup.get(), ctx, sizeof(*ctx));
    blend_scatter_enabled=1;ctx->gpr[0]._u32[0]=0xc7a8;
    ctx->gpr[3]._u32[0]=0x30000;ctx->gpr[4]._u32[0]=0x10000;ctx->gpr[5]._u32[0]=0x10080;ctx->gpr[7]._u32[0]=0u-128;
    ctx->gpr[6]=spu_il(-1);ctx->gpr[8]=spu_splat_u32(0x3f000000);memset(ctx->ls+0x30000,0,32);
    auto blend_entry=std::make_unique<spu_context>();memcpy(blend_entry.get(),ctx,sizeof(*ctx));
    buzz_geometry_job_blend_scatter(ctx,blend_reference,buzz_job_6eba_spu_func_0000C7A8);
    assert(blend_native==1 && !blend_references && ctx->pc==ctx->gpr[0]._u32[0]);
    g_spu_trampoline_fn=nullptr;memcpy(ctx,blend_entry.get(),sizeof(*ctx));flow_mode=2;
    buzz_geometry_job_blend_scatter(ctx,buzz_job_6eba_spu_func_0000C750,buzz_job_6eba_spu_func_0000C7A8);
    assert(blend_native==2 && blend_checked==1);
    g_spu_trampoline_fn=nullptr;memcpy(ctx,blend_entry.get(),sizeof(*ctx));flow_mode=1;blend_scatter_enabled=2;
    buzz_geometry_job_blend_scatter(ctx,buzz_job_6eba_spu_func_0000C750,buzz_job_6eba_spu_func_0000C7A8);
    assert(blend_native==3 && blend_checked==2);
    g_spu_trampoline_fn=nullptr;buzz_geometry_job_blend_scatter(ctx,blend_reference,nullptr);
    blend_scatter_enabled=0;buzz_geometry_job_blend_scatter(ctx,blend_reference,buzz_job_6eba_spu_func_0000C7A8);blend_scatter_enabled=1;
    auto blend_foreign=std::make_unique<spu_context>();memcpy(blend_foreign.get(),ctx,sizeof(*ctx));blend_foreign->ls=blend_foreign->ls_store;
    buzz_geometry_job_blend_scatter(blend_foreign.get(),blend_reference,buzz_job_6eba_spu_func_0000C7A8);
    ctx->gpr[7]._u32[0]=0;buzz_geometry_job_blend_scatter(ctx,blend_reference,buzz_job_6eba_spu_func_0000C7A8);
    assert(blend_references==4 && blend_fallback==2 && blend_native==3);
    memcpy(ctx,backup.get(),sizeof(*ctx));
    fixed_stream_enabled=true;prepare_stream_routing(ctx);
    const uint32_t fixed_tables[][4]={{0x80808001,0x80808003,0x80808005,0x80808007},{0x80808000,0x80808002,0x80808004,0x80808006}};
    for(unsigned i=0;i<2;++i){u128 q;memcpy(&q,fixed_tables[i],16);spu_ls_write128(ctx,0xd230+i*16,q);}
    spu_ls_write128(ctx,0x3d0d0,spu_il(0x3b000));spu_ls_write128(ctx,0x3d110,spu_il(0x3a000));
    spu_ls_write128(ctx,0x3d0e0,spu_ila(0x10203));ctx->gpr[84]=spu_il(0);
    for(unsigned i=0;i<4;++i){ctx->ls[0x3a000+i*2]=8;ctx->ls[0x3a001+i*2]=0;}

    auto fixed_entry=std::make_unique<spu_context>();memcpy(fixed_entry.get(),ctx,sizeof(*ctx));
    buzz_geometry_job_decode_fixed_stream(ctx,fixed_reference,buzz_job_6eba_spu_func_000083C8);
    assert(fixed_native==1 && !fixed_references && ctx->pc==0x83c8 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;memcpy(ctx,fixed_entry.get(),sizeof(*ctx));flow_mode=2;
    buzz_geometry_job_decode_fixed_stream(ctx,buzz_job_6eba_spu_func_00009058,buzz_job_6eba_spu_func_000083C8);
    assert(fixed_checked==1 && fixed_native==2 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;flow_mode=1;fixed_stream_enabled=2;
    memcpy(ctx,fixed_entry.get(),sizeof(*ctx));
    buzz_geometry_job_decode_fixed_stream(ctx,buzz_job_6eba_spu_func_00009058,buzz_job_6eba_spu_func_000083C8);
    assert(fixed_checked==2 && fixed_native==3 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;fixed_stream_enabled=1;
    buzz_geometry_job_decode_fixed_stream(ctx,fixed_reference,nullptr);
    fixed_stream_enabled=false;buzz_geometry_job_decode_fixed_stream(ctx,fixed_reference,buzz_job_6eba_spu_func_000083C8);fixed_stream_enabled=true;
    auto fixed_foreign=std::make_unique<spu_context>();memcpy(fixed_foreign.get(),ctx,sizeof(*ctx));fixed_foreign->ls=fixed_foreign->ls_store;
    buzz_geometry_job_decode_fixed_stream(fixed_foreign.get(),fixed_reference,buzz_job_6eba_spu_func_000083C8);
    ctx->gpr[121]._u32[0]=1;ctx->gpr[107]._u32[0]=17;buzz_geometry_job_decode_fixed_stream(ctx,fixed_reference,buzz_job_6eba_spu_func_000083C8);
    assert(fixed_references==4 && fixed_fallback==2 && fixed_native==3);
    memcpy(ctx,backup.get(),sizeof(*ctx));
    i16_stream_enabled=true;prepare_stream_routing(ctx);
    const uint32_t i16_table[]={0x0001ffff,0x0203ffff,0x0405ffff,0x0607ffff};
    u128 i16_q;memcpy(&i16_q,i16_table,16);spu_ls_write128(ctx,0xd910,i16_q);
    auto i16_entry=std::make_unique<spu_context>();memcpy(i16_entry.get(),ctx,sizeof(*ctx));
    buzz_geometry_job_decode_i16_stream(ctx,i16_reference,buzz_job_6eba_spu_func_000083C8);
    assert(i16_native==1 && !i16_references && ctx->pc==0x83c8 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;memcpy(ctx,i16_entry.get(),sizeof(*ctx));flow_mode=2;
    buzz_geometry_job_decode_i16_stream(ctx,buzz_job_6eba_spu_func_00008B94,buzz_job_6eba_spu_func_000083C8);
    assert(i16_checked==1 && i16_native==2 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;flow_mode=1;i16_stream_enabled=2;
    memcpy(ctx,i16_entry.get(),sizeof(*ctx));
    buzz_geometry_job_decode_i16_stream(ctx,buzz_job_6eba_spu_func_00008B94,buzz_job_6eba_spu_func_000083C8);
    assert(i16_checked==2 && i16_native==3 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;i16_stream_enabled=1;
    buzz_geometry_job_decode_i16_stream(ctx,i16_reference,nullptr);
    i16_stream_enabled=false;buzz_geometry_job_decode_i16_stream(ctx,i16_reference,buzz_job_6eba_spu_func_000083C8);i16_stream_enabled=true;
    auto i16_foreign=std::make_unique<spu_context>();memcpy(i16_foreign.get(),ctx,sizeof(*ctx));i16_foreign->ls=i16_foreign->ls_store;
    buzz_geometry_job_decode_i16_stream(i16_foreign.get(),i16_reference,buzz_job_6eba_spu_func_000083C8);
    ctx->gpr[121]._u32[0]=1;ctx->gpr[107]._u32[0]=17;buzz_geometry_job_decode_i16_stream(ctx,i16_reference,buzz_job_6eba_spu_func_000083C8);
    assert(i16_references==4 && i16_fallback==2 && i16_native==3);
    memcpy(ctx,backup.get(),sizeof(*ctx));
    cmp_stream_enabled=true;prepare_stream_routing(ctx);
    const uint32_t cmp_table[]={0x0203ffff,0x0102ffff,0x0001ffff,0xffffffff};
    u128 cmp_q;memcpy(&cmp_q,cmp_table,16);spu_ls_write128(ctx,0xda80,cmp_q);
    auto cmp_entry=std::make_unique<spu_context>();memcpy(cmp_entry.get(),ctx,sizeof(*ctx));
    buzz_geometry_job_decode_cmp_stream(ctx,cmp_reference,buzz_job_6eba_spu_func_000083C8);
    assert(cmp_native==1 && !cmp_references && ctx->pc==0x83c8 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;memcpy(ctx,cmp_entry.get(),sizeof(*ctx));flow_mode=2;
    buzz_geometry_job_decode_cmp_stream(ctx,buzz_job_6eba_spu_func_00008D34,buzz_job_6eba_spu_func_000083C8);
    assert(cmp_checked==1 && cmp_native==2 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;flow_mode=1;cmp_stream_enabled=2;
    memcpy(ctx,cmp_entry.get(),sizeof(*ctx));
    buzz_geometry_job_decode_cmp_stream(ctx,buzz_job_6eba_spu_func_00008D34,buzz_job_6eba_spu_func_000083C8);
    assert(cmp_checked==2 && cmp_native==3 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;cmp_stream_enabled=1;
    buzz_geometry_job_decode_cmp_stream(ctx,cmp_reference,nullptr);
    cmp_stream_enabled=false;buzz_geometry_job_decode_cmp_stream(ctx,cmp_reference,buzz_job_6eba_spu_func_000083C8);cmp_stream_enabled=true;
    auto cmp_foreign=std::make_unique<spu_context>();memcpy(cmp_foreign.get(),ctx,sizeof(*ctx));cmp_foreign->ls=cmp_foreign->ls_store;
    buzz_geometry_job_decode_cmp_stream(cmp_foreign.get(),cmp_reference,buzz_job_6eba_spu_func_000083C8);
    ctx->gpr[121]._u32[0]=1;ctx->gpr[107]._u32[0]=17;buzz_geometry_job_decode_cmp_stream(ctx,cmp_reference,buzz_job_6eba_spu_func_000083C8);
    assert(cmp_references==4 && cmp_fallback==2 && cmp_native==3);
    memcpy(ctx,backup.get(),sizeof(*ctx));
    u8_integer_stream_enabled=true;prepare_stream_routing(ctx);
    const uint32_t u8_integer_table[]={0xff00ffff,0xff01ffff,0xff02ffff,0xff03ffff};
    u128 u8_integer_q;memcpy(&u8_integer_q,u8_integer_table,16);spu_ls_write128(ctx,0xd8c0,u8_integer_q);
    auto u8_integer_entry=std::make_unique<spu_context>();memcpy(u8_integer_entry.get(),ctx,sizeof(*ctx));
    buzz_geometry_job_decode_u8_integer_stream(ctx,u8_integer_reference,buzz_job_6eba_spu_func_000083C8);
    assert(u8_integer_native==1 && !u8_integer_references && ctx->pc==0x83c8 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;memcpy(ctx,u8_integer_entry.get(),sizeof(*ctx));flow_mode=2;
    buzz_geometry_job_decode_u8_integer_stream(ctx,buzz_job_6eba_spu_func_00008EA8,buzz_job_6eba_spu_func_000083C8);
    assert(u8_integer_checked==1 && u8_integer_native==2 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;flow_mode=1;u8_integer_stream_enabled=2;
    memcpy(ctx,u8_integer_entry.get(),sizeof(*ctx));
    buzz_geometry_job_decode_u8_integer_stream(ctx,buzz_job_6eba_spu_func_00008EA8,buzz_job_6eba_spu_func_000083C8);
    assert(u8_integer_checked==2 && u8_integer_native==3 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;u8_integer_stream_enabled=1;
    buzz_geometry_job_decode_u8_integer_stream(ctx,u8_integer_reference,nullptr);
    u8_integer_stream_enabled=false;buzz_geometry_job_decode_u8_integer_stream(ctx,u8_integer_reference,buzz_job_6eba_spu_func_000083C8);u8_integer_stream_enabled=true;
    auto u8_integer_foreign=std::make_unique<spu_context>();memcpy(u8_integer_foreign.get(),ctx,sizeof(*ctx));u8_integer_foreign->ls=u8_integer_foreign->ls_store;
    buzz_geometry_job_decode_u8_integer_stream(u8_integer_foreign.get(),u8_integer_reference,buzz_job_6eba_spu_func_000083C8);
    ctx->gpr[121]._u32[0]=1;ctx->gpr[107]._u32[0]=17;buzz_geometry_job_decode_u8_integer_stream(ctx,u8_integer_reference,buzz_job_6eba_spu_func_000083C8);
    assert(u8_integer_references==4 && u8_integer_fallback==2 && u8_integer_native==3);
    memcpy(ctx,backup.get(),sizeof(*ctx));
    snorm_stream_enabled=true;prepare_stream_routing(ctx);
    const uint32_t snorm_table[]={0x0001ffff,0x0203ffff,0x0405ffff,0x0607ffff};
    u128 snorm_q;memcpy(&snorm_q,snorm_table,16);spu_ls_write128(ctx,0xd910,snorm_q);
    auto snorm_entry=std::make_unique<spu_context>();memcpy(snorm_entry.get(),ctx,sizeof(*ctx));
    buzz_geometry_job_decode_snorm_stream(ctx,snorm_reference,buzz_job_6eba_spu_func_000083C8);
    assert(snorm_native==1 && !snorm_references && ctx->pc==0x83c8 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;memcpy(ctx,snorm_entry.get(),sizeof(*ctx));flow_mode=2;
    buzz_geometry_job_decode_snorm_stream(ctx,buzz_job_6eba_spu_func_000085E8,buzz_job_6eba_spu_func_000083C8);
    assert(snorm_checked==1 && snorm_native==2 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;flow_mode=1;snorm_stream_enabled=2;
    memcpy(ctx,snorm_entry.get(),sizeof(*ctx));
    buzz_geometry_job_decode_snorm_stream(ctx,buzz_job_6eba_spu_func_000085E8,buzz_job_6eba_spu_func_000083C8);
    assert(snorm_checked==2 && snorm_native==3 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;snorm_stream_enabled=1;
    buzz_geometry_job_decode_snorm_stream(ctx,snorm_reference,nullptr);
    snorm_stream_enabled=false;buzz_geometry_job_decode_snorm_stream(ctx,snorm_reference,buzz_job_6eba_spu_func_000083C8);snorm_stream_enabled=true;
    auto snorm_foreign=std::make_unique<spu_context>();memcpy(snorm_foreign.get(),ctx,sizeof(*ctx));snorm_foreign->ls=snorm_foreign->ls_store;
    buzz_geometry_job_decode_snorm_stream(snorm_foreign.get(),snorm_reference,buzz_job_6eba_spu_func_000083C8);
    ctx->gpr[121]._u32[0]=1;ctx->gpr[107]._u32[0]=17;buzz_geometry_job_decode_snorm_stream(ctx,snorm_reference,buzz_job_6eba_spu_func_000083C8);
    assert(snorm_references==4 && snorm_fallback==2 && snorm_native==3);
    memcpy(ctx,backup.get(),sizeof(*ctx));
    float32_stream_enabled=true;prepare_stream_routing(ctx);
    auto float32_entry=std::make_unique<spu_context>();memcpy(float32_entry.get(),ctx,sizeof(*ctx));
    buzz_geometry_job_decode_float32_stream(ctx,float32_reference,buzz_job_6eba_spu_func_000083C8);
    assert(float32_native==1 && !float32_references && ctx->pc==0x83c8 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;memcpy(ctx,float32_entry.get(),sizeof(*ctx));flow_mode=2;
    buzz_geometry_job_decode_float32_stream(ctx,buzz_job_6eba_spu_func_000087CC,buzz_job_6eba_spu_func_000083C8);
    assert(float32_checked==1 && float32_native==2 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;flow_mode=1;float32_stream_enabled=2;
    memcpy(ctx,float32_entry.get(),sizeof(*ctx));
    buzz_geometry_job_decode_float32_stream(ctx,buzz_job_6eba_spu_func_000087CC,buzz_job_6eba_spu_func_000083C8);
    assert(float32_checked==2 && float32_native==3 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;float32_stream_enabled=1;
    buzz_geometry_job_decode_float32_stream(ctx,float32_reference,nullptr);
    float32_stream_enabled=false;buzz_geometry_job_decode_float32_stream(ctx,float32_reference,buzz_job_6eba_spu_func_000083C8);float32_stream_enabled=true;
    auto float32_foreign=std::make_unique<spu_context>();memcpy(float32_foreign.get(),ctx,sizeof(*ctx));float32_foreign->ls=float32_foreign->ls_store;
    buzz_geometry_job_decode_float32_stream(float32_foreign.get(),float32_reference,buzz_job_6eba_spu_func_000083C8);
    ctx->gpr[121]._u32[0]=1;ctx->gpr[107]._u32[0]=17;buzz_geometry_job_decode_float32_stream(ctx,float32_reference,buzz_job_6eba_spu_func_000083C8);
    assert(float32_references==4 && float32_fallback==2 && float32_native==3);
    memcpy(ctx,backup.get(),sizeof(*ctx));
    half_stream_enabled=true;prepare_stream_routing(ctx);
    auto half_entry=std::make_unique<spu_context>();memcpy(half_entry.get(),ctx,sizeof(*ctx));
    buzz_geometry_job_decode_half_stream(ctx,half_reference,buzz_job_6eba_spu_func_000083C8);
    assert(half_native==1 && !half_references && ctx->pc==0x83c8 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;memcpy(ctx,half_entry.get(),sizeof(*ctx));flow_mode=2;
    buzz_geometry_job_decode_half_stream(ctx,buzz_job_6eba_spu_func_00008378,buzz_job_6eba_spu_func_000083C8);
    assert(half_checked==1 && half_native==2 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;flow_mode=1;
    buzz_geometry_job_decode_half_stream(ctx,half_reference,nullptr);
    half_stream_enabled=false;buzz_geometry_job_decode_half_stream(ctx,half_reference,buzz_job_6eba_spu_func_000083C8);half_stream_enabled=true;
    auto half_foreign=std::make_unique<spu_context>();memcpy(half_foreign.get(),ctx,sizeof(*ctx));half_foreign->ls=half_foreign->ls_store;
    buzz_geometry_job_decode_half_stream(half_foreign.get(),half_reference,buzz_job_6eba_spu_func_000083C8);
    ctx->gpr[107]._u32[0]=17;buzz_geometry_job_decode_half_stream(ctx,half_reference,buzz_job_6eba_spu_func_000083C8);
    assert(half_references==4 && half_fallback==2 && half_native==2);
    memcpy(ctx,backup.get(),sizeof(*ctx));
    unit_vectors_enabled=true;ctx->gpr[121]._u32[0]=0;
    buzz_geometry_job_decode_unit_vectors(ctx,unit_reference,unit_reference);
    assert(unit_native==1 && !unit_references && ctx->pc==0x83c8 && g_spu_trampoline_fn==unit_reference);
    g_spu_trampoline_fn=nullptr;flow_mode=2;
    buzz_geometry_job_decode_unit_vectors(ctx,unit_zero_reference,unit_reference);
    assert(unit_checked==1 && unit_native==2 && g_spu_trampoline_fn==unit_reference);
    g_spu_trampoline_fn=nullptr;flow_mode=1;
    buzz_geometry_job_decode_unit_vectors(ctx,unit_reference,nullptr);
    unit_vectors_enabled=false;buzz_geometry_job_decode_unit_vectors(ctx,unit_reference,unit_reference);unit_vectors_enabled=true;
    auto unit_foreign=std::make_unique<spu_context>();memcpy(unit_foreign.get(),ctx,sizeof(*ctx));unit_foreign->ls=unit_foreign->ls_store;
    buzz_geometry_job_decode_unit_vectors(unit_foreign.get(),unit_reference,unit_reference);
    ctx->gpr[121]._u32[0]=1;ctx->gpr[107]._u32[0]=17;
    buzz_geometry_job_decode_unit_vectors(ctx,unit_reference,unit_reference);
    assert(unit_references==4 && unit_fallback==2 && unit_native==2);
    memcpy(ctx,backup.get(),sizeof(*ctx));
    descriptors_enabled=true;ctx->gpr[7]._u32[0]=0;
    buzz_geometry_descriptor_functions descriptor_functions{};
    for(auto& fn:descriptor_functions.codec)fn=descriptor_reference;
    descriptor_functions.blend=descriptor_functions.continuation=descriptor_reference;
    buzz_geometry_job_decode_descriptors(ctx,descriptor_reference,&descriptor_functions);
    assert(descriptor_native==1 && !descriptor_references);
    auto descriptor_foreign=std::make_unique<spu_context>();memcpy(descriptor_foreign.get(),ctx,sizeof(*ctx));descriptor_foreign->ls=descriptor_foreign->ls_store;
    buzz_geometry_job_decode_descriptors(descriptor_foreign.get(),descriptor_reference,&descriptor_functions);
    descriptors_enabled=false;buzz_geometry_job_decode_descriptors(ctx,descriptor_reference,&descriptor_functions);descriptors_enabled=true;
    buzz_geometry_job_decode_descriptors(ctx,descriptor_reference,nullptr);
    const auto retained_blend=descriptor_functions.blend;descriptor_functions.blend=nullptr;
    buzz_geometry_job_decode_descriptors(ctx,descriptor_reference,&descriptor_functions);descriptor_functions.blend=retained_blend;
    ctx->gpr[3]._u32[0]=3;buzz_geometry_job_decode_descriptors(ctx,descriptor_reference,&descriptor_functions);
    assert(descriptor_references==5 && descriptor_native==1 && descriptor_fallback==3);
    memcpy(ctx,backup.get(),sizeof(*ctx));
    codecs_enabled = true; ctx->gpr[121]._u32[0] = 0;
    buzz_geometry_job_decode_color(ctx,color_reference,color_reference);
    assert(color_native == 1 && !color_references && ctx->pc == 0x83c8 && g_spu_trampoline_fn == color_reference);
    g_spu_trampoline_fn = nullptr;
    flow_mode = 2;
    buzz_geometry_job_decode_color(ctx,color_zero_reference,color_reference);
    assert(color_region_checked == 1 && color_native == 2 && g_spu_trampoline_fn == color_reference);
    g_spu_trampoline_fn = nullptr; flow_mode = 1;
    buzz_geometry_job_decode_color(ctx,color_reference,nullptr);
    codecs_enabled = false; buzz_geometry_job_decode_color(ctx,color_reference,color_reference); codecs_enabled = true;
    auto color_foreign = std::make_unique<spu_context>(); memcpy(color_foreign.get(),ctx,sizeof(*ctx)); color_foreign->ls = color_foreign->ls_store;
    buzz_geometry_job_decode_color(color_foreign.get(),color_reference,color_reference);
    ctx->gpr[121]._u32[0] = 1; ctx->gpr[107]._u32[0] = 17;
    buzz_geometry_job_decode_color(ctx,color_reference,color_reference);
    assert(color_references == 4 && color_fallback == 2 && color_native == 2);
    memcpy(ctx,backup.get(),sizeof(*ctx));
    const buzz_geometry_attribute_functions functions{attribute_reference,attribute_reference};
    const uint32_t context = ctx->gpr[3]._u32[0]; memset(ctx->ls + context,0,0x190);
    ctx->gpr[4] = ctx->gpr[5] = ctx->gpr[6] = ctx->gpr[7] = spu_il(0);
    attributes_enabled = true;
    buzz_geometry_job_decode_stream(ctx,attribute_reference);
    assert(attribute_native[1] == 1 && !attribute_references);
    ctx->gpr[3] = spu_ila(context);
    buzz_geometry_job_decode_attributes(ctx,attribute_reference,&functions);
    assert(attribute_native[0] == 1 && attribute_references == 1);
    ctx->gpr[3] = spu_ila(context);
    auto foreign = std::make_unique<spu_context>(); memcpy(foreign.get(),ctx,sizeof(*ctx)); foreign->ls = foreign->ls_store;
    buzz_geometry_job_decode_attributes(foreign.get(),attribute_reference,&functions);
    buzz_geometry_job_decode_stream(foreign.get(),attribute_reference);
    attributes_enabled = false;
    buzz_geometry_job_decode_attributes(ctx,attribute_reference,&functions);
    buzz_geometry_job_decode_stream(ctx,attribute_reference);
    attributes_enabled = true;
    buzz_geometry_job_decode_attributes(ctx,attribute_reference,nullptr);
    const buzz_geometry_attribute_functions null_functions{};
    buzz_geometry_job_decode_attributes(ctx,attribute_reference,&null_functions);
    ctx->gpr[3]._u32[0] = 3;
    buzz_geometry_job_decode_attributes(ctx,attribute_reference,&functions);
    buzz_geometry_job_decode_stream(ctx,attribute_reference);
    assert(attribute_references == 9 && attribute_fallback[0] == 3 && attribute_fallback[1] == 1);
    memcpy(ctx,backup.get(),sizeof(*ctx));
}
#include "../analysis/native-geometry/phase3/conversion-control-original.inc"

void buzz_job_6eba_spu_func_00005648(spu_context*){assert(!"builtin output frontier executed");abort();}
#include "../analysis/native-geometry/phase3/builtin-float3-output-original.inc"
#include "../analysis/native-geometry/phase3/builtin20-output-original.inc"
#include "../analysis/native-geometry/phase3/builtin24-output-original.inc"
#include "../analysis/native-geometry/phase3/builtin24-cmp-output-original.inc"
static void builtin_output_reference0(spu_context* c){++builtin_output_references[0];buzz_builtin_float3_output_original_000056C8(c);}
static void builtin_output_reference1(spu_context* c){++builtin_output_references[1];buzz_builtin20_output_original_00005E28(c);}
static void builtin_output_reference2(spu_context* c){++builtin_output_references[2];buzz_builtin24_output_original_00005760(c);}
static void builtin_output_reference3(spu_context* c){++builtin_output_references[3];buzz_builtin24_cmp_output_original_00005A30(c);}
static void builtin_output_fallback_call(spu_context* c){
    assert(builtin_output_unchanged && !memcmp(c,builtin_output_unchanged,sizeof(*c)) && g_spu_trampoline_fn==builtin_output_queue);
    ++builtin_output_fallback_calls[builtin_output_current];
}
static void builtin_output_routing(spu_context* ctx){
    auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
    const buzz_geometry_job_fn refs[]={builtin_output_reference0,builtin_output_reference1,builtin_output_reference2,builtin_output_reference3};
    const buzz_geometry_job_fn originals[]={buzz_builtin_float3_output_original_000056C8,buzz_builtin20_output_original_00005E28,buzz_builtin24_output_original_00005760,buzz_builtin24_cmp_output_original_00005A30};
    const unsigned roots[]={0x56c8,0x5e28,0x5760,0x5a30};
    const char* files[]={"builtin-float3-output-calibration.bin","builtin20-output-calibration.bin","builtin24-output-calibration.bin","builtin24-cmp-output-calibration.bin"};
    for(unsigned format=0;format<4;++format){
        builtin_output_current=format;auto* entry=buzz::clone_function_state(ctx);const auto path=std::string("analysis/native-geometry/phase3/")+files[format];auto* file=fopen(path.c_str(),"rb");assert(file);
        assert(fread(entry->gpr,sizeof entry->gpr,1,file)==1 && fread(entry->ls,SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF);fclose(file);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;ctx->pc=roots[format];ctx->int_enable=0;ctx->irq_saved=0;ctx->irq_frame=nullptr;
            flow_mode=1;for(auto& mode:builtin_output_mode)mode=0;builtin_output_mode[format]=1;g_spu_trampoline_fn=spu_indirect_branch;};
        for(unsigned depth=0;depth<3;++depth)for(unsigned mode=0;mode<3;++mode)for(unsigned count:{2u,4u,8u,18u})for(unsigned bias=0;bias<16;++bias){
            reset();ctx->host_depth=depth;flow_mode=mode==1?2:1;builtin_output_mode[format]=mode==2?2:1;ctx->gpr[29]._u32[0]=count;ctx->gpr[10]._u32[0]=count;
            ctx->gpr[3]._u32[0]=ctx->gpr[82]._u32[0]=0x28000+bias;ctx->event_mask=0x98765432+bias;ctx->policy_mode=mode;ctx->resident_ovl=bias%5;
            auto* expected=buzz::clone_function_state(ctx);g_spu_trampoline_fn=nullptr;originals[format](expected);
            for(unsigned steps=0;!(expected->pc==0x5648 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_00005648);++steps){assert(steps<65536 && g_spu_trampoline_fn);const auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(expected);}
            g_spu_trampoline_fn=spu_indirect_branch;buzz_geometry_job_builtin_output(ctx,builtin_output_fallback_call,refs[format],buzz_job_6eba_spu_func_00005648,format);
            assert(buzz::compare_function_state(*ctx,*expected).exact() && ctx->host_depth==expected->host_depth && ctx->image_id==expected->image_id && ctx->policy_mode==expected->policy_mode && ctx->resident_ovl==expected->resident_ovl && g_spu_trampoline_fn==buzz_job_6eba_spu_func_00005648);buzz::release_function_state(expected);
        }
        auto unchanged=[&](spu_context* target,buzz_geometry_job_fn reference,buzz_geometry_job_fn finish){auto before=std::make_unique<spu_context>();memcpy(before.get(),target,sizeof(*target));
            builtin_output_unchanged=before.get();builtin_output_queue=g_spu_trampoline_fn;buzz_geometry_job_builtin_output(target,builtin_output_fallback_call,reference,finish,format);
            assert(!memcmp(target,before.get(),sizeof(*target)) && g_spu_trampoline_fn==builtin_output_queue);builtin_output_unchanged=nullptr;};
        reset();builtin_output_mode[format]=0;unchanged(ctx,refs[format],buzz_job_6eba_spu_func_00005648);
        reset();ctx->image_id=40;unchanged(ctx,refs[format],buzz_job_6eba_spu_func_00005648);
        reset();auto* foreign=buzz::clone_function_state(ctx);unchanged(foreign,refs[format],buzz_job_6eba_spu_func_00005648);buzz::release_function_state(foreign);
        for(unsigned failure=0;failure<12;++failure){reset();switch(failure){
            case 2:ctx->ls=entry->ls_store;break;case 3:ctx->int_enable=1;break;case 4:ctx->irq_saved=1;break;case 5:ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;
            case 6:ctx->gpr[10]._u32[0]=ctx->gpr[29]._u32[0]=0;break;
            case 7:ctx->gpr[29]._u32[0]=3;if(!format)ctx->gpr[3]._u32[0]=0;break;
            case 8:ctx->gpr[10]._u32[0]=65536;ctx->gpr[29]._u32[0]=65538;break;
            case 9:ctx->gpr[3]._u32[0]=ctx->gpr[82]._u32[0]=0;break;
            case 10:ctx->gpr[3]._u32[0]=ctx->gpr[82]._u32[0]=0x3fff0;ctx->gpr[10]._u32[0]=ctx->gpr[29]._u32[0]=8;break;
            case 11:ctx->gpr[3]._u32[0]=ctx->gpr[82]._u32[0]=0xe680;ctx->gpr[10]._u32[0]=65535;ctx->gpr[29]._u32[0]=65536;break;}
            unchanged(ctx,failure==0?nullptr:refs[format],failure==1?nullptr:buzz_job_6eba_spu_func_00005648);
        }
        assert(builtin_output_native[format]==576 && builtin_output_checked[format]==384 && builtin_output_references[format]==384 && builtin_output_fallback[format]==12 && builtin_output_fallback_calls[format]==15);
        buzz::release_function_state(entry);
    }
    for(auto& mode:builtin_output_mode)mode=0;flow_mode=1;memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
}
static void conversion_control_reference(spu_context* c){++conversion_control_references;buzz_conversion_original_run(c);}
static const spu_context* conversion_control_unchanged=nullptr;
static buzz_geometry_job_fn conversion_control_queue=nullptr;
static void conversion_control_fallback_call(spu_context* c){
    if(conversion_control_unchanged)assert(!memcmp(c,conversion_control_unchanged,sizeof(*c)) && g_spu_trampoline_fn==conversion_control_queue);
    ++conversion_control_fallback_calls;
}
static void conversion_control_formatter(spu_context* c){
    buzz::GeometryFormatter plan;const char* why=nullptr;assert(buzz::read_geometry_formatter(*c,plan,&why));
    const buzz::GeometryFormatterCallees functions{{formatter_d0,formatter_d1,formatter_d2,formatter_d3,formatter_d4,formatter_d5,formatter_d6},buzz_formatter_original_00004610,buzz_formatter_original_00004618};
    buzz::execute_geometry_formatter(*c,plan,functions);
}
static void conversion_control_routing(spu_context* ctx)
{
    auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;auto* entry=buzz::clone_function_state(ctx);
    auto* fixture=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin","rb");assert(fixture);
    assert(fread(entry->gpr,sizeof entry->gpr,1,fixture)==1 && fread(entry->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
    const unsigned manager=entry->gpr[3]._u32[0]&~15u,desc=buzz::geometry_job_be32(entry->ls+manager+0x74);
    int* modes[]={&snorm_output_mode,&float_output_mode,&half_output_mode,&unorm_output_mode,&i16_output_mode,&cmp_output_mode,&u8_integer_output_mode};
    auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;
        conversion_control_mode=formatter_mode=flow_mode=1;for(auto* m:modes)*m=1;};
    auto field=[&](unsigned kind){ctx->ls[desc]=1;ctx->ls[desc+1]=23;ctx->ls[desc+9]=kind;
        const unsigned widths[]={0,6,12,6,3,6,4,3};ctx->ls[desc+12]=widths[kind];};
    auto checked_call=[&](buzz_geometry_job_fn formatter){auto* expected=buzz::clone_function_state(ctx);const auto incoming=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;buzz_conversion_original_run(expected);const auto finish=g_spu_trampoline_fn;g_spu_trampoline_fn=incoming;
        buzz_geometry_job_conversion_control(ctx,conversion_control_fallback_call,conversion_control_reference,formatter,nullptr);
        assert(buzz::compare_function_state(*ctx,*expected).exact() && ctx->host_depth==expected->host_depth && ctx->image_id==expected->image_id &&
            ctx->policy_mode==expected->policy_mode && ctx->resident_ovl==expected->resident_ovl && g_spu_trampoline_fn==finish);buzz::release_function_state(expected);};
    for(unsigned path=0;path<3;++path)for(unsigned depth=0;depth<3;++depth)for(unsigned mode=0;mode<3;++mode){
        reset();ctx->host_depth=depth;flow_mode=mode==1?2:1;conversion_control_mode=mode==2?2:1;ctx->gpr[1]._u32[0]+=path*4;
        if(path==0)ctx->ls[manager]|=16;if(path==1)ctx->ls[manager+8]=ctx->ls[manager+9]=0;
        if(path<2){formatter_mode=0;for(auto* m:modes)*m=0;}checked_call(path<2?nullptr:conversion_control_formatter);
    }
    for(unsigned kind=1;kind<=7;++kind)for(unsigned depth=0;depth<3;++depth)for(unsigned mode=0;mode<3;++mode){
        reset();field(kind);ctx->host_depth=depth;flow_mode=mode==1?2:1;conversion_control_mode=mode==2?2:1;
        for(auto* m:modes)*m=0;*modes[kind-1]=1;checked_call(conversion_control_formatter);
    }
    assert(conversion_control_native==90 && conversion_control_checked==60 && conversion_control_references==60 && !conversion_control_fallback && !conversion_control_fallback_calls);
    auto unchanged=[&](buzz_geometry_job_fn reference,buzz_geometry_job_fn formatter){auto before=std::make_unique<spu_context>();memcpy(before.get(),ctx,sizeof(*ctx));
        conversion_control_unchanged=before.get();conversion_control_queue=g_spu_trampoline_fn;
        buzz_geometry_job_conversion_control(ctx,conversion_control_fallback_call,reference,formatter,nullptr);
        assert(!memcmp(ctx,before.get(),sizeof(*ctx)) && g_spu_trampoline_fn==conversion_control_queue);conversion_control_unchanged=nullptr;};
    reset();conversion_control_mode=0;unchanged(conversion_control_reference,conversion_control_formatter);
    reset();ctx->image_id=40;unchanged(conversion_control_reference,conversion_control_formatter);
    reset();auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_conversion_control(foreign,conversion_control_fallback_call,conversion_control_reference,conversion_control_formatter,nullptr);buzz::release_function_state(foreign);
    for(unsigned failure=0;failure<19;++failure){reset();switch(failure){
        case 2:formatter_mode=0;break;case 3:ctx->ls=entry->ls_store;break;case 4:ctx->int_enable=1;break;case 5:ctx->irq_saved=1;break;
        case 6:ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;case 7:ctx->ls[manager+4]=0;buzz::geometry_job_put32(ctx->ls+0x5604,0x1234);break;
        case 8:buzz::geometry_job_put32(ctx->ls+manager+0x8c,0xbeef);break;case 9:buzz::geometry_job_put32(ctx->ls+manager+0x88,0xbeef);break;
        case 10:buzz::geometry_job_put32(ctx->ls+manager+0x74,0);break;
        case 11:buzz::geometry_job_put32(ctx->ls+manager+0x74,ctx->gpr[1]._u32[0]-32);break;
        case 12:buzz::geometry_job_put32(ctx->ls+manager+0x74,ctx->gpr[1]._u32[0]-224);break;
        case 13:buzz::geometry_job_put32(ctx->ls+manager+0x58,0);break;case 14:buzz::geometry_job_put32(ctx->ls+manager+0x58,manager);break;
        case 15:buzz::geometry_job_put32(ctx->ls+0x4460,0x1234);break;case 16:ctx->gpr[1]._u32[0]=0xe680;break;case 17:ctx->gpr[3]._u32[0]=0;break;
        case 18:ctx->ls[manager+8]=0;ctx->ls[manager+9]=1;buzz::geometry_job_put32(ctx->ls+manager+0x58,(ctx->gpr[1]._u32[0]&~15u)-128);break;}
        unchanged(failure==0?nullptr:conversion_control_reference,failure==1?nullptr:conversion_control_formatter);
    }
    for(unsigned kind=1;kind<=7;++kind){reset();field(kind);*modes[kind-1]=0;unchanged(conversion_control_reference,conversion_control_formatter);}
    assert(conversion_control_native==90 && conversion_control_checked==60 && conversion_control_references==60 && conversion_control_fallback==26 && conversion_control_fallback_calls==29);
    for(auto* m:modes)*m=0;conversion_control_mode=formatter_mode=0;flow_mode=1;
    buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
}
static void builtin_control_data0(spu_context* c){buzz_geometry_job_builtin_output(c,builtin_output_fallback_call,builtin_output_reference0,buzz_job_6eba_spu_func_00005648,0);}
static void builtin_control_data1(spu_context* c){buzz_geometry_job_builtin_output(c,builtin_output_fallback_call,builtin_output_reference1,buzz_job_6eba_spu_func_00005648,1);}
static void builtin_control_data2(spu_context* c){buzz_geometry_job_builtin_output(c,builtin_output_fallback_call,builtin_output_reference2,buzz_job_6eba_spu_func_00005648,2);}
static void builtin_control_data3(spu_context* c){buzz_geometry_job_builtin_output(c,builtin_output_fallback_call,builtin_output_reference3,buzz_job_6eba_spu_func_00005648,3);}
static void builtin_control_routing(spu_context* ctx){
    auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;auto* entry=buzz::clone_function_state(ctx);
    auto* file=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin","rb");assert(file);
    assert(fread(entry->gpr,sizeof entry->gpr,1,file)==1 && fread(entry->ls,SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF);fclose(file);
    const unsigned manager=entry->gpr[3]._u32[0]&~15u;
    const buzz_geometry_job_fn data[]={builtin_control_data0,builtin_control_data1,builtin_control_data2,builtin_control_data3};
    const buzz_geometry_builtin_output_functions full{{data[0],data[1],data[2],data[3]},buzz_job_6eba_spu_func_00005648};
    auto reset=[&](unsigned format,unsigned count=3,unsigned bias=0,unsigned depth=0,unsigned mode=0){
        memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;ctx->host_depth=depth;ctx->int_enable=0;ctx->irq_saved=0;ctx->irq_frame=nullptr;g_spu_trampoline_fn=spu_indirect_branch;
        ctx->gpr[1]._u32[0]+=bias;ctx->ls[manager]&=~16u;ctx->ls[manager+4]=format;ctx->ls[manager+8]=count>>8;ctx->ls[manager+9]=count;buzz::geometry_job_put32(ctx->ls+manager+0x58,0x28000+bias);
        formatter_mode=snorm_output_mode=float_output_mode=half_output_mode=unorm_output_mode=i16_output_mode=cmp_output_mode=u8_integer_output_mode=0;
        flow_mode=mode==1?2:1;conversion_control_mode=mode==2?2:1;for(auto& flag:builtin_output_mode)flag=0;builtin_output_mode[format]=mode==3?2:1;builtin_output_current=format;
    };
    for(unsigned format=0;format<4;++format){
        buzz_geometry_builtin_output_functions library{};library.data[format]=data[format];library.finish=full.finish;
        for(unsigned mode=0;mode<4;++mode)for(unsigned depth=0;depth<3;++depth)for(unsigned bias=0;bias<16;++bias)for(unsigned count:{1u,3u,8u,17u}){
            reset(format,count,bias,depth,mode);auto* expected=buzz::clone_function_state(ctx);g_spu_trampoline_fn=nullptr;buzz_conversion_original_run(expected);const auto finish=g_spu_trampoline_fn;g_spu_trampoline_fn=spu_indirect_branch;
            buzz_geometry_job_conversion_control(ctx,conversion_control_fallback_call,conversion_control_reference,nullptr,&library);
            assert(buzz::compare_function_state(*ctx,*expected).exact() && ctx->host_depth==expected->host_depth && ctx->image_id==expected->image_id && ctx->policy_mode==expected->policy_mode && ctx->resident_ovl==expected->resident_ovl && g_spu_trampoline_fn==finish);buzz::release_function_state(expected);
        }
        auto unchanged=[&](spu_context* target,const buzz_geometry_builtin_output_functions* functions){auto before=std::make_unique<spu_context>();memcpy(before.get(),target,sizeof(*target));
            conversion_control_unchanged=before.get();conversion_control_queue=g_spu_trampoline_fn;const auto children=builtin_output_native[format];
            buzz_geometry_job_conversion_control(target,conversion_control_fallback_call,conversion_control_reference,nullptr,functions);
            assert(!memcmp(target,before.get(),sizeof(*target)) && g_spu_trampoline_fn==conversion_control_queue && children==builtin_output_native[format]);conversion_control_unchanged=nullptr;};
        reset(format);conversion_control_mode=0;unchanged(ctx,&library);
        reset(format);ctx->image_id=40;unchanged(ctx,&library);
        reset(format);auto* foreign=buzz::clone_function_state(ctx);unchanged(foreign,&library);buzz::release_function_state(foreign);
        for(unsigned failure=0;failure<14;++failure){reset(format);auto functions=library;switch(failure){
            case 1:functions.finish=nullptr;break;case 2:functions.data[format]=nullptr;break;case 3:builtin_output_mode[format]=0;break;
            case 4:buzz::geometry_job_put32(ctx->ls+manager+0x58,manager);break;
            case 5:ctx->ls[manager+8]=0;ctx->ls[manager+9]=1;buzz::geometry_job_put32(ctx->ls+manager+0x58,manager-32);break;
            case 6:buzz::geometry_job_put32(ctx->ls+0x5604+format*4,0x1234);break;case 7:buzz::geometry_job_put32(ctx->ls+0x4104+format*4,0x1234);break;
            case 8:ctx->int_enable=1;break;case 9:ctx->irq_saved=1;break;case 10:ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;
            case 11:ctx->ls=entry->ls_store;break;case 12:buzz::geometry_job_put32(ctx->ls+manager+0x58,0);break;case 13:ctx->ls[manager+8]=ctx->ls[manager+9]=255;break;}
            unchanged(ctx,failure==0?nullptr:&functions);
        }
        assert(builtin_output_native[format]==1344 && builtin_output_checked[format]==768 && builtin_output_references[format]==768 && builtin_output_fallback[format]==12 && builtin_output_fallback_calls[format]==15);
    }
    assert(conversion_control_native==3162 && conversion_control_checked==1596 && conversion_control_references==1596 && conversion_control_fallback==82 && conversion_control_fallback_calls==97);
    for(auto& flag:builtin_output_mode)flag=0;conversion_control_mode=formatter_mode=0;flow_mode=1;memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);buzz::release_function_state(entry);g_spu_trampoline_fn=continuation;
    puts("Builtin native-parent routing:3072 native/1536 parent strict/1536 child strict roots;all4 formats/4modes/3depths/16biases/4counts;56 unchanged owned rejects+12 off/image/foreign;unused formatter/encoder/builtin function dependencies disabled PASS");
}

#include "../analysis/native-geometry/phase3/input-control-original.inc"
#include "../analysis/native-geometry/phase3/builtin20-input-original.inc"
#include "../analysis/native-geometry/phase3/builtin24-input-original.inc"
#include "../analysis/native-geometry/phase3/builtin24-cmp-input-original.inc"
#include "../analysis/native-geometry/phase3/builtin12-input-original.inc"
#include "../analysis/native-geometry/phase3/builtin15-input-original.inc"
static const spu_context* input_unchanged=nullptr;
static buzz_geometry_job_fn input_queue=nullptr;
static void input_fallback(spu_context* c){assert(input_unchanged && !memcmp(c,input_unchanged,sizeof(*c)) && g_spu_trampoline_fn==input_queue);++input_control_fallback_calls;}
static void input_reference(spu_context* c){++input_control_references;buzz_input_original_run(c);}
static void input_reference1(spu_context* c){++builtin_input_references[0];buzz_builtin20_input_original_0000A8F8(c);}
static void input_reference2(spu_context* c){++builtin_input_references[1];buzz_builtin24_input_original_0000A6A8(c);}
static void input_reference3(spu_context* c){++builtin_input_references[2];buzz_builtin24_cmp_input_original_0000A390(c);}
static void input_reference4(spu_context* c){++builtin_input_references[3];buzz_builtin12_input_original_00009E70(c);}
static void input_reference5(spu_context* c){++builtin_input_references[4];buzz_builtin15_input_original_000097F8(c);}
static void input_data1(spu_context* c){buzz_geometry_job_builtin_input(c,input_fallback,input_reference1,buzz_job_6eba_spu_func_00009498,1);}
static void input_data2(spu_context* c){buzz_geometry_job_builtin_input(c,input_fallback,input_reference2,buzz_job_6eba_spu_func_00009498,2);}
static void input_data3(spu_context* c){buzz_geometry_job_builtin_input(c,input_fallback,input_reference3,buzz_job_6eba_spu_func_00009498,3);}
static void input_data4(spu_context* c){buzz_geometry_job_builtin_input(c,input_fallback,input_reference4,buzz_job_6eba_spu_func_00009498,4);}
static void input_data5(spu_context* c){buzz_geometry_job_builtin_input(c,input_fallback,input_reference5,buzz_job_6eba_spu_func_00009498,5);}
static void input_control_routing(spu_context* ctx){
    auto* saved=buzz::clone_function_state(ctx);const auto queue=g_spu_trampoline_fn;auto* base=buzz::clone_function_state(ctx);
    auto* f=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin","rb");assert(f);assert(fread(base->gpr,sizeof base->gpr,1,f)==1 && fread(base->ls,SPU_LS_SIZE,1,f)==1 && fgetc(f)==EOF);fclose(f);
    const bool prior_attributes=attributes_enabled;const auto prior_custom=custom_stream_mode;attributes_enabled=false;custom_stream_mode=0;
    const buzz_geometry_job_fn data[]={input_data1,input_data2,input_data3,input_data4,input_data5};unsigned roots=0,parent_checks=0,child_checks=0;
    auto reset=[&](unsigned format,unsigned count,unsigned bias,unsigned depth,unsigned mode){
        memcpy(ctx,base,sizeof(*ctx));ctx->ls=ctx->ls_store;ctx->int_enable=0;ctx->irq_saved=0;ctx->irq_frame=nullptr;ctx->host_depth=depth;ctx->pc=0x93f0;
        ctx->gpr[0]=spu_link(0x12345678);ctx->gpr[1]=spu_ila(0x3f000+bias);ctx->gpr[3]=spu_ila(0x3d000);ctx->gpr[4]=spu_ila(0x18000+bias);ctx->gpr[5]=spu_ila(0x20000+bias);ctx->gpr[6]=spu_il(format);
        memset(ctx->ls+0x3d000,0,0x190);ctx->ls[0x3d008]=count>>8;ctx->ls[0x3d009]=count;for(unsigned slot=0;slot<17;++slot)buzz::geometry_job_put32(ctx->ls+0x3d010+slot*4,(slot<4?0x24000+slot*0x4000:0x30000)+bias);
        flow_mode=mode==1?2:1;input_control_mode=mode==2?2:1;for(auto& flag:builtin_input_mode)flag=0;if(format>=1 && format<=5)builtin_input_mode[format-1]=mode==3?2:1;g_spu_trampoline_fn=spu_indirect_branch;
    };
    auto compare=[&](const buzz_geometry_builtin_input_functions* library,unsigned mode,bool child){
        auto* expected=buzz::clone_function_state(ctx);g_spu_trampoline_fn=nullptr;buzz_input_original_run(expected);const auto finish=g_spu_trampoline_fn;g_spu_trampoline_fn=spu_indirect_branch;
        buzz_geometry_job_input_control(ctx,input_fallback,input_reference,nullptr,library);
        assert(buzz::compare_function_state(*ctx,*expected).exact() && ctx->host_depth==expected->host_depth && ctx->image_id==expected->image_id && ctx->policy_mode==expected->policy_mode && ctx->resident_ovl==expected->resident_ovl && g_spu_trampoline_fn==finish);buzz::release_function_state(expected);
        ++roots;if(mode==1 || mode==2)++parent_checks;if(child && (mode==1 || mode==3))++child_checks;
    };
    for(unsigned format=1;format<=5;++format){buzz_geometry_builtin_input_functions library{};library.data[format-1]=data[format-1];library.finish=buzz_job_6eba_spu_func_00009498;
        for(unsigned mode=0;mode<4;++mode)for(unsigned depth=0;depth<3;++depth)for(unsigned bias=0;bias<16;++bias)for(unsigned count:{1u,3u,17u}){reset(format,count,bias,depth,mode);compare(&library,mode,true);}
    }
    for(unsigned mode=0;mode<4;++mode)for(unsigned depth=0;depth<3;++depth){
        for(unsigned count:{1u,3u,17u}){reset(0,count,0,depth,mode);compare(nullptr,mode,false);}
        for(unsigned bias=0;bias<16;++bias)for(unsigned format:{0u,1u,5u,6u,255u}){reset(format,0,bias,depth,mode);compare(nullptr,mode,false);}
    }
    assert(roots==3876 && parent_checks==1938 && child_checks==1440 && input_control_native==roots && input_control_checked==parent_checks && input_control_references==parent_checks);
    for(unsigned i=0;i<5;++i)assert(builtin_input_native[i]==576 && builtin_input_checked[i]==288 && builtin_input_references[i]==288 && !builtin_input_fallback[i]);
    auto unchanged=[&](spu_context* c,const buzz_geometry_builtin_input_functions* library,buzz_geometry_job_fn reference=input_reference){
        auto before=std::make_unique<spu_context>();memcpy(before.get(),c,sizeof(*c));input_unchanged=before.get();input_queue=g_spu_trampoline_fn;const auto children=builtin_input_native[0];buzz_geometry_job_input_control(c,input_fallback,reference,nullptr,library);
        assert(!memcmp(c,before.get(),sizeof(*c)) && g_spu_trampoline_fn==input_queue && children==builtin_input_native[0]);input_unchanged=nullptr;
    };
    buzz_geometry_builtin_input_functions library{};library.data[0]=data[0];library.finish=buzz_job_6eba_spu_func_00009498;
    for(unsigned failure=0;failure<16;++failure){reset(1,17,0,0,0);auto functions=library;switch(failure){
        case 0:input_control_mode=0;break;case 1:ctx->image_id=40;break;case 2:functions.finish=nullptr;break;case 3:functions.data[0]=nullptr;break;case 4:builtin_input_mode[0]=0;break;
        case 5:ctx->int_enable=1;break;case 6:ctx->irq_saved=1;break;case 7:ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;case 8:ctx->ls=base->ls_store;break;
        case 9:buzz::geometry_job_put32(ctx->ls+0x3d010,0x3f000-32);break;case 10:buzz::geometry_job_put32(ctx->ls+0x3d010,0x3d000);break;case 11:ctx->gpr[3]._u32[0]=0x3f000-96;break;
        case 12:ctx->gpr[1]._u32[0]=0xe700;break;case 13:ctx->ls[0x95cb]^=1;break;}
        unchanged(ctx,failure==14?nullptr:&functions,failure==15?nullptr:input_reference);
    }
    reset(1,17,0,0,0);auto* foreign=buzz::clone_function_state(ctx);unchanged(foreign,&library);buzz::release_function_state(foreign);
    reset(1,17,0,0,0);auto before=std::make_unique<spu_context>();memcpy(before.get(),ctx,sizeof(*ctx));input_unchanged=before.get();input_queue=g_spu_trampoline_fn;data[0](ctx);input_unchanged=nullptr;
    assert(input_control_native==3876 && input_control_checked==1938 && input_control_references==1938 && input_control_fallback==14 && input_control_fallback_calls==18);
    for(auto& flag:builtin_input_mode)flag=0;input_control_mode=0;flow_mode=1;attributes_enabled=prior_attributes;custom_stream_mode=prior_custom;memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);buzz::release_function_state(base);g_spu_trampoline_fn=queue;
    puts("Native input production routing:3876 roots/1938 parent strict/1440 child strict;all6 formats/empty/independent options/depths/biases;14 unchanged owned rejects/off/image/foreign and out-of-parent leaf scope PASS");
}
// Use the same production entry wrappers as the generated game. Foreign
// comparison contexts must execute the independent retained lift, not an
// encoder model or the native core under test.
static void formatter_register_corrupt(spu_context* c){
    if(!formatter_registers_corruption)return;
    if(!strcmp(formatter_registers_corruption,"gpr"))c->gpr[127]._u32[3]^=0x12345678;
    else if(!strcmp(formatter_registers_corruption,"ls"))c->ls[0x2000]^=0xff;
    else if(!strcmp(formatter_registers_corruption,"channel"))c->ch_out_mbox.value^=1;
    else if(!strcmp(formatter_registers_corruption,"host"))c->host_depth^=1;
    else abort();
}
#define FORMATTER_REGISTER_DATA(N,TYPE,ORIGINAL) \
static void formatter_register_reference##N(spu_context* c){++formatter_registers_references[N];ORIGINAL(c);formatter_register_corrupt(c);} \
static void formatter_register_data##N(spu_context* c){++formatter_registers_entries[N]; \
    buzz_geometry_job_##TYPE##_output(c,formatter_register_reference##N,formatter_register_reference##N,buzz_formatter_original_00004610,buzz_formatter_original_00004618);}
FORMATTER_REGISTER_DATA(0,snorm,buzz_formatter_original_00004478)
FORMATTER_REGISTER_DATA(1,float,buzz_formatter_original_000046D0)
FORMATTER_REGISTER_DATA(2,half,buzz_formatter_original_00004880)
FORMATTER_REGISTER_DATA(3,unorm,buzz_formatter_original_00004B9C)
FORMATTER_REGISTER_DATA(4,i16,buzz_formatter_original_00004DD0)
FORMATTER_REGISTER_DATA(5,cmp,buzz_formatter_original_00004FDC)
FORMATTER_REGISTER_DATA(6,u8_integer,buzz_formatter_original_000052B4)
#undef FORMATTER_REGISTER_DATA
static const buzz_geometry_formatter_functions formatter_register_library{
    {formatter_register_data0,formatter_register_data1,formatter_register_data2,formatter_register_data3,
     formatter_register_data4,formatter_register_data5,formatter_register_data6},buzz_formatter_original_00004610,buzz_formatter_original_00004618};

static void formatter_register_routing(spu_context* ctx)
{
    auto* saved=buzz::clone_function_state(ctx);const auto queue=g_spu_trampoline_fn;
    auto* entry=buzz::clone_function_state(ctx);
    auto* fixture=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_4178.bin","rb");assert(fixture);
    assert(fread(entry->gpr,sizeof entry->gpr,1,fixture)==1 && fread(entry->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
    const unsigned manager=entry->gpr[3]._u32[0]&~15u,desc=entry->gpr[4]._u32[0]&~15u;
    int* modes[]={&snorm_output_mode,&float_output_mode,&half_output_mode,&unorm_output_mode,&i16_output_mode,&cmp_output_mode,&u8_integer_output_mode};
    unsigned* natives[]={&snorm_output_native,&float_output_native,&half_output_native,&unorm_output_native,&i16_output_native,&cmp_output_native,&u8_integer_output_native};
    unsigned* stricts[]={&snorm_output_checked,&float_output_checked,&half_output_checked,&unorm_output_checked,&i16_output_checked,&cmp_output_checked,&u8_integer_output_checked};
    unsigned expected_roots=0,expected_parents=0,expected_children=0,expected_strict_children=0,rejects=0;
    const unsigned widths[]={0,6,12,6,3,6,4,3};
    auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;
        formatter_mode=flow_mode=1;formatter_registers_mode=1;for(auto* m:modes)*m=1;};
    auto field=[&](unsigned index,unsigned kind,unsigned bias){
        if(index)memcpy(ctx->ls+desc+8+8*index,entry->ls+desc+8,8);
        ctx->ls[desc+9+8*index]=kind;ctx->ls[desc+12+8*index]=widths[kind];ctx->ls[desc+13+8*index]=bias;
    };
    auto check=[&](){
        buzz::GeometryFormatter plan;const char* why=nullptr;
        if(!buzz::read_geometry_formatter(*ctx,plan,&why)){fprintf(stderr,"host formatter test%u reject:%s\n",formatter_registers_cases,why);abort();}
        const bool parent_checked=flow_mode==2 || formatter_mode==2;
        unsigned expected_native[7]{},expected_strict[7]{},references[7],entries_before[7],native_before[7],strict_before[7];
        const unsigned count=ctx->ls[desc];
        // Every known field is accounted separately, including repeated types.
        if(plan.fields)for(unsigned i=0;i<count;++i){const unsigned kind=ctx->ls[desc+9+8*i];if(kind<1 || kind>7)continue;
            ++expected_native[kind-1];expected_strict[kind-1]+=flow_mode==2 || *modes[kind-1]==2;
        }
        unsigned children=0,checked_children=0;
        for(unsigned i=0;i<7;++i){children+=expected_native[i];checked_children+=expected_strict[i];
            native_before[i]=*natives[i];strict_before[i]=*stricts[i];references[i]=formatter_registers_references[i];entries_before[i]=formatter_registers_entries[i];}
        assert(children==plan.known);
        auto* expected=buzz::clone_function_state(ctx);const auto incoming=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;buzz_formatter_original_run(expected);const auto finish=g_spu_trampoline_fn;g_spu_trampoline_fn=incoming;
        const auto prior_references=formatter_references,prior_native=formatter_native,prior_checked=formatter_checked;
        buzz_geometry_job_formatter(ctx,formatter_fallback_call,formatter_reference,&formatter_register_library);
        assert(buzz::compare_function_state(*ctx,*expected).exact() && ctx->host_depth==expected->host_depth && ctx->image_id==expected->image_id &&
            ctx->policy_mode==expected->policy_mode && ctx->resident_ovl==expected->resident_ovl && g_spu_trampoline_fn==finish);
        assert(formatter_references-prior_references==unsigned(parent_checked) && formatter_checked-prior_checked==unsigned(parent_checked) && formatter_native-prior_native==1);
        for(unsigned i=0;i<7;++i){
            assert(*natives[i]-native_before[i]==expected_native[i] && *stricts[i]-strict_before[i]==expected_strict[i]);
            assert(formatter_registers_references[i]-references[i]==expected_strict[i]);
            assert(formatter_registers_entries[i]-entries_before[i]==(formatter_registers_mode?expected_strict[i]:expected_native[i]));
        }
        buzz::release_function_state(expected);
        if(formatter_registers_mode){++expected_roots;expected_parents+=parent_checked;expected_children+=children;expected_strict_children+=checked_children;}
        assert(formatter_registers_roots==expected_roots && formatter_registers_parents==expected_parents &&
            formatter_registers_children==expected_children && formatter_registers_strict_children==expected_strict_children);
        ++formatter_registers_cases;
    };
    if(formatter_registers_corruption){
        reset();ctx->ls[manager+8]=0;ctx->ls[manager+9]=1;ctx->ls[desc]=1;field(0,2,0);float_output_mode=2;
        check();assert(!"strict child failed to detect injected mismatch");
    }
    for(unsigned host=0;host<2;++host)for(unsigned kind=1;kind<=7;++kind)for(unsigned count:{0u,1u,5u,17u})
    for(unsigned bias:{0u,15u})for(unsigned depth=0;depth<3;++depth)for(unsigned mode=0;mode<5;++mode){
        reset();formatter_registers_mode=host;ctx->host_depth=depth;ctx->ls[manager+8]=0;ctx->ls[manager+9]=count;
        ctx->ls[desc]=1;ctx->ls[desc+1]=23;field(0,kind,bias);
        if(mode==1)flow_mode=2;if(mode==2)formatter_mode=2;if(mode==3)*modes[kind-1]=2;if(mode==4)*modes[kind%7]=2;
        check();
    }
    // Shared bank across multiple actual encoders and a skipped descriptor.
    for(unsigned host=0;host<2;++host)for(unsigned count:{1u,17u})for(unsigned depth=0;depth<3;++depth)for(unsigned mode=0;mode<4;++mode){
        reset();formatter_registers_mode=host;ctx->host_depth=depth;ctx->ls[manager+8]=0;ctx->ls[manager+9]=count;ctx->ls[desc]=8;ctx->ls[desc+1]=48;
        for(unsigned i=0;i<8;++i)field(i,i==7?0:i+1,(i*3)&15);
        if(mode==1)flow_mode=2;if(mode==2)formatter_mode=2;if(mode==3){half_output_mode=2;i16_output_mode=2;}
        check();
    }
    // Actual stores overwrite saved formatter vectors; the final bank must
    // come from late LS reloads, even though encoders shared a host bank.
    for(unsigned host=0;host<2;++host)for(unsigned offset:{16u,32u,48u,64u})
    for(unsigned depth=0;depth<3;++depth)for(unsigned mode=0;mode<2;++mode){
        reset();formatter_registers_mode=host;ctx->host_depth=depth;flow_mode=mode?2:1;
        ctx->ls[manager+8]=0;ctx->ls[manager+9]=1;ctx->ls[desc]=1;ctx->ls[desc+1]=0;field(0,2,0);
        const unsigned target=(ctx->gpr[1]._u32[0]&~15u)-offset;
        spu_ls_write128(ctx,manager+0x50,spu_ila(target));check();
        const unsigned reg=offset==64?126:79+offset/16;
        const auto reloaded=spu_ls_read128(ctx,target);
        assert(!memcmp(&ctx->gpr[reg],&reloaded,16));
    }
    auto unchanged=[&](spu_context* target,const buzz_geometry_formatter_functions* functions){
        auto* before=buzz::clone_function_state(target);const auto incoming=g_spu_trampoline_fn;
        const auto roots=formatter_registers_roots,children=formatter_registers_children,native=formatter_native;
        buzz_geometry_job_formatter(target,formatter_fallback_call,formatter_reference,functions);
        assert(buzz::compare_function_state(*target,*before).exact() && g_spu_trampoline_fn==incoming &&
            roots==formatter_registers_roots && children==formatter_registers_children && native==formatter_native);
        buzz::release_function_state(before);++rejects;
    };
    for(unsigned kind=1;kind<=7;++kind){reset();ctx->ls[desc]=1;field(0,kind,0);*modes[kind-1]=0;unchanged(ctx,&formatter_register_library);
        reset();ctx->ls[desc]=1;field(0,kind,0);auto functions=formatter_register_library;functions.data[kind-1]=nullptr;unchanged(ctx,&functions);}
    reset();formatter_mode=0;unchanged(ctx,&formatter_register_library);
    reset();ctx->image_id=40;unchanged(ctx,&formatter_register_library);
    reset();ctx->int_enable=1;unchanged(ctx,&formatter_register_library);
    reset();ctx->irq_saved=1;unchanged(ctx,&formatter_register_library);
    reset();ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));unchanged(ctx,&formatter_register_library);
    reset();auto* foreign=buzz::clone_function_state(ctx);unchanged(foreign,&formatter_register_library);buzz::release_function_state(foreign);
    assert(formatter_registers_cases==1776 && rejects==20 && expected_children && expected_strict_children);
    for(auto cross:formatter_registers_cross)assert(cross);
    printf("PASS production formatter registers:%u full original-state roots;%u shared-bank roots/%u strict parents;%u host children/%u strict children;all7 types,8-field mixtures,zero/one/bulk,cross bias,3depths,JOB2/parent2/used+unused child2;ABI route retained;20 unchanged rejects/off/image/foreign;old stage ledger and actual reference-entry deltas exact\n",formatter_registers_cases,expected_roots,expected_parents,expected_children,expected_strict_children);
    for(auto* m:modes)*m=0;formatter_mode=formatter_registers_mode=0;flow_mode=1;
    memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);buzz::release_function_state(entry);g_spu_trampoline_fn=queue;
}

static void routing_inside(spu_context* ctx)
{
    if(formatter_registers_tests_only){formatter_register_routing(ctx);return;}
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;auto* entry=buzz::clone_function_state(ctx);
        auto* fixture=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_4178.bin","rb");assert(fixture);
        assert(fread(entry->gpr,sizeof entry->gpr,1,fixture)==1 && fread(entry->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
        const unsigned manager=entry->gpr[3]._u32[0]&~15u,desc=entry->gpr[4]._u32[0]&~15u;
        int* modes[]={&snorm_output_mode,&float_output_mode,&half_output_mode,&unorm_output_mode,&i16_output_mode,&cmp_output_mode,&u8_integer_output_mode};
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;
            formatter_mode=1;flow_mode=1;for(auto* m:modes)*m=1;};
        auto field=[&](unsigned kind){ctx->ls[desc]=1;ctx->ls[desc+1]=23;ctx->ls[desc+9]=kind;
            const unsigned widths[]={0,6,12,6,3,6,4,3};ctx->ls[desc+12]=widths[kind];};
        // Independently compare the actual native-mode exit too. The wrapper's
        // reference counter proves mode1 itself never executes the original.
        auto checked_call=[&](){auto* expected=buzz::clone_function_state(ctx);const auto incoming=g_spu_trampoline_fn;
            g_spu_trampoline_fn=nullptr;buzz_formatter_original_run(expected);const auto finish=g_spu_trampoline_fn;g_spu_trampoline_fn=incoming;
            buzz_geometry_job_formatter(ctx,formatter_fallback_call,formatter_reference,&formatter_library);
            assert(buzz::compare_function_state(*ctx,*expected).exact() && ctx->host_depth==expected->host_depth &&
                ctx->image_id==expected->image_id && ctx->policy_mode==expected->policy_mode && ctx->resident_ovl==expected->resident_ovl && g_spu_trampoline_fn==finish);
            buzz::release_function_state(expected);};
        for(unsigned path=0;path<6;++path)for(unsigned depth=0;depth<3;++depth)for(unsigned mode=0;mode<3;++mode){
            reset();ctx->host_depth=depth;flow_mode=mode==1?2:1;formatter_mode=mode==2?2:1;
            if(path==0)ctx->ls[manager]|=16;
            if(path==1)ctx->ls[manager+8]=ctx->ls[manager+9]=0;
            if(path==2)ctx->ls[desc]=0;
            if(path==3){ctx->ls[desc]=1;ctx->ls[desc+9]=255;for(auto* m:modes)*m=0;}
            if(path==4)field(2);
            if(path==5){field(2);ctx->ls[desc]=2;auto* p=ctx->ls+desc+16;p[0]=0;p[1]=255;p[2]=3;p[3]=1;p[4]=4;p[5]=p[6]=p[7]=0;}
            checked_call();
        }
        for(unsigned kind=1;kind<=7;++kind)for(unsigned depth=0;depth<3;++depth)for(unsigned mode=0;mode<3;++mode){
            reset();field(kind);ctx->host_depth=depth;flow_mode=mode==1?2:1;formatter_mode=mode==2?2:1;
            // Only the used storage must be enabled; each other switch is off.
            for(auto* m:modes)*m=0;*modes[kind-1]=1;checked_call();
        }
        assert(formatter_native==117 && formatter_checked==78 && formatter_references==78 && !formatter_fallback && !formatter_fallback_calls && formatter_children==81);
        auto unchanged=[&](buzz_geometry_job_fn reference,const buzz_geometry_formatter_functions* functions){
            auto before=std::make_unique<spu_context>();memcpy(before.get(),ctx,sizeof(*ctx));formatter_unchanged=before.get();formatter_unchanged_queue=g_spu_trampoline_fn;
            buzz_geometry_job_formatter(ctx,formatter_fallback_call,reference,functions);
            assert(!memcmp(ctx,before.get(),sizeof(*ctx)) && g_spu_trampoline_fn==formatter_unchanged_queue);formatter_unchanged=nullptr;};
        // Owner/off/wrong image do not count as an unsupported owned root.
        reset();formatter_mode=0;unchanged(formatter_reference,&formatter_library);
        reset();ctx->image_id=40;unchanged(formatter_reference,&formatter_library);
        reset();auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_formatter(foreign,formatter_fallback_call,formatter_reference,&formatter_library);buzz::release_function_state(foreign);
        for(unsigned failure=0;failure<14;++failure){reset();auto functions=formatter_library;
            switch(failure){case 2:functions.finish=nullptr;break;case 3:functions.skip=nullptr;break;
            case 4:ctx->ls=entry->ls_store;break;case 5:ctx->int_enable=1;break;case 6:ctx->irq_saved=1;break;
            case 7:ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;case 8:ctx->gpr[3]=spu_il(0);break;
            case 9:ctx->gpr[1]=spu_ila(0xe680);break;case 10:ctx->gpr[4]=ctx->gpr[3];break;
            case 11:buzz::geometry_job_put32(ctx->ls+0x4460,0x1234);break;
            case 12:buzz::geometry_job_put32(ctx->ls+manager+0x58,manager);break;
            case 13:buzz::geometry_job_put32(ctx->ls+manager+0x58,0);break;}
            unchanged(failure==0?nullptr:formatter_reference,failure==1?nullptr:&functions);
        }
        for(unsigned kind=1;kind<=7;++kind){reset();field(kind);*modes[kind-1]=0;unchanged(formatter_reference,&formatter_library);
            reset();field(kind);auto functions=formatter_library;functions.data[kind-1]=nullptr;unchanged(formatter_reference,&functions);}
        assert(formatter_native==117 && formatter_checked==78 && formatter_references==78 && formatter_fallback==28 && formatter_fallback_calls==31);
        for(auto* m:modes)*m=0;formatter_mode=0;flow_mode=1;
        buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    conversion_control_routing(ctx);
    builtin_output_routing(ctx);
    builtin_control_routing(ctx);
    input_control_routing(ctx);

    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;auto* entry=buzz::clone_function_state(ctx);
        auto* fixture=fopen("analysis/native-geometry/phase3/snorm-output-v1-fixture/pc_4478.bin","rb");assert(fixture);
        assert(fread(entry->gpr,sizeof entry->gpr,1,fixture)==1 && fread(entry->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;};
        const auto finish=i16_output_oracle::buzz_job_6eba_spu_func_00004610,skip=i16_output_oracle::buzz_job_6eba_spu_func_00004618;
        for(unsigned bias:{0u,15u})for(unsigned empty:{0u,1u})for(unsigned depth:{0u,2u})for(unsigned mode:{0u,1u,2u}){
            reset();flow_mode=mode==1?2:1;i16_output_mode=mode==2?2:1;ctx->host_depth=depth;ctx->gpr[53]=spu_il(bias);if(empty)ctx->gpr[77]._u32[0]=0;
            buzz_geometry_job_i16_output(ctx,i16_output_fallback_call,i16_output_reference,finish,skip);
            assert(ctx->pc==(empty?0x4618:0x4610) && g_spu_trampoline_fn==(empty?skip:finish) && ctx->host_depth==depth);
        }
        reset();flow_mode=2;i16_output_mode=1;ctx->gpr[60]._u32[0]=256;ctx->gpr[48]=spu_il(16);ctx->gpr[45]=spu_ila(65535);
        buzz_geometry_job_i16_output(ctx,i16_output_fallback_call,i16_output_reference,finish,skip);
        assert(ctx->pc==0x4610 && g_spu_trampoline_fn==finish);
        assert(i16_output_native==25 && i16_output_checked==17 && i16_output_references==17 && !i16_output_fallback && !i16_output_fallback_calls);
        reset();i16_output_mode=0;buzz_geometry_job_i16_output(ctx,i16_output_fallback_call,i16_output_reference,finish,skip);i16_output_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_i16_output(foreign,i16_output_fallback_call,i16_output_reference,finish,skip);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_i16_output(ctx,i16_output_fallback_call,i16_output_reference,finish,skip);
        for(unsigned failure=0;failure<10;++failure){reset();
            switch(failure){case 3:ctx->ls=entry->ls_store;break;case 4:ctx->int_enable=1;ctx->event_status=ctx->event_mask=1;break;case 5:ctx->gpr[60]._u32[0]=257;break;case 6:ctx->gpr[67]=spu_il(0);break;
                case 7:ctx->int_enable=1;break;case 8:ctx->irq_saved=1;break;case 9:ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;}
            buzz_geometry_job_i16_output(ctx,i16_output_fallback_call,failure==0?nullptr:i16_output_reference,failure==1?nullptr:finish,failure==2?nullptr:skip);
        }
        assert(i16_output_native==25 && i16_output_checked==17 && i16_output_references==17 && i16_output_fallback==10 && i16_output_fallback_calls==13);
        i16_output_mode=0;flow_mode=1;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;auto* entry=buzz::clone_function_state(ctx);
        auto* fixture=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_4B9C.bin","rb");assert(fixture);
        assert(fread(entry->gpr,sizeof entry->gpr,1,fixture)==1 && fread(entry->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;};
        const auto finish=u8_integer_output_oracle::buzz_job_6eba_spu_func_00004610,skip=u8_integer_output_oracle::buzz_job_6eba_spu_func_00004618;
        for(unsigned bias:{0u,15u})for(unsigned empty:{0u,1u})for(unsigned depth:{0u,2u})for(unsigned mode:{0u,1u,2u}){
            reset();flow_mode=mode==1?2:1;u8_integer_output_mode=mode==2?2:1;ctx->host_depth=depth;ctx->gpr[53]=spu_il(bias);if(empty)ctx->gpr[77]._u32[0]=0;
            buzz_geometry_job_u8_integer_output(ctx,u8_integer_output_fallback_call,u8_integer_output_reference,finish,skip);
            assert(ctx->pc==(empty?0x4618:0x4610) && g_spu_trampoline_fn==(empty?skip:finish) && ctx->host_depth==depth);
        }
        reset();flow_mode=2;u8_integer_output_mode=1;ctx->gpr[60]._u32[0]=256;ctx->gpr[48]=spu_il(16);ctx->gpr[45]=spu_ila(65535);
        buzz_geometry_job_u8_integer_output(ctx,u8_integer_output_fallback_call,u8_integer_output_reference,finish,skip);
        assert(ctx->pc==0x4610 && g_spu_trampoline_fn==finish);
        assert(u8_integer_output_native==25 && u8_integer_output_checked==17 && u8_integer_output_references==17 && !u8_integer_output_fallback && !u8_integer_output_fallback_calls);
        reset();u8_integer_output_mode=0;buzz_geometry_job_u8_integer_output(ctx,u8_integer_output_fallback_call,u8_integer_output_reference,finish,skip);u8_integer_output_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_u8_integer_output(foreign,u8_integer_output_fallback_call,u8_integer_output_reference,finish,skip);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_u8_integer_output(ctx,u8_integer_output_fallback_call,u8_integer_output_reference,finish,skip);
        for(unsigned failure=0;failure<10;++failure){reset();
            switch(failure){case 3:ctx->ls=entry->ls_store;break;case 4:ctx->int_enable=1;ctx->event_status=ctx->event_mask=1;break;case 5:ctx->gpr[60]._u32[0]=257;break;case 6:ctx->gpr[67]=spu_il(0);break;
                case 7:ctx->int_enable=1;break;case 8:ctx->irq_saved=1;break;case 9:ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;}
            buzz_geometry_job_u8_integer_output(ctx,u8_integer_output_fallback_call,failure==0?nullptr:u8_integer_output_reference,failure==1?nullptr:finish,failure==2?nullptr:skip);
        }
        assert(u8_integer_output_native==25 && u8_integer_output_checked==17 && u8_integer_output_references==17 && u8_integer_output_fallback==10 && u8_integer_output_fallback_calls==13);
        u8_integer_output_mode=0;flow_mode=1;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;auto* entry=buzz::clone_function_state(ctx);
        auto* fixture=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_4B9C.bin","rb");assert(fixture);
        assert(fread(entry->gpr,sizeof entry->gpr,1,fixture)==1 && fread(entry->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;};
        const auto finish=unorm_output_oracle::buzz_job_6eba_spu_func_00004610,skip=unorm_output_oracle::buzz_job_6eba_spu_func_00004618;
        for(unsigned bias:{0u,15u})for(unsigned empty:{0u,1u})for(unsigned depth:{0u,2u})for(unsigned mode:{0u,1u,2u}){
            reset();flow_mode=mode==1?2:1;unorm_output_mode=mode==2?2:1;ctx->host_depth=depth;ctx->gpr[53]=spu_il(bias);if(empty)ctx->gpr[77]._u32[0]=0;
            buzz_geometry_job_unorm_output(ctx,unorm_output_fallback_call,unorm_output_reference,finish,skip);
            assert(ctx->pc==(empty?0x4618:0x4610) && g_spu_trampoline_fn==(empty?skip:finish) && ctx->host_depth==depth);
        }
        reset();flow_mode=2;unorm_output_mode=1;ctx->gpr[60]._u32[0]=256;ctx->gpr[48]=spu_il(16);ctx->gpr[45]=spu_ila(65535);
        buzz_geometry_job_unorm_output(ctx,unorm_output_fallback_call,unorm_output_reference,finish,skip);
        assert(ctx->pc==0x4610 && g_spu_trampoline_fn==finish);
        assert(unorm_output_native==25 && unorm_output_checked==17 && unorm_output_references==17 && !unorm_output_fallback && !unorm_output_fallback_calls);
        reset();unorm_output_mode=0;buzz_geometry_job_unorm_output(ctx,unorm_output_fallback_call,unorm_output_reference,finish,skip);unorm_output_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_unorm_output(foreign,unorm_output_fallback_call,unorm_output_reference,finish,skip);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_unorm_output(ctx,unorm_output_fallback_call,unorm_output_reference,finish,skip);
        for(unsigned failure=0;failure<10;++failure){reset();
            switch(failure){case 3:ctx->ls=entry->ls_store;break;case 4:ctx->int_enable=1;ctx->event_status=ctx->event_mask=1;break;case 5:ctx->gpr[60]._u32[0]=257;break;case 6:ctx->gpr[67]=spu_il(0);break;
                case 7:ctx->int_enable=1;break;case 8:ctx->irq_saved=1;break;case 9:ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;}
            buzz_geometry_job_unorm_output(ctx,unorm_output_fallback_call,failure==0?nullptr:unorm_output_reference,failure==1?nullptr:finish,failure==2?nullptr:skip);
        }
        assert(unorm_output_native==25 && unorm_output_checked==17 && unorm_output_references==17 && unorm_output_fallback==10 && unorm_output_fallback_calls==13);
        unorm_output_mode=0;flow_mode=1;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;auto* entry=buzz::clone_function_state(ctx);
        auto* fixture=fopen("analysis/native-geometry/phase3/snorm-output-v1-fixture/pc_4478.bin","rb");assert(fixture);
        assert(fread(entry->gpr,sizeof entry->gpr,1,fixture)==1 && fread(entry->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;};
        const auto finish=snorm_output_oracle::buzz_job_6eba_spu_func_00004610,skip=snorm_output_oracle::buzz_job_6eba_spu_func_00004618;
        for(unsigned bias:{0u,15u})for(unsigned empty:{0u,1u})for(unsigned depth:{0u,2u})for(unsigned mode:{0u,1u,2u}){
            reset();flow_mode=mode==1?2:1;snorm_output_mode=mode==2?2:1;ctx->host_depth=depth;ctx->gpr[53]=spu_il(bias);if(empty)ctx->gpr[77]._u32[0]=0;
            buzz_geometry_job_snorm_output(ctx,snorm_output_fallback_call,snorm_output_reference,finish,skip);
            assert(ctx->pc==(empty?0x4618:0x4610) && g_spu_trampoline_fn==(empty?skip:finish) && ctx->host_depth==depth);
        }
        reset();flow_mode=2;snorm_output_mode=1;ctx->gpr[60]._u32[0]=256;ctx->gpr[48]=spu_il(16);ctx->gpr[45]=spu_ila(65535);
        buzz_geometry_job_snorm_output(ctx,snorm_output_fallback_call,snorm_output_reference,finish,skip);
        assert(ctx->pc==0x4610 && g_spu_trampoline_fn==finish);
        assert(snorm_output_native==25 && snorm_output_checked==17 && snorm_output_references==17 && !snorm_output_fallback && !snorm_output_fallback_calls);
        reset();snorm_output_mode=0;buzz_geometry_job_snorm_output(ctx,snorm_output_fallback_call,snorm_output_reference,finish,skip);snorm_output_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_snorm_output(foreign,snorm_output_fallback_call,snorm_output_reference,finish,skip);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_snorm_output(ctx,snorm_output_fallback_call,snorm_output_reference,finish,skip);
        for(unsigned failure=0;failure<10;++failure){reset();
            switch(failure){case 3:ctx->ls=entry->ls_store;break;case 4:ctx->int_enable=1;ctx->event_status=ctx->event_mask=1;break;case 5:ctx->gpr[60]._u32[0]=257;break;case 6:ctx->gpr[67]=spu_il(0);break;
                case 7:ctx->int_enable=1;break;case 8:ctx->irq_saved=1;break;case 9:ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;}
            buzz_geometry_job_snorm_output(ctx,snorm_output_fallback_call,failure==0?nullptr:snorm_output_reference,failure==1?nullptr:finish,failure==2?nullptr:skip);
        }
        assert(snorm_output_native==25 && snorm_output_checked==17 && snorm_output_references==17 && snorm_output_fallback==10 && snorm_output_fallback_calls==13);
        snorm_output_mode=0;flow_mode=1;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;auto* entry=buzz::clone_function_state(ctx);
        auto* fixture=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_4FDC.bin","rb");assert(fixture);
        assert(fread(entry->gpr,sizeof entry->gpr,1,fixture)==1 && fread(entry->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;};
        const auto finish=cmp_output_oracle::buzz_job_6eba_spu_func_00004610,skip=cmp_output_oracle::buzz_job_6eba_spu_func_00004618;
        for(unsigned bias:{0u,15u})for(unsigned empty:{0u,1u})for(unsigned depth:{0u,2u})for(unsigned mode:{0u,1u,2u}){
            reset();flow_mode=mode==1?2:1;cmp_output_mode=mode==2?2:1;ctx->host_depth=depth;ctx->gpr[53]=spu_il(bias);if(empty)ctx->gpr[77]._u32[0]=0;
            buzz_geometry_job_cmp_output(ctx,cmp_output_fallback_call,cmp_output_reference,finish,skip);
            assert(ctx->pc==(empty?0x4618:0x4610) && g_spu_trampoline_fn==(empty?skip:finish) && ctx->host_depth==depth);
        }
        reset();flow_mode=2;cmp_output_mode=1;ctx->gpr[60]._u32[0]=256;ctx->gpr[48]=spu_il(16);ctx->gpr[45]=spu_ila(65535);
        buzz_geometry_job_cmp_output(ctx,cmp_output_fallback_call,cmp_output_reference,finish,skip);
        assert(ctx->pc==0x4610 && g_spu_trampoline_fn==finish);
        assert(cmp_output_native==25 && cmp_output_checked==17 && cmp_output_references==17 && !cmp_output_fallback && !cmp_output_fallback_calls);
        reset();cmp_output_mode=0;buzz_geometry_job_cmp_output(ctx,cmp_output_fallback_call,cmp_output_reference,finish,skip);cmp_output_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_cmp_output(foreign,cmp_output_fallback_call,cmp_output_reference,finish,skip);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_cmp_output(ctx,cmp_output_fallback_call,cmp_output_reference,finish,skip);
        for(unsigned failure=0;failure<10;++failure){reset();
            switch(failure){case 3:ctx->ls=entry->ls_store;break;case 4:ctx->int_enable=1;ctx->event_status=ctx->event_mask=1;break;case 5:ctx->gpr[60]._u32[0]=257;break;case 6:ctx->gpr[67]=spu_il(0);break;
                case 7:ctx->int_enable=1;break;case 8:ctx->irq_saved=1;break;case 9:ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;}
            buzz_geometry_job_cmp_output(ctx,cmp_output_fallback_call,failure==0?nullptr:cmp_output_reference,failure==1?nullptr:finish,failure==2?nullptr:skip);
        }
        assert(cmp_output_native==25 && cmp_output_checked==17 && cmp_output_references==17 && cmp_output_fallback==10 && cmp_output_fallback_calls==13);
        cmp_output_mode=0;flow_mode=1;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;auto* entry=buzz::clone_function_state(ctx);
        auto* fixture=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_4880.bin","rb");assert(fixture);
        assert(fread(entry->gpr,sizeof entry->gpr,1,fixture)==1 && fread(entry->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;};
        const auto finish=half_output_oracle::buzz_job_6eba_spu_func_00004610,skip=half_output_oracle::buzz_job_6eba_spu_func_00004618;
        for(unsigned bias:{0u,15u})for(unsigned empty:{0u,1u})for(unsigned depth:{0u,2u})for(unsigned mode:{0u,1u,2u}){
            reset();flow_mode=mode==1?2:1;half_output_mode=mode==2?2:1;ctx->host_depth=depth;ctx->gpr[53]=spu_il(bias);if(empty)ctx->gpr[77]._u32[0]=0;
            buzz_geometry_job_half_output(ctx,half_output_fallback_call,half_output_reference,finish,skip);
            assert(ctx->pc==(empty?0x4618:0x4610) && g_spu_trampoline_fn==(empty?skip:finish) && ctx->host_depth==depth);
        }
        reset();flow_mode=2;half_output_mode=1;ctx->gpr[60]._u32[0]=256;ctx->gpr[48]=spu_il(16);ctx->gpr[45]=spu_ila(65535);
        buzz_geometry_job_half_output(ctx,half_output_fallback_call,half_output_reference,finish,skip);
        assert(ctx->pc==0x4610 && g_spu_trampoline_fn==finish);
        assert(half_output_native==25 && half_output_checked==17 && half_output_references==17 && !half_output_fallback && !half_output_fallback_calls);
        reset();half_output_mode=0;buzz_geometry_job_half_output(ctx,half_output_fallback_call,half_output_reference,finish,skip);half_output_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_half_output(foreign,half_output_fallback_call,half_output_reference,finish,skip);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_half_output(ctx,half_output_fallback_call,half_output_reference,finish,skip);
        for(unsigned failure=0;failure<10;++failure){reset();
            switch(failure){case 3:ctx->ls=entry->ls_store;break;case 4:ctx->int_enable=1;ctx->event_status=ctx->event_mask=1;break;case 5:ctx->gpr[60]._u32[0]=257;break;case 6:ctx->gpr[67]=spu_il(0);break;
                case 7:ctx->int_enable=1;break;case 8:ctx->irq_saved=1;break;case 9:ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;}
            buzz_geometry_job_half_output(ctx,half_output_fallback_call,failure==0?nullptr:half_output_reference,failure==1?nullptr:finish,failure==2?nullptr:skip);
        }
        assert(half_output_native==25 && half_output_checked==17 && half_output_references==17 && half_output_fallback==10 && half_output_fallback_calls==13);
        half_output_mode=0;flow_mode=1;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;auto* entry=buzz::clone_function_state(ctx);
        auto* fixture=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_46D0.bin","rb");assert(fixture);
        assert(fread(entry->gpr,sizeof entry->gpr,1,fixture)==1 && fread(entry->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;};
        const auto finish=float_output_oracle::buzz_job_6eba_spu_func_00004610,skip=float_output_oracle::buzz_job_6eba_spu_func_00004618;
        for(unsigned bias:{0u,12u})for(unsigned empty:{0u,1u})for(unsigned depth:{0u,2u})for(unsigned mode:{0u,1u,2u}){
            reset();flow_mode=mode==1?2:1;float_output_mode=mode==2?2:1;ctx->host_depth=depth;ctx->gpr[53]=spu_il(bias);if(empty)ctx->gpr[77]._u32[0]=0;
            buzz_geometry_job_float_output(ctx,float_output_fallback_call,float_output_reference,finish,skip);
            assert(ctx->pc==(empty?0x4618:0x4610) && g_spu_trampoline_fn==(empty?skip:finish) && ctx->host_depth==depth);
        }
        assert(float_output_native==24 && float_output_checked==16 && float_output_references==16 && !float_output_fallback && !float_output_fallback_calls);
        reset();float_output_mode=0;buzz_geometry_job_float_output(ctx,float_output_fallback_call,float_output_reference,finish,skip);float_output_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_float_output(foreign,float_output_fallback_call,float_output_reference,finish,skip);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_float_output(ctx,float_output_fallback_call,float_output_reference,finish,skip);
        for(unsigned failure=0;failure<10;++failure){reset();
            switch(failure){case 3:ctx->ls=entry->ls_store;break;case 4:ctx->int_enable=1;ctx->event_status=ctx->event_mask=1;break;case 5:ctx->gpr[60]._u32[0]=257;break;case 6:ctx->gpr[67]=spu_il(0);break;
                case 7:ctx->int_enable=1;break;case 8:ctx->irq_saved=1;break;case 9:ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;}
            buzz_geometry_job_float_output(ctx,float_output_fallback_call,failure==0?nullptr:float_output_reference,failure==1?nullptr:finish,failure==2?nullptr:skip);
        }
        assert(float_output_native==24 && float_output_checked==16 && float_output_references==16 && float_output_fallback==10 && float_output_fallback_calls==13);
        float_output_mode=0;flow_mode=1;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        ctx->int_enable=0;ctx->irq_saved=0;ctx->irq_frame=nullptr;ctx->policy_mode=0;ctx->resident_ovl=0;
        ctx->gpr[1]=spu_ila(0x30000);ctx->gpr[3]=spu_ila(0x20000);ctx->gpr[4]=spu_ila(0x100003);ctx->gpr[5]=spu_ila(0x123);ctx->gpr[6]=spu_ila(0x21000);
        auto* entry=buzz::clone_function_state(ctx);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;};
        const buzz_geometry_ring_functions f{ring_oracle::buzz_job_6eba_spu_func_000038D0,ring_oracle::buzz_job_6eba_spu_func_00003DD0,ring_oracle::buzz_job_6eba_spu_func_00004078};
        ring_mode=1;
        for(unsigned bias:{0u,3u,8u,15u})for(unsigned depth:{0u,2u})for(unsigned mode:{1u,2u}){
            reset();flow_mode=mode;ctx->gpr[1]._u32[0]+=bias;ctx->gpr[6]._u32[0]+=bias;ctx->host_depth=depth;ctx->gpr[0]._u32[0]=0x38d0;
            auto* expected=buzz::clone_function_state(ctx);ring_oracle::buzz_job_6eba_spu_func_000037C0(expected);g_spu_trampoline_fn=nullptr;ring_oracle::buzz_job_6eba_spu_func_00003800(expected);g_spu_trampoline_fn=nullptr;ring_oracle::buzz_job_6eba_spu_func_000038D0(expected);const auto fn=g_spu_trampoline_fn;
            g_spu_trampoline_fn=spu_indirect_branch;buzz_geometry_job_ring(ctx,ring_reference_call,&f);
            assert(buzz::compare_function_state(*ctx,*expected).exact() && g_spu_trampoline_fn==fn);buzz::release_function_state(expected);
        }
        assert(ring_native==16 && !ring_checked && !ring_fallback && !ring_references);
        reset();flow_mode=1;ring_mode=0;buzz_geometry_job_ring(ctx,ring_reference_call,&f);ring_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_ring(foreign,ring_reference_call,&f);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_ring(ctx,ring_reference_call,&f);
        for(unsigned failure=0;failure<11;++failure){
            reset();auto functions=f;
            switch(failure){case 1:functions.tail=nullptr;break;case 2:functions.initial_report=nullptr;break;case 3:functions.shared_report=nullptr;break;case 4:ctx->ls=entry->ls_store;break;case 5:ctx->image_id=40;break;case 6:ctx->policy_mode=1;break;case 7:ctx->resident_ovl=1;break;case 8:ctx->int_enable=1;break;case 9:ctx->irq_saved=1;break;case 10:ctx->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;}
            buzz_geometry_job_ring(ctx,ring_reference_call,failure==0?nullptr:&functions);
        }
        // Foreign image is an outer routing rejection, not an owned fallback.
        assert(ring_native==16 && ring_fallback==10 && ring_references==14 && !ring_checked);
        ring_mode=0;flow_mode=1;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }

    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;auto* entry=buzz::clone_function_state(ctx);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;};
        const auto finish=alternate_cull_oracle::buzz_job_6eba_spu_func_00006958;
        for(unsigned n:{1u,2u,4u})for(unsigned depth=0;depth<2;++depth)for(unsigned mode=0;mode<3;++mode){
            reset();ctx->gpr[15]._u32[0]=n-2;ctx->host_depth=depth?2:0;ctx->gpr[37]._u32[0]=0xffffffff;
            flow_mode=mode==1?2:1;alternate_cull_mode=mode==2?2:1;
            buzz_geometry_job_alternate_cull(ctx,alternate_cull_reference,finish);assert(ctx->pc==0x6958 && g_spu_trampoline_fn==finish && ctx->host_depth==(depth?2:0));
        }
        assert(alternate_cull_native==18 && alternate_cull_checked==12 && alternate_cull_references==12 && !alternate_cull_fallback && !alternate_cull_fallback_calls);
        flow_mode=1;reset();alternate_cull_mode=0;buzz_geometry_job_alternate_cull(ctx,alternate_cull_fallback_call,finish);alternate_cull_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_alternate_cull(foreign,alternate_cull_fallback_call,finish);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_alternate_cull(ctx,alternate_cull_fallback_call,finish);
        for(unsigned failure=0;failure<5;++failure){reset();ctx->gpr[15]._u32[0]=0;switch(failure){case 0:ctx->ls=entry->ls_store;break;case 1:ctx->int_enable=1;ctx->event_status=ctx->event_mask=1;break;case 2:ctx->gpr[15]._u32[0]=0xfffffffe;break;case 3:ctx->gpr[15]._u32[0]=8191;break;}
            buzz_geometry_job_alternate_cull(ctx,alternate_cull_fallback_call,failure==4?nullptr:finish);
        }
        assert(alternate_cull_fallback_calls==8 && alternate_cull_references==12 && alternate_cull_fallback==5 && alternate_cull_native==18 && alternate_cull_checked==12);
        alternate_cull_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }

    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        ctx->gpr[1]=spu_ila(0x39005);ctx->gpr[3]=spu_ila(0x3d007);auto* entry=buzz::clone_function_state(ctx);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;};
        const unsigned modes[]={0,0,4,1,2,3,0x80000003};
        for(unsigned shape=0;shape<7;++shape)for(unsigned depth=0;depth<2;++depth)for(unsigned mode=0;mode<3;++mode){
            reset();transform_control_expected_cull_pc=shape==6?0x69e8:0x6e08;for(auto& word:ctx->gpr[4]._u32)word=modes[shape];ctx->gpr[0]=spu_ila(0x6608);ctx->host_depth=depth?2:0;
            u128 config=spu_ls_read128(ctx,0x3d000);config._u32[3]=shape==0?0xffffffff:17;spu_ls_write128(ctx,0x3d000,config);ctx->ls[0x3d000]=16;ctx->ls[0x3d140]=4;
            flow_mode=mode==1?2:1;transform_control_mode=mode==2?2:1;
            buzz_geometry_job_transform_control(ctx,transform_control_fallback_call,transform_control_reference,&transform_control_library);
            assert(ctx->gpr[1]._u32[0]==0x39005 && ctx->pc==ctx->gpr[0]._u32[0] && ctx->host_depth==(depth?2:0));
            assert(g_spu_trampoline_fn==(depth?nullptr:spu_indirect_branch));
        }
        assert(transform_control_native==42 && transform_control_checked==28 && transform_control_references==28 && !transform_control_fallback && !transform_control_fallback_calls);
        flow_mode=1;reset();transform_control_mode=0;buzz_geometry_job_transform_control(ctx,transform_control_fallback_call,transform_control_reference,&transform_control_library);transform_control_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_transform_control(foreign,transform_control_fallback_call,transform_control_reference,&transform_control_library);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_transform_control(ctx,transform_control_fallback_call,transform_control_reference,&transform_control_library);
        for(unsigned failure=0;failure<16;++failure){reset();ctx->gpr[4]=spu_ila(1);ctx->ls[0x3d000]=16;auto functions=transform_control_library;
            switch(failure){case 0:ctx->ls=entry->ls_store;break;case 1:ctx->int_enable=1;ctx->event_status=ctx->event_mask=1;break;case 2:ctx->gpr[3]._u32[0]=0;break;
            case 3:ctx->gpr[3]._u32[0]=SPU_LS_SIZE-0x180;break;case 4:ctx->gpr[1]._u32[0]=0xe680;break;case 5:ctx->gpr[1]._u32[0]=SPU_LS_SIZE-16;break;
            case 6:ctx->gpr[1]._u32[0]=SPU_LS_SIZE;break;case 7:ctx->gpr[1]._u32[0]=0x3d100;break;case 8:ctx->ls[0x3d000]=0;break;
            case 11:functions.vertex=nullptr;break;case 12:functions.cull=nullptr;break;case 13:functions.alternate=nullptr;break;case 14:functions.finish=nullptr;break;case 15:functions.restore=nullptr;break;}
            buzz_geometry_job_transform_control(ctx,transform_control_fallback_call,failure==9?nullptr:transform_control_reference,failure==10?nullptr:&functions);
        }
        assert(transform_control_fallback_calls==19 && transform_control_references==28 && transform_control_fallback==16 && transform_control_native==42 && transform_control_checked==28);
        transform_control_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }

    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        ctx->gpr[3]=spu_ila(0x3d005);auto* entry=buzz::clone_function_state(ctx);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=spu_indirect_branch;};
        const unsigned formats[]={2,4,5,255};
        const auto vertex=vertex_prepare_oracle::buzz_job_6eba_spu_func_000063E0;
        for(unsigned format:formats)for(unsigned depth=0;depth<2;++depth)for(unsigned mode=0;mode<3;++mode){
            reset();ctx->ls[0x3d140]=format;ctx->host_depth=depth?2:0;
            flow_mode=mode==1?2:1;vertex_prepare_mode=mode==2?2:1;
            buzz_geometry_job_vertex_prepare(ctx,vertex_prepare_reference,vertex);
            assert(ctx->pc==0x63e0 && g_spu_trampoline_fn==vertex && ctx->host_depth==(depth?2:0));
        }
        assert(vertex_prepare_native==24 && vertex_prepare_checked==16 && vertex_prepare_references==16 && !vertex_prepare_fallback && !vertex_prepare_fallback_calls);
        flow_mode=1;reset();vertex_prepare_mode=0;buzz_geometry_job_vertex_prepare(ctx,vertex_prepare_fallback_call,vertex);vertex_prepare_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_vertex_prepare(foreign,vertex_prepare_fallback_call,vertex);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_vertex_prepare(ctx,vertex_prepare_fallback_call,vertex);
        for(unsigned failure=0;failure<7;++failure){reset();switch(failure){case 0:ctx->ls=entry->ls_store;break;
            case 1:ctx->int_enable=1;ctx->event_status=ctx->event_mask=1;break;case 2:ctx->gpr[3]._u32[0]=0;break;
            case 3:ctx->gpr[3]._u32[0]=0xe670;break;case 4:ctx->gpr[3]._u32[0]=SPU_LS_SIZE;break;case 5:ctx->gpr[3]._u32[0]=SPU_LS_SIZE-0x140;break;}
            buzz_geometry_job_vertex_prepare(ctx,vertex_prepare_fallback_call,failure==6?nullptr:vertex);
        }
        assert(vertex_prepare_fallback_calls==10 && vertex_prepare_references==16 && vertex_prepare_fallback==7 && vertex_prepare_native==24 && vertex_prepare_checked==16);
        vertex_prepare_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }

    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        ctx->gpr[1]=spu_ila(0x39005-480);ctx->gpr[81]=spu_ila(0x3d007);ctx->gpr[50]=spu_il(0x55555556);
        auto* entry=buzz::clone_function_state(ctx);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;};
        for(unsigned publish=0;publish<2;++publish)for(unsigned depth=0;depth<2;++depth)for(unsigned mode=0;mode<3;++mode){
            reset();ctx->host_depth=depth?2:0;ctx->ls[0x3d000]=publish?0:16;
            // Return links name an interior block; aliases may also overwrite
            // that saved link before restoration. Compare actual return FN.
            ctx->gpr[0]=spu_ila(0x66c8);spu_ls_write128(ctx,0x39015,ctx->gpr[0]);
            if(depth){ctx->gpr[1]._u32[0]=0x3d050-480-16+5;spu_ls_write128(ctx,0x3d055,ctx->gpr[0]);}
            const unsigned stack=ctx->gpr[1]._u32[0]+480;
            flow_mode=mode==1?2:1;transform_finish_mode=mode==2?2:1;
            buzz_geometry_job_transform_finish(ctx,transform_finish_reference);
            assert(ctx->gpr[1]._u32[0]==stack && ctx->pc==ctx->gpr[0]._u32[0] && ctx->host_depth==(depth?2:0));
            assert(g_spu_trampoline_fn==(depth?nullptr:spu_indirect_branch));
        }
        assert(transform_finish_native==12 && transform_finish_checked==8 && transform_finish_references==8 && !transform_finish_fallback && !transform_finish_fallback_calls);
        flow_mode=1;reset();transform_finish_mode=0;buzz_geometry_job_transform_finish(ctx,transform_finish_fallback_call);transform_finish_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_transform_finish(foreign,transform_finish_fallback_call);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_transform_finish(ctx,transform_finish_fallback_call);
        for(unsigned failure=0;failure<6;++failure){reset();switch(failure){case 0:ctx->ls=entry->ls_store;break;
            case 1:ctx->int_enable=1;ctx->event_status=ctx->event_mask=1;break;case 2:ctx->gpr[81]._u32[0]=0;break;
            case 3:ctx->gpr[81]._u32[0]=0xe670;break;case 4:ctx->gpr[81]._u32[0]=SPU_LS_SIZE;break;case 5:ctx->gpr[81]._u32[0]=SPU_LS_SIZE-0x180;break;}
            buzz_geometry_job_transform_finish(ctx,transform_finish_fallback_call);
        }
        assert(transform_finish_fallback_calls==9 && transform_finish_references==8 && transform_finish_fallback==6 && transform_finish_native==12 && transform_finish_checked==8);
        transform_finish_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        index_control_oracle::seed_tables(*ctx);ctx->gpr[1]=spu_ila(0x3d005);ctx->gpr[3]=spu_ila(0x39005);ctx->gpr[4]=spu_ila(0x10000);
        auto* entry=buzz::clone_function_state(ctx);const auto& functions=index_control_oracle::library;
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;
            ctx->ls[0x3917c]=0;ctx->ls[0x3917d]=3;ctx->ls[0x3917e]=0x80;ctx->ls[0x3917f]=0;
            ctx->ls[0x39180]=0;ctx->ls[0x39181]=12;ctx->ls[0x39006]=0x30;
            memset(ctx->ls+0x10000,0,16);ctx->ls[0x10001]=32;ctx->ls[0x10005]=2;ctx->ls[0x10006]=4;
        };
        for(unsigned shape=0;shape<5;++shape)for(unsigned depth=0;depth<2;++depth)for(unsigned mode=0;mode<3;++mode){
            reset();ctx->host_depth=depth?2:0;ctx->gpr[0]._u32[0]=depth?0xaf10:0xaea0;
            if(shape==0)ctx->ls[0x39181]=0;else if(shape==1)ctx->ls[0x39006]=0xf0;
            else if(shape==2){ctx->ls[0x10001]=0;ctx->ls[0x10006]=0;}else if(shape==4)ctx->ls[0x10006]=12;
            flow_mode=mode==1?2:1;index_control_mode=mode==2?2:1;
            g_spu_trampoline_fn=index_control_oracle::buzz_job_6eba_spu_func_0000AEE8;
            buzz_geometry_job_index_control(ctx,index_control_fallback_call,index_control_reference,&functions);
            assert(ctx->pc==(depth?0xaf10u:0xaea0u) && ctx->gpr[1]._u32[0]==0x3d005 && ctx->host_depth==(depth?2:0));
            assert(g_spu_trampoline_fn==(depth?nullptr:spu_indirect_branch));
        }
        assert(index_control_native==30 && index_control_checked==20 && index_control_references==20 && !index_control_fallback && !index_control_fallback_calls);
        flow_mode=1;reset();index_control_mode=0;buzz_geometry_job_index_control(ctx,index_control_fallback_call,index_control_reference,&functions);index_control_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_index_control(foreign,index_control_fallback_call,index_control_reference,&functions);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_index_control(ctx,index_control_fallback_call,index_control_reference,&functions);
        for(unsigned failure=0;failure<15;++failure){reset();auto f=functions;const buzz_geometry_index_functions* fs=&f;auto ref=index_control_reference;
            switch(failure){case 0:fs=nullptr;break;case 1:ref=nullptr;break;case 2:f.unpack=nullptr;break;case 3:f.deltas=nullptr;break;
            case 4:f.expansion_setup=nullptr;break;case 5:f.expand=nullptr;break;case 6:f.relocate=nullptr;break;case 7:f.triangles=nullptr;break;case 8:f.publish=nullptr;break;
            case 9:ctx->gpr[4]._u32[0]++;break;case 10:memset(ctx->ls+0x3917c,255,4);break;case 11:ctx->ls=entry->ls_store;break;
            case 12:ctx->int_enable=1;ctx->event_status=ctx->event_mask=1;break;case 13:ctx->gpr[3]._u32[0]=0;break;case 14:ctx->gpr[1]._u32[0]=0;break;}
            buzz_geometry_job_index_control(ctx,index_control_fallback_call,ref,fs);
        }
        assert(index_control_fallback_calls==18 && index_control_references==20 && index_control_fallback==15 && index_control_native==30 && index_control_checked==20);
        index_control_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        index_triangles_oracle::seed_tables(*ctx);ctx->gpr[83]=spu_ls_read128(ctx,0xd980);ctx->gpr[85]=spu_ilh(0x303);
        ctx->gpr[18]=spu_ila(0xe805);ctx->gpr[5]=spu_il(0);ctx->gpr[20]=spu_ila(0x1a005);ctx->gpr[31]=spu_ila(0x16002);
        ctx->gpr[24]=spu_ila(0xd690);ctx->gpr[27]=spu_ila(0xd790);ctx->gpr[28]=spu_il(-2);ctx->gpr[22]=spu_il(8);
        ctx->gpr[33]=spu_ls_read128(ctx,0xd180);ctx->gpr[34]=spu_ls_read128(ctx,0xd170);ctx->gpr[35]=spu_ls_read128(ctx,0xd160);
        ctx->gpr[36]=spu_ila(0xf000);ctx->gpr[37]=spu_il(3840);ctx->gpr[38]=spu_ila(0xaaaa);ctx->gpr[1]=spu_ila(0x39000-432);
        auto* entry=buzz::clone_function_state(ctx);const auto tail=index_triangles_oracle::buzz_job_6eba_spu_func_00002738;
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;};
        for(unsigned shape=0;shape<3;++shape)for(unsigned alias=0;alias<2;++alias)for(unsigned mode=0;mode<3;++mode){
            reset();ctx->gpr[22]._u32[0]=shape==0?1:shape==1?3:8;ctx->gpr[0]._u32[0]=0x2738;
            if(alias){ctx->gpr[24]._u32[0]=ctx->gpr[27]._u32[0]=ctx->gpr[18]._u32[0];ctx->gpr[31]._u32[0]=ctx->gpr[18]._u32[0]+2;
                ctx->gpr[1]._u32[0]=ctx->gpr[18]._u32[0]+0x190-432;}
            flow_mode=mode==1?2:1;index_triangles_mode=mode==2?2:1;
            buzz_geometry_job_index_triangles(ctx,index_triangles_fallback_call,index_triangles_reference,tail);
            assert(ctx->pc==0x2738 && g_spu_trampoline_fn==tail);
        }
        assert(index_triangles_native==18 && index_triangles_checked==12 && index_triangles_references==12 && !index_triangles_fallback && !index_triangles_fallback_calls);
        flow_mode=1;reset();index_triangles_mode=0;buzz_geometry_job_index_triangles(ctx,index_triangles_fallback_call,index_triangles_reference,tail);index_triangles_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_index_triangles(foreign,index_triangles_fallback_call,index_triangles_reference,tail);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_index_triangles(ctx,index_triangles_fallback_call,index_triangles_reference,tail);
        for(unsigned failure=0;failure<9;++failure){reset();auto t=tail;auto ref=index_triangles_reference;switch(failure){
            case 0:ref=nullptr;break;case 1:t=nullptr;break;case 2:ctx->gpr[18]._u32[0]=0;break;case 3:ctx->gpr[18]._u32[0]=0xe670;break;
            case 4:ctx->gpr[18]._u32[0]=SPU_LS_SIZE;break;case 5:ctx->gpr[22]._u32[0]=0;break;case 6:ctx->gpr[22]._u32[0]=0xffffffff;break;
            case 7:ctx->gpr[18]._u32[0]=SPU_LS_SIZE-32;break;case 8:ctx->ls=entry->ls_store;break;}
            buzz_geometry_job_index_triangles(ctx,index_triangles_fallback_call,ref,t);
        }
        assert(index_triangles_fallback_calls==12 && index_triangles_references==12 && index_triangles_fallback==9 && index_triangles_native==18 && index_triangles_checked==12);
        index_triangles_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        index_relocate_oracle::seed_tables(*ctx);ctx->gpr[83]=spu_ls_read128(ctx,0xd980);ctx->gpr[85]=spu_ilh(0x303);
        ctx->gpr[80]=spu_ila(0x19ff5);ctx->gpr[81]=spu_ila(0xe805);ctx->gpr[87]=spu_il(65);
        auto* entry=buzz::clone_function_state(ctx);const auto tail=index_relocate_oracle::buzz_job_6eba_spu_func_00007A90;
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;};
        for(unsigned shape=0;shape<3;++shape)for(unsigned alias=0;alias<2;++alias)for(unsigned mode=0;mode<3;++mode){
            reset();ctx->gpr[87]._u32[0]=shape==0?0:shape==1?16:65;ctx->gpr[0]._u32[0]=0x7a90;
            if(alias)ctx->gpr[80]._u32[0]=ctx->gpr[81]._u32[0]-16;
            flow_mode=mode==1?2:1;index_relocate_mode=mode==2?2:1;
            buzz_geometry_job_index_relocate(ctx,index_relocate_oracle::buzz_job_6eba_spu_func_000079A8,tail);
            assert(ctx->pc==0x7a90 && g_spu_trampoline_fn==tail);
        }
        assert(index_relocate_native==18 && index_relocate_checked==12 && !index_relocate_fallback && !index_relocate_references);
        flow_mode=1;reset();index_relocate_mode=0;buzz_geometry_job_index_relocate(ctx,index_relocate_reference,tail);index_relocate_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_index_relocate(foreign,index_relocate_reference,tail);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_index_relocate(ctx,index_relocate_reference,tail);
        for(unsigned failure=0;failure<7;++failure){reset();auto t=tail;switch(failure){
            case 0:t=nullptr;break;case 1:ctx->gpr[81]._u32[0]=0;break;case 2:ctx->gpr[81]._u32[0]=0xe670;break;
            case 3:ctx->gpr[81]._u32[0]=SPU_LS_SIZE;break;case 4:ctx->gpr[87]._u32[0]=0xffffffff;break;case 5:ctx->gpr[81]._u32[0]=SPU_LS_SIZE-16;break;case 6:ctx->ls=entry->ls_store;break;}
            buzz_geometry_job_index_relocate(ctx,index_relocate_reference,t);
        }
        assert(index_relocate_references==10 && index_relocate_fallback==7 && index_relocate_native==18 && index_relocate_checked==12);
        index_relocate_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        index_expand_oracle::seed_tables(*ctx);ctx->gpr[83]=spu_ls_read128(ctx,0xd980);ctx->gpr[85]=spu_ilh(0x303);
        ctx->gpr[92]=spu_ila(0xe800);ctx->gpr[80]=spu_il(0);ctx->gpr[82]=spu_il(32);ctx->gpr[81]=spu_ila(0x16002);ctx->gpr[88]=spu_ila(0x1a000);
        ctx->gpr[95]=spu_ila(0x3d000);ctx->gpr[87]=spu_il(0);ctx->gpr[102]=spu_il(8);
        auto* entry=buzz::clone_function_state(ctx);const auto normal=index_expand_oracle::buzz_job_6eba_spu_func_000079A8,error=index_expand_oracle::buzz_job_6eba_spu_func_00007DCC;assert(normal!=error);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;};
        for(unsigned shape=0;shape<3;++shape)for(unsigned fault=0;fault<2;++fault)for(unsigned mode=0;mode<3;++mode){
            reset();const unsigned count=shape==0?1:shape==1?3:8;ctx->gpr[82]._u32[0]=(count-1)*16;ctx->gpr[0]._u32[0]=fault?0x7dcc:0x79a8;
            const unsigned limit=fault?0:0x3ff00;for(unsigned b=0;b<4;++b)ctx->ls[0x3d17c+b]=uint8_t(limit>>(24-b*8));
            if(shape==2){ctx->gpr[88]._u32[0]=0xe800;ctx->gpr[81]._u32[0]=0xe802;}
            flow_mode=mode==1?2:1;index_expand_mode=mode==2?2:1;
            buzz_geometry_job_index_expand(ctx,index_expand_oracle::buzz_job_6eba_spu_func_00007838,normal,error);
            assert(ctx->pc==(fault?0x7dccu:0x79a8u) && g_spu_trampoline_fn==(fault?error:normal));
        }
        assert(index_expand_native==18 && index_expand_checked==12 && !index_expand_fallback && !index_expand_references);
        flow_mode=1;reset();index_expand_mode=0;buzz_geometry_job_index_expand(ctx,index_expand_reference,normal,error);index_expand_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_index_expand(foreign,index_expand_reference,normal,error);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_index_expand(ctx,index_expand_reference,normal,error);
        for(unsigned failure=0;failure<8;++failure){reset();auto n=normal,e=error;switch(failure){
            case 0:n=nullptr;break;case 1:e=nullptr;break;case 2:ctx->gpr[92]._u32[0]=0;break;case 3:ctx->gpr[92]._u32[0]=0xe670;break;
            case 4:ctx->gpr[92]._u32[0]=SPU_LS_SIZE;break;case 5:ctx->gpr[82]._u32[0]=0xffffffff;break;case 6:ctx->gpr[92]._u32[0]=SPU_LS_SIZE-16;break;case 7:ctx->ls=entry->ls_store;break;}
            buzz_geometry_job_index_expand(ctx,index_expand_reference,n,e);
        }
        assert(index_expand_references==11 && index_expand_fallback==8 && index_expand_native==18 && index_expand_checked==12);
        index_expand_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        index_unpack_oracle::seed_tables(*ctx);ctx->gpr[83]=spu_ls_read128(ctx,0xd980);ctx->gpr[85]=spu_ilh(0x303);ctx->gpr[99]=spu_ls_read128(ctx,0xda30);ctx->gpr[101]=spu_ila(65535);
        ctx->gpr[88]=spu_ila(0xe800);ctx->gpr[94]=spu_ila(0xe810);ctx->gpr[93]=spu_ila(0xe820);ctx->gpr[86]=spu_ila(0xe830);ctx->gpr[98]=spu_ila(0x18000);
        ctx->gpr[96]=spu_il(32);ctx->gpr[97]=ctx->gpr[80]=spu_il(0);ctx->gpr[81]=spu_ilh(4);ctx->gpr[82]=spu_il(4);ctx->gpr[90]=spu_il(0x04040404);
        auto* entry=buzz::clone_function_state(ctx);const auto deltas=index_unpack_oracle::buzz_job_6eba_spu_func_00007730,expansion=index_unpack_oracle::buzz_job_6eba_spu_func_000077F8;assert(deltas!=expansion);
        auto reset=[&](){memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;};
        for(unsigned high=0;high<2;++high)for(unsigned shape=0;shape<3;++shape)for(unsigned mode=0;mode<3;++mode){
            reset();const unsigned width=high?12:4;ctx->gpr[81]=spu_ilh(width);ctx->gpr[82]=spu_il(width);ctx->gpr[90]=spu_il(width*0x01010101);
            ctx->gpr[96]._u32[0]=shape?64:0;ctx->gpr[97]._u32[0]=shape?width*4:0;ctx->gpr[80]._u32[0]=shape?64:0;ctx->gpr[0]._u32[0]=shape?0x7730:0x77f8;
            if(shape==2){ctx->gpr[98]._u32[0]=0xe800;ctx->gpr[94]._u32[0]=ctx->gpr[93]._u32[0]=ctx->gpr[86]._u32[0]=0xe800;}
            flow_mode=mode==1?2:1;index_unpack_mode=mode==2?2:1;
            buzz_geometry_job_index_unpack(ctx,index_unpack_oracle::buzz_job_6eba_spu_func_000074F0,deltas,expansion);
            assert(ctx->pc==(shape?0x7730u:0x77f8u) && g_spu_trampoline_fn==(shape?deltas:expansion));
        }
        assert(index_unpack_native==18 && index_unpack_checked==12 && !index_unpack_fallback && !index_unpack_references);
        flow_mode=1;reset();index_unpack_mode=0;buzz_geometry_job_index_unpack(ctx,index_unpack_reference,deltas,expansion);index_unpack_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_index_unpack(foreign,index_unpack_reference,deltas,expansion);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_index_unpack(ctx,index_unpack_reference,deltas,expansion);
        for(unsigned failure=0;failure<8;++failure){reset();auto d=deltas,e=expansion;
            switch(failure){case 0:d=nullptr;break;case 1:e=nullptr;break;case 2:ctx->gpr[82]._u32[0]=0;break;case 3:ctx->gpr[82]._u32[0]=17;break;
            case 4:ctx->gpr[97]._u32[0]=65536;break;case 5:ctx->gpr[96]._u32[0]=65536;break;case 6:ctx->gpr[88]._u32[0]=0;break;case 7:ctx->ls=entry->ls_store;break;}
            buzz_geometry_job_index_unpack(ctx,index_unpack_reference,d,e);
        }
        assert(index_unpack_references==11 && index_unpack_fallback==8 && index_unpack_native==18 && index_unpack_checked==12);
        index_unpack_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        ctx->gpr[88]=spu_ila(0xe800);ctx->gpr[31]=spu_ila(0xe800);ctx->gpr[0]._u32[0]=0x77f8;
        auto* entry=buzz::clone_function_state(ctx);const auto tail=index_deltas_oracle::buzz_job_6eba_spu_func_000077F8;
        for(unsigned shape=0;shape<3;++shape)for(unsigned mode=0;mode<3;++mode){
            memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;
            ctx->gpr[96]._u32[0]=shape==0?0:shape==1?1:65;if(shape==2)ctx->gpr[31]._u32[0]+=16;
            flow_mode=mode==1?2:1;index_deltas_mode=mode==2?2:1;
            buzz_geometry_job_index_deltas(ctx,index_deltas_oracle::buzz_job_6eba_spu_func_00007730,tail);
            assert(ctx->pc==0x77f8 && g_spu_trampoline_fn==tail);
        }
        assert(index_deltas_native==9 && index_deltas_checked==6 && !index_deltas_fallback && !index_deltas_references);
        flow_mode=1;index_deltas_mode=0;buzz_geometry_job_index_deltas(ctx,index_deltas_reference,tail);index_deltas_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_index_deltas(foreign,index_deltas_reference,tail);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_index_deltas(ctx,index_deltas_reference,tail);ctx->image_id=41;
        buzz_geometry_job_index_deltas(ctx,index_deltas_reference,nullptr);
        ctx->gpr[96]._u32[0]=65536;buzz_geometry_job_index_deltas(ctx,index_deltas_reference,tail);
        ctx->gpr[96]._u32[0]=32;ctx->gpr[31]._u32[0]=0;buzz_geometry_job_index_deltas(ctx,index_deltas_reference,tail);
        ctx->ls=entry->ls_store;buzz_geometry_job_index_deltas(ctx,index_deltas_reference,tail);ctx->ls=ctx->ls_store;
        assert(index_deltas_references==7 && index_deltas_fallback==4 && index_deltas_native==9 && index_deltas_checked==6);
        index_deltas_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        skin_normalized_oracle::seed_constants(*ctx);ctx->gpr[33]._u32[0]=ctx->gpr[67]._u32[0]=0xe800;ctx->gpr[71]._u32[0]=0x30000;ctx->gpr[93]._u32[0]=0x34000;ctx->gpr[40]._u32[0]=0x18000;ctx->gpr[0]._u32[0]=0xb868;
        auto* entry=buzz::clone_function_state(ctx);const auto tail=skin_normalized_oracle::buzz_job_6eba_spu_func_0000B868;
        for(unsigned streams=1;streams<=3;++streams)for(unsigned shape=0;shape<3;++shape)for(unsigned mode=0;mode<3;++mode){
            memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;ctx->gpr[84]._u32[0]=shape?16:4;
            if(shape==2)ctx->gpr[71]._u32[0]=ctx->gpr[93]._u32[0]=ctx->gpr[40]._u32[0]=0xe800;
            flow_mode=mode==1?2:1;skin_normalized_mode=mode==2?2:1;
            buzz_geometry_job_skin_normalized(ctx,skin_normalized_oracle::roots[streams-1],tail,streams);assert(ctx->pc==0xb868 && g_spu_trampoline_fn==tail);
        }
        assert(skin_normalized_native==27 && skin_normalized_checked==18 && !skin_normalized_fallback && !skin_normalized_references);
        flow_mode=1;skin_normalized_mode=0;buzz_geometry_job_skin_normalized(ctx,skin_normalized_reference,tail,1);skin_normalized_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_skin_normalized(foreign,skin_normalized_reference,tail,1);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_skin_normalized(ctx,skin_normalized_reference,tail,1);ctx->image_id=41;
        buzz_geometry_job_skin_normalized(ctx,skin_normalized_reference,nullptr,1);buzz_geometry_job_skin_normalized(ctx,skin_normalized_reference,tail,0);ctx->gpr[84]._u32[0]=0;buzz_geometry_job_skin_normalized(ctx,skin_normalized_reference,tail,1);
        assert(skin_normalized_references==6 && skin_normalized_fallback==3 && skin_normalized_native==27 && skin_normalized_checked==18);
        skin_normalized_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }

    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        skin_cofactor_oracle::seed_constants(*ctx);ctx->gpr[83]._u32[0]=0x20000;ctx->gpr[96]._u32[0]=0xe800;ctx->gpr[0]._u32[0]=0xb4c0;
        auto* entry=buzz::clone_function_state(ctx);const auto tail=skin_cofactor_oracle::buzz_job_6eba_spu_func_0000B4C0;
        for(unsigned shape=0;shape<3;++shape)for(unsigned mode=0;mode<3;++mode){
            memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;ctx->gpr[84]._u32[0]=shape?16:4;
            if(shape==2)ctx->gpr[83]._u32[0]=0xe800+192;
            flow_mode=mode==1?2:1;skin_cofactor_mode=mode==2?2:1;
            buzz_geometry_job_skin_cofactor(ctx,skin_cofactor_oracle::buzz_job_6eba_spu_func_0000BB58,tail);
            assert(ctx->pc==0xb4c0 && g_spu_trampoline_fn==tail);
        }
        assert(skin_cofactor_native==9 && skin_cofactor_checked==6 && !skin_cofactor_fallback && !skin_cofactor_references);
        flow_mode=1;skin_cofactor_mode=0;buzz_geometry_job_skin_cofactor(ctx,skin_cofactor_reference,tail);skin_cofactor_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_skin_cofactor(foreign,skin_cofactor_reference,tail);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_skin_cofactor(ctx,skin_cofactor_reference,tail);ctx->image_id=41;
        buzz_geometry_job_skin_cofactor(ctx,skin_cofactor_reference,nullptr);ctx->gpr[84]._u32[0]=0;buzz_geometry_job_skin_cofactor(ctx,skin_cofactor_reference,tail);
        assert(skin_cofactor_references==5 && skin_cofactor_fallback==2 && skin_cofactor_native==9 && skin_cofactor_checked==6);
        skin_cofactor_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }

    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        rigid_skin_oracle::seed_constants(*ctx);ctx->gpr[80]._u32[0]=0x17000;ctx->gpr[85]._u32[0]=0x12000;
        ctx->gpr[96]._u32[0]=0x23000;ctx->gpr[54]._u32[0]=0xe800;ctx->gpr[53]._u32[0]=0xe810;ctx->gpr[52]._u32[0]=0xe820;ctx->gpr[0]._u32[0]=0xb4c0;
        auto* entry=buzz::clone_function_state(ctx);const auto directions=rigid_skin_oracle::buzz_job_6eba_spu_func_0000B4C0,normals=rigid_skin_oracle::buzz_job_6eba_spu_func_0000BB58;
        for(unsigned shape=0;shape<3;++shape)for(unsigned tail=0;tail<2;++tail)for(unsigned mode=0;mode<3;++mode){
            memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;
            ctx->gpr[84]._u32[0]=shape?8:2;ctx->gpr[102]._u32[0]=tail?0x10001:0x10000;
            if(shape==2)ctx->gpr[96]._u32[0]=ctx->gpr[80]._u32[0]+16;
            flow_mode=mode==1?2:1;rigid_skin_mode=mode==2?2:1;
            buzz_geometry_job_rigid_skin(ctx,rigid_skin_oracle::buzz_job_6eba_spu_func_0000B348,directions,normals);
            assert(ctx->pc==(tail?0xb4c0:0xbb58) && g_spu_trampoline_fn==(tail?directions:normals));
        }
        assert(rigid_skin_native==18 && rigid_skin_checked==12 && !rigid_skin_fallback && !rigid_skin_references);
        flow_mode=1;rigid_skin_mode=0;buzz_geometry_job_rigid_skin(ctx,rigid_skin_reference,directions,normals);rigid_skin_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_rigid_skin(foreign,rigid_skin_reference,directions,normals);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_rigid_skin(ctx,rigid_skin_reference,directions,normals);ctx->image_id=41;
        buzz_geometry_job_rigid_skin(ctx,rigid_skin_reference,nullptr,normals);
        buzz_geometry_job_rigid_skin(ctx,rigid_skin_reference,directions,nullptr);
        ctx->gpr[84]._u32[0]=0;buzz_geometry_job_rigid_skin(ctx,rigid_skin_reference,directions,normals);
        assert(rigid_skin_references==6 && rigid_skin_fallback==3 && rigid_skin_native==18 && rigid_skin_checked==12);
        rigid_skin_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }

    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        ctx->gpr[1]._u32[0]=0x3f000;ctx->gpr[3]._u32[0]=0xe800;ctx->gpr[0]._u32[0]=0xaf18;
        auto* entry=buzz::clone_function_state(ctx);const auto& f=skin_flow_oracle::library;
        for(unsigned branch=0;branch<3;++branch)for(unsigned mode=0;mode<3;++mode){
            memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;
            ctx->ls[0xe800]=branch==0?0x90:0x80;ctx->ls[0xe806]=branch==2?0:1;ctx->gpr[5]._u32[0]=branch==1?0:1;
            flow_mode=mode==1?2:1;skin_flow_mode=mode==2?2:1;
            buzz_geometry_job_skin_flow(ctx,skin_flow_oracle::buzz_job_6eba_spu_func_0000AF18,&f);
            assert(ctx->pc==0xaf18 && g_spu_trampoline_fn==(ctx->host_depth?nullptr:spu_indirect_branch));
        }
        assert(skin_flow_native==9 && skin_flow_checked==6 && !skin_flow_fallback && !skin_flow_references);
        flow_mode=1;skin_flow_mode=0;buzz_geometry_job_skin_flow(ctx,skin_flow_reference,&f);skin_flow_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_skin_flow(foreign,skin_flow_reference,&f);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_skin_flow(ctx,skin_flow_reference,&f);ctx->image_id=41;
        buzz_geometry_job_skin_flow(ctx,skin_flow_reference,nullptr);
        auto missing=f;missing.rigid=nullptr;buzz_geometry_job_skin_flow(ctx,skin_flow_reference,&missing);
        ctx->gpr[1]._u32[0]|=1;buzz_geometry_job_skin_flow(ctx,skin_flow_reference,&f);
        assert(skin_flow_references==6 && skin_flow_fallback==3 && skin_flow_native==9 && skin_flow_checked==6);
        skin_flow_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }

    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        ctx->gpr[84]._u32[0]=8;ctx->gpr[33]._u32[0]=ctx->gpr[67]._u32[0]=0xe800;
        ctx->gpr[71]._u32[0]=0xf000;ctx->gpr[93]._u32[0]=0xf800;ctx->gpr[40]._u32[0]=0x18000;ctx->gpr[0]._u32[0]=0xb868;
        auto* entry=buzz::clone_function_state(ctx);const auto tail=skin_directions_oracle::buzz_job_6eba_spu_func_0000B868;
        for(unsigned streams=1;streams<=3;++streams)for(unsigned mode=0;mode<3;++mode){
            memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;
            flow_mode=mode==1?2:1;skin_directions_mode=mode==2?2:1;
            buzz_geometry_job_skin_directions(ctx,skin_directions_oracle::roots[streams-1],tail,streams);
            assert(ctx->pc==0xb868 && g_spu_trampoline_fn==tail);
        }
        assert(skin_directions_native==9 && skin_directions_checked==6 && !skin_directions_fallback && !skin_directions_references);
        flow_mode=1;skin_directions_mode=0;buzz_geometry_job_skin_directions(ctx,skin_directions_reference,tail,1);skin_directions_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_skin_directions(foreign,skin_directions_reference,tail,1);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_skin_directions(ctx,skin_directions_reference,tail,1);ctx->image_id=41;
        buzz_geometry_job_skin_directions(ctx,skin_directions_reference,nullptr,1);
        buzz_geometry_job_skin_directions(ctx,skin_directions_reference,tail,0);
        ctx->gpr[84]._u32[0]=0;buzz_geometry_job_skin_directions(ctx,skin_directions_reference,tail,1);
        assert(skin_directions_references==6 && skin_directions_fallback==3 && skin_directions_native==9 && skin_directions_checked==6);
        skin_directions_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }

    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        weighted_skin_oracle::seed_constants(*ctx);
        ctx->gpr[80]._u32[0]=0x17005;ctx->gpr[85]._u32[0]=0x12008;ctx->gpr[96]._u32[0]=0x23003;
        ctx->gpr[54]._u32[0]=0xe800;ctx->gpr[53]._u32[0]=0xe810;ctx->gpr[52]._u32[0]=0xe820;
        ctx->gpr[0]._u32[0]=0xbb58;ctx->pc=0xb880;
        auto* entry=buzz::clone_function_state(ctx);
        const auto directions=weighted_skin_oracle::buzz_job_6eba_spu_func_0000B4C0,normals=weighted_skin_oracle::buzz_job_6eba_spu_func_0000BB58;
        for(unsigned branch=0;branch<2;++branch)for(unsigned mode=0;mode<3;++mode){
            memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;
            ctx->gpr[84]._u32[0]=branch?6:2;ctx->gpr[102]._u32[0]=branch?0:1;
            flow_mode=mode==1?2:1;weighted_skin_mode=mode==2?2:1;
            buzz_geometry_job_weighted_skin(ctx,weighted_skin_fallback_call,weighted_skin_reference,directions,normals);
            assert(ctx->pc==(branch?0xbb58:0xb4c0) && g_spu_trampoline_fn==(branch?normals:directions));
        }
        assert(weighted_skin_native==6 && weighted_skin_checked==4 && !weighted_skin_references && !weighted_skin_fallback);
        flow_mode=1;weighted_skin_mode=0;buzz_geometry_job_weighted_skin(ctx,weighted_skin_fallback_call,weighted_skin_reference,directions,normals);weighted_skin_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_weighted_skin(foreign,weighted_skin_fallback_call,weighted_skin_reference,directions,normals);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_weighted_skin(ctx,weighted_skin_fallback_call,weighted_skin_reference,directions,normals);ctx->image_id=41;
        buzz_geometry_job_weighted_skin(ctx,weighted_skin_fallback_call,nullptr,directions,normals);
        buzz_geometry_job_weighted_skin(ctx,weighted_skin_fallback_call,weighted_skin_reference,nullptr,normals);
        buzz_geometry_job_weighted_skin(ctx,weighted_skin_fallback_call,weighted_skin_reference,directions,nullptr);
        ctx->gpr[84]._u32[0]=0;buzz_geometry_job_weighted_skin(ctx,weighted_skin_fallback_call,weighted_skin_reference,directions,normals);
        assert(weighted_skin_references==7 && weighted_skin_fallback==4 && weighted_skin_native==6 && weighted_skin_checked==4);
        weighted_skin_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }

    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        const auto& f=skin_dispatch_oracle::library;
        ctx->gpr[0]._u32[0]=0x2c00;ctx->gpr[1]._u32[0]=0x3f000;ctx->gpr[3]._u32[0]=0xe800;
        auto* entry=buzz::clone_function_state(ctx);
        for(unsigned branch=0;branch<3;++branch)for(unsigned mode=0;mode<3;++mode){
            memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;
            ctx->gpr[9]._u32[0]=branch?0x10001:0x10000;ctx->gpr[10]=spu_il(0);skin_dispatch_secondary=branch==2?2:0;
            flow_mode=mode==1?2:1;skin_dispatch_mode=mode==2?2:1;
            buzz_geometry_job_skin_dispatch(ctx,skin_dispatch_oracle::buzz_job_6eba_spu_func_000009F8,&f);
            assert(ctx->pc==0x2c00 || ctx->pc==0xaf18);
            assert(g_spu_trampoline_fn==(branch==0?f.weighted:branch==2?f.normalize:nullptr));
        }
        assert(skin_dispatch_native==9 && skin_dispatch_checked==6 && !skin_dispatch_fallback);
        flow_mode=1;skin_dispatch_mode=0;buzz_geometry_job_skin_dispatch(ctx,skin_dispatch_reference,&f);skin_dispatch_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_skin_dispatch(foreign,skin_dispatch_reference,&f);buzz::release_function_state(foreign);
        ctx->image_id=40;buzz_geometry_job_skin_dispatch(ctx,skin_dispatch_reference,&f);ctx->image_id=41;
        buzz_geometry_job_skin_dispatch(ctx,skin_dispatch_reference,nullptr);
        auto missing=f;missing.weighted=nullptr;buzz_geometry_job_skin_dispatch(ctx,skin_dispatch_reference,&missing);
        ctx->gpr[1]._u32[0]|=1;buzz_geometry_job_skin_dispatch(ctx,skin_dispatch_reference,&f);
        assert(skin_dispatch_references==6 && skin_dispatch_fallback==3 && skin_dispatch_native==9 && skin_dispatch_checked==6);
        skin_dispatch_mode=0;buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }

    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        ctx->gpr[0]._u32[0]=0x7df8;ctx->gpr[1]._u32[0]=0x3f000;ctx->gpr[3]._u32[0]=0xe800;ctx->gpr[4]._u32[0]=0x20000;
        ctx->gpr[6]._u32[0]=255;ctx->gpr[7]._u32[0]=0x24000;ctx->gpr[8]._u32[0]=0;ctx->pc=0xacd0;
        ctx->ls[0xe808]=0;ctx->ls[0xe809]=5;
        auto* entry=buzz::clone_function_state(ctx);g_spu_trampoline_fn=nullptr;custom_stream_mode=1;
        assert(!attributes_enabled); // The new switch is independent of AC68/raw.
        buzz_geometry_job_decode_custom_stream(ctx,custom_stream_reference,buzz_job_6eba_spu_func_00007DF8);
        assert(custom_stream_native==1 && !custom_stream_checked && !custom_stream_references && ctx->pc==0x7df8 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_00007DF8);
        memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;flow_mode=2;
        buzz_geometry_job_decode_custom_stream(ctx,buzz_job_6eba_spu_func_000093F0,buzz_job_6eba_spu_func_00007DF8);
        memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;flow_mode=1;custom_stream_mode=2;
        buzz_geometry_job_decode_custom_stream(ctx,buzz_job_6eba_spu_func_000093F0,buzz_job_6eba_spu_func_00007DF8);
        assert(custom_stream_native==3 && custom_stream_checked==2 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_00007DF8);
        g_spu_trampoline_fn=nullptr;custom_stream_mode=0;buzz_geometry_job_decode_custom_stream(ctx,custom_stream_reference,buzz_job_6eba_spu_func_00007DF8);custom_stream_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_decode_custom_stream(foreign,custom_stream_reference,buzz_job_6eba_spu_func_00007DF8);buzz::release_function_state(foreign);
        memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;
        buzz_geometry_job_decode_custom_stream(ctx,custom_stream_reference,nullptr);
        ctx->gpr[8]._u32[0]=0x1230;buzz_geometry_job_decode_custom_stream(ctx,custom_stream_reference,buzz_job_6eba_spu_func_00007DF8);
        ctx->gpr[8]._u32[0]=0;ctx->gpr[7]._u32[0]=0;buzz_geometry_job_decode_custom_stream(ctx,custom_stream_reference,buzz_job_6eba_spu_func_00007DF8);
        ctx->gpr[7]._u32[0]=0x24000;ctx->gpr[6]._u32[0]=5;buzz_geometry_job_decode_custom_stream(ctx,custom_stream_reference,buzz_job_6eba_spu_func_00007DF8);
        ctx->gpr[6]._u32[0]=255;ctx->ls[0xe808]=ctx->ls[0xe809]=0;buzz_geometry_job_decode_custom_stream(ctx,custom_stream_reference,buzz_job_6eba_spu_func_00007DF8);
        assert(custom_stream_references==7 && custom_stream_fallback==4);custom_stream_mode=0;
        buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        auto put16=[&](unsigned at,unsigned v){ctx->ls[at]=uint8_t(v>>8);ctx->ls[at+1]=uint8_t(v);};
        for(unsigned table=0;table<9;++table)for(unsigned i=0;i<8;++i)
            put16(0xd080+table*16+i*2,table<4?(table*8+i)*16:((table-4)*8+i)*2+1);
        for(unsigned i=0;i<16;++i)ctx->ls[0xd980+i]=i;
        ctx->gpr[0]._u32[0]=0x7298;ctx->gpr[3]._u32[0]=0x20000;ctx->gpr[4]._u32[0]=6;ctx->gpr[5]._u32[0]=6;ctx->pc=0x1018;
        put16(0x20000,0x20);put16(0x20002,0x41);put16(0x20004,0x62);
        auto* entry=buzz::clone_function_state(ctx);g_spu_trampoline_fn=nullptr;run_lists_mode=1;
        buzz_geometry_job_run_lists(ctx,run_list_reference,buzz_job_6eba_spu_func_00007298);
        assert(run_list_native==1 && run_list_expanded==1 && !run_list_checked && !run_list_reference_count);
        memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;flow_mode=2;
        buzz_geometry_job_run_lists(ctx,buzz_job_6eba_spu_func_00007118,buzz_job_6eba_spu_func_00007298);
        memcpy(ctx,entry,sizeof(*ctx));ctx->ls=ctx->ls_store;g_spu_trampoline_fn=nullptr;flow_mode=1;run_lists_mode=2;
        buzz_geometry_job_run_lists(ctx,buzz_job_6eba_spu_func_00007118,buzz_job_6eba_spu_func_00007298);
        ctx->gpr[4]._u32[0]=0;g_spu_trampoline_fn=nullptr;
        buzz_geometry_job_run_lists(ctx,buzz_job_6eba_spu_func_00007118,buzz_job_6eba_spu_func_00007298);
        assert(run_list_native==4 && run_list_expanded==3 && run_list_empty==1 && run_list_checked==3);
        buzz_geometry_job_run_lists(ctx,run_list_reference,nullptr);
        run_lists_mode=0;buzz_geometry_job_run_lists(ctx,run_list_reference,buzz_job_6eba_spu_func_00007298);run_lists_mode=1;
        auto* foreign=buzz::clone_function_state(ctx);buzz_geometry_job_run_lists(foreign,run_list_reference,buzz_job_6eba_spu_func_00007298);buzz::release_function_state(foreign);
        ctx->gpr[4]._u32[0]=7;buzz_geometry_job_run_lists(ctx,run_list_reference,buzz_job_6eba_spu_func_00007298);
        assert(run_list_reference_count==4 && run_list_fallback==2);run_lists_mode=0;
        buzz::release_function_state(entry);memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);g_spu_trampoline_fn=continuation;
    }
    {
        auto* saved=buzz::clone_function_state(ctx);const auto continuation=g_spu_trampoline_fn;
        ctx->gpr[1]._u32[0]=0x3f000;ctx->gpr[5]._u32[0]=0;ctx->pc=0xbf8;g_spu_trampoline_fn=nullptr;
        buzz_geometry_blend_list_functions functions{list_reference,list_reference,list_reference,list_reference,list_reference};
        lists_enabled=true;
        buzz_geometry_job_blend_lists(ctx,list_reference,&functions);
        assert(list_native==1 && !list_references && !list_fallback && !ctx->gpr[3]._u32[0]);
        g_spu_trampoline_fn=nullptr;lists_enabled=false;
        buzz_geometry_job_blend_lists(ctx,list_reference,&functions);lists_enabled=true;
        auto* foreign=buzz::clone_function_state(ctx);
        buzz_geometry_job_blend_lists(foreign,list_reference,&functions);buzz::release_function_state(foreign);
        buzz_geometry_job_blend_lists(ctx,list_reference,nullptr);
        ctx->gpr[1]._u32[0]|=1;buzz_geometry_job_blend_lists(ctx,list_reference,&functions);
        assert(list_native==1 && list_references==4 && list_fallback==2);
        lists_enabled=false;
        memcpy(ctx,saved,sizeof(*ctx));ctx->ls=ctx->ls_store;buzz::release_function_state(saved);
        g_spu_trampoline_fn=continuation;
    }
    attribute_routing(ctx);
    std::array<u128, 128> saved;
    memcpy(saved.data(), ctx->gpr, sizeof ctx->gpr); const auto pc = ctx->pc;
    const uint32_t context = ctx->gpr[3]._u32[0];
    buzz::geometry_job_put32(ctx->ls + context + 0x58, 0x20000);
    ctx->gpr[4] = spu_ila(0x28000); ctx->gpr[5] = spu_ila(0x10000);
    ctx->gpr[6] = spu_il(0); ctx->gpr[7] = spu_il(0);
    buzz_geometry_job_prepare_commands(ctx, routing_reference);
    assert(routed_native == 1 && routed_references == 1);
    assert(buzz::geometry_job_be32(ctx->ls + 0x28000) == 0x20080 &&
           buzz::geometry_job_be32(ctx->ls + 0x28004) == 0x20084);
    auto foreign = std::make_unique<spu_context>(); memcpy(foreign.get(), ctx, sizeof(*ctx)); foreign->ls = foreign->ls_store;
    buzz_geometry_job_prepare_commands(foreign.get(), routing_reference);
    assert(routed_references == 2 && routed_native == 1);
    commands_enabled = false; buzz_geometry_job_prepare_commands(ctx, routing_reference); commands_enabled = true;
    assert(routed_references == 3 && routed_native == 1);
    ctx->gpr[4]._u32[0] = SPU_LS_SIZE - 1;
    buzz_geometry_job_prepare_commands(ctx, routing_reference);
    assert(routed_references == 4 && routed_fallback == 1);
    ctx->gpr[5] = spu_il(0); // The no-destination path needs no LS or DMA.
    buzz_geometry_job_publish_commands(ctx, publication_reference);
    assert(publication_native == 1 && publication_references == 1);
    buzz_geometry_job_publish_commands(foreign.get(), publication_reference);
    command_publish_enabled = false;
    buzz_geometry_job_publish_commands(ctx, publication_reference);
    command_publish_enabled = true;
    ctx->gpr[5] = spu_il(1); // Unsupported before mutation.
    buzz_geometry_job_publish_commands(ctx, publication_reference);
    assert(publication_references == 4 && publication_fallback == 1 && publication_native == 1);
    ctx->gpr[3] = spu_ila(context); ctx->gpr[4] = spu_ila(0x28000);
    ctx->gpr[5] = spu_ila(0x28020); ctx->gpr[6] = spu_il(1);
    buzz::geometry_job_put32(ctx->ls + context + 0x74, 0x2a000); ctx->ls[0x2a000] = 0;
    buzz_geometry_job_command_bindings(ctx, emit_reference, 0);
    ctx->gpr[3] = spu_ila(context); ctx->gpr[4] = spu_ila(0x28000); ctx->gpr[5] = spu_il(1);
    buzz_geometry_job_command_bindings(ctx, emit_reference, 1);
    assert(emit_native[0] == 1 && emit_native[1] == 1);
    buzz_geometry_job_command_bindings(foreign.get(), emit_reference, 0);
    buzz_geometry_job_emit_position(foreign.get(), emit_reference, nullptr);
    buzz_geometry_job_emit_full(foreign.get(), emit_reference, nullptr);
    emit_enabled = full_enabled = false;
    buzz_geometry_job_command_bindings(ctx, emit_reference, 0);
    buzz_geometry_job_emit_position(ctx, emit_reference, nullptr);
    buzz_geometry_job_emit_full(ctx, emit_reference, nullptr);
    emit_enabled = full_enabled = true;
    ctx->gpr[3]._u32[0] = SPU_LS_SIZE - 1;
    buzz_geometry_job_command_bindings(ctx, emit_reference, 0);
    ctx->gpr[1]._u32[0] = 3;
    buzz_geometry_job_emit_position(ctx, emit_reference, nullptr);
    buzz_geometry_job_emit_full(ctx, emit_reference, nullptr);
    assert(emit_fallback[0] == 1 && emit_fallback[2] == 1);
    memcpy(ctx->gpr, saved.data(), sizeof ctx->gpr); ctx->pc = pc;
}

#endif

static uint32_t random_word(uint32_t& value) { value ^= value << 13; value ^= value >> 17; value ^= value << 5; return value; }
static void synthetic(spu_context& ctx, unsigned trial)
{
    memset(&ctx, 0, sizeof ctx); ctx.ls = ctx.ls_store; ctx.image_id = 41; ctx.pc = 0x1c18;
    uint32_t seed = trial + 1;
    for (unsigned i = 0; i < SPU_LS_SIZE; ++i) ctx.ls[i] = uint8_t(random_word(seed));
    for (auto& reg : ctx.gpr) for (auto& word : reg._u32) word = random_word(seed);
    ctx.gpr[0]._u32[0] = 0x3ff00; ctx.gpr[1]._u32[0] = 0x137e0;
    ctx.gpr[3]._u32[0] = 0x3fbc0; ctx.gpr[4]._u32[0] = 0x3fc00;
    auto put = [&](unsigned at, uint32_t value) { buzz::geometry_job_put32(ctx.ls + at, value); };
    memset(ctx.ls + 0x3fbc0, 0, 0x140);
    put(0x3fbc0, 0xe800); put(0x3fbd4, 0x2003fbf0); put(0x3fbd8, 21 << 16);
    put(0x3fbdc, 0x10800); put(0x3fbe0, 0x10800); put(0x3fbe4, 20);
    put(0x3fc08, 0x0e6800a8); put(0x3fc14, 8192); put(0x3fc1c, 256);
    const uint32_t constants[] = {0, 0x7fff, 0, 0, 0x04050607, 0x80808080, 0x0c0d0e0f, 0x80808080};
    for (unsigned i = 0; i < 8; ++i) put(0xcd10 + i * 4, constants[i]);
    unsigned cursor = 0xe800;
    for (unsigned i = 0; i < 21; ++i) {
        const unsigned size = i == 0 ? 64 : i == 11 && (trial & 64) ? 0 : i == 14 ? 128 : i == 15 ? 64 : i == 16 ? 16 : 32;
        const unsigned flags = (i == 4 && (trial & 256)) || (i == 5 && (trial & 512)) ? 0x80000000u : 0;
        put(0x3fc30 + i * 8, size | flags); put(0x3fc34 + i * 8, 0x100000 + i * 256);
        if (i == 16) {
            const uint8_t config[] = {0x82, 0, 0, 0xff, 0xff, 0xff, 0x30, 0, 0, 8, 0, 24, 0xff, 0xff, 0xff, 0xff};
            memcpy(ctx.ls + cursor, config, 16);
        }
        cursor += size;
    }
    // The pipeline model does not execute the secondary descriptor decoder,
    // but its declared metadata must still satisfy the real parent guard.
    buzz::GeometryJobEntry modeled_input;assert(buzz::read_geometry_job_entry(ctx,modeled_input));
    auto* modeled_descriptor=ctx.ls+modeled_input.input[20].lsa;
    memset(modeled_descriptor,0,16);modeled_descriptor[0]=1;modeled_descriptor[1]=16;
    modeled_descriptor[9]=2;modeled_descriptor[10]=4;modeled_descriptor[11]=2;modeled_descriptor[12]=12;
    put(0xe800, 0);
    static const uint32_t bits[] = {0x10000000, 0x80, 0x100, 8, 32, 64};
    uint32_t flags = 0;
    for (unsigned i = 0; i < 6; ++i) if (trial & (1 << i)) flags |= bits[i];
    static const uint32_t weights[] = {0, 1, 0x8000, 0xffff};
    put(0x3fcec, flags); put(0x3fce0, trial & 128 ? (weights[trial >> 8] << 16) | 1 : 0);
    put(0x3fcf0, trial * 0x19707u); put(0x3fcf8, trial * 0x193031u);
    ctx.ls[0x3fcfa] &= 0xfe;
    put(0x3fcfc, trial * 0x7913u);
}
static void compare(spu_context& native, spu_context& reference)
{
    memcpy(&reference, &native, sizeof native); reference.ls = reference.ls_store;
    const uint32_t link = native.gpr[0]._u32[0];
    calls.clear(); g_spu_trampoline_fn = nullptr;
    buzz_job_6eba_spu_func_00001C18(&reference); drain(&reference, link);
    const auto expected = calls;
    calls.clear(); g_spu_trampoline_fn = nullptr;
    buzz_geometry_job_native_start(&native, &model_library); drain(&native, link);
    if (calls.size() != expected.size()) { fprintf(stderr, "%s: call count %zu/%zu\n", current_case, calls.size(), expected.size()); abort(); }
    for (unsigned i = 0; i < calls.size(); ++i) {
        const auto& a = calls[i]; const auto& b = expected[i];
        if (a.pc != b.pc || a.link != b.link || memcmp(a.arguments.data(), b.arguments.data(), sizeof a.arguments) ||
            memcmp(a.nonvolatile.data(), b.nonvolatile.data(), sizeof a.nonvolatile)) {
            fprintf(stderr, "%s: call %u pc=%X/%X link=%X/%X differs\n", current_case, i, a.pc, b.pc, a.link, b.link);
            for (unsigned reg = 0; reg < 31; ++reg) if (memcmp(&a.nonvolatile[reg], &b.nonvolatile[reg], 16))
                fprintf(stderr, " nonvolatile r%u %08X/%08X\n", reg == 30 ? 126 : 80 + reg, a.nonvolatile[reg]._u32[0], b.nonvolatile[reg]._u32[0]);
            abort();
        }
    }
    const auto difference = buzz::compare_function_state(native, reference);
    if (!difference.exact()) {
        fprintf(stderr, "%s: exit r%d byte%d LS=%X bytes=%u control=%s\n", current_case,
            difference.first_register, difference.first_register_byte, difference.first_ls, difference.ls_bytes,
            difference.first_state ? difference.first_state : "equal"); abort();
    }
    assert(native.host_depth == 0 && reference.host_depth == 0);
}
#if defined(BUZZ_GEOMETRY_LS_DRIVER)
int main()
{
    auto native = std::make_unique<spu_context>();
    auto reference = std::make_unique<spu_context>();
    const auto fp = buzz_arm_fp_save();
    buzz_arm_fp_enter_rtz(fp);
    for (scenario = 0; scenario < 1024; ++scenario) {
        synthetic(*native, scenario);
        assert(buzz_geometry_job_native_eligible(native.get()));
        compare(*native, *reference);
    }
    synthetic(*native, 0);
    buzz::geometry_job_put32(native->ls + 0x3fcf4, 16);
    std::memcpy(reference.get(), native.get(), sizeof(*native));
    assert(!buzz_geometry_job_native_eligible(native.get()));
    assert(!std::memcmp(reference.get(), native.get(), sizeof(*native)));
    buzz_arm_fp_restore(fp);
    puts("PASS ARM64 geometry driver: 1024 call transcripts and full exit states");
}
#else
int main(int argc, char** argv)
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    const unsigned previous = _mm_getcsr();
    _mm_setcsr((previous & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO | 0x8040);
    auto native = std::make_unique<spu_context>(), reference = std::make_unique<spu_context>();
    if(argc==2 && (!strcmp(argv[1],"formatter-registers") || !strncmp(argv[1],"formatter-registers-",20))){
        if(!strncmp(argv[1],"formatter-registers-",20))formatter_registers_corruption=argv[1]+20;
        formatter_registers_tests_only=routing_probe=true;synthetic(*native,0);const unsigned link=native->gpr[0]._u32[0];g_spu_trampoline_fn=nullptr;
        buzz_geometry_job_native_start(native.get(),&model_library);drain(native.get(),link);
        assert(formatter_registers_cases==1776);_mm_setcsr(previous);return 0;
    }
    synthetic(*native,0);buzz::GeometryJobEntry input_entry;assert(buzz::read_geometry_job_entry(*native,input_entry));
    const unsigned input_config=input_entry.input[16].lsa;native->ls[input_config+2]=1;native->ls[input_config+3]=5;input_control_mode=0;
    auto untouched=std::make_unique<unsigned char[]>(sizeof(*native));memcpy(untouched.get(),native.get(),sizeof(*native));
    assert(!buzz_geometry_job_native_eligible(native.get()) && !memcmp(untouched.get(),native.get(),sizeof(*native)));
    input_control_mode=1;assert(!buzz_geometry_job_native_eligible(native.get()) && !memcmp(untouched.get(),native.get(),sizeof(*native)));
    buzz::GeometryJobSetup refused;memset(&refused,0xa5,sizeof refused);unsigned char refused_before[sizeof refused];memcpy(refused_before,&refused,sizeof refused);const char* refuse_reason=nullptr;
    assert(!buzz::prepare_geometry_job(*native,refused,&refuse_reason,true) && refuse_reason && !memcmp(&refused,refused_before,sizeof refused) && !memcmp(untouched.get(),native.get(),sizeof(*native)));
    native->ls[input_config]=(native->ls[input_config]&0xf0)|7;memcpy(untouched.get(),native.get(),sizeof(*native));
    assert(buzz_geometry_job_native_eligible(native.get()) && !memcmp(untouched.get(),native.get(),sizeof(*native)));
    auto* guarded=buzz::clone_function_state(native.get());auto* baseline=buzz::clone_function_state(native.get());buzz::GeometryJobSetup guarded_setup,baseline_setup;
    assert(buzz::prepare_geometry_job(*guarded,guarded_setup,nullptr,true) && buzz::prepare_geometry_job(*baseline,baseline_setup));
    assert(buzz::compare_function_state(*guarded,*baseline).exact());buzz::release_function_state(guarded);buzz::release_function_state(baseline);input_control_mode=0;
    puts("Input parent entry guard:slot shortage rejected before preamble mutation;valid declared tables accepted;guarded/legacy preamble state identical PASS");
    routing_probe = commands_enabled = command_publish_enabled = emit_enabled = full_enabled = true;
    synthetic(*native, 0); native->host_depth = 1;
    buzz_geometry_job_prepare_commands(native.get(), routing_reference);
    buzz_geometry_job_publish_commands(native.get(), publication_reference);
    buzz_geometry_job_command_bindings(native.get(), emit_reference, 0);
    buzz_geometry_job_emit_position(native.get(), emit_reference, nullptr);
    buzz_geometry_job_emit_full(native.get(), emit_reference, nullptr);
    assert(emit_references == 3);
    assert(publication_references == 1 && !publication_native && !publication_fallback);
    assert(routed_references == 1 && !routed_native && !routed_fallback);
    synthetic(*native, 0); g_spu_trampoline_fn = nullptr;
    const uint32_t routing_link = native->gpr[0]._u32[0];
    buzz_geometry_job_native_start(native.get(), &model_library); drain(native.get(), routing_link);
    native->host_depth = 1;
    buzz_geometry_job_prepare_commands(native.get(), routing_reference);
    assert(routed_references == 5 && routed_native == 1 && routed_fallback == 1);
    buzz_geometry_job_publish_commands(native.get(), publication_reference);
    assert(publication_references == 5 && publication_native == 1 && publication_fallback == 1);
    buzz_geometry_job_command_bindings(native.get(), emit_reference, 0);
    buzz_geometry_job_emit_position(native.get(), emit_reference, nullptr);
    buzz_geometry_job_emit_full(native.get(), emit_reference, nullptr);
    assert(emit_references == 15 && emit_native[0] == 1 && emit_native[1] == 1 && !emit_native[2]);
    assert(emit_fallback[0] == 1 && !emit_fallback[1] && emit_fallback[2] == 1);
    assert(emit_fallback[3] == 1 && !emit_native[3]);
    buzz_geometry_job_decode_attributes(native.get(),attribute_reference,nullptr);
    buzz_geometry_job_decode_stream(native.get(),attribute_reference);
    assert(attribute_references == 11 && attribute_native[0] == 1 && attribute_native[1] == 1);
    buzz_geometry_job_decode_color(native.get(),color_reference,color_reference);
    assert(color_references == 5 && color_native == 2 && color_fallback == 2 && color_region_checked == 1);
    codecs_enabled = false;
    buzz_geometry_job_decode_descriptors(native.get(),descriptor_reference,nullptr);
    assert(descriptor_references==6 && descriptor_native==1 && descriptor_fallback==3);descriptors_enabled=false;
    buzz_geometry_job_decode_unit_vectors(native.get(),unit_reference,unit_reference);
    assert(unit_references==5 && unit_native==2 && unit_fallback==2 && unit_checked==1);unit_vectors_enabled=false;
    buzz_geometry_job_decode_half_stream(native.get(),half_reference,buzz_job_6eba_spu_func_000083C8);
    assert(half_references==5 && half_native==2 && half_fallback==2 && half_checked==1);half_stream_enabled=false;
    buzz_geometry_job_decode_float32_stream(native.get(),float32_reference,buzz_job_6eba_spu_func_000083C8);
    assert(float32_references==5 && float32_native==3 && float32_fallback==2 && float32_checked==2);float32_stream_enabled=false;
    buzz_geometry_job_decode_fixed_stream(native.get(),fixed_reference,buzz_job_6eba_spu_func_000083C8);
    assert(fixed_references==5 && fixed_native==3 && fixed_fallback==2 && fixed_checked==2);fixed_stream_enabled=false;
    buzz_geometry_job_decode_snorm_stream(native.get(),snorm_reference,buzz_job_6eba_spu_func_000083C8);
    assert(snorm_references==5 && snorm_native==3 && snorm_fallback==2 && snorm_checked==2);snorm_stream_enabled=false;
    buzz_geometry_job_decode_i16_stream(native.get(),i16_reference,buzz_job_6eba_spu_func_000083C8);
    assert(i16_references==5 && i16_native==3 && i16_fallback==2 && i16_checked==2);i16_stream_enabled=false;
    buzz_geometry_job_decode_cmp_stream(native.get(),cmp_reference,buzz_job_6eba_spu_func_000083C8);
    assert(cmp_references==5 && cmp_native==3 && cmp_fallback==2 && cmp_checked==2);cmp_stream_enabled=false;
    buzz_geometry_job_decode_u8_integer_stream(native.get(),u8_integer_reference,buzz_job_6eba_spu_func_000083C8);
    assert(u8_integer_references==5 && u8_integer_native==3 && u8_integer_fallback==2 && u8_integer_checked==2);u8_integer_stream_enabled=false;
    buzz_geometry_job_blend_scatter(native.get(),blend_reference,buzz_job_6eba_spu_func_0000C7A8);
    assert(blend_references==5 && blend_native==3 && blend_fallback==2 && blend_checked==2);blend_scatter_enabled=0;
    run_lists_mode=1;buzz_geometry_job_run_lists(native.get(),run_list_reference,buzz_job_6eba_spu_func_00007298);
    assert(run_list_reference_count==5 && run_list_native==4 && run_list_fallback==2 && run_list_checked==3);run_lists_mode=0;
    custom_stream_mode=1;buzz_geometry_job_decode_custom_stream(native.get(),custom_stream_reference,buzz_job_6eba_spu_func_00007DF8);
    assert(custom_stream_references==8 && custom_stream_native==3 && custom_stream_fallback==4 && custom_stream_checked==2);custom_stream_mode=0;
    skin_dispatch_mode=1;buzz_geometry_job_skin_dispatch(native.get(),skin_dispatch_reference,&skin_dispatch_oracle::library);
    assert(skin_dispatch_references==7 && skin_dispatch_native==9 && skin_dispatch_fallback==3 && skin_dispatch_checked==6);skin_dispatch_mode=0;
    puts("Skin dispatch routing: owner native; both strict modes; weighted/return/normalize boundaries; foreign/disabled/unsupported/post-scope PASS");
    weighted_skin_mode=1;buzz_geometry_job_weighted_skin(native.get(),weighted_skin_fallback_call,weighted_skin_reference,weighted_skin_oracle::buzz_job_6eba_spu_func_0000B4C0,weighted_skin_oracle::buzz_job_6eba_spu_func_0000BB58);
    assert(weighted_skin_references==8 && weighted_skin_native==6 && weighted_skin_fallback==4 && weighted_skin_checked==4);weighted_skin_mode=0;
    puts("Complete weighted skin routing: one-pair/bulk; both strict modes; both tails; foreign/disabled/unsupported/post-scope PASS");
    index_expand_mode=1;buzz_geometry_job_index_expand(native.get(),index_expand_reference,index_expand_oracle::buzz_job_6eba_spu_func_000079A8,index_expand_oracle::buzz_job_6eba_spu_func_00007DCC);
    assert(index_expand_references==12 && index_expand_native==18 && index_expand_checked==12 && index_expand_fallback==8);index_expand_mode=0;
    puts("Complete index expansion routing:warmup/short/bulk,both exits/strict modes,aliases,tail-PC collisions;owner/foreign/off/image/null/invalid/post-scope PASS");
    index_relocate_mode=1;buzz_geometry_job_index_relocate(native.get(),index_relocate_reference,index_relocate_oracle::buzz_job_6eba_spu_func_00007A90);
    assert(index_relocate_references==11 && index_relocate_native==18 && index_relocate_checked==12 && index_relocate_fallback==7);index_relocate_mode=0;
    puts("Complete index relocation routing:warmup/short/bulk,both strict modes,aliases,tail-PC collisions;owner/foreign/off/image/null/invalid/post-scope PASS");
    index_triangles_mode=1;buzz_geometry_job_index_triangles(native.get(),index_triangles_fallback_call,index_triangles_reference,index_triangles_oracle::buzz_job_6eba_spu_func_00002738);
    assert(index_triangles_fallback_calls==13 && index_triangles_references==12 && index_triangles_native==18 && index_triangles_checked==12 && index_triangles_fallback==9);index_triangles_mode=0;
    puts("Complete index triangles routing:warmup/short/bulk,both strict modes,table/code/frame aliases,tail-PC collisions;separate fallback/reference;owner/foreign/off/image/null/invalid/post-scope PASS");
    index_control_mode=1;buzz_geometry_job_index_control(native.get(),index_control_fallback_call,index_control_reference,&index_control_oracle::library);
    assert(index_control_fallback_calls==19 && index_control_references==20 && index_control_native==30 && index_control_checked==20 && index_control_fallback==15);index_control_mode=0;
    puts("Complete index control routing:five paths,both strict modes/depths,biased stack/manager,return-PC collisions,incoming continuation;separate fallback/reference;owner/foreign/off/image/null/invalid/post-scope PASS");
    conversion_control_mode=1;buzz_geometry_job_conversion_control(native.get(),conversion_control_fallback_call,nullptr,nullptr,nullptr);
    assert(conversion_control_native==3162 && conversion_control_checked==1596 && conversion_control_references==1596 && conversion_control_fallback==82 && conversion_control_fallback_calls==98);conversion_control_mode=0;
    puts("Complete conversion control routing:90 native/60 strict roots;all3 paths/all7 encoders;26 unchanged owned rejects;formatter/encoder dependencies and off/foreign/image/post-scope PASS");
    for(unsigned format=0;format<4;++format){builtin_output_current=format;builtin_output_mode[format]=1;auto before=std::make_unique<spu_context>();memcpy(before.get(),native.get(),sizeof(*native));builtin_output_unchanged=before.get();builtin_output_queue=g_spu_trampoline_fn;
        buzz_geometry_job_builtin_output(native.get(),builtin_output_fallback_call,nullptr,nullptr,format);builtin_output_unchanged=nullptr;builtin_output_mode[format]=0;
        assert(builtin_output_native[format]==1344 && builtin_output_checked[format]==768 && builtin_output_references[format]==768 && builtin_output_fallback[format]==12 && builtin_output_fallback_calls[format]==16);}
    puts("Complete builtin output routing:all4 formats,each576 native/384 strict roots;both strict modes/3depths/16biases/4counts;12 unchanged owned rejects plus off/image/foreign/post-scope;default-off switches independent PASS");
    formatter_mode=1;buzz_geometry_job_formatter(native.get(),formatter_fallback_call,nullptr,nullptr);
    assert(formatter_native==117 && formatter_checked==78 && formatter_references==78 && formatter_fallback==28 && formatter_fallback_calls==32);formatter_mode=0;
    puts("Complete formatter routing:117 native/78 strict roots,81 all7 data callees;28 unchanged unsupported +off/foreign/image/post-scope;used-switch dependencies and original46-block reference PASS");
    i16_output_mode=1;buzz_geometry_job_i16_output(native.get(),i16_output_fallback_call,nullptr,nullptr,nullptr);assert(i16_output_fallback_calls==14 && i16_output_native==25 && i16_output_fallback==10);i16_output_mode=0;
    u8_integer_output_mode=1;buzz_geometry_job_u8_integer_output(native.get(),u8_integer_output_fallback_call,nullptr,nullptr,nullptr);assert(u8_integer_output_fallback_calls==14 && u8_integer_output_native==25 && u8_integer_output_fallback==10);u8_integer_output_mode=0;
    unorm_output_mode=1;buzz_geometry_job_unorm_output(native.get(),unorm_output_fallback_call,nullptr,nullptr,nullptr);assert(unorm_output_fallback_calls==14 && unorm_output_native==25 && unorm_output_fallback==10);unorm_output_mode=0;
    snorm_output_mode=1;buzz_geometry_job_snorm_output(native.get(),snorm_output_fallback_call,nullptr,nullptr,nullptr);assert(snorm_output_fallback_calls==14 && snorm_output_native==25 && snorm_output_fallback==10);snorm_output_mode=0;
    cmp_output_mode=1;buzz_geometry_job_cmp_output(native.get(),cmp_output_fallback_call,nullptr,nullptr,nullptr);assert(cmp_output_fallback_calls==14 && cmp_output_native==25 && cmp_output_fallback==10);cmp_output_mode=0;
    half_output_mode=1;buzz_geometry_job_half_output(native.get(),half_output_fallback_call,nullptr,nullptr,nullptr);assert(half_output_fallback_calls==14 && half_output_native==25 && half_output_fallback==10);half_output_mode=0;
    float_output_mode=1;buzz_geometry_job_float_output(native.get(),float_output_fallback_call,nullptr,nullptr,nullptr);assert(float_output_fallback_calls==14 && float_output_native==24 && float_output_fallback==10);float_output_mode=0;
    ring_mode=1;buzz_geometry_job_ring(native.get(),ring_reference_call,nullptr);assert(ring_references==15 && ring_native==16 && ring_fallback==10);ring_mode=0;
    alternate_cull_mode=1;buzz_geometry_job_alternate_cull(native.get(),alternate_cull_fallback_call,alternate_cull_oracle::buzz_job_6eba_spu_func_00006958);
    assert(alternate_cull_fallback_calls==9 && alternate_cull_references==12 && alternate_cull_native==18 && alternate_cull_checked==12 && alternate_cull_fallback==5);alternate_cull_mode=0;
    puts("Complete alternate cull routing:warmup/short/bulk,both strict modes/depths,wrapped output,incoming continuation;owner/off/foreign/image/null/invalid/post-scope PASS");
    transform_control_mode=1;buzz_geometry_job_transform_control(native.get(),transform_control_fallback_call,transform_control_reference,&transform_control_library);
    assert(transform_control_fallback_calls==20 && transform_control_references==28 && transform_control_native==42 && transform_control_checked==28 && transform_control_fallback==16);transform_control_mode=0;
    puts("Complete transform control routing:seven paths,both strict modes/depths,biased stack/manager,late overwritten interior return link,incoming continuation;owner/off/foreign/image/null/invalid/post-scope PASS");
    vertex_prepare_mode=1;buzz_geometry_job_vertex_prepare(native.get(),vertex_prepare_fallback_call,vertex_prepare_oracle::buzz_job_6eba_spu_func_000063E0);
    assert(vertex_prepare_fallback_calls==11 && vertex_prepare_references==16 && vertex_prepare_native==24 && vertex_prepare_checked==16 && vertex_prepare_fallback==7);vertex_prepare_mode=0;
    puts("Complete vertex preparation routing:four formats,both strict modes/depths,biased manager,incoming continuation;owner/off/foreign/image/null/invalid/post-scope PASS");
    transform_finish_mode=1;buzz_geometry_job_transform_finish(native.get(),transform_finish_fallback_call);
    assert(transform_finish_fallback_calls==10 && transform_finish_references==8 && transform_finish_native==12 && transform_finish_checked==8 && transform_finish_fallback==6);transform_finish_mode=0;
    puts("Complete transform finish routing:both publications/modes/depths,biased stack/manager,overwritten return link;owner/off/foreign/image/invalid/post-scope PASS");
    index_unpack_mode=1;buzz_geometry_job_index_unpack(native.get(),index_unpack_reference,index_unpack_oracle::buzz_job_6eba_spu_func_00007730,index_unpack_oracle::buzz_job_6eba_spu_func_000077F8);
    assert(index_unpack_references==12 && index_unpack_native==18 && index_unpack_checked==12 && index_unpack_fallback==8);index_unpack_mode=0;
    puts("Complete index unpack routing: low/high,zero/nonzero,ordered aliases,both strict modes,tail collisions;owner/foreign/off/image/null-tail/invalid/post-scope PASS");
    index_deltas_mode=1;buzz_geometry_job_index_deltas(native.get(),index_deltas_reference,index_deltas_oracle::buzz_job_6eba_spu_func_000077F8);
    assert(index_deltas_references==8 && index_deltas_native==9 && index_deltas_fallback==4 && index_deltas_checked==6);index_deltas_mode=0;
    puts("Complete index deltas routing: empty/one-group/bulk/aliases; both strict modes; owner/foreign/disabled/image/null-tail/unsupported/post-scope PASS");
    skin_normalized_mode=1;buzz_geometry_job_skin_normalized(native.get(),skin_normalized_reference,skin_normalized_oracle::buzz_job_6eba_spu_func_0000B868,1);
    assert(skin_normalized_references==7 && skin_normalized_native==27 && skin_normalized_fallback==3 && skin_normalized_checked==18);skin_normalized_mode=0;
    skin_cofactor_mode=1;buzz_geometry_job_skin_cofactor(native.get(),skin_cofactor_reference,skin_cofactor_oracle::buzz_job_6eba_spu_func_0000B4C0);
    assert(skin_cofactor_references==6 && skin_cofactor_native==9 && skin_cofactor_fallback==2 && skin_cofactor_checked==6);skin_cofactor_mode=0;
    rigid_skin_mode=1;buzz_geometry_job_rigid_skin(native.get(),rigid_skin_reference,rigid_skin_oracle::buzz_job_6eba_spu_func_0000B4C0,rigid_skin_oracle::buzz_job_6eba_spu_func_0000BB58);
    assert(rigid_skin_references==7 && rigid_skin_native==18 && rigid_skin_fallback==3 && rigid_skin_checked==12);rigid_skin_mode=0;
    skin_flow_mode=1;buzz_geometry_job_skin_flow(native.get(),skin_flow_reference,&skin_flow_oracle::library);
    assert(skin_flow_references==7 && skin_flow_native==9 && skin_flow_fallback==3 && skin_flow_checked==6);skin_flow_mode=0;
    skin_directions_mode=1;buzz_geometry_job_skin_directions(native.get(),skin_directions_reference,skin_directions_oracle::buzz_job_6eba_spu_func_0000B868,1);
    assert(skin_directions_references==7 && skin_directions_native==9 && skin_directions_fallback==3 && skin_directions_checked==6);skin_directions_mode=0;
    puts("Skin directions routing:1/2/3 streams; both strict modes; exact B868 tail; foreign/disabled/unsupported/post-scope PASS");
    attributes_enabled = false;
    puts("Attribute routing: owner native; foreign/disabled/null-table/unsupported/post-scope PASS");
    routing_probe = commands_enabled = command_publish_enabled = emit_enabled = full_enabled = false;
    puts("Command preparation/publication/emission routing: owner native; reference/foreign/disabled/unsupported/post-scope checks PASS");
    for (scenario = 0; scenario < 1024; ++scenario) { synthetic(*native, scenario); compare(*native, *reference); }
    unsigned corpus = 0;
    if (argc > 1) for (const auto& path : std::filesystem::directory_iterator(argv[1])) {
        if (path.path().extension() != ".gz" || path.path().string().find(".bjc.gz") == std::string::npos) continue;
        const auto name = path.path().string(); current_case = name.c_str();
        gzFile file = gzopen(name.c_str(), "rb"); assert(file);
        uint32_t header[6]; assert(gzread(file, header, sizeof header) == sizeof header);
        assert(header[0] == 0x434a5a42 && header[1] == 1 && header[2] == 41 && header[5] == SPU_LS_SIZE);
        memset(native.get(), 0, sizeof(*native)); native->ls = native->ls_store; native->image_id = 41; native->pc = 0x1c18;
        assert(gzread(file, native->gpr, sizeof native->gpr) == sizeof native->gpr);
        assert(gzread(file, native->ls, SPU_LS_SIZE) == SPU_LS_SIZE); gzclose(file);
        scenario = corpus++ % 1024; compare(*native, *reference);
    }
    if (argc > 1) assert(corpus);
    _mm_setcsr(previous);
    printf("Native outer driver: 1024 mock-pipeline scenarios, %u real entries, exact call/argument/nonvolatile-spill transcript and full exit state PASS\n", corpus);
}

#endif
