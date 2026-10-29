#include "../ps3recomp/runtime/spu/spurs_completion_queue.h"
#include "../ps3recomp/runtime/platform/win32_compat.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
static atomic_uint ready,ack;
static uint64_t ticket;
static DWORD WINAPI consumer(LPVOID unused) {
    for(unsigned n=0;n<2000;++n) {
        atomic_store(&ready,n+1);
        uint64_t bits=spurs_completion_take(ticket,1000);
        assert(bits==(1ull<<(n%64)));
        atomic_store(&ack,n+1);
    }
    return 0;
}
int main(void) {
    assert(!spurs_completion_begin(0,2048,4));assert(!spurs_completion_begin(128,3,4));
    assert(!spurs_completion_begin(128,2048,65));assert(!spurs_completion_begin(0xffffff80,2048,2));
    // Already-set flags are covered by the initial scan, regardless of whether
    // a notification happened before registration.
    spurs_completion_notify(0x10000);
    ticket=spurs_completion_begin(0x10000,2048,64);assert(ticket);
    assert(spurs_completion_take(ticket,0)==UINT64_MAX);
    spurs_completion_notify(0x10000+63*2048);
    spurs_completion_notify(0x10000+63*2048+2);
    spurs_completion_notify(0x10000+128); // unrelated line
    assert(spurs_completion_take(ticket,0)==(1ull<<63));
    assert(!spurs_completion_take(ticket,1));
    HANDLE thread=CreateThread(NULL,0,consumer,NULL,0,NULL);assert(thread);
    for(unsigned n=0;n<2000;++n) {
        ULONGLONG deadline=GetTickCount64()+2000;
        while(atomic_load(&ready)!=n+1){assert(GetTickCount64()<deadline);SwitchToThread();}
        spurs_completion_notify(0x10000+(n%64)*2048);
        while(atomic_load(&ack)!=n+1){assert(GetTickCount64()<deadline);SwitchToThread();}
    }
    assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);CloseHandle(thread);
    uint64_t old=ticket;spurs_completion_end(ticket);
    ticket=spurs_completion_begin(0x10000,2048,1);assert(ticket&&ticket!=old);
    assert(!spurs_completion_take(old,0));spurs_completion_end(old);
    assert(spurs_completion_take(ticket,0)==1);spurs_completion_end(ticket);
    uint64_t all[32];for(unsigned n=0;n<32;++n){all[n]=spurs_completion_begin(0x10000+n*128,2048,1);assert(all[n]);spurs_completion_take(all[n],0);}
    assert(!spurs_completion_begin(0x20000,2048,1));
    for(unsigned n=0;n<32;++n){spurs_completion_notify(0x10000+n*128);assert(spurs_completion_take(all[n],0)==1);}
    for(unsigned n=0;n<32;++n)spurs_completion_end(all[n]);
    puts("PASS: 2000 check/park races, 64 slots, deduplication, prepublication, timeout, isolation, stale tickets, capacity/reuse");
}
