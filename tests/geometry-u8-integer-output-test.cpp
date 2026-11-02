#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_u8_integer_output.h"
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
#include "../analysis/native-geometry/phase3/test-u8-integer-output-reference.inc"
static unsigned exact=0;
static unsigned random_word(unsigned& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial,bool canonical=false){
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x52b4;unsigned state=trial+1;
    for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word(state);
    for(unsigned at=0;at<SPU_LS_SIZE;at+=4){const auto w=random_word(state);memcpy(c.ls+at,&w,4);}
    u8_integer_output_oracle::seed_constants(c);const unsigned columns=1+trial%16,stride=32,width=trial%17;
    c.gpr[77]._u32[0]=1;c.gpr[60]._u32[0]=columns;c.gpr[67]=spu_ila(0x28000);c.gpr[76]=spu_ila(0x28010);
    c.gpr[52]=spu_ila(0x10000);c.gpr[3]=spu_ila(0x10000+columns*16-1);
    c.gpr[75]=spu_il(columns*16);c.gpr[74]=spu_il(columns*32);c.gpr[73]=spu_il(columns*48);
    c.gpr[29]=spu_il(columns*64);c.gpr[31]=spu_il(columns*4*stride);c.gpr[55]=spu_il(stride);c.gpr[53]=spu_il((trial/16)&15);
    c.gpr[48]=spu_il(width);c.gpr[45]=spu_ila((0xffff0000u>>width)&65535);c.gpr[49]=spu_orbi(c.gpr[63],16);
    for(unsigned lane=0;lane<4;++lane){c.gpr[54]._u32[lane]=lane*columns;c.gpr[30]._u32[lane]=0x9000+lane*64;}
    if(!canonical){
        for(unsigned lane=0;lane<4;++lane){
            c.gpr[55]._u32[lane]|=random_word(state)&0xffff0000;c.gpr[54]._u32[lane]|=random_word(state)&0xffff0000;
            c.gpr[53]._u32[lane]|=random_word(state)&0xfffc0000;c.gpr[30]._u32[lane]|=random_word(state)&0xfffc0000;
            c.gpr[31]._u32[lane]|=random_word(state)&0xfffc0000;
            if(lane){c.gpr[52]._u32[lane]=random_word(state);c.gpr[3]._u32[lane]=random_word(state);c.gpr[29]._u32[lane]=random_word(state);c.gpr[48]._u32[lane]=random_word(state);c.gpr[45]._u32[lane]=random_word(state);}
        }
        if(trial&32)for(unsigned reg:{43u,71u,63u,49u})for(auto& w:c.gpr[reg]._u32)w=random_word(state);
    }
    c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;c.event_mask=trial*13;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;
}
// Independent integer-byte result of RTZ SPU (x+.5), unsigned scale24/high byte.
// Integers below256 are exact representable boundaries: RTZ cannot cross them.
static uint8_t scalar_field(uint32_t bits){
    if(bits>>31)return 0;
    const unsigned exponent=(bits>>23)&255;const int power=int(exponent)-127;
    if(!exponent || power<-1)return 0;
    if(power>=8)return 255;
    const unsigned shift=23-power;
    const uint32_t rounded=(0x800000u+(bits&0x7fffffu)+(1u<<(shift-1)))>>shift;
    return uint8_t(rounded>255?255:rounded);
}
static void set_grid(spu_context& c,unsigned trial){
    for(unsigned at=0;at<SPU_LS_SIZE;at+=16)for(unsigned lane=0;lane<4;++lane){
        const int n=int((at/4+lane+trial*17)%8193)-4096;const float value=float(n)/16.0f;uint32_t bits;memcpy(&bits,&value,4);geometry_job_put32(c.ls+at+lane*4,bits);
    }
}
static int grid_value(const unsigned char* ls,unsigned at){uint32_t bits=geometry_job_be32(ls+at);float value;memcpy(&value,&bits,4);return int(value*2048.0f);}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void compare(spu_context& native,spu_context& reference,unsigned trial){
    GeometryU8IntegerOutput plan;const char* why=nullptr;if(!read_geometry_u8_integer_output(native,plan,&why)){fprintf(stderr,"reject trial%u: %s\n",trial,why);abort();}
    const auto finish=u8_integer_output_oracle::buzz_job_6eba_spu_func_00004610,skip=u8_integer_output_oracle::buzz_job_6eba_spu_func_00004618;
    g_spu_trampoline_fn=nullptr;u8_integer_output_oracle::buzz_job_6eba_spu_func_000052B4(&reference);
    for(unsigned steps=0;g_spu_trampoline_fn!=finish && g_spu_trampoline_fn!=skip;++steps){assert(steps<2048 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);}
    const auto expected=g_spu_trampoline_fn;
    geometry_output_register_test::compare("u8-integer",native,reference,plan,finish,skip,expected,trial,emit_geometry_u8_integer_output_registers,read_geometry_u8_integer_output);
    g_spu_trampoline_fn=nullptr;emit_geometry_u8_integer_output(native,plan,finish,skip);
    const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"U8 integer output trial%u cols%u groups%u r%d byte%d LS%X/%u state%s\n",trial,plan.columns,plan.groups,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned reg=0;reg<128;++reg)if(memcmp(&native.gpr[reg],&reference.gpr[reg],16)){
            fprintf(stderr,"r%u native",reg);for(auto w:native.gpr[reg]._u32)fprintf(stderr," %08X",w);fprintf(stderr," ref");for(auto w:reference.gpr[reg]._u32)fprintf(stderr," %08X",w);fputc('\n',stderr);
        }abort();
    }
    assert(g_spu_trampoline_fn==expected && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);++exact;
}
int main(){
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
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
    // Mixed columns plus input/output aliases exercise the mask transitions.
    for(unsigned stride:{12u,20u,24u,28u})for(unsigned trial=0;trial<256;++trial){
        seed(*native,trial,true);native->gpr[55]=spu_il(stride);native->gpr[31]=spu_il((1+trial%16)*4*stride);
        if(trial&128){native->gpr[67]=native->gpr[52];native->gpr[76]=spu_ai(native->gpr[67],16);}
        copy(*reference,*native);compare(*native,*reference,18000+stride*256+trial);
    }
    // Independent byte-copy model: distinct nonaliased fields; low nibble is
    // the nominal field's nibble even on the final sentinel route.
    auto expected=std::make_unique<unsigned char[]>(SPU_LS_SIZE);unsigned numeric=0;
    for(unsigned width=1;width<=16;++width)for(unsigned bias=0;bias<16;++bias){
        seed(*native,3,true);native->gpr[48]=spu_il(width);native->gpr[45]=spu_ila((0xffff0000u>>width)&65535);native->gpr[53]=spu_il(bias);
        set_grid(*native,width*16+bias);
        memcpy(expected.get(),native->ls,SPU_LS_SIZE);const unsigned columns=4,stride=32,source=0x10000,destination=0x28000;
        for(unsigned col=0;col<columns;++col)for(unsigned group=0;group<2;++group)for(unsigned lane=0;lane<4;++lane){
            const unsigned input=source+col*16+group*columns*64+lane*columns*16;
            const unsigned route=group?0x9000+lane*64:stride*(col+lane*columns)+bias;
            const unsigned start=((destination+route)&~15u)+((stride*col+bias)&15);
            unsigned char converted[16]{};
            for(unsigned component=0;component<4;++component)converted[component]=scalar_field(geometry_job_be32(native->ls+input+component*4));
            memcpy(expected.get()+start,converted,width);
        }
        copy(*reference,*native);compare(*native,*reference,20000+numeric);++numeric;
        assert(!memcmp(expected.get(),native->ls,SPU_LS_SIZE));
    }
    for(unsigned trial=0;trial<16;++trial){seed(*native,trial,true);native->gpr[77]._u32[0]=0;native->gpr[60]._u32[0]=0xffffffff;
        copy(*reference,*native);compare(*native,*reference,30000+trial);
        seed(*native,trial,true);native->gpr[60]._u32[0]=0;copy(*reference,*native);compare(*native,*reference,31000+trial);
    }
    for(unsigned columns:{1u,128u,256u})for(unsigned bias:{0u,15u}){
        seed(*native,3,true);native->gpr[60]._u32[0]=columns;native->gpr[48]=spu_il(16);native->gpr[45]=spu_ila(65535);native->gpr[53]=spu_il(bias);
        copy(*reference,*native);compare(*native,*reference,35000+columns+bias);
    }
    for(unsigned step:{0x7ff0u,0x8000u,0xffffu,0xfffffff0u})for(unsigned bias:{0u,15u}){
        seed(*native,3,true);native->gpr[53]=spu_il(bias);
        for(unsigned lane=0;lane<4;++lane){native->gpr[29]._u32[lane]=step;native->gpr[3]._u32[lane]=0u-uint32_t(int32_t(int16_t(step)));}
        copy(*reference,*native);compare(*native,*reference,36000+bias);
    }
    seed(*native,0,true);auto* fixture=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_4B9C.bin","rb");assert(fixture);
    assert(fread(native->gpr,sizeof native->gpr,1,fixture)==1 && fread(native->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
    copy(*reference,*native);compare(*native,*reference,40000);
    for(unsigned failure=0;failure<9;++failure){seed(*native,0,true);switch(failure){
        case 0:native->ls=reference->ls_store;break;case 1:native->int_enable=1;native->event_status=native->event_mask=1;break;
        case 2:native->gpr[60]._u32[0]=257;break;case 3:native->gpr[29]=spu_il(0);break;
        case 4:native->gpr[67]=spu_il(0);break;case 5:native->gpr[48]=spu_il(16);native->gpr[53]=spu_il(15);native->gpr[76]=spu_il(0);break;
        case 6:native->int_enable=1;break;case 7:native->irq_saved=1;break;case 8:native->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryU8IntegerOutput plan{123,456};const char* why=nullptr;
        assert(!read_geometry_u8_integer_output(*native,plan,&why) && why && plan.columns==123 && plan.groups==456 && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    // Every high halfword with16 distinct low patterns;16 unique source words
    // per pack, permuted independently across records/components.
    seed(*native,0,true);unsigned patterns=0;
    const unsigned lows[]={0,1,2,3,7,0x3ff,0x7ff,0x3fff,0x4000,0x7ffe,0x7fff,0x8000,0x8001,0xbfff,0xfffe,0xffff};
    for(unsigned high=0;high<65536;++high){
        u128 input[4];for(unsigned i=0;i<4;++i)for(unsigned lane=0;lane<4;++lane)input[i]._u32[lane]=(high<<16)|lows[((i*4+lane)*5+(high&15))&15];
        const auto result=pack_geometry_u8_integer_output(native->gpr,input);
        for(unsigned i=0;i<4;++i)for(unsigned lane=0;lane<4;++lane){assert(uint8_t(result.integer[i]._u32[lane]>>24)==scalar_field(input[i]._u32[lane]));++patterns;}
    }
    const unsigned special[]={0,0x80000000u,1,0x80000001u,0x7fffff,0x807fffffu,0x800000,0x80800000u,0x7f7fffff,0xff7fffffu,0x7f800000u,0xff800000u,0x7fffffffu,0xffffffffu,0x3f800000,0xbf800000u};
    for(unsigned trial=0;trial<256;++trial){seed(*native,trial,true);
        for(unsigned at=0;at<SPU_LS_SIZE;at+=4)geometry_job_put32(native->ls+at,special[(at/4+trial)%16]);
        copy(*reference,*native);compare(*native,*reference,50000+trial);
    }
    const unsigned edges[]={0,0x80000000u,0x377fffff,0xb77fffffu,0x37800000,0xb7800000u,0x37800001,0xb7800001u,
        0x3effffff,0xbeffffffu,0x3f000000,0xbf000000u,0x3f000001,0xbf000001u,0x3f800000,0xbf800000u,
        0x437effff,0xc37effffu,0x437f0000,0xc37f0000u,0x437f8000,0xc37f8000u,0x43800000,0xc3800000u,
        0x46ffffff,0xc6ffffffu,0x47000000,0xc7000000u,0x47000001,0xc7000001u,0x7fffffff,0xffffffffu};
    for(unsigned trial=0;trial<32;++trial){seed(*native,trial,true);
        for(unsigned at=0;at<SPU_LS_SIZE;at+=4)geometry_job_put32(native->ls+at,edges[(at/4+trial)%32]);
        copy(*reference,*native);compare(*native,*reference,60000+trial);
    }
    assert(patterns==1048576);
    geometry_output_register_test::report(exact);
    printf("PASS complete U8 integer output: %u strict full-state/LS/FN cases;1792 alias/wrap cases;1024 mixed/alias cases;256 independent byte models;1048576 independent FP-bit components;256 special-FP roots;32 boundary roots;one calibrated synthetic root;no captured cold root;9 unchanged rejects\n",exact);
}
