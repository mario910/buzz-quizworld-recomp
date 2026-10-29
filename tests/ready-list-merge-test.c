#define CHAIN_RESUME_TEST_MAIN unused_resume_main
#ifdef _WIN32
#define setenv(k,v,o) _putenv_s(k,v)
#endif
#include "chain-resume-test.c"
#ifdef _WIN32
/* Resolve dead COFF SDK entry points; reaching one is a fixture error. */
void (*g_spu_line_commit_hook)(uint32_t);
void spu_taskset_signal_task(uint32_t a,uint32_t b){abort();}
void spu_ef_bits_post(uint32_t a,uint16_t b){abort();}
int spu_taskset_signal_parked_obj(uint32_t a,uint32_t b){abort();}
void spu_taskset_latch_wake(uint32_t a){abort();}
void spurs_policy_release_instance(uint32_t a){abort();}
int spurs_policy_release_workload(uint32_t a,uint32_t b){abort();}
void sys_event_queue_cancel_by_id(uint32_t a){abort();}
void sys_event_queue_uncancel_by_id(uint32_t a){abort();}
void spurs_taskset_init(uint32_t a,uint32_t b,uint64_t c,uint32_t d,uint32_t e,uint32_t f,uint32_t g){abort();}
uint32_t spurs_taskset_take_slot(uint32_t a,uint32_t b){abort();}
void spu_taskset_mark_running(uint32_t a,uint32_t b){abort();}
void spu_taskset2_set_exit(uint32_t a,uint32_t b,int c,uint32_t d){abort();}
void spurs_taskset_add_task(uint32_t a,uint32_t b,uint64_t c,uint64_t d,const uint32_t e[4],const uint32_t f[4]){abort();}
size_t spu_elf_image_size(const uint8_t* a,size_t b){abort();}
int spu_workload_dispatch_async(const uint8_t* a,uint32_t b,uint32_t c){abort();}
SPU_THREAD_LOCAL volatile unsigned g_spurs_pm_polls;
int spu_run_policy_module(spu_lifted_entry_fn a,int b,const uint8_t* c,uint32_t d,uint64_t e,uint32_t f,uint32_t g,uint32_t h){abort();}
#endif
static void list(unsigned n, unsigned count, unsigned first) {
    vm_write64(0x10000+n*8, (0x12000+n*16)|6);
    vm_write32(0x12000+n*16,count);vm_write32(0x12004+n*16,64);
    vm_write64(0x12008+n*16,first);
    for (unsigned j=0;j<count;++j) {
        vm_write64(first+j*64,0x30000);vm_write32(first+j*64+8,4u<<16);
    }
}
int main(void) {
    setenv("SPURS_MERGE_READY_LISTS","1",1);
    setenv("SPURS_JOB_LIST_WORKERS","4",1);
    vm_write64(0x20078,0x28000);
    s_inst[0].ea=0x28000;s_inst[0].nspus=5;
    mock_job_image=41;mock_range_result=1;
    list(0,2,0x14000);list(1,3,0x15000);list(2,1,0x16000);
    const uint64_t barriers[]={2,18,5,0x30000,0x10003,0x10004,15,127,0x800000012ull};
    for (unsigned n=0;n<sizeof(barriers)/sizeof(barriers[0]);++n) {
        vm_write64(0x10018,barriers[n]);
        unsigned lists=0,jobs=0;
        assert(jc_merged_lists(0x20000,0x10000,7,&lists,&jobs));
        assert(lists==3&&jobs==6&&mock_range_count==3);
        assert(mock_ranges[0].first_index==7&&mock_ranges[1].first_index==9&&mock_ranges[2].first_index==12);
    }
    setenv("SPURS_JOB_KERNEL_MIRROR","1",1);
    vm_write64(0x20000,0x10000);vm_write64(0x10018,127);
    unsigned cursor=0, calls_before=mock_range_calls;
    jc_execute(0x10000,0x20000,256,&cursor);
    assert(cursor==0x10020&&mock_range_calls==calls_before+1&&mock_range_count==3);
    unsigned lists=0,jobs=0;
    mock_job_image=40;
    assert(!jc_merged_lists(0x20000,0x10000,0,&lists,&jobs));
    mock_job_image=41;
    /* Duplicate descriptors stop batching; one eligible list alone is not merged. */
    list(1,2,0x14000);
    assert(!jc_merged_lists(0x20000,0x10000,0,&lists,&jobs));
    list(1,3,0x15000);vm_write32(0x12014,63);
    assert(!jc_merged_lists(0x20000,0x10000,0,&lists,&jobs));
    vm_write32(0x12014,64);mock_range_result=0;
    assert(!jc_merged_lists(0x20000,0x10000,0,&lists,&jobs));
    mock_range_result=1;
    for (unsigned n=0;n<32;++n) list(n,1,0x14000+n*256);
    assert(jc_merged_lists(0x20000,0x10000,0,&lists,&jobs));
    assert(lists==32&&jobs==32);
    list(0,128,0x14000);list(1,128,0x18000);list(2,1,0x1c000);
    assert(jc_merged_lists(0x20000,0x10000,0,&lists,&jobs));
    assert(lists==2&&jobs==256);
    puts("PASS: adjacent published geometry lists; SYNC/LWSYNC/FLUSH/JOB/branch/guard/JTS boundaries, indices, overlap/format/image rejection, pool fallback");
}
