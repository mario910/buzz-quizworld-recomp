#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_builtin12_input.h"
#include "../port/native_geometry_context.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
namespace builtin12_input_control_oracle {static void dispatch(spu_context*);}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* c){builtin12_input_control_oracle::dispatch(c);}
void spu_stop(spu_context*){abort();}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_drain_call(spu_context*,uint32_t){abort();}
}
#include "../analysis/native-geometry/phase3/test-builtin12-input-reference.inc"
#include "../analysis/native-geometry/phase3/test-builtin12-input-control-reference.inc"
static unsigned strict_cases=0,seed_word=1,models=0,full_cases=0,child_cases=0;
static unsigned random_word(){seed_word^=seed_word<<13;seed_word^=seed_word>>17;seed_word^=seed_word<<5;return seed_word;}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void equal(const spu_context& a,const spu_context& b,const char* label,unsigned trial){
    const auto d=compare_function_state(a,b);if(!d.exact()){fprintf(stderr,"builtin12 input %s trial%u r%d byte%d LS%X/%u state%s PC%X/%X\n",label,trial,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal",a.pc,b.pc);abort();}
    assert(a.host_depth==b.host_depth && a.image_id==b.image_id && a.policy_mode==b.policy_mode && a.resident_ovl==b.resident_ovl);
}
// Independent integer dyadic position model, including 32-bit wrap and RTZ.
static uint32_t scalar_position(unsigned raw,uint32_t offset){
    const uint32_t wrapped=uint32_t(raw)+offset;const int64_t value=int64_t(wrapped)-((wrapped&0x80000000u)?0x100000000ll:0);
    const uint64_t magnitude=value<0?uint64_t(-value):uint64_t(value);if(!magnitude)return 0;
    unsigned bits=0;for(auto n=magnitude;n;n>>=1)++bits;
    const uint32_t significand=bits>24?uint32_t(magnitude>>(bits-24)):uint32_t(magnitude<<(24-bits));
    return (value<0?0x80000000u:0u)|((bits-1-12+127)<<23)|(significand&0x7fffff);
}
// Independent byte gathers, metadata and ordered stores. Unit arithmetic uses
// SPU primitives deliberately; only the position numeric model is independent.
static void byte_model(const spu_context& c,unsigned groups,unsigned char* result){
    memcpy(result,c.ls,SPU_LS_SIZE);const unsigned manager=c.gpr[3]._u32[0]&~15u,source=c.gpr[4]._u32[0],future=c.gpr[84]._u32[0];
    const unsigned starts[]={geometry_job_be32(result+manager+16),geometry_job_be32(result+manager+20),geometry_job_be32(result+manager+24)};
    const unsigned parameter=c.gpr[5]._u32[0];unsigned offsets[3];for(unsigned i=0;i<3;++i)offsets[i]=geometry_job_be32(result+parameter+i*4);
    auto read=[&](unsigned address,unsigned char* dst,unsigned length){for(unsigned i=0;i<length;++i)dst[i]=result[(address+i)&0x3ffff];};
    auto store=[&](unsigned address,const unsigned char* data){memcpy(result+(address&0x3fff0),data,16);};
    auto put=[&](unsigned address,const u128& vector){unsigned char data[16];for(unsigned i=0;i<4;++i)geometry_job_put32(data+i*4,vector._u32[i]);store(address,data);};
    struct Units {u128 xy[2],z[2];unsigned selectors[2][2];};
    auto unpack=[&](const unsigned char* pair){Units u{};for(unsigned stream=0;stream<2;++stream){
        u128 bits{};for(unsigned row=0;row<2;++row){const auto* p=pair+row*12+6+stream*3;const unsigned packed=(unsigned(p[0])<<16)|(unsigned(p[1])<<8)|p[2];
            u.selectors[stream][row]=(packed&15)*16;bits._u32[row*2]=0x3f800000|((packed>>14)&1023);bits._u32[row*2+1]=0x3f800000|((packed>>4)&1023);}
        u.xy[stream]=spu_fma(c.gpr[72],bits,c.gpr[71]);const auto square=spu_fm(u.xy[stream],u.xy[stream]);
        const auto remaining=spu_fs(spu_ilhu(0x3f80),spu_fa(square,spu_shlqbyi(square,4)));u.z[stream]=spu_fm(remaining,spu_fi(remaining,spu_frsqest(remaining)));
    }return u;};
    unsigned char cache[2][24];read(source,cache[0],24);
    for(unsigned i=0;i<3;++i){result[manager+0x160+i]=i+1;geometry_job_put32(result+manager+0x5c+i*4,starts[i]);}read(future+24,cache[1],24);
    Units previous{};unsigned char tables[2][2][16]{};
    auto rows=[&](unsigned stream,unsigned char (&out)[2][16]){unsigned char xy[16],z[16];for(unsigned i=0;i<4;++i){geometry_job_put32(xy+i*4,previous.xy[stream]._u32[i]);geometry_job_put32(z+i*4,previous.z[stream]._u32[i]);}
        for(unsigned row=0;row<2;++row){unsigned char data[32],controls[16];memcpy(data,z,16);memcpy(data+16,xy,16);
            for(unsigned lane=0;lane<3;++lane)data[lane*4]=(data[lane*4]&127)|(tables[stream][row][lane*4]&128);memcpy(data+12,tables[stream][row]+12,4);
            for(unsigned lane=0;lane<4;++lane)geometry_job_put32(controls+lane*4,lane==3?0x0c0d0e0f:(geometry_job_be32(tables[stream][row]+lane*4)&0x7fffffff)+row*0x08080808);
            for(unsigned byte=0;byte<16;++byte){const unsigned control=controls[byte];out[row][byte]=(control&128)?((control&0xe0)==0xc0?255:(control&0xe0)==0xe0?128:0):data[control&31];}}
    };
    for(unsigned group=0;group<groups;++group){const auto current=unpack(cache[0]);unsigned char pos[2][16],n[2][16],t[2][16];rows(0,n);rows(1,t);
        for(unsigned row=0;row<2;++row){for(unsigned lane=0;lane<3;++lane){const auto* p=cache[0]+row*12+lane*2;geometry_job_put32(pos[row]+lane*4,scalar_position((unsigned(p[0])<<8)|p[1],offsets[lane]));}geometry_job_put32(pos[row]+12,0x3f800000);}
        for(unsigned row=0;row<2;++row)store(starts[0]+group*32+row*16,pos[row]);
        const unsigned delayed=group?uint32_t((group-1)*32):groups*32;
        for(unsigned row=0;row<2;++row)store(starts[1]+delayed+row*16,n[row]);
        for(unsigned row=0;row<2;++row)read((c.gpr[8]._u32[0]+current.selectors[0][row])&0x3fff0,tables[0][row],16);
        for(unsigned row=0;row<2;++row)store(starts[2]+delayed+row*16,t[row]);
        memcpy(cache[0],cache[1],24);read(future+(group+2)*24,cache[1],24);
        for(unsigned row=0;row<2;++row)read((c.gpr[8]._u32[0]+current.selectors[1][row])&0x3fff0,tables[1][row],16);
        previous=current;
    }
    unsigned char n[2][16],t[2][16];rows(0,n);rows(1,t);for(unsigned row=0;row<2;++row)store(starts[1]+(groups-1)*32+row*16,n[row]);for(unsigned row=0;row<2;++row)store(starts[2]+(groups-1)*32+row*16,t[row]);
}
static void compare(spu_context& native,spu_context& reference,unsigned trial,bool independent=false){
    auto before=std::make_unique<spu_context>();memcpy(before.get(),&native,sizeof native);GeometryBuiltin12Input p;const char* why=nullptr;
    if(!read_geometry_builtin12_input(native,p,&why)){fprintf(stderr,"builtin12 input admission trial%u %s\n",trial,why);abort();}
    assert(!memcmp(before.get(),&native,sizeof native));auto expected=std::make_unique<unsigned char[]>(SPU_LS_SIZE);if(independent)byte_model(native,p.groups,expected.get());copy(reference,native);g_spu_trampoline_fn=nullptr;
    builtin12_input_oracle::buzz_job_6eba_spu_func_00009E70(&reference);const auto finish=builtin12_input_oracle::buzz_job_6eba_spu_func_00009498;
    for(unsigned step=0;!(reference.pc==0x9498 && g_spu_trampoline_fn==finish);++step){assert(step<65536 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);}
    g_spu_trampoline_fn=spu_indirect_branch;decode_geometry_builtin12_input(native,p,finish);equal(native,reference,"data",trial);assert(g_spu_trampoline_fn==finish);if(independent){for(unsigned i=0;i<SPU_LS_SIZE;++i)if(native.ls[i]!=expected[i]){fprintf(stderr,"builtin12 byte model trial%u LS%X got%02X expected%02X\n",trial,i,native.ls[i],expected[i]);abort();}++models;}++strict_cases;
}
static void seed(spu_context& c,const spu_context& base,unsigned trial,unsigned groups,unsigned mode){
    copy(c,base);seed_word=trial+1;for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word();for(unsigned reg:{38u,63u,65u,71u,72u,80u,87u})c.gpr[reg]=base.gpr[reg]; c.gpr[1]=spu_ila(0x3f000+(trial&15));c.gpr[5]=c.gpr[34]=spu_ila(0x20000);c.gpr[8]=spu_ila(0xddf0);c.gpr[49]=spu_il(0);
    const unsigned manager=0x3d000;unsigned source=0x18000+(trial&15),a=0x24000+((trial>>4)&15),b=0x28000+((trial>>8)&15),d=0x2c000+((trial>>12)&15);
    switch(mode){case 1:a=source;break;case 2:b=source+16;break;case 3:d=source+40;break;case 4:a=b=d=source+80;break;case 5:b=a+16;d=a+32;break;case 6:source=0x3fff0+(trial&15);break;}
    for(unsigned row=0;row<192;++row)for(unsigned lane=0;lane<4;++lane)geometry_job_put32(c.ls+((source+row*16)&0x3fff0)+lane*4,random_word());
    for(unsigned i=0;i<16;++i)geometry_job_put32(c.ls+0x20000+i*4,random_word());
    memset(c.ls+manager+0x160,mode==7?0x55:0,16);
    for(unsigned slot=0;slot<17;++slot)geometry_job_put32(c.ls+manager+0x10+slot*4,slot==0?a:slot==1?b:slot==2?d:0x30000);
    c.gpr[3]._u32[0]=c.gpr[82]._u32[0]=manager+(trial&15);c.gpr[83]._u32[0]=manager+16;
    c.gpr[4]._u32[0]=c.gpr[84]._u32[0]=source;c.gpr[35]._u32[0]=manager+0x5c+(trial&15);c.gpr[37]._u32[0]=manager+0x60+(trial&15);c.gpr[61]._u32[0]=manager+0x64+(trial&15);c.gpr[39]._u32[0]=manager+0x68+(trial&15);
    if(mode==8)c.gpr[35]._u32[0]=manager+0x14;if(mode==9)c.gpr[35]._u32[0]=manager+0x160;
    c.gpr[36]._u32[0]=groups*32;c.gpr[42]._u32[0]=0u-groups*32;c.pc=0x9e70;c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;
    c.event_status=random_word();c.event_mask=random_word();c.resv_valid=trial&1;c.mfc_tag_status=random_word();c.int_enable=0;c.irq_saved=0;c.irq_frame=nullptr;
}
static void full_run(spu_context& c,bool native){
    builtin12_input_control_oracle::native_data=native?+[](spu_context* c){
        auto* original=clone_function_state(c);builtin12_input_control_oracle::native_data=nullptr;g_spu_trampoline_fn=nullptr;
        builtin12_input_control_oracle::buzz_job_6eba_spu_func_00009E70(original);const auto finish=builtin12_input_control_oracle::buzz_job_6eba_spu_func_00009498;
        for(unsigned step=0;!(original->pc==0x9498 && g_spu_trampoline_fn==finish);++step){assert(step<65536 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(original);}
        GeometryBuiltin12Input p;assert(read_geometry_builtin12_input(*c,p));decode_geometry_builtin12_input(*c,p,finish);equal(*c,*original,"full child",child_cases);assert(g_spu_trampoline_fn==finish);release_function_state(original);++child_cases;
    }:nullptr;
    const unsigned stack=c.gpr[1]._u32[0];g_spu_trampoline_fn=nullptr;builtin12_input_control_oracle::buzz_job_6eba_spu_func_000093F0(&c);
    for(unsigned step=0;!(c.gpr[1]._u32[0]==stack && c.pc==c.gpr[0]._u32[0] && g_spu_trampoline_fn==(c.host_depth?nullptr:spu_indirect_branch));++step){assert(step<65536 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&c);}
}
int main(){
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto base=std::make_unique<spu_context>(),entry=std::make_unique<spu_context>(),native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    memset(base.get(),0,sizeof(*base));base->ls=base->ls_store;base->image_id=41;base->pc=0x93f0;
    auto* file=fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin","rb");assert(file);assert(fread(base->gpr,sizeof base->gpr,1,file)==1 && fread(base->ls,SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF);fclose(file);
    base->gpr[1]=spu_ila(0x3f000);base->gpr[3]=spu_ila(0x3d000);base->gpr[4]=spu_ila(0x18000);base->gpr[5]=spu_ila(0x20000);base->gpr[6]=spu_il(4);base->gpr[7]=spu_il(0);base->gpr[8]=spu_ila(0xddf0);
    memset(base->ls+0x3d000,0,0x190);base->ls[0x3d009]=3;for(unsigned slot=0;slot<17;++slot)geometry_job_put32(base->ls+0x3d010+slot*4,slot==0?0x24000:slot==1?0x28000:slot==2?0x2c000:0x30000);
    copy(*entry,*base);g_spu_trampoline_fn=nullptr;builtin12_input_control_oracle::buzz_job_6eba_spu_func_000093F0(entry.get());
    for(unsigned step=0;!(entry->pc==0x9e70 && g_spu_trampoline_fn==spu_indirect_branch);++step){assert(step<32 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(entry.get());}
    const char* calibration="analysis/native-geometry/phase3/builtin12-input-calibration.bin";file=fopen(calibration,"rb");
    if(file){auto bytes=std::make_unique<unsigned char[]>(sizeof entry->gpr+SPU_LS_SIZE);assert(fread(bytes.get(),sizeof entry->gpr+SPU_LS_SIZE,1,file)==1 && fgetc(file)==EOF && !memcmp(bytes.get(),entry->gpr,sizeof entry->gpr) && !memcmp(bytes.get()+sizeof entry->gpr,entry->ls,SPU_LS_SIZE));fclose(file);}
    else{file=fopen(calibration,"wb");assert(file);assert(fwrite(entry->gpr,sizeof entry->gpr,1,file)==1 && fwrite(entry->ls,SPU_LS_SIZE,1,file)==1);fclose(file);}
    for(unsigned depth=0;depth<3;++depth){copy(*native,*entry);native->host_depth=depth;compare(*native,*reference,strict_cases);}
    for(unsigned trial=0;trial<4096;++trial){seed(*native,*entry,trial,1+trial%64,trial%10);compare(*native,*reference,strict_cases);}
    for(unsigned trial=0;trial<1024;++trial){seed(*native,*entry,5000+trial,1+trial%16,trial%10);
        for(unsigned reg:{38u,63u,65u,71u,72u,80u,87u})for(auto& word:native->gpr[reg]._u32)word=random_word();
        const unsigned masks[]={0xd980,0xd8b0,0xd1d0,0xd1e0,0xd1f0,0xda20,0xd280,0xd290,0xd2a0,0xd200,0xd210,0xd220,0xcd60};for(unsigned index=0;index<13;++index)if(random_word()&1)for(unsigned lane=0;lane<4;++lane)geometry_job_put32(native->ls+masks[index]+lane*4,random_word());
        native->gpr[5]._u32[0]+=trial&15;native->gpr[34]._u32[0]+=(trial>>4)&31;native->gpr[8]._u32[0]=random_word();
        if(trial&32)native->gpr[84]._u32[0]=0x3fff0+(trial&15);if(trial&64)native->gpr[36]._u32[0]+=16*(trial%4);compare(*native,*reference,strict_cases);
    }
    const unsigned special[]={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,0x7f800000,0xff800000,0x7fc00000,0x7f800001,0xff800001,0xffffffff,0x7f7fffff,0xff7fffff};
    for(unsigned trial=0;trial<256;++trial){seed(*native,*entry,7000+trial,1+trial%16,trial%10);for(unsigned reg:{71u,72u})for(unsigned lane=0;lane<4;++lane)native->gpr[reg]._u32[lane]=special[(trial+reg+lane)%16];compare(*native,*reference,strict_cases);}
    for(unsigned groups=1;groups<=16;++groups)for(unsigned bias=0;bias<16;++bias){seed(*native,*entry,10000+groups*16+bias,groups,0);const unsigned manager=0x3d000;
        native->gpr[1]=spu_ila(0x3f000);
        native->gpr[3]._u32[0]=native->gpr[82]._u32[0]=manager;native->gpr[35]._u32[0]=manager+0x5c;native->gpr[37]._u32[0]=manager+0x60;native->gpr[61]._u32[0]=manager+0x64;native->gpr[39]._u32[0]=manager+0x68;
        const unsigned source=groups%4==0?manager+0x30+bias:groups%4==1?manager+0x138+bias:0x18000+bias;
        native->gpr[4]._u32[0]=native->gpr[84]._u32[0]=source;
        const unsigned a=groups%3==0?source+16:0x24000+bias,b=groups%3==1?source+40:groups%3==2?a+16:0x28000+bias,d=groups%3==2?source+80:0x2c000+bias;
        geometry_job_put32(native->ls+manager+16,a);geometry_job_put32(native->ls+manager+20,b);geometry_job_put32(native->ls+manager+24,d);compare(*native,*reference,strict_cases,true);
    }
    for(unsigned bias=0;bias<32;++bias){seed(*native,*entry,30000+bias,1+bias%16,0);native->gpr[82]._u32[0]=0x3d800+(bias&15);memset(native->ls+0x3d960,0,16);compare(*native,*reference,strict_cases);}
    for(unsigned bias=0;bias<32;++bias){seed(*native,*entry,35000+bias,1+bias%16,0);native->gpr[1]=spu_ila(0x3f000);native->gpr[8]._u32[0]=(bias&16?0x28000:0x2c000)+(bias&15);
        native->gpr[3]._u32[0]=native->gpr[82]._u32[0]=0x3d000;native->gpr[35]._u32[0]=0x3d05c;native->gpr[37]._u32[0]=0x3d060;native->gpr[61]._u32[0]=0x3d064;compare(*native,*reference,strict_cases,true);}
    for(unsigned bias=0;bias<64;++bias){seed(*native,*entry,36000+bias,1+bias%16,0);native->gpr[49]._u32[0]=32*(int(bias%7)-3);for(unsigned lane=1;lane<4;++lane)native->gpr[49]._u32[lane]=random_word();compare(*native,*reference,strict_cases);}
    for(unsigned bias=0;bias<32;++bias){seed(*native,*entry,37000+bias,1+bias%16,0);native->gpr[5]._u32[0]=native->gpr[34]._u32[0]=bias&16?0x3d160+(bias&15):native->gpr[4]._u32[0];compare(*native,*reference,strict_cases);}
    unsigned components=0;const unsigned offsets[]={0,1,4095,4096,0x7fff,0xffff,0x7fffffff,0x80000000,0x80000001,0xffff0000,0xfffff000,0xffffffff,0x01000001,0xff000001,0x12345678,0xdeadbeef};
    const auto mask=spu_ls_read128(entry.get(),0xd280),left=spu_ls_read128(entry.get(),0xd2a0),right=spu_ls_read128(entry.get(),0xd290);
    const auto fields=spu_rotm(spu_il(-1),mask);
    for(unsigned raw=0;raw<65536;++raw)for(unsigned offset:offsets){const auto v=numeric_geometry_builtin12_input(spu_il(raw),fields,spu_il(offset),left,right);for(unsigned lane=0;lane<3;++lane){assert(v._u32[lane]==scalar_position(raw,offset));++components;}}
    for(unsigned groups:{1u,2u,3u,4u,5u,127u,128u,129u,512u,514u,2048u}){seed(*native,*entry,20000+groups,groups,0);compare(*native,*reference,strict_cases);}
    for(unsigned count=0;count<=65;++count)for(unsigned bias=0;bias<16;++bias)for(unsigned depth=0;depth<3;++depth){copy(*native,*base);native->ls[0x3d008]=count>>8;native->ls[0x3d009]=count;native->gpr[1]._u32[0]+=bias;native->host_depth=depth;native->gpr[4]._u32[0]+=bias;native->gpr[7]._u32[0]=0x32000+bias;native->gpr[8]._u32[0]=0x12345678+bias;
        geometry_job_put32(native->ls+0x3d010,0x24000+bias);geometry_job_put32(native->ls+0x3d014,0x28000+bias);geometry_job_put32(native->ls+0x3d018,0x2c000+bias);
        copy(*reference,*native);full_run(*reference,false);const auto expected=g_spu_trampoline_fn;full_run(*native,true);equal(*native,*reference,"full93F0",full_cases);assert(g_spu_trampoline_fn==expected);++full_cases;}
    for(unsigned depth=0;depth<3;++depth)for(unsigned bias=0;bias<16;++bias){copy(*native,*base);native->ls[0x3d009]=1;native->gpr[1]._u32[0]+=bias;native->host_depth=depth;
        geometry_job_put32(native->ls+0x3d010,(native->gpr[1]._u32[0]&~15u)-32);geometry_job_put32(native->ls+0x3d014,(native->gpr[1]._u32[0]&~15u)+16);
        copy(*reference,*native);full_run(*reference,false);const auto expected=g_spu_trampoline_fn;full_run(*native,true);equal(*native,*reference,"late frame aliases",full_cases);assert(g_spu_trampoline_fn==expected);++full_cases;}
    for(unsigned failure=0;failure<16;++failure){seed(*native,*entry,40000,17,0);switch(failure){case 0:native->ls=reference->ls_store;break;case 1:native->int_enable=1;break;case 2:native->irq_saved=1;break;case 3:native->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;
        case 4:native->gpr[42]._u32[0]=0;break;case 5:native->gpr[42]._u32[0]=-16u;break;case 6:native->gpr[42]._u32[0]=32;break;case 7:native->gpr[42]._u32[0]=-1048608u;break;
        case 8:native->gpr[35]._u32[0]=0;break;case 9:native->gpr[37]._u32[0]=SPU_LS_SIZE;break;case 10:native->gpr[61]._u32[0]=0;break;
        case 11:native->gpr[3]._u32[0]=SPU_LS_SIZE-0x160;break;case 12:geometry_job_put32(native->ls+0x3d010,0);break;case 13:native->gpr[36]._u32[0]=1048576;native->gpr[42]._u32[0]=-1048576u;break;case 14:native->gpr[49]._u32[0]=0u-0x28000-17*32;break;case 15:native->gpr[49]._u32[0]=0u-0x2c000-17*32;break;}
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryBuiltin12Input p{123};const char* why=nullptr;g_spu_trampoline_fn=spu_indirect_branch;
        assert(!read_geometry_builtin12_input(*native,p,&why) && why && p.groups==123 && !memcmp(native.get(),reference.get(),sizeof(*native)) && g_spu_trampoline_fn==spu_indirect_branch);
    }
    assert(strict_cases==5806 && models==288 && components==3145728 && full_cases==3216 && child_cases==3168);
    printf("PASS builtin12 input:%u strict full-state/LS/FN cases;%u ordered byte models (SPU unit arithmetic);%u independent fixed12 position components;%u full original93F0 cases/%u native strict child roots;16 unchanged rejects;synthetic input4 only\n",strict_cases,models,components,full_cases,child_cases);
}
