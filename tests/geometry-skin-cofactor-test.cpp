#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_skin_cofactor.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <cmath>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*){abort();}
void spu_stop(spu_context*){abort();}
}
#include "../analysis/native-geometry/phase3/test-skin-cofactor-reference.inc"
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0xbb58;c.host_depth=trial%3;
    uint32_t state=trial+1;for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word(state);
    for(unsigned i=0;i<SPU_LS_SIZE;++i)c.ls[i]=uint8_t(random_word(state));
    skin_cofactor_oracle::seed_constants(c);
    const unsigned counts[]={4,8,12,16,20,28,32,36,64,68,128,256,512,1024,2048,4096};
    c.gpr[84]._u32[0]=counts[(trial/16)%16];c.gpr[83]._u32[0]=0x20000+(trial&15);c.gpr[96]._u32[0]=0xe800+((trial/3)&15);
    c.int_enable=trial&1;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;c.policy_mode=trial%3;c.resident_ovl=trial%5;
}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    GeometrySkinCofactor plan;const char* reason=nullptr;
    if(!read_geometry_skin_cofactor(native,plan,&reason)){fprintf(stderr,"unexpected reject%u %s\n",trial,reason);abort();}
    const auto tail=skin_cofactor_oracle::buzz_job_6eba_spu_func_0000B4C0;
    g_spu_trampoline_fn=nullptr;skin_cofactor_oracle::buzz_job_6eba_spu_func_0000BB58(&reference);
    for(unsigned steps=0;g_spu_trampoline_fn!=tail;++steps){assert(steps<4 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);}
    g_spu_trampoline_fn=nullptr;transform_geometry_skin_cofactor(native,plan,tail);
    const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"skin_cofactor%u count%u r%d byte%d LS%X/%u state%s\n",trial,plan.count,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned reg=0;reg<128;++reg)if(memcmp(&native.gpr[reg],&reference.gpr[reg],16)){
            fprintf(stderr,"r%u native",reg);for(auto word:native.gpr[reg]._u32)fprintf(stderr," %08x",word);fprintf(stderr," ref");for(auto word:reference.gpr[reg]._u32)fprintf(stderr," %08x",word);fputc('\n',stderr);
        }
        abort();
    }
    assert(native.pc==0xb4c0 && g_spu_trampoline_fn==tail && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    for(unsigned trial=0;trial<6400;++trial){seed(*native,trial);memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;compare(*native,*reference,trial);}
    unsigned aliases=0;
    for(unsigned variant=0;variant<10;++variant)for(unsigned trial=0;trial<64;++trial){
        seed(*native,trial);native->gpr[84]._u32[0]=trial&2?16:8;const unsigned matrix=native->gpr[96]._u32[0],normal=native->gpr[83]._u32[0];
        switch(variant){
        case 0:native->gpr[83]._u32[0]=matrix;break;
        case 1:native->gpr[83]._u32[0]=matrix+16;break;
        case 2:native->gpr[83]._u32[0]=matrix+32;break;
        case 3:native->gpr[83]._u32[0]=matrix+48;break;
        case 4:native->gpr[83]._u32[0]=matrix+192;break;
        case 5:native->gpr[96]._u32[0]=normal+16;break;
        case 6:native->gpr[96]._u32[0]=normal-64;break;
        case 7:native->gpr[96]._u32[0]=SPU_LS_SIZE-32+(trial&15);break;
        case 8:native->gpr[96]._u32[0]=SPU_LS_SIZE-512+(trial&15);native->gpr[83]._u32[0]=SPU_LS_SIZE-256+(trial&15);break;
        case 9:native->gpr[96]._u32[0]=native->gpr[83]._u32[0]=0xe680+(trial&15);break;
        }
        GeometrySkinCofactor plan;assert(read_geometry_skin_cofactor(*native,plan));assert(plan.aliases==(variant!=7));aliases+=plan.aliases;
        memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;compare(*native,*reference,trial);
    }
    const unsigned constants[]={38,39,43,55,70,72,73,74,75,76,78,79,91,92};
    for(unsigned trial=0;trial<512;++trial){
        seed(*native,trial);native->gpr[84]._u32[0]=4*(1+trial%16);uint32_t state=trial+1234;
        for(unsigned reg:constants)for(auto& word:native->gpr[reg]._u32)word=random_word(state);
        memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;compare(*native,*reference,trial);
    }
    const uint32_t special[]={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fffffff,0xffffffff};
    for(unsigned trial=0;trial<256;++trial){
        seed(*native,trial);const unsigned count=4*(1+trial%8);native->gpr[84]._u32[0]=count;
        for(unsigned i=0;i<count;++i){u128 normal;for(unsigned lane=0;lane<4;++lane)normal._u32[lane]=special[(i+lane+trial)%16];spu_ls_write128(native.get(),native->gpr[83]._u32[0]+i*16,normal);
            for(unsigned row=0;row<3;++row){u128 matrix;for(unsigned lane=0;lane<4;++lane)matrix._u32[lane]=special[(i+row*3+lane+trial/16)%16];spu_ls_write128(native.get(),native->gpr[96]._u32[0]+i*48+row*16,matrix);}}
        memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;compare(*native,*reference,trial);
    }
    // Independent geometric check: inverse-transpose of diag(2,3,4).
    // SPU's approximate reciprocal root is checked to its numeric tolerance;
    // all strict cases above retain full byte-exact oracle comparison.
    for(unsigned count:{4u,8u,64u}){
        seed(*native,0);native->gpr[84]=spu_il(count);native->gpr[83]=spu_ila(0x20000);native->gpr[96]=spu_ila(0xe800);
        const int points[4][3]={{1,2,3},{-3,5,7},{0,-1,2},{1,0,0}};
        for(unsigned v=0;v<count;++v){u128 point{},a{},zero{};for(unsigned axis=0;axis<3;++axis)point._f32[axis]=points[v%4][axis];a._f32[0]=2;a._f32[1]=3;a._f32[2]=4;
            spu_ls_write128(native.get(),0x20000+v*16,point);spu_ls_write128(native.get(),0xe800+v*48,a);spu_ls_write128(native.get(),0xe810+v*48,zero);spu_ls_write128(native.get(),0xe820+v*48,zero);}
        uint8_t padding[64];memcpy(padding,native->ls+0x20000+count*16,64);GeometrySkinCofactor plan;assert(read_geometry_skin_cofactor(*native,plan));
        transform_geometry_skin_cofactor(*native,plan,skin_cofactor_oracle::buzz_job_6eba_spu_func_0000B4C0);
        for(unsigned v=0;v<count;++v){const double x=points[v%4][0]/2.0,y=points[v%4][1]/3.0,z=points[v%4][2]/4.0,length=sqrt(x*x+y*y+z*z);const double expected[]={x/length,y/length,z/length};const u128 result=spu_ls_read128(native.get(),0x20000+v*16);
            for(unsigned axis=0;axis<3;++axis)assert(fabs(result._f32[axis]-expected[axis])<5e-4);assert(!result._u32[3]);
            const u128 a=spu_ls_read128(native.get(),0xe800+v*48),b=spu_ls_read128(native.get(),0xe810+v*48),c=spu_ls_read128(native.get(),0xe820+v*48);
            assert(a._f32[0]==2 && a._f32[1]==3 && a._f32[2]==4 && !a._u32[3]);for(auto word:b._u32)assert(!word);for(auto word:c._u32)assert(!word);
        }
        assert(!memcmp(padding,native->ls+0x20000+count*16,64) && native->gpr[83]._u32[0]==0x20000+count*16 && native->gpr[40]._u32[0]==0xe800+count*48 && !native->gpr[41]._u32[0]);
    }
    for(unsigned fault=0;fault<10;++fault){seed(*native,0);switch(fault){
        case 0:native->gpr[84]._u32[0]=0;break;
        case 1:native->gpr[84]._u32[0]=2;break;
        case 2:native->gpr[84]._u32[0]=4100;break;
        case 3:native->gpr[83]._u32[0]=0;break;
        case 4:native->gpr[83]._u32[0]=SPU_LS_SIZE-16;break;
        case 5:native->gpr[96]._u32[0]=SPU_LS_SIZE;break;
        case 6:native->gpr[96]._u32[0]=0xe67f;break;
        case 7:native->gpr[83]._u32[0]=SPU_LS_SIZE;break;
        case 8:native->ls=reference->ls_store;break;
        case 9:native->gpr[83]._u32[0]=0xe67f;break;}
        auto* before=clone_function_state(native.get());GeometrySkinCofactor plan;g_spu_trampoline_fn=nullptr;
        assert(!read_geometry_skin_cofactor(*native,plan) && compare_function_state(*native,*before).exact() && !g_spu_trampoline_fn);release_function_state(before);native->ls=native->ls_store;
    }
    printf("Native cofactor skin:7808 strict full-state/boundary cases (6400random,640alias/wrap incl%u aliases,512arbitrary masks/shuffles,256special FP);3 independent inverse-transpose/padding cases(tolerance5e-4);10 unchanged rejects PASS\n",aliases);
}
