#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_formatter.h"
#include "../analysis/native-geometry/phase3/format-layout-v1/formatter-admission-reference.inc"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" { SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*); }
static unsigned random_word(unsigned& s) { s^=s<<13;s^=s>>17;s^=s<<5;return s; }

int main()
{
    auto base=std::make_unique<spu_context>(),c=std::make_unique<spu_context>(),before=std::make_unique<spu_context>();
    memset(base.get(),0,sizeof(*base));base->ls=base->ls_store;
    auto* f=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_4178.bin","rb");assert(f);
    assert(fread(base->gpr,sizeof(base->gpr),1,f)==1 && fread(base->ls,SPU_LS_SIZE,1,f)==1 && fgetc(f)==EOF);fclose(f);
    const unsigned manager=base->gpr[3]._u32[0]&~15u,descriptor=base->gpr[4]._u32[0]&~15u;
    unsigned accepted=0,rejected=0,parents=0,state=0x735a49;
    for(unsigned trial=0;trial<50000;++trial){
        memcpy(c.get(),base.get(),sizeof(*c));c->ls=c->ls_store;
        for(auto& r:c->gpr)for(auto& w:r._u32)w=random_word(state);
        c->gpr[0]=base->gpr[0];
        c->gpr[1]._u32[0]=base->gpr[1]._u32[0]+(trial&15);
        c->gpr[3]._u32[0]=manager+((trial%11)?0:trial&15);
        c->gpr[4]._u32[0]=descriptor+(trial&15);
        c->ls[manager]=(c->ls[manager]&~16u)|((trial%29)?0:16);
        const unsigned vertices=trial%31?1+trial%512:0;
        c->ls[manager+8]=vertices>>8;c->ls[manager+9]=vertices;
        geometry_job_put32(c->ls+manager+0x58,0x20000+(trial&31)*128);
        const unsigned count=trial%17?1+trial%8:0;
        c->ls[descriptor]=count;c->ls[descriptor+1]=trial&255;
        for(unsigned slot=0;slot<16;++slot){
            c->ls[manager+0x160+slot]=(slot+trial/7)%7;
            geometry_job_put32(c->ls+manager+16+slot*4,0x18000+slot*0x100);
        }
        for(unsigned field=0;field<count;++field){
            auto* p=c->ls+c->gpr[4]._u32[0]+8+field*8;
            p[0]=random_word(state)&31;p[1]=1+(trial+field)%9;p[2]=0;
            p[3]=(trial+field)%9;p[4]=random_word(state)&31;p[5]=p[6]=p[7]=0;
        }
        // Arbitrary nonpreferred routing lanes, including signed low-halfword
        // indices, high bits and wrapped physical destinations.
        if(trial%5==0)for(unsigned lane=1;lane<4;++lane){
            geometry_job_put32(c->ls+0xcf50+lane*4,random_word(state));
            geometry_job_put32(c->ls+0xcf60+lane*4,random_word(state));
        }
        if(trial%997==0)geometry_job_put32(c->ls+0xcf50,0x80000000);
        if(trial%37==0)c->ls[0x445c+4*(trial%7)]^=1;
        if(trial%101==0)c->int_enable=1;
        GeometryFormatArguments args{c->gpr[1]._u32[0],c->gpr[3]._u32[0],c->gpr[4]._u32[0]};
        u128 registers[128];memcpy(registers,c->gpr,sizeof registers);
        u128 pending=spu_ls_read128(c.get(),args.manager+0x50);
        const bool parent=(trial&1)!=0;
        if(parent){args.stack-=96;registers[1]=spu_ai(registers[1],-96);++parents;
            pending._u32[1]^=random_word(state);}
        GeometryFormatterEntry entry{args,&pending,c->gpr[1]._u32[0]-96,128,c->gpr[1]._u32[0]-128,16};
        legacy_formatter_admission::GeometryFormatterEntry old_entry{registers,&pending,entry.metadata_exclusion,entry.metadata_exclusion_bytes,entry.protected_data,entry.protected_data_bytes};
        memcpy(before.get(),c.get(),sizeof(*c));
        GeometryFormatter actual{123,456,789,111},expected=actual;
        const auto untouched=actual;const char* why=nullptr;
        const bool want=legacy_formatter_admission::read_geometry_formatter(*c,expected,&why,parent?&old_entry:nullptr,false);
        const bool got=read_geometry_formatter(*c,actual,&why,parent?&entry:nullptr,(trial&2)!=0);
        if(want!=got || memcmp(&actual,&expected,sizeof actual)){
            fprintf(stderr,"layout trial%u parent%u count%u stride%u manager%X desc%X: accepted %u/%u plan%u,%u,%u,%X/%u,%u,%u,%X\n",trial,parent,count,c->ls[descriptor+1],args.manager,args.descriptor,got,want,actual.fields,actual.known,actual.skipped,actual.mask,expected.fields,expected.known,expected.skipped,expected.mask);abort();
        }
        assert(!memcmp(before.get(),c.get(),sizeof(*c)));
        if(got)++accepted;else{++rejected;assert(!memcmp(&actual,&untouched,sizeof actual));}
    }
    assert(accepted>10000 && rejected>1000 && parents==25000);
    printf("Host format layout:50000 independent legacy-admission comparisons,%u accepted/%u rejected,%u parent forecasts;all strides,unaligned metadata/SP,semantic misses,signed routes,arbitrary lanes;input/output preservation PASS\n",accepted,rejected,parents);
}
