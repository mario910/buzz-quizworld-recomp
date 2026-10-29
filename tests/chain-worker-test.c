#include "../ps3recomp/runtime/spu/spurs_chain_worker.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

static atomic_uint active[32], completed[32], first_tid[32];
static DWORD WINAPI work(LPVOID arg)
{
    unsigned slot = (unsigned)(uintptr_t)arg;
    assert(slot < 32);
    assert(atomic_fetch_add(&active[slot], 1) == 0);
    unsigned tid = GetCurrentThreadId(), expect = 0;
    if (!atomic_compare_exchange_strong(&first_tid[slot], &expect, tid)) assert(expect == tid);
    if (atomic_load(&completed[slot]) % 17 == 0) SwitchToThread();
    atomic_fetch_add(&completed[slot], 1);
    assert(atomic_fetch_sub(&active[slot], 1) == 1);
    return 0;
}
static DWORD WINAPI other(LPVOID arg) { (void)arg; assert(0); return 0; }
static DWORD WINAPI producer(LPVOID arg)
{
    unsigned slot = (unsigned)(uintptr_t)arg;
    for (unsigned i = 0; i < 2000; ++i) assert(spurs_chain_worker_submit(slot, work));
    return 0;
}
int main(void)
{
    assert(!spurs_chain_worker_submit(32, work));
    assert(!spurs_chain_worker_submit(0, NULL));
    for (unsigned round = 0; round < 3; ++round) {
        HANDLE producers[8];
        for (unsigned i = 0; i < 4; ++i) {
            atomic_store(&completed[i], 0); atomic_store(&first_tid[i], 0);
        }
        for (unsigned i = 0; i < 8; ++i)
            producers[i] = CreateThread(NULL, 0, producer, (LPVOID)(uintptr_t)(i % 4), 0, NULL);
        for (unsigned i = 0; i < 8; ++i) {
            assert(producers[i]); assert(WaitForSingleObject(producers[i], 15000) == WAIT_OBJECT_0);
            CloseHandle(producers[i]);
        }
        assert(!spurs_chain_worker_submit(0, other));
        spurs_chain_workers_shutdown(); /* drains, never drops pending callbacks */
        for (unsigned i = 0; i < 4; ++i) {
            assert(atomic_load(&completed[i]) == 4000); assert(!atomic_load(&active[i]));
        }
    }
    puts("PASS: 48000 submissions, concurrent producers, serial slot ownership, persistent TID, drain and restart");
}
