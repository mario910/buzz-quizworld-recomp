#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_transform_control.h"
#include "../port/native_geometry_vertex_prepare.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*){abort();}
void spu_stop(spu_context*){abort();}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_drain_call(spu_context* c,uint32_t link){
    for(unsigned steps=0;g_spu_trampoline_fn && g_spu_trampoline_fn!=spu_indirect_branch;++steps){assert(steps<32);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);}
    if(g_spu_trampoline_fn==spu_indirect_branch && c->pc==link)g_spu_trampoline_fn=nullptr;assert(!g_spu_trampoline_fn && c->pc==link);
}
}
static void transform_control_test_bridge(spu_context*,buzz_geometry_job_fn,buzz_geometry_job_fn,buzz_geometry_job_fn);
#include "../analysis/native-geometry/phase3/test-transform-control-reference.inc"
#include "../analysis/native-geometry/phase3/test-vertex-prepare-reference.inc"
static unsigned child_mask=0;static bool late_alias=false;
static std::vector<std::unique_ptr<spu_context>> trace;
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void record(const spu_context& c){auto p=std::make_unique<spu_context>();memcpy(p.get(),&c,sizeof c);p->ls=p->ls_store;trace.push_back(std::move(p));}
static void vertex_data(spu_context* c){
    record(*c);uint32_t state=c->gpr[3]._u32[0]+0x123456;
    for(unsigned reg=2;reg<80;++reg)for(auto& word:c->gpr[reg]._u32)word=random_word(state);
    // A retained data leaf may change memory consumed by later control.
    u128 counts{};for(auto& word:counts._u32)word=random_word(state);spu_ls_write128(c,c->gpr[81]._u32[0]+0x180,counts);SPU_RET(c);
}
static void vertex(spu_context* c){
    record(*c);
    if(child_mask&1){GeometryVertexPrepare p;assert(read_geometry_vertex_prepare(*c,p));prepare_geometry_vertices(*c,p,vertex_data);}
    else{
        g_spu_trampoline_fn=nullptr;vertex_prepare_oracle::buzz_job_6eba_spu_func_00006188(c);
        while(g_spu_trampoline_fn!=vertex_prepare_oracle::buzz_job_6eba_spu_func_000063E0){auto next=g_spu_trampoline_fn;assert(next);g_spu_trampoline_fn=nullptr;next(c);}
        g_spu_trampoline_fn=vertex_data;
    }
}
static void finish(spu_context* c){
    record(*c);
    if(child_mask&2){GeometryTransformFinish p;assert(read_geometry_transform_finish(*c,p));finish_geometry_transform(*c,p);}
    else transform_control_oracle::buzz_job_6eba_spu_func_00006958(c);
}
static void cull(spu_context* c){
    record(*c);uint32_t state=c->gpr[6]._u32[0]+(child_mask&4?0x987654:0x123456);
    for(unsigned reg=2;reg<104;++reg)if(reg!=81)for(auto& word:c->gpr[reg]._u32)word=random_word(state);
    c->gpr[50]=spu_ila(state&65535);
    u128 written{};for(auto& word:written._u32)word=random_word(state);spu_ls_write128(c,c->gpr[81]._u32[0]+0x160,written);
    if(late_alias){written._u32[0]=0x6958;spu_ls_write128(c,c->gpr[1]._u32[0]+480+16,written);}
    c->pc=0x6958;g_spu_trampoline_fn=finish;
}
static void alternate(spu_context* c){assert(c->pc==0x69e8);cull(c);}
static const buzz_geometry_transform_functions library={vertex,cull,alternate,finish,transform_control_oracle::buzz_job_6eba_spu_func_000066C8};
static void transform_control_test_bridge(spu_context* c,buzz_geometry_job_fn root,buzz_geometry_job_fn boundary,buzz_geometry_job_fn replacement){
    g_spu_trampoline_fn=nullptr;root(c);assert(c->pc==0x6958 && g_spu_trampoline_fn==boundary);g_spu_trampoline_fn=replacement;
}
static void seed(spu_context& c,unsigned trial,unsigned mode){
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x6608;uint32_t state=trial+1;
    for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word(state);
    for(unsigned at=0;at<SPU_LS_SIZE;at+=4){const auto w=random_word(state);memcpy(c.ls+at,&w,4);}
    c.gpr[1]._u32[0]=0x39000+(trial&15);c.gpr[3]._u32[0]=0x3d000+((trial/16)&15);c.gpr[4]._u32[0]=mode;
    c.ls[0x3d000]=uint8_t((trial&0x90)?trial:trial|0x80);c.ls[0x3d140]=trial%7;
    c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;c.int_enable=trial&1;c.event_mask=trial*13;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;
}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void compare(spu_context& native,spu_context& reference,unsigned trial){
    GeometryTransformControl p;const char* why=nullptr;assert(read_geometry_transform_control(native,p,&why));const unsigned stack=native.gpr[1]._u32[0];
    trace.clear();transform_control_oracle::children=&library;g_spu_trampoline_fn=spu_indirect_branch;
    transform_control_oracle::buzz_job_6eba_spu_func_00006608(&reference);
    for(unsigned steps=0;!(reference.gpr[1]._u32[0]==stack && reference.pc==reference.gpr[0]._u32[0] && g_spu_trampoline_fn==(reference.host_depth?nullptr:spu_indirect_branch));++steps){
        assert(steps<32 && g_spu_trampoline_fn && g_spu_trampoline_fn!=spu_indirect_branch);auto next=g_spu_trampoline_fn;
        if(next==transform_control_oracle::buzz_job_6eba_spu_func_00006958)record(reference);
        g_spu_trampoline_fn=nullptr;next(&reference);
    }
    const auto expected=g_spu_trampoline_fn;auto original_trace=std::move(trace);trace.clear();g_spu_trampoline_fn=spu_indirect_branch;
    execute_geometry_transform_control(native,p,library);const auto d=compare_function_state(native,reference);
    if(!d.exact()){fprintf(stderr,"control trial%u child%u r%d byte%d LS%X/%u state%s\n",trial,child_mask,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");abort();}
    assert(g_spu_trampoline_fn==expected && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
    // Check every child ENTRY, before a clobber/footer could hide setup errors.
    // Original footer entries are captured by the independent graph drainer.
    assert(trace.size()==original_trace.size());
    for(unsigned i=0;i<trace.size();++i){const auto diff=compare_function_state(*trace[i],*original_trace[i]);if(!diff.exact()){
        fprintf(stderr,"entry trial%u child%u step%u r%d byte%d LS%X/%u state%s PC%X/%X\n",trial,child_mask,i,diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal",trace[i]->pc,original_trace[i]->pc);abort();}
        assert(trace[i]->host_depth==original_trace[i]->host_depth && trace[i]->image_id==original_trace[i]->image_id && trace[i]->policy_mode==original_trace[i]->policy_mode && trace[i]->resident_ovl==original_trace[i]->resident_ovl);
    }
}
int main(){
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();unsigned exact=0;
    const unsigned modes[]={0,1,2,3,4,0x80000001,0x80000002,0x80000003,0xffffffff};
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial,modes[trial%9]);child_mask=trial%8;late_alias=trial%4==0;
        if(trial%9==0){auto config=spu_ls_read128(native.get(),0x3d000);config._u32[3]=trial&1?0xffffffff:17;spu_ls_write128(native.get(),0x3d000,config);}
        copy(*reference,*native);compare(*native,*reference,trial);++exact;
    }
    for(unsigned mask=0;mask<8;++mask)for(unsigned shape=0;shape<9;++shape)for(unsigned bias=0;bias<16;++bias){seed(*native,bias,modes[shape]);child_mask=mask;late_alias=bias&1;
        native->gpr[3]._u32[0]=0xe680+bias;native->ls[0xe680]=bias&1?0x80:0x10;native->ls[0xe7c0]=shape%2?5:4;
        native->gpr[0]=spu_ila(bias%3==0?0x66c8:bias&1?0x6958:0x6608);copy(*reference,*native);compare(*native,*reference,10000+mask*144+shape*16+bias);++exact;
    }
    seed(*native,0,1);FILE* fixture=fopen("analysis/native-geometry/phase3/transform-audit-v1/pc_6608.bin","rb");assert(fixture);
    assert(fread(native->gpr,sizeof native->gpr,1,fixture)==1 && fread(native->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);child_mask=3;late_alias=false;
    copy(*reference,*native);compare(*native,*reference,30000);++exact;
    auto expected_ls=std::make_unique<unsigned char[]>(SPU_LS_SIZE);
    const unsigned stacks[]={0x39000,0xe680+480,SPU_LS_SIZE-32,0x30000,0x28000,0x18000,0x10000,0x20000};
    for(unsigned publish=0;publish<2;++publish)for(unsigned stack:stacks)for(unsigned bias=0;bias<16;++bias){
        seed(*native,bias,0);native->gpr[1]._u32[0]=stack+bias;child_mask=3;late_alias=false;
        u128 config=spu_ls_read128(native.get(),0x3d000);config._u32[3]=publish?17:0xffffffff;spu_ls_write128(native.get(),0x3d000,config);
        memcpy(expected_ls.get(),native->ls,SPU_LS_SIZE);const auto saved_sp=native->gpr[1],saved_link=native->gpr[0],saved_pic=native->gpr[126];u128 saved[24];
        auto store=[&](unsigned address,const u128& value){address&=0x3fff0;for(unsigned lane=0;lane<4;++lane)for(unsigned b=0;b<4;++b)expected_ls[address+lane*4+b]=uint8_t(value._u32[lane]>>(24-b*8));};
        store(stack+bias-0x190,saved_pic);store(stack+bias+16,saved_link);store(stack+bias-480,saved_sp);
        for(unsigned reg=80;reg<=103;++reg){saved[reg-80]=native->gpr[reg];store(stack+bias-(reg-79)*16,saved[reg-80]);}
        if(publish){const unsigned dest=0x3d050+((stack+bias-480+8)&12);for(unsigned b=0;b<4;++b)expected_ls[dest+b]=native->ls[0x3d050+b];}
        u128 result{};for(unsigned lane=0;lane<4;++lane)result._u32[lane]=(unsigned(native->ls[0x3d180+lane*4])<<8)|native->ls[0x3d181+lane*4];
        copy(*reference,*native);compare(*native,*reference,40000+exact);++exact;
        assert(!memcmp(native->ls,expected_ls.get(),SPU_LS_SIZE) && !memcmp(&native->gpr[3],&result,16) && !memcmp(&native->gpr[1],&saved_sp,16) && !memcmp(&native->gpr[0],&saved_link,16) && !memcmp(&native->gpr[126],&saved_pic,16));
        for(unsigned reg=80;reg<=103;++reg)assert(!memcmp(&native->gpr[reg],&saved[reg-80],16));
    }
    for(unsigned failure=0;failure<9;++failure){seed(*native,0,1);switch(failure){case 0:native->ls=reference->ls_store;break;case 1:native->int_enable=1;native->event_status=native->event_mask=1;break;
        case 2:native->gpr[3]._u32[0]=0;break;case 3:native->gpr[3]._u32[0]=SPU_LS_SIZE-0x180;break;case 4:native->gpr[1]._u32[0]=0xe680;break;
        case 5:native->gpr[1]._u32[0]=SPU_LS_SIZE-16;break;case 6:native->gpr[1]._u32[0]=SPU_LS_SIZE;break;case 7:native->gpr[1]._u32[0]=0x3d100;break;case 8:native->ls[0x3d000]=0;break;}
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryTransformControl p;p.active=true;const char* why=nullptr;
        assert(!read_geometry_transform_control(*native,p,&why) && why && p.active && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    printf("PASS transform control: %u strict full-state/LS/return and child-entry traces;8 child/baseline combinations;256 independent bypass/frame models;9 unchanged rejects\n",exact);
}
