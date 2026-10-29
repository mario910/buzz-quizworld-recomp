#include "../ps3recomp/runtime/syscalls/timer_short_spin.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    assert(ps3_short_sleep_ticks(2,10000000,10)==20);
    assert(ps3_short_sleep_ticks(2,10000001,10)==21);
    assert(ps3_short_sleep_ticks(2,1,10)==1);
    assert(!ps3_short_sleep_ticks(0,10000000,10));
    assert(!ps3_short_sleep_ticks(2,10000000,0));
    assert(!ps3_short_sleep_ticks(2,10000000,101));
    assert(!ps3_short_sleep_ticks(11,10000000,10));
    assert(!ps3_short_sleep_ticks(UINT64_MAX,UINT64_MAX,100));
    assert(!ps3_short_sleep_ticks(2,UINT64_MAX,100));
    assert(!ps3_short_sleep_ticks(2,0,100));
    assert(ps3_short_sleep_ticks(100,1000000000000ull,100)==100000000);
#ifdef _WIN32
    LARGE_INTEGER frequency;assert(QueryPerformanceFrequency(&frequency));
    assert(!ps3_short_sleep_spin(2,frequency.QuadPart,0));
    assert(!ps3_short_sleep_spin(101,frequency.QuadPart,100));
    assert(!ps3_short_sleep_spin(2,-1,100));
    int64_t minimum=INT64_MAX,maximum=0,total=0;
    for(unsigned i=0;i<10000;++i){LARGE_INTEGER begin,end;QueryPerformanceCounter(&begin);
        assert(ps3_short_sleep_spin(2,frequency.QuadPart,10));QueryPerformanceCounter(&end);
        int64_t elapsed=end.QuadPart-begin.QuadPart;
        assert(elapsed>=ps3_short_sleep_ticks(2,frequency.QuadPart,10));
        if(elapsed<minimum)minimum=elapsed;if(elapsed>maximum)maximum=elapsed;total+=elapsed;}
    printf("Real 2-us waits: min=%.3f mean=%.3f max=%.3f us; no early return\n",minimum*1e6/frequency.QuadPart,total*1e6/frequency.QuadPart/10000,maximum*1e6/frequency.QuadPart);
#endif
    puts("PASS: bounded short-sleep selection, ceiling deadlines, overflow rejection and reference fallback");return 0;
}
