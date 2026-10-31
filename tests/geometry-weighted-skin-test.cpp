#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_weighted_skin.h"
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
#include "../analysis/native-geometry/phase3/test-weighted-skin-reference.inc"
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0xb880;c.host_depth=trial%3;
    uint32_t state=trial+1;
    for(auto& reg:c.gpr)for(auto& word:reg._u32)word=random_word(state);
    for(unsigned i=0;i<SPU_LS_SIZE;++i)c.ls[i]=uint8_t(random_word(state));
    weighted_skin_oracle::seed_constants(c);
    const unsigned counts[]={2,4,6,8,10,12,14,16,18,30,32,34,62,64,66,128,256,512,1024,2048};
    c.gpr[84]._u32[0]=counts[(trial/16)%20];
    c.gpr[80]._u32[0]=0x17000+(trial&15);c.gpr[85]._u32[0]=0x12000+((trial/3)&15);
    c.gpr[96]._u32[0]=0x23000+((trial/7)&15);
    c.gpr[54]._u32[0]=0xe800+((trial/5)&15);c.gpr[53]._u32[0]=0xe810+((trial/11)&15);c.gpr[52]._u32[0]=0xe820+((trial/13)&15);
    if(trial&16){c.gpr[53]._u32[0]=0xea00+((trial/11)&15);c.gpr[52]._u32[0]=0xee00+((trial/13)&15);}
    c.gpr[102]._u32[0]=(trial&1)?0x10000:0x10001;
    c.int_enable=trial&1;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;c.policy_mode=trial%3;c.resident_ovl=trial%5;
    const unsigned count=c.gpr[84]._u32[0],weights=c.gpr[85]._u32[0]&~15u;
    // Each sparse influence position, all-zero, dense, and non-normalized
    // byte weights. Include all 256 indices even when their weights are zero.
    if(trial%8<5)for(unsigned v=0;v<count;++v)for(unsigned j=0;j<4;++j)
        c.ls[weights+v*8+j*2]=trial%8==j+1?255:0;
}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    GeometryWeightedSkin p;const char* reason=nullptr;
    if(!read_geometry_weighted_skin(native,p,&reason)){fprintf(stderr,"unexpected reject%u %s\n",trial,reason);abort();}
    g_spu_trampoline_fn=nullptr;weighted_skin_oracle::buzz_job_6eba_spu_func_0000B880(&reference);
    for(unsigned steps=0;g_spu_trampoline_fn!=weighted_skin_oracle::buzz_job_6eba_spu_func_0000B4C0 && g_spu_trampoline_fn!=weighted_skin_oracle::buzz_job_6eba_spu_func_0000BB58;++steps){
        assert(g_spu_trampoline_fn && steps<4096);auto fn=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;fn(&reference);
    }
    const auto expected=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;
    transform_geometry_weighted_skin(native,p,weighted_skin_oracle::buzz_job_6eba_spu_func_0000B4C0,weighted_skin_oracle::buzz_job_6eba_spu_func_0000BB58);
    const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"weighted_skin%u count%u r%d byte%d LS%X/%u state%s\n",trial,p.count,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
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
    for(unsigned trial=0;trial<256;++trial){
        seed(*native,trial);native->gpr[84]._u32[0]=2;
        native->gpr[80]._u32[0]=0xe890+(trial&15);
        const unsigned weights=native->gpr[85]._u32[0]&~15u;
        for(unsigned i=0;i<8;++i)native->ls[weights+i*2+1]=i&1?255:0;
        memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;
        compare(*native,*reference,trial);
    }
    for(unsigned trial=0;trial<256;++trial){
        seed(*native,trial);native->gpr[84]._u32[0]=trial&1?8:32;
        const unsigned position=native->gpr[80]._u32[0]&~15u,destination=native->gpr[96]._u32[0]&~15u;
        const unsigned base=trial&2?position:destination;
        native->gpr[54]._u32[0]=base;native->gpr[53]._u32[0]=base+16;native->gpr[52]._u32[0]=base+32;
        const unsigned weights=native->gpr[85]._u32[0]&~15u;
        for(unsigned i=0;i<native->gpr[84]._u32[0]*4;++i){native->ls[weights+i*2]=uint8_t(1+(i*37+trial)%255);native->ls[weights+i*2+1]=uint8_t(i%3);}
        GeometryWeightedSkin plan;assert(read_geometry_weighted_skin(*native,plan) && plan.palette_alias);
        memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;
        compare(*native,*reference,trial);
    }
    for(unsigned fault=0;fault<12;++fault){
        seed(*native,0);switch(fault){
        case 0:native->gpr[84]._u32[0]=0;break;
        case 1:native->gpr[84]._u32[0]=3;break;
        case 2:native->gpr[84]._u32[0]=4098;break;
        case 3:native->gpr[80]._u32[0]=0;break;
        case 4:native->gpr[80]._u32[0]=SPU_LS_SIZE-16;break;
        case 5:native->gpr[85]._u32[0]=SPU_LS_SIZE-16;break;
        case 6:native->gpr[96]._u32[0]=SPU_LS_SIZE-16;break;
        case 7:native->gpr[52]._u32[0]=SPU_LS_SIZE-16;break;
        case 8:native->gpr[96]=native->gpr[80];break;
        case 9:native->gpr[80]=native->gpr[85];break;
        case 10:native->gpr[90]._u32[2]^=1;break;
        case 11:native->ls=reference->ls_store;break;
        }
        auto* before=clone_function_state(native.get());GeometryWeightedSkin p;g_spu_trampoline_fn=nullptr;
        assert(!read_geometry_weighted_skin(*native,p) && compare_function_state(*native,*before).exact() && !g_spu_trampoline_fn);
        release_function_state(before);native->ls=native->ls_store;
    }
    puts("Native complete weighted skin:6912 strict full-state/boundary cases (2..2048 vertices, all pointer biases, sparse/dense weights, arbitrary FP bits, sparse palette gaps, ordered nonzero palette aliases) +12 unchanged/no-I/O rejects PASS");
}
