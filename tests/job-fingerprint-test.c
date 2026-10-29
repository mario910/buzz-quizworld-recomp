/* Link with --gc-sections: exercise the exact production cache in isolation. */
#include "../ps3recomp/runtime/spu/spu_workload.c"
#include <assert.h>

int main(void)
{
    uint8_t images[20][4096];
    for (unsigned i = 0; i < 20; ++i) {
        memset(images[i], (int)i, sizeof images[i]);
        assert(spu_job_fingerprint(images[i], sizeof images[i]) ==
               spu_workload_fingerprint(images[i], sizeof images[i]));
    }
    for (unsigned repeat = 0; repeat < 4; ++repeat)
        for (unsigned i = 0; i < 20; ++i) {
            assert(spu_job_fingerprint(images[i], sizeof images[i]) ==
                   spu_workload_fingerprint(images[i], sizeof images[i]));
            images[i][repeat * 997] ^= 0x5a; /* same address, changed content */
            assert(spu_job_fingerprint(images[i], sizeof images[i]) ==
                   spu_workload_fingerprint(images[i], sizeof images[i]));
            assert(spu_job_fingerprint(images[i], 17) ==
                   spu_workload_fingerprint(images[i], 17));
        }
    spu_workload_clear_job_cache();
    spu_workload_clear_job_cache();
    assert(spu_job_fingerprint(images[0], 4096) ==
           spu_workload_fingerprint(images[0], 4096));
    spu_workload_clear_job_cache();
    puts("job fingerprint: hits, mutations, resize, eviction and cleanup PASS");
    return 0;
}
