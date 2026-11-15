/* Synthetic full-state and real-runtime history checks; oracle generated locally. */
#include "spu_helpers.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void buzz_job_14b4_spu_func_000015BC(spu_context*);
void buzz_job_14b4_spu_func_000015A8(spu_context*);
void buzz_job_14b4_spu_func_000015E8(spu_context* c) {(void)c;abort();}
void buzz_job_14b4_spu_func_00001628(spu_context* c) {(void)c;abort();}
volatile int g_yz_lockstep_on;
volatile unsigned g_spu_stack_reset_n;
void search_test_history_reset(unsigned);
void search_test_history_copy(uint32_t*,unsigned*);
void search_test_hook(spu_context*);
void spu_drain_step_hooks(spu_context* c,void* fn) {
    assert(c->pc==0x15A8u && fn==(void*)buzz_job_14b4_spu_func_000015A8);
    search_test_hook(c);
}
#include "native_image36_search.h"
static spu_context a,b;
static uint32_t seed=0x36ab15bc;
static uint32_t rnd(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
static void put(uint32_t base, unsigned i, uint32_t v) {
    v=SPU_BSWAP32(v);memcpy(a.ls+base+4u*i,&v,4);
}
static void setup(unsigned limit,unsigned start,unsigned base,int match) {
    memset(&a,0,sizeof a);a.ls=a.ls_store;a.pc=0x15BC;a.image_id=36;a.steps=rnd();
    for(unsigned r=0;r<128;r++)for(unsigned w=0;w<4;w++)a.gpr[r]._u32[w]=rnd();
    for(unsigned i=0;i<SPU_LS_SIZE;i+=4){uint32_t v=rnd();memcpy(a.ls+i,&v,4);}
    a.gpr[5]._u32[0]=(rnd()&0xffff0000u)|start;a.gpr[6]._u32[0]=65535u;
    a.gpr[9]._u32[0]=base;a.gpr[11]._u32[0]=0;a.gpr[11]._u32[1]=limit;
    if(match>=0){put(base,(unsigned)match,a.gpr[7]._u32[0]);
        if((unsigned)match+1<limit)put(base,(unsigned)match+1,a.gpr[7]._u32[0]);}
}

static void run(spu_context* ctx, void(*entry)(spu_context*)) {
    g_spu_trampoline_fn=0;entry(ctx);
    unsigned count=0;
    while(g_spu_trampoline_fn==buzz_job_14b4_spu_func_000015BC ||
          g_spu_trampoline_fn==buzz_job_14b4_spu_func_000015A8) {
        assert(++count<131072);
        void(*fn)(spu_context*)=g_spu_trampoline_fn;g_spu_trampoline_fn=0;
        search_test_hook(ctx);g_spu_pch[g_spu_pch_n++&7u]=ctx->pc;fn(ctx);
    }
}
static unsigned checked;
static void unexpected_observer(spu_context* ctx, uint32_t pc) {
    (void)ctx;(void)pc;abort();
}
static void check(void) {
    if(checked&1u)a.steps=0xfffffff0u;
    memcpy(&b,&a,sizeof b);b.ls=b.ls_store;
    unsigned initial=(checked&1)?0xfffffff0u:rnd();
    uint32_t hist[32],actual[32],pc_hist[8];unsigned hist_n,actual_n,pc_n;
    search_test_history_reset(initial);
    run(&a,buzz_job_14b4_spu_func_000015BC);
    void(*next)(spu_context*)=g_spu_trampoline_fn;
    search_test_history_copy(hist,&hist_n);
    memcpy(pc_hist,g_spu_pch,sizeof pc_hist);pc_n=g_spu_pch_n;
    search_test_history_reset(initial);
    run(&b,buzz_image36_search_15bc);
    search_test_history_copy(actual,&actual_n);
    assert(hist_n==actual_n && !memcmp(hist,actual,sizeof hist));
    assert(pc_n==g_spu_pch_n && !memcmp(pc_hist,g_spu_pch,sizeof pc_hist));
    assert(next==g_spu_trampoline_fn);
    b.ls=a.ls;assert(!memcmp(&a,&b,sizeof a));checked++;
}
int main(int argc, char** argv) {
    const int disabled=argc==2 && !strcmp(argv[1],"disabled");
    assert(argc==1 || disabled);
    for(unsigned n=0;n<4000;n++) {
        unsigned limit=rnd()%513,start=rnd()%514;
        unsigned base=(n&3u)*4u+(n&4u?0x20000u:0);
        int hit=(n&1u)&&start+1<limit?(int)(start+1+rnd()%(limit-start-1)):-1;
        setup(limit,start,base,hit);check();
    }
    for(unsigned limit=0;limit<48;limit++)for(unsigned start=0;start<=limit;start++)
        for(int match=-1;match<(int)limit;match++) {
            setup(limit,start,SPU_LS_SIZE-4u*limit,match);check();
        }
    for(unsigned n=0;n<8;n++) {
        setup(65535,n&1u?65534u:0u,0,n&2u?65534:-1);check();
    }
    /* Native accounting must respect the real admission gate. */
    for(unsigned n=0;n<10;n++) {
        const int saved_tailret=g_spu_tailret;
        setup(20,0,0x1000,-1);
        if(n==0)a.int_enable=1;
        if(n==1)a.drain_ret_pc=0x18FC;
        if(n==2)a.host_depth=1;
        if(n==3)a.policy_mode=1;
        if(n==4)g_spu_step_observer=unexpected_observer;
        if(n==5)g_yz_lockstep_on=1;
        if(n==6)g_spu_stack_reset_n=1;
        if(n==7)g_pm_flow_ctx=&b;
        if(n==8)g_spu_tailret=1;
        if(n==9)a.gpr[0]._u32[0]=0x15A8u;
        unsigned long long native_before=search_native;
        check();assert(search_native==native_before);
        g_spu_step_observer=0;g_yz_lockstep_on=0;g_spu_stack_reset_n=0;g_pm_flow_ctx=0;g_spu_tailret=saved_tailret;
    }
    for(unsigned n=0;n<6;n++) {
        setup(20,0,0x1000,-1);
        if(n==0)a.ls=b.ls_store;
        if(n==1)a.gpr[6]._u32[0]=0xfffe;
        if(n==2)a.gpr[11]._u32[0]=1;
        if(n==3)a.gpr[11]._u32[1]=65536;
        if(n==4)a.gpr[5]._u32[0]=65535;
        if(n==5)a.gpr[9]._u32[0]=SPU_LS_SIZE-1;
        memcpy(&b,&a,sizeof b);unsigned skipped=0x1234;
        assert(!image36_search(&a,&skipped) && skipped==0x1234 && !memcmp(&a,&b,sizeof a));
    }
    if(disabled)assert(!search_native);
    else assert(search_calls==checked && search_native>40000);
    printf("PASS %u full-context comparisons, actual bulk ring implementation, counter wrap, first match and guarded fallbacks; native=%llu calls=%llu\n",checked,search_native,search_calls);
}
