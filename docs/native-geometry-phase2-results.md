# Native geometry phase 2: results and open gates

Status: **in progress**, as of 2026-10-31. This is an interim results report,
not a declaration that phase 2 is complete. The active goal remains phase 2;
the next goal will be created after its remaining implementations and gates
are resolved and this report is finalized and committed.

The phase 0/1 base is `719f325`. Contracts and reproduction details are in
[native-geometry-kernels.md](native-geometry-kernels.md); the roadmap and
tracker are in [native-geometry-plan.md](native-geometry-plan.md).

## Correctness evidence

The full image-41 corpus contains 19,508 jobs and 58,195 transfers. Both
checked execution and separate actual-native execution of all five implemented
groups pass every job, with zero input divergences or failures. The existing
Stage F baseline is held in checked mode for these whole-job checks; its
temporary volatile-register exception is unchanged. Every new kernel is
checked against the full register, LS, control and continuation state.

| Kernel/group | Switch bit | Strict groups / fallbacks in full corpus | Live strict matches | Commit |
|---|---|---|---|---|
| Unit vectors `9148/92A0` | 0 | 6,628 / 5,439 | >=600,000 | `a7ad7b2` |
| Skinning `B880/B934/B940` | 1 | 5,700 / 413 | >=100,000 on final build | `2416a77` |
| Halfword encoder `4AB0` | 2 | 18,776 / 0 | >=500,000 | `81dadfb` |
| Halfword decoder `CAD8/CB78` | 5 | 9,063 / 3,368 | >=200,000 | `eb29523` |
| Input pointers `0988/09E0` | 10 | 19,508 / 0 | >=300,000 | `635e2f2` |

Group counts are invocations, not unique jobs. Unsupported inputs retain the
lift and are not counted as successful native comparisons. Synthetic tests
also cover non-IEEE FP edges, wrapping pointers/counters, padding, sentinels,
host returns and unchanged-state rejection of unsupported inputs.

Latest private corpus logs: `analysis/native-geometry/phase2/pointers-mode{2,1}.log`.
The original game save remains unchanged; tests use private saves.

## Completed scene measurements

Each row compares the same executable and settings within its own session,
with reference/native/native/reference order. Absolute FPS from different
sessions should not be compared as an accumulated improvement.

| Kernel | Menu FPS | Round FPS | Current default |
|---|---|---|---|
| Unit vectors | 31.60 -> 31.94 (+1.08%) | 39.31 -> 40.65 (+3.41%) | on |
| Skinning | 31.98 -> 31.87 (-0.36%) | 39.50 -> 40.24 (+1.88%) | off; mixed FPS, CPU-cost evidence not yet collected |

Both groups have normal menu/round native renderer dumps and completed
reversed pairs. Private reports are `phase2/unit24-ab-report.json` and
`phase2/skin-ab-report.json` under `analysis/native-geometry/`.

The halfword encoder/decoder and input-pointer builder are still default-off.
Their actual-native menu dumps have been inspected and look normal. Their
shared-reference series `p2-batch1-ab` has completed the menu runs and is
running the round runs. Round images, complete FPS results and final default
decisions remain open. Masks are 1/5/33/1025; each new kernel is enabled alone
above the fixed unit-vector baseline. The executable and initial save hashes
are checked throughout the series.

## CPU work as a separate benefit

Unchanged FPS does not establish that a native replacement is useless. If
another stage limits the frame, less geometry-worker CPU work can leave FPS
unchanged. The default decision may therefore use a repeatable reduction in
worker CPU time or cycles per job/frame, once every correctness/render gate
passes and FPS/frame times show no material regression.

Record matched worker cost and whole-process cost, completed frame/job counts,
scene phase, executable/settings and reversal order. Normalize by completed
work; cycles/second or CPU percentage alone can rise when a faster version
processes more frames. Measure CPU time and cycle counters independently;
do not convert elapsed microbenchmark time into claimed CPU cycles.

No matched CPU-cycle savings have been established yet. The local kernel
microbenchmarks are encouraging, but do not by themselves justify defaults.
Existing RIP samplers read cycle counters while suspending threads and are
explicitly diagnostic; CPU-cost comparisons need counters collected without
suspending workers. Run them separately from the existing frozen FPS series.
The skinning default may be reconsidered with this evidence; its mixed FPS
result is not by itself a permanent rejection.

## Remaining work before phase closure

- Resolve `7A90`, `51E0`, `78A8`, `97A8`, `4750`, `4808` and `28F0`; private
  prototypes are not counted as completed replacements.
- Finish the three pending groups' round/render/FPS gates and default decisions.
- Collect matched CPU-cost evidence wherever the default decision relies on
  CPU savings rather than an FPS gain.
- Validate the final selected defaults, update this report with complete
  measurements and commit the phase results.
- Only then close phase 2 and set the next goal to phase 3. Keep the planned
  strict Stage F correction as the final roadmap phase 7.
