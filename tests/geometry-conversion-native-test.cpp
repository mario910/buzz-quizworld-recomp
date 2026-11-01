#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_conversion_control.h"
#include "../port/native_geometry_snorm_output.h"
#include "../port/native_geometry_float_output.h"
#include "../port/native_geometry_half_output.h"
#include "../port/native_geometry_unorm_output.h"
#include "../port/native_geometry_i16_output.h"
#include "../port/native_geometry_cmp_output.h"
#include "../port/native_geometry_u8_integer_output.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
namespace conversion_control_oracle {static void dispatch(spu_context*);}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* c){conversion_control_oracle::dispatch(c);}
void spu_stop(spu_context*){abort();}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void buzz_geometry_job_native_fail(spu_context*,const char* why){fprintf(stderr,"native conversion: %s\n",why);abort();}
void spu_drain_call(spu_context* c,uint32_t link){
    for(unsigned steps=0;!(c->pc==link && (!g_spu_trampoline_fn || g_spu_trampoline_fn==spu_indirect_branch));++steps){
        assert(steps<1048576 && g_spu_trampoline_fn);const auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);
    }g_spu_trampoline_fn=nullptr;
}
}
#include "../analysis/native-geometry/phase3/test-conversion-control-reference.inc"
#include "../analysis/native-geometry/phase3/conversion-control-original.inc"
static unsigned strict_cases=0,stride_cases=0,formatter_calls=0,storage_calls[7]{};
static GeometryFormatter forecast;
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void equal(const spu_context& a,const spu_context& b,unsigned trial,const char* label){
    const auto d=compare_function_state(a,b);
    if(!d.exact()){fprintf(stderr,"conversion native %s trial%u r%d byte%d LS%X/%u state%s PC%X/%X\n",label,trial,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal",a.pc,b.pc);abort();}
    assert(a.host_depth==b.host_depth && a.image_id==b.image_id && a.policy_mode==b.policy_mode && a.resident_ovl==b.resident_ovl);
}
static void data(spu_context* c,unsigned kind){
    const auto finish=conversion_control_oracle::buzz_job_6eba_spu_func_00004610,skip=conversion_control_oracle::buzz_job_6eba_spu_func_00004618;const char* why=nullptr;
    switch(kind){
    case 0:{GeometrySnormOutput p;assert(read_geometry_snorm_output(*c,p,&why));emit_geometry_snorm_output(*c,p,finish,skip);break;}
    case 1:{GeometryFloatOutput p;assert(read_geometry_float_output(*c,p,&why));emit_geometry_float_output(*c,p,finish,skip);break;}
    case 2:{GeometryHalfOutput p;assert(read_geometry_half_output(*c,p,&why));emit_geometry_half_output(*c,p,finish,skip);break;}
    case 3:{GeometryUnormOutput p;assert(read_geometry_unorm_output(*c,p,&why));emit_geometry_unorm_output(*c,p,finish,skip);break;}
    case 4:{GeometryI16Output p;assert(read_geometry_i16_output(*c,p,&why));emit_geometry_i16_output(*c,p,finish,skip);break;}
    case 5:{GeometryCmpOutput p;assert(read_geometry_cmp_output(*c,p,&why));emit_geometry_cmp_output(*c,p,finish,skip);break;}
    case 6:{GeometryU8IntegerOutput p;assert(read_geometry_u8_integer_output(*c,p,&why));emit_geometry_u8_integer_output(*c,p,finish,skip);break;}
    }++storage_calls[kind];
}
static void d0(spu_context* c){data(c,0);}static void d1(spu_context* c){data(c,1);}static void d2(spu_context* c){data(c,2);}
static void d3(spu_context* c){data(c,3);}static void d4(spu_context* c){data(c,4);}static void d5(spu_context* c){data(c,5);}static void d6(spu_context* c){data(c,6);}
static void formatter(spu_context* c){
    auto before=std::make_unique<spu_context>();memcpy(before.get(),c,sizeof(*c));GeometryFormatter plan;const char* why=nullptr;
    assert(read_geometry_formatter(*c,plan,&why) && !memcmp(before.get(),c,sizeof(*c)));
    assert(plan.fields==forecast.fields && plan.known==forecast.known && plan.skipped==forecast.skipped && plan.mask==forecast.mask);
    const GeometryFormatterCallees f{{d0,d1,d2,d3,d4,d5,d6},conversion_control_oracle::buzz_job_6eba_spu_func_00004610,conversion_control_oracle::buzz_job_6eba_spu_func_00004618};
    execute_geometry_formatter(*c,plan,f);++formatter_calls;
}
static void compare(spu_context& native,spu_context& reference,spu_context& production,unsigned trial){
    if(trial>=3){
        unsigned random=trial+123;auto word=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
        for(unsigned reg=0;reg<128;++reg)for(unsigned lane=0;lane<4;++lane)
            if(lane || (reg!=0 && reg!=1 && reg!=3))native.gpr[reg]._u32[lane]=word();
        native.policy_mode=trial%3;native.resident_ovl=trial%5;native.event_status=word();native.event_mask=word();native.mfc_tag_status=word();native.resv_valid=trial&1;
    }
    auto before=std::make_unique<spu_context>();memcpy(before.get(),&native,sizeof native);GeometryConversionAdmission plan;const char* why=nullptr;
    if(!read_geometry_conversion_native(native,plan,&why)){fprintf(stderr,"conversion forecast reject trial%u %s\n",trial,why);abort();}
    assert(!memcmp(before.get(),&native,sizeof native));forecast=plan.formatter;
    copy(reference,native);copy(production,native);const unsigned stack=native.gpr[1]._u32[0];g_spu_trampoline_fn=nullptr;
    conversion_control_oracle::buzz_job_6eba_spu_func_000054E8(&reference);
    for(unsigned steps=0;!(reference.gpr[1]._u32[0]==stack && reference.pc==reference.gpr[0]._u32[0] && g_spu_trampoline_fn==(reference.host_depth?nullptr:spu_indirect_branch));++steps){
        assert(steps<1048576 && g_spu_trampoline_fn);const auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);
    }
    const auto expected=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;buzz_conversion_original_run(&production);
    equal(production,reference,trial,"closed C reference");assert(g_spu_trampoline_fn==expected);
    g_spu_trampoline_fn=spu_indirect_branch;const GeometryConversionCallees functions{formatter,geometry_conversion_stride};
    assert(execute_geometry_conversion_control(native,plan.control,functions)==plan.path);
    equal(native,reference,trial,"full native");assert(g_spu_trampoline_fn==expected);++strict_cases;
}
int main(){
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto base=std::make_unique<spu_context>(),native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>(),production=std::make_unique<spu_context>();
    memset(base.get(),0,sizeof(*base));base->ls=base->ls_store;base->pc=0x54e8;base->image_id=41;
    auto* file=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin","rb");assert(file);
    assert(fread(base->gpr,sizeof base->gpr,1,file)==1 && fread(base->ls,SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF);fclose(file);
    const unsigned manager=base->gpr[3]._u32[0]&~15u,desc=geometry_job_be32(base->ls+manager+0x74);
    for(unsigned depth=0;depth<3;++depth){copy(*native,*base);native->host_depth=depth;compare(*native,*reference,*production,strict_cases);}
    for(unsigned path=0;path<2;++path)for(unsigned format=0;format<256;++format){
        copy(*native,*base);native->ls[manager]=(native->ls[manager]&~16u)|(path==0?16:0);native->ls[manager+4]=format;
        native->gpr[1]._u32[0]+=format&15;native->host_depth=format%3;
        if(path)native->ls[manager+8]=native->ls[manager+9]=0;
        compare(*native,*reference,*production,strict_cases);
    }
    auto seven=[&](unsigned stride,unsigned vertices,unsigned bias,unsigned depth){
        copy(*native,*base);native->gpr[1]._u32[0]+=strict_cases&15;native->host_depth=depth;
        native->ls[manager+8]=vertices>>8;native->ls[manager+9]=vertices;native->ls[desc]=7;native->ls[desc+1]=stride;
        geometry_job_put32(native->ls+manager+0x74,desc+bias);const unsigned widths[]={6,12,6,3,6,4,3};
        for(unsigned i=0;i<7;++i){auto* p=native->ls+desc+bias+8+i*8;p[0]=i*2;p[1]=i+1;p[2]=3;p[3]=1;p[4]=widths[i];p[5]=p[6]=p[7]=0;}
        compare(*native,*reference,*production,strict_cases);
    };
    for(unsigned stride=0;stride<64;++stride)for(unsigned count:{1u,4u,17u})for(unsigned bias:{0u,8u})for(unsigned depth=0;depth<3;++depth)seven(stride,count,bias,depth);
    for(unsigned stride:{16u,23u,48u})for(unsigned bias:{0u,8u})for(unsigned depth=0;depth<3;++depth)seven(stride,408,bias,depth);
    for(unsigned depth=0;depth<3;++depth)for(unsigned bias=0;bias<16;++bias){copy(*native,*base);native->host_depth=depth;native->gpr[1]._u32[0]+=bias;native->ls[desc]=0;compare(*native,*reference,*production,strict_cases);}
    for(unsigned link:{0u,0x54e8u,0x5500u,0x5538u,0x5558u,0x5614u,0x5648u,0x56acu})for(unsigned depth=0;depth<3;++depth){
        copy(*native,*base);native->host_depth=depth;native->gpr[0]._u32[0]=link;compare(*native,*reference,*production,strict_cases);
    }
    // Only the saved manager pointer is protected. Data may still replace
    // late outer-frame vectors and the formatter's saved result vector.
    for(unsigned alias=0;alias<2;++alias)for(unsigned bias=0;bias<16;++bias)for(unsigned depth=0;depth<3;++depth){
        copy(*native,*base);native->gpr[1]._u32[0]+=bias;native->host_depth=depth;
        native->ls[manager+8]=0;native->ls[manager+9]=1;
        geometry_job_put32(native->ls+manager+0x58,(native->gpr[1]._u32[0]&~15u)-(alias?144:64));
        if(alias)native->ls[desc+1]=0;compare(*native,*reference,*production,strict_cases);
    }
    // The complete getter preserves even speculative descriptor and table loads,
    // upper-lane masks, null pointers and wrapped LS inputs.
    unsigned state=1;auto random=[&](){state^=state<<13;state^=state>>17;state^=state<<5;return state;};
    for(unsigned format=0;format<256;++format)for(unsigned bias=0;bias<16;++bias){
        copy(*native,*base);for(auto& r:native->gpr)for(auto& w:r._u32)w=random();native->gpr[3]._u32[0]=manager+bias;native->gpr[4]._u32[0]=format;
        native->host_depth=bias%3;native->gpr[0]._u32[0]=0x3f000;
        const unsigned pointer=bias==0?0:bias==15?SPU_LS_SIZE-1:desc+bias;
        geometry_job_put32(native->ls+manager+0x74,pointer);geometry_job_put32(native->ls+manager+0x88,0);
        copy(*reference,*native);g_spu_trampoline_fn=nullptr;conversion_control_oracle::buzz_job_6eba_spu_func_000040C8(reference.get());
        for(unsigned steps=0;!(reference->pc==reference->gpr[0]._u32[0] && g_spu_trampoline_fn==(reference->host_depth?nullptr:spu_indirect_branch));++steps){assert(steps<32 && g_spu_trampoline_fn);const auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(reference.get());}
        const auto expected=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;geometry_conversion_stride(native.get());equal(*native,*reference,stride_cases,"stride");assert(g_spu_trampoline_fn==expected);++stride_cases;
    }
    for(unsigned failure=0;failure<21;++failure){copy(*native,*base);
        switch(failure){case 0:native->ls=reference->ls_store;break;case 1:native->int_enable=1;break;case 2:native->irq_saved=1;break;case 3:native->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;
        case 4:native->gpr[3]._u32[0]=0;break;case 5:native->gpr[1]._u32[0]=0xe680;break;case 6:native->gpr[1]._u32[0]=manager+64;break;
        case 7:geometry_job_put32(native->ls+manager+0x74,native->gpr[1]._u32[0]-32);break;
        case 8:geometry_job_put32(native->ls+manager+0x74,native->gpr[1]._u32[0]-224);break;
        case 9:geometry_job_put32(native->ls+manager+0x74,SPU_LS_SIZE);break;
        case 10:geometry_job_put32(native->ls+manager+0x88,0xbeef);break;case 11:geometry_job_put32(native->ls+manager+0x8c,0xbeef);break;
        case 12:native->ls[manager+4]=0;break;case 13:geometry_job_put32(native->ls+manager+0x74,0);break;
        case 14:geometry_job_put32(native->ls+0x4460,0x1234);break;
        case 15:geometry_job_put32(native->ls+manager+0x58,manager);break;
        case 16:geometry_job_put32(native->ls+manager+0x58,desc);break;
        case 17:geometry_job_put32(native->ls+manager+0x58,0);break;case 18:native->gpr[1]._u32[0]=SPU_LS_SIZE-16;break;
        case 19:geometry_job_put32(native->ls+manager+0x74,SPU_LS_SIZE-16);native->ls[SPU_LS_SIZE-16]=255;break;
        case 20:native->ls[manager+8]=0;native->ls[manager+9]=1;geometry_job_put32(native->ls+manager+0x58,(native->gpr[1]._u32[0]&~15u)-128);break;}
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryConversionAdmission plan;plan.formatter.fields=123;const auto before=plan;const char* why=nullptr;
        if(read_geometry_conversion_native(*native,plan,&why)){fprintf(stderr,"conversion unexpectedly admitted reject case%u\n",failure);abort();}
        assert(why && !memcmp(&plan,&before,sizeof plan) && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    for(auto n:storage_calls)assert(n);assert(strict_cases==1853 && stride_cases==4096 && formatter_calls==1341);
    printf("PASS complete native conversion:%u strict full-state/LS/FN cases;73-block closed C reference cross-check;read-only forecast equals actual formatter admission;%u formatter calls/all7 native encoders;%u complete stride cases;21 unchanged rejects;static/empty/zero-field,biased descriptor/stack,alias and return-PC collision cases\n",strict_cases,formatter_calls,stride_cases);
}
