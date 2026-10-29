#include "../ps3recomp/runtime/spu/spurs_job_ls.h"
#include "../ps3recomp/runtime/platform/win32_compat.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
enum { LS_SIZE = 256 * 1024 };
static atomic_int done;
static DWORD WINAPI writer(LPVOID unused)
{
    (void)unused;
    uint8_t* bytes = malloc(LS_SIZE);
    assert(bytes);
    for (unsigned n = 0; n < 1000; ++n) {
        memset(bytes, (n & 1) ? 0xaa : 0x55, LS_SIZE);
        spurs_job_ls_publish(0x222, bytes);
    }
    free(bytes);
    atomic_store(&done, 1);
    return 0;
}
int main(void)
{
    uint8_t* bytes = malloc(LS_SIZE), out[128];
    assert(bytes);
    assert(!spurs_job_ls_read(0x111, 0, 8, out));
    memset(bytes, 0x33, LS_SIZE); spurs_job_ls_publish(0x111, bytes);
    memset(bytes, 0x55, LS_SIZE); spurs_job_ls_publish(0x222, bytes);
    assert(spurs_job_ls_read(0x111, 16, sizeof out, out));
    for (unsigned n = 0; n < sizeof out; ++n) assert(out[n] == 0x33);
    assert(!spurs_job_ls_read(0x111, LS_SIZE - 4, 8, out));
    assert(!spurs_job_ls_read(0x111, 0xffffffffu, 8, out));
    HANDLE worker = CreateThread(NULL, 0, writer, NULL, 0, NULL);
    assert(worker);
    while (!atomic_load(&done)) {
        assert(spurs_job_ls_read(0x222, LS_SIZE / 2, sizeof out, out));
        assert(out[0] == 0x55 || out[0] == 0xaa);
        for (unsigned n = 1; n < sizeof out; ++n) assert(out[n] == out[0]);
        assert(spurs_job_ls_read(0x111, 0, sizeof out, out));
        for (unsigned n = 0; n < sizeof out; ++n) assert(out[n] == 0x33);
    }
    assert(WaitForSingleObject(worker, 10000) == WAIT_OBJECT_0);
    CloseHandle(worker); free(bytes);
    puts("SPURS LS: isolated chain snapshots, bounds, concurrent untorn reads PASS");
}
