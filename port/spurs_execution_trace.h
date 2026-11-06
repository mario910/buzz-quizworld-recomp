#pragma once
#include "ps3emu/ps3_ftrace.h"
#include "../ps3recomp/runtime/platform/win32_compat.h"
#ifndef _WIN32
#include <time.h>
#endif

/* Diagnostic only. Windows reports scheduled thread cycles; POSIX reports
 * thread CPU nanoseconds. Neither is elapsed job time or an FPS measurement. */
static inline uint64_t buzz_spurs_cpu_stamp(void)
{
#ifdef _WIN32
    ULONG64 value = 0;
    return QueryThreadCycleTime(GetCurrentThread(), &value) ? value : 0;
#else
    struct timespec value;
    return clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) == 0 ?
        (uint64_t)value.tv_sec * 1000000000ull + value.tv_nsec : 0;
#endif
}
static inline void buzz_spurs_execution_end(int image, uint64_t start, uint64_t cpu)
{
    if (!start) return;
    const uint64_t finish_cpu = buzz_spurs_cpu_stamp();
    const uint64_t finish = ps3_ft_now();
    const uint64_t delta = finish_cpu >= cpu ? finish_cpu - cpu : UINT64_MAX;
    /* Saturation/failure is explicit; never silently wrap a long job. */
    const uint32_t count = cpu && finish_cpu && delta < UINT32_MAX ? (uint32_t)delta : UINT32_MAX;
    ps3_ft_span(117, GetCurrentThreadId(), (uint32_t)image, count, start, finish);
}
