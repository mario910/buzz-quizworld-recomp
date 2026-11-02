#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_index_relocate.h"
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
#include "../analysis/native-geometry/phase3/test-index-relocate-reference.inc"
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x79a8;uint32_t state=trial+1;
    for(auto& r:c.gpr)for(auto& word:r._u32)word=random_word(state);
    for(unsigned at=0;at<SPU_LS_SIZE;at+=4){const auto w=random_word(state);memcpy(c.ls+at,&w,4);}
    index_relocate_oracle::seed_tables(c);
    const unsigned bytes[]={0,1,15,16,17,31,32,33,63,64,65,127,256,1023,4096,60000};
    c.gpr[87]._u32[0]=bytes[trial%16];c.gpr[81]._u32[0]=0xe800+(trial&15);
    c.gpr[80]._u32[0]=0x1a000-16+((trial/16)&15);
    c.gpr[83]=spu_ls_read128(&c,0xd980);c.gpr[85]=spu_ilh(0x303);
    if(trial&32){for(unsigned reg:{83u,85u})for(auto& w:c.gpr[reg]._u32)w=random_word(state);}
    c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;c.int_enable=trial&1;c.event_mask=trial*13;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;
}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    GeometryIndexRelocate p;const char* why=nullptr;
    if(!read_geometry_index_relocate(native,p,&why)){fprintf(stderr,"unexpected relocation reject%u %s\n",trial,why);abort();}
    const auto tail=index_relocate_oracle::buzz_job_6eba_spu_func_00007A90;
    g_spu_trampoline_fn=nullptr;index_relocate_oracle::buzz_job_6eba_spu_func_000079A8(&reference);
    assert(g_spu_trampoline_fn==index_relocate_oracle::buzz_job_6eba_spu_func_000079C8);g_spu_trampoline_fn=nullptr;
    index_relocate_oracle::buzz_job_6eba_spu_func_000079C8(&reference);assert(g_spu_trampoline_fn==tail);
    g_spu_trampoline_fn=nullptr;relocate_geometry_index_commands(native,p,tail);const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"relocation trial%u N%u r%d byte%d LS%X/%u state%s\n",trial,p.iterations,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned reg=0;reg<128;++reg)if(memcmp(&native.gpr[reg],&reference.gpr[reg],16)){
            fprintf(stderr,"r%u native",reg);for(auto w:native.gpr[reg]._u32)fprintf(stderr," %08x",w);fprintf(stderr," ref");for(auto w:reference.gpr[reg]._u32)fprintf(stderr," %08x",w);fputc('\n',stderr);
        }abort();
    }
    assert(g_spu_trampoline_fn==tail && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
}
// Independent byte model: commit the cached quad, then fetch a fresh byte
// window from the ALREADY modified LS. This intentionally differs from memcpy.
static void scalar_relocation(unsigned char* ls,unsigned source,unsigned destination,unsigned bytes)
{
    unsigned char cached[16]{};const unsigned n=(bytes+15)/16+1;
    for(unsigned i=0;i<n;++i){
        const unsigned to=(destination+(i?i-1:0)*16)&0x3fff0;
        memcpy(ls+to,cached,16);
        for(unsigned j=0;j<16;++j)cached[j]=ls[(source+i*16+j)&0x3ffff];
    }
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();unsigned exact=0,numeric=0;
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial);copy(*reference,*native);compare(*native,*reference,trial);++exact;}
    for(unsigned shape=0;shape<8;++shape)for(unsigned trial=0;trial<64;++trial){
        seed(*native,trial);native->gpr[87]._u32[0]=17+(trial%48);
        switch(shape){
        case 0:native->gpr[80]._u32[0]=native->gpr[81]._u32[0]-16;break;
        case 1:native->gpr[80]._u32[0]=native->gpr[81]._u32[0]-32;break;
        case 2:native->gpr[80]._u32[0]=native->gpr[81]._u32[0];break;
        case 3:native->gpr[80]._u32[0]=SPU_LS_SIZE-32+(trial&15);break;
        case 4:native->gpr[80]._u32[0]=0xffffffe0+(trial&15);break;
        case 5:native->gpr[81]._u32[0]=SPU_LS_SIZE-80+(trial&15);break;
        case 6:native->gpr[87]._u32[0]=0;native->gpr[81]._u32[0]=SPU_LS_SIZE-16+(trial&15);break;
        case 7:native->gpr[80]._u32[0]=native->gpr[81]._u32[0]+16;break;
        }
        copy(*reference,*native);compare(*native,*reference,10000+shape*64+trial);++exact;
    }
    for(unsigned trial=0;trial<16;++trial){seed(*native,trial);native->gpr[87]._u32[0]=SPU_LS_SIZE-0xe800;copy(*reference,*native);compare(*native,*reference,20000+trial);++exact;}
    seed(*native,0);FILE* fixture=fopen("analysis/native-geometry/phase3/index-audit-v1/pc_79A8.bin","rb");assert(fixture);
    assert(fread(native->gpr,sizeof native->gpr,1,fixture)==1 && fread(native->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
    copy(*reference,*native);compare(*native,*reference,30000);++exact;
    auto expected=std::make_unique<unsigned char[]>(SPU_LS_SIZE);
    for(unsigned align=0;align<16;++align)for(unsigned shape=0;shape<8;++shape)for(unsigned length:{0u,1u,15u,16u,17u,31u,32u,33u,127u}){
        seed(*native,0);const unsigned output=0xe800+((align*7)&15);
        const unsigned sources[]={0x1a000u,output&~15u,(output&~15u)-16,(output&~15u)+16,(output&~15u)+32,0x3fff0u,0xfffffff0u,(output&~15u)-32};
        const unsigned source=sources[shape]+align;
        native->gpr[80]._u32[0]=source-16;native->gpr[81]._u32[0]=output;native->gpr[87]._u32[0]=length;
        memcpy(expected.get(),native->ls,SPU_LS_SIZE);scalar_relocation(expected.get(),source,output,length);
        copy(*reference,*native);compare(*native,*reference,40000+numeric);++exact;++numeric;
        assert(!memcmp(native->ls,expected.get(),SPU_LS_SIZE));
    }
    for(unsigned trial=0;trial<8;++trial){seed(*native,0);switch(trial){
        case 0:native->ls=reference->ls_store;break;case 1:native->gpr[81]._u32[0]=0;break;case 2:native->gpr[81]._u32[0]=0xe67f;break;
        case 3:native->gpr[81]._u32[0]=SPU_LS_SIZE;break;case 4:native->gpr[87]._u32[0]=0xffffffff;break;
        case 5:native->gpr[81]._u32[0]=SPU_LS_SIZE-16;native->gpr[87]._u32[0]=17;break;
        case 6:native->gpr[81]._u32[0]=0xffffffff;break;case 7:native->gpr[87]._u32[0]=0xfffffff0;break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryIndexRelocate p;p.iterations=123;const char* why=nullptr;
        assert(!read_geometry_index_relocate(*native,p,&why) && why && p.iterations==123 && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    printf("PASS index relocation: %u strict full-state/LS; %u independent byte models; 8 unchanged rejects\n",exact,numeric);
}
