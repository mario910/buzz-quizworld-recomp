#define main original_stack_main
#include "../ps3recomp/runtime/spu/tests/test_spurs_job_stack.c"
#undef main
#include "../ps3recomp/runtime/spu/spurs_job_ls.h"
#include <assert.h>
extern void spurs_job_set_owner(uint32_t);
static unsigned depth;
static void nested_entry(spu_context* ctx)
{
    assert(ctx->ls == ctx->ls_store && ((uintptr_t)ctx & 15) == 0);
    assert(ctx->ls[0x3e000] == 0); /* recycled contexts must still start zeroed */
    memset(ctx->ls + 0x3e000, depth ? 0x5a : 0xa5, 256);
    if (!depth) {
        depth = 1;
        spurs_job_set_owner(0x222);
        assert(spu_run_spurs_job(nested_entry, 42, JOB_EA, 256) == 0);
        depth = 0;
        for (unsigned n = 0; n < 256; ++n) assert(ctx->ls[0x3e000 + n] == 0xa5);
    }
}
int main(void)
{
    int result = original_stack_main();
    assert(!result);
    build_job(0, 0);
    uint8_t bytes[256];
    for (unsigned run = 0; run < 1000; ++run) {
        spurs_job_set_owner(0x111);
        assert(spu_run_spurs_job(nested_entry, 42, JOB_EA, 256) == 0);
        assert(spurs_job_ls_read(0x111, 0x3e000, sizeof bytes, bytes));
        for (unsigned n = 0; n < sizeof bytes; ++n) assert(bytes[n] == 0xa5);
        assert(spurs_job_ls_read(0x222, 0x3e000, sizeof bytes, bytes));
        for (unsigned n = 0; n < sizeof bytes; ++n) assert(bytes[n] == 0x5a);
        if (run % 17 == 0) spurs_job_release_thread_context();
    }
    spurs_job_release_thread_context();
    puts("PASS: real SPURS runner, 1000 nested dispatches, owner/context isolation, aligned LS, zeroed reuse, thread cache cleanup");
}
