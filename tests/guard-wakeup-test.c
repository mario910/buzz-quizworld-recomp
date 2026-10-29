#include "../ps3recomp/runtime/platform/win32_compat.h"
#include <stdatomic.h>
#ifdef _WIN32
#include <stdlib.h>
static int test_setenv(const char* name, const char* value, int overwrite)
{
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name,value);
}
#define setenv test_setenv
#endif
static SRWLOCK* target_lock;
/* Stretch ONLY the safety poll in this fixture to one second. An observed
 * wake within 500 ms must therefore be a real notification, not the poll. */
static BOOL stretch_wait(CONDITION_VARIABLE* cv, SRWLOCK* lock, DWORD ms, ULONG flags)
{
    return SleepConditionVariableSRW(cv, lock, lock == target_lock ? 1000 : ms, flags);
}
#define SleepConditionVariableSRW stretch_wait
#define CHAIN_RESUME_TEST_MAIN chain_resume_unused_main
#include "chain-resume-test.c"
#undef CHAIN_RESUME_TEST_MAIN
#undef SleepConditionVariableSRW
static atomic_int result;
static DWORD WINAPI waiter(LPVOID count)
{
    int total = (int)(uintptr_t)count, value = 0;
    for (int n = 0; n < total; ++n) {
        value = jg_wait(0x20000,0x10000,0x30000);
        if (value != 1) break;
    }
    atomic_store(&result,value);
    return 0;
}
static void initialized(unsigned credits)
{
    vm_write64(0x10000,0x30000);
    assert(cellSpursJobGuardInitialize(0x10000,0x20000,credits,1,1) == 0);
    atomic_store(&result,0);
}
static HANDLE start_wait(unsigned iterations)
{
    HANDLE thread = CreateThread(NULL,0,waiter,(LPVOID)(uintptr_t)iterations,0,NULL);
    assert(thread);
    ULONGLONG start = GetTickCount64();
    while (!vm_read32(0x20030)) { assert(GetTickCount64()-start < 3000); Sleep(1); }
    return thread;
}
static void finish(HANDLE thread, int wanted, unsigned ms)
{
    assert(WaitForSingleObject(thread,ms) == WAIT_OBJECT_0);
    CloseHandle(thread); assert(atomic_load(&result) == wanted);
}
static DWORD WINAPI notifier(LPVOID count)
{
    for (unsigned n = 0; n < (unsigned)(uintptr_t)count; ++n)
        assert(cellSpursJobGuardNotify(0x20000) == 0);
    return 0;
}
int main(void)
{
    setenv("SPURS_JOB_KERNEL_MIRROR","1",1);
    target_lock = &s_jobguards_lock;
    initialized(3);
    HANDLE thread = start_wait(1);
    for (int n = 0; n < 3; ++n) assert(cellSpursJobGuardNotify(0x20000) == 0);
    finish(thread,1,500);
    initialized(1); thread = start_wait(1);
    vm_write32(0x20000,0); jc_spu_line_committed(0x20000);
    finish(thread,1,500); /* native atomic release */
    initialized(1); thread = start_wait(1);
    vm_write64(0x10000,0x30008); jc_spu_line_committed(0x10000);
    finish(thread,2,500); /* changed published PC */
    initialized(3);
    for (int n = 0; n < 6; ++n) assert(cellSpursJobGuardNotify(0x20000) == 0);
    assert(jg_wait(0x20000,0x10000,0x30000) == 1);
    assert(jg_wait(0x20000,0x10000,0x30000) == 1);
    assert(s_jobguards[jg_find(0x20000)].pending == 0);
    initialized(1); thread = start_wait(2000);
    HANDLE producers[4];
    for (unsigned n = 0; n < 4; ++n) producers[n] = CreateThread(NULL,0,notifier,(LPVOID)(uintptr_t)500,0,NULL);
    for (unsigned n = 0; n < 4; ++n) {
        assert(producers[n] && WaitForSingleObject(producers[n],10000) == WAIT_OBJECT_0);
        CloseHandle(producers[n]);
    }
    finish(thread,1,10000);
    puts("Guard wakeups: PPU/native/PC change without safety poll, retained credits, 2000 concurrent notifications PASS");
}
