# Phase3 native whole-job performance

Status: **keep JOB0/geometry827/new flags0**. Phase3 remains open. The older
combined menu regression, isolated SPANS gains and newer REGISTERS measurements
below are separate experiments. REGISTERS has small/mixed gains and stays
opt-in; the still-unobserved cold formats retain their open gates.

## Shared formatter bank: measurements and default decision

The completed four-series matrix uses compiled source `a5fbf3d`, frozen
`build-vulkan-phase3-formatter-registers-v1`, EXE `bc0d35df` / MAP `dc3e3a05`,
unchanged generated C/SDK and initial save `92ed9caf`. All16 fresh runs use
JOB1 and REGISTERS0/1/1/0; all other production options, including SPANS, are1,
with audit0, geometry827 and StageF1. These measure the isolated shared-bank
option. The older aggregate JOB0/JOB1 regression remains separate and unresolved.

FPS and CPU use different four-run series. Menu windows are40..98s; round
windows are200..270s. CPU results use the same map-bound frame counter and
four named worker lanes plus the process. No compilation, CPU tests, other
game or diagnostic profiler ran concurrently. All runtime flags, actual native
coverage, disabled strict checks, raw counter windows and frozen bindings
pass the public reporter. All77 pins and305 sources are unchanged after the
matrix. Eight round readbacks were inspected after their apps closed: normal
green arena, host and Adam robot, with camera/question/score-panel variations.
Each preserves exact1280x720 PNG/PPM from one190s GPU readback, continuous
readback0, before the measurement window.

| Independent FPS series | Control -> shared bank | Mean change | Forward / reverse pair |
| --- | --- | ---: | --- |
| Menu | 25.616 ->25.845 | +0.894% | +0.994% /+0.795% |
| Round | 31.725 ->31.778 | +0.169% | +0.627% /-0.288% |

| Scene / frame time | REG0 / REG1 / REG1 / REG0, ms |
| --- | --- |
| Menu median | 38.973 /37.951 /38.424 /39.069 |
| Menu p90 | 51.450 /50.897 /50.977 /50.624 |
| Menu p99 | 59.795 /57.709 /58.057 /57.241 |
| Round median | 31.264 /31.111 /31.229 /31.1825 |
| Round p90 | 38.052 /37.234 /37.849 /37.549 |
| Round p99 | 43.401 /42.396 /43.470 /42.129 |

Menu FPS and median improve in both pairs. Menu p90/p99 and all round
comparisons are mixed; the second candidate p99 is0.816ms higher in menu and
1.341ms higher in round. No run is discarded or replaced.

| Separate CPU series / per completed frame | Control -> shared bank | Mean change | Forward / reverse pair |
| --- | --- | ---: | --- |
| Menu worker cycles | 497.111M ->495.414M | -0.341% | -0.543% /-0.141% |
| Menu worker CPU ms | 134.564 ->134.567 | +0.002% | -0.224% /+0.226% |
| Menu process cycles | 855.783M ->854.288M | -0.175% | -0.427% /+0.078% |
| Menu process CPU ms | 232.997 ->231.938 | -0.454% | -0.926% /+0.019% |
| Round worker cycles | 394.475M ->394.437M | -0.010% | +0.028% /-0.048% |
| Round worker CPU ms | 107.504 ->106.270 | -1.148% | -1.606% /-0.683% |
| Round process cycles | 653.867M ->653.565M | -0.046% | -0.147% /+0.055% |
| Round process CPU ms | 177.476 ->176.950 | -0.296% | -0.651% /+0.063% |

Round worker CPU time improves in both pairs, while worker cycles are nearly
flat. Menu worker time and process cycles/time in both scenes are mixed.
The reason for the difference between cycles and time was not established.
CPU-series FPS is contextual only: menu -0.097%, round -0.049%; it does not
replace either independent FPS series.

**Decision: retain REGISTERS0.** The implementation is a correct first host-bank
boundary and remains available for experiments. The measurements do not show
a broad repeatable saving in cycles/process work or round FPS. Remaining
pipeline streams, interpreter/cold coverage and enclosing-driver gates stay
open. CPU savings can justify a native path with flat FPS; these results retain
the small worker-cycle/menu and worker-time/round observations with their
limits. No aggregate native-job speedup or phase3 completion is claimed.

## Earlier combined-job baseline: method and binding

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


## Isolated formatter span proof: same-EXE menu follow-up

The capture-enabled3df8d057 EXE/MAP9b027e64 now also completes a fresh four-run
menu FPS reversal and a separate four-run menu CPU reversal. Both keep actual
JOB1/all other production options1,audit0,geometry827/StageF1 and the same
initial private save;only SPANS changes0/1/1/0. Each fresh100s run measures
40..98 renderer seconds. No GPU captures,interpreter observer,concurrent
build,CPU tests,diagnostic profiler or other game are used. All runtime,
window/hash/native-coverage and CPU raw-counter/boundary/lane checks pass;
no DIFF,first-fallback or unrecordable marker appears.

Menu FPS: **25.00 ->25.64 (+2.56%)**,pairs **+1.07%,+4.07%**.
Both pairs also improve median,p90 and p99.

| Order | SPANS | FPS | Median interval(ms) | p90(ms) | p99(ms) |
| --- | --- | --- | --- | --- | --- |
| 1 | 0 | 25.30 | 39.42 | 52.52 | 60.53 |
| 2 | 1 | 25.57 | 39.12 | 51.34 | 58.02 |
| 3 | 1 | 25.70 | 38.88 | 50.89 | 57.77 |
| 4 | 0 | 24.69 | 40.28 | 52.92 | 60.60 |

| CPU cost per completed frame | Span0 | Span1 | Change | Reversed pair changes |
| --- | --- | --- | --- | --- |
| SPURS workers: million cycles | 510.18 | 494.92 | -2.99% | -2.94%, -3.05% |
| SPURS workers: CPU ms | 136.93 | 135.34 | -1.16% | -1.46%, -0.86% |
| Whole process: million cycles | 870.89 | 854.83 | -1.84% | -1.91%, -1.78% |
| Whole process: CPU ms | 235.44 | 232.65 | -1.19% | -1.14%, -1.23% |

Both CPU pairs reduce worker/process cycles **and** CPU time per completed
frame. CPU-series FPS is contextual(+1.99%);the primary FPS result is+2.56%.
These values belong to the new3df EXE. The earlier B95 menu series remains
valid separate evidence rather than being substituted for this same-EXE check.

The same frozen EXE now has isolated menu and round mean-FPS gains and lower
normalized worker/process CPU costs,with separate observer-free FPS series.
That completes collection of the menu/round measurement matrix for this option;
it does not close the round FPS tail follow-up or whole-job/default gates.
The first round FPS series still contains one native p99=54.80ms versus44.06ms
in its paired control. Keep SPANS0 until an independent round FPS repetition
clarifies that result. Do not attribute a cause or discard it because the
menu or CPU-series tails improve. Questions/cameras vary in round runs.

Private evidence:`spans-fps-menu-v2-report.json`,`spans-cpu-menu-v2-report.json`
and their complete progress/raw snapshot/log/CSV/phase sets. All16 menu/round
runs use the same3df EXE/MAP/initial-save and production option map,apart from
the intended single knob and scene/metric settings. The earlier V3 aggregate
JOB0/JOB1 regression remains separately bound;no aggregate gain is claimed.
JOB0/geometry827/new flags0/SPANS0 remain unchanged. Phase3,cold real/live,
round interpreted-count and final default gates remain open.


## Independent round FPS repetition and next native work

A second independent SPANS0/1/1/0 round FPS series uses the same3df8d057 EXE,
MAP9b027e64,initial save and actual JOB1/all other production options1.
Four inspected native green result-arena images are normal;one shows the
normal Adam0 panel with a different camera. One190s GPU copy/readback0 and
lossless PPM/PNG/captured-frame/QA bindings pass in every run. All200..270s
windows are complete,with no CPU/interpreter observer,concurrent build,test,
other game or diagnostic profiler. There are no DIFF,first-fallback or
unrecordable markers.

Mean FPS: **31.04 ->31.73 (+2.22%)**,pairs **+2.61%,+1.84%**.
Median,p90 and p99 improve in both new native pairs.

| Order | SPANS | FPS | Median interval(ms) | p90(ms) | p99(ms) |
| --- | --- | --- | --- | --- | --- |
| 1 | 0 | 30.99 | 31.66 | 39.04 | 44.46 |
| 2 | 1 | 31.80 | 31.10 | 37.40 | 42.37 |
| 3 | 1 | 31.66 | 31.16 | 37.67 | 42.94 |
| 4 | 0 | 31.09 | 31.66 | 38.71 | 43.68 |

The first series'54.80ms native p99 is retained in its original table and raw
report. It was not reproduced in this repetition:42.37ms versus44.46ms and
42.94ms versus43.68ms in the new pairs. Across both primary round FPS series,
all four mean-FPS pairs improve,all median/p90 pairs improve,and three of four
p99 pairs improve. This completes the targeted repetition;the original outlier
still limits prediction and its cause is not established. Questions/cameras
vary,so do not claim identical-frame determinism or a universal tail bound.

The span proof is a validated opt-in optimization. Keep its product default0
while the enclosing whole-job driver remains experimental/default0;use SPANS1
explicitly in the optimized native profile from here. This policy defers
promotion with its parent rather than claiming the whole job benefits from
an individual-option saving. No default,checker,ABI or native implementation
changed in these measurements.

Separate diagnostic-only menu profiles now bind the3df EXE/MAP,immutable
sampler/source and four named pool lanes. The matched automatic-start series
has21,132native samples and20,601reference samples. Native leaf shares include
half-output pack2.03%,unit values1.95%,index triangles1.91%,float-output step
1.86%,CMP pack1.70%,unit final ABI1.25% and result counters1.19%. Job worker
is22.91%,VCRUNTIME5.97%,and the column proof0.75%. These are sampled shares,
not elapsed costs;external module samples do not identify a memcpy source.
The sampler briefly suspends threads,and these runs are excluded from every
FPS/CPU comparison. Nearest-map padding/folded aliases and unverified external
caller guesses are not exact call attribution.

A first manual control-profile attachment was too late and produced no valid
sample;its logs are retained. The private helper now starts the source-bound
sampler automatically at launch and verifies completion. An earlier valid
native-only profile is supplementary;the paired comparison uses only the
matched automatic-start series.

Next is shared host-register data execution in the formatter,as specified in
[the phase3 remaining-work audit](native-geometry-phase3-remaining.md). Source
inspection confirms full bank copies around encoder boundaries. Removing those
copies is a proposed implementation,with no saving claimed before measurement.
The full host-buffer pipeline,round interpreted counts,cold real/live and
aggregate/default gates remain open;phase3 is not complete.
