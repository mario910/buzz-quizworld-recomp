#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_builtin24_output.h"
#include "../port/native_geometry_context.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
namespace builtin24_conversion_oracle {static void dispatch(spu_context*);}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* c){builtin24_conversion_oracle::dispatch(c);}
void spu_stop(spu_context*){abort();}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_drain_call(spu_context* c,uint32_t link){
    for(unsigned step=0;!(c->pc==link && (!g_spu_trampoline_fn || g_spu_trampoline_fn==spu_indirect_branch));++step){assert(step<1048576 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);}g_spu_trampoline_fn=nullptr;
}
}
#include "../analysis/native-geometry/phase3/test-builtin24-output-reference.inc"
#include "../analysis/native-geometry/phase3/test-builtin24-conversion-reference.inc"
static unsigned strict_cases=0,models=0,full_cases=0,child_cases=0,seed_word=1;
static unsigned random_word(){seed_word^=seed_word<<13;seed_word^=seed_word>>17;seed_word^=seed_word<<5;return seed_word;}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void equal(const spu_context& a,const spu_context& b,const char* label,unsigned trial){
    const auto d=compare_function_state(a,b);if(!d.exact()){fprintf(stderr,"builtin 24-byte SNORM %s trial%u r%d byte%d LS%X/%u state%s PC%X/%X\n",label,trial,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal",a.pc,b.pc);abort();}
    assert(a.host_depth==b.host_depth && a.image_id==b.image_id && a.policy_mode==b.policy_mode && a.resident_ovl==b.resident_ovl);
}
// Independent exact dyadic-grid model of CMP attributes. Results
// have <=24 significant bits, so FMA rounding is exact on this grid.
static uint32_t scalar_component(int n,unsigned component){
    const int64_t numerator=int64_t(n)*(component==2?1023:2047)-2048;
    const int64_t value=numerator*(component==2?1024:512);
    return value>2147483647ll?0x7fffffff:value<(-2147483647ll-1)?0x80000000u:uint32_t(int32_t(value));
}
static uint32_t scalar_cmp(int x,int y,int z){return ((scalar_component(x,0)>>21)&2047)|(((scalar_component(y,1)>>21)&2047)<<11)|(scalar_component(z,2)&0xffc00000u);}
static int grid_value(const unsigned char* row,unsigned word){const uint32_t bits=geometry_job_be32(row+word*4);float f;memcpy(&f,&bits,4);return int(f*2048.0f);}
static uint32_t scalar_snorm32(int n){
    const int64_t numerator=int64_t(n)*65535-2048;uint64_t magnitude=numerator<0?uint64_t(-numerator):uint64_t(numerator);
    unsigned bits=0;for(auto value=magnitude;value;value>>=1)++bits;
    if(bits>24)magnitude=(magnitude>>(bits-24))<<(bits-24);
    const int64_t value=(numerator<0?-int64_t(magnitude):int64_t(magnitude))*16;
    return value>2147483647ll?0x7fffffff:value<(-2147483647ll-1)?0x80000000u:uint32_t(int32_t(value));
}
// Independent byte model: raw XYZ, one CMP and four high SNORM16 halfwords.
// Raw XYZ may alias output; quantized streams stay nonaliased exact-grid data.
// Initial three-pair prefetch and fresh post-store reads preserve cache age.
static void byte_model(const spu_context& c,unsigned groups,unsigned char* result){
    memcpy(result,c.ls,SPU_LS_SIZE);const unsigned manager=c.gpr[81]._u32[0]&~15u;
    const unsigned starts[]={geometry_job_be32(result+manager+20),geometry_job_be32(result+manager+24),geometry_job_be32(result+manager+16)};
    unsigned char cache[3][3][2][16];uint32_t cmp[2][2],snorm[2][2][4];
    auto read=[&](unsigned index,unsigned slot){for(unsigned stream=0;stream<3;++stream)for(unsigned row=0;row<2;++row)memcpy(cache[slot][stream][row],result+((starts[stream]+index*32+row*16)&0x3fff0),16);};
    auto encode=[&](unsigned slot,uint32_t* packed,uint32_t half[2][4]){for(unsigned row=0;row<2;++row){const auto* bytes=cache[slot][0][row];packed[row]=scalar_cmp(grid_value(bytes,0),grid_value(bytes,1),grid_value(bytes,2));for(unsigned lane=0;lane<4;++lane)half[row][lane]=scalar_snorm32(grid_value(cache[slot][1][row],lane));}};
    for(unsigned pair=0;pair<3;++pair)read(pair,pair);encode(0,cmp[0],snorm[0]);encode(1,cmp[1],snorm[1]);unsigned output=c.gpr[82]._u32[0];
    for(unsigned group=0;group<groups;++group){
        uint32_t next_cmp[2],next_snorm[2][4];encode(2,next_cmp,next_snorm);unsigned char packed[48];
        for(unsigned row=0;row<2;++row){auto* vertex=packed+row*24;memcpy(vertex,cache[0][2][row],12);geometry_job_put32(vertex+12,cmp[0][row]);
            for(unsigned lane=0;lane<4;++lane){const uint16_t value=uint16_t(snorm[0][row][lane]>>16);vertex[16+lane*2]=value>>8;vertex[17+lane*2]=value;}}
        for(unsigned offset:{32u,16u,0u})memcpy(result+((output+offset)&0x3fff0),packed+offset,16);
        memcpy(cache[0],cache[1],sizeof cache[0]);memcpy(cache[1],cache[2],sizeof cache[1]);read(group+3,2);
        memcpy(cmp[0],cmp[1],sizeof cmp[0]);memcpy(cmp[1],next_cmp,sizeof cmp[1]);memcpy(snorm[0],snorm[1],sizeof snorm[0]);memcpy(snorm[1],next_snorm,sizeof snorm[1]);output+=48;
    }
}
static void compare(spu_context& native,spu_context& reference,unsigned trial,bool independent=false){
    auto before=std::make_unique<spu_context>();memcpy(before.get(),&native,sizeof native);GeometryBuiltin24Output plan;const char* why=nullptr;
    if(!read_geometry_builtin24_output(native,plan,&why)){fprintf(stderr,"builtin 24-byte SNORM admission trial%u: %s\n",trial,why);abort();}
    assert(!memcmp(before.get(),&native,sizeof native));auto bytes=std::make_unique<unsigned char[]>(SPU_LS_SIZE);
    if(independent)byte_model(native,plan.groups,bytes.get());copy(reference,native);g_spu_trampoline_fn=nullptr;
    builtin24_output_oracle::buzz_job_6eba_spu_func_00005760(&reference);
    const auto finish=builtin24_output_oracle::buzz_job_6eba_spu_func_00005648;
    for(unsigned step=0;!(reference.pc==0x5648 && g_spu_trampoline_fn==finish);++step){assert(step<65536 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);}
    g_spu_trampoline_fn=spu_indirect_branch;emit_geometry_builtin24_output(native,plan,finish);equal(native,reference,"data",trial);assert(g_spu_trampoline_fn==finish);
    if(independent){if(memcmp(native.ls,bytes.get(),SPU_LS_SIZE)){fprintf(stderr,"builtin24 independent byte model trial%u\n",trial);abort();}++models;}++strict_cases;
}
static void seed(spu_context& c,const spu_context& base,unsigned trial,unsigned vertices,unsigned mode){
    copy(c,base);seed_word=trial+1;
    for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word();
    for(unsigned reg:{27u,30u,31u,32u,33u,35u,41u,42u,44u,48u})c.gpr[reg]=base.gpr[reg];
    unsigned raw=0x18000+((trial>>4)&15),a=0x1c000+((trial>>8)&15),b=0x20000+((trial>>12)&15),output=0x28000+(trial&15),manager=0x3d000+(trial&15);
    switch(mode){case 1:output=raw;break;case 2:output=raw+16;break;case 3:output=raw+48;break;case 4:output=a;break;case 5:output=a+32;break;case 6:output=b+64;break;case 7:a=b=raw;output=raw+80;break;case 8:a=0x3fff0+(trial&15);break;}
    for(unsigned stream:{raw,a,b})for(unsigned row=0;row<96;++row)for(unsigned lane=0;lane<4;++lane)geometry_job_put32(c.ls+((stream+row*16)&0x3fff0)+lane*4,random_word());
    const unsigned m=manager&~15u;memset(c.ls+m+0x160,0,16);c.ls[m+0x160]=1;c.ls[m+0x161]=2;c.ls[m+0x162]=3;c.ls[m+0x163]=4;
    geometry_job_put32(c.ls+m+16,raw);geometry_job_put32(c.ls+m+20,a);geometry_job_put32(c.ls+m+24,b);geometry_job_put32(c.ls+m+28,raw);
    geometry_job_put32(c.ls+m+0x50,raw);geometry_job_put32(c.ls+m+0x54,a);geometry_job_put32(c.ls+m+0x58,b);geometry_job_put32(c.ls+m+0x5c,raw);
    if(mode==9)memset(c.ls+m+0x160,0,16);
    c.gpr[81]._u32[0]=manager;c.gpr[82]._u32[0]=output;c.gpr[29]._u32[0]=vertices;c.gpr[28]=spu_ai(c.gpr[81],16);
    c.pc=0x5760;c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;
    c.event_mask=random_word();c.event_status=random_word();c.mfc_tag_status=random_word();c.resv_valid=trial&1;c.int_enable=0;c.irq_saved=0;c.irq_frame=nullptr;
}
static void full_run(spu_context& c,bool native){
    builtin24_conversion_oracle::native_data=native?+[](spu_context* c){
        auto* original=clone_function_state(c);builtin24_conversion_oracle::native_data=nullptr;g_spu_trampoline_fn=nullptr;
        builtin24_conversion_oracle::buzz_job_6eba_spu_func_00005760(original);
        const auto finish=builtin24_conversion_oracle::buzz_job_6eba_spu_func_00005648;
        for(unsigned step=0;!(original->pc==0x5648 && g_spu_trampoline_fn==finish);++step){assert(step<65536 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(original);}
        GeometryBuiltin24Output plan;assert(read_geometry_builtin24_output(*c,plan));emit_geometry_builtin24_output(*c,plan,finish);equal(*c,*original,"full child",child_cases);assert(g_spu_trampoline_fn==finish);release_function_state(original);++child_cases;
    }:nullptr;
    const unsigned stack=c.gpr[1]._u32[0];g_spu_trampoline_fn=nullptr;builtin24_conversion_oracle::buzz_job_6eba_spu_func_000054E8(&c);
    for(unsigned step=0;!(c.gpr[1]._u32[0]==stack && c.pc==c.gpr[0]._u32[0] && g_spu_trampoline_fn==(c.host_depth?nullptr:spu_indirect_branch));++step){assert(step<1048576 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&c);}
}
int main(){
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto base=std::make_unique<spu_context>(),entry=std::make_unique<spu_context>(),native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    memset(base.get(),0,sizeof(*base));base->ls=base->ls_store;base->pc=0x54e8;base->image_id=41;
    auto* file=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin","rb");assert(file);assert(fread(base->gpr,sizeof base->gpr,1,file)==1 && fread(base->ls,SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF);fclose(file);
    const unsigned manager=base->gpr[3]._u32[0]&~15u;copy(*entry,*base);entry->ls[manager+4]=2;
    g_spu_trampoline_fn=nullptr;builtin24_conversion_oracle::buzz_job_6eba_spu_func_000054E8(entry.get());
    for(unsigned steps=0;!(entry->pc==0x5760 && g_spu_trampoline_fn==spu_indirect_branch);++steps){assert(steps<32 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(entry.get());}
    // Explicit synthetic format mutation of a real controller input; not a
    // captured format-2 root. Save only the reproducible GPR/LS anchor.
    const char* calibration="analysis/native-geometry/phase3/builtin24-output-calibration.bin";
    file=fopen(calibration,"rb");if(file){auto saved=std::make_unique<unsigned char[]>(sizeof entry->gpr+SPU_LS_SIZE);assert(fread(saved.get(),sizeof entry->gpr+SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF);assert(!memcmp(saved.get(),entry->gpr,sizeof entry->gpr) && !memcmp(saved.get()+sizeof entry->gpr,entry->ls,SPU_LS_SIZE));fclose(file);}
    else{file=fopen(calibration,"wb");assert(file);assert(fwrite(entry->gpr,sizeof entry->gpr,1,file)==1 && fwrite(entry->ls,SPU_LS_SIZE,1,file)==1);fclose(file);}
    for(unsigned depth=0;depth<3;++depth){copy(*native,*entry);native->host_depth=depth;compare(*native,*reference,strict_cases);}
    for(unsigned trial=0;trial<4096;++trial){seed(*native,*entry,trial,2*(1+trial%64),trial%10);compare(*native,*reference,strict_cases);}
    for(unsigned trial=0;trial<1024;++trial){seed(*native,*entry,5000+trial,2*(1+trial%16),trial%10);
        for(unsigned reg:{27u,30u,31u,32u,33u,35u,41u,42u,44u,48u})for(auto& word:native->gpr[reg]._u32)word=random_word();
        if(trial&1)for(unsigned lane=0;lane<4;++lane)geometry_job_put32(native->ls+0xcf20+lane*4,random_word());
        if(trial&2)for(unsigned lane=0;lane<4;++lane)geometry_job_put32(native->ls+0xd9b0+lane*4,random_word());compare(*native,*reference,strict_cases);
    }
    const unsigned special[]={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,0x7f800000,0xff800000,0x7fc00000,0x7f800001,0xff800001,0xffffffff,0x7f7fffff,0xff7fffff};
    for(unsigned trial=0;trial<256;++trial){seed(*native,*entry,7000+trial,2*(1+trial%16),0);native->gpr[81]._u32[0]=0x3d000;native->gpr[28]=spu_ila(0x3d010);
        for(unsigned stream:{0x18000u,0x1c000u,0x20000u})for(unsigned row=0;row<96;++row)for(unsigned lane=0;lane<4;++lane)geometry_job_put32(native->ls+stream+row*16+lane*4,special[(row*4+lane+trial)%16]);
        geometry_job_put32(native->ls+0x3d010,0x18000);geometry_job_put32(native->ls+0x3d014,0x1c000);geometry_job_put32(native->ls+0x3d018,0x20000);compare(*native,*reference,strict_cases);
    }
    for(unsigned groups=1;groups<=16;++groups)for(unsigned bias=0;bias<16;++bias){
        seed(*native,*entry,10000+groups*16+bias,groups*2,0);native->gpr[81]._u32[0]=0x3d000;native->gpr[28]=spu_ila(0x3d010);
        geometry_job_put32(native->ls+0x3d010,0x18000);geometry_job_put32(native->ls+0x3d014,0x1c000);geometry_job_put32(native->ls+0x3d018,0x20000);
        const unsigned offsets[]={0,16,48,64,80};native->gpr[82]._u32[0]=0x18000+offsets[groups%5]+bias;
        for(unsigned stream:{0x1c000u,0x20000u})for(unsigned row=0;row<96;++row)for(unsigned lane=0;lane<4;++lane){const int n=int((row*4+lane+groups*31+bias+(stream>>12)*193)%8193)-4096;const float f=float(n)/2048.0f;uint32_t bits;memcpy(&bits,&f,4);geometry_job_put32(native->ls+stream+row*16+lane*4,bits);}
        compare(*native,*reference,strict_cases,true);
    }
    for(unsigned vertices:{2u,4u,6u,8u,10u,254u,256u,258u,1024u,1026u,8462u,8464u}){seed(*native,*entry,9000+vertices,vertices,0);native->gpr[82]._u32[0]=0xe680;compare(*native,*reference,strict_cases);}
    for(unsigned bias=0;bias<16;++bias){seed(*native,*entry,30000+bias,34,0);native->gpr[81]._u32[0]=0x3ffb0+bias;native->gpr[28]=spu_ai(native->gpr[81],16);memset(native->ls+0x110,0,16);geometry_job_put32(native->ls,0x18000);geometry_job_put32(native->ls+4,0x1c000);geometry_job_put32(native->ls+8,0x20000);geometry_job_put32(native->ls+12,0x18000);compare(*native,*reference,strict_cases);}
    unsigned patterns=0,snorm_components=0;
    copy(*native,*entry);native->gpr[18]=spu_ls_read128(native.get(),0xcf20);
    for(int n=-4096;n<=4096;++n){int values[4][4];u128 input[4];for(unsigned i=0;i<4;++i)for(unsigned component=0;component<4;++component){values[i][component]=((n+4096+int(i*1237+component*997))%8193)-4096;input[i]._f32[component]=float(values[i][component])/2048.0f;}
        for(unsigned pair=0;pair<2;++pair){
            const auto cmp=pack_geometry_builtin24_cmp_pair(native->gpr,input[pair*2],input[pair*2+1]);const auto snorm=pack_geometry_builtin24_snorm_pair(native->gpr,input[pair*2],input[pair*2+1]);
            for(unsigned row=0;row<2;++row){const auto i=pair*2+row;assert(cmp.packed._u32[row]==scalar_cmp(values[i][0],values[i][1],values[i][2]));++patterns;
                for(unsigned lane=0;lane<4;++lane){assert(snorm.integer[row]._u32[lane]==scalar_snorm32(values[i][lane]));++snorm_components;}}
        }
    }
    // Full original controller/getter remain on both arms. Only format-2 data
    // is native. Compare child and full exit, including late outer-frame loads.
    for(unsigned count=1;count<=65;++count)for(unsigned bias:{0u,4u,8u,12u})for(unsigned depth=0;depth<3;++depth){
        copy(*native,*base);native->ls[manager+4]=2;native->ls[manager+8]=count>>8;native->ls[manager+9]=count;native->host_depth=depth;native->gpr[1]._u32[0]+=bias;
        geometry_job_put32(native->ls+manager+0x58,0x24000+bias);copy(*reference,*native);full_run(*reference,false);const auto expected=g_spu_trampoline_fn;full_run(*native,true);equal(*native,*reference,"full conversion",full_cases);assert(g_spu_trampoline_fn==expected);++full_cases;
    }
    for(unsigned depth=0;depth<3;++depth)for(unsigned bias=0;bias<16;++bias){copy(*native,*base);native->ls[manager+4]=2;native->ls[manager+8]=0;native->ls[manager+9]=1;native->host_depth=depth;native->gpr[1]._u32[0]+=bias;
        geometry_job_put32(native->ls+manager+0x58,(native->gpr[1]._u32[0]&~15u)-64);copy(*reference,*native);full_run(*reference,false);const auto expected=g_spu_trampoline_fn;full_run(*native,true);equal(*native,*reference,"late frame alias",full_cases);assert(g_spu_trampoline_fn==expected);++full_cases;}
    for(unsigned failure=0;failure<12;++failure){seed(*native,*entry,40000,34,0);switch(failure){case 0:native->ls=reference->ls_store;break;case 1:native->int_enable=1;break;case 2:native->irq_saved=1;break;case 3:native->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;case 4:native->gpr[29]._u32[0]=0;break;case 5:native->gpr[29]._u32[0]=1;break;case 6:native->gpr[29]._u32[0]=3;break;case 7:native->gpr[29]._u32[0]=65538;break;case 8:native->gpr[82]._u32[0]=0;break;case 9:native->gpr[29]._u32[0]=8466;native->gpr[82]._u32[0]=0xe680;break;case 10:native->gpr[82]._u32[0]=SPU_LS_SIZE-16;break;case 11:native->gpr[29]._u32[0]=65536;native->gpr[82]._u32[0]=0xe680;break;}
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryBuiltin24Output plan{123,456};const char* why=nullptr;
        assert(!read_geometry_builtin24_output(*native,plan,&why) && why && plan.vertices==123 && plan.groups==456 && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    assert(strict_cases==5663 && models==256 && full_cases==828 && child_cases==828 && patterns==32772 && snorm_components==131088);
    printf("PASS builtin24:%u strict full-state/LS/FN cases;%u independent ordered byte models;%u independent dyadic CMP encodings;%u independent full-SNORM32 components;1024 arbitrary-control cases;256 special-FP cases;%u full original-controller cases/%u native strict child roots;48 late outer-frame aliases;12 unchanged rejects;synthetic cold format2 only\n",strict_cases,models,patterns,snorm_components,full_cases,child_cases);
}
