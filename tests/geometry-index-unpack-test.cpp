#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_index_unpack.h"
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
#include "../analysis/native-geometry/phase3/test-index-unpack-reference.inc"
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.pc=0x74f0;c.image_id=41;
    uint32_t state=trial+1;for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word(state);
    for(unsigned at=0;at<SPU_LS_SIZE;at+=4){const auto word=random_word(state);memcpy(c.ls+at,&word,4);}
    index_unpack_oracle::seed_tables(c);
    const unsigned counts[]={0,1,31,32,33,64,65,1024,32768,65535};
    const unsigned width=trial%16+1,count=counts[(trial/16)%10],groups=count?(count+31)/32:1;
    c.gpr[81]=spu_ilh(width);c.gpr[82]=spu_il(width);c.gpr[90]=spu_il(int(width*0x01010101));
    c.gpr[83]=spu_ls_read128(&c,0xd980);c.gpr[85]=spu_ilh(0x303);c.gpr[99]=spu_ls_read128(&c,0xda30);c.gpr[101]=spu_ila(65535);
    c.gpr[96]._u32[0]=count;c.gpr[97]._u32[0]=(4*width*(groups-1))&65535;c.gpr[80]._u32[0]=(groups-1)*64;
    c.gpr[88]._u32[0]=0xe800+(trial&15);c.gpr[94]._u32[0]=c.gpr[88]._u32[0]+16;
    c.gpr[93]._u32[0]=c.gpr[88]._u32[0]+32;c.gpr[86]._u32[0]=c.gpr[88]._u32[0]+48;
    c.gpr[98]._u32[0]=0x18000+((trial/16)&15);
    if(trial&16){
        for(unsigned at:{0xd110u,0xd8e0u,0xd8f0u,0xd9e0u}){u128 value;for(auto& w:value._u32)w=random_word(state);spu_ls_write128(&c,at,value);}
        for(unsigned reg:{81u,83u,85u,90u,101u})for(auto& w:c.gpr[reg]._u32)w=random_word(state);
        for(unsigned i=1;i<4;++i)c.gpr[82]._u32[i]=random_word(state);
    }
    c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;
    c.int_enable=trial&1;c.event_mask=trial*13;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;
}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    GeometryIndexUnpack p;const char* why=nullptr;
    if(!read_geometry_index_unpack(native,p,&why)){fprintf(stderr,"unexpected unpack reject%u: %s\n",trial,why);abort();}
    const auto deltas=index_unpack_oracle::buzz_job_6eba_spu_func_00007730,expansion=index_unpack_oracle::buzz_job_6eba_spu_func_000077F8;
    assert(deltas!=expansion);
    g_spu_trampoline_fn=nullptr;index_unpack_oracle::buzz_job_6eba_spu_func_000074F0(&reference);
    for(unsigned n=0;g_spu_trampoline_fn!=deltas && g_spu_trampoline_fn!=expansion;++n){assert(g_spu_trampoline_fn && n<5);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);}
    const auto expected=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;unpack_geometry_indices(native,p,deltas,expansion);
    const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"unpack trial%u W%u N%u r%d byte%d LS%X/%u state%s\n",trial,p.width,p.iterations,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned r=0;r<128;++r)if(memcmp(&native.gpr[r],&reference.gpr[r],16)){
            fprintf(stderr,"r%u native",r);for(auto w:native.gpr[r]._u32)fprintf(stderr," %08x",w);
            fprintf(stderr," ref");for(auto w:reference.gpr[r]._u32)fprintf(stderr," %08x",w);fputc('\n',stderr);
        }abort();
    }
    assert(g_spu_trampoline_fn==expected && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();unsigned exact=0;
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial);copy(*reference,*native);compare(*native,*reference,trial);++exact;}
    for(unsigned shape=0;shape<6;++shape)for(unsigned trial=0;trial<128;++trial){
        seed(*native,trial);native->gpr[96]._u32[0]=64;native->gpr[97]._u32[0]=4*native->gpr[82]._u32[0];native->gpr[80]._u32[0]=64;
        switch(shape){
        case 0:native->gpr[98]._u32[0]=native->gpr[88]._u32[0];break;
        case 1:native->gpr[94]._u32[0]=native->gpr[93]._u32[0]=native->gpr[86]._u32[0]=native->gpr[88]._u32[0];break;
        case 2:native->gpr[98]._u32[0]=native->gpr[88]._u32[0]+64;break;
        case 3:spu_ls_write128(native.get(),0xd120,spu_il(0));break;
        case 4:{u128 step=spu_il(16);step._u32[0]=step._u32[1]=0;spu_ls_write128(native.get(),0xd120,step);break;}
        case 5:{spu_ls_write128(native.get(),0xd120,spu_il(0));uint32_t s=trial+3;for(auto& b:native->gpr[99]._u8)b=uint8_t(random_word(s)&31);break;}
        }
        copy(*reference,*native);compare(*native,*reference,10000+shape*128+trial);++exact;
    }
    for(unsigned trial=0;trial<256;++trial){seed(*native,trial);native->gpr[96]._u32[0]=32;native->gpr[97]._u32[0]=0;native->gpr[80]._u32[0]=0;
        native->gpr[98]._u32[0]=SPU_LS_SIZE-1-(trial&15);copy(*reference,*native);compare(*native,*reference,20000+trial);++exact;}
    for(const char* name:{"analysis/native-geometry/phase3/index-audit-v1/pc_74F0.bin","analysis/native-geometry/phase3/index-unpack-v1-high-entry/pc_74F0.bin"}){
        seed(*native,0);FILE* f=fopen(name,"rb");assert(f);assert(fread(native->gpr,sizeof native->gpr,1,f)==1 && fread(native->ls,SPU_LS_SIZE,1,f)==1 && fgetc(f)==EOF);fclose(f);
        copy(*reference,*native);compare(*native,*reference,30000+exact);++exact;
    }
    for(unsigned alignment=0;alignment<16;++alignment)for(unsigned width=1;width<=16;++width){
        seed(*native,width-1);index_unpack_oracle::seed_tables(*native);
        native->gpr[81]=spu_ilh(width);native->gpr[82]=spu_il(width);native->gpr[90]=spu_il(int(width*0x01010101));
        native->gpr[83]=spu_ls_read128(native.get(),0xd980);native->gpr[85]=spu_ilh(0x303);native->gpr[99]=spu_ls_read128(native.get(),0xda30);native->gpr[101]=spu_ila(65535);
        native->gpr[96]._u32[0]=64;native->gpr[97]._u32[0]=4*width;native->gpr[80]._u32[0]=64;
        native->gpr[88]._u32[0]=0xe800;native->gpr[94]._u32[0]=0xe810;native->gpr[93]._u32[0]=0xe820;native->gpr[86]._u32[0]=0xe830;native->gpr[98]._u32[0]=0x18000+alignment;
        memset(native->ls+0x18000,0,144);const unsigned mask=(1u<<width)-1;
        for(unsigned i=0;i<64;++i){const unsigned value=(i*997+0x1234)&mask;
            for(unsigned bit=0;bit<width;++bit){const unsigned at=i*width+bit;native->ls[0x18000+alignment+at/8]|=((value>>(width-1-bit))&1)<<(7-at%8);}}
        copy(*reference,*native);compare(*native,*reference,40000+alignment*16+width);++exact;
        if(width==16){
            // Source compatibility, not ordinary u16 decoding: both shifts
            // are zero, selector32 begins [0,0,1,2], selector33 [0,0,2,3],
            // and SELB takes the first pair's upper half. Selector32 starts
            // every word with byte0: even outputs combine the high byte of
            // this eight-value group's first value with their own high byte.
            // Odd outputs preserve their original full halfword.
            assert(native->gpr[27]._u32[0]==0 && native->gpr[28]._u32[0]==0);
            assert(native->gpr[32]._u32[0]==0x00000102 && native->gpr[33]._u32[0]==0x00000203);
            assert(native->gpr[22]._u32[0]==0x0000ffff && native->gpr[25]._u32[0]==0xffffffff);
        }
        for(unsigned i=0;i<64;++i){const unsigned at=0xe800+i*2,value=unsigned(native->ls[at])<<8|native->ls[at+1];
            const unsigned packed=(i*997+0x1234)&mask;
            const unsigned first=((i&~7u)*997+0x1234)&mask;
            const unsigned expected=width==16 && !(i&1)?(first&0xff00)|(packed>>8):packed;
            if(value!=expected){
                fprintf(stderr,"independent packed numeric W%u i%u got%u expected%u\n",width,i,value,expected);
                for(unsigned n=0;n<32;++n)fprintf(stderr," %u",unsigned(native->ls[0xe800+n*2])<<8|native->ls[0xe801+n*2]);fputc('\n',stderr);
                for(unsigned reg:{25u,27u,28u,32u,33u,39u,40u}){fprintf(stderr,"r%u",reg);for(auto w:native->gpr[reg]._u32)fprintf(stderr," %08x",w);fputc('\n',stderr);}abort();}}
    }
    for(unsigned trial=0;trial<10;++trial){seed(*native,0);switch(trial){
        case 0:native->ls=reference->ls_store;break;case 1:native->gpr[82]._u32[0]=0;break;case 2:native->gpr[82]._u32[0]=17;break;
        case 3:native->gpr[96]._u32[0]=65536;break;case 4:native->gpr[97]._u32[0]=65536;break;
        case 5:native->gpr[88]._u32[0]=0;break;case 6:native->gpr[94]._u32[0]=SPU_LS_SIZE;break;
        case 7:native->gpr[93]._u32[0]=0xe670;break;case 8:native->gpr[86]._u32[0]=0xffffffff;break;
        case 9:native->gpr[88]._u32[0]=SPU_LS_SIZE-16;spu_ls_write128(native.get(),0xd120,spu_il(16));break;}
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryIndexUnpack p;p.iterations=123;const char* why=nullptr;
        assert(!read_geometry_index_unpack(*native,p,&why) && why && p.iterations==123 && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    printf("PASS index unpack: %u strict full-state/LS;240 independent MSB-packed checks +16 width16 source-quirk proofs;10 unchanged rejects\n",exact);
}
