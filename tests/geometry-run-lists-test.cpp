#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_run_lists.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*) { abort(); }
void spu_stop(spu_context*) { abort(); }
void spu_drain_call(spu_context*,uint32_t) { abort(); }
void spu_img_restore(spu_context*,int32_t) { abort(); }
}
#include "../analysis/native-geometry/phase3/test-run-lists-reference.inc"
static uint8_t fixture[264192];
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void put16(uint8_t* out,unsigned value){out[0]=uint8_t(value>>8);out[1]=uint8_t(value);}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof(c));c.ls=c.ls_store;c.image_id=41;c.pc=trial&1?0x1018:0x7118;c.host_depth=trial&1;
    uint32_t state=trial+1;
    for(auto& r:c.gpr)for(auto& word:r._u32)word=random_word(state);
    for(unsigned i=0;i<SPU_LS_SIZE;++i)c.ls[i]=uint8_t(random_word(state));
    memcpy(c.ls,fixture+2048,0xe680);
    const unsigned counts[]={0,1,2,3,4,5,7,8,9,15,16,17,31,32,63,64,127,128,257};
    const unsigned count=counts[(trial/4)%19],base=0x10000+((trial/76)%4)*0x4000;
    c.gpr[0]._u32[0]=trial&2?0x7298:0x1098;c.gpr[3]._u32[0]=base;c.gpr[4]._u32[0]=count*2;
    unsigned vertices=0;
    for(unsigned i=0;i<count;++i){
        const unsigned len=1+(trial/304)%32,start=(random_word(state)>>5)&2047;
        const auto code=uint16_t((start<<5)|(len-1));put16(c.ls+base+i*2,code);vertices+=len;
    }
    c.gpr[5]._u32[0]=vertices;c.int_enable=trial&1;c.mfc_tag_status=trial*7;c.resv_valid=trial&1;
}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    memcpy(&reference,&native,sizeof(native));reference.ls=reference.ls_store;
    GeometryRunList p;const char* why=nullptr;
    if(!read_geometry_run_list(native,p,&why)){fprintf(stderr,"run-list rejected trial%u:%s\n",trial,why);abort();}
    g_spu_trampoline_fn=nullptr;buzz_job_6eba_spu_func_00007118(&reference);
    if(g_spu_trampoline_fn==buzz_job_6eba_spu_func_00007298){g_spu_trampoline_fn=nullptr;buzz_job_6eba_spu_func_00007298(&reference);}
    const auto expected_next=g_spu_trampoline_fn;
    g_spu_trampoline_fn=nullptr;expand_geometry_run_list(native,p);
    const auto diff=compare_function_state(native,reference);
    if(!diff.exact() || g_spu_trampoline_fn!=expected_next){
        fprintf(stderr,"run-list trial%u codes%zu:r%d byte%d LS%X/%u state%s\n",trial,p.codes.size(),diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
        if(diff.first_register>=0)for(unsigned i=0;i<4;++i)fprintf(stderr,"lane%u %08X/%08X\n",i,native.gpr[diff.first_register]._u32[i],reference.gpr[diff.first_register]._u32[i]);
        abort();
    }
    assert(native.host_depth==reference.host_depth && native.image_id==reference.image_id);
    // Independent format expectation: consecutive scaled offsets per control.
    unsigned out=0;
    for(const auto code:p.codes)for(unsigned j=0;j<(code&31)+1;++j){
        const unsigned expected=((code>>5)+j)*16;
        assert(geometry_job_be16(native.ls+p.base+out*2)==expected);++out;
    }
    assert(out==p.vertices || !p.bytes);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto* file=fopen("analysis/native-geometry/phase3/test-run-lists-fixture.bin","rb");assert(file);
    assert(fread(fixture,1,sizeof fixture,file)==sizeof fixture);fclose(file);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    for(unsigned trial=0;trial<9728;++trial){seed(*native,trial);compare(*native,*reference,trial);}
    for(unsigned trial=0;trial<1024;++trial){
        seed(*native,trial);uint32_t state=trial+73;const unsigned count=native->gpr[4]._u32[0]/2,base=native->gpr[3]._u32[0];
        unsigned vertices=0;
        for(unsigned i=0;i<count;++i){const auto code=uint16_t(random_word(state));put16(native->ls+base+i*2,code);vertices+=(code&31)+1;}
        native->gpr[5]._u32[0]=vertices;compare(*native,*reference,9728+trial);
    }
    for(unsigned extra=0;extra<4;++extra){
        seed(*native,8);const unsigned count=extra==3?3:extra==2?4096:extra==1?1024:512;
        const unsigned base=extra==3?SPU_LS_SIZE-32:0x10000;
        native->gpr[3]._u32[0]=base;native->gpr[4]._u32[0]=count*2;
        unsigned vertices=0;
        for(unsigned i=0;i<count;++i){const unsigned length=extra==3?1:1+i%32;put16(native->ls+base+i*2,((i%2048)<<5)|(length-1));vertices+=length;}
        native->gpr[5]._u32[0]=vertices;compare(*native,*reference,10752+extra);
    }
    for(unsigned batch=0;batch<16;++batch){
        seed(*native,8);native->gpr[3]._u32[0]=0x10000;native->gpr[4]._u32[0]=8192;
        unsigned vertices=0;
        for(unsigned i=0;i<4096;++i){const unsigned code=batch*4096+i;put16(native->ls+0x10000+i*2,code);vertices+=(code&31)+1;}
        native->gpr[5]._u32[0]=vertices;compare(*native,*reference,10756+batch);
    }
    for(unsigned empty=0;empty<2;++empty){
        seed(*native,empty);native->gpr[3]._u32[0]=UINT32_MAX;native->gpr[4]._u32[0]=0;
        native->gpr[5]._u32[0]=UINT32_MAX;memset(native->ls+0xd080,0,0x90);compare(*native,*reference,10772+empty);
    }
    for(unsigned fault=0;fault<17;++fault){
        seed(*native,8);const unsigned base=native->gpr[3]._u32[0];
        if(fault<9)native->ls[0xd080+fault*16+15]^=1;
        else switch(fault){
        case 9:native->ls[0xd980+15]^=1;break;
        case 10:native->gpr[3]._u32[0]|=1;break;
        case 11:native->gpr[3]._u32[0]=0xd080;break;
        case 12:native->gpr[4]._u32[0]|=1;break;
        case 13:native->gpr[4]._u32[0]=SPU_LS_SIZE;break;
        case 14:native->gpr[5]._u32[0]=UINT32_MAX;break;
        case 15:++native->gpr[5]._u32[0];break;
        case 16:native->ls=reference->ls_store;break;
        }
        auto* before=clone_function_state(native.get());GeometryRunList p;g_spu_trampoline_fn=nullptr;
        assert(!read_geometry_run_list(*native,p));
        assert(compare_function_state(*native,*before).exact() && !g_spu_trampoline_fn);release_function_state(before);native->ls=native->ls_store;
    }
    printf("Native run lists: 10774 strict root + independent numeric cases (all65536 control encodings),17 unchanged/no-I/O rejects PASS\n");
}
