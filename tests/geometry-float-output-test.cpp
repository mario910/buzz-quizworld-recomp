#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_float_output.h"
#include "../port/native_geometry_context.h"
#include "../port/native_function_check.h"
#include "geometry-output-register-test.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*){abort();}
void spu_stop(spu_context*){abort();}
}
#include "../analysis/native-geometry/phase3/test-float-output-reference.inc"
static unsigned exact=0;
static unsigned random_word(unsigned& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial,bool canonical=false){
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x46d0;unsigned state=trial+1;
    for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word(state);
    for(unsigned at=0;at<SPU_LS_SIZE;at+=4){const auto w=random_word(state);memcpy(c.ls+at,&w,4);}
    float_output_oracle::seed_shuffle(c);const unsigned columns=1+trial%16,stride=32,width=trial%17;
    c.gpr[77]._u32[0]=1;c.gpr[60]._u32[0]=columns;c.gpr[67]=spu_ila(0x28000);c.gpr[76]=spu_ila(0x28010);
    c.gpr[52]=spu_ila(0x10000);c.gpr[3]=spu_ila(0x10000+columns*16-1);
    c.gpr[75]=spu_il(columns*16);c.gpr[74]=spu_il(columns*32);c.gpr[73]=spu_il(columns*48);
    c.gpr[29]=spu_il(columns*64);c.gpr[31]=spu_il(columns*4*stride);c.gpr[55]=spu_il(stride);c.gpr[53]=spu_il((trial/16)&15);
    c.gpr[48]=spu_il(width);c.gpr[45]=spu_ila((0xffff0000u>>width)&65535);c.gpr[63]=spu_ls_read128(&c,0xd980);c.gpr[49]=spu_orbi(c.gpr[63],16);
    for(unsigned lane=0;lane<4;++lane){c.gpr[54]._u32[lane]=lane*columns;c.gpr[30]._u32[lane]=0x9000+lane*64;}
    if(!canonical){
        for(unsigned lane=0;lane<4;++lane){
            c.gpr[55]._u32[lane]|=random_word(state)&0xffff0000;c.gpr[54]._u32[lane]|=random_word(state)&0xffff0000;
            c.gpr[53]._u32[lane]|=random_word(state)&0xfffc0000;c.gpr[30]._u32[lane]|=random_word(state)&0xfffc0000;
            c.gpr[31]._u32[lane]|=random_word(state)&0xfffc0000;
            if(lane){c.gpr[52]._u32[lane]=random_word(state);c.gpr[3]._u32[lane]=random_word(state);c.gpr[29]._u32[lane]=random_word(state);c.gpr[48]._u32[lane]=random_word(state);c.gpr[45]._u32[lane]=random_word(state);}
        }
        if(trial&32)for(unsigned reg:{63u,49u})for(auto& w:c.gpr[reg]._u32)w=random_word(state);
    }
    c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;c.event_mask=trial*13;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;
}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void compare(spu_context& native,spu_context& reference,unsigned trial){
    GeometryFloatOutput plan;const char* why=nullptr;if(!read_geometry_float_output(native,plan,&why)){fprintf(stderr,"reject trial%u: %s\n",trial,why);abort();}
    const auto finish=float_output_oracle::buzz_job_6eba_spu_func_00004610,skip=float_output_oracle::buzz_job_6eba_spu_func_00004618;
    g_spu_trampoline_fn=nullptr;float_output_oracle::buzz_job_6eba_spu_func_000046D0(&reference);
    for(unsigned steps=0;g_spu_trampoline_fn!=finish && g_spu_trampoline_fn!=skip;++steps){assert(steps<1000 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);}
    const auto expected=g_spu_trampoline_fn;
    geometry_output_register_test::compare("float",native,reference,plan,finish,skip,expected,trial,emit_geometry_float_output_registers);
    g_spu_trampoline_fn=nullptr;emit_geometry_float_output(native,plan,finish,skip);
    const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"float output trial%u cols%u groups%u r%d byte%d LS%X/%u state%s\n",trial,plan.columns,plan.groups,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned reg=0;reg<128;++reg)if(memcmp(&native.gpr[reg],&reference.gpr[reg],16)){
            fprintf(stderr,"r%u native",reg);for(auto w:native.gpr[reg]._u32)fprintf(stderr," %08X",w);fprintf(stderr," ref");for(auto w:reference.gpr[reg]._u32)fprintf(stderr," %08X",w);fputc('\n',stderr);
        }abort();
    }
    assert(g_spu_trampoline_fn==expected && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);++exact;
}
int main(){
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial);copy(*reference,*native);compare(*native,*reference,trial);}
    for(unsigned shape=0;shape<7;++shape)for(unsigned trial=0;trial<256;++trial){seed(*native,trial,true);
        switch(shape){
        case 0:native->gpr[67]=native->gpr[52];native->gpr[76]=spu_ai(native->gpr[67],16);break;
        case 1:native->gpr[67]=spu_ai(native->gpr[52],-16);native->gpr[76]=spu_ai(native->gpr[67],16);break;
        case 2:native->gpr[67]=spu_ai(native->gpr[52],16);native->gpr[76]=spu_ai(native->gpr[67],16);break;
        case 3:native->gpr[52]=spu_ila(SPU_LS_SIZE-16);native->gpr[3]=spu_ai(native->gpr[52],32);break;
        case 4:native->gpr[52]=spu_il(-32);native->gpr[3]=spu_il(0);break;
        case 5:native->gpr[67]=spu_il(-65536);native->gpr[76]=spu_ai(native->gpr[67],16);break;
        case 6:for(unsigned lane=0;lane<4;++lane){native->gpr[54]._u32[lane]=0;native->gpr[30]._u32[lane]=0x9000;}break;
        }
        copy(*reference,*native);compare(*native,*reference,10000+shape*256+trial);
    }
    // Independent byte-copy model: distinct nonaliased fields; low nibble is
    // the nominal field's nibble even on the final sentinel route.
    auto expected=std::make_unique<unsigned char[]>(SPU_LS_SIZE);unsigned numeric=0;
    for(unsigned width=1;width<=16;++width)for(unsigned bias=0;bias<16;++bias){
        seed(*native,3,true);native->gpr[48]=spu_il(width);native->gpr[45]=spu_ila((0xffff0000u>>width)&65535);native->gpr[53]=spu_il(bias);
        memcpy(expected.get(),native->ls,SPU_LS_SIZE);const unsigned columns=4,stride=32,source=0x10000,destination=0x28000;
        for(unsigned col=0;col<columns;++col)for(unsigned group=0;group<2;++group)for(unsigned lane=0;lane<4;++lane){
            const unsigned input=source+col*16+group*columns*64+lane*columns*16;
            const unsigned route=group?0x9000+lane*64:stride*(col+lane*columns)+bias;
            const unsigned start=((destination+route)&~15u)+((stride*col+bias)&15);
            memcpy(expected.get()+start,native->ls+input,width);
        }
        copy(*reference,*native);compare(*native,*reference,20000+numeric);++numeric;
        assert(!memcmp(expected.get(),native->ls,SPU_LS_SIZE));
    }
    for(unsigned trial=0;trial<16;++trial){seed(*native,trial,true);native->gpr[77]._u32[0]=0;native->gpr[60]._u32[0]=0xffffffff;
        copy(*reference,*native);compare(*native,*reference,30000+trial);
        seed(*native,trial,true);native->gpr[60]._u32[0]=0;copy(*reference,*native);compare(*native,*reference,31000+trial);
    }
    seed(*native,0,true);auto* fixture=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_46D0.bin","rb");assert(fixture);
    assert(fread(native->gpr,sizeof native->gpr,1,fixture)==1 && fread(native->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
    copy(*reference,*native);compare(*native,*reference,40000);
    for(unsigned failure=0;failure<9;++failure){seed(*native,0,true);switch(failure){
        case 0:native->ls=reference->ls_store;break;case 1:native->int_enable=1;native->event_status=native->event_mask=1;break;
        case 2:native->gpr[60]._u32[0]=257;break;case 3:native->gpr[29]=spu_il(0);break;
        case 4:native->gpr[67]=spu_il(0);break;case 5:native->gpr[48]=spu_il(16);native->gpr[53]=spu_il(15);native->gpr[76]=spu_il(0);break;
        case 6:native->int_enable=1;break;case 7:native->irq_saved=1;break;case 8:native->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryFloatOutput plan{123,456};const char* why=nullptr;
        assert(!read_geometry_float_output(*native,plan,&why) && why && plan.columns==123 && plan.groups==456 && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    geometry_output_register_test::report(exact);
    printf("PASS complete float output: %u strict full-state/LS/FN cases;1792 alias/wrap cases;256 independent byte models;one real root;9 unchanged rejects\n",exact);
}
