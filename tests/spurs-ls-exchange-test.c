#include "../ps3recomp/runtime/spu/spurs_job_ls.h"
#include "../ps3recomp/runtime/platform/win32_compat.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LS_BYTES = 256 * 1024 };
typedef struct { uint8_t metadata[16], bytes[LS_BYTES]; } Storage;
static atomic_uint done, allocations;
static Storage* allocate(void)
{
    Storage* s = malloc(sizeof *s); assert(s);
    atomic_fetch_add(&allocations, 1);
    return s;
}
static DWORD WINAPI writer(LPVOID arg)
{
    unsigned id = (unsigned)(uintptr_t)arg;
    Storage* spare = NULL;
    for (unsigned n = 0; n < 1000; ++n) {
        if (!spare) spare = allocate();
        memset(spare->bytes, 1 + id * 2 + (n & 1), LS_BYTES);
        void* old = NULL;
        assert(spurs_job_ls_exchange(0x555, spare, spare->bytes, &old));
        assert(old != spare);
        spare = old; /* only returned storage may now be overwritten/freed */
    }
    free(spare);
    atomic_fetch_add(&done, 1);
    return 0;
}
int main(void)
{
    uint8_t* out = malloc(LS_BYTES); assert(out);
    memset(out, 0x77, LS_BYTES); spurs_job_ls_publish(0x111, out);
    Storage* original = allocate(); memset(original->bytes, 1, LS_BYTES);
    void* old = (void*)1;
    assert(!spurs_job_ls_exchange(0, original, original->bytes, &old) && old == (void*)1);
    assert(spurs_job_ls_exchange(0x555, original, original->bytes, &old) && !old);
    HANDLE threads[4];
    for (unsigned n = 0; n < 4; ++n)
        threads[n] = CreateThread(NULL, 0, writer, (LPVOID)(uintptr_t)n, 0, NULL);
    unsigned reads = 0;
    do {
        assert(spurs_job_ls_read(0x555, 0, LS_BYTES, out));
        assert(out[0] >= 1 && out[0] <= 8);
        for (unsigned n = 1; n < LS_BYTES; ++n) assert(out[n] == out[0]);
        ++reads;
    } while (atomic_load(&done) != 4);
    for (unsigned n = 0; n < 4; ++n) {
        assert(threads[n]); assert(WaitForSingleObject(threads[n], 10000) == WAIT_OBJECT_0);
        CloseHandle(threads[n]);
    }
    assert(atomic_load(&allocations) <= 5);
    assert(spurs_job_ls_read(0x111, 0, LS_BYTES, out));
    for (unsigned n = 0; n < LS_BYTES; ++n) assert(out[n] == 0x77);
    /* Legacy publication into an owned slot preserves ownership and contents. */
    memset(out, 0xcc, LS_BYTES); spurs_job_ls_publish(0x555, out);
    Storage* next = allocate(); memset(next->bytes, 0xdd, LS_BYTES);
    assert(spurs_job_ls_exchange(0x555, next, next->bytes, &old) && old);
    Storage* returned = old;
    for (unsigned n = 0; n < LS_BYTES; ++n) assert(returned->bytes[n] == 0xcc);
    free(returned);
    assert(spurs_job_ls_read(0x555, 0, LS_BYTES, out));
    for (unsigned n = 0; n < LS_BYTES; ++n) assert(out[n] == 0xdd);
    free(out);
    printf("PASS: 4000 exchanges, %u full 256-KiB untorn reads, bounded reuse, chain isolation, legacy interoperability\n", reads);
}
