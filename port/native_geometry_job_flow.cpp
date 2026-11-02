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
#include "native_geometry_run_lists.h"
#include "native_geometry_custom_stream.h"
#include "native_geometry_skin_dispatch.h"
#include "native_geometry_weighted_skin.h"
#include "native_geometry_skin_directions.h"
#include "native_geometry_skin_flow.h"
#include "native_geometry_rigid_skin.h"
#include "native_geometry_skin_cofactor.h"
#include "native_geometry_skin_normalized.h"
#include "native_geometry_index_deltas.h"
#include "native_geometry_index_unpack.h"
#include "native_geometry_index_expand.h"
#include "native_geometry_index_relocate.h"
#include "native_geometry_index_triangles.h"
#include "native_geometry_index_control.h"
#include "native_geometry_transform_finish.h"
#include "native_geometry_vertex_prepare.h"
#include "native_geometry_transform_control.h"
#include "native_geometry_alternate_cull.h"
#include "native_geometry_ring.h"
#include "native_geometry_float_output.h"
#include "native_geometry_half_output.h"
#include "native_geometry_cmp_output.h"
#include "native_geometry_snorm_output.h"
#include "native_geometry_unorm_output.h"
#include "native_geometry_u8_integer_output.h"
#include "native_geometry_i16_output.h"
#include "native_geometry_formatter.h"
#include "native_geometry_conversion_control.h"
#include "native_geometry_input_control.h"
#include "native_geometry_builtin_float3_output.h"
#include "native_geometry_builtin20_output.h"
#include "native_geometry_builtin24_cmp_output.h"
#include "native_geometry_descriptors.h"
#include "native_function_check.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <memory>

namespace {
thread_local spu_context* native_pipeline_context = nullptr;
inline bool check_job_stages() { return buzz_geometry_job_mode()==2 && buzz_geometry_job_debug_stages(); }
thread_local const spu_context* prepared_job_owner=nullptr;
thread_local buzz::GeometryJobSetup prepared_job{};
thread_local buzz::GeometryHostOutputRecipe prepared_host_output{};
// Only workers entering the experimental host path allocate its storage.
thread_local std::unique_ptr<buzz::GeometryHostJobWorkspace> host_job_workspace;
inline buzz::GeometryHostJobWorkspace& worker_host_job()
{
    if(!host_job_workspace)host_job_workspace=std::make_unique<buzz::GeometryHostJobWorkspace>();
    return *host_job_workspace;
}
thread_local spu_context* native_input_control_context = nullptr;
// The enclosing conversion has already forecast the complete formatter,
// including its frame/cursor updates. Forward that proof to the one matching
// synchronous call; unrelated contexts and independent formatter calls recheck.
struct FormatterAdmissionScope;
thread_local FormatterAdmissionScope* formatter_admission=nullptr;
struct FormatterAdmissionScope {
    FormatterAdmissionScope* previous;
    spu_context* context;const buzz::GeometryFormatter* plan;
    uint32_t manager,stack;
    FormatterAdmissionScope(spu_context* c,const buzz::GeometryFormatter* p)
        :previous(formatter_admission),context(c),plan(p),manager(c->gpr[3]._u32[0]),stack(c->gpr[1]._u32[0]-96){formatter_admission=this;}
    ~FormatterAdmissionScope(){formatter_admission=previous;}
    const buzz::GeometryFormatter* take(spu_context* c) {
        if(!plan || context!=c || c->gpr[3]._u32[0]!=manager || c->gpr[1]._u32[0]!=stack || c->gpr[0]._u32[0]!=0x56c4)return nullptr;
        context=nullptr;return plan;
    }
};
struct NativeInputControlScope {
    spu_context* previous;
    explicit NativeInputControlScope(spu_context* c):previous(native_input_control_context){native_input_control_context=c;}
    ~NativeInputControlScope(){native_input_control_context=previous;}
};
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
struct RingReference {
    buzz_geometry_job_fn entry;
    const buzz_geometry_ring_functions* functions;
    unsigned frontier;
};
thread_local const RingReference* ring_reference=nullptr;
void run_ring_reference(spu_context* ctx)
{
    const auto* original=ring_reference;
    if(!original)buzz_geometry_job_native_fail(ctx,"missing ring reference scope");
    buzz::run_geometry_ring_reference(*ctx,original->entry,*original->functions,original->frontier);
}
}

extern "C" void buzz_geometry_job_builtin_input(spu_context* ctx,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,buzz_geometry_job_fn finish,unsigned format)
{
    if(format<1 || format>5)buzz_geometry_job_native_fail(ctx,"unknown builtin input format");
    if(ctx!=native_pipeline_context || ctx!=native_input_control_context || ctx->image_id!=41 || !buzz_geometry_job_builtin_input_enabled(format)){fallback(ctx);return;}
    buzz::GeometryBuiltin20Input p1;buzz::GeometryBuiltin24Input p2;buzz::GeometryBuiltin24CmpInput p3;buzz::GeometryBuiltin12Input p4;buzz::GeometryBuiltin15Input p5;
    const char* reason=nullptr;bool admitted=reference && finish;
    if(admitted)switch(format){
    case 1:admitted=buzz::read_geometry_builtin20_input(*ctx,p1,&reason);break;
    case 2:admitted=buzz::read_geometry_builtin24_input(*ctx,p2,&reason);break;
    case 3:admitted=buzz::read_geometry_builtin24_cmp_input(*ctx,p3,&reason);break;
    case 4:admitted=buzz::read_geometry_builtin12_input(*ctx,p4,&reason);break;
    case 5:admitted=buzz::read_geometry_builtin15_input(*ctx,p5,&reason);break;
    }
    const unsigned stage=BUZZ_GEOMETRY_JOB_BUILTIN20_INPUT+format-1;
    if(!admitted){buzz_geometry_job_output_result(stage,0,0);fallback(ctx);return;}
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_builtin_input_enabled(format)==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!(comparison->pc==0x9498 && g_spu_trampoline_fn==finish);++steps){const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=65536)buzz_geometry_job_native_fail(ctx,"builtin input reference escaped data frontier");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    switch(format){
    case 1:buzz::decode_geometry_builtin20_input(*ctx,p1,finish);break;
    case 2:buzz::decode_geometry_builtin24_input(*ctx,p2,finish);break;
    case 3:buzz::decode_geometry_builtin24_cmp_input(*ctx,p3,finish);break;
    case 4:buzz::decode_geometry_builtin12_input(*ctx,p4,finish);break;
    case 5:buzz::decode_geometry_builtin15_input(*ctx,p5,finish);break;
    }
    if(comparison){const auto d=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id && ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);if(!d.exact() || !exact){std::fprintf(stderr,"[geometry-job/builtin-input%u] DIFF r%d byte%d LS=%X/%u state=%s\n",format,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");buzz_geometry_job_native_fail(ctx,"builtin input strict full state differs");}
        buzz_geometry_job_builtin_input_result(format,1);
    }
    buzz_geometry_job_builtin_input_result(format,0);buzz_geometry_job_output_result(stage,1,0);
}
extern "C" void buzz_geometry_job_input_control(spu_context* ctx,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,buzz_geometry_job_fn descriptors,const buzz_geometry_builtin_input_functions* functions)
{
    auto retained=[&]{buzz_geometry_job_decode_custom_stream(ctx,fallback,descriptors);};
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_input_control_enabled()){retained();return;}
    if(ctx->ls==ctx->ls_store && (ctx->gpr[6]._u32[0]&255)>=6 && spu_rotqbyi(spu_ls_read128(ctx,ctx->gpr[3]._u32[0]),6)._u16[0]){retained();return;}
    buzz::GeometryInputControl p;const char* reason=nullptr;bool admitted=reference && buzz::read_geometry_input_control(*ctx,p,&reason,true);
    if(admitted && !p.empty && p.format)admitted=functions && functions->finish && functions->data[p.format-1] && buzz_geometry_job_builtin_input_enabled(p.format);
    if(!admitted){buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INPUT_CONTROL,0,0);static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/input-control] first fallback: %s\n",reason?reason:"missing reference/selected native data switch/library");retained();return;}
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_input_control_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;reference(comparison);expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    {NativeInputControlScope scope(ctx);buzz::execute_geometry_input_control(*ctx,p,functions);}
    if(comparison){const auto d=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id && ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);if(!d.exact() || !exact){std::fprintf(stderr,"[geometry-job/input-control] DIFF r%d byte%d LS=%X/%u state=%s\n",d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");buzz_geometry_job_native_fail(ctx,"input control strict full state differs");}
        buzz_geometry_job_input_control_result(p.empty?0:p.format+1,1);
    }
    buzz_geometry_job_input_control_result(p.empty?0:p.format+1,0);buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INPUT_CONTROL,1,0);
}

extern "C" void buzz_geometry_job_builtin_output(spu_context* ctx,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,buzz_geometry_job_fn finish,unsigned format)
{
    if(format>=4)buzz_geometry_job_native_fail(ctx,"unknown builtin output format");
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_builtin_output_enabled(format)){
        fallback(ctx);return;
    }
    buzz::GeometryBuiltinFloat3Output p0;buzz::GeometryBuiltin20Output p1;
    buzz::GeometryBuiltin24Output p2;buzz::GeometryBuiltin24CmpOutput p3;const char* reason=nullptr;
    bool admitted=reference && finish;
    if(admitted)switch(format){
        case 0:admitted=buzz::read_geometry_builtin_float3_output(*ctx,p0,&reason);break;
        case 1:admitted=buzz::read_geometry_builtin20_output(*ctx,p1,&reason);break;
        case 2:admitted=buzz::read_geometry_builtin24_output(*ctx,p2,&reason);break;
        case 3:admitted=buzz::read_geometry_builtin24_cmp_output(*ctx,p3,&reason);break;
    }
    const unsigned stage=BUZZ_GEOMETRY_JOB_BUILTIN_FLOAT3_OUTPUT+format;
    if(!admitted){
        buzz_geometry_job_output_result(stage,0,0);
        static std::atomic_flag reported[4]={ATOMIC_FLAG_INIT,ATOMIC_FLAG_INIT,ATOMIC_FLAG_INIT,ATOMIC_FLAG_INIT};
        if(!reported[format].test_and_set())std::fprintf(stderr,"[geometry-job/builtin-output%u] first fallback: %s\n",format,reason?reason:"missing reference/continuation");
        fallback(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_builtin_output_enabled(format)==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!(comparison->pc==0x5648 && g_spu_trampoline_fn==finish);++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=65536)buzz_geometry_job_native_fail(ctx,"builtin output reference escaped data frontier");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    switch(format){
        case 0:buzz::emit_geometry_builtin_float3_output(*ctx,p0,finish);break;
        case 1:buzz::emit_geometry_builtin20_output(*ctx,p1,finish);break;
        case 2:buzz::emit_geometry_builtin24_output(*ctx,p2,finish);break;
        case 3:buzz::emit_geometry_builtin24_cmp_output(*ctx,p3,finish);break;
    }
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/builtin-output%u] DIFF r%d byte%d LS=%X/%u state=%s\n",format,diff.first_register,diff.first_register_byte,diff.ls_bytes?diff.first_ls:0,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"builtin output strict data/LS/register/control/continuation differs");
        }
        buzz_geometry_job_builtin_output_result(format,1);
    }
    buzz_geometry_job_builtin_output_result(format,0);buzz_geometry_job_output_result(stage,1,0);
}

extern "C" void buzz_geometry_job_conversion_control(spu_context* ctx,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,buzz_geometry_job_fn formatter,const buzz_geometry_builtin_output_functions* builtins)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_conversion_control_enabled()){
        fallback(ctx);return;
    }
    buzz::GeometryConversionAdmission plan;const char* reason=nullptr;
    bool admitted=reference && buzz::read_geometry_conversion_native(*ctx,plan,&reason,buzz_geometry_job_formatter_spans_enabled()!=0);
    if(admitted && plan.path==buzz::GeometryConversionPath::descriptor){
        if(!formatter || !buzz_geometry_job_formatter_enabled()){admitted=false;reason="native formatter disabled or missing";}
        const int modes[]={buzz_geometry_job_snorm_output_enabled(),buzz_geometry_job_float_output_enabled(),
            buzz_geometry_job_half_output_enabled(),buzz_geometry_job_unorm_output_enabled(),
            buzz_geometry_job_i16_output_enabled(),buzz_geometry_job_cmp_output_enabled(),buzz_geometry_job_u8_integer_output_enabled()};
        for(unsigned i=0;i<7;++i)if((plan.formatter.mask&(1u<<i)) && !modes[i]){
            admitted=false;reason="used output encoder disabled";break;
        }
    }
    if(admitted && unsigned(plan.path)>=unsigned(buzz::GeometryConversionPath::builtin0)){
        const unsigned format=unsigned(plan.path)-unsigned(buzz::GeometryConversionPath::builtin0);
        if(!builtins || !builtins->finish || !builtins->data[format] || !buzz_geometry_job_builtin_output_enabled(format)){
            admitted=false;reason="used builtin output disabled or missing";
        }
    }
    if(!admitted){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_CONVERSION_CONTROL,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/conversion-control] first fallback: %s\n",reason?reason:"missing conversion reference");
        fallback(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_conversion_control_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    const buzz::GeometryConversionCallees functions{formatter,buzz::geometry_conversion_stride,builtins};
    FormatterAdmissionScope forecast(ctx,plan.path==buzz::GeometryConversionPath::descriptor?&plan.formatter:nullptr);
    const auto path=buzz::execute_geometry_conversion_control(*ctx,plan.control,functions);
    if(path!=plan.path)buzz_geometry_job_native_fail(ctx,"conversion control path escaped admission");
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/conversion-control] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"conversion control strict data/LS/register/control/continuation differs");
        }
        buzz_geometry_job_conversion_control_result(unsigned(path),1);
    }
    buzz_geometry_job_conversion_control_result(unsigned(path),0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_CONVERSION_CONTROL,1,0);
}

namespace {
// Admission uses the live host bank. The physical bank can still contain the
// formatter's entry state; normal execution does not publish a child ABI.
template<class Plan>
bool formatter_output_registers(spu_context& ctx,u128* r,unsigned stage,int mode,
    const buzz::GeometryFormatterCallees& functions,unsigned storage,unsigned& strict_children,
    bool (*read)(const spu_context&,Plan&,const char**,const u128*),
    bool (*emit)(spu_context&,u128*,const Plan&),void (*result)(unsigned,unsigned,int))
{
    Plan plan{};
    const bool check=check_job_stages() || mode==2;
    if(check){
        // Optional per-function debugging may independently verify admission.
        const char* reason=nullptr;
        if(!read(ctx,plan,&reason,r)){
            std::fprintf(stderr,"[geometry-job/formatter-registers] child%u admission invariant: %s\n",storage,reason?reason:"unknown");
            buzz_geometry_job_native_fail(&ctx,"admitted formatter child rejected after mutation;cannot fall back");
        }
    }else if(r[77]._u32[0]){
        // Whole-formatter admission already proved each child's stores and
        // loop budgets. Only the coverage counters need column/cross totals.
        plan.columns=r[60]._u32[0]?r[60]._u32[0]:1;
        for(unsigned col=0;col<plan.columns;++col){
            const auto route=spu_mpya(r[55],spu_a(spu_il(col),r[54]),r[53]);
            plan.cross_columns+=spu_cgti(spu_a(spu_andi(route,15),r[48]),16)._u32[0]!=0;
        }
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || mode==2){
        comparison=buzz::clone_function_state(&ctx);
        memcpy(comparison->gpr,r,sizeof(comparison->gpr));comparison->pc=r[4]._u32[0];
        const auto scheduled=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;
        // This is a different context from native_pipeline_context, so the
        // production data wrapper takes its retained original/reference path.
        functions.data[storage-1](comparison);
        const unsigned budget=stage==BUZZ_GEOMETRY_JOB_FLOAT_OUTPUT?1024:2048;
        for(unsigned steps=0;!((comparison->pc==0x4610 && g_spu_trampoline_fn==functions.finish) ||
                              (comparison->pc==0x4618 && g_spu_trampoline_fn==functions.skip));++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=budget)
                buzz_geometry_job_native_fail(&ctx,"formatter register-bank reference escaped encoder frontier");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    const bool converted=emit(ctx,r,plan);
    if(comparison){
        // A requested strict boundary materializes the actual bank. There is
        // no corresponding round trip in unchecked native execution.
        if(r!=ctx.gpr)memcpy(ctx.gpr,r,sizeof(ctx.gpr));ctx.pc=converted?0x4610:0x4618;
        g_spu_trampoline_fn=converted?functions.finish:functions.skip;
        const auto diff=buzz::compare_function_state(ctx,*comparison);
        const bool exact=ctx.host_depth==comparison->host_depth && ctx.image_id==comparison->image_id &&
            ctx.policy_mode==comparison->policy_mode && ctx.resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/formatter-registers] child%u DIFF r%d byte%d LS=%X/%u state=%s\n",storage,diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(&ctx,"formatter register-bank encoder strict state differs");
        }
        result(plan.columns,plan.cross_columns,1);++strict_children;
    }
    result(plan.columns,plan.cross_columns,0);buzz_geometry_job_output_result(stage,1,0);
    return converted;
}
}

extern "C" void buzz_geometry_job_formatter(spu_context* ctx,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,const buzz_geometry_formatter_functions* functions)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_formatter_enabled()){
        fallback(ctx);return;
    }
    buzz::GeometryFormatter plan;const char* reason=nullptr;
    bool admitted=reference && functions && functions->finish && functions->skip;
    if(admitted){
        const auto* forecast=formatter_admission?formatter_admission->take(ctx):nullptr;
        if(forecast)plan=*forecast;
        else admitted=buzz::read_geometry_formatter(*ctx,plan,&reason,nullptr,buzz_geometry_job_formatter_spans_enabled()!=0);
    }
    std::array<int,7> modes{};
    if(admitted){
        modes={buzz_geometry_job_snorm_output_enabled(),buzz_geometry_job_float_output_enabled(),
            buzz_geometry_job_half_output_enabled(),buzz_geometry_job_unorm_output_enabled(),
            buzz_geometry_job_i16_output_enabled(),buzz_geometry_job_cmp_output_enabled(),buzz_geometry_job_u8_integer_output_enabled()};
        // The formatter's read-only scan checks every eventual data store and
        // the same iteration bounds as the complete data readers. Its stronger
        // whole-root budget and immutable metadata prove those child layouts
        // remain admitted. Require each USED complete encoder switch as well;
        // disabled unused types must not block a supported root.
        for(unsigned i=0;i<7;++i)if((plan.mask&(1u<<i)) && (!modes[i] || !functions->data[i])){
            admitted=false;reason="used output encoder disabled or missing";break;
        }
    }
    if(!admitted){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_FORMATTER,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/formatter] first fallback: %s\n",reason?reason:"missing formatter reference/callee");
        fallback(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_formatter_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    buzz::GeometryFormatterCallees callees;
    for(unsigned i=0;i<7;++i)callees.data[i]=functions->data[i];
    callees.finish=functions->finish;callees.skip=functions->skip;
    const bool parent_checked=comparison!=nullptr;
    unsigned children=0,strict_children=0;
    const bool registers=buzz_geometry_job_formatter_registers_enabled()!=0;
    if(registers){
        buzz::execute_geometry_formatter_registers(*ctx,plan,[&](spu_context& c,u128* r,unsigned storage){
            ++children;
            switch(storage){
            case 1:return formatter_output_registers(c,r,BUZZ_GEOMETRY_JOB_SNORM_OUTPUT,modes[0],callees,storage,strict_children,buzz::read_geometry_snorm_output,buzz::emit_geometry_snorm_output_registers,buzz_geometry_job_snorm_output_result);
            case 2:return formatter_output_registers(c,r,BUZZ_GEOMETRY_JOB_FLOAT_OUTPUT,modes[1],callees,storage,strict_children,buzz::read_geometry_float_output,buzz::emit_geometry_float_output_registers,buzz_geometry_job_float_output_result);
            case 3:return formatter_output_registers(c,r,BUZZ_GEOMETRY_JOB_HALF_OUTPUT,modes[2],callees,storage,strict_children,buzz::read_geometry_half_output,buzz::emit_geometry_half_output_registers,buzz_geometry_job_half_output_result);
            case 4:return formatter_output_registers(c,r,BUZZ_GEOMETRY_JOB_UNORM_OUTPUT,modes[3],callees,storage,strict_children,buzz::read_geometry_unorm_output,buzz::emit_geometry_unorm_output_registers,buzz_geometry_job_unorm_output_result);
            case 5:return formatter_output_registers(c,r,BUZZ_GEOMETRY_JOB_I16_OUTPUT,modes[4],callees,storage,strict_children,buzz::read_geometry_i16_output,buzz::emit_geometry_i16_output_registers,buzz_geometry_job_i16_output_result);
            case 6:return formatter_output_registers(c,r,BUZZ_GEOMETRY_JOB_CMP_OUTPUT,modes[5],callees,storage,strict_children,buzz::read_geometry_cmp_output,buzz::emit_geometry_cmp_output_registers,buzz_geometry_job_cmp_output_result);
            case 7:return formatter_output_registers(c,r,BUZZ_GEOMETRY_JOB_U8_INTEGER_OUTPUT,modes[6],callees,storage,strict_children,buzz::read_geometry_u8_integer_output,buzz::emit_geometry_u8_integer_output_registers,buzz_geometry_job_u8_integer_output_result);
            default:buzz_geometry_job_native_fail(&c,"formatter register-bank storage invariant");return false;
            }
        });
    }else buzz::execute_geometry_formatter(*ctx,plan,callees);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/formatter] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"formatter strict data/LS/register/control/continuation differs");
        }
        buzz_geometry_job_formatter_result(plan.fields,plan.known,plan.skipped,plan.mask,1);
    }
    buzz_geometry_job_formatter_result(plan.fields,plan.known,plan.skipped,plan.mask,0);
    if(registers){
        if(children!=plan.known)buzz_geometry_job_native_fail(ctx,"formatter register-bank child coverage invariant");
        buzz_geometry_job_formatter_registers_result(children,strict_children,parent_checked);
    }
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_FORMATTER,1,0);
}

extern "C" void buzz_geometry_job_i16_output(spu_context* ctx,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,buzz_geometry_job_fn finish,buzz_geometry_job_fn skip)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_i16_output_enabled()){
        fallback(ctx);return;
    }
    buzz::GeometryI16Output plan;const char* reason=nullptr;
    if(!reference || !finish || !skip || !buzz::read_geometry_i16_output(*ctx,plan,&reason)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_I16_OUTPUT,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/i16-output] first fallback: %s\n",reason?reason:"missing continuation");
        fallback(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_i16_output_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!((comparison->pc==0x4610 && g_spu_trampoline_fn==finish) || (comparison->pc==0x4618 && g_spu_trampoline_fn==skip));++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=2048)buzz_geometry_job_native_fail(ctx,"I16 output reference escaped formatter frontier");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    buzz::emit_geometry_i16_output(*ctx,plan,finish,skip);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/i16-output] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"I16 output strict data/LS/register/control/continuation differs");
        }
        buzz_geometry_job_i16_output_result(plan.columns,plan.cross_columns,1);
    }
    buzz_geometry_job_i16_output_result(plan.columns,plan.cross_columns,0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_I16_OUTPUT,1,0);
}

extern "C" void buzz_geometry_job_u8_integer_output(spu_context* ctx,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,buzz_geometry_job_fn finish,buzz_geometry_job_fn skip)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_u8_integer_output_enabled()){
        fallback(ctx);return;
    }
    buzz::GeometryU8IntegerOutput plan;const char* reason=nullptr;
    if(!reference || !finish || !skip || !buzz::read_geometry_u8_integer_output(*ctx,plan,&reason)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_U8_INTEGER_OUTPUT,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/u8-integer-output] first fallback: %s\n",reason?reason:"missing continuation");
        fallback(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_u8_integer_output_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!((comparison->pc==0x4610 && g_spu_trampoline_fn==finish) || (comparison->pc==0x4618 && g_spu_trampoline_fn==skip));++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=2048)buzz_geometry_job_native_fail(ctx,"U8 integer output reference escaped formatter frontier");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    buzz::emit_geometry_u8_integer_output(*ctx,plan,finish,skip);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/u8-integer-output] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"U8 integer output strict data/LS/register/control/continuation differs");
        }
        buzz_geometry_job_u8_integer_output_result(plan.columns,plan.cross_columns,1);
    }
    buzz_geometry_job_u8_integer_output_result(plan.columns,plan.cross_columns,0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_U8_INTEGER_OUTPUT,1,0);
}

extern "C" void buzz_geometry_job_unorm_output(spu_context* ctx,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,buzz_geometry_job_fn finish,buzz_geometry_job_fn skip)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_unorm_output_enabled()){
        fallback(ctx);return;
    }
    buzz::GeometryUnormOutput plan;const char* reason=nullptr;
    if(!reference || !finish || !skip || !buzz::read_geometry_unorm_output(*ctx,plan,&reason)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_UNORM_OUTPUT,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/unorm-output] first fallback: %s\n",reason?reason:"missing continuation");
        fallback(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_unorm_output_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!((comparison->pc==0x4610 && g_spu_trampoline_fn==finish) || (comparison->pc==0x4618 && g_spu_trampoline_fn==skip));++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=2048)buzz_geometry_job_native_fail(ctx,"UNORM output reference escaped formatter frontier");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    buzz::emit_geometry_unorm_output(*ctx,plan,finish,skip);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/unorm-output] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"UNORM output strict data/LS/register/control/continuation differs");
        }
        buzz_geometry_job_unorm_output_result(plan.columns,plan.cross_columns,1);
    }
    buzz_geometry_job_unorm_output_result(plan.columns,plan.cross_columns,0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_UNORM_OUTPUT,1,0);
}

extern "C" void buzz_geometry_job_snorm_output(spu_context* ctx,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,buzz_geometry_job_fn finish,buzz_geometry_job_fn skip)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_snorm_output_enabled()){
        fallback(ctx);return;
    }
    buzz::GeometrySnormOutput plan;const char* reason=nullptr;
    if(!reference || !finish || !skip || !buzz::read_geometry_snorm_output(*ctx,plan,&reason)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SNORM_OUTPUT,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/snorm-output] first fallback: %s\n",reason?reason:"missing continuation");
        fallback(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_snorm_output_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!((comparison->pc==0x4610 && g_spu_trampoline_fn==finish) || (comparison->pc==0x4618 && g_spu_trampoline_fn==skip));++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=2048)buzz_geometry_job_native_fail(ctx,"SNORM output reference escaped formatter frontier");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    buzz::emit_geometry_snorm_output(*ctx,plan,finish,skip);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/snorm-output] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"SNORM output strict data/LS/register/control/continuation differs");
        }
        buzz_geometry_job_snorm_output_result(plan.columns,plan.cross_columns,1);
    }
    buzz_geometry_job_snorm_output_result(plan.columns,plan.cross_columns,0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SNORM_OUTPUT,1,0);
}

extern "C" void buzz_geometry_job_cmp_output(spu_context* ctx,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,buzz_geometry_job_fn finish,buzz_geometry_job_fn skip)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_cmp_output_enabled()){
        fallback(ctx);return;
    }
    buzz::GeometryCmpOutput plan;const char* reason=nullptr;
    if(!reference || !finish || !skip || !buzz::read_geometry_cmp_output(*ctx,plan,&reason)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_CMP_OUTPUT,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/cmp-output] first fallback: %s\n",reason?reason:"missing continuation");
        fallback(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_cmp_output_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!((comparison->pc==0x4610 && g_spu_trampoline_fn==finish) || (comparison->pc==0x4618 && g_spu_trampoline_fn==skip));++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=2048)buzz_geometry_job_native_fail(ctx,"CMP output reference escaped formatter frontier");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    buzz::emit_geometry_cmp_output(*ctx,plan,finish,skip);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/cmp-output] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"CMP output strict data/LS/register/control/continuation differs");
        }
        buzz_geometry_job_cmp_output_result(plan.columns,plan.cross_columns,1);
    }
    buzz_geometry_job_cmp_output_result(plan.columns,plan.cross_columns,0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_CMP_OUTPUT,1,0);
}

extern "C" void buzz_geometry_job_half_output(spu_context* ctx,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,buzz_geometry_job_fn finish,buzz_geometry_job_fn skip)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_half_output_enabled()){
        fallback(ctx);return;
    }
    buzz::GeometryHalfOutput plan;const char* reason=nullptr;
    if(!reference || !finish || !skip || !buzz::read_geometry_half_output(*ctx,plan,&reason)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_HALF_OUTPUT,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/half-output] first fallback: %s\n",reason?reason:"missing continuation");
        fallback(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_half_output_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!((comparison->pc==0x4610 && g_spu_trampoline_fn==finish) || (comparison->pc==0x4618 && g_spu_trampoline_fn==skip));++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=2048)buzz_geometry_job_native_fail(ctx,"half output reference escaped formatter frontier");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    buzz::emit_geometry_half_output(*ctx,plan,finish,skip);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/half-output] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"half output strict data/LS/register/control/continuation differs");
        }
        buzz_geometry_job_half_output_result(plan.columns,plan.cross_columns,1);
    }
    buzz_geometry_job_half_output_result(plan.columns,plan.cross_columns,0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_HALF_OUTPUT,1,0);
}

extern "C" void buzz_geometry_job_float_output(spu_context* ctx,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,buzz_geometry_job_fn finish,buzz_geometry_job_fn skip)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_float_output_enabled()){
        fallback(ctx);return;
    }
    buzz::GeometryFloatOutput plan;const char* reason=nullptr;
    if(!reference || !finish || !skip || !buzz::read_geometry_float_output(*ctx,plan,&reason)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_FLOAT_OUTPUT,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/float-output] first fallback: %s\n",reason?reason:"missing continuation");
        fallback(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_float_output_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!((comparison->pc==0x4610 && g_spu_trampoline_fn==finish) || (comparison->pc==0x4618 && g_spu_trampoline_fn==skip));++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=1024)buzz_geometry_job_native_fail(ctx,"float output reference escaped formatter frontier");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    buzz::emit_geometry_float_output(*ctx,plan,finish,skip);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/float-output] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"float output strict data/LS/register/control/continuation differs");
        }
        buzz_geometry_job_float_output_result(plan.columns,plan.cross_columns,1);
    }
    buzz_geometry_job_float_output_result(plan.columns,plan.cross_columns,0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_FLOAT_OUTPUT,1,0);
}

extern "C" void buzz_geometry_job_ring(spu_context* ctx,buzz_geometry_job_fn reference,const buzz_geometry_ring_functions* functions)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_ring_enabled()){
        reference(ctx);return;
    }
    buzz::GeometryRing plan;
    if(!functions || !functions->tail || !functions->initial_report || !functions->shared_report || !buzz::read_geometry_ring(*ctx,plan)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_RING,0,0);reference(ctx);return;
    }
    auto* check=buzz_geometry_job_io_root_begin(ctx);
    const buzz::GeometryRingContinuations callbacks{functions->initial_report,functions->shared_report};
    const unsigned frontier=buzz::allocate_geometry_ring(*ctx,plan,callbacks);
    const RingReference original{reference,functions,frontier};const auto* previous=ring_reference;ring_reference=&original;
    buzz_geometry_job_io_root_end(ctx,check,run_ring_reference);ring_reference=previous;
    if(check)buzz_geometry_job_ring_result(frontier,1);
    buzz_geometry_job_ring_result(frontier,0);
    // Retained reporting starts from the current state after I/O. It never
    // restarts the root, and does not earn a complete native-return credit.
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_RING,!frontier,0);
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

extern "C" void buzz_geometry_job_skin_flow(spu_context* ctx,buzz_geometry_job_fn reference,
                                             const buzz_geometry_skin_flow_functions* functions)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_skin_flow_enabled()){
        reference(ctx);return;
    }
    buzz::GeometrySkinFlow plan;
    bool complete=functions && functions->position_pointer && functions->normal_pointer && functions->tangent_pointer &&
        functions->aux_pointer && functions->publish_palette && functions->weighted && functions->rigid &&
        functions->direction_entry && functions->batch_entry;
    if(complete)for(auto fn:functions->directions)complete=complete && fn;
    if(!complete || !buzz::read_geometry_skin_flow(*ctx,plan)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SKIN_FLOW,0,0);reference(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_skin_flow_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        const uint32_t link=ctx->gpr[0]._u32[0],stack=ctx->gpr[1]._u32[0];
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;;++steps){
            const auto next=g_spu_trampoline_fn;
            if(comparison->pc==link && comparison->gpr[1]._u32[0]==stack &&
               (comparison->host_depth ? next==nullptr : next==spu_indirect_branch))break;
            if(!next || steps>=1048576)buzz_geometry_job_native_fail(ctx,"skin flow reference escaped return");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    buzz::execute_geometry_skin_flow(*ctx,plan,*functions);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/skin-flow] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"skin flow strict state/return differs");
        }
        buzz_geometry_job_skin_flow_result(plan.work,1);
    }
    buzz_geometry_job_skin_flow_result(plan.work,0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SKIN_FLOW,1,0);
}

extern "C" void buzz_geometry_job_skin_dispatch(spu_context* ctx,buzz_geometry_job_fn reference,
                                                 const buzz_geometry_skin_dispatch_functions* functions)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_skin_dispatch_enabled()){
        reference(ctx);return;
    }
    buzz::GeometrySkinDispatch plan;
    if(!functions || !functions->position_pointer || !functions->normal_pointer || !functions->tangent_pointer ||
       !functions->transform || !functions->normalize || !functions->weighted || !buzz::read_geometry_skin_dispatch(*ctx,plan)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SKIN_DISPATCH,0,0);reference(ctx);return;
    }
    spu_context* comparison=nullptr;auto expected=buzz_geometry_job_fn(nullptr);
    if(check_job_stages() || buzz_geometry_job_skin_dispatch_enabled()==2){
        comparison=buzz::clone_function_state(ctx);auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;;++steps){
            auto next=g_spu_trampoline_fn;
            if(next==functions->weighted || next==functions->normalize)break;
            if(!next || next==spu_indirect_branch){
                if(comparison->pc!=comparison->gpr[0]._u32[0] ||
                   (comparison->host_depth==0)!=(next==spu_indirect_branch))
                    buzz_geometry_job_native_fail(ctx,"skin dispatch reference escaped return");
                break;
            }
            if(steps>=32)buzz_geometry_job_native_fail(ctx,"skin dispatch reference escaped boundary");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;
        if((plan.weighted && (expected!=functions->weighted || comparison->pc!=0xaf18)) ||
           (!plan.weighted && expected==functions->weighted) ||
           (expected==functions->normalize && comparison->pc!=0x2c00))
            buzz_geometry_job_native_fail(ctx,"skin dispatch reference tail differs");
        g_spu_trampoline_fn=scheduled;
    }
    buzz::dispatch_geometry_skin(*ctx,plan,*functions);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/skin-dispatch] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"skin dispatch strict state/boundary differs");
        }
        buzz_geometry_job_skin_dispatch_result(plan.weighted,1);
    }
    buzz_geometry_job_skin_dispatch_result(plan.weighted,0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SKIN_DISPATCH,1,0);
}

extern "C" void buzz_geometry_job_alternate_cull(spu_context* ctx,buzz_geometry_job_fn reference,buzz_geometry_job_fn finish)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_alternate_cull_enabled()){
        reference(ctx);return;
    }
    buzz::GeometryAlternateCull plan;
    if(!finish || !buzz::read_geometry_alternate_cull(*ctx,plan)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_ALTERNATE_CULL,0,0);reference(ctx);return;
    }
    spu_context* comparison=nullptr;
    if(check_job_stages() || buzz_geometry_job_alternate_cull_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!(comparison->pc==0x6958 && g_spu_trampoline_fn==finish);++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || steps>=2*plan.iterations+1 || (comparison->pc!=0x6b18 && comparison->pc!=0x6b10))
                buzz_geometry_job_native_fail(ctx,"alternate cull reference escaped data boundary");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        g_spu_trampoline_fn=scheduled;
    }
    buzz::cull_geometry_alternate(*ctx,plan,finish);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==finish;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/alternate-cull] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"alternate cull strict state/data/handoff differs");
        }
        buzz_geometry_job_alternate_cull_result(plan.iterations,1);
    }
    buzz_geometry_job_alternate_cull_result(plan.iterations,0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_ALTERNATE_CULL,1,0);
}

extern "C" void buzz_geometry_job_transform_reference_cull(spu_context* ctx,buzz_geometry_job_fn root,
                                                           buzz_geometry_job_fn boundary,buzz_geometry_job_fn replacement)
{
    // Bind the identical accepted data callback to the private original
    // controller's footer. No data algorithm or state repair runs here.
    buzz::run_geometry_transform_cull(*ctx,root,boundary);g_spu_trampoline_fn=replacement;
}
extern "C" void buzz_geometry_job_transform_control(spu_context* ctx,buzz_geometry_job_fn fallback,
                                                     buzz_geometry_job_fn reference,const buzz_geometry_transform_functions* f)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_transform_control_enabled()){
        fallback(ctx);return;
    }
    buzz::GeometryTransformControl plan;
    if(!reference || !f || !f->vertex || !f->cull || !f->alternate || !f->finish || !f->restore ||
       !buzz::read_geometry_transform_control(*ctx,plan)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_TRANSFORM_CONTROL,0,0);fallback(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_transform_control_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;const unsigned stack=ctx->gpr[1]._u32[0],depth=ctx->host_depth;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!(comparison->gpr[1]._u32[0]==stack && comparison->pc==comparison->gpr[0]._u32[0] &&
            g_spu_trampoline_fn==(depth?nullptr:spu_indirect_branch));++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=32)buzz_geometry_job_native_fail(ctx,"transform control reference escaped frame return");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    const auto path=buzz::execute_geometry_transform_control(*ctx,plan,*f);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/transform-control] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"transform control strict frame/setup/call/return differs");
        }
        buzz_geometry_job_transform_control_result(unsigned(path),1);
    }
    buzz_geometry_job_transform_control_result(unsigned(path),0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_TRANSFORM_CONTROL,1,0);
}

extern "C" void buzz_geometry_job_vertex_prepare(spu_context* ctx,buzz_geometry_job_fn reference,buzz_geometry_job_fn vertex)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_vertex_prepare_enabled()){
        reference(ctx);return;
    }
    buzz::GeometryVertexPrepare plan;
    if(!vertex || !buzz::read_geometry_vertex_prepare(*ctx,plan)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_VERTEX_PREPARE,0,0);reference(ctx);return;
    }
    spu_context* comparison=nullptr;
    if(check_job_stages() || buzz_geometry_job_vertex_prepare_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!(comparison->pc==0x63e0 && g_spu_trampoline_fn==vertex);++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=2)buzz_geometry_job_native_fail(ctx,"vertex preparation reference escaped Stage F handoff");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        g_spu_trampoline_fn=scheduled;
    }
    buzz::prepare_geometry_vertices(*ctx,plan,vertex);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==vertex;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/vertex-prepare] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"vertex preparation strict matrix/state/handoff differs");
        }
        buzz_geometry_job_vertex_prepare_result(plan.path,1);
    }
    buzz_geometry_job_vertex_prepare_result(plan.path,0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_VERTEX_PREPARE,1,0);
}

extern "C" void buzz_geometry_job_transform_finish(spu_context* ctx,buzz_geometry_job_fn reference)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_transform_finish_enabled()){
        reference(ctx);return;
    }
    buzz::GeometryTransformFinish plan;
    if(!buzz::read_geometry_transform_finish(*ctx,plan)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_TRANSFORM_FINISH,0,0);reference(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_transform_finish_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        const unsigned stack=ctx->gpr[1]._u32[0]+480,depth=ctx->host_depth;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!(comparison->gpr[1]._u32[0]==stack && comparison->pc==comparison->gpr[0]._u32[0] &&
            g_spu_trampoline_fn==(depth?nullptr:spu_indirect_branch));++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=4)buzz_geometry_job_native_fail(ctx,"transform finish reference escaped frame return");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    buzz::finish_geometry_transform(*ctx,plan);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/transform-finish] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"transform finish strict state/publication/frame/return differs");
        }
        buzz_geometry_job_transform_finish_result(plan.publish,1);
    }
    buzz_geometry_job_transform_finish_result(plan.publish,0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_TRANSFORM_FINISH,1,0);
}

extern "C" void buzz_geometry_job_index_control(spu_context* ctx,buzz_geometry_job_fn fallback,
                                                buzz_geometry_job_fn reference,const buzz_geometry_index_functions* functions)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_index_control_enabled()){
        fallback(ctx);return;
    }
    buzz::GeometryIndexControl plan;const char* reason=nullptr;
    const bool complete=reference && functions && functions->unpack && functions->deltas && functions->expansion_setup &&
        functions->expand && functions->relocate && functions->triangles && functions->publish;
    if(!complete || !buzz::read_geometry_index_control(*ctx,plan,&reason)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INDEX_CONTROL,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/index-control] first fallback: %s\n",reason?reason:"missing callback");
        fallback(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_index_control_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        const unsigned link=ctx->gpr[0]._u32[0],stack=ctx->gpr[1]._u32[0],depth=ctx->host_depth;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;!(comparison->pc==link && comparison->gpr[1]._u32[0]==stack &&
            g_spu_trampoline_fn==(depth?nullptr:spu_indirect_branch));++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || next==spu_indirect_branch || steps>=32)buzz_geometry_job_native_fail(ctx,"index control reference escaped root return");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;g_spu_trampoline_fn=scheduled;
    }
    buzz::execute_geometry_index_control(*ctx,plan,*functions);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/index-control] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"index control strict state/frame/publication/return differs");
        }
        buzz_geometry_job_index_control_result(unsigned(plan.path),1);
    }
    buzz_geometry_job_index_control_result(unsigned(plan.path),0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INDEX_CONTROL,1,0);
}

extern "C" void buzz_geometry_job_index_triangles(spu_context* ctx,buzz_geometry_job_fn fallback,
                                                  buzz_geometry_job_fn reference,buzz_geometry_job_fn publish)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_index_triangles_enabled()){
        fallback(ctx);return;
    }
    buzz::GeometryIndexTriangles plan;
    if(!reference || !publish || !buzz::read_geometry_index_triangles(*ctx,plan)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INDEX_TRIANGLES,0,0);fallback(ctx);return;
    }
    spu_context* comparison=nullptr;
    if(check_job_stages() || buzz_geometry_job_index_triangles_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        if(g_spu_trampoline_fn!=publish || comparison->pc!=0x2738u)
            buzz_geometry_job_native_fail(ctx,"triangle reference escaped publication boundary");
        g_spu_trampoline_fn=scheduled;
    }
    buzz::stitch_geometry_index_triangles(*ctx,plan,publish);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==publish;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/index-triangles] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"triangle strict state/frame/boundary differs");
        }
        buzz_geometry_job_index_triangles_result(plan.iterations,1);
    }
    buzz_geometry_job_index_triangles_result(plan.iterations,0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INDEX_TRIANGLES,1,0);
}

extern "C" void buzz_geometry_job_index_relocate(spu_context* ctx, buzz_geometry_job_fn reference,
                                                 buzz_geometry_job_fn triangles)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_index_relocate_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryIndexRelocate plan;
    if (!triangles || !buzz::read_geometry_index_relocate(*ctx, plan)) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INDEX_RELOCATE, 0, 0); reference(ctx); return;
    }
    spu_context* comparison = nullptr;
    if (check_job_stages() || buzz_geometry_job_index_relocate_enabled() == 2) {
        comparison = buzz::clone_function_state(ctx); const auto scheduled = g_spu_trampoline_fn;
        g_spu_trampoline_fn = nullptr; reference(comparison);
        for (unsigned steps = 0; g_spu_trampoline_fn != triangles; ++steps) {
            const auto next = g_spu_trampoline_fn;
            if (!next || steps >= 2) buzz_geometry_job_native_fail(ctx, "index relocation reference escaped data boundary");
            g_spu_trampoline_fn = nullptr; next(comparison);
        }
        if (comparison->pc != 0x7a90u) buzz_geometry_job_native_fail(ctx, "index relocation reference PC differs");
        g_spu_trampoline_fn = scheduled;
    }
    buzz::relocate_geometry_index_commands(*ctx, plan, triangles);
    if (comparison) {
        const auto diff = buzz::compare_function_state(*ctx, *comparison);
        const bool exact = ctx->host_depth == comparison->host_depth && ctx->image_id == comparison->image_id &&
            ctx->policy_mode == comparison->policy_mode && ctx->resident_ovl == comparison->resident_ovl && g_spu_trampoline_fn == triangles;
        buzz::release_function_state(comparison);
        if (!diff.exact() || !exact) {
            std::fprintf(stderr, "[geometry-job/index-relocate] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register, diff.first_register_byte, diff.first_ls, diff.ls_bytes, diff.first_state ? diff.first_state : "equal");
            buzz_geometry_job_native_fail(ctx, "index relocation strict data/state/boundary differs");
        }
        buzz_geometry_job_index_relocate_result(plan.iterations, 1);
    }
    buzz_geometry_job_index_relocate_result(plan.iterations, 0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INDEX_RELOCATE, 1, 0);
}

extern "C" void buzz_geometry_job_index_expand(spu_context* ctx, buzz_geometry_job_fn reference,
                                               buzz_geometry_job_fn commands, buzz_geometry_job_fn capacity_error)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_index_expand_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryIndexExpand plan;
    if (!commands || !capacity_error || !buzz::read_geometry_index_expand(*ctx, plan)) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INDEX_EXPAND, 0, 0); reference(ctx); return;
    }
    spu_context* comparison = nullptr; buzz_geometry_job_fn expected = nullptr;
    if (check_job_stages() || buzz_geometry_job_index_expand_enabled() == 2) {
        comparison = buzz::clone_function_state(ctx); const auto scheduled = g_spu_trampoline_fn;
        g_spu_trampoline_fn = nullptr; reference(comparison);
        for (unsigned steps = 0; g_spu_trampoline_fn != commands && g_spu_trampoline_fn != capacity_error; ++steps) {
            const auto next = g_spu_trampoline_fn;
            if (!next || steps >= 2) buzz_geometry_job_native_fail(ctx, "index expansion reference escaped data boundary");
            g_spu_trampoline_fn = nullptr; next(comparison);
        }
        expected = g_spu_trampoline_fn;
        if (comparison->pc != (expected == capacity_error ? 0x7dccu : 0x79a8u))
            buzz_geometry_job_native_fail(ctx, "index expansion reference PC differs");
        g_spu_trampoline_fn = scheduled;
    }
    buzz::expand_geometry_indices(*ctx, plan, commands, capacity_error);
    if (comparison) {
        const auto diff = buzz::compare_function_state(*ctx, *comparison);
        const bool exact = ctx->host_depth == comparison->host_depth && ctx->image_id == comparison->image_id &&
            ctx->policy_mode == comparison->policy_mode && ctx->resident_ovl == comparison->resident_ovl && g_spu_trampoline_fn == expected;
        buzz::release_function_state(comparison);
        if (!diff.exact() || !exact) {
            std::fprintf(stderr, "[geometry-job/index-expand] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register, diff.first_register_byte, diff.first_ls, diff.ls_bytes, diff.first_state ? diff.first_state : "equal");
            buzz_geometry_job_native_fail(ctx, "index expansion strict data/state/boundary differs");
        }
        buzz_geometry_job_index_expand_result(plan.iterations, 1, ctx->pc == 0x7dcc);
    }
    buzz_geometry_job_index_expand_result(plan.iterations, 0, ctx->pc == 0x7dcc);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INDEX_EXPAND, 1, 0);
}

extern "C" void buzz_geometry_job_index_unpack(spu_context* ctx, buzz_geometry_job_fn reference,
                                               buzz_geometry_job_fn deltas, buzz_geometry_job_fn expansion)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_index_unpack_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryIndexUnpack plan;
    if (!deltas || !expansion || !buzz::read_geometry_index_unpack(*ctx, plan)) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INDEX_UNPACK, 0, 0); reference(ctx); return;
    }
    spu_context* comparison = nullptr; buzz_geometry_job_fn expected = nullptr;
    if (check_job_stages() || buzz_geometry_job_index_unpack_enabled() == 2) {
        comparison = buzz::clone_function_state(ctx); const auto scheduled = g_spu_trampoline_fn;
        g_spu_trampoline_fn = nullptr; reference(comparison);
        for (unsigned steps = 0; g_spu_trampoline_fn != deltas && g_spu_trampoline_fn != expansion; ++steps) {
            const auto next = g_spu_trampoline_fn;
            if (!next || steps >= 5) buzz_geometry_job_native_fail(ctx, "index unpack reference escaped data boundary");
            g_spu_trampoline_fn = nullptr; next(comparison);
        }
        expected = g_spu_trampoline_fn;
        if (comparison->pc != (expected == deltas ? 0x7730u : 0x77f8u))
            buzz_geometry_job_native_fail(ctx, "index unpack reference PC differs");
        g_spu_trampoline_fn = scheduled;
    }
    buzz::unpack_geometry_indices(*ctx, plan, deltas, expansion);
    if (comparison) {
        const auto diff = buzz::compare_function_state(*ctx, *comparison);
        const bool exact = ctx->host_depth == comparison->host_depth && ctx->image_id == comparison->image_id &&
            ctx->policy_mode == comparison->policy_mode && ctx->resident_ovl == comparison->resident_ovl && g_spu_trampoline_fn == expected;
        buzz::release_function_state(comparison);
        if (!diff.exact() || !exact) {
            std::fprintf(stderr, "[geometry-job/index-unpack] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register, diff.first_register_byte, diff.first_ls, diff.ls_bytes, diff.first_state ? diff.first_state : "equal");
            buzz_geometry_job_native_fail(ctx, "index unpack strict data/state/boundary differs");
        }
        buzz_geometry_job_index_unpack_result(plan.width, 1);
    }
    buzz_geometry_job_index_unpack_result(plan.width, 0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INDEX_UNPACK, 1, 0);
}

extern "C" void buzz_geometry_job_index_deltas(spu_context* ctx, buzz_geometry_job_fn reference, buzz_geometry_job_fn tail)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_index_deltas_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryIndexDeltas plan;
    if (!tail || !buzz::read_geometry_index_deltas(*ctx, plan)) {
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INDEX_DELTAS, 0, 0); reference(ctx); return;
    }
    spu_context* comparison = nullptr;
    if (check_job_stages() || buzz_geometry_job_index_deltas_enabled() == 2) {
        comparison = buzz::clone_function_state(ctx); const auto scheduled = g_spu_trampoline_fn;
        g_spu_trampoline_fn = nullptr; reference(comparison);
        for (unsigned steps = 0; g_spu_trampoline_fn != tail; ++steps) {
            const auto next = g_spu_trampoline_fn;
            if (!next || steps >= 2) buzz_geometry_job_native_fail(ctx, "index delta reference escaped data boundary");
            g_spu_trampoline_fn = nullptr; next(comparison);
        }
        if (comparison->pc != 0x77f8) buzz_geometry_job_native_fail(ctx, "index delta reference PC differs");
        g_spu_trampoline_fn = scheduled;
    }
    buzz::reconstruct_geometry_index_deltas(*ctx, plan, tail);
    if (comparison) {
        const auto diff = buzz::compare_function_state(*ctx, *comparison);
        const bool exact = ctx->host_depth == comparison->host_depth && ctx->image_id == comparison->image_id &&
            ctx->policy_mode == comparison->policy_mode && ctx->resident_ovl == comparison->resident_ovl && g_spu_trampoline_fn == tail;
        buzz::release_function_state(comparison);
        if (!diff.exact() || !exact) {
            std::fprintf(stderr, "[geometry-job/index-deltas] DIFF r%d byte%d LS=%X/%u state=%s\n",
                diff.first_register, diff.first_register_byte, diff.first_ls, diff.ls_bytes, diff.first_state ? diff.first_state : "equal");
            buzz_geometry_job_native_fail(ctx, "index delta strict data/state/boundary differs");
        }
        buzz_geometry_job_index_deltas_result(plan.groups, 1, plan.aliases);
    }
    buzz_geometry_job_index_deltas_result(plan.groups, 0, plan.aliases);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_INDEX_DELTAS, 1, 0);
}

extern "C" void buzz_geometry_job_skin_normalized(spu_context* ctx,buzz_geometry_job_fn reference,buzz_geometry_job_fn tail,unsigned streams)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_skin_normalized_enabled()){
        reference(ctx);return;
    }
    buzz::GeometrySkinNormalized plan;
    if(!tail || !buzz::read_geometry_skin_normalized(*ctx,streams,plan)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SKIN_NORMALIZED,0,0);reference(ctx);return;
    }
    spu_context* comparison=nullptr;
    if(check_job_stages() || buzz_geometry_job_skin_normalized_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;g_spu_trampoline_fn!=tail;++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || steps>=8192)buzz_geometry_job_native_fail(ctx,"normalized skin reference escaped data boundary");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        if(comparison->pc!=0xb868)buzz_geometry_job_native_fail(ctx,"normalized skin reference PC differs");
        g_spu_trampoline_fn=scheduled;
    }
    buzz::transform_geometry_skin_normalized(*ctx,plan,tail);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==tail;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/skin-normalized] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"normalized skin strict data/state/boundary differs");
        }
        buzz_geometry_job_skin_normalized_result(streams,1,plan.aliases);
    }
    buzz_geometry_job_skin_normalized_result(streams,0,plan.aliases);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SKIN_NORMALIZED,1,0);
}

extern "C" void buzz_geometry_job_skin_cofactor(spu_context* ctx,buzz_geometry_job_fn reference,buzz_geometry_job_fn tail)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_skin_cofactor_enabled()){
        reference(ctx);return;
    }
    buzz::GeometrySkinCofactor plan;
    if(!tail || !buzz::read_geometry_skin_cofactor(*ctx,plan)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SKIN_COFACTOR,0,0);reference(ctx);return;
    }
    spu_context* comparison=nullptr;
    if(check_job_stages() || buzz_geometry_job_skin_cofactor_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;g_spu_trampoline_fn!=tail;++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || steps>=4)buzz_geometry_job_native_fail(ctx,"skin cofactor reference escaped data boundary");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        if(comparison->pc!=0xb4c0)buzz_geometry_job_native_fail(ctx,"skin cofactor reference PC differs");
        g_spu_trampoline_fn=scheduled;
    }
    buzz::transform_geometry_skin_cofactor(*ctx,plan,tail);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==tail;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/skin-cofactor] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"skin cofactor strict data/state/boundary differs");
        }
        buzz_geometry_job_skin_cofactor_result(plan.count,1,plan.aliases);
    }
    buzz_geometry_job_skin_cofactor_result(plan.count,0,plan.aliases);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SKIN_COFACTOR,1,0);
}

extern "C" void buzz_geometry_job_rigid_skin(spu_context* ctx,buzz_geometry_job_fn reference,
                                              buzz_geometry_job_fn directions,buzz_geometry_job_fn normals)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_rigid_skin_enabled()){
        reference(ctx);return;
    }
    buzz::GeometryRigidSkin plan;
    if(!directions || !normals || !buzz::read_geometry_rigid_skin(*ctx,plan)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_RIGID_SKIN,0,0);reference(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_rigid_skin_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;g_spu_trampoline_fn!=directions && g_spu_trampoline_fn!=normals;++steps){
            const auto next=g_spu_trampoline_fn;
            if(!next || steps>=8192)buzz_geometry_job_native_fail(ctx,"rigid skin reference escaped data boundary");
            g_spu_trampoline_fn=nullptr;next(comparison);
        }
        expected=g_spu_trampoline_fn;
        if((expected!=directions || comparison->pc!=0xb4c0) && (expected!=normals || comparison->pc!=0xbb58))
            buzz_geometry_job_native_fail(ctx,"rigid skin reference PC differs");
        g_spu_trampoline_fn=scheduled;
    }
    buzz::transform_geometry_rigid_skin(*ctx,plan,directions,normals);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/rigid-skin] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"rigid skin strict data/state/boundary differs");
        }
        buzz_geometry_job_rigid_skin_result(plan.count,1,plan.aliases);
    }
    buzz_geometry_job_rigid_skin_result(plan.count,0,plan.aliases);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_RIGID_SKIN,1,0);
}

extern "C" void buzz_geometry_job_weighted_skin(spu_context* ctx,buzz_geometry_job_fn fallback,buzz_geometry_job_fn reference,
                                                 buzz_geometry_job_fn directions,buzz_geometry_job_fn normals)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_weighted_skin_enabled()){
        fallback(ctx);return;
    }
    buzz::GeometryWeightedSkin plan;const char* reason=nullptr;
    if(!reference || !directions || !normals || !buzz::read_geometry_weighted_skin(*ctx,plan,&reason)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_WEIGHTED_SKIN,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/weighted-skin] first fallback: %s count=%u position=%X weights=%X destination=%X rows=%X/%X/%X\n",reason?reason:"missing callback",ctx->gpr[84]._u32[0],ctx->gpr[80]._u32[0],ctx->gpr[85]._u32[0],ctx->gpr[96]._u32[0],ctx->gpr[54]._u32[0],ctx->gpr[53]._u32[0],ctx->gpr[52]._u32[0]);
        fallback(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected=nullptr;
    if(check_job_stages() || buzz_geometry_job_weighted_skin_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);expected=g_spu_trampoline_fn;
        if((expected!=directions || comparison->pc!=0xb4c0) && (expected!=normals || comparison->pc!=0xbb58))
            buzz_geometry_job_native_fail(ctx,"weighted skin reference escaped data boundary");
        g_spu_trampoline_fn=scheduled;
    }
    buzz::transform_geometry_weighted_skin(*ctx,plan,directions,normals);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/weighted-skin] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"weighted skin strict data/state/boundary differs");
        }
        buzz_geometry_job_weighted_skin_result(plan.count,1,plan.palette_alias);
    }
    buzz_geometry_job_weighted_skin_result(plan.count,0,plan.palette_alias);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_WEIGHTED_SKIN,1,0);
}

extern "C" void buzz_geometry_job_skin_directions(spu_context* ctx,buzz_geometry_job_fn reference,buzz_geometry_job_fn tail,unsigned streams)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_skin_directions_enabled()){
        reference(ctx);return;
    }
    buzz::GeometrySkinDirections plan;const char* reason=nullptr;
    if(!tail || !buzz::read_geometry_skin_directions(*ctx,streams,plan,&reason)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SKIN_DIRECTIONS,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/skin-directions] first fallback: %s streams=%u count=%u\n",reason?reason:"missing tail",streams,ctx->gpr[84]._u32[0]);
        reference(ctx);return;
    }
    spu_context* comparison=nullptr;
    if(check_job_stages() || buzz_geometry_job_skin_directions_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;g_spu_trampoline_fn!=tail;++steps){
            if(!g_spu_trampoline_fn || steps>=16384)buzz_geometry_job_native_fail(ctx,"skin directions reference escaped data boundary");
            auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(comparison);
        }
        if(comparison->pc!=0xb868)buzz_geometry_job_native_fail(ctx,"skin directions reference tail PC differs");
        g_spu_trampoline_fn=scheduled;
    }
    buzz::transform_geometry_skin_directions(*ctx,plan,tail);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==tail;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/skin-directions] DIFF streams%u r%d byte%d LS=%X/%u state=%s\n",streams,diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"skin directions strict data/state/boundary differs");
        }
        buzz_geometry_job_skin_directions_result(streams,1,plan.aliases);
    }
    buzz_geometry_job_skin_directions_result(streams,0,plan.aliases);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_SKIN_DIRECTIONS,1,0);
}

extern "C" void buzz_geometry_job_prepare_commands(spu_context* ctx, buzz_geometry_job_fn reference)
{
    // In JOB=2 the retained reference must stay original, even when it calls
    // this same library entry. A foreign comparison clone is not this owner.
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_commands_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryCommandBuffer plan;
    const auto* admitted = buzz::geometry_command_admission ? buzz::geometry_command_admission->take_prepare(*ctx) : nullptr;
    const bool supported = admitted || buzz::read_geometry_command_buffer(*ctx, plan);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_COMMAND_BUFFER, supported, 0);
    if (!supported) { reference(ctx); return; }
    buzz::prepare_geometry_command_buffer(*ctx, admitted ? *admitted : plan);
    SPU_RET(ctx);
}

extern "C" void buzz_geometry_job_publish_commands(spu_context* ctx, buzz_geometry_job_fn reference)
{
    if (ctx != native_pipeline_context || ctx->image_id != 41 || !buzz_geometry_job_command_publish_enabled()) {
        reference(ctx); return;
    }
    buzz::GeometryCommandPublication plan;
    const char* reason = nullptr;
    const auto* forecast = buzz::geometry_command_admission ? buzz::geometry_command_admission->take_publish(*ctx) : nullptr;
    const bool supported = buzz::read_geometry_command_publication(*ctx, plan, &reason, forecast);
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

extern "C" void buzz_geometry_job_decode_custom_stream(spu_context* ctx,buzz_geometry_job_fn reference,buzz_geometry_job_fn descriptors)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_custom_stream_enabled() ||
       (ctx->gpr[6]._u32[0]&255)<6){buzz_geometry_job_decode_stream(ctx,reference);return;}
    buzz::GeometryCustomStream plan;
    if(!descriptors || !buzz::read_geometry_custom_stream(*ctx,plan)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_CUSTOM_STREAM,0,0);
        buzz_geometry_job_decode_stream(ctx,reference);return;
    }
    spu_context* comparison=nullptr;
    if(check_job_stages() || buzz_geometry_job_custom_stream_enabled()==2){
        comparison=buzz::clone_function_state(ctx);auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        for(unsigned steps=0;g_spu_trampoline_fn!=descriptors;++steps){
            if(!g_spu_trampoline_fn || steps>=128)buzz_geometry_job_native_fail(ctx,"custom stream reference escaped descriptor handoff");
            auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(comparison);
        }
        if(comparison->pc!=0x7df8)buzz_geometry_job_native_fail(ctx,"custom stream reference descriptor PC differs");
        g_spu_trampoline_fn=scheduled;
    }
    buzz::dispatch_geometry_custom_stream(*ctx,plan,descriptors);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==descriptors;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/custom-stream] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"custom stream strict state/handoff differs");
        }
        buzz_geometry_job_custom_stream_checked_result();
    }
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_CUSTOM_STREAM,1,0);
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
    if (check_job_stages()) {
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
    if (check_job_stages()) {
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
    if (check_job_stages()) {
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
    if (check_job_stages() || buzz_geometry_job_float32_stream_enabled() == 2) {
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
    if (check_job_stages() || buzz_geometry_job_fixed_stream_enabled() == 2) {
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
    if (check_job_stages() || buzz_geometry_job_snorm_stream_enabled() == 2) {
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
    if (check_job_stages() || buzz_geometry_job_i16_stream_enabled() == 2) {
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
    if (check_job_stages() || buzz_geometry_job_cmp_stream_enabled() == 2) {
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
    if (check_job_stages() || buzz_geometry_job_u8_integer_stream_enabled() == 2) {
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
    if(check_job_stages() || buzz_geometry_job_blend_scatter_enabled()==2){
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

extern "C" void buzz_geometry_job_run_lists(spu_context* ctx,buzz_geometry_job_fn reference,buzz_geometry_job_fn tail)
{
    if(ctx!=native_pipeline_context || ctx->image_id!=41 || !buzz_geometry_job_run_lists_enabled()){
        reference(ctx);return;
    }
    buzz::GeometryRunList plan;const char* reason=tail?nullptr:"null tail";
    if(!tail || !buzz::read_geometry_run_list(*ctx,plan,&reason)){
        buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_RUN_LISTS,0,0);
        static std::atomic_flag reported=ATOMIC_FLAG_INIT;
        if(!reported.test_and_set())std::fprintf(stderr,"[geometry-job/run-lists] first fallback: %s\n",reason);
        reference(ctx);return;
    }
    spu_context* comparison=nullptr;buzz_geometry_job_fn expected_next=nullptr;
    if(check_job_stages() || buzz_geometry_job_run_lists_enabled()==2){
        comparison=buzz::clone_function_state(ctx);const auto scheduled=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;reference(comparison);
        if(g_spu_trampoline_fn==tail){
            if(comparison->pc!=0x7298)buzz_geometry_job_native_fail(ctx,"run-list reference tail escaped");
            g_spu_trampoline_fn=nullptr;tail(comparison);
        }
        expected_next=comparison->host_depth?nullptr:spu_indirect_branch;
        if(comparison->pc!=comparison->gpr[0]._u32[0] || g_spu_trampoline_fn!=expected_next)
            buzz_geometry_job_native_fail(ctx,"run-list reference root return differs");
        g_spu_trampoline_fn=scheduled;
    }
    buzz::expand_geometry_run_list(*ctx,plan);
    if(comparison){
        const auto diff=buzz::compare_function_state(*ctx,*comparison);
        const bool exact=ctx->host_depth==comparison->host_depth && ctx->image_id==comparison->image_id &&
            ctx->policy_mode==comparison->policy_mode && ctx->resident_ovl==comparison->resident_ovl && g_spu_trampoline_fn==expected_next;
        buzz::release_function_state(comparison);
        if(!diff.exact() || !exact){
            std::fprintf(stderr,"[geometry-job/run-lists] DIFF r%d byte%d LS=%X/%u state=%s\n",diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
            buzz_geometry_job_native_fail(ctx,"run-list strict root state/return differs");
        }
        buzz_geometry_job_run_lists_checked_result(plan.bytes!=0);
    }
    buzz_geometry_job_run_lists_native_result(plan.bytes!=0);
    buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_RUN_LISTS,1,0);
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
    if(check_job_stages()){
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
    prepared_job_owner=nullptr;
    // Owner lifetime is granted by the controller, not by an arbitrary prior
    // eligibility query. Before mutation, prove both input control frames.
    const bool host_pipeline=buzz_geometry_job_host_pipeline_enabled()!=0;
    bool supported=buzz::read_geometry_job_setup(*ctx,prepared_job,nullptr,!host_pipeline);
    if(supported && host_pipeline)
        supported=buzz::prepare_geometry_host_job(*ctx,prepared_job.entry,worker_host_job());
    else if(supported && buzz_geometry_host_output_enabled())
        supported=buzz::read_geometry_host_output_recipe(*ctx,prepared_job.entry,prepared_host_output);
    if(supported && buzz_geometry_job_owns_admission(ctx))prepared_job_owner=ctx;
    return supported;
}

extern "C" void buzz_geometry_job_native_start(spu_context* ctx, const buzz_geometry_job_functions* functions)
{
    buzz::GeometryJobSetup setup;
    buzz::GeometryContextState state;
    buzz::GeometryHostOutputRecipe host_output;
    const char* reason = nullptr;
    const bool reuse=prepared_job_owner==ctx && buzz_geometry_job_owns_admission(ctx);
    prepared_job_owner=nullptr;
    const bool host_pipeline=buzz_geometry_job_host_pipeline_enabled()!=0;
    const bool host=!host_pipeline && buzz_geometry_host_output_enabled()!=0;
    if(reuse){setup=prepared_job;if(host)host_output=prepared_host_output;}
    else {
        if(!buzz::read_geometry_job_setup(*ctx,setup,&reason,!host_pipeline))buzz_geometry_job_native_fail(ctx,reason);
        if(host_pipeline && !buzz::prepare_geometry_host_job(*ctx,setup.entry,worker_host_job(),&reason))
            buzz_geometry_job_native_fail(ctx,reason);
        if(host && !buzz::read_geometry_host_output_recipe(*ctx,setup.entry,host_output,&reason))
            buzz_geometry_job_native_fail(ctx,reason);
    }
    buzz::commit_geometry_job_setup(*ctx,setup);
    buzz::materialize_geometry_job_initial_call(*ctx, setup);
    if (!buzz::initialize_geometry_context(*ctx, setup.initialization, state, &reason))
        buzz_geometry_job_native_fail(ctx, reason);
    NativePipelineScope scope(ctx);
    buzz::GeometryPipeline pipeline(*ctx, *functions, setup, host?&host_output:nullptr,host_pipeline?&worker_host_job():nullptr);
    pipeline.run();
    if(host_pipeline)buzz_geometry_job_host_pipeline_result();
    SPU_RET(ctx);
}
