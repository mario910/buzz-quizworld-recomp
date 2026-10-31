#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_skin_dispatch.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*){abort();}
void spu_stop(spu_context*){abort();}
void spu_drain_call(spu_context* ctx,uint32_t link){assert(!g_spu_trampoline_fn && ctx->pc==link);}
void spu_img_restore(spu_context* ctx,int32_t image){assert(ctx->image_id==image);}
}
struct Call { uint32_t pc,entry,depth; u128 arguments[9]; };
static std::vector<Call> calls;
static unsigned trial;
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void skin_dispatch_model(spu_context* ctx,uint32_t pc)
{
    Call call{};call.pc=pc;call.entry=ctx->pc;call.depth=ctx->host_depth;
    memcpy(call.arguments,ctx->gpr+3,sizeof(call.arguments));calls.push_back(call);
    // Include ALL argument lanes, link and entry PC in the callee's effects.
    uint32_t state=pc^ctx->pc^ctx->host_depth;
    for(unsigned reg=3;reg<=11;++reg)for(auto word:ctx->gpr[reg]._u32)state=(state^word)*16777619;
    for(unsigned reg=2;reg<80;++reg)for(auto& word:ctx->gpr[reg]._u32)word=random_word(state);
    spu_ls_write128(ctx,0x30000+16*(calls.size()%64),ctx->gpr[12]);
    if(pc==0x2750)ctx->gpr[3]._u32[0]=0x18000+(trial&15);
    if(pc==0x2778)ctx->gpr[3]._u32[0]=(trial&2)?0x19000+(trial&15):0;
    if(pc==0x2780)ctx->gpr[3]._u32[0]=(trial&4)?0x1a000+(trial&15):0;
    ctx->pc=ctx->gpr[0]._u32[0];
}
#include "../analysis/native-geometry/phase3/test-skin-dispatch-reference.inc"
static void seed(spu_context& c)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x9f8;c.host_depth=(trial/16)%3;
    uint32_t state=trial+1;for(auto& r:c.gpr)for(auto& word:r._u32)word=random_word(state);
    for(unsigned i=0;i<SPU_LS_SIZE;++i)c.ls[i]=uint8_t(random_word(state));
    const unsigned returns[]={0x9f8,0xaf18,0x2c00,0xb48,0x1234};c.gpr[0]._u32[0]=returns[(trial/48)%5];
    c.gpr[1]._u32[0]=0x3f000-16*((trial/7)%16);c.gpr[3]._u32[0]=0xe800+16*((trial/3)%16);
    c.gpr[9]._u32[0]=(c.gpr[9]._u32[0]&~255u)|((trial&1)?(1+(trial/8)%255):0);
    const unsigned flags[]={0,1,2,255,256,257,0xabcdef01,0x12340100};c.gpr[10]._u32[0]=flags[(trial/8)%8];
    c.int_enable=trial&1;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;c.policy_mode=trial%3;c.resident_ovl=trial%5;
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    unsigned weighted=0,returned=0,tangent=0,normal=0;
    const auto& f=skin_dispatch_oracle::library;
    for(trial=0;trial<6144;++trial){
        seed(*native);memcpy(reference.get(),native.get(),sizeof(*native));reference->ls=reference->ls_store;
        GeometrySkinDispatch p;assert(read_geometry_skin_dispatch(*native,p));calls.clear();g_spu_trampoline_fn=nullptr;
        skin_dispatch_oracle::buzz_job_6eba_spu_func_000009F8(reference.get());
        for(unsigned steps=0;g_spu_trampoline_fn && g_spu_trampoline_fn!=f.weighted && g_spu_trampoline_fn!=f.normalize && g_spu_trampoline_fn!=spu_indirect_branch;++steps){
            assert(steps<16);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(reference.get());
        }
        auto expected=g_spu_trampoline_fn;const auto expected_calls=calls;calls.clear();g_spu_trampoline_fn=nullptr;
        dispatch_geometry_skin(*native,p,f);
        const auto d=compare_function_state(*native,*reference);
        if(!d.exact()){
            fprintf(stderr,"skin_dispatch%u weighted%d r%d byte%d LS%X/%u state%s\n",trial,p.weighted,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");abort();
        }
        assert(g_spu_trampoline_fn==expected && native->host_depth==reference->host_depth && native->image_id==reference->image_id && native->policy_mode==reference->policy_mode && native->resident_ovl==reference->resident_ovl);
        assert(calls.size()==expected_calls.size() && !memcmp(calls.data(),expected_calls.data(),calls.size()*sizeof(Call)));
        if(p.weighted){++weighted;assert(calls.empty() && expected==f.weighted && native->pc==0xaf18);}
        else{
            assert(calls.size()>=4 && calls[0].pc==0x2750 && calls[0].entry==0xa84 && calls[0].depth==(trial/16)%3+1);
            assert(calls[1].pc==0x2778 && calls[1].entry==0xa88 && calls[2].pc==0x2780 && calls[2].entry==0xa98);
            assert(calls[3].pc==0xc518 && calls[3].entry==0xaa4 && calls[3].arguments[1]._u32[0]==0x18000+(trial&15));
            const unsigned secondary_flags[]={0,1,2,255,256,257,0xabcdef01,0x12340100};
            const bool secondary=!(secondary_flags[(trial/8)%8]&1);
            // The vector AND observes bit zero of flags^1, including 255.
            const bool want_normal=(trial&2) && secondary,want_tangent=(trial&4) && secondary;
            assert(calls.size()==4+2*want_normal+want_tangent);
            if(want_normal){++normal;assert(calls[4].entry==0xb9c && calls[5].entry==0xbb0 && calls[5].pc==0x2c00);}
            if(want_tangent){++tangent;assert(expected==f.normalize && native->pc==0x2c00 && calls.back().entry==0xb54);}
            else{++returned;assert(expected==(native->host_depth?nullptr:spu_indirect_branch) && native->pc==native->gpr[0]._u32[0]);}
        }
    }
    for(unsigned fault=0;fault<8;++fault){
        seed(*native);switch(fault){
        case 0:native->gpr[3]._u32[0]|=1;break;
        case 1:native->gpr[3]._u32[0]=0;break;
        case 2:native->gpr[3]._u32[0]=SPU_LS_SIZE-16;break;
        case 3:native->gpr[1]._u32[0]|=1;break;
        case 4:native->gpr[1]._u32[0]=0xe680;break;
        case 5:native->gpr[1]._u32[0]=SPU_LS_SIZE-16;break;
        case 6:native->gpr[1]._u32[0]=native->gpr[3]._u32[0]+160;break;
        case 7:native->ls=reference->ls_store;break;
        }
        auto* before=clone_function_state(native.get());GeometrySkinDispatch p;calls.clear();g_spu_trampoline_fn=nullptr;
        assert(!read_geometry_skin_dispatch(*native,p) && compare_function_state(*native,*before).exact() && !g_spu_trampoline_fn && calls.empty());
        release_function_state(before);native->ls=native->ls_store;
    }
    printf("Native skin dispatch:6144 strict full-state/boundary/call-trace cases (weighted%u return%u normal%u tangent%u) +8 unchanged/no-I/O rejects PASS\n",weighted,returned,normal,tangent);
}
