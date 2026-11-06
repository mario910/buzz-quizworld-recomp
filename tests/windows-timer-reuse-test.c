#include "../port/windows_timer_wait.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"Failed line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static DWORD WINAPI worker(void* parameter)
{
    (void)parameter;
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    HANDLE previous = NULL;
    for (unsigned n = 0; n < 8; ++n) {
        QueryPerformanceCounter(&start);
        CHECK(buzz_windows_timer_wait(1000));
        QueryPerformanceCounter(&end);
        CHECK(end.QuadPart-start.QuadPart >= frequency.QuadPart/1000);
        HANDLE timer = FlsGetValue(buzz_timer_slot);
        CHECK(timer && (!previous || timer == previous));
        previous = timer;
    }
    return 0;
}
int main(void)
{
    CHECK(!buzz_windows_timer_wait(0));
    CHECK(!buzz_windows_timer_wait(UINT64_MAX));
    /* Warm process/FLS initialization before the leak check. */
    HANDLE first = CreateThread(NULL,0,worker,NULL,0,NULL);
    CHECK(first && WaitForSingleObject(first,5000)==WAIT_OBJECT_0);
    CloseHandle(first);
    DWORD before, after;
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    for (unsigned round=0;round<4;++round) {
        HANDLE threads[16];
        for (unsigned n=0;n<16;++n) CHECK(threads[n]=CreateThread(NULL,0,worker,NULL,0,NULL));
        CHECK(WaitForMultipleObjects(16,threads,TRUE,10000)==WAIT_OBJECT_0);
        for (unsigned n=0;n<16;++n) CloseHandle(threads[n]);
    }
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after));
    CHECK(before==after);
    puts("PASS: 520 waits, concurrent ownership, minimum delay, reuse and thread-exit cleanup");
    return 0;
}
