#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_formatter.h"
#include "../port/native_geometry_snorm_output.h"
#include "../port/native_geometry_float_output.h"
#include "../port/native_geometry_half_output.h"
#include "../port/native_geometry_unorm_output.h"
#include "../port/native_geometry_i16_output.h"
#include "../port/native_geometry_cmp_output.h"
#include "../port/native_geometry_u8_integer_output.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
using namespace buzz;
namespace formatter_oracle {static void dispatch(spu_context*);}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* c){formatter_oracle::dispatch(c);}
void spu_stop(spu_context*){abort();}
}
#include "../analysis/native-geometry/phase3/test-formatter-reference.inc"
static std::vector<std::unique_ptr<spu_context>> entries;
static unsigned strict_cases=0,child_cases=0,native_data=0,recipe=0,native_types[7]{};
static bool actual_data=false;
static unsigned random_word(unsigned& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void equal(const spu_context& a,const spu_context& b,const char* label,unsigned trial){
    const auto d=compare_function_state(a,b);
    if(!d.exact()){fprintf(stderr,"formatter %s trial%u r%d byte%d LS%X/%u state%s PC%X/%X\n",label,trial,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal",a.pc,b.pc);abort();}
    assert(a.host_depth==b.host_depth && a.image_id==b.image_id && a.policy_mode==b.policy_mode && a.resident_ovl==b.resident_ovl);
}
static void observe(spu_context* c,unsigned){auto p=std::make_unique<spu_context>();copy(*p,*c);entries.push_back(std::move(p));}
static void mock(spu_context* c,unsigned){
    const auto sp=c->gpr[1]._u32[0];unsigned state=0x56789+recipe;
    if(recipe&1)for(unsigned at=64;at<128;at+=16){u128 value;for(auto& w:value._u32)w=random_word(state);spu_ls_write128(c,sp+at,value);}
    if(recipe&2)c->ls[c->gpr[64]._u32[0]&~15u]=1; // late count reload terminates
    if(recipe&4){for(unsigned reg=2;reg<16;++reg)for(auto& w:c->gpr[reg]._u32)w=random_word(state);}
    c->pc=0x4610;g_spu_trampoline_fn=formatter_oracle::buzz_job_6eba_spu_func_00004610;
}
static void data(spu_context* c,unsigned kind){
    static buzz_geometry_job_fn originals[]={formatter_oracle::buzz_job_6eba_spu_func_00004478,formatter_oracle::buzz_job_6eba_spu_func_000046D0,
        formatter_oracle::buzz_job_6eba_spu_func_00004880,formatter_oracle::buzz_job_6eba_spu_func_00004B9C,formatter_oracle::buzz_job_6eba_spu_func_00004DD0,
        formatter_oracle::buzz_job_6eba_spu_func_00004FDC,formatter_oracle::buzz_job_6eba_spu_func_000052B4};
    observe(c,kind);
    if(recipe){mock(c,kind);return;}
    if(!actual_data){const auto saved=formatter_oracle::observer;formatter_oracle::observer=nullptr;originals[kind](c);formatter_oracle::observer=saved;return;}
    const auto finish=formatter_oracle::buzz_job_6eba_spu_func_00004610,skip=formatter_oracle::buzz_job_6eba_spu_func_00004618;
    const char* why=nullptr;
    switch(kind){
    case 0:{GeometrySnormOutput p;assert(read_geometry_snorm_output(*c,p,&why));emit_geometry_snorm_output(*c,p,finish,skip);break;}
    case 1:{GeometryFloatOutput p;assert(read_geometry_float_output(*c,p,&why));emit_geometry_float_output(*c,p,finish,skip);break;}
    case 2:{GeometryHalfOutput p;assert(read_geometry_half_output(*c,p,&why));emit_geometry_half_output(*c,p,finish,skip);break;}
    case 3:{GeometryUnormOutput p;assert(read_geometry_unorm_output(*c,p,&why));emit_geometry_unorm_output(*c,p,finish,skip);break;}
    case 4:{GeometryI16Output p;assert(read_geometry_i16_output(*c,p,&why));emit_geometry_i16_output(*c,p,finish,skip);break;}
    case 5:{GeometryCmpOutput p;assert(read_geometry_cmp_output(*c,p,&why));emit_geometry_cmp_output(*c,p,finish,skip);break;}
    case 6:{GeometryU8IntegerOutput p;assert(read_geometry_u8_integer_output(*c,p,&why));emit_geometry_u8_integer_output(*c,p,finish,skip);break;}
    }
    ++native_data;++native_types[kind];
}
static void d0(spu_context* c){data(c,0);}static void d1(spu_context* c){data(c,1);}static void d2(spu_context* c){data(c,2);}
static void d3(spu_context* c){data(c,3);}static void d4(spu_context* c){data(c,4);}static void d5(spu_context* c){data(c,5);}static void d6(spu_context* c){data(c,6);}
static void compare(spu_context& native,spu_context& reference,unsigned trial){
    static auto admission=std::make_unique<spu_context>();memcpy(admission.get(),&native,sizeof native);
    GeometryFormatter plan;const char* why=nullptr;if(!read_geometry_formatter(native,plan,&why)){fprintf(stderr,"formatter reject trial%u %s\n",trial,why);abort();}
    GeometryFormatter exhaustive;assert(read_geometry_formatter(native,exhaustive,&why,nullptr,false));
    assert(plan.fields==exhaustive.fields && plan.known==exhaustive.known && plan.skipped==exhaustive.skipped && plan.mask==exhaustive.mask);
    assert(!memcmp(admission.get(),&native,sizeof native));
    const auto sp=reference.gpr[1]._u32[0];entries.clear();formatter_oracle::observer=observe;formatter_oracle::mock_data=recipe?mock:nullptr;g_spu_trampoline_fn=nullptr;
    formatter_oracle::buzz_job_6eba_spu_func_00004178(&reference);
    for(unsigned steps=0;!(reference.gpr[1]._u32[0]==sp && reference.pc==reference.gpr[0]._u32[0] && g_spu_trampoline_fn==(reference.host_depth?nullptr:spu_indirect_branch));++steps){
        assert(steps<1048576 && g_spu_trampoline_fn);const auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);
    }
    const auto expected=g_spu_trampoline_fn;auto expected_entries=std::move(entries);entries.clear();formatter_oracle::observer=nullptr;formatter_oracle::mock_data=nullptr;
    const GeometryFormatterCallees f{{d0,d1,d2,d3,d4,d5,d6},formatter_oracle::buzz_job_6eba_spu_func_00004610,formatter_oracle::buzz_job_6eba_spu_func_00004618};
    execute_geometry_formatter(native,plan,f);equal(native,reference,"exit",trial);
    if(g_spu_trampoline_fn!=expected || entries.size()!=expected_entries.size()){fprintf(stderr,"formatter frontier trial%u fn%p/%p entries%zu/%zu plan%u/%u/%u\n",trial,(void*)g_spu_trampoline_fn,(void*)expected,entries.size(),expected_entries.size(),plan.fields,plan.known,plan.skipped);abort();}
    if(!recipe)assert(entries.size()==plan.known);
    for(unsigned i=0;i<entries.size();++i){equal(*entries[i],*expected_entries[i],"child",trial);++child_cases;}++strict_cases;
}
// Independent scalar enumeration of actual quads, including per-lane dummy
// selection. The production proof uses spans and division instead.
static bool enumerate_column(u128 cursor,const u128& limit,u128 route,const u128& step,const u128& advance,const u128& dummy,
    unsigned base,unsigned second,bool cross,const GeometryFormatterWriteBounds& b,unsigned total,unsigned& groups)
{
    for(unsigned g=1;g<=65536;++g){
        if(++total>1048576)return false;
        const bool stop=cursor._s32[0]>limit._s32[0];
        for(unsigned lane=0;lane<4;++lane)for(unsigned side=0;side<(cross?2u:1u);++side){
            const unsigned chosen=cursor._s32[lane]>limit._s32[lane]?dummy._u32[lane]:route._u32[lane];
            const unsigned at=((side?second:base)+chosen)&0x3fff0;
            if(at<0xe680)return false;
            for(unsigned byte=0;byte<16;++byte){const uint64_t address=uint64_t(at)+byte;
                if((address>=b.manager && address<uint64_t(b.manager)+0x170) ||
                   (address>=b.descriptor && address<uint64_t(b.descriptor)+b.descriptor_bytes) ||
                   (b.protected_bytes && address>=b.protected_data && address<uint64_t(b.protected_data)+b.protected_bytes))return false;
            }
        }
        if(stop){groups=g;return true;}
        for(unsigned lane=0;lane<4;++lane){cursor._u32[lane]+=step._u32[lane];route._u32[lane]+=advance._u32[lane];}
    }
    return false;
}
static void test_column_proof()
{
    unsigned state=0x91a739,proved=0,retained=0;
    for(unsigned trial=0;trial<50000;++trial){
        u128 cursor{},limit{},route{},step{},advance{},dummy{};
        const unsigned delta=1+(random_word(state)%1024),wanted=random_word(state)%96;
        const int start=int(random_word(state)%0x20000)-0x10000;
        unsigned base=(trial%17==0?0xfffff000u:0x10000u)+(random_word(state)&15);
        GeometryFormatterWriteBounds b{0xea00,0xf400,64,0x2d010,16};
        for(unsigned lane=0;lane<4;++lane){
            cursor._s32[lane]=start+(int(random_word(state)%7)-3)*int(delta);limit._s32[lane]=start+int(wanted*delta);
            step._u32[lane]=delta;advance._u32[lane]=(random_word(state)%128)*16+(random_word(state)&15)*0x40000;
            unsigned target=0x10000+random_word(state)%0x17000;
            if(trial%11==0)target=0xe5f0;if(trial%13==0)target=0x3ffe0;
            route._u32[lane]=target-base;dummy._u32[lane]=0x30000+lane*16-base;
        }
        if(trial%19==0)advance._u32[0]|=1;
        if(trial%23==0)step._u32[3]++;
        if(trial%29==0){cursor._s32[0]=INT32_MAX-8;limit._s32[0]=INT32_MAX;step=spu_il(16);}
        if(trial%5==0)b.protected_data=(base+route._u32[1])&0x3fff0;
        const unsigned total=trial%31==0?1048550:0;const bool cross=trial&1;
        unsigned groups=123456,expected=0;
        if(prove_geometry_formatter_column(cursor,limit,route,step,advance,dummy,base,base+16,cross,b,total,groups)){
            assert(enumerate_column(cursor,limit,route,step,advance,dummy,base,base+16,cross,b,total,expected) && groups==expected);++proved;
        }else {assert(groups==123456);++retained;}
    }
    assert(proved>10000 && retained>1000);
    GeometryFormatterWriteBounds b{0xe680,0xe900,16,0,0};unsigned groups=0,expected=0;
    const auto zero=spu_il(0),one=spu_il(1),limit=spu_il(65535);
    assert(prove_geometry_formatter_column(one,limit,zero,one,zero,zero,0x10000,0x10010,true,b,983040,groups) && groups==65536);
    assert(enumerate_column(one,limit,zero,one,zero,zero,0x10000,0x10010,true,b,983040,expected) && expected==groups);
    groups=77;assert(!prove_geometry_formatter_column(one,limit,zero,one,zero,zero,0x10000,0x10010,true,b,983041,groups) && groups==77);
    assert(!prove_geometry_formatter_column(one,spu_il(65536),zero,one,zero,zero,0x10000,0x10010,true,b,0,groups) && groups==77);
    // A protected gap lies inside the conservative span but is never written.
    // The proof must defer, while the retained exact enumeration accepts it.
    b.protected_data=0x10010;b.protected_bytes=16;
    assert(!prove_geometry_formatter_column(zero,spu_il(16),zero,spu_il(16),spu_il(32),spu_il(0x4000),0x10000,0x10010,false,b,0,groups));
    assert(enumerate_column(zero,spu_il(16),zero,spu_il(16),spu_il(32),spu_il(0x4000),0x10000,0x10010,false,b,0,expected));
    b.protected_bytes=0;
    assert(prove_geometry_formatter_column(zero,spu_il(16),zero,spu_il(16),spu_il(0x40000),zero,0x10000,0x10010,false,b,0,groups) && groups==3);
    printf("PASS formatter column proof:50000 independent scalar-quad models;%u proven/%u retained;per-lane tails,cross stores,modular high bits,physical-wrap deferral,protected gaps and65536/1048576 budgets\n",proved,retained);
}
int main(){
    test_column_proof();
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto base=std::make_unique<spu_context>(),native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();memset(base.get(),0,sizeof(*base));base->ls=base->ls_store;base->image_id=41;base->pc=0x56ac;
    auto* file=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_4178.bin","rb");assert(file);assert(fread(base->gpr,sizeof base->gpr,1,file)==1 && fread(base->ls,SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF);fclose(file);
    const unsigned manager=base->gpr[3]._u32[0]&~15u,descriptor=base->gpr[4]._u32[0]&~15u;
    for(unsigned trial=0;trial<1536;++trial){
        copy(*native,*base);unsigned state=trial+1;
        for(unsigned reg=0;reg<128;++reg)for(auto& word:native->gpr[reg]._u32)word=random_word(state);
        for(unsigned reg:{0u,1u,3u,4u})native->gpr[reg]._u32[0]=base->gpr[reg]._u32[0];
        const unsigned path=trial%4;native->host_depth=trial%3;native->policy_mode=trial%3;native->resident_ovl=trial%5;native->event_mask=trial*13;native->mfc_tag_status=trial*123;native->resv_valid=trial&1;
        native->gpr[1]._u32[0]+=trial&15;
        native->ls[manager]=(native->ls[manager]&~16u)|(path==0?16:0);
        native->ls[manager+8]=0;native->ls[manager+9]=path==1?0:1+(trial%63);
        const unsigned bias=(trial&16)?8:0;native->gpr[4]._u32[0]=descriptor+bias;
        const unsigned count=path==2?0:1+trial%7;native->ls[descriptor]=count;
        native->ls[descriptor+1]=(trial&32)?23:16;
        for(unsigned field=0;field<count;++field){
            auto* p=native->ls+descriptor+bias+8+field*8;
            p[0]=(trial+field)&15;p[1]=1+(trial+field)%9;p[2]=3;p[3]=1;
            p[4]=p[1]==6?4:p[1]==2?12:p[1]==1||p[1]==3||p[1]==5?6:3;p[5]=p[6]=p[7]=0;
        }
        actual_data=(trial&64)!=0;recipe=0;copy(*reference,*native);compare(*native,*reference,trial);
    }
    // Actual entry and native seven-family integration retain the original data.
    for(unsigned depth=0;depth<3;++depth){copy(*native,*base);native->host_depth=depth;actual_data=true;copy(*reference,*native);compare(*native,*reference,20000+depth);}
    // Both arms use identical mock callees; late count and saved-frame changes
    // must be observed, never repaired from entry metadata.
    for(unsigned r=1;r<8;++r)for(unsigned depth=0;depth<3;++depth){copy(*native,*base);native->host_depth=depth;recipe=r;copy(*reference,*native);compare(*native,*reference,21000+r*3+depth);}
    recipe=0;
    auto expected_ls=std::make_unique<unsigned char[]>(SPU_LS_SIZE);unsigned frame_models=0,collisions=0,unknown=0;
    for(unsigned path=0;path<2;++path)for(unsigned bias=0;bias<16;++bias)for(unsigned depth=0;depth<3;++depth){
        copy(*native,*base);native->host_depth=depth;native->gpr[1]._u32[0]+=bias;
        native->ls[manager]=(native->ls[manager]&~16u)|(path==0?16:0);
        if(path){native->ls[manager+8]=native->ls[manager+9]=0;}
        const auto stack=native->gpr[1];const auto cursor=spu_ls_read128(native.get(),manager+0x50);
        const auto result=spu_rotqbyi(cursor,path==0?4:8);
        const u128 saved[]={native->gpr[126],native->gpr[80],native->gpr[81],native->gpr[82],stack};
        const unsigned offsets[]={64,16,32,48,128};
        memcpy(expected_ls.get(),native->ls,SPU_LS_SIZE);
        for(unsigned i=0;i<5;++i){const unsigned at=(stack._u32[0]-offsets[i])&0x3fff0;
            for(unsigned lane=0;lane<4;++lane)geometry_job_put32(expected_ls.get()+at+lane*4,saved[i]._u32[lane]);}
        if(path)geometry_job_put32(expected_ls.get()+manager+0x50+((stack._u32[0]+4)&12),cursor._u32[2]);
        copy(*reference,*native);compare(*native,*reference,30000+frame_models);
        assert(!memcmp(native->ls,expected_ls.get(),SPU_LS_SIZE) && !memcmp(&native->gpr[3],&result,16) && !memcmp(&native->gpr[1],&stack,16));++frame_models;
    }
    for(unsigned link:{0u,0x4178u,0x4190u,0x41c0u,0x43d0u,0x4610u,0x4618u,0x52b4u})for(unsigned depth=0;depth<3;++depth){
        copy(*native,*base);native->host_depth=depth;native->gpr[0]._u32[0]=link;actual_data=true;copy(*reference,*native);compare(*native,*reference,31000+collisions);++collisions;
    }
    for(unsigned storage:{0u,255u})for(unsigned bias=0;bias<16;++bias){
        copy(*native,*base);native->gpr[1]._u32[0]+=bias;native->ls[descriptor]=1;native->ls[descriptor+9]=storage;
        copy(*reference,*native);compare(*native,*reference,32000+unknown);++unknown;
    }
    unsigned seven_family=0;
    auto all_types=[&](unsigned stride,unsigned vertices){
        copy(*native,*base);native->ls[manager+8]=vertices>>8;native->ls[manager+9]=vertices;
        native->ls[descriptor]=7;native->ls[descriptor+1]=stride;
        const unsigned widths[]={6,12,6,3,6,4,3};
        for(unsigned i=0;i<7;++i){auto* p=native->ls+descriptor+8+i*8;
            p[0]=i*2;p[1]=i+1;p[2]=3;p[3]=1;p[4]=widths[i];p[5]=p[6]=p[7]=0;}
        actual_data=true;copy(*reference,*native);compare(*native,*reference,33000+seven_family);++seven_family;
    };
    for(unsigned stride=0;stride<64;++stride)for(unsigned vertices:{1u,4u,17u})all_types(stride,vertices);
    for(unsigned stride:{16u,23u,48u})all_types(stride,408);
    copy(*native,*base);native->gpr[4]._u32[0]=0x24000;native->ls[0x24000]=255;native->ls[0x24001]=16;
    for(unsigned i=0;i<255;++i){auto* p=native->ls+0x24008+i*8;p[0]=0;p[1]=255;p[2]=3;p[3]=1;p[4]=4;p[5]=p[6]=p[7]=0;}
    copy(*reference,*native);compare(*native,*reference,34000);
    for(unsigned failure=0;failure<15;++failure){
        copy(*native,*base);switch(failure){
        case 0:native->ls=reference->ls_store;break;case 1:native->int_enable=1;break;case 2:native->irq_saved=1;break;
        case 3:native->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;case 4:native->gpr[3]._u32[0]=0;break;
        case 5:native->gpr[1]._u32[0]=0xe680;break;case 6:native->gpr[4]._u32[0]=native->gpr[1]._u32[0]-64;break;
        case 7:native->gpr[4]._u32[0]=manager;break;case 8:geometry_job_put32(native->ls+0x4460,0x1234);break;
        case 9:geometry_job_put32(native->ls+manager+0x58,manager);break;
        case 10:geometry_job_put32(native->ls+manager+0x58,0);break;
        case 11:native->gpr[3]._u32[0]=SPU_LS_SIZE-16;break;
        case 12:native->gpr[1]._u32[0]=SPU_LS_SIZE;break;
        case 13:native->gpr[4]._u32[0]=SPU_LS_SIZE-16;native->ls[SPU_LS_SIZE-16]=255;break;
        case 14:native->gpr[4]._u32[0]=0;break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryFormatter p{123,456};const char* why=nullptr;
        assert(!read_geometry_formatter(*native,p,&why) && why && p.fields==123 && p.known==456 && !memcmp(native.get(),reference.get(),sizeof(*native)));
        assert(!read_geometry_formatter(*native,p,&why,nullptr,false) && p.fields==123 && p.known==456 && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    for(auto n:native_types)assert(n);
    assert(frame_models==96 && collisions==24 && unknown==32 && seven_family==195 && strict_cases==1908);
    printf("PASS formatter:%u strict full-state/LS/FN cases;%u exact child entries;%u native data roots/all7 types;195 seven-family stride/count cases;255-field skip;96 independent frame models;24 return-PC collisions;32 unknown-storage cases;late count/frame;15 unchanged rejects;standalone only\n",strict_cases,child_cases,native_data);
}
