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
checked execution and separate actual-native execution of all eight implemented
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
| Raw float3 decoder `97A8` | 8 | 18,968 / 540 | >=256 | `cc9c877` |
| Raw float3 output `46FC/4750/4808/47F0` | 9 | 4,975 / 2,226 | >=256 | `eee46de` |
| BE word fill `28F0` | 12 | 19,068 / 435 | >=256 | `f23c3c7` |

Group counts are invocations, not unique jobs. Unsupported inputs retain the
lift and are not counted as successful native comparisons. Synthetic tests
also cover non-IEEE FP edges, wrapping pointers/counters, padding, sentinels,
host returns and unchanged-state rejection of unsupported inputs.

Latest private corpus logs: `analysis/native-geometry/phase2/raw-fill-mode{2,1}.log`.
The new groups' live log is `analysis/vk-scene-p2-raw-fill-live.stderr.log`;
their source/executable binding is `phase2/raw-fill-build.json`. Renderer
and A/B gates remain open for these three groups; their defaults are off.
In-progress series `p2-batch2-ab` has normal native menu dumps for all three
groups and the default mask. Its original bit-8 menu reverse run
`p2-batch2-ab-menu-7-1` is invalid because a standalone compile/test overlapped
the timed window. All original bit-8 menu results will be replaced by a fresh
four-run menu A/B/B/A; original round results and other kernels' valid runs
can be retained. The private `p2-batch2-ab/exclusions.json` records the
timestamps and scope. This series is not yet performance evidence.
The original game save remains unchanged; tests use private saves.

## Completed scene measurements

Each row compares the same executable and settings within its own session,
with reference/native/native/reference order. Absolute FPS from different
sessions should not be compared as an accumulated improvement.

| Kernel | Menu FPS | Round FPS | Current default |
|---|---|---|---|
| Unit vectors | 31.60 -> 31.94 (+1.08%) | 39.31 -> 40.65 (+3.41%) | on |
| Skinning | 31.98 -> 31.87 (-0.36%) | 39.50 -> 40.24 (+1.88%) | on; repeated worker/process CPU savings, negligible menu frame-time change |
| Halfword encoder | 31.73 -> 31.93 (+0.65%) | 39.69 -> 40.16 (+1.18%) | off pending CPU evidence; one menu pair negative |
| Halfword decoder | 31.73 -> 32.10 (+1.18%) | 39.69 -> 40.75 (+2.66%) | on |
| Input pointers | 31.73 -> 31.94 (+0.67%) | 39.69 -> 40.26 (+1.44%) | off pending CPU evidence; one round pair negative |

Both groups have normal menu/round native renderer dumps and completed
reversed pairs. Private reports are `phase2/unit24-ab-report.json` and
`phase2/skin-ab-report.json` under `analysis/native-geometry/`.

The shared-reference series `p2-batch1-ab` completed all sixteen runs. The
actual-native menu and round dumps for all three new groups look normal.
Masks are 1/5/33/1025; each new kernel is enabled alone above the fixed
unit-vector baseline. Private report: `phase2/batch1-ab-report.json`, bound
EXE SHA256 `ce12f9c1280896ed5bf851b03135b2c3949b15f80f71a5157d45f86020c65c31`.
The executable and initial save hashes were checked throughout the series.

| Kernel | Menu pair changes | Round pair changes |
|---|---|---|
| Halfword encoder | -0.45%, +1.75% | +1.94%, +0.44% |
| Halfword decoder | +0.07%, +2.31% | +4.36%, +1.02% |
| Input pointers | +0.25%, +1.09% | +3.10%, -0.17% |

These small effects have visible run-to-run variation; a positive average
does not establish a repeatable gain for every kernel. Decoder reversal
pairs are all positive. The new default mask **35 (0x23)** selects unit
vectors, skinning and the halfword decoder. Both checked and actual-native
replay with no explicit mask pass all 19,508 jobs / 58,195 transfers, with
zero differences or failures (`phase2/defaults35-mode{2,1}.log`). Generated
geometry C is byte-identical to the eight-kernel validation; only the
runtime default changed. All other kernel switches remain off.
A 45 s live run without a mask override reports `kernels=0x23`, at least
256 strict matches for each selected group and over 100,000 for skinning,
with no differences (`analysis/vk-scene-p2-defaults35-live.stderr.log`).
`phase2/defaults35-build.json` binds the final sources and frozen EXE.

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

The local kernel microbenchmarks do not by themselves justify defaults.
Existing RIP samplers read cycle counters while suspending threads and are
explicitly diagnostic; CPU-cost comparisons need counters collected without
suspending workers. Run them separately from the existing frozen FPS series.
The new [CPU measurement observer](native-geometry-cpu-measurements.md) reads
counters without suspension and has passed unit/API self-tests. Initial
series `p2-cpu-menu1` was rejected at its first boundary: thread discovery
ran inside the boundary and exceeded the 50 ms tolerance. It is invalid
evidence. Discovery now runs before the window; series `p2-cpu-menu2` is
completed isolated skinning/encoder/pointer comparisons, with readbacks
disabled in every arm and the observer source hash held fixed. Eight runs
use masks 1/3/5/1025/1025/5/3/1 in the 40..98 s menu window. Private report:
`phase2/cpu-menu2-report.json`, same frozen EXE as the FPS batch. Both endpoint
queries complete in less than a millisecond and satisfy the boundary and
frame-denominator gates.

| Kernel | Pool cycles/frame | Process cycles/frame | Pool CPU ms/frame | Process CPU ms/frame |
|---|---|---|---|---|
| Skinning | -0.95% (-1.23%, -0.66%) | -0.73% (-0.91%, -0.54%) | -0.42% (-1.23%, +0.41%) | -1.20% (-1.64%, -0.75%) |
| Halfword encoder | +0.78% (+1.70%, -0.14%) | +1.15% (+2.53%, -0.24%) | +1.03% (+1.56%, +0.49%) | +1.21% (+2.73%, -0.33%) |
| Input pointers | +0.67% (+1.57%, -0.24%) | +0.70% (+1.88%, -0.48%) | +1.76% (+2.79%, +0.71%) | +0.63% (+2.03%, -0.78%) |

Negative means less work; parentheses contain the two reversal pairs. The
skinning cycle reduction repeats in both pool/process pairs, supported by
lower process CPU time in both pairs. Pool CPU time alone is mixed. Given
the small menu FPS change (-0.36%) and round gain (+1.88%), this supports
enabling skinning. This is a modest
measured CPU benefit, not an energy claim. Encoder and pointer measurements
do not show repeatable savings; both remain off. The decoder's all-positive
FPS pairs support enabling it.

## Remaining work before phase closure

- Resolve `7A90`, `51E0` and `78A8`; private
  prototypes are not counted as completed replacements.
- Finish raw float3 input/output and fill renderer/A/B gates and default decisions.
- Validate the final selected defaults again when the remaining phase kernels resolve.
- Collect matched CPU-cost evidence wherever the default decision relies on
  CPU savings rather than an FPS gain.
- Validate the final selected defaults, update this report with complete
  measurements and commit the phase results.
- Only then close phase 2 and set the next goal to phase 3. Keep the planned
  strict Stage F correction as the final roadmap phase 7.
