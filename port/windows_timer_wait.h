#pragma once
#include <windows.h>
#include <stdint.h>

/* Reuse a high-resolution timer per fiber/thread. FLS closes the handle on
 * thread/fiber exit; a plain TLS HANDLE would leak for short-lived workers.
 * Failure returns to the original syscall implementation. */
static INIT_ONCE buzz_timer_once = INIT_ONCE_STATIC_INIT;
static DWORD buzz_timer_slot = FLS_OUT_OF_INDEXES;

static VOID CALLBACK buzz_timer_close(PVOID value)
{
    if (value) CloseHandle((HANDLE)value);
}

static BOOL CALLBACK buzz_timer_init(PINIT_ONCE once, PVOID argument, PVOID* context)
{
    (void)once; (void)argument; (void)context;
    buzz_timer_slot = FlsAlloc(buzz_timer_close);
    return buzz_timer_slot != FLS_OUT_OF_INDEXES;
}

static int buzz_windows_timer_wait(uint64_t usec)
{
    if (!usec || usec > INT64_MAX / 10 ||
        !InitOnceExecuteOnce(&buzz_timer_once, buzz_timer_init, NULL, NULL)) return 0;
    HANDLE timer = (HANDLE)FlsGetValue(buzz_timer_slot);
    if (!timer) {
        timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002,
                                     TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!timer) return 0;
        if (!FlsSetValue(buzz_timer_slot, timer)) {
            CloseHandle(timer);
            return 0;
        }
    }
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)usec * 10;
    return SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE) &&
           WaitForSingleObject(timer, INFINITE) == WAIT_OBJECT_0;
}
