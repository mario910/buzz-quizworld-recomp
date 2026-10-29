#ifndef BUZZ_JOB_REPLAY_POLICY_H
#define BUZZ_JOB_REPLAY_POLICY_H

/* Replay verdicts: 0 exact, 1 output/state mismatch, 2 read-input divergence.
 * A differing captured read is not an output failure, but must remain visible.
 * Repeat every non-failing job, not just the exact-input subset; otherwise the
 * aggregate benchmark overweights that subset by the requested repeat count. */
template<class Run>
int buzz_replay_repeat_checked(unsigned repeat, Run run)
{
    if (!repeat) repeat = 1;
    int verdict = 0;
    for (unsigned pass = 0; pass < repeat; ++pass) {
        const int result = run(pass);
        if (result != 0 && result != 2) return 1;
        if (result == 2) verdict = 2;
    }
    return verdict;
}
#endif
