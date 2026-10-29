#include "../ps3recomp/runtime/spu/spurs_job_pool.h"
#include "../ps3recomp/runtime/platform/win32_compat.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static _Thread_local uint32_t lane;
void spurs_job_release_thread_context(void) {}
void spurs_job_set_spu_num(uint32_t n) { lane = n; }
uint32_t spurs_job_get_spu_num(void) { return lane; }
static atomic_uint lane_active[6], seen[4][16], total;
static LARGE_INTEGER frequency;
static unsigned delay_us;
static void job(uint32_t ea, int index, uint32_t stride, uint32_t chain)
{
    unsigned group = chain - 0x1000;
    assert(group < 4 && index >= 0 && index < 16 && stride == 256);
    assert(ea == 0x2000 + group * 0x1000 + (unsigned)index * stride);
    assert(lane > 0 && lane <= 4);
    assert(atomic_fetch_add(&lane_active[lane], 1) == 0);
    assert(atomic_fetch_add(&seen[group][index], 1) == 0);
    assert(!spurs_job_pool_run(0x55, 5, 4, ea, 1, 256, chain, index, job));
    if (delay_us) {
        LARGE_INTEGER start, now;
        QueryPerformanceCounter(&start);
        do { QueryPerformanceCounter(&now); }
        while ((uint64_t)(now.QuadPart - start.QuadPart) * 1000000 <
               (uint64_t)frequency.QuadPart * delay_us);
    } else if ((index & 3) == 0) SwitchToThread();
    atomic_fetch_add(&total, 1);
    assert(atomic_fetch_sub(&lane_active[lane], 1) == 1);
}
static DWORD WINAPI producer(LPVOID arg)
{
    unsigned group = (unsigned)(uintptr_t)arg;
    lane = 99;
    for (unsigned iteration = 0; iteration < 2000; ++iteration) {
        unsigned count = 1 + iteration % 15;
        for (unsigned i = 0; i < 16; ++i) atomic_store(&seen[group][i], 0);
        assert(spurs_job_pool_run(0x55, 5, 4, 0x2000 + group * 0x1000,
                                  count, 256, 0x1000 + group, 0, job));
        assert(lane == 99);
        for (unsigned i = 0; i < 16; ++i)
            assert(atomic_load(&seen[group][i]) == (i < count));
    }
    return 0;
}
int main(void)
{
    QueryPerformanceFrequency(&frequency);
    HANDLE callers[4];
    for (unsigned i = 0; i < 4; ++i)
        callers[i] = CreateThread(NULL, 0, producer, (LPVOID)(uintptr_t)i, 0, NULL);
    for (unsigned i = 0; i < 4; ++i) {
        assert(callers[i]); assert(WaitForSingleObject(callers[i], 30000) == WAIT_OBJECT_0);
        CloseHandle(callers[i]);
    }
    unsigned expected = 0;
    for (unsigned i = 0; i < 2000; ++i) expected += 4 * (1 + i % 15);
    assert(atomic_load(&total) == expected);
    printf("PASS: 8000 lists, %u exactly-once jobs, four producers, lane exclusion, nested refusal, TLS restore\n", expected);
    delay_us = 12;
    lane = 99;
    for (unsigned count = 2; count <= 16; count *= 2) {
        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);
        for (unsigned n = 0; n < 500; ++n) {
            for (unsigned i = 0; i < 16; ++i) atomic_store(&seen[0][i], 0);
            assert(spurs_job_pool_run(0x55, 5, 4, 0x2000, count, 256, 0x1000, 0, job));
        }
        QueryPerformanceCounter(&end);
        printf("BENCH caller_help=%s jobs/list=%u job_work=12us mean_list=%.1fus\n",
               getenv("SPURS_POOL_CALLER_HELP"), count,
               (double)(end.QuadPart - start.QuadPart) * 1e6 / frequency.QuadPart / 500);
    }
    spurs_job_pool_shutdown();
}
