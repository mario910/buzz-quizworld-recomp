#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_skin_directions.h"
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
#include "../analysis/native-geometry/phase3/test-skin-directions-reference.inc"
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0xbd4c;c.host_depth=trial%3;
    uint32_t state=trial+1;for(auto& reg:c.gpr)for(auto& word:reg._u32)word=random_word(state);
    for(unsigned i=0;i<SPU_LS_SIZE;++i)c.ls[i]=uint8_t(random_word(state));
    const unsigned counts[]={4,8,12,16,20,28,32,36,60,64,68,128,256,512,1024,2048};
    c.gpr[84]._u32[0]=counts[(trial/16)%16];
    c.gpr[33]._u32[0]=0xe800+(trial&15);c.gpr[67]._u32[0]=0xe800+((trial/3)&15);
    c.gpr[71]._u32[0]=0x30000+((trial/5)&15);c.gpr[93]._u32[0]=0x34000+((trial/7)&15);
    c.gpr[40]._u32[0]=0x18000+((trial/11)&15);
    // Vary coherent standard shuffles and arbitrary SPU selector bytes.
    if(trial&1){c.gpr[38]=spu_ila(0x10203);c.gpr[43]._u32[0]=0x04050607;c.gpr[43]._u32[1]=0x08090a0b;c.gpr[43]._u32[2]=0x00010203;c.gpr[43]._u32[3]=0x0c0d0e0f;c.gpr[39]._u32[0]=0x08090a0b;c.gpr[39]._u32[1]=0x00010203;c.gpr[39]._u32[2]=0x04050607;c.gpr[39]._u32[3]=0x0c0d0e0f;}
    c.int_enable=trial&1;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;c.policy_mode=trial%3;c.resident_ovl=trial%5;
}
static void compare(spu_context& native,spu_context& reference,unsigned streams,unsigned trial)
{
    GeometrySkinDirections p;const char* why=nullptr;if(!read_geometry_skin_directions(native,streams,p,&why)){fprintf(stderr,"unexpected direction reject%u/%u %s\n",streams,trial,why);abort();}
    g_spu_trampoline_fn=nullptr;skin_directions_oracle::roots[streams-1](&reference);
    for(unsigned steps=0;g_spu_trampoline_fn!=skin_directions_oracle::buzz_job_6eba_spu_func_0000B868;++steps){assert(g_spu_trampoline_fn && steps<4096);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);}
    g_spu_trampoline_fn=nullptr;transform_geometry_skin_directions(native,p,skin_directions_oracle::buzz_job_6eba_spu_func_0000B868);
    const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"directions%u trial%u count%u r%d byte%d LS%X/%u state%s\n",streams,trial,p.count,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned r=0;r<128;++r)if(memcmp(&native.gpr[r],&reference.gpr[r],16)){
            fprintf(stderr,"r%u native",r);for(auto w:native.gpr[r]._u32)fprintf(stderr," %08x",w);fprintf(stderr," ref");for(auto w:reference.gpr[r]._u32)fprintf(stderr," %08x",w);fputc('\n',stderr);
        }abort();
    }
    assert(g_spu_trampoline_fn==skin_directions_oracle::buzz_job_6eba_spu_func_0000B868 && native.pc==0xb868 && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    unsigned aliases=0;
    for(unsigned streams=1;streams<=3;++streams)for(unsigned trial=0;trial<2048;++trial){
        seed(*native,trial);memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;
        compare(*native,*reference,streams,trial);
    }
    for(unsigned streams=1;streams<=3;++streams)for(unsigned trial=0;trial<256;++trial){
        seed(*native,trial);native->gpr[84]._u32[0]=trial&1?8:32;
        native->gpr[71]._u32[0]=0xe800+(trial&15);native->gpr[93]._u32[0]=0xe820+((trial/7)&15);
        if(trial&2)native->gpr[40]._u32[0]=0xe800+((trial/11)&15);
        if(trial&4)native->gpr[67]._u32[0]=0x11000+((trial/3)&15);
        GeometrySkinDirections plan;assert(read_geometry_skin_directions(*native,streams,plan));aliases+=plan.aliases;
        memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;
        compare(*native,*reference,streams,trial);
    }
    for(unsigned streams=1;streams<=3;++streams){
        seed(*native,1);native->gpr[84]._u32[0]=8;
        const unsigned xregs[]={37,36,35,34},mregs[4][3]={{58,57,56},{50,49,48},{47,46,45},{44,42,41}};
        u128 one{},zero{};for(auto& value:one._f32)value=1.0f;
        for(unsigned v=0;v<4;++v){
            for(auto& value:native->gpr[xregs[v]]._f32)value=float(70+v);
            for(unsigned row=0;row<3;++row)native->gpr[mregs[v][row]]=row?zero:one;
        }
        for(unsigned v=0;v<8;++v){
            for(unsigned row=0;row<3;++row)spu_ls_write128(native.get(),0x18000+v*48+row*16,row?zero:one);
            for(unsigned stream=0;stream<streams;++stream){u128 data;for(auto& value:data._f32)value=float(20+stream*10+v);spu_ls_write128(native.get(),native->gpr[stream==0?33:stream==1?71:93]._u32[0]+v*16,data);}
        }
        GeometrySkinDirections p;assert(read_geometry_skin_directions(*native,streams,p));
        transform_geometry_skin_directions(*native,p,skin_directions_oracle::buzz_job_6eba_spu_func_0000B868);
        for(unsigned stream=0;stream<streams;++stream)for(unsigned v=0;v<8;++v){
            const unsigned base=stream==0?0xe800:stream==1?0x30000:0x34000;
            const auto actual=spu_ls_read128(native.get(),base+v*16);
            const float expected=stream==0 && v<4?float(70+v):float(20+stream*10+v);
            for(auto value:actual._f32)assert(value==expected);
        }
    }
    for(unsigned fault=0;fault<12;++fault){
        seed(*native,0);native->gpr[84]._u32[0]=8;unsigned streams=3;
        switch(fault){case 0:native->gpr[84]._u32[0]=0;break;case 1:native->gpr[84]._u32[0]=6;break;case 2:native->gpr[84]._u32[0]=4100;break;case 3:streams=0;break;case 4:streams=4;break;case 5:native->gpr[33]._u32[0]=0;break;case 6:native->gpr[33]._u32[0]=SPU_LS_SIZE-64;break;case 7:native->gpr[67]._u32[0]=SPU_LS_SIZE-64;break;case 8:native->gpr[40]._u32[0]=SPU_LS_SIZE-16;break;case 9:native->gpr[71]._u32[0]=SPU_LS_SIZE-16;break;case 10:native->gpr[93]._u32[0]=SPU_LS_SIZE-16;break;case 11:native->ls=reference->ls_store;break;}
        auto* before=clone_function_state(native.get());GeometrySkinDirections p;g_spu_trampoline_fn=nullptr;
        assert(!read_geometry_skin_directions(*native,streams,p) && compare_function_state(*native,*before).exact() && !g_spu_trampoline_fn);release_function_state(before);native->ls=native->ls_store;
    }
    printf("Native skin directions:6912 strict full-state/boundary cases (1/2/3 streams,4..2048 vertices, cached input, arbitrary FP/shuffle bits, alias cases%u) +3 independent identity/cache cases +12 unchanged/no-I/O rejects PASS\n",aliases);
}
