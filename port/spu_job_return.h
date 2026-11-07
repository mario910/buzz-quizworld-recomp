/* Native completion of the existing job-manager return, on the driver stack. */
#pragma once
#include "spu_context.h"
#include "spu_workload.h"
#include "spu_lockstep.h"
#include <stdlib.h>
#include <stdio.h>
#if defined(_WIN32) && defined(_M_X64) && defined(_MSC_VER) && defined(__clang__)
#include <setjmp.h>
#include <xmmintrin.h>
static inline void buzz_spu_restore_job_fp(const void* saved)
{
    const _JUMP_BUFFER* jump=(const _JUMP_BUFFER*)saved;
    _mm_setcsr(jump->MxCsr);
    __asm__ __volatile__("fldcw %0" : : "m"(jump->FpCsr));
}
#else
static inline void buzz_spu_restore_job_fp(const void* saved){(void)saved;}
#endif
static int buzz_spu_job_return_enabled;
typedef struct buzz_spu_job_frame {
    spu_context* context;
    volatile struct buzz_spu_job_frame* parent;
    int allow,completed;
} buzz_spu_job_frame;
#if defined(_MSC_VER)
static __declspec(thread) volatile buzz_spu_job_frame* buzz_spu_job_current;
#else
static __thread volatile buzz_spu_job_frame* buzz_spu_job_current;
#endif
void spu_indirect_branch(spu_context*);
static inline void buzz_spu_mark_dispatch(spu_context* context,void (*target)(spu_context*))
{
    volatile buzz_spu_job_frame* frame=buzz_spu_job_current;
    if(buzz_spu_job_return_enabled && frame && frame->context==context && target==spu_indirect_branch)
        frame->allow=1;
}
static inline void buzz_spu_clear_dispatch(spu_context* context)
{
    volatile buzz_spu_job_frame* frame=buzz_spu_job_current;
    if(frame && frame->context==context && frame->allow)frame->allow=0;
}
static inline int buzz_spu_take_dispatch(spu_context* context)
{
    volatile buzz_spu_job_frame* frame=buzz_spu_job_current;
    if(!frame || frame->context!=context)return 0;
    int direct=frame->allow;frame->allow=0;return direct;
}
static int buzz_spu_complete_job_return(spu_context* context,int direct)
{
#if defined(_WIN32) && !(defined(_M_X64) && defined(_MSC_VER) && defined(__clang__))
    // Other Windows jump-buffer ABIs retain the existing non-local exit.
    (void)context;(void)direct;return 0;
#else
    volatile buzz_spu_job_frame* frame=buzz_spu_job_current;
    if(!direct || !frame || frame->context!=context || context->host_depth ||
       context->ls!=context->ls_store || context->irq_frame || context->irq_saved || context->int_enable ||
       context->gpr[0]._u32[0]!=0x3ff00u || g_spu_trampoline_fn || g_yz_lockstep_on || g_spu_step_observer)
        return 0;
    frame->completed=1;
    return 1;
#endif
}
__attribute__((constructor)) static void buzz_spu_job_return_init(void)
{
    const char* value=getenv("BUZZ_SPU_JOB_RETURN");
    if(value && ((value[0]!='0' && value[0]!='1') || value[1]))abort();
    buzz_spu_job_return_enabled=!value || value[0]=='1';
    fprintf(stderr,"[spu-job-return] native-completion=%d\n",buzz_spu_job_return_enabled);
}
