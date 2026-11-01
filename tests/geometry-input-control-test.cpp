#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_input_control.h"
#include "../port/native_geometry_context.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
namespace input_control_oracle {static void dispatch(spu_context*);}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* c){input_control_oracle::dispatch(c);}
void spu_stop(spu_context*){abort();}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_drain_call(spu_context* c,uint32_t link){
    for(unsigned steps=0;!(c->pc==link && (!g_spu_trampoline_fn || g_spu_trampoline_fn==spu_indirect_branch));++steps){assert(steps<1048576 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);}g_spu_trampoline_fn=nullptr;
}
}
#include "../analysis/native-geometry/phase3/test-input-control-reference.inc"
static unsigned strict_cases=0,prefix_cases=0,frame_models=0,seed_word=1,paths[7]{};
static unsigned random_word(){seed_word^=seed_word<<13;seed_word^=seed_word>>17;seed_word^=seed_word<<5;return seed_word;}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void equal(const spu_context& a,const spu_context& b,const char* label,unsigned trial){
    const auto d=compare_function_state(a,b);if(!d.exact()){fprintf(stderr,"input control %s trial%u r%d byte%d LS%X/%u state%s PC%X/%X\n",label,trial,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal",a.pc,b.pc);abort();}
    assert(a.host_depth==b.host_depth && a.image_id==b.image_id && a.policy_mode==b.policy_mode && a.resident_ovl==b.resident_ovl);
}
static void original(spu_context& c){
    const auto stack=c.gpr[1]._u32[0];g_spu_trampoline_fn=nullptr;input_control_oracle::buzz_job_6eba_spu_func_000093F0(&c);
    for(unsigned steps=0;!(c.gpr[1]._u32[0]==stack && c.pc==c.gpr[0]._u32[0] && g_spu_trampoline_fn==(c.host_depth?nullptr:spu_indirect_branch));++steps){assert(steps<1048576 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&c);}
}
static void frame_model(const spu_context& c,unsigned char* expected){
    memcpy(expected,c.ls,SPU_LS_SIZE);const unsigned sp=c.gpr[1]._u32[0];
    auto put=[&](unsigned address,const u128& value){address&=0x3fff0;for(unsigned lane=0;lane<4;++lane)geometry_job_put32(expected+address+lane*4,value._u32[lane]);};
    put(sp-416,c.gpr[126]);for(unsigned reg=80;reg<=104;++reg)put(sp-(reg-79)*16,c.gpr[reg]);put(sp+16,c.gpr[0]);put(sp-448,c.gpr[1]);
}
static void check(spu_context& c,spu_context& ref,spu_context& prefix){
    auto saved=std::make_unique<spu_context>();memcpy(saved.get(),&c,sizeof c);GeometryInputControl plan;const char* why=nullptr;g_spu_trampoline_fn=spu_indirect_branch;
    if(!read_geometry_input_control(c,plan,&why)){fprintf(stderr,"input control admission trial%u format%u: %s\n",strict_cases,c.gpr[6]._u32[0]&255,why);abort();}
    assert(!memcmp(saved.get(),&c,sizeof c) && g_spu_trampoline_fn==spu_indirect_branch);
    auto frame=std::make_unique<unsigned char[]>(SPU_LS_SIZE);frame_model(c,frame.get());copy(ref,c);open_geometry_input_frame(ref);assert(!memcmp(ref.ls,frame.get(),SPU_LS_SIZE));++frame_models;
    if(!plan.empty){
        copy(prefix,c);g_spu_trampoline_fn=nullptr;input_control_oracle::buzz_job_6eba_spu_func_000093F0(&prefix);
        const unsigned targets[]={0x9700,0xa8f8,0xa6a8,0xa390,0x9e70,0x97f8};
        for(unsigned steps=0;!(prefix.pc==targets[plan.format] && g_spu_trampoline_fn==spu_indirect_branch);++steps){assert(steps<16 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&prefix);}
        copy(ref,c);open_geometry_input_frame(ref);prepare_geometry_input_dispatch(ref);equal(ref,prefix,"dispatch",strict_cases);assert(g_spu_trampoline_fn==spu_indirect_branch);++prefix_cases;
    }
    copy(ref,c);original(ref);const auto expected=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;execute_geometry_input_control(c,plan);equal(c,ref,"whole93F0",strict_cases);assert(g_spu_trampoline_fn==expected);++strict_cases;++paths[plan.empty?0:plan.format+1];
}
static void seed(spu_context& c,const spu_context& anchor,unsigned trial,unsigned format,unsigned count,unsigned bias=0,unsigned mode=0){
    copy(c,anchor);seed_word=trial+1;for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word();
    unsigned sp=0x3f000+bias,manager=0x3d000,source=0x18000+bias,params=0x20000+bias;
    switch(mode){case 1:source=(manager&~15u)+0x138+bias;break;case 2:source=(sp&~15u)-96+bias;break;case 3:params=(sp&~15u)-128+bias;break;
    case 7:manager=(sp&~15u)-320;break;case 8:manager=(sp&~15u)-608;break;case 9:manager=(sp&~15u)-16;break;case 10:manager=(sp&~15u)-96;break;}
    for(unsigned reg=80;reg<=104;++reg)for(unsigned lane=0;lane<4;++lane)c.gpr[reg]._u32[lane]=0x24000+((reg+lane)%4)*0x4000;
    c.gpr[95]=spu_il(0);const auto base=manager&~15u;
    for(unsigned row=0;row<512;++row)for(unsigned lane=0;lane<4;++lane)geometry_job_put32(c.ls+((source+row*16)&0x3fff0)+lane*4,random_word());
    for(unsigned row=0;row<4;++row)for(unsigned lane=0;lane<4;++lane)geometry_job_put32(c.ls+((params+row*16)&0x3fff0)+lane*4,random_word());
    memset(c.ls+base,0,0x190);c.ls[base+8]=count>>8;c.ls[base+9]=count;
    memset(c.ls+base+0x160,mode==11?0x55:0,16);
    for(unsigned slot=0;slot<17;++slot){unsigned pointer=slot<4?0x24000+slot*0x4000:0x30000;
        if(mode==4 && !slot)pointer=(sp&~15u)-32;if(mode==5 && slot==1)pointer=(sp&~15u)+16;if(mode==6)pointer=0x24000+slot*16;
        geometry_job_put32(c.ls+base+16+slot*4,pointer+bias);}
    c.gpr[0]=spu_link(0x12345678);c.gpr[1]._u32[0]=sp;c.gpr[3]._u32[0]=manager;c.gpr[4]._u32[0]=source;c.gpr[5]._u32[0]=params;c.gpr[6]._u32[0]=(c.gpr[6]._u32[0]&~255u)|format;
    c.pc=0x93f0;c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;c.event_status=random_word();c.event_mask=random_word();c.resv_valid=trial&1;c.mfc_tag_status=random_word();c.int_enable=0;c.irq_saved=0;c.irq_frame=nullptr;
}
int main(){
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto anchor=std::make_unique<spu_context>(),native=std::make_unique<spu_context>(),ref=std::make_unique<spu_context>(),prefix=std::make_unique<spu_context>();
    memset(anchor.get(),0,sizeof(*anchor));anchor->ls=anchor->ls_store;anchor->image_id=41;
    auto* file=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin","rb");assert(file);assert(fread(anchor->gpr,sizeof anchor->gpr,1,file)==1 && fread(anchor->ls,SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF);fclose(file);
    const unsigned counts[]={0,1,2,3,4,5,7,8,15,16,31,32,33,63,64,65,127,128,129};
    for(unsigned format=0;format<6;++format)for(unsigned count:counts)for(unsigned bias=0;bias<(format?16:1);++bias)for(unsigned depth=0;depth<3;++depth){seed(*native,*anchor,strict_cases,format,count,bias);native->host_depth=depth;check(*native,*ref,*prefix);}
    for(unsigned format=1;format<6;++format)for(unsigned trial=0;trial<512;++trial){seed(*native,*anchor,10000+format*1000+trial,format,1+trial%129,trial&15,trial%12);check(*native,*ref,*prefix);}
    const unsigned masks[]={0xd1b0,0xd1c0,0xd980,0xda80,0xd190,0xd1a0,0xcd60,0xd8b0,0xd1d0,0xd1e0,0xd1f0,0xda20,0xd280,0xd290,0xd2a0,0xd200,0xd210,0xd220,0xd910};
    for(unsigned trial=0;trial<512;++trial){seed(*native,*anchor,20000+trial,1+trial%5,1+trial%65,trial&15,trial%7);for(unsigned address:masks)if(random_word()&1)for(unsigned lane=0;lane<4;++lane)geometry_job_put32(native->ls+address+lane*4,random_word());check(*native,*ref,*prefix);}
    for(unsigned trial=0;trial<256;++trial){seed(*native,*anchor,30000+trial,trial,0,trial&15,7+trial%4);check(*native,*ref,*prefix);}
    for(unsigned format=1;format<6;++format)for(unsigned bias=0;bias<16;++bias)for(unsigned depth=0;depth<3;++depth){seed(*native,*anchor,31000+strict_cases,format,1+bias,bias);native->host_depth=depth;native->gpr[3]._u32[0]+=bias;
        const auto pointers=spu_rotqby(spu_il(0x24000+bias),spu_il(-int(bias)));for(unsigned quad=0;quad<5;++quad)spu_ls_write128(native.get(),0x3d010+quad*16,pointers);check(*native,*ref,*prefix);}
    for(unsigned trial=0;trial<96;++trial){seed(*native,*anchor,32000+trial,trial*7,0,trial&15);native->gpr[1]._u32[0]=(trial%3==0?0x3ffe0:trial%3==1?0x7f000:0xfffff000)+(trial&15);native->gpr[3]._u32[0]=native->gpr[1]._u32[0]-128;
        const unsigned manager=native->gpr[3]._u32[0]&0x3fff0;native->ls[manager+8]=native->ls[manager+9]=0;check(*native,*ref,*prefix);}
    for(unsigned format=1;format<6;++format)for(unsigned bias=0;bias<16;++bias){seed(*native,*anchor,33000+strict_cases,format,1+bias,bias);for(unsigned slot=0;slot<6;++slot)if(slot!=format)geometry_job_put32(native->ls+0x95c4+slot*4,random_word());check(*native,*ref,*prefix);}
    for(unsigned format=1;format<6;++format)for(unsigned count:{255u,256u,257u,1023u,2048u,4095u}){seed(*native,*anchor,34000+strict_cases,format,count);geometry_job_put32(native->ls+0x3d01c,0x20000);check(*native,*ref,*prefix);}
    for(unsigned failure=0;failure<23;++failure){seed(*native,*anchor,40000,1,17);switch(failure){
        case 0:native->ls=ref->ls_store;break;case 1:native->int_enable=1;break;case 2:native->irq_saved=1;break;case 3:native->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;
        case 4:native->gpr[1]._u32[0]=0xe700;break;case 5:native->gpr[1]._u32[0]=16;break;case 6:native->gpr[6]._u32[0]=6;break;case 7:native->gpr[6]._u32[0]=255;break;case 8:native->ls[0x95cb]^=1;break;
        case 9:geometry_job_put32(native->ls+0x3d010,0);break;case 10:native->gpr[3]._u32[0]=0x1000;native->ls[0x1008]=0;native->ls[0x1009]=17;break;
        case 11:native->gpr[6]._u32[0]=4;native->ls[0x95d7]^=1;break;case 12:native->gpr[6]._u32[0]=0;native->gpr[4]._u32[0]|=1;break;
        case 13:native->gpr[6]._u32[0]=0;native->ls[0xd250]^=1;break;case 14:native->gpr[6]._u32[0]=0;memset(native->ls+0x3d160,1,16);break;
        case 15:native->gpr[6]._u32[0]=4;geometry_job_put32(native->ls+0x3d014,SPU_LS_SIZE-((17+1)&~1u)*16);break;case 16:native->gpr[6]._u32[0]=5;geometry_job_put32(native->ls+0x3d01c,SPU_LS_SIZE-((17+1)&~1u)*16);break;}
        if(failure>=17){const unsigned format=failure-17;native->gpr[6]._u32[0]=format;native->ls[0x95c4+format*4+3]^=1;}
        memcpy(ref.get(),native.get(),sizeof(*native));GeometryInputControl plan;memset(&plan,0xa5,sizeof plan);auto saved=plan;const char* why=nullptr;g_spu_trampoline_fn=spu_indirect_branch;
        assert(!read_geometry_input_control(*native,plan,&why) && why && !memcmp(&plan,&saved,sizeof plan) && !memcmp(native.get(),ref.get(),sizeof(*native)) && g_spu_trampoline_fn==spu_indirect_branch);
    }
    assert(strict_cases==8391 && prefix_cases==7796 && frame_models==8391);
    printf("PASS whole input control:%u strict complete93F0 roots/%u exact dispatch boundaries/%u independent frame byte models;paths empty/raw/1/2/3/4/5=%u/%u/%u/%u/%u/%u/%u;512 arbitrary-mask cases;240 biased-manager roots;352 empty ignored-format/frame-alias/wrapped-stack roots;80 unselected-target cases;30 large-count roots;23 unchanged rejects;synthetic only\n",strict_cases,prefix_cases,frame_models,paths[0],paths[1],paths[2],paths[3],paths[4],paths[5],paths[6]);
}
