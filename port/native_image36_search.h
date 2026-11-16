/* Generated-lift adapter for the audited EU 1.00 image36 search.
 * On by default since the 2026-11-16 Deck qualification (+1.8% heavy menu on
 * top of direct hops); BUZZ_IMAGE36_SEARCH=0 keeps the original loop.
 * Pure private-LS search only: no DMA, channel, FP or publication changes.
 * Original block bodies and their PGO names stay intact. */
#pragma once
// Value-returning helpers and system headers below: not lifted fragments
// (spu_direct_hops.h redefines `return` for those).
#pragma push_macro("return")
#undef return
#include "image36_search.h"
#include "spu_pure_loop.h"
#include <stdlib.h>
#include <stdio.h>
extern void (*g_spu_step_observer)(spu_context*, uint32_t);
extern volatile int g_yz_lockstep_on;
extern volatile unsigned g_spu_stack_reset_n;
extern void* volatile g_pm_flow_ctx;
int spu_fast_lift_allowed(void);

static SPU_THREAD_LOCAL int search_mode=-1, search_stats;
static SPU_THREAD_LOCAL unsigned long long search_calls, search_native, search_steps, search_reject[4];
static int search_enabled(void) {
    int mode=search_mode;
    if(mode>=0)return mode;
    const char* env=getenv("BUZZ_IMAGE36_SEARCH");mode=!(env && env[0]=='0');
    if(getenv("SPU_PCHIST"))mode=0;
    search_stats=getenv("BUZZ_IMAGE36_SEARCH_STATS")!=0;
    fprintf(stderr,"[image36-search] mode=%d (default on; BUZZ_IMAGE36_SEARCH=0 disables)\n",mode);
    search_mode=mode;return mode;
}
static void search_report(void) {
    if(search_stats && search_calls%4096u==0)
        fprintf(stderr,"[image36-search] calls=%llu native=%llu steps=%llu reject=%llu,%llu,%llu,%llu\n",
                search_calls,search_native,search_steps,search_reject[0],search_reject[1],search_reject[2],search_reject[3]);
}
/* Keep the original 15BC function and its PGO identity; retarget incoming
 * edges and the entry table to this wrapper. No FP arithmetic changes. */
void buzz_image36_search_15bc(spu_context* ctx) {
    if(!search_enabled()) { buzz_job_14b4_spu_func_000015BC(ctx);return; }
    if(search_stats)search_calls++;
    if(search_stats && search_calls<=4)
        fprintf(stderr,"[image36-search] entry int=%u mask=%x status=%x depth=%u ret=%x policy=%u observer=%d lockstep=%d stack=%u flow=%d\n",
                ctx->int_enable,ctx->event_mask,ctx->event_status,ctx->host_depth,ctx->drain_ret_pc,ctx->policy_mode,
                g_spu_step_observer!=0,g_yz_lockstep_on,g_spu_stack_reset_n,g_pm_flow_ctx==(void*)ctx);
#if SPU_LS_DEBUG_HOOKS
    if(search_stats){search_reject[0]++;search_report();}
#else
    if(!spu_fast_lift_allowed()) {
        if(search_stats)search_reject[1]++;
    } else if(ctx->int_enable || ctx->drain_ret_pc || ctx->host_depth ||
              ctx->policy_mode || g_spu_step_observer || g_yz_lockstep_on ||
              g_spu_stack_reset_n || g_pm_flow_ctx==(void*)ctx) {
        if(search_stats)search_reject[2]++;
    } else if(!buzz_spu_pure_loop_budget(ctx,0x15A8u,0x15BCu) ||
              (ctx->gpr[11]._u32[1] > (ctx->gpr[5]._u32[0]&65535u) &&
               ctx->gpr[11]._u32[1]-(ctx->gpr[5]._u32[0]&65535u) >
                   buzz_spu_pure_loop_budget(ctx,0x15A8u,0x15BCu))) {
        if(search_stats)search_reject[3]++;
    } else {
        uint32_t skipped=0, pc=image36_search(ctx,&skipped);
        if(pc) {
            /* Reuse the port's bounded-loop accounting. The full table
             * must fit its quantum, and all existing admission checks remain.
             * Even pairs update both history rings and the step count exactly;
             * an odd final read uses the ordinary hook. */
            buzz_spu_pure_loop_hops(ctx,0x15A8u,0x15BCu,skipped/2u);
            if(skipped&1u) {
                ctx->pc=0x15A8u;
                spu_drain_step_hooks(ctx,(void*)buzz_job_14b4_spu_func_000015A8);
                g_spu_pch[g_spu_pch_n++&7u]=(uint32_t)ctx->pc;
            }
            ctx->pc=pc;
            g_spu_trampoline_fn=pc==0x1628u?buzz_job_14b4_spu_func_00001628:buzz_job_14b4_spu_func_000015E8;
            if(search_stats){search_native++;search_steps+=skipped;search_report();}
            return;
        }
        if(search_stats)search_reject[3]++;
    }
    search_report();
#endif
    buzz_job_14b4_spu_func_000015BC(ctx);
}
#pragma pop_macro("return")
