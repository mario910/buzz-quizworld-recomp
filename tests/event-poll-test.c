/* Exercise the real flag lookup/TryWait without lifted game assets. */
#ifdef _WIN32
#include <stdlib.h>
static int test_setenv(const char* name,const char* value,int overwrite)
{ if(!overwrite && getenv(name))return 0; return _putenv_s(name,value); }
#define setenv test_setenv
#endif
#define CHAIN_RESUME_TEST_MAIN chain_resume_unused_main
#include "chain-resume-test.c"
#undef CHAIN_RESUME_TEST_MAIN
#include <time.h>
#include <stdatomic.h>
void (*g_spu_line_commit_hook)(uint32_t);
void spu_taskset_signal_task(uint32_t a,uint32_t b){(void)a;(void)b;}
void spu_ef_bits_post(uint32_t a,uint16_t b){(void)a;(void)b;}
int spu_taskset_signal_parked_obj(uint32_t a,uint32_t b){(void)a;(void)b;return 0;}
void spu_taskset_latch_wake(uint32_t a){(void)a;}
#ifdef _WIN32
/* COFF requires references from otherwise dead SDK entry points to resolve.
 * None of these services belongs to the event-flag fixture. Fail if reached. */
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
static CellSpursEventFlag* flag(uint32_t ea){return (CellSpursEventFlag*)(uintptr_t)ea;}
static u16* bits(uint32_t ea){return (u16*)(uintptr_t)ea;}
static atomic_uint received,stop;
static DWORD WINAPI poller(LPVOID arg)
{
    const uint32_t out=0x31000+16*(uint32_t)(uintptr_t)arg;
    while(!atomic_load(&stop)){
        if ((uintptr_t)arg == 0 && spurs_event_flag_busy_probe(0x10000,1)==1) continue;
        vm_write16(out,1);
        if(cellSpursEventFlagTryWait(flag(0x10000),bits(out),1)==0)
            atomic_fetch_add(&received,1);
    }
    return 0;
}
int main(void)
{
    const uint32_t a=0x10000,b=0x18000,out=0x30000;
    assert(!ef_sync_find(a));
    assert(cellSpursEventFlagInitialize(0,flag(a),0,1)==0);
    assert(spurs_event_flag_busy_probe(a,1)==1);
    assert(spurs_event_flag_busy_probe(0,1)==-1);
    assert(spurs_event_flag_busy_probe(a,0)==-1);
    assert(spurs_event_flag_busy_probe(0x19000,1)==-1);
    EventFlagSync* slot=ef_sync_find(a);
    for(unsigned n=0;n<100000;++n)assert(ef_sync_find(a)==slot);
    ef_sync_free(slot);
    assert(!ef_sync_find(a));
    assert(ef_sync_alloc(b)==slot);
    assert(!ef_sync_find(a));
    assert(ef_sync_find(b)==slot);
    /* Same bucket, different EA; a miss must not return the cached slot. */
    for(uint32_t ea=0x20000;ea<0x28000;ea+=128){
        EventFlagSync* s=ef_sync_alloc(ea);assert(s);
        assert(ef_sync_find(ea)==s && ef_sync_find(b)==slot);
        ef_sync_free(s);assert(!ef_sync_find(ea));
    }
    assert(cellSpursEventFlagInitialize(0,flag(a),0,1)==0);
    vm_write16(out,3);
    assert(cellSpursEventFlagTryWait(flag(a),bits(out),1)==CELL_SPURS_TASK_ERROR_BUSY);
    assert(vm_read16(out)==3);
    assert(cellSpursEventFlagSet(flag(a),5)==0);
    uint64_t queue=spurs_completion_begin(a,2048,1);assert(queue);
    assert(spurs_completion_take(queue,0)==1);
    assert(cellSpursEventFlagSet(flag(a),5)==0);
    assert(spurs_completion_take(queue,0)==1); // HLE producer
    g_spu_line_commit_hook(a);
    assert(spurs_completion_take(queue,0)==1); // SPU commit producer
    spurs_completion_end(queue);
    assert(spurs_event_flag_busy_probe(a,1)==0 && vm_read16(a)==5);
    assert(spurs_event_flag_busy_probe(a,3)==1 && vm_read16(a)==5);
    assert(cellSpursEventFlagTryWait(flag(a),bits(out),1)==CELL_SPURS_TASK_ERROR_BUSY);
    assert(vm_read16(out)==3 && vm_read16(a)==5);
    assert(cellSpursEventFlagTryWait(flag(a),bits(out),0)==0);
    assert(vm_read16(out)==5 && vm_read16(a)==4); /* OR consumes matched bit only */
    vm_write16(out,4);
    assert(cellSpursEventFlagTryWait(flag(a),bits(out),1)==0);
    assert(vm_read16(a)==0);
    assert(cellSpursEventFlagInitialize(0,flag(a),1,1)==0);
    assert(cellSpursEventFlagSet(flag(a),7)==0);
    vm_write16(out,3);
    assert(cellSpursEventFlagTryWait(flag(a),bits(out),1)==0);
    assert(vm_read16(out)==7 && vm_read16(a)==7); /* MANUAL never clears */
    assert(cellSpursEventFlagTryWait(0,bits(out),1)==CELL_SPURS_TASK_ERROR_NULL_POINTER);
    vm_write16(out,0x8000);
    clock_t started=clock();
    for(unsigned n=0;n<2000000;++n)
        assert(cellSpursEventFlagTryWait(flag(a),bits(out),1)==CELL_SPURS_TASK_ERROR_BUSY);
    printf("PASS: lookup/recycle/collisions, BUSY, AND/OR, AUTO/MANUAL; 2M polls %.3fs\n",
           (double)(clock()-started)/CLOCKS_PER_SEC);
    assert(cellSpursEventFlagInitialize(0,flag(a),0,1)==0);
    HANDLE threads[2]={CreateThread(0,0,poller,(LPVOID)0,0,0),CreateThread(0,0,poller,(LPVOID)1,0,0)};
    assert(threads[0] && threads[1]);
    ULONGLONG deadline=GetTickCount64()+15000;
    for(unsigned n=1;n<=10000;++n){
        assert(cellSpursEventFlagSet(flag(a),1)==0);
        while(atomic_load(&received)<n){assert(GetTickCount64()<deadline);SwitchToThread();}
        assert(atomic_load(&received)==n);
    }
    atomic_store(&stop,1);
    for(unsigned i=0;i<2;++i){assert(WaitForSingleObject(threads[i],3000)==WAIT_OBJECT_0);CloseHandle(threads[i]);}
    assert(atomic_load(&received)==10000 && vm_read16(a)==0);
    puts("PASS: two concurrent PPU pollers, 10000 real posts consumed exactly once");
}
