# Phase3 native whole-job performance

Status: **menu regression; keep JOB0/geometry827/new flags0**. Phase3 remains
open. These results evaluate the combined native configuration, not individual
kernels and not the still-unobserved cold formats.

## Method and binding

Two separate fresh reversed series use JOB0/JOB1/JOB1/JOB0. The FPS series
has no CPU observer; the CPU series uses existing OS process/thread counters
and a map-bound presentation counter. Both use the40..98s menu camera window,
100s runs, uncapped/background/muted settings, no GPU dumps and no interpreter
observer. Every run starts from the same private saved profiles. No game build,
CPU tests or diagnostic profiling runs concurrently.

- Frozen build: `build-vulkan-phase3-input-routing-v3`, ABI48/61 stages.
- EXE: `bec8b85965eb662a171004af33060e30dc0e868a50e016d2c4cb45e2ea6efbd6`.
- MAP: `729a12aef82a51d8c8ddeb5810345681440886c734583ae5043725e55023da5d`.
- Initial save: `92ed9caf2b90849aac2e0236f334f3138548ef288b519badcdfb48d2342f2845`.
- Both arms retain geometry mask827 and native StageF1. JOB0 disables the new
  job options; JOB1 enables all recognized production options, with audit off.
  The complete option maps are bound in the progress JSON, along with tool,
  runtime-options source, save, EXE and MAP hashes.
- Every native measurement has nonzero actual whole-job/raw0 coverage checked
  from its runtime log. Corpus/live correctness remains separately established.

## FPS series

Mean FPS: **27.74 -> 25.30 (-8.78%)**.
Reversed pair changes: -8.63%, -8.92%.

| Order | JOB | FPS | Median interval(ms) | p90(ms) | p99(ms) |
| --- | --- | --- | --- | --- | --- |
| 1 | 0 | 27.65 | 35.20 | 47.58 | 53.33 |
| 2 | 1 | 25.26 | 39.59 | 51.48 | 59.30 |
| 3 | 1 | 25.34 | 39.71 | 51.55 | 59.47 |
| 4 | 0 | 27.82 | 35.23 | 47.90 | 53.44 |

## Separate CPU-cost series

The denominator is completed presentations, with stable identities for the
four named SPURS pool lanes and the process. Raw snapshots are recomputed;
missed boundaries, frame uncertainty, counter resets, incomplete/reordered
runs, changed settings, changed files and zero native coverage fail reporting.
CPU time is summed thread work, so it can exceed the wall interval per frame.

| Cost per completed frame | JOB0 | JOB1 | Change | Pair changes |
| --- | --- | --- | --- | --- |
| SPURS workers: million cycles | 438.06 | 507.78 | +15.92% | +16.55%, +15.29% |
| SPURS workers: CPU ms | 118.98 | 138.03 | +16.02% | +15.02%, +17.01% |
| Whole process: million cycles | 797.28 | 870.53 | +9.19% | +9.81%, +8.57% |
| Whole process: CPU ms | 216.90 | 236.71 | +9.13% | +8.94%, +9.31% |

Native has higher worker and process cost per completed frame in both pairs.
The total cycles consumed over the fixed wall window alone would conceal
this: the native run completes fewer frames. This is why raw CPU percentage
or cycles/second is not the optimization decision.

## Decision, limitations and next work

Do not enable the combined native configuration by default. It regresses
menu FPS and normalized CPU cost. This result does not attribute the loss to
an individual function or invalidate its exactness checks. The useful next
step is a separate diagnostic profile, then targeted changes and a new frozen
validation/measurement series. Keep the strict checker and fallback accounting.

The old36.3FPS baseline and45..50FPS forecast were different samples/estimates;
these paired same-EXE values are the applicable comparison for this build.
Round reversed A/B and CPU measurements have not run. Round interpreter counts,
cold real/live coverage and the other phase3/default gates remain open.
Flat FPS with repeatable CPU savings may still justify individual options
after their correctness/render gates; this aggregate configuration has no such
savings. Strict StageF reconstruction remains phase7 LAST.

The first attempted series (`job-fps-menu-v1`) is invalid: an empty replay
variable activated replay and the app exited before the scene. The corrected
runner removes replay and dump variables entirely. The failed files and source
are preserved; no partial attempt is included in these results. Seven report
tests pass, including incomplete/mixed/hash-tampered series, missing native
coverage and a cached summary hiding a CPU counter reset.

Private evidence is under `analysis/native-geometry/phase3/`:
`job-fps-menu-v2-report.json`, `job-cpu-menu-v1-report.json`, their complete
four-run progress/phase/log/CSV sets, and `job-ab-v2-cpu-binding.json`.
`frozen-job-menu-v2-measure-scene.ps1` and `frozen-job-menu-v2-runner.ps1`
bind the exact scripts used. A subsequent helper-only change initializes the
screenshot assembly before future round launches; it does not change game
code or retrospectively change these menu runs. Original source, saves and
older frozen artifacts remain pinned. The continuing objective stays active.


## Diagnostic profile and formatter admission candidate

Separate10s diagnostic menu samples (renderer40..50s) use the prior V3 frozen
EXE/MAP and verified names/IDs of all four SPURS pool lanes. They contain20,813
native and20,681 reference pool samples. The sampler briefly suspends threads
and filters inactive cycle intervals, so these are active leaf distributions,
not CPU-time percentages or new FPS measurements. All code symbols, including
native C++, are resolved; folded aliases/padding remain explicit and external
caller guesses are excluded.

| Native leaf | Samples | Active pool sample share |
| --- | --- | --- |
| `read_geometry_formatter` | 744 | 3.57% |
| `geometry_unit_values` | 434 | 2.09% |
| `geometry_unit_final_abi` | 288 | 1.38% |
| `geometry_unit_setup_abi` | 199 | 0.96% |

The formatter's admission walk checks each eventual write before conversion.
A new opt-in `BUZZ_GEOMETRY_JOB_FORMATTER_SPANS=0/1` (default0) replaces that
walk with a conservative span proof for regular columns. It retains the exact
walk whenever the proof is unavailable, including physical LS wrap, signed
cursor wrap, metadata inside gaps or unusual advances. Decoder arithmetic,
ordered stores, restored registers and strict checkers are unchanged.
The [conversion audit](native-geometry-conversion-audit.md#formatter-column-span-proof)
records the proof and validation. Six replay arms each pass19,508exact jobs /
58,195transfers, with unchanged native ledgers; the new freeze also passes
>=35,000 strict live formatter returns and>=45,000 whole-job/I/O checks.

This candidate has **no measured FPS or CPU benefit yet**. It is a different
frozen EXE (`build-vulkan-phase3-formatter-spans-v1`), not the V3 binary measured
above. Its own0/1 reversal, actual-native menu/round frames and remaining gates
are pending; the option stays default0 and the combined driver stays off.
The earlier menu regression remains the last measured aggregate result.


## Formatter span proof: isolated menu measurements

The new frozen `build-vulkan-phase3-formatter-spans-v1` has now passed inspected
actual-native menu and actual-question round readbacks, with exact lossless
PPM/PNG pairs. The round displays the David Beckham1999 question,answers,timer,
host and Adam UI. These GPU-readback functional runs are excluded from timings.

Two further reversed series change **only FORMATTER_SPANS0/1**. All four arms
in each series keep JOB1 and every other production job option enabled,with
audit off,mask827/StageF1,the same initial save and the same frozen EXE/MAP.
This is not another JOB0/JOB1 comparison. FPS and CPU observation are separate;
both use40..98s of fresh100s menu runs,without readbacks or interpreter sampling.
The report checks the actual runtime span option as well as native coverage.

FPS: **25.01 ->25.73 (+2.88%)**, pair changes +3.19%, +2.58%.

| Cost per completed frame | Span0 | Span1 | Change | Reversed pair changes |
| --- | --- | --- | --- | --- |
| SPURS workers: million cycles | 507.84 | 491.74 | -3.17% | -3.42%, -2.91% |
| SPURS workers: CPU ms | 138.29 | 132.41 | -4.25% | -3.52%, -4.99% |
| Whole process: million cycles | 870.05 | 852.08 | -2.07% | -2.36%, -1.77% |
| Whole process: CPU ms | 236.11 | 230.44 | -2.40% | -2.01%, -2.79% |

The menu benefit is repeatable in these two pairs,including lower worker and
process CPU cost per completed frame. It does not establish the aggregate
JOB0/JOB1 benefit: both arms already use the combined native driver. The prior
V3 aggregate regression remains a separate result on its separately bound EXE.

FORMATTER_SPANS remains default0 pending its round reversal/CPU checks and
remaining acceptance. JOB0/geometry827/other new flags0 remain unchanged.
Round interpreted-count proof,cold real/live paths and the other phase3 gates
are still open. Ten report tests cover actual JOB1 in both option arms,unchanged
other options and the runtime option marker,in addition to earlier validity
checks. Private results: `spans-fps-menu-v1-report.json`,
`spans-cpu-menu-v1-report.json`,their complete progress/raw-counter/log/CSV sets,
`formatter-spans-v1-cpu-binding.json` and `formatter-spans-v1-frame-qa.json`.


## Round scene-proof capture correction

The first isolated round FPS attempt (`spans-fps-round-v1`) is **invalid**.
The inspected195s PrintWindow PNG contains a title bar and a black Vulkan
client. Its own launcher/game were stopped;the partial CSV/logs/phases/image
and exact failed scripts are preserved privately. No four-run report or round
performance result was accepted. A separate functional Windows Graphics
Capture attempt failed twice with CreateForMonitor0x80070057;no result was
inferred from it.

The SDK now supports opt-in `PS3RECOMP_VK_DUMP_ONCE_AT_MS`. It allocates the
staging buffer at setup,waits until the requested time,records one GPU copy,
waits for its fence,writes the exact PPM and retires the request even on write
failure. Continuous presentation readback remains0. Thirteen GPU smoke tests
pass,including exact first-clear pixels retained after later different clears,
legacy periodic dumps,normal window readback,explicit readback suppression and
invalid delay/path rejection. Thirteen report tests pass,including rejection
of late,repeated,continuous or unbound captures and inconsistent RGB pixels.

The round runner requests one190s capture,converts its exact RGB to PNG at195s
and keeps the200..270s measurement window free of capture work. Acceptance
requires a single successful completion with readback0,a captured-frame CSV
row before195s,lossless PPM/PNG agreement and explicit visual inspection.
It uses no PrintWindow or screenshot-helper compilation in these runs.

New frozen `build-vulkan-phase3-span-capture-v1` (EXE3df8d057,SDKac0f6aef)
has passed native menu and actual green challenge/result-arena captures.
The round image shows the host,Adam robot and Poprawne0 scoreboard;it is
not a question screenshot. The individual readbacks are bound to matching
frame-timing rows and completed functional runs. The15 native geometry sources,
generated C,ABI48,61 counters,strict checkers and StageF exception are unchanged.
These are functional runs,not accepted timing results.

The earlier b95e457f menu benefit remains bound to that earlier EXE. Round
FPS/CPU and repeat menu pairs must use the new frozen EXE before the own-switch
performance gate can close. JOB0/geometry827/newflags0/SPANS0 remain unchanged;
phase3,round interpreter proof,cold real/live and final default gates stay open.
Private evidence includes `spans-fps-round-v1-invalid.json`,
`spans-capture-v1-frames-qa.json`,`spans-capture-v1-frozen-hashes.json` and the
new `spans-capture-v1-cpu-binding.json`.


New-freeze validation after the capture repair:all six replay arms each pass
19,508exact jobs/58,195transfers,zero divergent inputs/zero failures. Enabled
arms execute19,508native jobs with zero whole-job fallback;every earlier stage
ledger is unchanged,and SPANS0/1 agree. A separate80s checked menu run passes
at least45,000 strict whole-job/I/O and35,000 strict formatter returns,without
DIFF,first-fallback or unrecordable errors. Periodic live counts are lower
bounds,not final fallback totals. These correctness checks do not replace
cold real/live,interpreter or new-EXE performance/default gates.


## Isolated formatter span proof: round FPS

Four complete fresh runs on the capture-enabled3df8d057 EXE/MAP9b027e64
use SPANS0/1/1/0,with actual JOB1 in every arm and all other production options1,
audit0,geometry827/StageF1 and the same initial private save. This measures
one option within the native driver. It does not measure aggregate JOB0/JOB1.
The window is200..270s;FPS has no CPU/interpreter observer or concurrent
build,test or diagnostic profiler. Each run has one190s GPU readback,completed
and converted before the window;its retained continuous-readback setting is0.

All four hash-bound native PPM/PNG images were inspected and are normal actual
rounds. Three show the green result arena/host/Adam/Poprawne0;the third run also
shows the question about Nice in France. Questions/cameras vary,so this is not
a deterministic identical-frame workload. The first control's navigation
round marker occurs at171.20s,the others near155.1s;all have proven actual
round content before200s. Every measured window is complete and monotonic,
with nonzero actual native whole-job/raw0 coverage and no DIFF,unrecordable or
first-fallback marker.

Mean FPS: **30.93 ->31.52 (+1.91%)**;pair changes **+2.87%,+0.96%**.

| Order | SPANS | FPS | Median interval(ms) | p90(ms) | p99(ms) |
| --- | --- | --- | --- | --- | --- |
| 1 | 0 | 30.74 | 31.78 | 39.41 | 44.18 |
| 2 | 1 | 31.62 | 31.16 | 38.45 | 43.58 |
| 3 | 1 | 31.43 | 31.12 | 37.60 | 54.80 |
| 4 | 0 | 31.13 | 31.61 | 38.91 | 44.06 |

The higher average is consistent across these two reversed pairs,but the tail
result is mixed. Native run3 has p99=54.80ms versus44.06ms in its paired final
control,while the first native pair improves43.58ms versus44.18ms. Its median
and p90 improve. The isolated mean benefit does **not** close acceptance of
frame-time tails;do not attribute the one-run p99 increase to a cause without
further matched evidence. Keep the option default0 pending separate round CPU,
repeat menu FPS/CPU on this same3df EXE and follow-up round tail checks.
The earlier B95 menu result remains separately bound;no same-EXE combined
menu/round performance gate is claimed yet. JOB0/geometry827/new flags0 remain.

Private evidence:`spans-fps-round-v2-report.json`,the complete series progress,
phase/raw log/CSV/readback set and`spans-fps-round-v2/scene-qa.json`. The public
reporter recomputes FPS and validates the capture timing/frame/RGB/inspection
bindings. Phase3,cold real/live,round interpreted-count and final default gates
remain open. Next is the separate round CPU reversal on this same frozen EXE.


## Isolated formatter span proof: round CPU

A separate four-run CPU reversal on the same3df8d057 EXE/MAP9b027e64 retains
JOB1/all other production options1,audit0,geometry827/StageF1 and the same
private initial save;only SPANS changes0/1/1/0. The map-bound observer reads
OS CPU/cycle counters at200/270 renderer seconds and normalizes by completed
presentations. Raw snapshots recompute successfully,with four stable named
SPURS lane identities per run,valid boundaries/frame uncertainty and no reset.
No build,CPU tests,diagnostic profiler or other game ran concurrently.

All four native readbacks were inspected after the series:normal green
challenge/result arenas,host,Adam robot and Poprawne0;two include the normal
Adam0 portrait panel. Exactly one GPU copy per run completes before195s,
continuous readback stays0,and the lossless PPM/PNG and timing-row/QA hashes
pass. There are no DIFF,first-fallback or unrecordable markers.

| CPU cost per completed frame | Span0 | Span1 | Change | Reversed pair changes |
| --- | --- | --- | --- | --- |
| SPURS workers: million cycles | 411.08 | 394.60 | -4.01% | -3.28%, -4.74% |
| SPURS workers: CPU ms | 111.67 | 106.70 | -4.45% | -3.38%, -5.51% |
| Whole process: million cycles | 673.35 | 654.35 | -2.82% | -2.48%, -3.17% |
| Whole process: CPU ms | 182.93 | 176.75 | -3.38% | -2.96%, -3.80% |

Both reversed pairs reduce worker and whole-process cycles **and** CPU time
per completed frame. This establishes an observed repeatable CPU saving for
the isolated span option in these round runs. It does not establish aggregate
JOB0/JOB1 savings or an energy/power measurement. Questions/cameras still vary.
CPU-series FPS is contextual(+2.16%);the primary separate FPS result remains
+1.91%. Contextual p99 here is44.27/43.84/43.54/44.25ms in run order;this does
not close follow-up of the separate FPS run3's54.80ms tail.

Original filesystem UTC metadata for all eight FPS/CPU round runs corroborates
PNG completion194.76..194.97s after CSV creation,which occurs immediately before
the renderer epoch is initialized. Phase-file creation precedes CSV creation
by0.34..0.42s. This checks the suspected helper/renderer clock offset and finds
no overlapping conversion. It supplements the monotonic native capture/frame
binding;filesystem UTC is not an independent QPC measurement of conversion.
Raw attributes and hashes are preserved in`spans-round-v2-clock-alignment.json`.

Private CPU evidence:`spans-cpu-round-v2-report.json`,the complete progress,
raw snapshots/log/CSV/phase/readback set and`spans-cpu-round-v2/scene-qa.json`.
Next:repeat menu FPS and separate CPU reversals on this same3df frozen EXE,
then resolve round FPS tails before default acceptance. JOB0/geometry827/all
new flags0/SPANS0 remain unchanged. Phase3,cold real/live,round interpreted
counts and aggregate whole-job/default gates remain open.
