#include "../ps3recomp/include/ps3emu/log_sample.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
static unsigned count,hits[8];
#ifdef _WIN32
static DWORD WINAPI work(void* opaque)
#else
static void* work(void* opaque)
#endif
{
    unsigned sum=0;
    for(unsigned n=0;n<100000;++n)sum+=ps3_log_sample(&count,17);
    hits[(uintptr_t)opaque]=sum;return 0;
}
int main(void)
{
#ifdef _WIN32
    HANDLE threads[8];
    for(uintptr_t n=0;n<8;++n){threads[n]=CreateThread(NULL,0,work,(void*)n,0,NULL);assert(threads[n]);}
    assert(WaitForMultipleObjects(8,threads,TRUE,10000)==WAIT_OBJECT_0);
    for(unsigned n=0;n<8;++n)CloseHandle(threads[n]);
#else
    pthread_t threads[8];
    for(uintptr_t n=0;n<8;++n)assert(!pthread_create(&threads[n],NULL,work,(void*)n));
    for(unsigned n=0;n<8;++n)assert(!pthread_join(threads[n],NULL));
#endif
    unsigned sum=0;for(unsigned n=0;n<8;++n)sum+=hits[n];assert(sum==17 && count==17);
    count=UINT_MAX;assert(!ps3_log_sample(&count,UINT_MAX));assert(count==UINT_MAX);
    count=UINT_MAX-1;assert(ps3_log_sample(&count,UINT_MAX));assert(count==UINT_MAX);
    count=0;assert(!ps3_log_sample(&count,0));assert(count==0);
    puts("PASS: 800000 concurrent sample attempts, exact bound, saturation and zero limit");
}
