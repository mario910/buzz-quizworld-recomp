#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_index_expand.h"
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
#include "../analysis/native-geometry/phase3/test-index-expand-reference.inc"
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void write_word(spu_context& c,unsigned at,unsigned value){for(unsigned i=0;i<4;++i)c.ls[at+i]=uint8_t(value>>(24-i*8));}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x7838;uint32_t state=trial+1;
    for(auto& r:c.gpr)for(auto& word:r._u32)word=random_word(state);
    for(unsigned at=0;at<SPU_LS_SIZE;at+=4){const auto word=random_word(state);memcpy(c.ls+at,&word,4);}
    index_expand_oracle::seed_tables(c);
    const unsigned counts[]={1,2,3,4,5,16,31,32,33,64,128,256,1024,4096,6000};
    const unsigned n=counts[trial%15];c.gpr[82]._u32[0]=(n-1)*16+(trial&15);
    c.gpr[92]._u32[0]=0xe800+(trial&15);c.gpr[80]._u32[0]=0;
    c.gpr[81]._u32[0]=0x16002+((trial/16)&15);c.gpr[88]._u32[0]=0x1a000+((trial/256)&15);
    c.gpr[83]=spu_ls_read128(&c,0xd980);c.gpr[85]=spu_ilh(0x303);
    c.gpr[95]._u32[0]=0x3d000;c.gpr[87]._u32[0]=trial%128;c.gpr[102]._u32[0]=trial%32;
    write_word(c,0x3d17c,trial&1?0xe000:0x3ff00);
    if(trial&16){
        for(unsigned at:{0xd950u,0xd9d0u,0xd8a0u,0xd130u,0xd150u,0xd140u}){u128 value;for(auto& w:value._u32)w=random_word(state);spu_ls_write128(&c,at,value);}
        for(unsigned reg:{83u,85u})for(auto& w:c.gpr[reg]._u32)w=random_word(state);
    }
    c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;c.int_enable=trial&1;c.event_mask=trial*13;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;
}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static bool compare(spu_context& native,spu_context& reference,unsigned trial)
{
    GeometryIndexExpand p;const char* why=nullptr;
    if(!read_geometry_index_expand(native,p,&why)){fprintf(stderr,"unexpected expand reject%u %s\n",trial,why);abort();}
    const auto normal=index_expand_oracle::buzz_job_6eba_spu_func_000079A8,error=index_expand_oracle::buzz_job_6eba_spu_func_00007DCC;assert(normal!=error);
    g_spu_trampoline_fn=nullptr;index_expand_oracle::buzz_job_6eba_spu_func_00007838(&reference);
    assert(g_spu_trampoline_fn==index_expand_oracle::buzz_job_6eba_spu_func_000078A8);g_spu_trampoline_fn=nullptr;
    index_expand_oracle::buzz_job_6eba_spu_func_000078A8(&reference);const auto expected=g_spu_trampoline_fn;assert(expected==normal||expected==error);
    g_spu_trampoline_fn=nullptr;expand_geometry_indices(native,p,normal,error);const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"expand trial%u N%u r%d byte%d LS%X/%u state%s\n",trial,p.iterations,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned reg=0;reg<128;++reg)if(memcmp(&native.gpr[reg],&reference.gpr[reg],16)){
            fprintf(stderr,"r%u native",reg);for(auto w:native.gpr[reg]._u32)fprintf(stderr," %08x",w);fprintf(stderr," ref");for(auto w:reference.gpr[reg]._u32)fprintf(stderr," %08x",w);fputc('\n',stderr);
        }abort();
    }
    assert(g_spu_trampoline_fn==expected && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
    return expected==error;
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();unsigned exact=0,errors=0;
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial);copy(*reference,*native);errors+=compare(*native,*reference,trial);++exact;}
    for(unsigned shape=0;shape<8;++shape)for(unsigned trial=0;trial<64;++trial){
        seed(*native,trial);native->gpr[82]._u32[0]=7*16+(trial&15);
        switch(shape){
        case 0:native->gpr[88]._u32[0]=native->gpr[92]._u32[0];break;
        case 1:native->gpr[81]._u32[0]=native->gpr[92]._u32[0]+2;break;
        case 2:native->gpr[95]._u32[0]=(native->gpr[92]._u32[0]&~15u)-0x170;break;
        case 3:native->gpr[88]._u32[0]=SPU_LS_SIZE-16+(trial&15);break;
        case 4:native->gpr[81]._u32[0]=SPU_LS_SIZE-2+(trial&1);break;
        case 5:native->gpr[88]._u32[0]=native->gpr[92]._u32[0]+32;native->gpr[81]._u32[0]=native->gpr[92]._u32[0]+18;break;
        case 6:native->gpr[95]._u32[0]=0xfffffff0;native->gpr[87]._u32[0]=0xffff0000;break;
        case 7:native->gpr[92]._u32[0]=0xffffe800;native->gpr[80]._u32[0]=0x10000;break;
        }
        copy(*reference,*native);errors+=compare(*native,*reference,10000+shape*64+trial);++exact;
    }
    for(unsigned trial=0;trial<16;++trial){seed(*native,trial);native->gpr[82]._u32[0]=6336*16;native->gpr[92]._u32[0]=0xe800+trial;copy(*reference,*native);errors+=compare(*native,*reference,20000+trial);++exact;}
    seed(*native,0);FILE* fixture=fopen("analysis/native-geometry/phase3/index-audit-v1/pc_7838.bin","rb");assert(fixture);
    assert(fread(native->gpr,sizeof native->gpr,1,fixture)==1 && fread(native->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
    copy(*reference,*native);errors+=compare(*native,*reference,30000);++exact;
    // Independent scalar merge: flag1 consumes explicit index, flag0 emits
    // the next sequential index. The caller requires a 16-byte-aligned
    // compressed header: its flag base stays B+2. Vary all explicit-data
    // byte alignments and cross flag-quad boundaries independently.
    for(unsigned alignment=0;alignment<16;++alignment)for(unsigned shape=0;shape<4;++shape){
        seed(*native,0);native->gpr[82]._u32[0]=8*16;native->gpr[81]._u32[0]=0x16002;
        native->gpr[88]._u32[0]=0x1a000+alignment;memset(native->ls+0x16000,0,48);
        const unsigned pattern[]={0,0xffff,0xa55a,0x6db7};
        for(unsigned group=0;group<8;++group){native->ls[0x16008+group*2]=uint8_t(pattern[shape]>>8);native->ls[0x16009+group*2]=uint8_t(pattern[shape]);}
        // SPU loads floor the explicit pointer; scalar byte alignment is
        // repaired by the same address low bits before consuming halfwords.
        for(unsigned i=0;i<160;++i){const unsigned at=0x1a000+alignment+i*2,v=(300+i*37)&65535;native->ls[at]=uint8_t(v>>8);native->ls[at+1]=uint8_t(v);}
        copy(*reference,*native);errors+=compare(*native,*reference,40000+alignment*4+shape);++exact;
        unsigned explicit_index=0,sequential=0;
        for(unsigned i=0;i<128;++i){const unsigned expected=pattern[shape]&(0x8000u>>(i%16))?(300+explicit_index++*37)&65535:sequential++;
            const unsigned value=unsigned(native->ls[0xe800+i*2])<<8|native->ls[0xe801+i*2];
            if(value!=expected){fprintf(stderr,"scalar merge alignment%u shape%u i%u got%u expected%u\n",alignment,shape,i,value,expected);abort();}}
    }
    for(unsigned explicit_values=0;explicit_values<2;++explicit_values){
        seed(*native,0);native->gpr[82]._u32[0]=2*16;native->gpr[87]._u32[0]=0;native->gpr[95]._u32[0]=0xe690;
        memset(native->ls+0x16000,explicit_values?255:0,32);write_word(*native,0xe80c,explicit_values?0x7fffffff:0);
        for(unsigned i=0;i<48;++i){const unsigned at=0x1a000+i*2,v=0xf000+i;native->ls[at]=uint8_t(v>>8);native->ls[at+1]=uint8_t(v);}
        copy(*reference,*native);const bool error=compare(*native,*reference,50000+explicit_values);assert(error==bool(explicit_values));errors+=error;++exact;
    }
    for(unsigned trial=0;trial<7;++trial){seed(*native,0);switch(trial){
        case 0:native->ls=reference->ls_store;break;case 1:native->gpr[92]._u32[0]=0;break;case 2:native->gpr[92]._u32[0]=0xe670;break;
        case 3:native->gpr[92]._u32[0]=SPU_LS_SIZE;break;case 4:native->gpr[82]._u32[0]=0xffffffff;break;
        case 5:native->gpr[92]._u32[0]=SPU_LS_SIZE-16;break;case 6:native->gpr[92]._u32[0]=0xffffffff;break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryIndexExpand p;p.iterations=123;const char* why=nullptr;
        assert(!read_geometry_index_expand(*native,p,&why) && why && p.iterations==123 && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    assert(errors && errors<exact);
    printf("PASS index expansion: %u strict full-state/LS (%u capacity exits);64 independent merges;2 post-write capacity reversals;7 unchanged rejects\n",exact,errors);
}
