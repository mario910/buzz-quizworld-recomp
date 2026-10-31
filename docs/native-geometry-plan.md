# Native geometry jobs: plan and progress

Goal: replace the lifted SPU code of the studio's geometry jobs (image 41,
then image 40) with native C++ that is **bit-exact** against the lift, so
the SPU job chain stops bounding the frame on this PC and the port gets
within reach of weaker CPUs (Steam Deck, laptops, Switch 2-class ARM).

This is a living document. Update the tracker tables after every session.

## Continuing objective (2026-10-31)

The user has authorized continuing through the remaining phases until the
native geometry functions are finished, documenting and committing each
validated step. This supersedes earlier instructions to stop at phase 3 or
wait for another request before proceeding to the following phase.

Continue from completed phase 2 (`ef906be`, default mask 827 / `0x33B`):

1. Complete phase 3: the native image-41 whole-job flow, using the validated
   kernels and preserving the lifted fallback for unsupported descriptors.
2. Complete phase 4: the remaining image-40 native routines and reuse the
   shared routines in images 43/45/47 after auditing each image's mapping,
   contracts and real coverage. Keep an explicit inventory of remaining
   required functions and paths; do not call the native work finished while
   required implementations or validation gates remain open.
3. Assess optional phases 5/6 after the mandatory native paths. They retain
   their optional status: choose render/GPU work from measured bottlenecks
   and portability work from the target platform and available validation.
   Document any selection or deferral and its reason. An untested ARM path
   must not be reported as validated or enabled by default.
4. Complete phase 7 last: reconstruct Stage F's full strict state, pass
   actual-native whole-job replay and all usual gates, then remove the
   temporary baseline exception. If either optional phase is selected, do
   it before this final Stage F cleanup.

For every function or tightly coupled group, retain its runtime switch and
strict correctness gates, record FPS/frame times and worker/process CPU
cycles/time per completed job/frame, update the tracker and commit the
reviewable result. Default-on decisions may use repeatable CPU savings with
flat FPS, as specified below. Never weaken a checker or count fallback as
native coverage. Preserve original saves, private corpus/derived artifacts,
source pins and frozen executable/map/save bindings; keep compilation,
CPU tests and profiling separate from performance runs.

At each phase boundary, finalize a separate results document, commit it and
continue to the next unfinished phase without requiring renewed permission.
The active objective includes phase 3 and the subsequent native-function
phases; individual phase boundaries are milestones, not a request to stop.
Keep the continuing objective active while required implementations or gates
remain open. The overall objective is finished only after the mandatory
native-function inventory and final strict Stage F gates are complete, with
optional-phase decisions recorded. Do not mark unfinished work complete just
to advance to another phase.

## Why these jobs

Profiles from 2026-10-30 (EU 1.00, Vulkan, Ryzen 9 7900, studio menu; see
[frame-limit-status](frame-limit-status.md)):

| Work | Cost per frame | Bound? |
|---|---|---|
| Image 41 jobs (`buzz_job_6eba`, Edge geometry), ~1900 jobs | ~20 ms on each of 4 pool lanes | **Yes**: SPU chain is the critical path |
| Image 40 jobs (`buzz_job_abe1`), ~360 jobs | ~13 ms total lane time | Same chain |
| Image 39 (`buzz_job_c0bc`), other chain | ~9.5 ms total | Partly native already |
| RSX/render thread | ~19 ms | Not bound at 36 FPS; next limit after SPU |

Already native: stage F (image 41 0x63E0 loop, 0xC518 transform, 0x2C00
normalize; `port/native_stagef*.{cpp,h}`, `BUZZ_STAGEF`), image 40 loop
0x6DB8 (`generate-image40-native.py`, `BUZZ_IMAGE40_LOCALS`), image 36/39
kernels. These cover roughly 7 % of image 41 lane time.

Expected payoff (estimate, not measured): native image 41 + 40 at 2-4x the
lift's speed takes the uncapped menu from ~36 to ~45-50 FPS on this PC
(then the render thread limits), lifts the round's heavy phase (~28 FPS
uncapped) above 30, and is the prerequisite for any low-power target.

## Ground rules

1. **Bit-exact or it does not ship.** Every native function must reproduce
   the lift's registers, local store, DMA/atomic stream and exit state for
   every corpus job. Floating point follows SPU semantics (round toward
   zero, flush denormals, no FMA contraction unless the SPU op fuses);
   mirror the stage F build flags (`-mavx2 -mfma -frounding-math
   -ffp-contract=off`, `SPU_NATIVE_RTZ_*`).
   Existing stage F is an explicitly accepted baseline exception (2026-10-30):
   reproduce its established volatile-register/cull contract while reporting
   strict differences separately. New kernels must pass strict full-state
   comparisons. This exception is temporary: the final phase 7 removes it
   after Stage F passes the same strict gates. See [data map and harness](native-geometry-data.md).
2. **One function per step.** Replace a single lifted function (or a
   tightly coupled group), prove it, measure it, commit it. Never a big-bang
   rewrite.
3. **Runtime switch per kernel**, like `BUZZ_STAGEF_KERNELS`: a bit per
   native function, default on only after it passes the gates below; 0
   restores the lift for A/B and bisecting.
4. **Pinned inputs.** Generators refuse unreviewed lifts (hash pins, as in
   `generate-geometry-stagef.py`); when the lift changes, re-audit and add
   the hash, never silently re-infer the ABI.
5. **Same-EXE A/B** for performance claims (`perf/measure-vulkan-scene.ps1`
   with `-Env`, reversed order, window stats), numbers recorded here.
6. **CPU savings count even when FPS is unchanged.** A kernel may become
   default-on after all correctness/render gates if matched reversed runs
   show a repeatable reduction in geometry-worker CPU time or cycles per
   completed job/frame, without a material FPS/frame-time regression. Track
   both worker cost and whole-process cost, with equivalent scene/workload
   and the same executable/settings. More FPS can mean more work per second,
   so raw CPU percentage or cycles/second alone is not a fair comparison.
   Microbenchmark elapsed time is supporting evidence, not a measured cycle
   reduction; diagnostic RIP sampling that suspends threads is not a benchmark.

## Tooling (exists)

| Tool | Use |
|---|---|
| `port/job_corpus.cpp` | Capture: `BUZZ_JOB_CORPUS=<dir> BUZZ_JOB_CORPUS_IMAGE=41 BUZZ_JOB_CORPUS_MAX=N BUZZ_JOB_CORPUS_PERIOD_MS=...`; replay offline: `BUZZ_REPLAY_CORPUS=<dir>` compares every transfer, atomic and the exit state |
| `perf/measure-vulkan-scene.ps1`, `perf/scene-window-stats.py` | Menu/round A/B (background, muted) |
| `perf/frame-pacing-stats.py` | Capped frame-time spread |
| RIP sampler (`tests/claude-rip-sampler.c`, `perf/claude-rip-report.py`) | Which lifted functions are hot |
| `SPU_INTERP_STATS`, `SPU_INTERP_ENTRIES` | Interpreted SPU code check (should stay ~0 on lanes) |
| `SPURS_JOB_PHASEPROF=10` | Per-image setup/run/post time |
| `tests/native-geometry-test.c`, `tests/scripts/test-image40.ps1` | Templates for standalone arithmetic tests |

## Tooling (to build in phase 0)

- **Corpus set**: at least 3000 image 41 jobs and 1000 image 40 jobs,
  spread over menu, solo round, multi-player round and scene loads
  (`PERIOD_MS` sampling). Kept under `analysis/` (private, never in git).
- **Per-function harness**: run one lifted function and its native
  replacement from the same captured entry state (registers + LS) and
  compare full state; reports first differing register/LS byte. Generalize
  the stage F checker.
- **Per-function profile**: lane time per lifted function from the RIP
  sampler, aggregated, so each step has a target number.
- **Call graph** of `buzz_job_6eba` from the lifted source (who calls whom,
  entry/exit registers, LS regions touched), to pick self-contained leaves
  first.

## Phases

### Phase 0: groundwork (1-2 sessions)
Deliverables: corpus set, per-function harness, hot-function ranking and
call graph for image 41, this tracker filled in. Exit: harness passes the
existing stage F kernels against the corpus (proves the harness).

### Phase 1: understand image 41's data (2-4 sessions)
Document in `docs/` (new section here): job descriptor/user data layout,
input DMA lists, Edge vertex stream formats (compressed positions,
normals, UVs, skinning weights/indices), output ring and RSX command
layout, scratch usage. Exit: annotated map of every LS region and every
DMA the job issues, checked against corpus traces.

### Phase 2: hot leaves of image 41 (4-8 sessions)
Replace the hottest self-contained functions first, in ranking order (from
the phase 0 menu profile: `func_000092A0`, `B940`, `4AB0`, `7A90`, `51E0`,
`CB78`, `78A8`, `9148`; see the tracker for the remaining sampled blocks).
`B940` and `4AB0` are effectively tied in the menu; `B940` is hotter in the
solo and multiplayer samples. Prove entry/exit boundaries before treating
these internal lifted blocks as independently replaceable functions.
One per step, each behind its own bit.
Exit per function: corpus bit-exact, in-game checked mode (compare native
vs lift live for N calls, as stage F's mode 2) clean, menu A/B recorded.

### Phase 3: whole-job native path for image 41 (3-6 sessions)
Once the leaves are native, replace the job's driver (decompress -> skin
-> transform -> cull -> emit) with one native routine that keeps data in
host registers/arrays between stages instead of LS round trips; the lifted
job stays as fallback for any descriptor it does not recognise. Exit:
whole-job corpus bit-exact, interpreted count unchanged, menu/round A/B.

Implementation and current evidence are tracked in
[native-geometry-phase3-results.md](native-geometry-phase3-results.md).
The driver boundary audit and reference/shadow I/O harness are validated on
the full corpus and at least 256 live jobs. The complete native pipeline and
its correctness/render/performance gates
remain open; the harness baseline is not a native implementation.
The native host input reader now matches the original 21-pointer setup on
all 19,508 corpus entries and 256 synthetic entries, with 14 mutation-free
rejection checks. It now feeds the experimental native setup flow.
The native C++ preamble and context initializer also match the entire LS on
all 19,508 entries. Synthetic checks cover 512 preamble/call combinations and
1,024 initializer cases. `BUZZ_GEOMETRY_JOB=0/1/2` (default 0) now selects
retained/native/checked execution. Native C++ owns the complete outer driver,
including stage decisions, palette counts, output allocation/publication,
commands and caller restoration; no original main-driver continuation is
dispatched. The Edge library still uses SPU-ABI wrappers and the existing
per-kernel switches. Vertex-data integration between those stages remains
open. All three driver modes pass 19,508 jobs / 58,195 transfers, with zero
native fallback or failures. A separate pinned model test passes 1,024
synthetic paths plus all real entries, including exact call arguments and
nonvolatile spill vectors. The frozen outer driver also passes at least
445,000 strict whole-job + I/O comparisons in a complete 65 s menu run.
Changed reference reservations are rejected before trial, without relaxing
the shadow checker. Final data-path, live/render/interpreter/performance
gates remain required; these results do not complete phase 3.

The first native publication stage (`0x2DC0`, index PUTs) now has a separate
`BUZZ_GEOMETRY_JOB_PUBLISH=0/1` switch, default 0. All 19,508 jobs / 58,195
transfers pass with the switch enabled in actual and checked modes, including
7,201 native publications and zero publication fallback. An independent
original-function oracle also passes 2,048 strict state/channel/payload cases
and nine mutation-free rejections. The phase-3 report now inventories every
remaining outer-driver library boundary explicitly; they are not silently
counted as native because an internal leaf is native.
On the frozen publisher build, live checking passes at least 445,000 strict
whole jobs including at least 370,000 native publications. Actual-native menu
and round-introduction renderer readbacks look normal. The new switch remains
off pending matched FPS/CPU measurements; these functional runs do not close
the phase's remaining data-path or interpreter/performance gates.

Output sizing (`0x2B80`) and vertex publication (`0x35F0`) now have native
opt-in paths, using a shared host layout retained across allocation, index
publication and vertex encoding. Independent `BUZZ_GEOMETRY_JOB_LAYOUT` and
`BUZZ_GEOMETRY_JOB_VERTEX` switches default to 0. Five corpus arms (original,
each new stage separately checked, combined actual and combined checked)
pass all 19,508 jobs / 58,195 transfers with no input divergence/failures or
new-stage fallback. Combined runs execute each stage 7,201 times and reuse
the host layout for all 7,201 vertex publications. Another 4,096 strict
synthetic size/publication cases and 20 mutation-free rejections pass,
including full-vector cache invalidation. Remaining library stages and final
interpreter/performance/default gates stay open. On the frozen output build,
a 65 s checked menu run passes at least 450,000 whole-job comparisons, with
at least 375,000 native executions of each new stage and layout reuses.
Actual-native menu and round-introduction renderer readbacks look normal.
These gates must be repeated on the completed pipeline; this increment does
not finish phase 3.

Command-buffer preparation (`0x32C8`) is now native behind independent
`BUZZ_GEOMETRY_JOB_COMMANDS=0/1`, default 0. It owns reservation, record scans,
the fenced synchronization GET and command preamble. Native-owner routing
also covers nested library callers while keeping the comparison reference
original. Four corpus arms pass 19,508 exact jobs / 58,195 transfers, including
19,508 native preparations with zero fallback in each enabled native arm.
Independent tests pass 4,096 strict state/channel/GET cases, 16 unchanged-state
rejections and reference/owner routing checks. At this checkpoint, final
command encoding, relocation and publication remained in the library;
phase 3 stays in progress.
The frozen commands build also passes at least 450,000 strict live whole-job
comparisons and native preparations. Actual-native menu and round-introduction
renderer readbacks look normal. Interpreter-count and reversed FPS/CPU gates
still need completion before the phase/default decision.

Final command relocation/publication (`0x27A0`) now has independent
`BUZZ_GEOMETRY_JOB_COMMAND_PUBLISH=0/1`, default 0. Host packet traversal,
semaphore construction, bulk tail shifts and NOP fill preserve strict full
state and fenced PUT effects. The read-only preflight keeps unsupported
entries on their original stage before mutation or I/O. Independent tests
pass 8,256 full-state/channel cases, including 64 wrapped look-ahead loads,
and 20 no-mutation/no-I/O rejections. Command encoding (`0x12F0`/`0x02E8`)
and the remaining data stages still require implementation; phase 3 remains
open. Four frozen corpus arms pass 19,508 exact jobs / 58,195 transfers,
with 19,508 native finalizations and zero fallback in each enabled arm.
The same frozen build passes at least 450,000 strict live whole-job/I/O
comparisons and native command finalizations, plus normal actual-native
menu/round-introduction renderer readbacks. See the separate phase-3 report
for bindings and remaining performance/interpreter gates.

Position-only command emission (`0x02E8`) and shared attribute binding/reset
writers (`0x0100`/`0x0020`) now have native implementations behind independent
`BUZZ_GEOMETRY_JOB_EMIT=0/1`, default 0. The host emitter plans the actual packet
space before preparation/GET, handles raster state, leading alignment, full
and final indexed batches, then resets attributes and publishes via the
independently switched entries. The same writers also serve the retained
full command emitter. Tests pass 8,192 strict writer cases plus 8,320 strict
position-emitter/channel cases and 54 unchanged-state rejections. Unsupported
entries retain the original stage before effects; no new checker exception
or default is introduced. Four frozen corpus arms pass all 19,508 jobs /
58,195 transfers, with 2,123 native position emissions and 7,201 bindings/
resets each, zero fallback. Live checking passes at least 450,000 strict whole
jobs, including 120,000 position emissions and 375,000 bindings/resets each;
actual-native menu/round-introduction frames look normal. `0x12F0` and the
remaining geometry data stages remain work for phase 3; final interpreter/
FPS/CPU gates remain open.

Full multipass command emission (`0x12F0`) now has a native host pass/state/
packet implementation behind independent `BUZZ_GEOMETRY_JOB_FULL_COMMANDS`,
default 0. It reuses the independently switched writers, preparation and
publication stages. The preflight validates the full repeated stream before
any GET, preserving marker skips and the original flags-vector selection.
Tests pass 8,512 strict state/channel cases and 32 unchanged-state/no-I/O
rejections, including independent parsing of the final packet size. No new
checker exception or default is introduced. Four frozen corpus arms pass all
19,508 jobs / 58,195 transfers, including 5,078 native full emissions and zero
fallback. With combined switches, both emitters and all command stages are
native across the captured jobs, without a difference. Live checking passes
at least 445,000 strict whole jobs, including 250,000 full-emitter calls;
actual-native menu/round-introduction frames look normal. Remaining geometry
data stages and final interpreter/FPS/CPU gates still keep phase 3 open.

Attribute orchestration (`0xAC68`) and the complete built-in raw float3 stream
(`0x93F0`, format 0) now have native implementations behind independent
`BUZZ_GEOMETRY_JOB_ATTRIBUTES`, default 0. The host SIMD data loop includes
small groups, final padding/lookahead, table allocation and the full return
ABI. Tests pass 720 strict raw-stream and 704 strict control cases, 20
unchanged-state rejects, owner routing, the real-entry model and controller
checks. Four frozen corpus arms pass all 19,508 jobs / 58,195 transfers.
Enabled arms execute 19,508 native attribute controls and complete raw streams
each. A separate counter reports 11,414 retained stream calls; descriptor
decoding (`0x7DF8`), other built-ins and the stride getter remain explicit
original bodies. They remain in the required inventory. Live checking passes
at least 450,000 strict whole jobs with native attribute/raw coverage; actual
native menu/round-introduction renderer readbacks look normal. Evidence is
recorded in the phase-3 results document; no CPU/FPS benefit or new default is
claimed. Remaining data stages and final validation keep phase 3 open.

The complete U8 normalized-color descriptor handler (`0x89B0..0x8B90`) now
has a native host column/batch loop behind `BUZZ_GEOMETRY_JOB_CODECS`, default
0. It preserves the internal `0x83C8` continuation, full state, biased logical
cursors, physical aligned stores, scratch aliases and speculative lookahead.
Standalone validation passes 4,544 strict cases and 18 unchanged-state rejects.
Four frozen corpus arms pass all 19,508 jobs / 58,195 transfers, with 109 native
color calls and zero fallback. JOB=2 also matches the full region exit on all
109 real calls, before the common descriptor continuation overwrites state.
Live checking passes at least 5,000 strict color-region and 975,000 whole-job/
I/O comparisons; actual-native menu/round-introduction readbacks look normal.
This is one complete typed data handler inside `0x7DF8`; the enclosing
descriptor control, other codecs and remaining pipeline stages stay open.
The phase-3 results document records corpus/live/render evidence; reversed
FPS/CPU, interpreter-count and default-selection gates remain required.

Descriptor iteration, table routing, position padding and blend orchestration
(`0x7DF8`) now have native control behind `BUZZ_GEOMETRY_JOB_DESCRIPTORS`,
default 0. The host attribute/padding loops preserve the full frame/return ABI
and call explicitly tracked typed handlers and blend data kernel. Tests pass
4,192 strict cases across all nine storage families and 28 unchanged-state
rejects. JOB=2 checks the complete function exit in addition to whole-job/I/O
state. The remaining typed handlers and `0xC750` data kernel remain required;
all four frozen corpus arms pass 19,508 jobs / 58,195 transfers, with 12,309
native descriptor-control calls and zero fallback. JOB=2 checks all 12,309
function exits strictly. Live checking passes at least 450,000 descriptor-
function and 665,000 whole-job/I/O comparisons; actual-native menu/round
readbacks look normal. Runtime dispatch also observes three fixed-point
additional-stream calls, refining the earlier primary-position inventory.
Native control does not close their inventory or phase 3. Corpus/live/render
evidence and the runtime dispatch counts are recorded in the phase-3 results
and data-map documents. New defaults and FPS/CPU claims remain gated.

The complete storage-9 unit-vector handler (`0x9114/9148/92A0`) now has
native data decoding behind the independent
`BUZZ_GEOMETRY_JOB_UNIT_VECTORS=0/1` switch (default 0). Host AVX2 batches and
the last two native pipeline iterations preserve every register, speculative
read and sentinel-store byte, without the old lifted final-column callback.
Tests pass 8,256 strict cases and 23 unchanged-state rejects; the all-family
descriptor matrix additionally exercises original/native unit callees.
All four frozen corpus arms pass 19,508 jobs / 58,195 transfers; the native
arms cover all 12,067 unit regions with zero fallback and JOB=2 checks every
region plus the whole job/I/O stream. This completes storage-9 data beyond
the earlier phase-2 leaf. The existing leaf default mask stays 827/0x33B;
remaining typed handlers, weighted scatter and other pipeline bodies remain
open. Live checking passes at least 350,000 strict unit-region and 525,000
whole-job/I/O comparisons; actual-native menu/round images look normal.
Evidence is in the phase-3 results document; final performance/default
decisions remain gated.

### Phase 4: image 40 (2-4 sessions)
Same library routines at shifted offsets (images 40/43/45/47 share them):
reuse phase 2 kernels with per-image entry mapping. Exit as phase 2/3.

### Phase 5: render thread (optional, 3-6 sessions)
After SPU is no longer the limit: GPU-side vertex decode (raw guest
buffers + per-format shader fetch), cheaper FIFO method dispatch. Exit:
frame dumps match, render drain reduced, A/B recorded.

### Phase 6: portability (optional)
Every native kernel gets a scalar reference + NEON path next to AVX2,
same corpus gate, for ARM targets.

### Phase 7: strict Stage F cleanup (final)
After the preceding selected phases, bring the existing Stage F vertex,
transform, normalize and cull kernels up to the strict contract used by new
native kernels. Reproduce all 128 GPRs, including volatile/dead temporaries,
the entire LS including scratch/padding, control state and return/trampoline
continuation. Fix the implementations or reconstruct their exact tail state;
do not exclude differing fields or weaken the checker.

Require zero strict per-function differences on the full corpus and at least
256 live comparisons per kernel. Whole-job replay must also pass with actual
native Stage F execution (`BUZZ_STAGEF=1`), without relying on checked mode
retaining the lifted state. Verify the complete DMA/atomic stream and exit
state, normal native menu/round frame dumps, and the usual reversed menu/round
A/B pairs. Preserve reference fallback, source pins and per-kernel switches;
record any performance cost of stricter state reconstruction.

Remove the accepted Stage F exception only after all these gates pass, update
the contracts/trackers and commit the correction. Until then, earlier phases
retain the explicitly accepted baseline contract. This remains the final
roadmap phase under the continuing objective above; perform it after the
other selected native-function phases, rather than folding it into phase 3.

## Gates for every step

- [ ] Corpus replay bit-exact (all captured jobs of the image)
- [ ] Per-function harness: 0 differences
- [ ] Live checked mode: N >= 256 calls compared in-game, PASS logged
- [ ] Frame dump of menu and round renders normally
- [ ] Menu A/B (2 pairs, reversed) and round A/B recorded below
- [ ] For FPS-neutral default-on decisions: matched worker/process CPU-cost evidence recorded, with no material frame-time regression
- [ ] Runtime bit documented; default flipped only after all of the above
- [ ] SDK/main commits, docs updated

## Tracker

Status: `todo`, `wip`, `exact` (bit-exact, off by default), `on` (default
on), `dropped` (with reason).

Phase 2 is complete (2026-10-31): all 13 ranked blocks / eleven groups passed
their correctness, live, renderer and reversed scene gates. Seven groups are
default-on after the FPS/CPU decisions; four are exact and opt-in. Final
default replay, live comparison and native menu/round rendering also pass.
The finalized separate results document is
[native-geometry-phase2-results.md](native-geometry-phase2-results.md).
Phase-2 results were committed in `ef906be`. Phase 3 is in progress in the
tracker below and remains incomplete. Stage F strict cleanup remains the
final phase 7.

### Phase 0

| Item | Status | Notes |
|---|---|---|
| Corpus image 41 (>= 3000 jobs, 4 scenes) | exact | 19,508 jobs: full replay exact, 0 input divergences; menu / solo / verified two-player / loads represented |
| Corpus image 40 (>= 1000 jobs) | exact | 17,714 jobs: full replay exact, 0 input divergences |
| Per-function harness | exact | Generic paired runner for synchronous DMA-free leaves, strict state diagnostics, fallback not counted; synthetic checks pass |
| Hot-function ranking (image 41) | exact | 3 x 10 s menu with native stage F, plus 15 s solo and two-player reference samples; build-bound maps and lane denominators saved privately |
| Call graph + LS region map | exact | Structural map: 534 blocks, 41 direct-call roots, 491 observed transitions (24 indirect); per-block LS access ranges and all 58,195 observed DMAs checked. Semantic ABI/scratch/format work belongs to phase 1 |
| Harness validated on stage F kernels | exact | 19,508 image 41 jobs: vertex 19,508, transform 27,561, normalize 9,877, cull 19,508; 0 established-contract failures. Strict mode correctly rejects dead-register differences |

### Phase 1

Detailed evidence, reproduction commands and coverage limits live in
[native-geometry-data.md](native-geometry-data.md). Phases 0 and 1 are complete
for the audited EU 1.00 lift. Phase 1's deliverable is an annotated map, not
a native implementation: static-only paths are clearly marked and remain
subject to the kernel gates before replacement.

| Item | Status | Evidence / remaining work |
|---|---|---|
| Descriptor, context and staged input lists | exact | Corpus inspector corrected for 21-entry DMA list and descriptor-size bitfield |
| Runtime DMA issue map | exact | All 18 lexical issue blocks annotated (one unreachable C tail); 58,195 ordered transfers / 19,508 jobs matched; 10 blocks exercised |
| LS regions and scratch | exact | Descriptor boundaries, 21 staged slots, globals, dynamic float4 tables, context fields, input reuse, stack and block-attributed read/write spans documented |
| Output ring and RSX commands | exact | 7,182 linear + 19 wrapped reservations checked; 19,508 command blocks and 16,276 active format bindings checked; unexercised shared fallback statically mapped |
| Compressed positions, normals, UV, skinning formats | exact | Raw position input, 24-bit unit vectors, half UVs, four interleaved weight/index pairs, output codecs and compressed-index sections mapped; unused fixed-point path documented as static-only |

### Image 41 functions

| Function | Menu lane sample share | Status | Bit | Corpus | Live check | Menu FPS before -> after | Commit |
|---|---|---|---|---|---|---|---|
| 0x63E0 loop (stage F) | native helper, not separately attributed | on | `BUZZ_STAGEF_KERNELS` 0 | yes | yes | (existing) | |
| 0xC518 transform (stage F) | native helper, not separately attributed | on | 1 | yes | yes | (existing) | |
| 0x2C00 normalize (stage F) | native helper, not separately attributed | on | 2 | yes | yes | (existing) | |
| cull (stage F) | native helper, not separately attributed | on | | yes | yes | (existing) | |
| 0x92A0 | 4.60 % | on | `BUZZ_GEOMETRY_KERNELS` 0 | 19,508 exact; 6,628 strict groups | >=600,000 exact | 31.60 -> 31.94 | a7ad7b2 |
| 0xB940 | 2.48 % | on; repeated CPU-cycle benefit | `BUZZ_GEOMETRY_KERNELS` 1 | 19,508 exact; 5,700 strict groups | >=100,000 exact | 31.98 -> 31.87; CPU benefit below | 2416a77 |
| 0x4AB0 | 2.44 % | exact; off, no repeatable CPU savings | `BUZZ_GEOMETRY_KERNELS` 2 | 19,508 exact; 18,776 strict groups, no fallback | >=500,000 exact | 31.73 -> 31.93 | 81dadfb |
| 0x7A90 | 1.89 % | on; repeated cycle/CPU-time savings and round gain | `BUZZ_GEOMETRY_KERNELS` 3 | 19,508 exact; 12,675 strict groups | >=256 exact | 32.82 -> 32.86 | 7d0f413 |
| 0x51E0 | 1.75 % | on; repeated cycle savings and round gain | `BUZZ_GEOMETRY_KERNELS` 4 | 19,508 exact; 2,665 strict groups | >=256 exact | 32.82 -> 32.74 | e18619a |
| 0xCB78 | 1.44 % | on | `BUZZ_GEOMETRY_KERNELS` 5 | 19,508 exact; 9,063 strict groups | >=200,000 exact | 31.73 -> 32.10 | eb29523 |
| 0x78A8 | 1.15 % | exact; off, menu decline and no CPU saving | `BUZZ_GEOMETRY_KERNELS` 6 | 19,508 exact; 12,385 strict groups | >=256 exact | 32.82 -> 32.47 | 4cddd99 |
| 0x9148 | 1.01 % | on | shares bit 0 with 0x92A0 | same coupled unit-vector decoder | same checked group | same measurement | a7ad7b2 |
| 0x97A8 | 0.78 % | on; repeated CPU/process benefit | `BUZZ_GEOMETRY_KERNELS` 8 | 19,508 exact; 18,968 strict groups | >=256 exact | 32.52 -> 32.62 | cc9c877 |
| 0x4750 | 0.76 % | on | `BUZZ_GEOMETRY_KERNELS` 9 | 19,508 exact; 4,975 strict groups | >=256 exact | 32.32 -> 32.73 | eee46de |
| 0x0988 | 0.75 % | exact; off, mixed FPS and no repeatable CPU savings | `BUZZ_GEOMETRY_KERNELS` 10 | 19,508 exact; 19,508 strict groups, no fallback | >=300,000 exact | 31.73 -> 31.94 | 635e2f2 |
| 0x4808 | 0.73 % | on; coupled with 4750 | shares bit 9 | same raw-float3 output group | same checked group | same measurement | eee46de |
| 0x28F0 | 0.67 % | exact; off, mixed process/FPS results | `BUZZ_GEOMETRY_KERNELS` 12 | 19,508 exact; 19,068 strict groups | >=256 exact | 32.32 -> 32.44 | f23c3c7 |
| Whole-job driver | n/a | wip; native outer control, SPU-ABI library/data-path integration and final gates open | `BUZZ_GEOMETRY_JOB=0/1/2`, default 0 | 19,508 exact / 58,195 transfers in all three modes; 1,024 synthetic outer paths | >=445,000 strict outer-flow + I/O matches | not measured for native driver | |
| 0x2DC0 index publication | n/a | native opt-in; final measurements open | `BUZZ_GEOMETRY_JOB_PUBLISH=0/1`, default 0 | 7,201 native publications, zero fallback in exact 19,508-job replay; 2,048 strict synthetic cases | >=370,000 executions inside strict whole-job checks; normal native menu/round intro dumps | not measured | |
| 0x2B80 output sizing | n/a | native opt-in; shared host layout; final gates open | `BUZZ_GEOMETRY_JOB_LAYOUT=0/1`, default 0 | 7,201 native sizes, zero fallback; independent/combined 19,508-job replay exact; 2,048 strict synthetic cases | >=375,000 inside strict whole-job checks; normal native menu/round intro dumps | not measured | |
| 0x35F0 vertex publication | n/a | native opt-in; final gates open | `BUZZ_GEOMETRY_JOB_VERTEX=0/1`, default 0 | 7,201 native publications, zero fallback; 7,201 host-layout reuses in combined runs; 2,048 strict synthetic cases | >=375,000 inside strict whole-job checks and layout reuses; normal native menu/round intro dumps | not measured | |
| 0x32C8 command-buffer preparation | n/a | native opt-in; final gates open | `BUZZ_GEOMETRY_JOB_COMMANDS=0/1`, default 0 | 19,508 native preparations, zero fallback; 58,195 exact transfers; 4,096 strict synthetic cases | >=450,000 inside strict whole-job checks; normal native menu/round intro dumps | not measured | |
| 0x27A0 command relocation/publication | n/a | native opt-in; final measurements open | `BUZZ_GEOMETRY_JOB_COMMAND_PUBLISH=0/1`, default 0 | 19,508 native publications, zero fallback; 58,195 exact transfers in four corpus arms; 8,256 strict cases and 20 unchanged-state rejects | >=450,000 inside strict whole-job checks; native frame checks in phase-3 report | not measured | |
| 0x0100/0x0020 command attribute binding/reset | n/a | native opt-in; final measurements open | `BUZZ_GEOMETRY_JOB_EMIT=0/1`, default 0 | 7,201 native each, zero fallback; four 19,508-job/58,195-transfer arms exact; 8,192 strict cases +28 unchanged-state rejects | >=375,000 each inside strict whole-job checks; render proof in phase-3 report | not measured | |
| 0x02E8 position-only command emission | n/a | native opt-in; full emitter body and shared writers; final gates open | shares `BUZZ_GEOMETRY_JOB_EMIT`, default 0 | 2,123 native, zero fallback; four full-corpus arms exact; 8,320 strict state/channel cases +26 unchanged-state rejects | >=120,000 inside strict whole-job checks; render proof in phase-3 report | not measured | |
| 0x12F0 full multipass command emission | n/a | native opt-in; remaining phase gates open | `BUZZ_GEOMETRY_JOB_FULL_COMMANDS=0/1`, default 0 | 5,078 native emissions, zero fallback; four full-corpus arms exact; 8,512 strict state/channel/packet-size cases +32 unchanged-state rejects | >=250,000 inside strict whole-job checks; normal native menu/round intro dumps | not measured | |

Shares are exclusive block RIP samples divided by 63,969 active sampled
intervals on four pool lanes (19,499 samples in image-41 block symbols),
from three consecutive 10 s menu windows, `BUZZ_STAGEF=1`. This replaces
the earlier one-window estimates. `0x92A0` varies 4.46-4.74 % across windows;
`B940` varies 2.25-2.62 %, `4AB0` 2.32-2.58 %. Native/shared helper time is
not attributed to individual lifted blocks; sampling is not an FPS benchmark
or an exact wall-time measurement. The 15 s native-stage-F solo sample has
`92A0` 5.00 %, `B940` 1.82 %, `7A90` 1.62 %, `CB78` 1.27 %, `4AB0` 0.75 %.
Private sources: `analysis/native-geometry/{menu-ranking,solo-verified-ranking,
multi-question-ranking}.json`; each sample has its matching linker map.

Phase 2 implementation notes and per-kernel evidence are in
[native-geometry-kernels.md](native-geometry-kernels.md). Bit 0 combines the
tightly coupled `0x9148/0x92A0` decoder; it is not two independent replacements.
It is enabled by default in the reviewed adapter after all its gates passed.
Bit 1 implements `0xB880/B934/B940` and is now on: both CPU reversal pairs
show less worker/process work, while menu frame times are effectively flat
and round FPS improves. Bit 5 is on after all four FPS pairs improved.
The selected default mask is 827 (0x33B): unit vectors, skinning, triangles,
packed output, halfword decoding and raw float3 input/output. Checked/native
default replay both pass all 19,508 jobs; a 45 s live run without a mask
override records >=256 strict matches per selected group and no differences.
Encoder, indices, pointer generation and word fill remain off for their
documented CPU/FPS results. Combined-default native menu/round frames look
normal and both runs completed successfully.
See the separate
[phase 2 results](native-geometry-phase2-results.md) for complete evidence.

### Image 40 and shared routines

| Function | Lane time share | Status | Notes |
|---|---|---|---|
| 0x6DB8 loop | | on | `BUZZ_IMAGE40_LOCALS` |
| (fill in from phase 0 ranking) | | | |

### Phase 7: strict Stage F cleanup

| Item | Status | Exit evidence |
|---|---|---|
| Audit/reconstruct volatile GPRs, scratch/padding and continuations | todo | Exact full-state contract per Stage F kernel; no excluded fields |
| Strict corpus and actual-native whole-job replay | todo | Zero differences/failures, including transfers and exit state |
| Live/render/performance gates | todo | >=256 strict calls per kernel, normal native menu/round dumps, reversed A/B pairs |
| Retire baseline exception | todo | All gates passed, switches/contracts updated, dedicated commit |

### Measurements log

| Date | Change | Scene | FPS before -> after | Median / p90 ms | Notes |
|---|---|---|---|---|---|
| 2026-10-30 | Baseline (all session fixes) | Menu 40-98 s | 36.3 | 29.6 / 34.8 | `vk-scene-final2-vk` |
| 2026-10-30 | Baseline | Round 150-250 s | 44.2 | 22.6 / 27.3 | `vk-scene-final2-rvk` |
| 2026-10-30 | Phase 0 capture / harness / profile | Menu, solo, two-player, loads | n/a (no optimization) | n/a | 37,222 real jobs replay exactly; native Stage F established contracts reproduced; native defaults unchanged |
| 2026-10-30 | Phase 1 formats / LS / DMA map | Entire image 41 corpus | n/a (no optimization) | n/a | 19,508 format inventories and 58,195 DMA transfers checked; includes 19 ring wraps; no new kernel/default |
| 2026-10-30 | Native 9148/92A0 decoder | Menu 40-98 s | 31.60 -> 31.94 (+1.08%) | mean run medians 34.10 -> 33.41 / mean p90 40.65 -> 40.32 | Same EXE, A/B then B/A; pair gains +1.38%, +0.78%; `p2-unit24-ab` |
| 2026-10-30 | Native 9148/92A0 decoder | Round 150-220 s | 39.31 -> 40.65 (+3.41%) | mean run medians 25.81 -> 25.14 / mean p90 29.48 -> 29.02 | Same EXE, A/B then B/A; pair gains +5.09%, +1.80%; different session from original baseline |
| 2026-10-31 | Native B880/B934/B940 skinning | Menu 40-98 s | 31.98 -> 31.87 (-0.36%) | mean run medians 33.46 -> 33.47 / mean p90 40.16 -> 39.93 | Same EXE, masks 1/3, A/B then B/A; pairs -0.44%, -0.28%; `p2-skin-ab2`; default off |
| 2026-10-31 | Native B880/B934/B940 skinning | Round 150-220 s | 39.50 -> 40.24 (+1.88%) | mean run medians 25.71 -> 25.19 / mean p90 29.30 -> 28.83 | Same EXE, masks 1/3; pairs +2.74%, +1.00%; normal native/reference round introduction dumps |
| 2026-10-31 | Native 4AB0 encoder | Menu 40-98 s | 31.73 -> 31.93 (+0.65%) | mean medians 33.72 -> 33.44 / p90 40.32 -> 39.77 | `p2-batch1-ab`, masks 1/5; pairs -0.45%, +1.75%; normal native dump; CPU decision recorded below |
| 2026-10-31 | Native 4AB0 encoder | Round 150-220 s | 39.69 -> 40.16 (+1.18%) | mean medians 25.61 -> 25.35 / p90 29.49 -> 28.95 | Same EXE; pairs +1.94%, +0.44%; normal native dump |
| 2026-10-31 | Native CAD8/CB78 decoder | Menu 40-98 s | 31.73 -> 32.10 (+1.18%) | mean medians 33.72 -> 33.18 / p90 40.32 -> 39.76 | Same batch, masks 1/33; pairs +0.07%, +2.31%; normal native dump |
| 2026-10-31 | Native CAD8/CB78 decoder | Round 150-220 s | 39.69 -> 40.75 (+2.66%) | mean medians 25.61 -> 25.01 / p90 29.49 -> 28.76 | Same EXE; pairs +4.36%, +1.02%; normal native dump; final default validation passed |
| 2026-10-31 | Native 0988 pointers | Menu 40-98 s | 31.73 -> 31.94 (+0.67%) | mean medians 33.72 -> 33.67 / p90 40.32 -> 39.84 | Same batch, masks 1/1025; pairs +0.25%, +1.09%; normal native dump |
| 2026-10-31 | Native 0988 pointers | Round 150-220 s | 39.69 -> 40.26 (+1.44%) | mean medians 25.61 -> 25.30 / p90 29.49 -> 29.12 | Same EXE; pairs +3.10%, -0.17%; normal native dump; CPU decision recorded below |
| 2026-10-31 | Skinning CPU cost | Menu 40-98 s | n/a; separate CPU series | n/a | `p2-cpu-menu2`; pool cycles/frame -0.95% (pairs -1.23%, -0.66%); process cycles -0.73% (pairs -0.91%, -0.54%); process CPU time -1.20%; supports default-on |
| 2026-10-31 | Encoder CPU cost | Menu 40-98 s | n/a | n/a | Same reversed series; pool cycles/frame +0.78%, process +1.15%; no repeatable savings; default-off |
| 2026-10-31 | Pointer CPU cost | Menu 40-98 s | n/a | n/a | Same reversed series; pool cycles/frame +0.67%, process +0.70%; no repeatable savings; default-off |
| 2026-10-31 | Native 97A8 raw float3 input | Menu 40-98 s | 32.52 -> 32.62 (+0.28%) | mean medians 32.85 -> 32.69 / p90 39.24 -> 39.13 | Fresh four-run menu correction replaces contaminated original set; pairs +0.30%, +0.26%; normal native images |
| 2026-10-31 | Native 97A8 raw float3 input | Round 150-220 s | 41.19 -> 41.29 (+0.23%) | mean medians 24.70 -> 24.56 / p90 28.49 -> 28.35 | Original valid batch2 rounds; pairs +0.09%, +0.37%; CPU/default decision recorded below |
| 2026-10-31 | Native 46FC/4750/4808 float3 output | Menu 40-98 s | 32.32 -> 32.73 (+1.25%) | mean medians 33.08 -> 32.62 / p90 39.24 -> 39.07 | Valid original batch2, masks 35/547; pairs +0.97%, +1.55%; normal native images |
| 2026-10-31 | Native 46FC/4750/4808 float3 output | Round 150-220 s | 41.19 -> 42.06 (+2.09%) | mean medians 24.70 -> 24.27 / p90 28.49 -> 28.12 | Pairs +2.85%, +1.33%; CPU/default decision recorded below |
| 2026-10-31 | Native 28F0 word fill | Menu 40-98 s | 32.32 -> 32.44 (+0.36%) | mean medians 33.08 -> 33.08 / p90 39.24 -> 39.24 | Valid original batch2, masks 35/4131; pairs -0.55%, +1.28%; normal native images |
| 2026-10-31 | Native 28F0 word fill | Round 150-220 s | 41.19 -> 40.91 (-0.69%) | mean medians 24.70 -> 24.69 / p90 28.49 -> 28.51 | Pairs -0.04%, -1.33%; CPU/default decision recorded below |
| 2026-10-31 | Native 7A90 triangle stitching | Menu 40-98 s | 32.82 -> 32.86 (+0.13%) | mean medians 32.69 -> 32.67 / p90 38.99 -> 39.01 | `p2-last3-ab`, masks 803/811; pairs +0.66%, -0.40%; normal native images; CPU decision recorded below |
| 2026-10-31 | Native 7A90 triangle stitching | Round 150-220 s | 40.50 -> 41.68 (+2.91%) | mean medians 24.87 -> 24.26 / p90 28.45 -> 27.99 | Same EXE; pairs +4.09%, +1.71% |
| 2026-10-31 | Native 5008/51E0 packed output | Menu 40-98 s | 32.82 -> 32.74 (-0.23%) | mean medians 32.69 -> 32.66 / p90 38.99 -> 39.08 | Same batch, masks 803/819; pairs -0.84%, +0.37%; normal native images; CPU decision recorded below |
| 2026-10-31 | Native 5008/51E0 packed output | Round 150-220 s | 40.50 -> 41.48 (+2.42%) | mean medians 24.87 -> 24.52 / p90 28.45 -> 28.21 | Same EXE; pairs +2.63%, +2.20% |
| 2026-10-31 | Native 78A8 index expansion | Menu 40-98 s | 32.82 -> 32.47 (-1.05%) | mean medians 32.69 -> 33.04 / p90 38.99 -> 39.35 | Same batch, masks 803/867; pairs -0.61%, -1.50%; normal native images; CPU decision recorded below |
| 2026-10-31 | Native 78A8 index expansion | Round 150-220 s | 40.50 -> 40.76 (+0.63%) | mean medians 24.87 -> 24.81 / p90 28.45 -> 28.28 | Same EXE; pairs -0.20%, +1.47% |
| 2026-10-31 | Triangle CPU cost | Menu 40-98 s | n/a; separate CPU series | n/a | `p2-cpu-last3-menu`; pool cycles/frame -0.64% (pairs -0.54%, -0.74%), process cycles -0.50%, pool/process CPU time -0.83%/-0.63%; all pairs lower; on |
| 2026-10-31 | Packed-output CPU cost | Menu 40-98 s | n/a | n/a | Same series; pool cycles/frame -0.25% (pairs -0.09%, -0.40%), process cycles -0.24% (pairs -0.36%, -0.12%); CPU-time pairs mixed; on for repeated cycle/round benefit |
| 2026-10-31 | Index CPU cost | Menu 40-98 s | n/a | n/a | Same series; pool/process cycles +0.11%/+0.17%, pool/process CPU time +0.12%/+0.34%; CPU time higher in both pairs; off |
| 2026-10-31 | Final phase 2 default 827 / 0x33B | Full corpus, live, menu and round | no aggregate FPS claim; isolated results above | native menu/round frames normal | Seven groups on, four opt-in; checked/native replay 19,508 exact each, 58,195 transfers, zero failures; >=256 live strict matches per selected group without mask override; phase 2 complete |
| 2026-10-31 | Phase 3 complete unit-vector decoder 9114/9148/92A0 | Four frozen corpus arms and same-EXE live/menu/round | functional evidence; no FPS/CPU claim | normal menu/round frames | 8,256 strict unit cases +23 rejects; 12,067 native regions /0 fallback; each replay19,508 exact /58,195 transfers; live>=350,000 strict unit +525,000 whole-job/I/O; own switch default0, leaf827 unchanged; phase3 remains open |

## Risks

- **Undocumented formats** (phase 1) are the hardest part; budget extra.
- **Corpus coverage**: rare descriptors (skinned characters, multi-player,
  scene loads) must be in the corpus before a kernel goes default-on.
- **FP corner cases**: SPU single precision differs from IEEE (no
  denormals, no NaN/Inf semantics, RTZ); the lift encodes this, the native
  code must too.
- **Lift changes** (new extra entries, lifter updates) move function
  boundaries: hash pins catch it; re-audit before regenerating.
