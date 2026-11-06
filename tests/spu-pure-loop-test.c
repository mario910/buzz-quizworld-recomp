#ifdef BUZZ_DRAIN_TEST_SOURCE
#include BUZZ_DRAIN_TEST_SOURCE
#else
#include "../ps3recomp/runtime/spu/spu_drain.c"
#include "../port/spu_pure_loop_runtime.inc"
#endif
#include <assert.h>
#include <string.h>

volatile int g_yz_lockstep_on;
volatile unsigned g_spu_stack_reset_n;
void yz_lockstep_tick(spu_context* ctx) { (void)ctx; assert(!"unexpected lockstep hook"); }
void spu_check_stack_reset(spu_context* ctx, void (*fn)(spu_context*)) { (void)ctx; (void)fn; assert(!"unexpected reset hook"); }
// COFF links resolve references in the unused SDK COMDATs before /OPT:REF.
void spu_halt(spu_context* ctx) { (void)ctx; assert(!"unexpected halt"); }
void spu_restart_dispatch(spu_context* ctx) { (void)ctx; assert(!"unexpected restart"); }
void spu_indirect_branch(spu_context* ctx) { (void)ctx; assert(!"unexpected dispatch"); }
spu_drain_fn spu_lookup(uint32_t pc, int image) { (void)pc; (void)image; assert(!"unexpected lookup"); return 0; }
static void observer(spu_context* ctx, uint32_t pc) { (void)ctx; (void)pc; }

static spu_context ctx;
int main(int argc, char** argv)
{
    memset(&ctx, 0, sizeof(ctx));
    ctx.gpr[0]._u32[0] = 0x100;
    if (argc == 2) {
        assert(!buzz_spu_pure_loop_budget(&ctx, 0x6ee0, 0x6ee8));
        puts("pure-loop diagnostic fallback PASS"); return 0;
    }
    (void)argv;
    assert(buzz_spu_pure_loop_budget(&ctx, 0x6ee0, 0x6ee8) == 256);
    ctx.int_enable=1; ctx.event_mask=4;
    assert(!buzz_spu_pure_loop_budget(&ctx,0x6ee0,0x6ee8));
    ctx.event_mask=0;
    assert(buzz_spu_pure_loop_budget(&ctx,0x6ee0,0x6ee8)==256); ctx.int_enable=0;
    g_spu_step_observer=observer; assert(!buzz_spu_pure_loop_budget(&ctx,0x6ee0,0x6ee8)); g_spu_step_observer=0;
    g_yz_lockstep_on=1; assert(!buzz_spu_pure_loop_budget(&ctx,0x6ee0,0x6ee8)); g_yz_lockstep_on=0;
    g_spu_stack_reset_n=1; assert(!buzz_spu_pure_loop_budget(&ctx,0x6ee0,0x6ee8)); g_spu_stack_reset_n=0;
    g_pm_flow_ctx=&ctx; assert(!buzz_spu_pure_loop_budget(&ctx,0x6ee0,0x6ee8)); g_pm_flow_ctx=0;
    ctx.gpr[0]._u32[0]=0x6ee0; assert(!buzz_spu_pure_loop_budget(&ctx,0x6ee0,0x6ee8)); ctx.gpr[0]._u32[0]=0x100;
    ctx.drain_ret_pc=0x6ee8; assert(!buzz_spu_pure_loop_budget(&ctx,0x6ee0,0x6ee8)); ctx.drain_ret_pc=0;
    for (unsigned start=0;start<45;++start) for(unsigned pairs=0;pairs<256;++pairs) {
        uint32_t recent[32], history[8];
        for(unsigned i=0;i<32;++i)recent[i]=t_recent[i]=i*17;
        for(unsigned i=0;i<8;++i)history[i]=g_spu_pch[i]=i*11;
        const uint32_t base=start&1?UINT32_MAX-start:start;
        t_recent_n=base;g_spu_pch_n=base;ctx.steps=base;
        for(unsigned i=0;i<pairs*2;++i) {
            const uint32_t pc=i&1?0x6ee8:0x6ee0;
            recent[(base+i)&31]=pc;history[(base+i)&7]=pc;
        }
        buzz_spu_pure_loop_hops(&ctx,0x6ee0,0x6ee8,pairs);
        assert(!memcmp(recent,t_recent,sizeof(recent)));
        assert(!memcmp(history,g_spu_pch,sizeof(history)));
        assert(t_recent_n==base+pairs*2 && g_spu_pch_n==base+pairs*2);
        assert(ctx.steps==base+(spu_launch_no_ls0()?0:pairs*2));
    }
    puts("pure-loop: guards, history, steps and counter wrap PASS");
}
