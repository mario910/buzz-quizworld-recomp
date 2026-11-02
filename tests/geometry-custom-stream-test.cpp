#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_custom_stream.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*){abort();}
void spu_stop(spu_context*){abort();}
void spu_drain_call(spu_context*,uint32_t){abort();}
void spu_img_restore(spu_context*,int32_t){abort();}
}
#include "../analysis/native-geometry/phase3/test-custom-stream-reference.inc"
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x93f0;c.host_depth=trial%3;
    uint32_t state=trial+1;
    for(auto& r:c.gpr)for(auto& word:r._u32)word=random_word(state);
    for(unsigned i=0;i<SPU_LS_SIZE;++i)c.ls[i]=uint8_t(random_word(state));
    c.gpr[0]._u32[0]=trial&1?0x7df8:0xad34;c.gpr[1]._u32[0]=0x3f000-16*(trial%16);
    c.gpr[3]._u32[0]=0xe800+16*(trial%16);c.gpr[4]._u32[0]=0x18000+trial%16;
    c.gpr[6]._u32[0]=(c.gpr[6]._u32[0]&~255u)|(6+trial%250);
    c.gpr[7]._u32[0]=0x24000+trial%16;c.gpr[8]._u32[0]=0;
    const unsigned counts[]={1,2,3,4,7,8,15,16,255,256,257,1024,32767,32768,65534,65535};
    const unsigned count=counts[(trial/250)%16];const unsigned at=c.gpr[3]._u32[0]+8;
    c.ls[at]=uint8_t(count>>8);c.ls[at+1]=uint8_t(count);
    c.int_enable=trial&1;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    for(unsigned trial=0;trial<5000;++trial){
        seed(*native,trial);memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;
        GeometryCustomStream p;assert(read_geometry_custom_stream(*native,p));
        g_spu_trampoline_fn=nullptr;buzz_job_6eba_spu_func_000093F0(reference.get());
        unsigned steps=0;
        while(g_spu_trampoline_fn!=buzz_job_6eba_spu_func_00007DF8){
            assert(g_spu_trampoline_fn && ++steps<=10);auto fn=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;fn(reference.get());
        }
        assert(reference->pc==0x7df8);g_spu_trampoline_fn=nullptr;
        dispatch_geometry_custom_stream(*native,p,buzz_job_6eba_spu_func_00007DF8);
        const auto d=compare_function_state(*native,*reference);
        if(!d.exact()){
            fprintf(stderr,"customstream%u fmt%u r%d byte%d LS%X/%u state%s\n",trial,p.format,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");abort();
        }
        assert(native->host_depth==reference->host_depth && native->image_id==reference->image_id && g_spu_trampoline_fn==buzz_job_6eba_spu_func_00007DF8);
        assert(native->gpr[6]._u32[0]==0x24000+trial%16 && native->gpr[7]._u32[0]==p.vertices && !native->gpr[8]._u32[0] && !native->gpr[9]._u32[0]);
    }
    for(unsigned fault=0;fault<13;++fault){
        seed(*native,0);switch(fault){
        case 0:native->gpr[3]._u32[0]|=1;break;
        case 1:native->gpr[3]._u32[0]=0;break;
        case 2:native->gpr[3]._u32[0]=SPU_LS_SIZE-16;break;
        case 3:native->gpr[1]._u32[0]|=1;break;
        case 4:native->gpr[1]._u32[0]=0xe680;break;
        case 5:native->gpr[1]._u32[0]=SPU_LS_SIZE-16;break;
        case 6:native->gpr[1]._u32[0]=0xe900;break;
        case 7:native->ls[0xe808]=native->ls[0xe809]=0;break;
        case 8:native->gpr[6]._u32[0]=0;break;
        case 9:native->gpr[6]._u32[0]=5;break;
        case 10:native->gpr[7]._u32[0]=0;break;
        case 11:native->gpr[8]._u32[0]=0x1230;break;
        case 12:native->ls=reference->ls_store;break;
        }
        auto* before=clone_function_state(native.get());GeometryCustomStream p;g_spu_trampoline_fn=nullptr;
        assert(!read_geometry_custom_stream(*native,p) && compare_function_state(*native,*before).exact() && !g_spu_trampoline_fn);
        release_function_state(before);native->ls=native->ls_store;
    }
    puts("Native custom stream:5000 strict full-state/handoff cases +13 unchanged-state/no-I/O rejects PASS");
}
