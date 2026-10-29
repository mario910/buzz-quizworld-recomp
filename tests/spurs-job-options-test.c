#include "../ps3recomp/runtime/spu/spurs_job_options.h"
#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#define change(k,v) _putenv_s(k,v)
#else
#define change(k,v) setenv(k,v,1)
#endif
int main(void)
{
    SpursJobOptions cache = {0}, scratch;
    change("SPURS_JOB_ABI_MIN_IMAGE", "42");
    change("SPURS_JOB_ABI_STAGE_IMAGE", "43");
    change("SPURS_JOB_DESCDUMP", "2");
    change("SPURS_JOB_KERNEL_MIRROR", "0"); /* presence, not truthiness */
    const SpursJobOptions* opts = spurs_job_options_for_call(&cache, &scratch, 1);
    assert(opts->min_present && opts->min_image == 42);
    assert(opts->stage_present && opts->stage_image == 43);
    assert(opts->desc_dump && opts->desc_detail == 2 && opts->kernel);
    change("SPURS_JOB_ABI_MIN_IMAGE", "7");
    opts = spurs_job_options_for_call(&cache, &scratch, 1);
    assert(opts->min_image == 42);
    opts = spurs_job_options_for_call(&cache, &scratch, 0);
    assert(opts->min_image == 7); /* live behavior retained */
    cache.initialized = 0;
    opts = spurs_job_options_for_call(&cache, &scratch, 1);
    assert(opts->min_image == 7);
    puts("SPURS options: startup cache, live updates, explicit reset and presence semantics PASS");
}
