#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_vertex_prepare.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*){abort();}
void spu_stop(spu_context*){abort();}
}
#include "../analysis/native-geometry/phase3/test-vertex-prepare-reference.inc"
static void vertex_boundary(spu_context*){abort();}
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x6188;uint32_t state=trial+1;
    for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word(state);
    for(unsigned at=0;at<SPU_LS_SIZE;at+=4){const auto w=random_word(state);memcpy(c.ls+at,&w,4);}
    c.gpr[3]._u32[0]=0x39000+(trial&15);c.ls[0x39140]=trial&255;
    c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;
    c.int_enable=trial&1;c.event_mask=trial*13;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;
}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    GeometryVertexPrepare p;assert(read_geometry_vertex_prepare(native,p));
    g_spu_trampoline_fn=spu_indirect_branch;vertex_prepare_oracle::buzz_job_6eba_spu_func_00006188(&reference);
    for(unsigned steps=0;g_spu_trampoline_fn!=vertex_prepare_oracle::buzz_job_6eba_spu_func_000063E0;++steps){
        assert(steps<2 && (g_spu_trampoline_fn==vertex_prepare_oracle::buzz_job_6eba_spu_func_000065EC || g_spu_trampoline_fn==vertex_prepare_oracle::buzz_job_6eba_spu_func_000065FC));
        const auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);
    }
    g_spu_trampoline_fn=spu_indirect_branch;prepare_geometry_vertices(native,p,vertex_boundary);
    const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"prepare trial%u path%u r%d byte%d LS%X/%u state%s\n",trial,p.path,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned reg=0;reg<128;++reg)if(memcmp(&native.gpr[reg],&reference.gpr[reg],16)){fprintf(stderr,"r%u native",reg);for(auto w:native.gpr[reg]._u32)fprintf(stderr," %08x",w);fprintf(stderr," ref");for(auto w:reference.gpr[reg]._u32)fprintf(stderr," %08x",w);fputc('\n',stderr);}abort();
    }
    assert(g_spu_trampoline_fn==vertex_boundary && native.pc==0x63e0 && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
}
static u128 vector(float a,float b,float c,float d){u128 v{};v._f32[0]=a;v._f32[1]=b;v._f32[2]=c;v._f32[3]=d;return v;}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();unsigned exact=0;
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial);copy(*reference,*native);compare(*native,*reference,trial);++exact;}
    for(unsigned format=0;format<256;++format)for(unsigned bias=0;bias<16;++bias){seed(*native,format*16+bias);
        const unsigned manager=format&1?0xe680:SPU_LS_SIZE-0x150;
        native->gpr[3]._u32[0]=manager+bias;native->ls[manager+0x140]=format;
        copy(*reference,*native);compare(*native,*reference,10000+format*16+bias);++exact;
    }
    const uint32_t special[]={0,0x80000000,1,0x807fffff,0x00800000,0x80800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc00001,0xffc00001,0x7fffffff,0xffffffff};
    const unsigned offsets[]={0xa0,0xb0,0xc0,0xd0,0xe0,0xf0,0x100,0x110,0x120,0x130};
    for(unsigned trial=0;trial<256;++trial){seed(*native,trial);const unsigned manager=native->gpr[3]._u32[0]&~15u;
        for(unsigned i=0;i<10;++i){u128 v{};for(unsigned lane=0;lane<4;++lane)v._u32[lane]=special[(trial+i+lane*3)&15];spu_ls_write128(native.get(),manager+offsets[i],v);}
        u128 v{};for(unsigned lane=0;lane<4;++lane)v._u32[lane]=special[(trial+lane*5)&15];spu_ls_write128(native.get(),0xcd60,v);
        copy(*reference,*native);compare(*native,*reference,20000+trial);++exact;
    }
    seed(*native,0);FILE* fixture=fopen("analysis/native-geometry/phase3/transform-audit-v1/pc_6188.bin","rb");assert(fixture);
    assert(fread(native->gpr,sizeof native->gpr,1,fixture)==1 && fread(native->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
    copy(*reference,*native);compare(*native,*reference,30000);++exact;
    // Independent identity-matrix geometry: exact integer values need no FP
    // tolerance. Verify both complete columns and every broadcast of column Y.
    for(unsigned shape=0;shape<4;++shape)for(unsigned bias=0;bias<16;++bias){seed(*native,bias);const unsigned manager=native->gpr[3]._u32[0]&~15u;
        const float k=float(shape+1);const u128 origin=vector(k,2*k,3*k,1);
        spu_ls_write128(native.get(),manager+0x110,origin);spu_ls_write128(native.get(),manager+0x130,vector(4,2,3,0));spu_ls_write128(native.get(),manager+0x120,vector(6,4,5,0));
        const unsigned positions[]={0xf0,0x100,0xe0,0xa0,0xb0,0xc0};
        for(unsigned i=0;i<6;++i){u128 unit{};unit._f32[i%3]=1;spu_ls_write128(native.get(),manager+positions[i],unit);}
        spu_ls_write128(native.get(),0xcd60,vector(0,0,0,1));copy(*reference,*native);compare(*native,*reference,40000+shape*16+bias);++exact;
        const u128 x=vector(2*k+4,4*k,6*k,2),z=vector(4*k,8*k,12*k+6,4);
        assert(!memcmp(&native->gpr[28],&x,16) && !memcmp(&native->gpr[36],&z,16) && !memcmp(&native->gpr[40],&origin,16));
        const unsigned regs[]={60,55,52,62};const float values[]={3*k,6*k+5,9*k,3};
        for(unsigned i=0;i<4;++i)for(float lane:native->gpr[regs[i]]._f32)assert(lane==values[i]);
    }
    for(unsigned trial=0;trial<6;++trial){seed(*native,0);switch(trial){case 0:native->ls=reference->ls_store;break;
        case 1:native->int_enable=1;native->event_status=native->event_mask=1;break;case 2:native->gpr[3]._u32[0]=0;break;
        case 3:native->gpr[3]._u32[0]=0xe670;break;case 4:native->gpr[3]._u32[0]=SPU_LS_SIZE;break;case 5:native->gpr[3]._u32[0]=SPU_LS_SIZE-0x140;break;}
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryVertexPrepare p;p.path=77;const char* why=nullptr;
        assert(!read_geometry_vertex_prepare(*native,p,&why) && why && p.path==77 && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    printf("PASS vertex prepare: %u strict full-state/LS/handoff;256 formats x16 biases;256 special FP;64 independent matrix cases;6 unchanged rejects\n",exact);
}
