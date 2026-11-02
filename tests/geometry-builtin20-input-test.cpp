#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_builtin20_input.h"
#include "../port/native_geometry_context.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
namespace builtin20_input_control_oracle {static void dispatch(spu_context*);}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* c){builtin20_input_control_oracle::dispatch(c);}
void spu_stop(spu_context*){abort();}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_drain_call(spu_context*,uint32_t){abort();}
}
#include "../analysis/native-geometry/phase3/test-builtin20-input-reference.inc"
#include "../analysis/native-geometry/phase3/test-builtin20-input-control-reference.inc"
static unsigned strict_cases=0,seed_word=1,models=0,full_cases=0,child_cases=0;
static unsigned random_word(){seed_word^=seed_word<<13;seed_word^=seed_word>>17;seed_word^=seed_word<<5;return seed_word;}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void equal(const spu_context& a,const spu_context& b,const char* label,unsigned trial){
    const auto d=compare_function_state(a,b);if(!d.exact()){fprintf(stderr,"builtin20 input %s trial%u r%d byte%d LS%X/%u state%s PC%X/%X\n",label,trial,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal",a.pc,b.pc);abort();}
    assert(a.host_depth==b.host_depth && a.image_id==b.image_id && a.policy_mode==b.policy_mode && a.resident_ovl==b.resident_ovl);
}
// Independent integer RTZ model: canonical coefficients are m*2^-23 and
// m*2^(-34/-33). Fused exact numerator is m*(2*signed_field+1).
static uint32_t scalar_cmp_component(unsigned packed,unsigned component){
    if(component==3)return 0x3f800000;const unsigned width=component==2?10:11,shift=component==0?0:component==1?11:22;
    const unsigned field=(packed>>shift)&((1u<<width)-1);const int signed_field=int(field^(1u<<(width-1)))-int(1u<<(width-1));
    const int64_t numerator=int64_t(component==2?0x802008:0x801002)*(2*signed_field+1);const uint64_t magnitude=numerator<0?uint64_t(-numerator):uint64_t(numerator);
    unsigned bits=0;for(auto n=magnitude;n;n>>=1)++bits;const uint32_t significand=bits>24?uint32_t(magnitude>>(bits-24)):uint32_t(magnitude<<(24-bits));
    const unsigned exponent=unsigned(int(bits)-1+(component==2?-33:-34)+127);
    return (numerator<0?0x80000000u:0u)|(exponent<<23)|(significand&0x7fffff);
}
static void byte_model(const spu_context& c,unsigned groups,unsigned char* result){
    memcpy(result,c.ls,SPU_LS_SIZE);const unsigned manager=c.gpr[82]._u32[0]&~15u,source=c.gpr[4]._u32[0];
    const unsigned starts[]={geometry_job_be32(result+manager+16),geometry_job_be32(result+manager+20),geometry_job_be32(result+manager+24)};
    unsigned char cache[2][40];auto read=[&](unsigned index,unsigned char* dst){for(unsigned i=0;i<40;++i)dst[i]=result[(source+index*40+i)&0x3ffff];};
    read(0,cache[0]);for(unsigned i=0;i<3;++i){result[manager+0x160+i]=i+1;geometry_job_put32(result+manager+0x5c+i*4,starts[i]);}read(1,cache[1]);
    for(unsigned group=0;group<groups;++group){
        unsigned char rows[3][2][16];for(unsigned row=0;row<2;++row){const auto* vertex=cache[0]+row*20;memcpy(rows[0][row],vertex,12);geometry_job_put32(rows[0][row]+12,0x3f800000);
            for(unsigned attr=0;attr<2;++attr){const auto packed=geometry_job_be32(vertex+12+attr*4);for(unsigned lane=0;lane<4;++lane)geometry_job_put32(rows[attr+1][row]+lane*4,scalar_cmp_component(packed,lane));}}
        for(unsigned stream=0;stream<3;++stream)for(unsigned row=0;row<2;++row)memcpy(result+((starts[stream]+group*32+row*16)&0x3fff0),rows[stream][row],16);
        memcpy(cache[0],cache[1],40);read(group+2,cache[1]);
    }
}
static void compare(spu_context& native,spu_context& reference,unsigned trial,bool independent=false){
    auto before=std::make_unique<spu_context>();memcpy(before.get(),&native,sizeof native);GeometryBuiltin20Input p;const char* why=nullptr;
    if(!read_geometry_builtin20_input(native,p,&why)){fprintf(stderr,"builtin20 input admission trial%u %s\n",trial,why);abort();}
    assert(!memcmp(before.get(),&native,sizeof native));auto expected=std::make_unique<unsigned char[]>(SPU_LS_SIZE);if(independent)byte_model(native,p.groups,expected.get());copy(reference,native);g_spu_trampoline_fn=nullptr;
    builtin20_input_oracle::buzz_job_6eba_spu_func_0000A8F8(&reference);const auto finish=builtin20_input_oracle::buzz_job_6eba_spu_func_00009498;
    for(unsigned step=0;!(reference.pc==0x9498 && g_spu_trampoline_fn==finish);++step){assert(step<65536 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);}
    g_spu_trampoline_fn=spu_indirect_branch;decode_geometry_builtin20_input(native,p,finish);equal(native,reference,"data",trial);assert(g_spu_trampoline_fn==finish);if(independent){if(memcmp(native.ls,expected.get(),SPU_LS_SIZE)){fprintf(stderr,"builtin20 input independent byte model trial%u\n",trial);abort();}++models;}++strict_cases;
}
static void seed(spu_context& c,const spu_context& base,unsigned trial,unsigned groups,unsigned mode){
    copy(c,base);seed_word=trial+1;for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word();for(unsigned reg:{27u,28u,65u,80u})c.gpr[reg]=base.gpr[reg];
    const unsigned manager=0x3d000;unsigned source=0x18000+(trial&15),a=0x24000+((trial>>4)&15),b=0x28000+((trial>>8)&15),d=0x2c000+((trial>>12)&15);
    switch(mode){case 1:a=source;break;case 2:b=source+16;break;case 3:d=source+40;break;case 4:a=b=d=source+80;break;case 5:b=a+16;d=a+32;break;case 6:source=0x3fff0+(trial&15);break;}
    for(unsigned row=0;row<192;++row)for(unsigned lane=0;lane<4;++lane)geometry_job_put32(c.ls+((source+row*16)&0x3fff0)+lane*4,random_word());
    memset(c.ls+manager+0x160,mode==7?0x55:0,16);
    for(unsigned slot=0;slot<17;++slot)geometry_job_put32(c.ls+manager+0x10+slot*4,slot==0?a:slot==1?b:slot==2?d:0x30000);
    c.gpr[3]._u32[0]=c.gpr[82]._u32[0]=manager+(trial&15);c.gpr[83]._u32[0]=manager+16;
    c.gpr[4]._u32[0]=c.gpr[84]._u32[0]=source;c.gpr[35]._u32[0]=manager+0x5c+(trial&15);c.gpr[37]._u32[0]=manager+0x60+(trial&15);c.gpr[61]._u32[0]=manager+0x64+(trial&15);
    if(mode==8)c.gpr[35]._u32[0]=manager+0x14;if(mode==9)c.gpr[35]._u32[0]=manager+0x160;
    c.gpr[36]._u32[0]=groups*32;c.gpr[42]._u32[0]=0u-groups*32;c.pc=0xa8f8;c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;
    c.event_status=random_word();c.event_mask=random_word();c.resv_valid=trial&1;c.mfc_tag_status=random_word();c.int_enable=0;c.irq_saved=0;c.irq_frame=nullptr;
}
static void full_run(spu_context& c,bool native){
    builtin20_input_control_oracle::native_data=native?+[](spu_context* c){
        auto* original=clone_function_state(c);builtin20_input_control_oracle::native_data=nullptr;g_spu_trampoline_fn=nullptr;
        builtin20_input_control_oracle::buzz_job_6eba_spu_func_0000A8F8(original);const auto finish=builtin20_input_control_oracle::buzz_job_6eba_spu_func_00009498;
        for(unsigned step=0;!(original->pc==0x9498 && g_spu_trampoline_fn==finish);++step){assert(step<65536 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(original);}
        GeometryBuiltin20Input p;assert(read_geometry_builtin20_input(*c,p));decode_geometry_builtin20_input(*c,p,finish);equal(*c,*original,"full child",child_cases);assert(g_spu_trampoline_fn==finish);release_function_state(original);++child_cases;
    }:nullptr;
    const unsigned stack=c.gpr[1]._u32[0];g_spu_trampoline_fn=nullptr;builtin20_input_control_oracle::buzz_job_6eba_spu_func_000093F0(&c);
    for(unsigned step=0;!(c.gpr[1]._u32[0]==stack && c.pc==c.gpr[0]._u32[0] && g_spu_trampoline_fn==(c.host_depth?nullptr:spu_indirect_branch));++step){assert(step<65536 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&c);}
}
int main(){
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto base=std::make_unique<spu_context>(),entry=std::make_unique<spu_context>(),native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    memset(base.get(),0,sizeof(*base));base->ls=base->ls_store;base->image_id=41;base->pc=0x93f0;
    auto* file=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin","rb");assert(file);assert(fread(base->gpr,sizeof base->gpr,1,file)==1 && fread(base->ls,SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF);fclose(file);
    base->gpr[1]=spu_ila(0x3f000);base->gpr[3]=spu_ila(0x3d000);base->gpr[4]=spu_ila(0x18000);base->gpr[5]=spu_ila(0x20000);base->gpr[6]=spu_il(1);base->gpr[7]=base->gpr[8]=spu_il(0);
    memset(base->ls+0x3d000,0,0x190);base->ls[0x3d009]=3;for(unsigned slot=0;slot<17;++slot)geometry_job_put32(base->ls+0x3d010+slot*4,slot==0?0x24000:slot==1?0x28000:slot==2?0x2c000:0x30000);
    copy(*entry,*base);g_spu_trampoline_fn=nullptr;builtin20_input_control_oracle::buzz_job_6eba_spu_func_000093F0(entry.get());
    for(unsigned step=0;!(entry->pc==0xa8f8 && g_spu_trampoline_fn==spu_indirect_branch);++step){assert(step<32 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(entry.get());}
    const char* calibration="analysis/native-geometry/phase3/builtin20-input-calibration.bin";file=fopen(calibration,"rb");
    if(file){auto bytes=std::make_unique<unsigned char[]>(sizeof entry->gpr+SPU_LS_SIZE);assert(fread(bytes.get(),sizeof entry->gpr+SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF && !memcmp(bytes.get(),entry->gpr,sizeof entry->gpr) && !memcmp(bytes.get()+sizeof entry->gpr,entry->ls,SPU_LS_SIZE));fclose(file);}
    else{file=fopen(calibration,"wb");assert(file);assert(fwrite(entry->gpr,sizeof entry->gpr,1,file)==1 && fwrite(entry->ls,SPU_LS_SIZE,1,file)==1);fclose(file);}
    for(unsigned depth=0;depth<3;++depth){copy(*native,*entry);native->host_depth=depth;compare(*native,*reference,strict_cases);}
    for(unsigned trial=0;trial<4096;++trial){seed(*native,*entry,trial,1+trial%64,trial%10);compare(*native,*reference,strict_cases);}
    for(unsigned trial=0;trial<1024;++trial){seed(*native,*entry,5000+trial,1+trial%16,trial%10);
        for(unsigned reg:{27u,28u,65u,80u})for(auto& word:native->gpr[reg]._u32)word=random_word();
        const unsigned masks[]={0xd980,0xda80,0xd190,0xd1a0,0xcd60};for(unsigned index=0;index<5;++index)if(trial&(1u<<index))for(unsigned lane=0;lane<4;++lane)geometry_job_put32(native->ls+masks[index]+lane*4,random_word());
        if(trial&32)native->gpr[84]._u32[0]=0x3fff0+(trial&15);if(trial&64)native->gpr[36]._u32[0]+=16*(trial%4);compare(*native,*reference,strict_cases);
    }
    const unsigned special[]={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,0x7f800000,0xff800000,0x7fc00000,0x7f800001,0xff800001,0xffffffff,0x7f7fffff,0xff7fffff};
    for(unsigned trial=0;trial<256;++trial){seed(*native,*entry,7000+trial,1+trial%16,trial%10);for(unsigned reg:{27u,28u})for(unsigned lane=0;lane<4;++lane)native->gpr[reg]._u32[lane]=special[(trial+reg+lane)%16];compare(*native,*reference,strict_cases);}
    for(unsigned groups=1;groups<=16;++groups)for(unsigned bias=0;bias<16;++bias){seed(*native,*entry,10000+groups*16+bias,groups,0);const unsigned manager=0x3d000;
        native->gpr[3]._u32[0]=native->gpr[82]._u32[0]=manager;native->gpr[35]._u32[0]=manager+0x5c;native->gpr[37]._u32[0]=manager+0x60;native->gpr[61]._u32[0]=manager+0x64;
        const unsigned source=groups%4==0?manager+0x30+bias:groups%4==1?manager+0x138+bias:0x18000+bias;
        native->gpr[4]._u32[0]=native->gpr[84]._u32[0]=source;
        const unsigned a=groups%3==0?source+16:0x24000+bias,b=groups%3==1?source+40:groups%3==2?a+16:0x28000+bias,d=groups%3==2?source+80:0x2c000+bias;
        geometry_job_put32(native->ls+manager+16,a);geometry_job_put32(native->ls+manager+20,b);geometry_job_put32(native->ls+manager+24,d);compare(*native,*reference,strict_cases,true);
    }
    unsigned components=0;copy(*native,*entry);const auto shifts=spu_ls_read128(native.get(),0xd1a0),mask=spu_ls_read128(native.get(),0xd190),packing=spu_ls_read128(native.get(),0xda80);
    for(unsigned pattern=0;pattern<8192;++pattern){const unsigned word=(pattern&2047)|(((pattern*997+123)&2047)<<11)|(((pattern*613+31)&1023)<<22);
        const auto packed=spu_il(word),bits=spu_shufb(packed,packed,packing);const auto decoded=decode_geometry_builtin_input_cmp(bits,shifts,mask,native->gpr[28],native->gpr[27]);
        for(unsigned component=0;component<4;++component){assert(decoded.scaled._u32[component]==scalar_cmp_component(word,component));++components;}
    }
    for(unsigned groups:{1u,2u,3u,4u,5u,127u,128u,129u,512u,514u,2048u}){seed(*native,*entry,20000+groups,groups,0);compare(*native,*reference,strict_cases);}
    for(unsigned count=0;count<=65;++count)for(unsigned bias=0;bias<16;++bias)for(unsigned depth=0;depth<3;++depth){copy(*native,*base);native->ls[0x3d008]=count>>8;native->ls[0x3d009]=count;native->gpr[1]._u32[0]+=bias;native->host_depth=depth;native->gpr[4]._u32[0]+=bias;native->gpr[7]._u32[0]=0x32000+bias;native->gpr[8]._u32[0]=0x12345678+bias;
        geometry_job_put32(native->ls+0x3d010,0x24000+bias);geometry_job_put32(native->ls+0x3d014,0x28000+bias);geometry_job_put32(native->ls+0x3d018,0x2c000+bias);
        copy(*reference,*native);full_run(*reference,false);const auto expected=g_spu_trampoline_fn;full_run(*native,true);equal(*native,*reference,"full93F0",full_cases);assert(g_spu_trampoline_fn==expected);++full_cases;}
    for(unsigned depth=0;depth<3;++depth)for(unsigned bias=0;bias<16;++bias){copy(*native,*base);native->ls[0x3d009]=1;native->gpr[1]._u32[0]+=bias;native->host_depth=depth;
        geometry_job_put32(native->ls+0x3d010,(native->gpr[1]._u32[0]&~15u)-32);geometry_job_put32(native->ls+0x3d014,(native->gpr[1]._u32[0]&~15u)+16);
        copy(*reference,*native);full_run(*reference,false);const auto expected=g_spu_trampoline_fn;full_run(*native,true);equal(*native,*reference,"late frame aliases",full_cases);assert(g_spu_trampoline_fn==expected);++full_cases;}
    for(unsigned failure=0;failure<14;++failure){seed(*native,*entry,40000,17,0);switch(failure){case 0:native->ls=reference->ls_store;break;case 1:native->int_enable=1;break;case 2:native->irq_saved=1;break;case 3:native->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;
        case 4:native->gpr[42]._u32[0]=0;break;case 5:native->gpr[42]._u32[0]=-16u;break;case 6:native->gpr[42]._u32[0]=32;break;case 7:native->gpr[42]._u32[0]=-1048608u;break;
        case 8:native->gpr[35]._u32[0]=0;break;case 9:native->gpr[37]._u32[0]=SPU_LS_SIZE;break;case 10:native->gpr[61]._u32[0]=0;break;
        case 11:native->gpr[3]._u32[0]=SPU_LS_SIZE-0x160;break;case 12:geometry_job_put32(native->ls+0x3d010,0);break;case 13:native->gpr[36]._u32[0]=1048576;native->gpr[42]._u32[0]=-1048576u;break;}
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryBuiltin20Input p{123};const char* why=nullptr;g_spu_trampoline_fn=spu_indirect_branch;
        assert(!read_geometry_builtin20_input(*native,p,&why) && why && p.groups==123 && !memcmp(native.get(),reference.get(),sizeof(*native)) && g_spu_trampoline_fn==spu_indirect_branch);
    }
    assert(strict_cases==5646 && models==256 && full_cases==3216 && child_cases==3168 && components==32768);
    printf("PASS builtin20 input:%u strict full-state/LS/FN cases;%u independent ordered byte models;8192 independent CMP words/%u float components;1024 arbitrary-control cases;256 special-FP cases;%u full original93F0 cases/%u native strict child roots;48 late-frame aliases;14 unchanged rejects;synthetic input1 only\n",strict_cases,models,components,full_cases,child_cases);
}
