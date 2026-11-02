#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_rigid_skin.h"
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
#include "../analysis/native-geometry/phase3/test-rigid-skin-reference.inc"
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0xb880;c.host_depth=trial%3;
    uint32_t state=trial+1;
    for(auto& reg:c.gpr)for(auto& word:reg._u32)word=random_word(state);
    for(unsigned i=0;i<SPU_LS_SIZE;++i)c.ls[i]=uint8_t(random_word(state));
    rigid_skin_oracle::seed_constants(c);
    const unsigned counts[]={2,4,6,8,10,12,14,16,18,30,32,34,62,64,66,128,256,512,1024,2048};
    c.gpr[84]._u32[0]=counts[(trial/16)%20];
    c.gpr[80]._u32[0]=0x17000+(trial&15);c.gpr[85]._u32[0]=0x12000+((trial/3)&15);
    c.gpr[96]._u32[0]=0x23000+((trial/7)&15);
    c.gpr[54]._u32[0]=0xe800+((trial/5)&15);c.gpr[53]._u32[0]=0xe810+((trial/11)&15);c.gpr[52]._u32[0]=0xe820+((trial/13)&15);
    if(trial&16){c.gpr[53]._u32[0]=0xea00+((trial/11)&15);c.gpr[52]._u32[0]=0xee00+((trial/13)&15);}
    c.gpr[102]._u32[0]=(trial&1)?0x10000:0x10001;
    c.int_enable=trial&1;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;c.policy_mode=trial%3;c.resident_ovl=trial%5;
    const unsigned count=c.gpr[84]._u32[0],weights=c.gpr[85]._u32[0]&~15u;

}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    GeometryRigidSkin p;const char* reason=nullptr;
    if(!read_geometry_rigid_skin(native,p,&reason)){fprintf(stderr,"unexpected reject%u %s\n",trial,reason);abort();}
    g_spu_trampoline_fn=nullptr;rigid_skin_oracle::buzz_job_6eba_spu_func_0000B348(&reference);
    for(unsigned steps=0;g_spu_trampoline_fn!=rigid_skin_oracle::buzz_job_6eba_spu_func_0000B4C0 && g_spu_trampoline_fn!=rigid_skin_oracle::buzz_job_6eba_spu_func_0000BB58;++steps){
        assert(g_spu_trampoline_fn && steps<4096);auto fn=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;fn(&reference);
    }
    const auto expected=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;
    transform_geometry_rigid_skin(native,p,rigid_skin_oracle::buzz_job_6eba_spu_func_0000B4C0,rigid_skin_oracle::buzz_job_6eba_spu_func_0000BB58);
    const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"rigid_skin%u count%u r%d byte%d LS%X/%u state%s\n",trial,p.count,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned r=0;r<128;++r)if(memcmp(&native.gpr[r],&reference.gpr[r],16)){
            fprintf(stderr,"r%u native",r);for(auto w:native.gpr[r]._u32)fprintf(stderr," %08x",w);
            fprintf(stderr," ref");for(auto w:reference.gpr[r]._u32)fprintf(stderr," %08x",w);fputc('\n',stderr);
        }
        abort();
    }
    assert(g_spu_trampoline_fn==expected && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
    assert(native.pc==((trial&1)?0xbb58:0xb4c0));
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    for(unsigned trial=0;trial<6400;++trial){
        seed(*native,trial);memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;
        compare(*native,*reference,trial);
    }
    unsigned alias_candidates=0;
    for(unsigned variant=0;variant<12;++variant)for(unsigned trial=0;trial<64;++trial){
        seed(*native,trial);native->gpr[84]._u32[0]=trial&2?32:8;
        const unsigned point=native->gpr[80]._u32[0],matrix=native->gpr[96]._u32[0],indices=native->gpr[85]._u32[0];
        switch(variant){
        case 0:native->gpr[96]._u32[0]=point;break;
        case 1:native->gpr[96]._u32[0]=point+16;break;
        case 2:native->gpr[80]._u32[0]=matrix+32;break;
        case 3:native->gpr[85]._u32[0]=point;break;
        case 4:native->gpr[85]._u32[0]=point+16;break;
        case 5:native->gpr[85]._u32[0]=matrix;break;
        case 6:native->gpr[85]._u32[0]=matrix+96;break;
        case 7:native->gpr[54]._u32[0]=point;native->gpr[53]._u32[0]=point+16;native->gpr[52]._u32[0]=point+32;break;
        case 8:native->gpr[54]._u32[0]=matrix;native->gpr[53]._u32[0]=matrix+16;native->gpr[52]._u32[0]=matrix+32;break;
        case 9:native->gpr[80]._u32[0]=native->gpr[85]._u32[0]=matrix;native->gpr[54]._u32[0]=matrix+64;native->gpr[53]._u32[0]=matrix+80;native->gpr[52]._u32[0]=matrix+96;break;
        case 10:native->gpr[80]._u32[0]=indices+16;break;
        case 11:native->gpr[54]._u32[0]=SPU_LS_SIZE-16+(trial&15);native->gpr[53]._u32[0]=SPU_LS_SIZE-32+(trial&15);native->gpr[52]._u32[0]=SPU_LS_SIZE-48+(trial&15);break;
        }
        GeometryRigidSkin plan;assert(read_geometry_rigid_skin(*native,plan));
        if(variant<10)assert(plan.aliases);if(variant==11)assert(!plan.aliases);
        alias_candidates+=plan.aliases;
        memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;
        compare(*native,*reference,trial);
    }
    for(unsigned trial=0;trial<512;++trial){
        seed(*native,trial);native->gpr[84]._u32[0]=2+2*(trial%16);
        uint32_t state=trial+1234;const unsigned constants[]={38,39,43,51,55,77,81};
        for(unsigned reg:constants)for(auto& word:native->gpr[reg]._u32)word=random_word(state);
        memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;
        compare(*native,*reference,trial);
    }
    for(unsigned count:{2u,4u,64u}){
        seed(*native,0);native->gpr[84]=spu_il(count);native->gpr[80]=spu_ila(0x17000);native->gpr[85]=spu_ila(0x12000);
        native->gpr[96]=spu_ila(0x23000);native->gpr[54]=spu_ila(0xe800);native->gpr[53]=spu_ila(0xe810);native->gpr[52]=spu_ila(0xe820);
        memset(native->ls+0x12000,0,count+16);
        u128 row0{},row1{},row2{};row0._f32[0]=row0._f32[1]=row0._f32[2]=1;row0._f32[3]=10;row1._f32[3]=20;row2._f32[3]=30;
        spu_ls_write128(native.get(),0xe800,row0);spu_ls_write128(native.get(),0xe810,row1);spu_ls_write128(native.get(),0xe820,row2);
        for(unsigned v=0;v<count+2;++v){u128 point{};point._f32[0]=v+1;point._f32[1]=v+2;point._f32[2]=v+3;point._f32[3]=1;spu_ls_write128(native.get(),0x17000+v*16,point);}
        GeometryRigidSkin plan;assert(read_geometry_rigid_skin(*native,plan));transform_geometry_rigid_skin(*native,plan,rigid_skin_oracle::buzz_job_6eba_spu_func_0000B4C0,rigid_skin_oracle::buzz_job_6eba_spu_func_0000BB58);
        for(unsigned v=0;v<count;++v){const u128 result=spu_ls_read128(native.get(),0x17000+v*16);
            assert(result._f32[0]==v+11 && result._f32[1]==v+22 && result._f32[2]==v+33 && result._f32[3]==1);
            const u128 a=spu_ls_read128(native.get(),0x23000+v*48),b=spu_ls_read128(native.get(),0x23010+v*48),c=spu_ls_read128(native.get(),0x23020+v*48);
            for(auto word:a._u32)assert(word==0x3f800000);for(auto word:b._u32)assert(!word);for(auto word:c._u32)assert(!word);
        }
        for(unsigned v=count;v<count+2;++v){const u128 point=spu_ls_read128(native.get(),0x17000+v*16);assert(point._f32[0]==v+1 && point._f32[1]==v+2 && point._f32[2]==v+3 && point._f32[3]==1);}
        assert(native->gpr[80]._u32[0]==0x17000+count*16 && native->gpr[85]._u32[0]==0x12000+count);
    }
    // Accepted maximum count with overlapping position/index/palette/output
    // spans. This also stresses the longest original trampoline closure.
    seed(*native,0);native->gpr[84]._u32[0]=4096;native->gpr[80]._u32[0]=0xe800;native->gpr[96]._u32[0]=0xe680;
    memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;compare(*native,*reference,0);
    for(unsigned fault=0;fault<10;++fault){
        seed(*native,0);switch(fault){
        case 0:native->gpr[84]._u32[0]=0;break;
        case 1:native->gpr[84]._u32[0]=3;break;
        case 2:native->gpr[84]._u32[0]=4098;break;
        case 3:native->gpr[80]._u32[0]=0;break;
        case 4:native->gpr[80]._u32[0]=SPU_LS_SIZE-16;break;
        case 5:native->gpr[85]._u32[0]=SPU_LS_SIZE-1;break;
        case 6:native->gpr[96]._u32[0]=SPU_LS_SIZE-16;break;
        case 7:native->gpr[52]._u32[0]=SPU_LS_SIZE;break;
        case 8:native->gpr[53]._u32[0]=0xe67f;break;
        case 9:native->ls=reference->ls_store;break;
        }
        auto* before=clone_function_state(native.get());GeometryRigidSkin plan;g_spu_trampoline_fn=nullptr;
        assert(!read_geometry_rigid_skin(*native,plan) && compare_function_state(*native,*before).exact() && !g_spu_trampoline_fn);
        release_function_state(before);native->ls=native->ls_store;
    }
    assert(alias_candidates>640);
    printf("Native rigid skin:7681 strict full-state/boundary cases (6400 random;768 targeted alias/wrapped gathers incl%u potential alias candidates;512 arbitrary masks/shuffles);1 maximum4096-vertex alias case;3 independent translated-identity/padding cases;10 unchanged rejects PASS\n",alias_candidates);
}
