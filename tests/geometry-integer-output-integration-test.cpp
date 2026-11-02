#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_i16_output.h"
#include "../port/native_geometry_u8_integer_output.h"
#include "../port/native_geometry_context.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
namespace integer_output_oracle {static void dispatch(spu_context*);}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* c){integer_output_oracle::dispatch(c);}
void spu_stop(spu_context*){abort();}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_drain_call(spu_context* c,uint32_t link){
    for(unsigned steps=0;!(c->pc==link && (!g_spu_trampoline_fn || g_spu_trampoline_fn==spu_indirect_branch));++steps){
        assert(steps<1000000 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);
    }g_spu_trampoline_fn=nullptr;
}
}
#include "../analysis/native-geometry/phase3/test-integer-output-integration-reference.inc"
static unsigned counts[2]{},mixed[2]{},strict_cases=0;
static void equal(const spu_context& a,const spu_context& b){
    const auto d=compare_function_state(a,b);
    if(!d.exact()){fprintf(stderr,"integer integration DIFF r%d byte%d LS%X/%u state%s PC%X/%X\n",d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal",a.pc,b.pc);abort();}
    assert(a.image_id==b.image_id && a.host_depth==b.host_depth && a.policy_mode==b.policy_mode && a.resident_ovl==b.resident_ovl);
}
static void cold(spu_context* c,unsigned pc){
    const unsigned type=pc==0x4dd0?0:1;assert(pc==0x4dd0 || pc==0x52b4);
    auto* reference=clone_function_state(c);const auto saved=g_spu_trampoline_fn;integer_output_oracle::cold_handler=nullptr;g_spu_trampoline_fn=nullptr;
    (type?integer_output_oracle::buzz_job_6eba_spu_func_000052B4:integer_output_oracle::buzz_job_6eba_spu_func_00004DD0)(reference);
    const auto finish=integer_output_oracle::buzz_job_6eba_spu_func_00004610,skip=integer_output_oracle::buzz_job_6eba_spu_func_00004618;
    for(unsigned steps=0;!((reference->pc==0x4610 && g_spu_trampoline_fn==finish)||(reference->pc==0x4618 && g_spu_trampoline_fn==skip));++steps){
        assert(steps<2048 && g_spu_trampoline_fn && g_spu_trampoline_fn!=spu_indirect_branch);const auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(reference);
    }
    const auto expected=g_spu_trampoline_fn;g_spu_trampoline_fn=saved;integer_output_oracle::cold_handler=cold;
    const char* why=nullptr;
    if(type){GeometryU8IntegerOutput plan;assert(read_geometry_u8_integer_output(*c,plan,&why));emit_geometry_u8_integer_output(*c,plan,finish,skip);mixed[type]+=plan.cross_columns && plan.cross_columns!=plan.columns;}
    else{GeometryI16Output plan;assert(read_geometry_i16_output(*c,plan,&why));emit_geometry_i16_output(*c,plan,finish,skip);mixed[type]+=plan.cross_columns && plan.cross_columns!=plan.columns;}
    equal(*c,*reference);assert(g_spu_trampoline_fn==expected);release_function_state(reference);++counts[type];
}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void run(spu_context& c,bool native){
    integer_output_oracle::cold_handler=native?cold:nullptr;const auto sp=c.gpr[1]._u32[0];g_spu_trampoline_fn=nullptr;
    integer_output_oracle::buzz_job_6eba_spu_func_000054E8(&c);
    for(unsigned steps=0;!(c.gpr[1]._u32[0]==sp && c.pc==c.gpr[0]._u32[0] && g_spu_trampoline_fn==(c.host_depth?nullptr:spu_indirect_branch));++steps){
        assert(steps<1000000 && g_spu_trampoline_fn);const auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&c);
    }
}
int main(){
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto base=std::make_unique<spu_context>(),native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();memset(base.get(),0,sizeof(*base));base->ls=base->ls_store;base->image_id=41;base->pc=0x54e8;
    auto* f=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin","rb");assert(f);assert(fread(base->gpr,sizeof base->gpr,1,f)==1 && fread(base->ls,SPU_LS_SIZE,1,f)==1 && fgetc(f)==EOF);fclose(f);
    const auto manager=base->gpr[3]._u32[0]&~15u,desc=geometry_job_be32(base->ls+manager+0x74);assert(base->ls[desc]==2 && base->ls[desc+1]==16 && base->ls[desc+9]==2);
    for(unsigned type:{5u,7u})for(unsigned components=1;components<=4;++components)for(unsigned offset=0;offset<16;++offset)for(unsigned stride:{16u,23u,48u})for(unsigned depth=0;depth<3;++depth){
        copy(*native,*base);native->ls[desc+1]=stride;native->ls[desc+8]=offset;native->ls[desc+9]=type;native->ls[desc+10]=components;native->ls[desc+12]=components*(type==5?2:1);native->host_depth=depth;
        native->policy_mode=components%3;native->resident_ovl=offset%5;native->event_mask=offset*13;native->mfc_tag_status=stride*123;native->resv_valid=offset&1;copy(*reference,*native);
        const unsigned index=type==5?0:1,previous=counts[index];run(*reference,false);const auto expected=g_spu_trampoline_fn;run(*native,true);
        equal(*native,*reference);assert(g_spu_trampoline_fn==expected && counts[index]==previous+1);++strict_cases;
    }
    fprintf(stderr,"integration counts cases%u I16%u U8%u mixed%u/%u\n",strict_cases,counts[0],counts[1],mixed[0],mixed[1]);
    assert(strict_cases==1152 && counts[0]==576 && counts[1]==576 && mixed[0]==192 && mixed[1]==144);
    printf("PASS integer output enclosing conversion:1152 synthetic descriptor cases;576 I16 +576 U8 integer native strict roots;3 host depths;4 component counts with explicit byte widths;16 offsets;3 strides including input/output overlap;full root and whole-conversion GPR/LS/control/FN exact;real/live gates OPEN\n");
}
