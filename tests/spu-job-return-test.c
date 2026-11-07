/* Exercise the real SDK dispatcher and its nesting/stop regressions. */
#include "../ps3recomp/runtime/spu/spu_workload.h"
#define main sdk_handoff_main
#include "../ps3recomp/runtime/spu/tests/test_spu_interpreter_handoff.c"
#undef main
#include <fenv.h>
#ifdef _WIN32
/* Link-only PPU wake diagnostics. No DMA is issued by these fixtures. */
volatile uint32_t g_ppu_wake_ea[2];
volatile long g_ppu_wake_count_dma;
#endif

static unsigned observed;
static int inner_fp;
static void fp_return(spu_context* c){fesetround(FE_DOWNWARD);feraiseexcept(FE_INEXACT);c->pc=0x3ff00;g_spu_trampoline_fn=spu_indirect_branch;}
static void x87_return(spu_context* c){
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ __volatile__("fld1; fchs; fsqrt; fstp %%st(0)" : : : "st");
#else
    feraiseexcept(FE_INVALID);
#endif
    c->pc=0x3ff00;g_spu_trampoline_fn=spu_indirect_branch;
}
static void observe(spu_context* c,uint32_t pc){(void)c;(void)pc;++observed;}
static void queue_job_return(spu_context* c){c->pc=0x3ff00;g_spu_trampoline_fn=spu_indirect_branch;}
static void direct_job_return(spu_context* c){c->pc=0x3ff00;spu_indirect_branch(c);c->gpr[19]._u32[0]++;}
static void normal_host_return(spu_context* c){(void)c;}
static void nested_depth(spu_context* c){c->host_depth=1;queue_job_return(c);}
static void irq_frame_return(spu_context* c){c->irq_frame=(void*)1;queue_job_return(c);}
static void enabled_irq_return(spu_context* c){c->int_enable=1;queue_job_return(c);}
static void wrong_link_return(spu_context* c){c->gpr[0]._u32[0]=0x200;queue_job_return(c);}
static void irq_restore_return(spu_context* c){
    memcpy(c->irq_gpr,c->gpr,sizeof(c->gpr));c->irq_gpr[16]._u32[0]=12345;
    c->irq_saved=1;c->irq_resume_pc=0x3ff00;c->int_enable=1;queue_job_return(c);
}
static void forbidden_target(spu_context* c){c->gpr[19]._u32[0]++;spu_halt(c);}
static void policy_target(spu_context* c){c->gpr[18]._u32[0]++;c->status=SPU_STATUS_STOPPED_BY_STOP;spu_halt(c);}
static void policy_return(spu_context* c){c->policy_mode=1;queue_job_return(c);}
static void inner_return(spu_context* outer){
    spu_context* inner=calloc(1,sizeof(*inner));assert(inner);spu_context_init(inner,0);
    inner->image_id=41;inner->gpr[0]._u32[0]=0x3ff00;
    assert(spu_run_with_halt(inner_fp?fp_return:queue_job_return,inner)==1);
    assert(inner->pc==0x3ff00 && !g_spu_trampoline_fn);
    free(inner);outer->gpr[17]._u32[0]++;queue_job_return(outer);
}
static void queued_inner(spu_context* c){c->pc=0x800;g_spu_trampoline_fn=inner_return;}
static void queued_direct(spu_context* c){c->pc=0x800;g_spu_trampoline_fn=direct_job_return;}
static void lifted_direct(spu_context* c){c->pc=0x800;g_spu_trampoline_fn=spu_indirect_branch;}
static uint64_t hash_bytes(uint64_t h,const void* bytes,size_t n){
    const unsigned char* p=bytes;while(n--){h^=*p++;h*=UINT64_C(1099511628211);}return h;
}
int main(int argc,char**argv){
    assert(argc==2);const char* mode=argv[1];
    if(!strncmp(mode,"sdk-",4)){char* args[]={argv[0],argv[1]+4};return sdk_handoff_main(2,args);}
    spu_context_init(&ctx,0);ctx.image_id=41;ctx.gpr[0]._u32[0]=0x3ff00;
    void(*entry)(spu_context*)=queue_job_return;
    unsigned char* foreign=NULL;
    if(!strcmp(mode,"direct"))entry=direct_job_return;
    else if(!strcmp(mode,"nested-depth"))entry=nested_depth;
    else if(!strcmp(mode,"inner"))entry=inner_return;
    else if(!strcmp(mode,"inner-fp")){inner_fp=1;entry=inner_return;}
    else if(!strcmp(mode,"fp-state"))entry=fp_return;
    else if(!strcmp(mode,"fp-x87"))entry=x87_return;
    else if(!strcmp(mode,"queued-inner"))entry=queued_inner;
    else if(!strcmp(mode,"queued-direct"))entry=queued_direct;
    else if(!strcmp(mode,"lifted-direct")){spu_begin_image(41);spu_register_function(0x800,direct_job_return);entry=lifted_direct;}
    else if(!strcmp(mode,"irq-frame"))entry=irq_frame_return;
    else if(!strcmp(mode,"irq-enabled"))entry=enabled_irq_return;
    else if(!strcmp(mode,"wrong-link"))entry=wrong_link_return;
    else if(!strcmp(mode,"irq-restore"))entry=irq_restore_return;
    else if(!strcmp(mode,"host-return"))entry=normal_host_return;
    else if(!strcmp(mode,"foreign-ls")){foreign=calloc(1,SPU_LS_SIZE);assert(foreign);ctx.ls=foreign;}
    else if(!strcmp(mode,"observer"))g_spu_step_observer=observe;
    else if(!strcmp(mode,"registered")){spu_begin_image(41);spu_register_function(0x3ff00,forbidden_target);}
    else if(!strcmp(mode,"overlay")){ctx.resident_ovl=42;spu_begin_image(42);spu_register_function(0x3ff00,forbidden_target);}
    else if(!strcmp(mode,"resident-code")){
        ctx.resident_code[0].image_id=42;ctx.resident_code[0].lsa=0x3f000;ctx.resident_code[0].size=0x1000;
        spu_begin_image(42);spu_register_function(0x3ff00,forbidden_target);
    }
    else if(!strcmp(mode,"policy")){spu_begin_image(41);spu_register_function(0x3ff00,policy_target);entry=policy_return;}
    else assert(!strcmp(mode,"simple"));
    int halted=spu_run_with_halt(entry,&ctx);
    assert(halted==(strcmp(mode,"host-return")!=0));
    assert(ctx.gpr[19]._u32[0]==0 && !g_spu_trampoline_fn);
    assert(ctx.gpr[17]._u32[0]==(!strcmp(mode,"inner") || !strcmp(mode,"queued-inner") || !strcmp(mode,"inner-fp")));
    assert(ctx.gpr[18]._u32[0]==!strcmp(mode,"policy"));
    if(!strcmp(mode,"observer"))assert(observed>0);
    if(!strcmp(mode,"irq-restore"))assert(ctx.gpr[16]._u32[0]==12345 && !ctx.irq_saved);
    if(!strcmp(mode,"resident-code"))assert(ctx.resident_code[0].image_id==42 && ctx.resident_code[0].lsa==0x3f000 && ctx.resident_code[0].size==0x1000);
    uint64_t hash=hash_bytes(UINT64_C(1469598103934665603),ctx.gpr,sizeof(ctx.gpr));
    hash=hash_bytes(hash,ctx.ls,SPU_LS_SIZE);
    printf("PASS %s halted=%d pc=%x status=%x stop=%x depth=%u irq=%u/%u resident=%u/%u observed=%u hash=%016llx\n",
        mode,halted,ctx.pc,ctx.status,ctx.stop_code,ctx.host_depth,ctx.int_enable,ctx.irq_saved,
        ctx.resident_task,ctx.resident_ovl,observed,(unsigned long long)hash);
    if(!strncmp(mode,"fp-",3) || !strcmp(mode,"inner-fp"))printf("host-fp round=%d flags=%d\n",fegetround(),fetestexcept(FE_ALL_EXCEPT));
    free(foreign);return 0;
}
