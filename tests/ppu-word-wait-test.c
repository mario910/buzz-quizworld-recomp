#include "../ps3recomp/runtime/platform/win32_compat.h"
static unsigned wakes;
static void tracked_wake(CONDITION_VARIABLE* cv)
{
    __atomic_fetch_add(&wakes,1,__ATOMIC_RELAXED);
    WakeConditionVariable(cv);
}
#define WakeConditionVariable tracked_wake
#include "../port/ppu_word_wait.c"
#undef WakeConditionVariable
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"Failed line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static uint8_t* memory;
static uint32_t requested, completed;
static const uint32_t watched = 0x100A0;
static void publish(uint32_t ea, uint32_t size)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    buzz_ppu_word_dma_written(ea,size);
}
static DWORD WINAPI producer(void* unused)
{
    (void)unused;
    for (uint32_t n=1;n<=20000;++n) {
        while (__atomic_load_n(&requested,__ATOMIC_ACQUIRE)!=n) YieldProcessor();
        /* Both publication-before-registration and after-park paths. */
        if ((n & 7)==0) Sleep(1);
        __atomic_store_n((uint32_t*)(memory+watched),0,__ATOMIC_RELEASE);
        switch(n%4) {
        case 0: publish(watched,4);break;
        case 1: publish(watched-32,128);break;
        case 2: publish(watched+3,1);break; /* last byte overlaps the word */
        default: publish(watched-8192,16384);break;
        }
        __atomic_store_n(&completed,n,__ATOMIC_RELEASE);
    }
    return 0;
}
static DWORD WINAPI parallel_waiter(void* argument)
{
    const uint32_t ea=watched+128*(uint32_t)(uintptr_t)argument;
    while (__atomic_load_n((uint32_t*)(memory+ea),__ATOMIC_ACQUIRE))
        CHECK(buzz_ppu_word_wait(memory,ea,0x288BE0));
    return 0;
}
int main(void)
{
    memory=calloc(1,0x20000);CHECK(memory);
    CHECK(!buzz_ppu_word_wait(NULL,watched,0));
    CHECK(!buzz_ppu_word_wait(memory,watched+1,0));
    CHECK(!buzz_ppu_word_wait(memory,0xFFF0,0));
    CHECK(!buzz_ppu_word_wait(memory,0xF0000000u,0));
    CHECK(buzz_ppu_word_wait(memory,watched,0)); /* already clear */
    /* Full registry cannot suppress the original syscall fallback. */
    *(uint32_t*)(memory+watched)=1;
    for(unsigned n=0;n<BUZZ_PPU_WAITERS;++n)buzz_ppu_waiters[n].active=1;
    CHECK(!buzz_ppu_word_wait(memory,watched,0));
    for(unsigned n=0;n<BUZZ_PPU_WAITERS;++n)buzz_ppu_waiters[n].active=0;
    CHECK(buzz_ppu_word_wait(memory,watched,0)); /* bounded unnotified writer */
    CHECK(*(uint32_t*)(memory+watched)==1 && !buzz_ppu_wait_lines);
    buzz_ppu_waiters[0].active=1; buzz_ppu_waiters[0].address=watched;
    buzz_ppu_waiters[0].memory=memory; buzz_ppu_update_filter();
    publish(watched,4); /* still busy: do not wake merely because DMA touched it */
    CHECK(!wakes);
    *(uint32_t*)(memory+watched)=0;
    publish(0xFFFFFFF0u,32);publish(watched,0);publish(watched-4,4);publish(watched+4,4);
    CHECK(!wakes);
    publish(watched+3,1);CHECK(wakes==1);
    buzz_ppu_waiters[0].active=0;buzz_ppu_update_filter();
    HANDLE parallel[8];
    for(unsigned n=0;n<8;++n) {
        *(uint32_t*)(memory+watched+128*n)=1;
        parallel[n]=CreateThread(NULL,0,parallel_waiter,(void*)(uintptr_t)n,0,NULL);CHECK(parallel[n]);
    }
    unsigned active=0;ULONGLONG deadline=GetTickCount64()+5000;
    while(active!=8 && GetTickCount64()<deadline) {
        AcquireSRWLockExclusive(&buzz_ppu_wait_lock);active=0;
        for(unsigned n=0;n<BUZZ_PPU_WAITERS;++n)active+=buzz_ppu_waiters[n].active!=0;
        ReleaseSRWLockExclusive(&buzz_ppu_wait_lock);SwitchToThread();
    }
    CHECK(active==8);
    for(unsigned n=0;n<8;++n)__atomic_store_n((uint32_t*)(memory+watched+128*n),0,__ATOMIC_RELEASE);
    publish(watched,1024);
    CHECK(WaitForMultipleObjects(8,parallel,TRUE,5000)==WAIT_OBJECT_0);
    for(unsigned n=0;n<8;++n)CloseHandle(parallel[n]);
    CHECK(!buzz_ppu_wait_lines);
    HANDLE thread=CreateThread(NULL,0,producer,NULL,0,NULL);CHECK(thread);
    for (uint32_t n=1;n<=20000;++n) {
        __atomic_store_n((uint32_t*)(memory+watched),1,__ATOMIC_RELEASE);
        __atomic_store_n(&requested,n,__ATOMIC_RELEASE);
        while (__atomic_load_n((uint32_t*)(memory+watched),__ATOMIC_ACQUIRE))
            CHECK(buzz_ppu_word_wait(memory,watched,0x288BE0));
        while (__atomic_load_n(&completed,__ATOMIC_ACQUIRE)!=n)YieldProcessor();
        CHECK(!buzz_ppu_wait_lines);
    }
    CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CloseHandle(thread);
    free(memory);
    puts("PASS: 20000 producer/waiter races, partial/large DMA ranges, repeated slot reuse, timeout and admission fallbacks");
    return 0;
}
