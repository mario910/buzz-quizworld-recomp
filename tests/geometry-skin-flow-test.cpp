#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_skin_flow.h"
#include "../port/native_geometry_rigid_skin.h"
#include "../port/native_geometry_skin_cofactor.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
using namespace buzz;
static void skin_flow_dispatch(spu_context*);
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* c){skin_flow_dispatch(c);}
void spu_stop(spu_context*){abort();}
void spu_drain_call(spu_context* c,uint32_t link){assert(!g_spu_trampoline_fn && c->pc==link);}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
}
struct SkinFlowCall {uint32_t callee,pc,depth;u128 registers[128];};
static std::vector<SkinFlowCall> calls;
static unsigned trial;
static bool real_rigid=false,real_cofactor=false;
static unsigned cofactor_integration_calls=0;
static void original_rigid_model(spu_context*);
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void skin_flow_trace(spu_context* c,uint32_t pc)
{
    SkinFlowCall t{};t.callee=pc;t.pc=c->pc;t.depth=c->host_depth;memcpy(t.registers,c->gpr,sizeof(t.registers));calls.push_back(t);
}
static void skin_flow_model(spu_context* c,uint32_t pc)
{
    skin_flow_trace(c,pc);
    if(real_rigid && pc==0xb348){original_rigid_model(c);return;}
    uint32_t state=pc^c->pc^c->host_depth;
    for(const auto& reg:c->gpr)for(auto word:reg._u32)state=(state^word)*16777619;
    const unsigned count=c->gpr[84]._u32[0];assert(count && !(count&3) && count<=4096);
    for(unsigned reg=2;reg<33;++reg)for(auto& word:c->gpr[reg]._u32)word=random_word(state);
    if(pc==0xb880 || pc==0xb348){
        for(unsigned i=0;i<count*3;++i){u128 q;for(auto& w:q._u32)w=random_word(state);spu_ls_write128(c,c->gpr[96]._u32[0]+i*16,q);}
        c->gpr[80]=spu_ai(c->gpr[80],count*16);c->gpr[85]=spu_ai(c->gpr[85],count*(pc==0xb880?8:2));
    }else{
        c->gpr[67]=spu_ai(c->gpr[67],count*16);c->gpr[71]=spu_ai(c->gpr[71],count*16);c->gpr[93]=spu_ai(c->gpr[93],count*16);
    }
    spu_ls_write128(c,0x34000+16*(calls.size()%128),c->gpr[12]);
}
#include "../analysis/native-geometry/phase3/test-skin-flow-reference.inc"
#include "../analysis/native-geometry/phase3/test-rigid-skin-reference.inc"
#include "../analysis/native-geometry/phase3/test-skin-cofactor-reference.inc"
static void finish_rigid_model(spu_context* c,bool native_cofactor)
{
    assert(c->pc==0xb4c0 || c->pc==0xbb58);g_spu_trampoline_fn=nullptr;
    if(c->pc==0xbb58){
        if(!real_cofactor){skin_flow_model(c,0xbb58);return;}
        skin_flow_trace(c,0xbb58);++cofactor_integration_calls;const auto tail=skin_cofactor_oracle::buzz_job_6eba_spu_func_0000B4C0;
        if(native_cofactor){GeometrySkinCofactor plan;assert(read_geometry_skin_cofactor(*c,plan));transform_geometry_skin_cofactor(*c,plan,tail);}
        else{skin_cofactor_oracle::buzz_job_6eba_spu_func_0000BB58(c);
            for(unsigned steps=0;g_spu_trampoline_fn!=tail;++steps){assert(steps<4 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);}}
        assert(c->pc==0xb4c0 && g_spu_trampoline_fn==tail);g_spu_trampoline_fn=nullptr;
    }
}
static void original_rigid_model(spu_context* c)
{
    g_spu_trampoline_fn=nullptr;rigid_skin_oracle::buzz_job_6eba_spu_func_0000B348(c);
    for(unsigned steps=0;c->pc!=0xb4c0 && c->pc!=0xbb58;++steps){assert(steps<4096 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);}
    finish_rigid_model(c,false);
}
static void native_rigid_model(spu_context* c)
{
    skin_flow_trace(c,0xb348);GeometryRigidSkin plan;assert(read_geometry_rigid_skin(*c,plan));
    transform_geometry_rigid_skin(*c,plan,rigid_skin_oracle::buzz_job_6eba_spu_func_0000B4C0,rigid_skin_oracle::buzz_job_6eba_spu_func_0000BB58);
    finish_rigid_model(c,true);c->pc=0xb4c0;g_spu_trampoline_fn=skin_flow_oracle::buzz_job_6eba_spu_func_0000B4C0;
}
static void original_rigid_native_cofactor_model(spu_context* c)
{
    skin_flow_trace(c,0xb348);g_spu_trampoline_fn=nullptr;rigid_skin_oracle::buzz_job_6eba_spu_func_0000B348(c);
    for(unsigned steps=0;c->pc!=0xb4c0 && c->pc!=0xbb58;++steps){assert(steps<4096 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);}
    finish_rigid_model(c,true);c->pc=0xb4c0;g_spu_trampoline_fn=skin_flow_oracle::buzz_job_6eba_spu_func_0000B4C0;
}
static void skin_flow_dispatch(spu_context* c){skin_flow_oracle::dispatch(c);}
static void seed(spu_context& c)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0xaf18;c.host_depth=(trial/17)%3;
    uint32_t state=trial+1;for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word(state);
    for(unsigned i=0;i<SPU_LS_SIZE;++i)c.ls[i]=uint8_t(random_word(state));
    const unsigned returns[]={0xaf18,0xb100,0xb868,0x1234};c.gpr[0]._u32[0]=returns[(trial/51)%4];
    c.gpr[1]._u32[0]=0x3f000-16*((trial/7)%16);c.gpr[3]._u32[0]=0xe800;
    c.gpr[4]._u32[0]=0x10000+(trial&15);c.gpr[5]._u32[0]=1+(trial/11)%256;c.gpr[6]._u32[0]=0x14000+(trial&15);
    auto* m=c.ls+0xe800;m[0]=0x80|((trial/5)%16);m[6]=(trial%15)+1;
    const unsigned counts[]={1,4,5,8,9,12,16,20,48,128,512};const unsigned n=counts[(trial/15)%11];
    m[8]=n>>8;m[9]=n;
    const unsigned presence=(trial/165)%8;
    for(unsigned off=0;off<4;++off)geometry_job_put32(m+0x5c+4*off,(!off || (presence&(1<<(off-1))))?(0x1b000+off*0x5000+(trial+off*3)%16):0);
    for(unsigned i=0;i<16;++i)geometry_job_put32(m+0x10+4*i,0x30000+(trial%16));
    const uint32_t targets[]={0xbd4c,0xbe20,0xbf7c,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xc14c,0xc280,0xb5a0};
    for(unsigned i=0;i<19;++i)geometry_job_put32(c.ls+0xb554+4*i,targets[i]-0xb554);
    switch((trial/1320)%4){case 1:m[0]|=16;break;case 2:c.gpr[5]._u32[0]=0;break;case 3:m[6]&=0xf0;break;}
    c.int_enable=trial&1;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;c.policy_mode=trial%3;c.resident_ovl=trial%5;
}
static void reference_run(spu_context& c)
{
    const uint32_t link=c.gpr[0]._u32[0],stack=c.gpr[1]._u32[0];
    skin_flow_oracle::buzz_job_6eba_spu_func_0000AF18(&c);
    for(unsigned steps=0;;++steps){
        if(c.pc==link && c.gpr[1]._u32[0]==stack && (g_spu_trampoline_fn==nullptr || g_spu_trampoline_fn==spu_indirect_branch))return;
        assert(steps<32768 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&c);
    }
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();unsigned work=0,empty=0,weighted=0,rigid=0,directions[6]{};
    for(trial=0;trial<6600;++trial){
        seed(*native);memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;
        GeometrySkinFlow p;assert(read_geometry_skin_flow(*native,p));calls.clear();g_spu_trampoline_fn=nullptr;
        reference_run(*reference);const auto expected=g_spu_trampoline_fn;const auto trace=calls;
        calls.clear();g_spu_trampoline_fn=nullptr;execute_geometry_skin_flow(*native,p,skin_flow_oracle::library);
        const auto diff=compare_function_state(*native,*reference);
        if(!diff.exact()){fprintf(stderr,"skin_flow%u work%d r%d byte%d LS%X/%u state%s\n",trial,p.work,diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");abort();}
        assert(g_spu_trampoline_fn==expected && native->host_depth==reference->host_depth && native->image_id==reference->image_id && native->policy_mode==reference->policy_mode && native->resident_ovl==reference->resident_ovl);
        if(calls.size()!=trace.size()){fprintf(stderr,"skin_flow%u calls%zu expected%zu\n",trial,calls.size(),trace.size());abort();}
        for(unsigned i=0;i<calls.size();++i)if(memcmp(&calls[i],&trace[i],sizeof(SkinFlowCall))){
            unsigned reg=0;while(reg<128 && !memcmp(&calls[i].registers[reg],&trace[i].registers[reg],16))++reg;
            fprintf(stderr,"skin_flow%u call%u callee%X pc%X/%X depth%u/%u firstreg%u\n",trial,i,calls[i].callee,calls[i].pc,trace[i].pc,calls[i].depth,trace[i].depth,reg);abort();
        }
        p.work?++work:++empty;
        for(const auto& call:calls){weighted+=call.callee==0xb880;rigid+=call.callee==0xb348;
            const uint32_t roots[]={0xbd4c,0xbe20,0xbf7c,0xc14c,0xc280,0xb5a0};for(unsigned i=0;i<6;++i)directions[i]+=call.callee==roots[i];}
    }
    real_rigid=true;
    for(trial=0;trial<384;++trial){
        seed(*native);native->ls[0xe806]=4+trial%3;const unsigned n=4*(1+trial%32);native->ls[0xe808]=n>>8;native->ls[0xe809]=n;
        const unsigned presence=(trial/3)%8;
        for(unsigned stream=1;stream<4;++stream)geometry_job_put32(native->ls+0xe85c+stream*4,(presence&(1<<(stream-1)))?(0x1b000+stream*0x5000+(trial+stream*3)%16):0);
        memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;GeometrySkinFlow plan;assert(read_geometry_skin_flow(*native,plan) && plan.work);
        auto library=skin_flow_oracle::library;library.rigid=native_rigid_model;
        calls.clear();g_spu_trampoline_fn=nullptr;reference_run(*reference);const auto expected=g_spu_trampoline_fn;const auto trace=calls;
        calls.clear();g_spu_trampoline_fn=nullptr;execute_geometry_skin_flow(*native,plan,library);
        const auto diff=compare_function_state(*native,*reference);
        if(!diff.exact()){fprintf(stderr,"skin_flow_real_rigid%u r%d byte%d LS%X/%u state%s\n",trial,diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");abort();}
        assert(g_spu_trampoline_fn==expected && native->host_depth==reference->host_depth && native->image_id==reference->image_id && native->policy_mode==reference->policy_mode && native->resident_ovl==reference->resident_ovl);
        assert(calls.size()==trace.size() && !memcmp(calls.data(),trace.data(),calls.size()*sizeof(SkinFlowCall)));
    }
    real_cofactor=true;
    for(unsigned backend=0;backend<2;++backend)for(trial=0;trial<384;++trial){
        seed(*native);native->ls[0xe806]=6;const unsigned n=4*(1+trial%32);native->ls[0xe808]=n>>8;native->ls[0xe809]=n;
        const unsigned presence=1+2*((trial/3)%4);
        for(unsigned stream=1;stream<4;++stream)geometry_job_put32(native->ls+0xe85c+stream*4,(presence&(1<<(stream-1)))?(0x1b000+stream*0x5000+(trial+stream*3)%16):0);
        if(trial&4)geometry_job_put32(native->ls+0xe860,0x30000+(trial&15)); // Matrix/normal feedback.
        memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;GeometrySkinFlow plan;assert(read_geometry_skin_flow(*native,plan) && plan.work);
        auto library=skin_flow_oracle::library;library.rigid=backend?native_rigid_model:original_rigid_native_cofactor_model;
        calls.clear();g_spu_trampoline_fn=nullptr;reference_run(*reference);const auto expected=g_spu_trampoline_fn;const auto trace=calls;
        calls.clear();g_spu_trampoline_fn=nullptr;execute_geometry_skin_flow(*native,plan,library);
        const auto diff=compare_function_state(*native,*reference);
        if(!diff.exact()){fprintf(stderr,"skin_flow_real_cofactor%u backend%u r%d byte%d LS%X/%u state%s\n",trial,backend,diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");abort();}
        assert(g_spu_trampoline_fn==expected && native->host_depth==reference->host_depth && native->image_id==reference->image_id && native->policy_mode==reference->policy_mode && native->resident_ovl==reference->resident_ovl);
        assert(calls.size()==trace.size() && !memcmp(calls.data(),trace.data(),calls.size()*sizeof(SkinFlowCall)));
    }
    real_cofactor=false;assert(cofactor_integration_calls>=1536);
    real_rigid=false;
    // Independent packed matrix mapping with the real format's two tables.
    // Expected words come from the documented layout, without SPU helpers.
    for(unsigned count:{1u,4u,256u}){
        trial=0;seed(*native);GeometrySkinFlow p;p.count=count;p.palette=0x10000;native->gpr[5]._u32[0]=count;native->gpr[97]=native->gpr[4];
        const uint8_t y[]={0x14,0x15,0x16,0x17,8,9,10,11,0x10,0x11,0x12,0x13,12,13,14,15};
        const uint8_t z[]={0x18,0x19,0x1a,0x1b,0x10,0x11,0x12,0x13,4,5,6,7,12,13,14,15};
        memcpy(native->ls+0xda10,y,16);memcpy(native->ls+0xda60,z,16);
        for(unsigned i=0;i<(count+2)*12;++i)geometry_job_put32(native->ls+p.palette+4*i,0x10000000+i);
        std::vector<uint8_t> before(native->ls+p.palette,native->ls+p.palette+(count+2)*48);
        transpose_geometry_skin_palette(*native,p);
        const unsigned source[]={0,5,10,3,1,6,8,7,2,4,9,11};
        for(unsigned i=0;i<count;++i)for(unsigned word=0;word<12;++word)
            assert(!memcmp(native->ls+p.palette+i*48+word*4,before.data()+i*48+source[word]*4,4));
        assert(!memcmp(native->ls+p.palette+count*48,before.data()+count*48,96));
    }
    // Early exits must not inspect unused palette, weights, output or dispatch data.
    for(unsigned early=0;early<3;++early){trial=0;seed(*native);if(!early)native->ls[0xe800]|=16;else if(early==1)native->gpr[5]._u32[0]=0;else native->ls[0xe806]=0;
        native->gpr[4]._u32[0]=~0u;native->gpr[6]._u32[0]=~0u;memset(native->ls+0xb554,0,76);GeometrySkinFlow p;assert(read_geometry_skin_flow(*native,p) && !p.work);}
    for(unsigned fault=0;fault<18;++fault){trial=0;seed(*native);switch(fault){
        case 0:native->ls=reference->ls_store;break;
        case 1:native->gpr[3]._u32[0]|=1;break;
        case 2:native->gpr[3]._u32[0]=0;break;
        case 3:native->gpr[1]._u32[0]|=1;break;
        case 4:native->gpr[1]._u32[0]=0xe680;break;
        case 5:native->gpr[1]._u32[0]=SPU_LS_SIZE-16;break;
        case 6:native->gpr[1]._u32[0]=0xe800+672;break;
        case 7:native->ls[0xe800]&=~128;break;
        case 8:native->gpr[5]._u32[0]=4097;break;
        case 9:native->gpr[4]._u32[0]=0xe800;break;
        case 10:native->gpr[4]._u32[0]=native->gpr[1]._u32[0]-64;break;
        case 11:native->ls[0xe808]=native->ls[0xe809]=0;break;
        case 12:geometry_job_put32(native->ls+0xe85c,0);break;
        case 13:geometry_job_put32(native->ls+0xe85c,0xe800);break;
        case 14:native->gpr[6]._u32[0]=SPU_LS_SIZE-16;break;
        case 15:native->ls[0xb554]^=1;break;
        case 16:native->ls[0xe809]=12;geometry_job_put32(native->ls+0xe810,0xe800);break;
        case 17:native->ls[0xe809]=12;geometry_job_put32(native->ls+0xe810,SPU_LS_SIZE-32);break;
    }
        auto* before=clone_function_state(native.get());GeometrySkinFlow p;calls.clear();g_spu_trampoline_fn=nullptr;
        assert(!read_geometry_skin_flow(*native,p) && compare_function_state(*native,*before).exact() && !g_spu_trampoline_fn && calls.empty());release_function_state(before);native->ls=native->ls_store;
    }
    assert(work && empty && weighted && rigid);for(auto n:directions)assert(n);
    printf("Native skin flow:6600 strict full-state/all-GPR call trace cases (work%u empty%u weighted%u rigid%u directions%u/%u/%u/%u/%u/%u);384 strict AF18/native-rigid integration cases (original rigid data oracle;cofactor/direction still models);768 strict AF18/actual-cofactor integration cases (original/native rigid backends,direction still models);3 independent numeric palette/padding cases;3 unused-input early exits;18 unchanged rejects PASS\n",work,empty,weighted,rigid,directions[0],directions[1],directions[2],directions[3],directions[4],directions[5]);
}
