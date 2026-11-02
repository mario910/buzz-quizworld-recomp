#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_skin_normalized.h"
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
#include "../analysis/native-geometry/phase3/test-skin-normalized-reference.inc"
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0xbd4c;c.host_depth=trial%3;
    uint32_t state=trial+1;for(auto& reg:c.gpr)for(auto& word:reg._u32)word=random_word(state);
    for(unsigned i=0;i<SPU_LS_SIZE;++i)c.ls[i]=uint8_t(random_word(state));
    if(trial&1)skin_normalized_oracle::seed_constants(c);
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
    GeometrySkinNormalized p;const char* why=nullptr;if(!read_geometry_skin_normalized(native,streams,p,&why)){fprintf(stderr,"unexpected direction reject%u/%u %s\n",streams,trial,why);abort();}
    g_spu_trampoline_fn=nullptr;skin_normalized_oracle::roots[streams-1](&reference);
    for(unsigned steps=0;g_spu_trampoline_fn!=skin_normalized_oracle::buzz_job_6eba_spu_func_0000B868;++steps){assert(g_spu_trampoline_fn && steps<4096);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);}
    g_spu_trampoline_fn=nullptr;transform_geometry_skin_normalized(native,p,skin_normalized_oracle::buzz_job_6eba_spu_func_0000B868);
    const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"directions%u trial%u count%u r%d byte%d LS%X/%u state%s\n",streams,trial,p.count,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned r=0;r<128;++r)if(memcmp(&native.gpr[r],&reference.gpr[r],16)){
            fprintf(stderr,"r%u native",r);for(auto w:native.gpr[r]._u32)fprintf(stderr," %08x",w);fprintf(stderr," ref");for(auto w:reference.gpr[r]._u32)fprintf(stderr," %08x",w);fputc('\n',stderr);
        }abort();
    }
    assert(g_spu_trampoline_fn==skin_normalized_oracle::buzz_job_6eba_spu_func_0000B868 && native.pc==0xb868 && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
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
    for(unsigned streams=1;streams<=3;++streams)for(unsigned variant=0;variant<12;++variant)for(unsigned trial=0;trial<64;++trial){
        seed(*native,trial);native->gpr[84]._u32[0]=trial&1?8:32;const unsigned first=native->gpr[33]._u32[0],matrix=native->gpr[40]._u32[0],secondary=native->gpr[71]._u32[0];
        switch(variant){
        case 0:native->gpr[71]._u32[0]=first;break;
        case 1:native->gpr[93]._u32[0]=secondary+16;break;
        case 2:native->gpr[40]._u32[0]=first;break;
        case 3:native->gpr[33]._u32[0]=native->gpr[67]._u32[0]+64;break;
        case 4:native->gpr[67]._u32[0]=first+64;break;
        case 5:native->gpr[71]._u32[0]=matrix+192;break;
        case 6:native->gpr[93]._u32[0]=matrix+16;break;
        case 7:native->gpr[40]._u32[0]=SPU_LS_SIZE-16+(trial&15);break;
        case 8:native->gpr[33]._u32[0]=SPU_LS_SIZE-64+(trial&15);break;
        case 9:native->gpr[71]._u32[0]=SPU_LS_SIZE-512+(trial&15);break;
        case 10:native->gpr[33]._u32[0]=secondary;native->gpr[71]._u32[0]=secondary+16;native->gpr[93]._u32[0]=secondary+32;break;
        case 11:native->gpr[33]._u32[0]=native->gpr[67]._u32[0]=native->gpr[71]._u32[0]=native->gpr[93]._u32[0]=matrix+64;break;
        }
        GeometrySkinNormalized plan;assert(read_geometry_skin_normalized(*native,streams,plan));
        if(variant==2 || variant==3 || variant==11)assert(plan.aliases);if(variant==7 || variant==8)assert(!plan.aliases);aliases+=plan.aliases;
        memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;compare(*native,*reference,streams,trial);
    }
    const unsigned xregs[]={37,36,35,34},yregs[]={65,61,64,59},zregs[]={66,63,62,60},mregs[4][3]={{58,57,56},{50,49,48},{47,46,45},{44,42,41}};
    const uint32_t special[]={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fffffff,0xffffffff};
    for(unsigned streams=1;streams<=3;++streams)for(unsigned trial=0;trial<256;++trial){
        seed(*native,trial);native->gpr[84]._u32[0]=4*(1+trial%8);
        for(unsigned v=0;v<4;++v)for(unsigned lane=0;lane<4;++lane){
            native->gpr[xregs[v]]._u32[lane]=special[(v+lane+trial)%16];native->gpr[yregs[v]]._u32[lane]=special[(v+lane+trial+3)%16];native->gpr[zregs[v]]._u32[lane]=special[(v+lane+trial+7)%16];
            for(unsigned row=0;row<3;++row)native->gpr[mregs[v][row]]._u32[lane]=special[(v+row*3+lane+trial/16)%16];
        }
        for(unsigned v=0;v<native->gpr[84]._u32[0];++v){u128 data;for(unsigned lane=0;lane<4;++lane)data._u32[lane]=special[(v+lane+trial)%16];
            for(unsigned stream=0;stream<streams;++stream)spu_ls_write128(native.get(),native->gpr[stream==0?33:stream==1?71:93]._u32[0]+v*16,data);
            for(unsigned row=0;row<3;++row){u128 matrix;for(unsigned lane=0;lane<4;++lane)matrix._u32[lane]=special[(v+row+lane+trial/16)%16];spu_ls_write128(native.get(),native->gpr[40]._u32[0]+v*48+row*16,matrix);}}
        memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;compare(*native,*reference,streams,trial);
    }
    for(unsigned streams=1;streams<=3;++streams){
        seed(*native,0);native->gpr[84]._u32[0]=4096;native->gpr[33]._u32[0]=0xe800;native->gpr[67]._u32[0]=0xf000;native->gpr[71]._u32[0]=0x20000;native->gpr[93]._u32[0]=0x28000;native->gpr[40]._u32[0]=SPU_LS_SIZE-16;
        memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;compare(*native,*reference,streams,0);
    }
    for(unsigned streams=1;streams<=3;++streams)for(unsigned count:{4u,8u,64u}){
        seed(*native,1);native->gpr[84]=spu_il(count);native->gpr[33]=native->gpr[67]=spu_ila(0xe800);native->gpr[71]=spu_ila(0x30000);native->gpr[93]=spu_ila(0x34000);native->gpr[40]=spu_ila(0x18000);
        const int points[4][3]={{1,2,3},{-3,5,7},{0,-1,2},{1,0,0}};u128 diag{},zero{};diag._f32[0]=2;diag._f32[1]=3;diag._f32[2]=4;
        for(unsigned v=0;v<4;++v){u128 cached{};for(unsigned axis=0;axis<3;++axis)cached._f32[axis]=points[v%4][axis]+(axis==0?5:0);
            native->gpr[xregs[v]]=cached;native->gpr[yregs[v]]=spu_shufb(cached,native->gpr[38],native->gpr[43]);native->gpr[zregs[v]]=spu_shufb(cached,native->gpr[38],native->gpr[39]);for(unsigned row=0;row<3;++row)native->gpr[mregs[v][row]]=row?zero:diag;}
        for(unsigned v=0;v<count;++v){u128 point{};for(unsigned axis=0;axis<3;++axis)point._f32[axis]=points[v%4][axis];for(unsigned stream=0;stream<streams;++stream)spu_ls_write128(native.get(),native->gpr[stream==0?33:stream==1?71:93]._u32[0]+v*16,point);
            for(unsigned row=0;row<3;++row)spu_ls_write128(native.get(),0x18000+v*48+row*16,row?zero:diag);}
        uint8_t padding[3][64];const unsigned bases[]={0xe800,0x30000,0x34000};for(unsigned stream=0;stream<streams;++stream)memcpy(padding[stream],native->ls+bases[stream]+count*16,64);
        GeometrySkinNormalized plan;assert(read_geometry_skin_normalized(*native,streams,plan));transform_geometry_skin_normalized(*native,plan,skin_normalized_oracle::buzz_job_6eba_spu_func_0000B868);
        for(unsigned stream=0;stream<streams;++stream){for(unsigned v=0;v<count;++v){const double x=(points[v%4][0]+(!stream && v<4?5:0))*2.0,y=points[v%4][1]*3.0,z=points[v%4][2]*4.0,length=sqrt(x*x+y*y+z*z);const double expected[]={x/length,y/length,z/length};const u128 actual=spu_ls_read128(native.get(),bases[stream]+v*16);
                for(unsigned axis=0;axis<3;++axis)assert(fabs(actual._f32[axis]-expected[axis])<5e-4);assert(!actual._u32[3]);}
            assert(!memcmp(padding[stream],native->ls+bases[stream]+count*16,64));}
    }
    for(unsigned fault=0;fault<12;++fault){seed(*native,0);native->gpr[84]._u32[0]=8;unsigned streams=3;
        switch(fault){case 0:native->gpr[84]._u32[0]=0;break;case 1:native->gpr[84]._u32[0]=6;break;case 2:native->gpr[84]._u32[0]=4100;break;case 3:streams=0;break;case 4:streams=4;break;case 5:native->gpr[33]._u32[0]=0;break;case 6:native->gpr[33]._u32[0]=SPU_LS_SIZE-48;break;case 7:native->gpr[67]._u32[0]=SPU_LS_SIZE-64;break;case 8:native->gpr[40]._u32[0]=SPU_LS_SIZE;break;case 9:native->gpr[71]._u32[0]=SPU_LS_SIZE-16;break;case 10:native->gpr[93]._u32[0]=SPU_LS_SIZE-16;break;case 11:native->ls=reference->ls_store;break;}
        auto* before=clone_function_state(native.get());GeometrySkinNormalized plan;g_spu_trampoline_fn=nullptr;assert(!read_geometry_skin_normalized(*native,streams,plan) && compare_function_state(*native,*before).exact() && !g_spu_trampoline_fn);release_function_state(before);native->ls=native->ls_store;
    }
    printf("Native normalized skin:9219 strict full-state/boundary cases (6144random,2304alias/wrap incl%u overlaps,768specialFP,3maximum4096 cases);9 independent geometry/cache/padding cases(tolerance5e-4);12 unchanged rejects PASS\n",aliases);
}
