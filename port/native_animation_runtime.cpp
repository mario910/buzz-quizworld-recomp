/* Host animation job owner. No SPU register bank or per-function ABI. */
#include "native_animation_runtime.h"
#include "native_geometry_job.h"
#include "native_geometry_ledger.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>

#ifndef BUZZ_ANIMATION_JOB_AVAILABLE
#define BUZZ_ANIMATION_JOB_AVAILABLE 0
#endif

namespace {
std::atomic<int> option{-1};
buzz::GeometryLedgerCounter attempted,executed,compared,fallback,audit_skipped;
}

extern "C" int buzz_animation_job_mode()
{
    int mode=option.load(std::memory_order_relaxed);
    if(mode<0) {
        const char* text=std::getenv("BUZZ_ANIMATION_JOB");
        if(text && (text[0]<'0'||text[0]>'2'||text[1]))std::abort();
        mode=text?text[0]-'0':0;
        option.store(mode,std::memory_order_relaxed);
    }
    return mode;
}
extern "C" void buzz_animation_job_report()
{
    if(buzz_animation_job_mode())std::fprintf(stderr,
        "[animation-job] attempted=%llu native=%llu compared=%llu fallback=%llu audit-skipped=%llu (job-boundary; default off)\n",
        attempted.load(),executed.load(),compared.load(),fallback.load(),audit_skipped.load());
}

#if BUZZ_ANIMATION_JOB_AVAILABLE
#include "native_animation_job.h"
#include "spu_dma.h"
extern "C" uint8_t g_vm_page_bitmap[65536/8];

namespace {
struct FloatEnvironment {
    unsigned previous=_mm_getcsr();
    FloatEnvironment(){_mm_setcsr((previous&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);}
    ~FloatEnvironment(){_mm_setcsr(previous);}
};
struct Workspace {
    buzz::AnimationNativeJobWorkspace work;
    buzz::AnimationNativeJobPlan plan;
    alignas(32) uint8_t reference[256*48],world[256*64],curve[256*4];
    const spu_context* prepared=nullptr;
    buzz::AnimationNativeJobOutputs outputs(){return {reference,world,curve};}
};
thread_local Workspace workspace;

buzz::AnimationByteView resolve_input(const spu_context* ctx,uint32_t ea,size_t bytes)
{
    if(!bytes || bytes>16384 || bytes>UINT64_C(0x100000000)-ea)return {};
    if(buzz_native_job_is_shadow(ctx)) {
        const auto* data=buzz_native_job_input(ctx,ea,bytes);
        return data?buzz::AnimationByteView{data,bytes}:buzz::AnimationByteView{};
    }
    if(!vm_base)return {};
    // Admission never demand-commits guest pages. Use the existing DMA page
    // cache on the hot path and query uncached spans without changing them.
    const uint32_t last=ea+uint32_t(bytes)-1;
    for(uint32_t page=ea>>16;page<=last>>16;++page) {
        if((g_vm_page_bitmap[page>>3]>>(page&7))&1)continue;
        MEMORY_BASIC_INFORMATION memory;
        const auto* at=vm_base+(uintptr_t(page)<<16);
        if(!VirtualQuery(at,&memory,sizeof(memory)) || memory.State!=MEM_COMMIT ||
            (memory.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return {};
    }
    return {vm_base+ea,bytes};
}

int prepare(const spu_context* ctx)
{
    workspace.prepared=nullptr;
    buzz::AnimationJobInput input;
    FloatEnvironment environment;
    if(!buzz::read_animation_job_input(ctx->ls,SPU_LS_SIZE,ctx->gpr[3]._u32[0],ctx->gpr[4]._u32[0],input))return 0;
    if(!buzz::prepare_native_animation_job(input,ctx->ls,SPU_LS_SIZE,ctx->int_enable!=0,
        [&](uint32_t ea,size_t bytes){return resolve_input(ctx,ea,bytes);},
        workspace.plan,workspace.work,workspace.outputs()))return 0;
    workspace.prepared=ctx;
    return 1;
}
struct Io {
    spu_context* ctx;
    uint32_t read(uint32_t channel){return buzz_geometry_job_rdch(ctx,channel)._u32[0];}
    void write(uint32_t channel,uint32_t value){u128 word{};word._u32[0]=value;buzz_geometry_job_wrch(ctx,channel,word);}
    void set_interrupts(bool enabled){ctx->int_enable=enabled;}
    uint8_t* ls(){return ctx->ls;}
    void finish(){SPU_RET(ctx);}
};
void native(spu_context* ctx)
{
    if(workspace.prepared!=ctx)buzz_geometry_job_native_fail(ctx,"animation preparation owner");
    workspace.prepared=nullptr;
    FloatEnvironment environment;
    Io io{ctx};
    buzz::execute_native_animation_job(workspace.plan,workspace.outputs(),io);
}
}
#endif

extern "C" void buzz_animation_job_run(spu_context* ctx,void (*reference)(spu_context*))
{
    const int mode=buzz_animation_job_mode();
    if(!mode){reference(ctx);return;}
    ++attempted;
#if BUZZ_ANIMATION_JOB_AVAILABLE
    // SPURS stages a fresh context with PC=0, then invokes the registered
    // 0x10 entry directly. The entry wrapper establishes the code boundary.
    if(buzz_native_job_eligible(ctx,40,0)) {
        if(mode==2) {
            if(!prepare(ctx)){++fallback;reference(ctx);return;}
            // Retain actual native execution, including its real GETs. The
            // original shadow consumes those GET payloads and checks native
            // publications. This also detects inputs changing between native
            // preparation and the corresponding observable DMA reads.
            if(buzz_native_job_audit(ctx,native,reference,nullptr,40,0))++compared;
            else ++audit_skipped; // Native ran, but this transcript was unrecordable.
            ++executed;
            return;
        }
        if(prepare(ctx)){
            native(ctx);
            const auto n=++executed;
            if(n==256 || n%5000==0)std::fprintf(stderr,"[animation-job/host-pipeline] native %llu (owner count)\n",n);
            return;
        }
    }
#endif
    ++fallback;
    reference(ctx);
}
namespace {
struct Init {
    Init() {
        const int mode=buzz_animation_job_mode();
        if(mode && !BUZZ_ANIMATION_JOB_AVAILABLE) {
            std::fprintf(stderr,"[animation-job] requires the pinned job adapter and checked AVX2/FMA/RTZ/result-guard build\n");
            std::abort();
        }
        if(mode) {
            std::fprintf(stderr,"[animation-job] mode=%d (0 retained lift,1 native,2 native-retained whole-job I/O compare; default off)\n",mode);
            std::atexit(buzz_animation_job_report);
        }
    }
} init;
}
