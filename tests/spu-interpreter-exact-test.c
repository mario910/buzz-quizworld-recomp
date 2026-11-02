#define SPU_LS_DEBUG_HOOKS 0
#include <windows.h>
/* Include the production TU to stress its private recorder directly, without
 * invoking unrelated legacy interpreter diagnostics concurrently. */
#include "../ps3recomp/runtime/spu/spu_interp.c"
#include <assert.h>
#include <process.h>
#include <string.h>

/* Real map-bound symbols for the observer's process-integration fixture. */
volatile uint64_t g_render_perf_epoch=1;
volatile uint64_t g_render_perf_serial;

u128 spu_rdch(spu_context* c,uint32_t ch){(void)c;(void)ch;abort();}
void spu_wrch(spu_context* c,uint32_t ch,u128 v){(void)c;(void)ch;(void)v;abort();}
uint32_t spu_rchcnt(spu_context* c,uint32_t ch){(void)c;(void)ch;abort();}
static void lifted(spu_context* c){(void)c;abort();}
spu_lifted_fn spu_lifted_lookup(const spu_context* c,uint32_t pc){(void)c;return pc==0x800?lifted:NULL;}
void spu_spurs_taskset_syscall(spu_context* c){c->gpr[3]._u32[0]=0x713;}
static void put32(spu_context* c,unsigned at,uint32_t word){
    c->ls[at]=word>>24;c->ls[at+1]=word>>16;c->ls[at+2]=word>>8;c->ls[at+3]=word;
}
static spu_context* context(void){
    spu_context* c=(spu_context*)calloc(1,sizeof(*c));assert(c);c->ls=c->ls_store;c->image_id=41;
    uint32_t state=0x12345678;
    for(unsigned r=0;r<128;++r)for(unsigned lane=0;lane<4;++lane){state=state*1664525+1013904223;c->gpr[r]._u32[lane]=state;}
    c->host_depth=2;c->policy_mode=1;c->resident_ovl=3;c->event_mask=9;c->mfc_tag_status=123;c->resv_valid=1;
    return c;
}
static spu_interp_count_row rows[SPU_INTERP_COUNT_CAPACITY];
static uint64_t row(int image,unsigned page,size_t n){
    for(size_t i=0;i<n;++i)if(rows[i].image==image && rows[i].page==page)return rows[i].steps;
    return 0;
}
static HANDLE start_event;
static LONG ready;
static int slow_counters;
static unsigned __stdcall counter_thread(void* argument){
    unsigned id=(unsigned)(uintptr_t)argument;spu_context* c=context();
    InterlockedIncrement(&ready);assert(WaitForSingleObject(start_event,10000)==WAIT_OBJECT_0);
    assert(spu_interp_counts_enabled());
    for(unsigned i=0;i<200000;++i){
        if(slow_counters && !(i&4095))Sleep(1);
        c->image_id=(i&1)?(int32_t)(100+id):-7;c->pc=(i&1)?0x1000:0x3f800;
        spu_interp_count_instruction(c);
    }
    free(c);return 0;
}
static void concurrent_counts(uint64_t before){
    HANDLE threads[4];start_event=CreateEventA(NULL,TRUE,FALSE,NULL);assert(start_event);
    for(unsigned i=0;i<4;++i){threads[i]=(HANDLE)_beginthreadex(NULL,0,counter_thread,(void*)(uintptr_t)i,0,NULL);assert(threads[i]);}
    while(InterlockedCompareExchange(&ready,0,0)!=4)Sleep(1);
    assert(SetEvent(start_event));assert(WaitForMultipleObjects(4,threads,TRUE,20000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<4;++i){DWORD code;assert(GetExitCodeThread(threads[i],&code) && !code);CloseHandle(threads[i]);}
    CloseHandle(start_event);
    spu_interp_count_info info;size_t n=spu_interp_counts_read(rows,SPU_INTERP_COUNT_CAPACITY,&info);
    assert(info.enabled==1 && !info.flags && !info.unbucketed_steps && info.total==before+800000);
    assert(row(-7,0x3f800,n)==400000);
    for(int id=0;id<4;++id)assert(row(100+id,0x1000,n)==100000);
    spu_interp_count_info again;assert(spu_interp_counts_read(NULL,0,&again)==n && !memcmp(&info,&again,sizeof(info)));
    puts("PASS 800000 concurrent recorder updates,one shared/four private keys,negative image,stable non-resetting totals");
}
int main(int argc,char** argv){
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    assert(argc>=2 && spu_interp_counts_descriptor()==SPU_INTERP_COUNT_DESCRIPTOR);
    spu_interp_count_info info;assert(!spu_interp_counts_read(rows,SPU_INTERP_COUNT_CAPACITY,&info));
    assert(info.enabled==-1 && !info.flags && !info.total); /* unarmed is not enabled-zero */
    if(!strcmp(argv[1],"startup-race")){concurrent_counts(0);return 0;}
    const int enabled=spu_interp_counts_enabled();
    if(!strcmp(argv[1],"invalid"))return 71;
    if(!strcmp(argv[1],"hold")){
        assert(enabled);
        slow_counters=1;
        printf("READY %llu %llu\n",(unsigned long long)g_render_perf_epoch,(unsigned long long)g_render_perf_serial);
        fflush(stdout);assert(getchar()=='r');
        concurrent_counts(0);g_render_perf_serial=1;
        puts("DONE");fflush(stdout);assert(getchar()=='q');return 0;
    }
    spu_context* c=context();
    if(!strcmp(argv[1],"full")){
        assert(enabled);
        for(unsigned i=0;i<=SPU_INTERP_COUNT_CAPACITY;++i){c->image_id=10000+i;c->pc=0;spu_interp_count_instruction(c);}
        size_t n=spu_interp_counts_read(rows,SPU_INTERP_COUNT_CAPACITY,&info);
        assert(n==SPU_INTERP_COUNT_CAPACITY && info.flags==SPU_INTERP_COUNT_FULL && info.unbucketed_steps==1 && info.total==SPU_INTERP_COUNT_CAPACITY+1);
        puts("PASS full inventory:explicit flag/unbucketed step,never silent loss");free(c);return 0;
    }
    if(!strcmp(argv[1],"overflow")){
        assert(enabled);c->pc=0;spu_interp_count_instruction(c);
        for(unsigned i=0;i<SPU_INTERP_COUNT_CAPACITY;++i)if(atomic_load(&g_spu_interp_exact_keys[i]))atomic_store(&g_spu_interp_exact_steps[i],UINT64_MAX);
        spu_interp_count_instruction(c);spu_interp_counts_read(rows,SPU_INTERP_COUNT_CAPACITY,&info);
        assert(info.flags==SPU_INTERP_COUNT_OVERFLOW && info.total==UINT64_MAX);
        c->pc=0x400;spu_interp_count_instruction(c);spu_interp_counts_read(rows,SPU_INTERP_COUNT_CAPACITY,&info);
        assert(info.flags==(SPU_INTERP_COUNT_OVERFLOW|SPU_INTERP_COUNT_TOTAL_OVERFLOW) && info.total==UINT64_MAX);
        puts("PASS bucket/total overflow:saturation and explicit invalidity,never wrap to zero");free(c);return 0;
    }
    /* IL r3,7; AI r3,r3,-1; BRNZ r3,-1; STOP0x123. Exactly16 executor
     * dispatches,8 on each side of the1KB boundary,derived from the loop. */
    put32(c,0x3f8,(0x81u<<23)|(7u<<7)|3u);
    put32(c,0x3fc,(0x1cu<<24)|(1023u<<14)|(3u<<7)|3u);
    put32(c,0x400,(0x42u<<23)|(65535u<<7)|3u);put32(c,0x404,0x123);
    uint32_t status=spu_interp_run(c,0x3f8);
    if(!strcmp(argv[1],"cap")){
        assert(enabled && status==0x2000);assert(!spu_interp_counts_read(rows,SPU_INTERP_COUNT_CAPACITY,&info) && !info.total);
        puts("PASS diagnostic step-cap before executor does not count an instruction");free(c);return 0;
    }
    assert(status==0x123 && c->pc==0x408 && c->status==SPU_STATUS_STOPPED_BY_STOP);
    for(unsigned lane=0;lane<4;++lane)assert(!c->gpr[3]._u32[lane]);
    size_t n=spu_interp_counts_read(rows,SPU_INTERP_COUNT_CAPACITY,&info);
    assert(!info.flags && info.enabled==enabled && info.total==(enabled?16:0));
    if(enabled)assert(n==2 && row(41,0,n)==8 && row(41,0x400,n)==8);else assert(!n);
    assert(argc==3);FILE* state=fopen(argv[2],"wb");assert(state);
    c->ls=NULL;assert(fwrite(c,sizeof(*c),1,state)==1);fclose(state);c->ls=c->ls_store;
    /* Neither a compiled rejoin nor an HLE call dispatches an SPU opcode. */
    spu_interp_run_until(c,0x800,0);c->image_id=22;c->gpr[0]._u32[0]=0x800;
    spu_interp_run_until(c,0xa70,0);assert(c->gpr[3]._u32[0]==0x713);
    spu_interp_counts_read(rows,SPU_INTERP_COUNT_CAPACITY,&info);assert(info.total==(enabled?16:0));
    c->image_id=41;put32(c,0x900,(0x81u<<23)|(9u<<7)|5u);spu_interp_run_until(c,0x900,0x904);
    n=spu_interp_counts_read(rows,SPU_INTERP_COUNT_CAPACITY,&info);assert(info.total==(enabled?17:0));
    if(enabled)assert(row(41,0x800,n)==1);
    puts("PASS actual interpreter:literal loop/page counts,STOP,compiled/explicit rejoin,HLE;full context serialized");
    if(enabled)concurrent_counts(17);
    free(c);return 0;
}
