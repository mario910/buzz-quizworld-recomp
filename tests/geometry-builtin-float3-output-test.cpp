#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_builtin_float3_output.h"
#include "../port/native_geometry_context.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
namespace builtin_float3_conversion_oracle {static void dispatch(spu_context*);}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* c){builtin_float3_conversion_oracle::dispatch(c);}
void spu_stop(spu_context*){abort();}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_drain_call(spu_context* c,uint32_t link){
    for(unsigned step=0;!(c->pc==link && (!g_spu_trampoline_fn || g_spu_trampoline_fn==spu_indirect_branch));++step){assert(step<1048576 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);}g_spu_trampoline_fn=nullptr;
}
}
#include "../analysis/native-geometry/phase3/test-builtin-float3-output-reference.inc"
#include "../analysis/native-geometry/phase3/test-builtin-float3-conversion-reference.inc"
static unsigned strict_cases=0,models=0,full_cases=0,child_cases=0,seed_word=1;
static unsigned random_word(){seed_word^=seed_word<<13;seed_word^=seed_word>>17;seed_word^=seed_word<<5;return seed_word;}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void equal(const spu_context& a,const spu_context& b,const char* label,unsigned trial){
    const auto d=compare_function_state(a,b);if(!d.exact()){fprintf(stderr,"builtin float3 %s trial%u r%d byte%d LS%X/%u state%s PC%X/%X\n",label,trial,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal",a.pc,b.pc);abort();}
    assert(a.host_depth==b.host_depth && a.image_id==b.image_id && a.policy_mode==b.policy_mode && a.resident_ovl==b.resident_ovl);
}
// Independent raw-byte model of XYZ packing and the prefetch-before-store
// pipeline. It does not use SPU shuffles, native packing or reference code.
static void byte_model(const spu_context& c,unsigned groups,unsigned char* result){
    memcpy(result,c.ls,SPU_LS_SIZE);const unsigned manager=(c.gpr[81]._u32[0]+0x50)&0x3fff0;
    unsigned source=geometry_job_be32(result+manager+12),output=c.gpr[3]._u32[0];unsigned char rows[64],next[64],packed[48];
    auto read=[&](unsigned address,unsigned char* out){for(unsigned i=0;i<4;++i)memcpy(out+i*16,result+((address+i*16)&0x3fff0),16);};
    read(source,rows);source+=64;
    for(unsigned group=0;group<groups;++group){
        for(unsigned row=0;row<4;++row)memcpy(packed+row*12,rows+row*16,12);
        read(source,next);source+=64;
        for(unsigned offset:{32u,0u,16u})memcpy(result+((output+offset)&0x3fff0),packed+offset,16);
        output+=48;memcpy(rows,next,64);
    }
}
static void compare(spu_context& native,spu_context& reference,unsigned trial,bool independent=true){
    auto before=std::make_unique<spu_context>();memcpy(before.get(),&native,sizeof native);GeometryBuiltinFloat3Output plan;const char* why=nullptr;
    if(!read_geometry_builtin_float3_output(native,plan,&why)){fprintf(stderr,"builtin float3 admission trial%u: %s\n",trial,why);abort();}
    assert(!memcmp(before.get(),&native,sizeof native));auto bytes=std::make_unique<unsigned char[]>(SPU_LS_SIZE);
    if(independent)byte_model(native,plan.groups,bytes.get());copy(reference,native);g_spu_trampoline_fn=nullptr;
    builtin_float3_output_oracle::buzz_job_6eba_spu_func_000056C8(&reference);
    const auto finish=builtin_float3_output_oracle::buzz_job_6eba_spu_func_00005648;
    for(unsigned step=0;!(reference.pc==0x5648 && g_spu_trampoline_fn==finish);++step){assert(step<4 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);}
    g_spu_trampoline_fn=spu_indirect_branch;emit_geometry_builtin_float3_output(native,plan,finish);equal(native,reference,"data",trial);assert(g_spu_trampoline_fn==finish);
    if(independent){if(memcmp(native.ls,bytes.get(),SPU_LS_SIZE)){fprintf(stderr,"builtin float3 independent model trial%u\n",trial);abort();}++models;}++strict_cases;
}
static void seed(spu_context& c,const spu_context& base,unsigned trial,unsigned vertices,unsigned mode){
    copy(c,base);seed_word=trial+1;
    for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word();
    unsigned source=0x18000+((trial>>4)&15),output=0x24000+(trial&15),manager=0x3d000+(trial&15);
    switch(mode){case 1:output=source;break;case 2:output=source+16;break;case 3:output=source+48;break;case 4:output=source-16;break;
        case 5:output=source+64;break;case 6:output=source+80;break;case 7:output=source+128;break;case 8:source=0x3fff0+(trial&15);break;case 9:source=manager;break;}
    for(unsigned row=0;row<96;++row)for(unsigned lane=0;lane<4;++lane)geometry_job_put32(c.ls+((source+row*16)&0x3fff0)+lane*4,random_word());
    c.gpr[81]._u32[0]=manager;c.gpr[3]._u32[0]=output;c.gpr[10]._u32[0]=(random_word()&0xffff0000)|vertices;
    geometry_job_put32(c.ls+((manager+0x50)&0x3fff0)+12,source);c.pc=0x56c8;c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;
    c.event_mask=random_word();c.event_status=random_word();c.mfc_tag_status=random_word();c.resv_valid=trial&1;c.int_enable=0;c.irq_saved=0;c.irq_frame=nullptr;
}
static void full_run(spu_context& c,bool native){
    builtin_float3_conversion_oracle::native_data=native?+[](spu_context* c){
        auto* original=clone_function_state(c);builtin_float3_conversion_oracle::native_data=nullptr;g_spu_trampoline_fn=nullptr;
        builtin_float3_conversion_oracle::buzz_job_6eba_spu_func_000056C8(original);
        const auto finish=builtin_float3_conversion_oracle::buzz_job_6eba_spu_func_00005648;
        for(unsigned step=0;!(original->pc==0x5648 && g_spu_trampoline_fn==finish);++step){assert(step<4 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(original);}
        GeometryBuiltinFloat3Output plan;assert(read_geometry_builtin_float3_output(*c,plan));emit_geometry_builtin_float3_output(*c,plan,finish);equal(*c,*original,"full child",child_cases);assert(g_spu_trampoline_fn==finish);release_function_state(original);++child_cases;
    }:nullptr;
    const unsigned stack=c.gpr[1]._u32[0];g_spu_trampoline_fn=nullptr;builtin_float3_conversion_oracle::buzz_job_6eba_spu_func_000054E8(&c);
    for(unsigned step=0;!(c.gpr[1]._u32[0]==stack && c.pc==c.gpr[0]._u32[0] && g_spu_trampoline_fn==(c.host_depth?nullptr:spu_indirect_branch));++step){assert(step<1048576 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&c);}
}
int main(){
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto base=std::make_unique<spu_context>(),entry=std::make_unique<spu_context>(),native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    memset(base.get(),0,sizeof(*base));base->ls=base->ls_store;base->pc=0x54e8;base->image_id=41;
    auto* file=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin","rb");assert(file);assert(fread(base->gpr,sizeof base->gpr,1,file)==1 && fread(base->ls,SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF);fclose(file);
    const unsigned manager=base->gpr[3]._u32[0]&~15u;copy(*entry,*base);entry->ls[manager+4]=0;
    g_spu_trampoline_fn=nullptr;builtin_float3_conversion_oracle::buzz_job_6eba_spu_func_000054E8(entry.get());
    for(unsigned steps=0;!(entry->pc==0x56c8 && g_spu_trampoline_fn==spu_indirect_branch);++steps){assert(steps<32 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(entry.get());}
    // Explicit synthetic format mutation of a real controller input; not a
    // captured format-0 root. Save only the reproducible GPR/LS anchor.
    const char* calibration="analysis/native-geometry/phase3/builtin-float3-output-calibration.bin";
    file=fopen(calibration,"rb");if(file){auto saved=std::make_unique<unsigned char[]>(sizeof entry->gpr+SPU_LS_SIZE);assert(fread(saved.get(),sizeof entry->gpr+SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF);assert(!memcmp(saved.get(),entry->gpr,sizeof entry->gpr) && !memcmp(saved.get()+sizeof entry->gpr,entry->ls,SPU_LS_SIZE));fclose(file);}
    else{file=fopen(calibration,"wb");assert(file);assert(fwrite(entry->gpr,sizeof entry->gpr,1,file)==1 && fwrite(entry->ls,SPU_LS_SIZE,1,file)==1);fclose(file);}
    for(unsigned depth=0;depth<3;++depth){copy(*native,*entry);native->host_depth=depth;compare(*native,*reference,strict_cases);}
    for(unsigned trial=0;trial<4096;++trial){seed(*native,*base,trial,1+trial%127,trial%10);compare(*native,*reference,strict_cases);}
    for(unsigned trial=0;trial<1024;++trial){seed(*native,*base,5000+trial,1+trial%63,trial%8);
        for(unsigned at:{0xd970u,0xd9f0u,0xda50u})for(unsigned lane=0;lane<4;++lane)geometry_job_put32(native->ls+at+lane*4,random_word());compare(*native,*reference,strict_cases,false);}
    const unsigned special[]={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,0x7f800000,0xff800000,0x7fc00000,0x7f800001,0xff800001,0xffffffff,0x7f7fffff,0xff7fffff};
    for(unsigned trial=0;trial<1024;++trial){seed(*native,*base,7000+trial,1+trial%33,trial%8);const unsigned source=geometry_job_be32(native->ls+0x3d05c);
        for(unsigned row=0;row<36;++row)for(unsigned lane=0;lane<4;++lane)geometry_job_put32(native->ls+((source+row*16)&0x3fff0)+lane*4,special[(row*4+lane+trial)%16]);compare(*native,*reference,strict_cases);}
    for(unsigned vertices:{1u,2u,3u,4u,5u,255u,256u,257u,1023u,1024u,1025u,16925u,16926u,16927u,16928u}){
        seed(*native,*base,9000+vertices,vertices,0);native->gpr[3]._u32[0]=0xe680;compare(*native,*reference,strict_cases);}
    for(unsigned bias=0;bias<16;++bias){seed(*native,*base,30000+bias,17,0);native->gpr[81]._u32[0]=0x3ffb0+bias;geometry_job_put32(native->ls+12,0x18000+bias);compare(*native,*reference,strict_cases);}
    // Full original controller/getter remain on both arms. Only format-0 data
    // is native. Compare child and full exit, including late outer-frame loads.
    for(unsigned count=1;count<=65;++count)for(unsigned bias:{0u,4u,8u,12u})for(unsigned depth=0;depth<3;++depth){
        copy(*native,*base);native->ls[manager+4]=0;native->ls[manager+8]=count>>8;native->ls[manager+9]=count;native->host_depth=depth;native->gpr[1]._u32[0]+=bias;
        geometry_job_put32(native->ls+manager+0x58,0x24000+bias);copy(*reference,*native);full_run(*reference,false);const auto expected=g_spu_trampoline_fn;full_run(*native,true);equal(*native,*reference,"full conversion",full_cases);assert(g_spu_trampoline_fn==expected);++full_cases;
    }
    for(unsigned depth=0;depth<3;++depth)for(unsigned bias=0;bias<16;++bias){copy(*native,*base);native->ls[manager+4]=0;native->ls[manager+8]=0;native->ls[manager+9]=1;native->host_depth=depth;native->gpr[1]._u32[0]+=bias;
        geometry_job_put32(native->ls+manager+0x58,(native->gpr[1]._u32[0]&~15u)-64);copy(*reference,*native);full_run(*reference,false);const auto expected=g_spu_trampoline_fn;full_run(*native,true);equal(*native,*reference,"late frame alias",full_cases);assert(g_spu_trampoline_fn==expected);++full_cases;}
    for(unsigned failure=0;failure<8;++failure){seed(*native,*base,10000,17,0);switch(failure){case 0:native->ls=reference->ls_store;break;case 1:native->int_enable=1;break;case 2:native->irq_saved=1;break;case 3:native->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;case 4:native->gpr[10]._u32[0]=0;break;case 5:native->gpr[10]._u32[0]=65536;break;case 6:native->gpr[3]._u32[0]=0;break;case 7:native->gpr[10]._u32[0]=16929;native->gpr[3]._u32[0]=0xe680;break;}
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryBuiltinFloat3Output plan{123,456};const char* why=nullptr;
        assert(!read_geometry_builtin_float3_output(*native,plan,&why) && why && plan.vertices==123 && plan.groups==456 && !memcmp(native.get(),reference.get(),sizeof(*native)));}
    assert(strict_cases==6178 && models==5154 && full_cases==828 && child_cases==828);
    printf("PASS builtin float3:%u strict full-state/LS/FN cases;%u independent ordered byte models;1024 arbitrary-mask cases;1024 special-FP-bit cases;%u full original-controller cases/%u native strict child roots;48 late outer-frame aliases;8 unchanged rejects;synthetic cold format0 only\n",strict_cases,models,full_cases,child_cases);
}
