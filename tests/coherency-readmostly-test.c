#include "../ps3recomp/runtime/platform/win32_compat.h"
#include <stdatomic.h>
#include <assert.h>
#define main original_coherency_test_main
#include "../ps3recomp/runtime/spu/tests/test_spu_coherency.c"
#undef main

static atomic_uint published, finished;
static DWORD WINAPI reserve_lines(LPVOID unused)
{
    (void)unused;
    for (unsigned round = 0; round < 100; ++round)
        for (unsigned i = 0; i < 1024; ++i) {
            spu_lockline_lock();
            spu_coh_reserve(NULL, 0x70000000 + i * 128);
            spu_lockline_unlock();
            if (!round) atomic_store_explicit(&published, i + 1, memory_order_release);
        }
    atomic_store(&finished, 1);
    return 0;
}
static DWORD WINAPI read_lines(LPVOID unused)
{
    (void)unused;
    do {
        unsigned count = atomic_load_explicit(&published, memory_order_acquire);
        for (unsigned i = 0; i < count; ++i)
            assert(spu_coh_is_reserved(0x70000000 + i * 128));
        assert(!spu_coh_is_reserved(0x71000000));
    } while (!atomic_load(&finished));
    return 0;
}
int main(void)
{
    assert(sizeof(s_coh_armed) == 64 && ((uintptr_t)&s_coh_armed & 63) == 0);
    HANDLE threads[5];
    for (unsigned i = 0; i < 4; ++i) threads[i] = CreateThread(NULL, 0, read_lines, NULL, 0, NULL);
    threads[4] = CreateThread(NULL, 0, reserve_lines, NULL, 0, NULL);
    for (unsigned i = 0; i < 5; ++i) {
        assert(threads[i]); assert(WaitForSingleObject(threads[i], 10000) == WAIT_OBJECT_0);
        CloseHandle(threads[i]);
    }
    puts("PASS: 102400 reservations, concurrent append-only bitmap visibility, no false positives, isolated 64-byte armed flag");
    return original_coherency_test_main();
}
