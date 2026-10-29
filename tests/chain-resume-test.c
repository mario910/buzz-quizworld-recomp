/* Isolated real chain walker, synthetic END/NEXT stream, no lifted assets. */
#define main guard_layout_unused_main
#include "guard-layout-test.c"
#undef main
#include "../ps3recomp/libs/spurs/cellSpurs.c"
uint32_t g_spurs_job_ls_handle;
SPURS_JOB_TLS uint32_t g_spurs_job_mbox,g_spurs_job_mbox_intr,g_spurs_job_cmd;
SPURS_JOB_TLS int g_spurs_job_mbox_valid;
void spurs_job_set_owner(uint32_t a){(void)a;}
void spu_workload_clear_job_cache(void){}
void spurs_job_release_thread_context(void){}
static int mock_job_image = -1;
int spu_workload_job_image(const uint8_t* p,uint32_t n){(void)p;(void)n;return mock_job_image;}
uint64_t spu_workload_fingerprint(const void* p,size_t n){(void)p;(void)n;return 0;}
spu_lifted_entry_fn spu_workload_find_img(uint64_t f,int* i){(void)f;if(i)*i=-1;return NULL;}
int spurs_job_pool_run(uint32_t a,unsigned b,unsigned c,uint32_t d,unsigned e,uint32_t f,
                       uint32_t g,int h,spurs_pool_job_fn fn){
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)fn;return 0;
}
static unsigned mock_range_calls, mock_range_count;
static SpursPoolRange mock_ranges[32];
static int mock_range_result;
int spurs_job_pool_run_ranges(uint32_t a,unsigned b,unsigned c,const SpursPoolRange* ranges,
                               unsigned count,uint32_t chain,spurs_pool_job_fn fn){
    (void)a;(void)b;(void)c;(void)chain;(void)fn;
    ++mock_range_calls;mock_range_count=count;
    memcpy(mock_ranges,ranges,count*sizeof(*ranges));return mock_range_result;
}
int spu_workload_dispatch_job(const uint8_t* a,uint32_t b,uint32_t d,uint32_t e){
    (void)a;(void)b;(void)d;(void)e;assert(!"Unexpected job dispatch");return 0;
}
int sys_event_queue_push_by_id(uint32_t q,uint64_t s,uint64_t a,uint64_t b,uint64_t d){
    (void)q;(void)s;(void)a;(void)b;(void)d;return 0;
}
#ifndef CHAIN_RESUME_TEST_MAIN
#define CHAIN_RESUME_TEST_MAIN main
#endif
int CHAIN_RESUME_TEST_MAIN(void){
    setenv("SPURS_JOB_KERNEL_MIRROR","1",1);
    vm_write64(0x20000,0x10000);
    vm_write64(0x10000,0x7F);
    vm_write64(0x10008,0x10023); /* NEXT -> 10020 */
    vm_write64(0x10020,0x7F);
    uint32_t cursor=0;
    jc_execute(0x10000,0x20000,256,&cursor);assert(cursor==0x10008);
    jc_execute(0x10000,0x20000,256,&cursor);assert(cursor==0x10028);
    puts("PASS: signalled chain resumes after END and follows NEXT instead of replaying entry");
}
