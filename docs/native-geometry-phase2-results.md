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
checked execution and separate actual-native execution of all eleven implemented
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
| Triangle stitching `7A90` | 3 | 12,675 / 6,833 | >=256 | `7d0f413` |
| Index expansion `78A8` | 6 | 12,385 / 7,123 | >=256 | `4cddd99` |
| Packed 11/11/10 output `5008/51E0/51C8` | 4 | 2,665 / 1,759 | >=256 | `e18619a` |

Group counts are invocations, not unique jobs. Unsupported inputs retain the
lift and are not counted as successful native comparisons. Synthetic tests
also cover non-IEEE FP edges, wrapping pointers/counters, padding, sentinels,
host returns and unchanged-state rejection of unsupported inputs.

Latest private corpus logs: `analysis/native-geometry/phase2/final11-mode{2,1}.log`.
The final three groups' live log is `analysis/vk-scene-p2-final11-live.stderr.log`;
their source/executable binding is `phase2/final11-build.json`. All 13 ranked
blocks are now covered by eleven groups, without counting `9148/92A0` or
`4750/4808` twice. Final-three renderer/A/B gates remain open.

Series `p2-batch2-ab` has normal native menu/round dumps for raw float3 input,
raw float3 output and word fill, plus the default mask. Its bit-8 menu reverse run
`p2-batch2-ab-menu-7-1` is invalid because a standalone compile/test overlapped
the timed window. All original bit-8 menu results were replaced by the fresh
four-run `p2-batch2-float-menu-clean` A/B/B/A; valid original rounds and other
kernels' menu results were retained. `p2-batch2-ab/exclusions.json` records
the timestamps/scope. The corrected report `phase2/batch2-corrected-ab-report.json`
validates both source sets and all EXE/save/readback hashes. Its private
reproduction script is `phase2/batch2-corrected-report.py`; the invalid row
does not contribute to any result. Source EXE SHA256:
`cd9b242b577ed41e92252f8bbcf5eecc198973ae5708a1d53b5e0d6e9206b4fb`.
The original game save remains unchanged; tests use private saves.

## Completed scene measurements

Each row compares the same executable and settings within its own session,
with reference/native/native/reference order. Absolute FPS from different
sessions should not be compared as an accumulated improvement.

| Kernel | Menu FPS | Round FPS | Current default |
|---|---|---|---|
| Unit vectors | 31.60 -> 31.94 (+1.08%) | 39.31 -> 40.65 (+3.41%) | on |
| Skinning | 31.98 -> 31.87 (-0.36%) | 39.50 -> 40.24 (+1.88%) | on; repeated worker/process CPU savings, negligible menu frame-time change |
| Halfword encoder | 31.73 -> 31.93 (+0.65%) | 39.69 -> 40.16 (+1.18%) | off; one menu pair negative, no repeatable CPU savings |
| Halfword decoder | 31.73 -> 32.10 (+1.18%) | 39.69 -> 40.75 (+2.66%) | on |
| Input pointers | 31.73 -> 31.94 (+0.67%) | 39.69 -> 40.26 (+1.44%) | off; one round pair negative, no repeatable CPU savings |
| Raw float3 input | 32.52 -> 32.62 (+0.28%) | 41.19 -> 41.29 (+0.23%) | on; repeated cycle/process-CPU-time savings |
| Raw float3 output | 32.32 -> 32.73 (+1.25%) | 41.19 -> 42.06 (+2.09%) | on; all FPS pairs positive |
| BE word fill | 32.32 -> 32.44 (+0.36%) | 41.19 -> 40.91 (-0.69%) | off; mixed process indicators and round decline |

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
| Raw float3 input | +0.30%, +0.26% | +0.09%, +0.37% |
| Raw float3 output | +0.97%, +1.55% | +2.85%, +1.33% |
| BE word fill | -0.55%, +1.28% | -0.04%, -1.33% |

These small effects have visible run-to-run variation; a positive average
does not establish a repeatable gain for every kernel. Decoder reversal
pairs are all positive. Intermediate default mask **35 (0x23)** selected unit
vectors, skinning and the halfword decoder. Both checked and actual-native
replay with no explicit mask pass all 19,508 jobs / 58,195 transfers, with
zero differences or failures (`phase2/defaults35-mode{2,1}.log`). Generated
geometry C is byte-identical to the eight-kernel validation; only the
runtime default changed. The raw-format measurements below subsequently
justify extending this mask to 803.
A 45 s live run without a mask override reports `kernels=0x23`, at least
256 strict matches for each selected group and over 100,000 for skinning,
with no differences (`analysis/vk-scene-p2-defaults35-live.stderr.log`).
`phase2/defaults35-build.json` binds those sources and frozen EXE.

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
They call the kernel directly; scene/CPU figures also include the runtime
dispatch and per-group atomic accounting used by checked/native selection.
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

A separate CPU series `p2-cpu-raw-menu` completed bits 8/9/12 against baseline
35, with no readbacks, the same frozen raw2 EXE and an exact map binding.
Private report: `phase2/cpu-raw-menu-report.json`.

| Kernel | Pool cycles/frame | Process cycles/frame | Pool CPU ms/frame | Process CPU ms/frame |
|---|---|---|---|---|
| Raw float3 input | -0.31% (-0.42%, -0.19%) | -0.62% (-1.17%, -0.08%) | +0.40% (+0.84%, -0.05%) | -0.31% (-0.45%, -0.17%) |
| Raw float3 output | -0.28% (-0.42%, -0.13%) | -0.27% (-0.51%, -0.02%) | +1.27% (+1.59%, +0.95%) | +0.38% (+0.59%, +0.17%) |
| BE word fill | -0.60% (-0.91%, -0.29%) | -0.80% (-1.60%, +0.01%) | +0.59% (+0.54%, +0.64%) | -0.22% (-0.63%, +0.20%) |

Raw input has small but repeated cycle/process-CPU-time savings, supporting
its four positive FPS pairs. Raw output's four positive FPS pairs justify
enabling it; CPU time increases slightly despite fewer reported cycles, so
no CPU-time saving is claimed for it. Word fill stays off: worker cycles
fall, but process indicators are mixed and both round FPS pairs decline.
Default mask **803 (0x323)** adds bits 8/9 to 35. Both checked and actual-native
replay without a mask override pass all 19,508 jobs / 58,195 transfers, with
zero differences/failures (`phase2/defaults803-mode{2,1}.log`). A 45 s live
run without a mask override reports `0x323` and at least 256 strict matches
for every selected group, with no differences (`vk-scene-p2-defaults803-live.stderr.log`).
`phase2/defaults803-build.json` binds the frozen release used for the last
three groups' scene gates. Its generated geometry C is identical to the
eleven-kernel validation; only the default changed.

## Remaining work before phase closure

- Finish `7A90`, `51E0` and `78A8` renderer/A/B gates and default decisions.
- Validate the final selected defaults again when the remaining phase kernels resolve.
- Collect matched CPU-cost evidence wherever the default decision relies on
  CPU savings rather than an FPS gain.
- Validate the final selected defaults, update this report with complete
  measurements and commit the phase results.
- Only then close phase 2 and set the next goal to phase 3. Keep the planned
  strict Stage F correction as the final roadmap phase 7.
