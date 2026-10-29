#include "../ps3recomp/runtime/spu/spurs_command_wait.h"
#include "../ps3recomp/runtime/platform/win32_compat.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

static uint8_t memory[4096];
static atomic_uint done;
static void store(unsigned address, uint64_t value)
{
    value = __builtin_bswap64(value);
    memcpy(memory + address, &value, sizeof value);
    spurs_command_notify64(address);
}
static DWORD WINAPI waiter(LPVOID ignored)
{
    (void)ignored;
    spurs_command_wait(memory, 128, 256, 128, 1000);
    atomic_store(&done, 1);
    return 0;
}
int main(void)
{
    /* Already published work/new chain PC must not park even without a wake. */
    store(256, 128); store(128, 0x7f);
    ULONGLONG start = GetTickCount64();
    spurs_command_wait(memory, 128, 256, 128, 1000);
    assert(GetTickCount64() - start < 500);
    store(128, 0x800000012ull); store(256, 512);
    start = GetTickCount64();
    spurs_command_wait(memory, 128, 256, 128, 1000);
    assert(GetTickCount64() - start < 500);
    for (unsigned n = 0; n < 200; ++n) {
        store(128, 0x800000012ull); store(256, 128); atomic_store(&done, 0);
        HANDLE thread = CreateThread(NULL, 0, waiter, NULL, 0, NULL); assert(thread);
        ULONGLONG deadline = GetTickCount64() + 500;
        while (!__atomic_load_n(&g_spurs_command_wait_lines, __ATOMIC_ACQUIRE)) {
            assert(GetTickCount64() < deadline); SwitchToThread();
        }
        /* Acquiring/releasing registration lock via the notification ensures
         * the predicate read is complete before this fixture mutates memory. */
        spurs_command_written(768);
        store(n & 1 ? 128 : 256, n & 1 ? 0x7f : 512);
        assert(WaitForSingleObject(thread, 500) == WAIT_OBJECT_0);
        assert(atomic_load(&done)); CloseHandle(thread);
        assert(!__atomic_load_n(&g_spurs_command_wait_lines, __ATOMIC_ACQUIRE));
    }
    /* Timeout without an instrumented producer is retained. */
    store(128, 0x800000012ull); store(256, 128);
    spurs_command_wait(memory, 128, 256, 128, 2);
    assert(!__atomic_load_n(&g_spurs_command_wait_lines, __ATOMIC_ACQUIRE));
    puts("PASS: prepublication, chain-PC change, 200 notified waits, registration cleanup, fallback timeout");
}
