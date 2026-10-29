#include "../port/job_replay_policy.h"
#include <cassert>
#include <cstdio>

int main()
{
    unsigned calls = 0;
    assert(buzz_replay_repeat_checked(0, [&](unsigned pass) {
        assert(pass == 0); ++calls; return 0;
    }) == 0 && calls == 1);

    calls = 0;
    assert(buzz_replay_repeat_checked(8, [&](unsigned pass) {
        assert(pass == calls++); return 0;
    }) == 0 && calls == 8);

    calls = 0;
    assert(buzz_replay_repeat_checked(8, [&](unsigned pass) {
        ++calls; return pass == 0 ? 2 : 0;
    }) == 2 && calls == 8); /* Do not drop divergent jobs, or clear a warning. */

    calls = 0;
    assert(buzz_replay_repeat_checked(8, [&](unsigned pass) {
        ++calls; return pass == 7 ? 2 : 0;
    }) == 2 && calls == 8);

    calls = 0;
    assert(buzz_replay_repeat_checked(8, [&](unsigned pass) {
        ++calls; return pass == 3 ? 1 : 2;
    }) == 1 && calls == 4); /* Fail fast even after input-divergence warnings. */

    calls = 0;
    assert(buzz_replay_repeat_checked(8, [&](unsigned) {
        ++calls; return 1;
    }) == 1 && calls == 1);
    puts("Job replay policy: equal cohort weighting, sticky divergence, fail-fast outputs PASS");
}
