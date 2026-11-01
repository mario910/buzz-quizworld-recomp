#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_conversion_control.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
using namespace buzz;
namespace conversion_control_oracle {static void dispatch(spu_context*);}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* c){conversion_control_oracle::dispatch(c);}
void spu_stop(spu_context*){abort();}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_drain_call(spu_context* c,uint32_t link){
    for(unsigned steps=0;!(c->pc==link && (!g_spu_trampoline_fn || g_spu_trampoline_fn==spu_indirect_branch));++steps){
        assert(steps<1000000 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);
    }
    g_spu_trampoline_fn=nullptr;
}
}
#include "../analysis/native-geometry/phase3/test-conversion-control-reference.inc"
static std::vector<std::unique_ptr<spu_context>> trace;
static unsigned child_recipe=0,strict_cases=0;
static unsigned random_word(unsigned& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void record(const spu_context& c){auto p=std::make_unique<spu_context>();memcpy(p.get(),&c,sizeof c);p->ls=p->ls_store;trace.push_back(std::move(p));}
static void formatter(spu_context* c){
    record(*c);unsigned state=c->gpr[3]._u32[0]+0x54321;
    for(unsigned reg=2;reg<80;++reg)for(auto& w:c->gpr[reg]._u32)w=random_word(state);
    const auto manager=c->gpr[81]._u32[0]&~15u,frame=c->gpr[1]._u32[0];
    if(child_recipe&1){
        auto config=spu_ls_read128(c,manager);config._u32[1]=(config._u32[1]&0x00ffffff)|((child_recipe%4)<<24);
        config._u32[2]^=0x9a7b0000;spu_ls_write128(c,manager,config);
    }
    if(child_recipe&2){
        u128 cursors;for(auto& w:cursors._u32)w=random_word(state);spu_ls_write128(c,manager+0x50,cursors);
        const auto desc=geometry_job_be32(c->ls+manager+0x74);c->ls[desc+1]=uint8_t(child_recipe*13);
    }
    if(child_recipe&4){
        for(unsigned offset:{32u,48u,64u,80u,112u}){
            u128 late;for(auto& w:late._u32)w=random_word(state);spu_ls_write128(c,frame+offset,late);
        }
    }
    SPU_RET(c);
}
static void stride(spu_context* c){
    record(*c);
    if(child_recipe&8){
        unsigned state=0x87654+child_recipe;
        for(unsigned reg=2;reg<80;++reg)for(auto& w:c->gpr[reg]._u32)w=random_word(state);
        const auto manager=c->gpr[81]._u32[0]&~15u;auto config=spu_ls_read128(c,manager);config._u32[2]^=0x83650000;spu_ls_write128(c,manager,config);
        c->gpr[3]._u32[0]=child_recipe*257;SPU_RET(c);
    }else{
        const auto saved=conversion_control_oracle::stride;conversion_control_oracle::stride=nullptr;
        conversion_control_oracle::buzz_job_6eba_spu_func_000040C8(c);conversion_control_oracle::stride=saved;
    }
}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void assert_same(const spu_context& a,const spu_context& b,unsigned trial,const char* label){
    const auto d=compare_function_state(a,b);
    if(!d.exact()){
        fprintf(stderr,"conversion %s trial%u recipe%u r%d byte%d LS%X/%u state%s PC%X/%X\n",label,trial,child_recipe,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal",a.pc,b.pc);abort();
    }
    assert(a.host_depth==b.host_depth && a.image_id==b.image_id && a.policy_mode==b.policy_mode && a.resident_ovl==b.resident_ovl);
}
static void seed(spu_context& c,unsigned trial,unsigned path){
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x54e8;unsigned state=trial+1;
    for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word(state);
    for(unsigned at=0;at<SPU_LS_SIZE;at+=4){const auto word=random_word(state);memcpy(c.ls+at,&word,4);}
    const unsigned manager=0x3d000,stack=0x39000;
    c.gpr[3]._u32[0]=manager+(trial&15);c.gpr[1]._u32[0]=stack+((trial/16)&15);
    c.ls[manager]=uint8_t((c.ls[manager]&~16u)|(path==0?16:0));c.ls[manager+4]=uint8_t(4+trial%252);
    c.ls[manager+8]=path==1?0:uint8_t(trial>>4);c.ls[manager+9]=path==1?0:uint8_t(trial|1);
    geometry_job_put32(c.ls+manager+0x74,0x24000);geometry_job_put32(c.ls+manager+0x88,0);geometry_job_put32(c.ls+manager+0x8c,0);
    c.ls[0x24001]=uint8_t(trial);
    const unsigned table[]={0x68,0x60,0x58,0x58};for(unsigned i=0;i<4;++i)geometry_job_put32(c.ls+0x4104+i*4,table[i]);
    c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;c.int_enable=trial&1;c.event_mask=trial*13;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;
}
static void compare(spu_context& native,spu_context& reference,unsigned trial,bool real=false){
    GeometryConversionControl plan;const char* why=nullptr;assert(read_geometry_conversion_control(native,plan,&why));const auto sp=native.gpr[1]._u32[0];
    conversion_control_oracle::formatter=real?nullptr:formatter;conversion_control_oracle::stride=real?nullptr:stride;trace.clear();g_spu_trampoline_fn=nullptr;
    conversion_control_oracle::buzz_job_6eba_spu_func_000054E8(&reference);
    for(unsigned steps=0;!(reference.gpr[1]._u32[0]==sp && reference.pc==reference.gpr[0]._u32[0] && g_spu_trampoline_fn==(reference.host_depth?nullptr:spu_indirect_branch));++steps){
        assert(steps<1000000 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;
        next(&reference);
    }
    auto expected_fn=g_spu_trampoline_fn;auto expected_trace=std::move(trace);trace.clear();g_spu_trampoline_fn=nullptr;
    const GeometryConversionCallees children{real?conversion_control_oracle::buzz_job_6eba_spu_func_00004178:formatter,
        real?conversion_control_oracle::buzz_job_6eba_spu_func_000040C8:stride};
    execute_geometry_conversion_control(native,plan,children);assert_same(native,reference,trial,"exit");assert(g_spu_trampoline_fn==expected_fn);
    assert(trace.size()==expected_trace.size());for(unsigned i=0;i<trace.size();++i)assert_same(*trace[i],*expected_trace[i],trial,"child entry");
    ++strict_cases;
}
int main(){
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    for(unsigned trial=0;trial<4096;++trial){child_recipe=(trial/3)%16;seed(*native,trial,trial%3);copy(*reference,*native);compare(*native,*reference,trial);}
    // No-cache checks: frame stores and publications are modeled independently.
    auto expected_ls=std::make_unique<unsigned char[]>(SPU_LS_SIZE);unsigned independent=0;
    for(unsigned path=0;path<2;++path)for(unsigned bias=0;bias<16;++bias)for(unsigned stack:{0x39000u,0xe6e0u,SPU_LS_SIZE-32u}){
        seed(*native,bias,path);native->gpr[1]._u32[0]=stack+bias;const auto sp=native->gpr[1],link=native->gpr[0],pic=native->gpr[126];
        const auto old=spu_ls_read128(native.get(),0x3d050),result=spu_rotqbyi(old,path==0?4:8);u128 nonvolatile[3];for(unsigned i=0;i<3;++i)nonvolatile[i]=native->gpr[80+i];
        memcpy(expected_ls.get(),native->ls,SPU_LS_SIZE);
        auto store=[&](unsigned at,const u128& value){at&=0x3fff0;for(unsigned lane=0;lane<4;++lane)geometry_job_put32(expected_ls.get()+at+lane*4,value._u32[lane]);};
        store(sp._u32[0]+16,link);store(sp._u32[0]-64,pic);store(sp._u32[0]-96,sp);
        for(unsigned i=0;i<3;++i)store(sp._u32[0]-(i+1)*16,nonvolatile[i]);
        if(path==1)geometry_job_put32(expected_ls.get()+0x3d050+((sp._u32[0]+4)&12),old._u32[2]);
        copy(*reference,*native);compare(*native,*reference,10000+independent);++independent;
        assert(!memcmp(expected_ls.get(),native->ls,SPU_LS_SIZE) && !memcmp(&native->gpr[3],&result,16) && !memcmp(&native->gpr[1],&sp,16) && !memcmp(&native->gpr[0],&link,16) && !memcmp(&native->gpr[126],&pic,16));
        for(unsigned i=0;i<3;++i)assert(!memcmp(&native->gpr[80+i],&nonvolatile[i],16));
    }
    // Exact real inputs and retained original data: no claim of native data.
    for(unsigned depth=0;depth<3;++depth){seed(*native,0,2);auto* file=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin","rb");assert(file);
        assert(fread(native->gpr,sizeof native->gpr,1,file)==1 && fread(native->ls,SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF);fclose(file);native->host_depth=depth;
        copy(*reference,*native);compare(*native,*reference,20000+depth,true);
    }
    for(unsigned failure=0;failure<11;++failure){seed(*native,0,2);switch(failure){
        case 0:native->ls=reference->ls_store;break;case 1:native->int_enable=1;native->event_status=native->event_mask=1;break;
        case 2:native->gpr[3]._u32[0]=0;break;case 3:native->gpr[3]._u32[0]=SPU_LS_SIZE-0x80;break;
        case 4:native->gpr[1]._u32[0]=0xe680;break;case 5:native->gpr[1]._u32[0]=SPU_LS_SIZE-16;break;
        case 6:native->gpr[1]._u32[0]=SPU_LS_SIZE;break;case 7:native->gpr[1]._u32[0]=0x3d080;break;
        case 8:native->ls[0x3d004]=0;break;case 9:geometry_job_put32(native->ls+0x3d08c,0xbeef);break;
        case 10:geometry_job_put32(native->ls+0x3d074,0);break;}
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryConversionControl plan;const char* why=nullptr;
        assert(!read_geometry_conversion_control(*native,plan,&why) && why && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    printf("PASS conversion control: %u strict full-state/LS/FN/child-entry cases;16 callee recipes;%u independent frame/publication models;3 real-input original-data combinations;11 unchanged rejects;production/native data gates OPEN\n",strict_cases,independent);
}
