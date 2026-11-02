# Native geometry jobs: plan and progress

Goal: replace the lifted SPU code of the studio's geometry jobs (image 41,
then image 40) with native C++ that is **bit-exact** against the lift, so
the SPU job chain stops bounding the frame on this PC and the port gets
within reach of weaker CPUs (Steam Deck, laptops, Switch 2-class ARM).

This is a living document. Update the tracker tables after every session.

## Course correction (2026-11-01, supersedes conflicting text below)

**Status: the phase-3 whole-job path is slower than the lift it replaces.**
JOB1 vs JOB0 on the menu: FPS -8.78%, SPURS worker cycles/frame +15.9%
([performance](native-geometry-phase3-performance.md)). The cause is the
method, not a missing kernel. Each "native" kernel is a transliteration of
the SPU instruction stream (`spu_shufb`/`spu_rotqbii` on a `u128 r[128]`
bank over LS), required to reproduce every register, dummy iteration,
prefetch and dead final value at every function boundary. That is the same
work the lifted C already does at -O2, plus overhead. Differential RIP
samples (`spans-rip-v3`, native vs off) attribute the extra lane time:

| Overhead | Share of active lane samples |
| --- | --- |
| Shared atomic ledgers + option getters (`native_geometry_job.cpp`) | ~4.5% |
| Admission walks `read_*`/`prove_*` repeating each loop before emit | ~4.9% |
| SPU-ABI shims: 2 KiB bank copies, `*_setup_abi`/`*_final_abi`/spills | ~4% |
| Outlined per-group encoder helpers returning ~700-byte structs | ~3.7% |

Already corrected (measure on top of these, keep them):

- `d0a688b`: per-thread sharded ledger counters, constant-initialized option
  cache, force-inlined encoder helpers. Menu JOB1 34.99 -> 35.94 FPS
  (+2.7%, both reversed pairs); JOB1 vs JOB0 gap -7.3% -> -4.8%.
- Closed-form output-encoder admission, narrowed weighted-skin palette scan,
  no per-call heap in unit vectors (exact; differential test
  `tests/geometry-output-admission-test.cpp`). Menu JOB1 35.21 -> 36.57 FPS
  (+3.9%, pairs +3.89%/+3.84%); JOB1 vs JOB0 gap now -3.3%.
- `b934a88`: the Vulkan runtime set follows `renderer` in `game-build.json`.
  Before it, a frozen build directory without "vulkan" in its name silently
  ran with FIFO present and no batching (~5 FPS). Name freezes
  `build-vulkan-*` anyway and check the run log for `present mode mailbox`.

### New exactness boundary: the job, not the function

Ground rule 1 now means **bit-exact at the job boundary**: the ordered
DMA/atomic/channel transcript, every byte the job publishes (vertex, index
and RSX command output) and the job's exit state. Corpus replay already
checks exactly this (19,508 jobs / 58,195 transfers); live whole-job I/O
comparison checks it in game. SPU floating-point semantics still apply to
every published value (RTZ, denormal flush, no contraction unless the SPU
op fuses).

Inside a job, native code no longer reproduces SPU registers, LS scratch,
dummy/lookahead iterations, prefetches, dead final register values or
SPU-ABI call frames that nothing outside the job observes. Per-function
strict full-state checkers stay available as debugging tools; they are not
required gates for new work and must not dictate data layout.

Consequences:

- Stop writing new per-function SPU-ABI transliterations, including cold
  storage formats with zero real coverage. Unsupported descriptors fall
  back to the lifted job as a whole.
- Phase 7 (strict Stage F register reconstruction) is cancelled. Stage F
  only has to meet the job-boundary gate like everything else.
- Default decisions require JOB1 to be at least as fast as JOB0, in FPS
  or in worker cycles per completed frame, before any further coverage work.

### Phase 3 work order from here

1. **3a: remove the remaining overhead without changing outputs.** One host
   register bank per job (REGISTERS everywhere, no `memcpy` of `c.gpr` per
   call); drop the per-call admission walks that the job-level admission
   already implies; no heap allocation on the hot path (`std::vector` in
   `color`, `fixed_stream`, `float32_stream`, `half_stream`, `typed_stream`,
   `blend_scatter`, `full_commands`, `position_commands`, `run_lists`,
   `command_publish`). Measure. Target: JOB1 >= JOB0 on the menu.
2. **3b: host data pipeline.** For a recognized job, decode its Edge input
   streams once into host little-endian SoA float/int arrays; skin,
   transform and cull on those arrays with AVX2 (8 lanes, not 4-lane u128
   emulation); write each output stream once, in its final format, into
   the LS/DMA area the job publishes. Start with the profile's hottest
   paths: float/half/CMP output, unit vectors, weighted skin, index
   triangles. Unrecognized jobs run the lifted job unchanged.
3. **Gates per step:** exact corpus replay of all job outputs; live
   whole-job I/O comparison (>=10,000 jobs); native menu and round frames
   inspected; same-EXE reversed menu and round FPS plus worker/process
   CPU per completed frame.
4. **Housekeeping:** collapse the 61 per-stage option switches into
   `BUZZ_GEOMETRY_JOB` plus a few group switches once 3b replaces those
   stages. Keep results documents short: a table per measurement and the
   decision, not a narrative of every proof.

## Continuing objective (revised 2026-11-01)

The **Course correction** above and the user's latest instructions govern
this objective and supersede conflicting historical requirements below.
Preserve the improvements in `d0a688b`, `1abd397` and `b934a88`; `51ab514`
defines the revised method. Continue in this order:

1. **3a: eliminate overhead with unchanged job outputs.** Use one host
   register bank for the entire job, extending REGISTERS throughout without
   per-call `memcpy(c.gpr)`. Remove per-call admission already implied by
   whole-job admission. Remove hot-path `std::vector` allocation in `color`,
   `fixed_stream`, `float32_stream`, `half_stream`, `typed_stream`,
   `blend_scatter`, `full_commands`, `position_commands`, `run_lists` and
   `command_publish`. Target: JOB1 >= JOB0 in the menu.
2. **3b: build the host data pipeline.** Decode recognized Edge streams once
   into little-endian host SoA arrays. Skin, transform and cull using AVX2
   with eight lanes. Write each output stream once in its final format at
   the location from which the job publishes it. Prioritize float/half/CMP
   output, unit vectors, weighted skin and index triangles in profile order.
   Unsupported descriptors use the lifted job as a whole, without partial
   mutation before the fallback decision.
3. **Validate each increment at the job boundary.** Require the exact ordered
   DMA/atomic/channel transcript, every published vertex/index/RSX byte and
   job exit state against all 19,508 corpus jobs / 58,195 transfers, plus
   >=10,000 live whole-job I/O comparisons and inspected native menu/round
   frames. Preserve SPU FP semantics for published values: RTZ, denormal
   flush and no contraction unless the SPU operation fuses. Internal SPU
   registers, LS scratch, lookahead/dummy iterations, prefetches and SPU-ABI
   frames are not requirements. Existing per-function strict checks remain
   optional debugging tools and must not dictate the native data layout.
4. **Measure and decide.** Run reversed same-EXE JOB0/JOB1 menu and round FPS
   series and separate worker/process cycles and CPU time per completed
   frame. Freeze builds as `build-vulkan-*`, record `renderer` in their
   manifests and verify both `window present mode mailbox` and
   `two-context asynchronous draw batches enabled` in every run log. Never
   run another game, compilation, CPU tests or independent profiling during
   a measurement. Default-on requires all gates and JOB1 >= JOB0; 3a's
   explicit target remains menu FPS parity or better. Do not substitute
   isolated-option gains for the aggregate JOB0/JOB1 comparison.
5. **Document, commit and continue.** For each validated increment, record a
   short measurement table and decision, then make a local commit. Preserve
   original saves, private corpus/derived-code policy, prior frozen builds
   and source/evidence bindings. After phase 3 passes, continue the remaining
   required shared native work in phase 4 under the same job-boundary method.
   Phases 5/6 remain optional. Consolidate stage switches into
   `BUZZ_GEOMETRY_JOB` and a few groups as 3b replaces them.

Stop new per-function SPU transliterations, cold-format implementation and
coverage work without real inputs, and round interpreter-count work.
**Phase 7 is cancelled**, including strict Stage F register reconstruction.
Stage F follows the same job-boundary contract. Do not resume these cancelled
or stopped tracks from older goals, checkpoints or results documents.

### Current implementation checkpoint (2026-11-02)

The experimental host output path passes correctness gates, but remains off:
menu FPS +0.964%, round FPS -0.077%, round worker cycles/frame +1.734%.
See the [measurement table and decision](native-geometry-phase3b-results.md).
Eight-vertex input helpers pass original-decoder and guarded source tests.
Job-owned input SoA now matches 54,481,150 logical component values across
19,399 real captures; 109 fixed-format entries retain whole-job fallback
([results](native-geometry-phase3b-input-results.md)). Eight-vertex skinning,
object transforms and direction normalization now match the full original
producer for those same 19,399 captures ([skin results](native-geometry-phase3b-skin-results.md)).
Host projection now produces exact logical clip records for the same family
([clip results](native-geometry-phase3b-clip-results.md)). Direct host index decoding now matches all
19,508 captured index streams ([index results](native-geometry-phase3b-index-results.md)).
Host culling now matches the composed original flow for 19,399 jobs
([cull results](native-geometry-phase3b-cull-results.md)). Final host index
allocations now match all logical and alignment bytes
([index output results](native-geometry-phase3b-index-output-results.md)).
Direct host vertex output now matches 5,801 admitted visible buffers, including
alignment bytes ([vertex output results](native-geometry-phase3b-vertex-output-results.md)).
Unsupported output storage and unresolved padded skin indices retain whole-job
fallback. The experimental host job pipeline now integrates these producers:
all JOB0/JOB1/JOB2 corpus arms match 19,508 jobs /58,195 transfers, and live
whole-job comparisons exceed 1,275,000. See [integrated pipeline results](native-geometry-phase3b-host-pipeline-results.md).
Native frames are normal. Reversed aggregate menu FPS improves +3.712%, with
worker cycles/frame −8.668%; round FPS improves +1.460% (both pairs positive).
The fresh reversed round CPU series improves worker cycles/frame −6.747%
and process cycles/frame −4.335%, with both pairs positive for CPU savings.
All experimental-profile correctness, native-frame and separate FPS/CPU
gates now pass. The initial incomplete CPU series remains rejected. Stage
switch consolidation now provides a compact host/publication/commands
profile. Its controller tests and eight-arm whole-job corpus pass
([profile results](native-geometry-phase3b-profile-results.md)). Live I/O
exceeds 1,275,000 completed comparisons and both native frames look normal.
The compact profile's reversed menu series improves FPS +3.510%, worker
cycles/frame −8.957% and process cycles/frame −4.920%, in both pairs.
Its separate round FPS series improves +2.849%, with both pairs positive.
Round worker cycles/frame improve −5.303% and process cycles/frame −3.287%,
with both pairs positive for CPU savings. All compact-profile gates pass.
The qualified EU 1.00 Vulkan/AVX2 normal build now selects phase 3 and both
launchers apply manifest JOB1 while preserving explicit JOB0. Default/off
normal-launch replay and actual default native menu/round frames pass;
the relink differs from the measured executable only in its timestamp.
See [default integration results](native-geometry-phase3b-default-results.md).
Phase 3b's supported image-41 pipeline is complete; unsupported jobs retain
whole-job fallback. Next: required phase 4 under the corrected host-data method.

### Historical implementation notes (superseded where conflicting)

The notes below retain prior evidence; their former next steps and gates do
not override the Course correction or the revised objective above.

Current next milestone: continue host data across attributes/skin/output and
close reliable interpreter-count validation, preserving existing StageF
vertex/cull boundaries until phase7. Shared-bank measurements on frozenbc0d35df
are complete:menuFPS+0.894%,round+0.169% with mixed pairs/tails;worker cycles/frame
-0.341%/-0.010%,round worker CPUtime-1.148% in both pairs,process results mixed.
REGISTERS stays0 after this small/mixed result. The implementation remains an
opt-in first host-data boundary.
The [exact interpreter counter foundation](native-geometry-interpreter-counts.md)
passes production-state neutrality, concurrent recorder/process and observer
failure tests. New9691ca43 EXE/a6bb229f MAP passes4full-corpus arms,each19,508exact
jobs/58,195transfers;all3enabled arms explicitly count0. Reference/native live
menu both prove all-image0 at requested15/60s,with actual read boundaries and
the first failed timing-admission attempt preserved. The observer defaults off
and is explicitly off in FPS/CPU series. Next is the round's positive per-page
inventory with justified matched work; its count gate remains open.
REGISTERS0/1 integrates all seven cores directly with the formatter's host
bank,default0. Ten frozen arms each pass19,508exact jobs/58,195transfers,
unchanged old ledgers and7,201host roots/17,278children where enabled. New
live checks pass45,000whole-job/I/O +35,000parents +85,833children;separate
UNORM2 proves>=10,000 strict calls. Native menu/actual round images pass.
Same-EXE reversed FPS+separate worker/process cycles/CPU are documented in the
[performance results](native-geometry-phase3-performance.md#shared-formatter-bank-measurements-and-default-decision). The
[current phase3 audit](native-geometry-phase3-remaining.md) identifies the
remaining host-data requirement;SPU-ABI wrappers and LS streams still remain.
The earlier isolated SPANS round FPS repetition on3df improves2.22%(pairs+2.61/+1.84),with
median/p90/p99 lower in both pairs. The earlier one-run54.80ms p99 remains
recorded and was not reproduced. SPANS is validated opt-in;its product default
stays0 with the experimental parent,and optimized native profiles use SPANS1.
Separate diagnostic profiles guide the next source change,without timing
claims. Round interpreted counts,cold real/live,full host-buffer integration,
aggregate/default decisions and final strictStageF7 remain open.
All five standard input decoders and the complete standard93F0 controller
are implemented and integrated behind independent default-off switches.
Their standalone and production-routing checks pass; real coverage of the
new builtin formats1..5 and the final acceptance gates remain OPEN.
Standard input production routing is now complete opt-in:3,876 wrapper roots,
1,938 parent-strict/1,440 child-strict;576 parent roots/all36 format pairs;
292 layout cases/877 rejects;472 production getter cases. Whole-job admission
protects parent frames and scratch on retained and new routes. Five final frozen
arms each pass19,508 exact jobs/58,195 transfers, all19,508 native with zero
whole-job fallback in enabled arms. The controller has19,508 real raw0 roots,
reusing the old helper; new inputs1..5 remain real/live0. ABI48/61 stages.
The initial5,838 coverage regression from an overlarge declared descriptor span
was rejected and corrected; final ledgers match after raw-stage migration.
Whole-job/raw0 live and menu/actual-question frame gates now pass on ABI48.
Cold-path live, interpreter/reversed A/B/normalized CPU/default gates stay OPEN.
The [input audit](native-geometry-input-audit.md) maps all23 standard blocks
and six95C4 routes. The native54E8
output controller now handles all seven paths, including all four complete
output built-ins; independent0/1/2 switches default0. That earlier ABI47
freeze is preserved; the current input integration uses ABI48/61 stages.
Synthetic builtin integration passes15,988 strict complete-controller/data
roots plus4,472 unchanged rejects; production routing adds3,072 native /
1,536 parent-strict /1,536 child-strict roots. Earlier standalone payload
and descriptor-control proofs remain valid. Five frozen full-corpus arms
each pass19,508 exact jobs/58,195 transfers; enabled controller arms have
7,201 native descriptor roots/zero fallback. All four new builtin real roots
remain0. Cold real/256-live/native-frame, interpreter/reversedA/B/normalizedCPU
and default gates stay OPEN; JOB0/geometry827/newflags0 remain unchanged.
The [conversion audit](native-geometry-conversion-audit.md) and separate
[phase3 results](native-geometry-phase3-results.md#native54e8-built-in-output-control-abi47)
record complete function coverage and the limits of synthetic acceptance.
The float32 output payload `46D0` now has a complete opt-in implementation,
including every final iteration and both scatter arms. V2 excludes every active
interrupt context and passes 6,177 strict cases plus five exact full-corpus
arms, with 7,201 native roots and zero fallback per enabled arm.
Binary16 `4880` now also has a complete
seven-block opt-in path, including post-store prefetch and full final ABI.
It passes 7,215 strict cases, 1,048,576 independent scalar inputs and five
exact full-corpus arms, with 4,513 native roots and zero fallback per enabled
arm. Live single/mixed checks and native menu/round frames pass.
Packed CMP `4FDC` also has its complete five-block encoder: 7,471 strict
cases, 32,772 independent dyadic encodings, 256 special-FP roots and five
exact corpus arms, with 4,424 native roots/zero fallback per enabled arm.
SNORM16 `4478` now also has a complete five-block encoder: 7,471 strict
cases, 131,088 independent dyadic components, 256 special-FP roots and five
exact corpus arms with 1,104 mixed native roots/zero fallback per enabled arm.
Strict mixed live checks and native menu/round frames pass.
UNORM8 `4B9C` also has a complete five-block path: 7,471 strict cases,
131,088 independent components, 256 special-FP roots and five exact corpus
arms with 36 mixed native roots/zero fallback per enabled arm. Dedicated
strict UNORM live checks and actual-native frames pass. I16 and U8 integer
were the last two storage implementations. Both now have complete five-block
kernels: 7,503 strict cases and 1,048,576 independent FP-bit components each,
plus 1,152 synthetic enclosing-conversion cases (576 native strict roots each).
Their real corpus/live counts remain zero; those acceptance gates stay OPEN.
All seven storage implementations exist. Shared `4178` is now production opt-in:
1,908 standalone strict cases, 117 routing cases, five exact corpus arms with
7,201 strict real roots and zero fallback, plus native live checks and normal
menu/round frames. Its own 0/1/2 switch defaults off; final interpreter/A/B/CPU
gates remain OPEN. Output built-ins and remaining cold real/live acceptance
are still required.
Custom callback boundaries are now
excluded from paired replay until independently audited; they execute once
on the retained arm. This is comparison protection, not native conversion.
Ring/atomic flow `37C0` is now production
opt-in with `BUZZ_GEOMETRY_JOB_RING=0/1`, default 0; JOB=2 checks its exact root
state/I/O. It passes 5,821 standalone comparisons, seven unchanged rejects,
six full-corpus arms and >=35,000 strict live root comparisons. Native menu/
round frames are normal. Cold retained paths and final acceptance gates stay open.
The [allocator audit](native-geometry-ring-audit.md) inventories 39 blocks,
7,201 original allocator calls and 14,422 ordered ring transfers. Its cold
interrupt-enable continuation `3CF4` contains a branch to `3B28`, but the
unregistered target still needs resolver/interpreter acceptance. The kernel
rejects enabled/saved interrupts before mutation and preserves original
reporting handoffs; neither those handoffs nor the audit count as fully native
allocator acceptance.
Transform control, vertex preparation, alternate cull data and finalization
now have complete native opt-in implementations, alongside the existing
normal Stage F loops. Alternate data still has zero real/live coverage and
open gates. See the [transform contract](native-geometry-transform-audit.md).
The complete AEA0/7390 index flow is also native opt-in. Required cold coverage,
final validation/performance/default gates and strict Stage F reconstruction
remain open; these implementation milestones do not close phase 3.

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

1. **Bit-exact at the job boundary or it does not ship** (revised
   2026-11-01, see the course correction above). Every native job must
   reproduce the lift's DMA/atomic/channel stream, all published output
   bytes and its exit state for every corpus job. Intra-job SPU registers
   and LS scratch are not part of the contract. Floating point follows SPU semantics (round toward
   zero, flush denormals, no FMA contraction unless the SPU op fuses);
   mirror the stage F build flags (`-mavx2 -mfma -frounding-math
   -ffp-contract=off`, `SPU_NATIVE_RTZ_*`).
   The former Stage F volatile-register exception and the phase 7 strict
   reconstruction are obsolete under the job-boundary rule. See [data map and harness](native-geometry-data.md).
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

The complete storage-3 halfword handler (`0x8378` -> `0xCAC0/CAD8/CB78`)
also has native data decoding behind
`BUZZ_GEOMETRY_JOB_HALF_STREAM=0/1` (default 0). Host AVX2 column/group loops
retain the gather table's low output bits and reconstruct all final ABI
state without a lifted last-column callback. Tests pass 8,192 strict cases,
covering all 65,536 halfword encodings, plus 23 unchanged-state rejects;
the descriptor matrix also exercises native half/unit/color combinations.
Four frozen corpus arms each pass 19,508 jobs / 58,195 transfers; all native
arms cover 12,431 half regions with zero fallback, and JOB=2 checks every
region and the whole-job/I/O stream. Remaining typed handlers, weighted
scatter and enclosing pipeline bodies remain open; final performance and
default decisions still require the phase-3 gates. Live checking passes at
least 280,000 strict half-region and 425,000 whole-job/I/O comparisons; native
menu/round readbacks look normal. Evidence is recorded in the phase-3
results document.

The complete storage-2 float32 handler (`0x87CC/8800/8878/8910/8928`) now
copies raw payloads natively, preserving cross-quad and circular within-quad
behavior, unused components and full branch-specific register state. Its
independent `BUZZ_GEOMETRY_JOB_FLOAT32_STREAM=0/1/2` switch defaults to 0;
mode 2 strictly compares this DMA-free region even with native JOB=1. This
allows real round calls to be checked without slow whole-job validation
preventing timed navigation. No comparison fields are relaxed. The corpus
contains only 12 calls; all are native with zero fallback in five frozen
replay arms, including independent kernel-checked JOB=1 / FLOAT32=2. Tests
pass 5,440 strict cases and 25 unchanged-state rejects. Final live coverage
passes at least 20,000 strict float32 calls in the round sequence and 70,000
whole-job/I/O checks in the menu; native menu/actual-round readbacks look
normal. Evidence is in the phase-3 results document; phase 3 and performance/
default gates stay open.

The complete storage-8 fixed-point handler (`0x9058/C810/C830/C910`) now
uses host bit extraction, word-wrapped offsets and strict integer/fraction
conversion, with a final full-ABI materialization. Its independent
`BUZZ_GEOMETRY_JOB_FIXED_STREAM=0/1/2` defaults to 0. Tests pass 9,216 strict
cases plus 33 unchanged-state rejects; five frozen replay arms each pass
19,508 exact jobs / 58,195 transfers, including all three native fixed calls
without fallback. Native menu/round images look normal and whole-job live
checks pass. The fixed-specific live gate passes at least 5,000 strict
comparisons in the later round sequence (the menu has no fixed calls).
This opt-in implementation remains default-off until final performance/
default measurements. Separate phase-3 results record the source-bound
evidence; phase 3 remains open.

Complete native data implementations now cover the remaining storage families
1 SNORM16, 5 I16, 6 CMP and 7 U8 integer, sharing guarded layout code and
retaining separate default-off 0/1/2 switches. Their pinned oracles pass
30,208 strict cases and 108 unchanged-state/no-I/O rejects; mixed descriptor,
real driver-entry and 144 production-getter tests pass. Five frozen corpus
arms each pass 19,508 exact jobs / 58,195 transfers, but contain zero calls
to these four families; replay compatibility is not new data-path coverage.
Native menu/round images look normal and whole-job live checks pass. The
separate phase-3 results record each actual native/strict live counter and
any still-open >=256-call gate. No default is promoted; weighted scatter,
enclosing stages and final performance/default work remain open.

The complete `0xC750/C7A8` weighted scatter now uses a host pipeline that
preserves duplicate destinations, source/output aliases, map pointer bias,
wrapped physical addresses and the asymmetric prefetch/store order. Its
independent `BUZZ_GEOMETRY_JOB_BLEND_SCATTER=0/1/2` defaults off and compares
the full root state with JOB=1 or JOB=2. Tests pass 6,153 strict cases plus
14 unchanged-state/no-I/O rejects; descriptor tests include 1,198 native
scatter calls. Five final frozen replay arms each pass 19,508 exact jobs /
58,195 transfers; all 2,326 scatter calls are native without fallback.
Actual strict live/root and normal native menu/round proofs are recorded in
the separate phase-3 results document. Default/performance gates, enclosing
pipeline implementations and the four cold typed-family live gates remain
open; this does not close phase 3.

The `0x0BF8` list/blend stage now has native reverse selection, double-buffered
DMA/prefetch, capacity deferral/reset and caller restoration. Its independent
`BUZZ_GEOMETRY_JOB_BLEND_LISTS=0/1` defaults off; JOB=2 checks the strict root
against a bounded I/O transcript without repeating guest transfers. Tests pass
2,672 strict root/call-ABI/I/O cases plus 15 unchanged-state/no-I/O rejects,
including 5,415 buffer resets. All four frozen replay configurations pass
19,508 exact jobs / 58,195 transfers; enabled paths have 328 BF8 roots and
zero fallback. Strict live checks pass at least 40,000 BF8 roots, with normal
native menu/round readbacks; the first fixed-timing checked diagnostic stopped
at a profile prompt and is not counted as round coverage. The successful
retry and final performance/default gates are tracked in the separate results
document. Its `0x7118` decoder is separately implemented and validated below;
native enclosing control alone does not establish native data coverage.
No FPS/CPU benefit is claimed.

The complete `0x7118/7298` run-list decoder now uses backward host expansion
and exact final pipeline ABI, with an independent default-off
`BUZZ_GEOMETRY_JOB_RUN_LISTS=0/1/2`. Tests pass 10,774 strict/numeric cases,
including all 65,536 control encodings, plus 17 unchanged-state/no-I/O rejects.
Five frozen corpus configurations each pass 19,508 exact jobs / 58,195
transfers. Enabled paths have 1,198 native roots without fallback: 895
nonempty expansions and 303 empty returns, counted separately. The strict
live data gate passes at least 580,000 nonempty expansions (plus 260,000
empty calls), with normal native menu/round readbacks. Final performance/
default gates remain open; full evidence is in the phase-3 results.

The `0x93F0` custom-descriptor path now has independent native dispatch and an
exact `0x7DF8` handoff. The corpus audit distinguishes its 11,414 former raw
fallbacks from built-in formats 1..5, which have zero real occurrences:
configuration pairs are `(0,255)` in 18,936 jobs and `(0,0)` in 572. Synthetic
tests pass 5,000 strict state/handoff cases and 13 unchanged/no-I/O rejects;
176 configuration cases and the complete driver-entry models pass. The first
combined frozen checked replay passes all 19,508 jobs / 58,195 transfers,
with 11,414 native custom handoffs / 0 fallback and 19,508 native raw paths /
0 fallback. All five replay configurations pass; live checking reaches at
least 410,000 strict handoffs, and native menu/round readbacks look normal.
Final performance/default gates remain open; this does not validate built-in
formats 1..5 or close phase 3.

The `0x9F8` skin/rigid controller now has an independent native implementation
through the exact AF18 weighted tail, final 2C00 normalization tail or r0
return. Its `BUZZ_GEOMETRY_JOB_SKIN_DISPATCH=0/1/2` switch defaults off; both
JOB=2 and its own mode 2 compare all state before any queued data tail.
Synthetic coverage passes 6,144 strict state/call-trace cases plus eight
unchanged/no-I/O rejects, all driver-entry models and 184 configuration cases.
Five frozen replay configurations each pass all 19,508 jobs / 58,195 transfers,
with 1,628 weighted handoffs and 17,880 rigid calls, zero fallback; all are
strictly checked in the three comparison arms. Live checking passes at least
300,000 rigid boundaries and 135,000 weighted handoffs; native menu and actual
round readbacks look normal. Final performance/default gates remain open
and are tracked in the separate phase-3 results. AF18 palette/batch control
is now implemented separately below; remaining rigid, cofactor and normalized
directional data retain explicit inventory rows. A native controller count
is not native data coverage. The later Course correction cancelled phase 7.

The complete `0xB880/B934/B940` weighted position/matrix kernel now has a
native implementation with no original setup or final-pair call. Its own
`BUZZ_GEOMETRY_JOB_WEIGHTED_SKIN=0/1/2` defaults off and preserves the old
phase-2 leaf path when disabled or ineligible. It supports one-pair counts,
quadword-floor pointer biases, independent palette rows and ordered actual
palette/output aliases. Tests pass 6,912 strict states and 12 unchanged-state
rejects, including sparse gaps and nonzero feedback aliases. All five frozen
V3 corpus configurations pass 19,508 exact jobs / 58,195 transfers; enabled
paths have 6,113 native data roots / zero fallback, including 30 real aliases.
The three comparison arms check every data root and alias. Live checking
passes at least 430,000 data calls and 256 actual palette aliases; native
menu and actual round readbacks look normal. Final performance/default
gates remain open in the separate phase-3 results.

The reproducible AF18 audit (`tools/inspect/inspect-geometry-skin.py`) maps
38 region blocks and six directional roots. Its valid path has no DMA;
the job loader already staged palettes/weights. AF18 control/transpose now
has a separately switched native implementation below. Rigid skin positions now have an implementation below, with real/live gates
OPEN; cofactor normals now have an implementation below with real/live gates
OPEN. Normalized directional variants now have implementations below; their
real/live gates remain OPEN. Basic directional progress is
recorded below. Replacing B880 does not
close these inventory rows or phase 3. The later Course correction cancelled phase 7.

Basic directional kernels `0xBD4C/BE20/BF7C` now have a shared native host
loop, AVX2 arithmetic and exact per-variant final ABI, independently switched
by `BUZZ_GEOMETRY_JOB_SKIN_DIRECTIONS=0/1/2`, default off. The first primary
vectors and matrix group are consumed from their GPR cache; following-group
reads precede stores, including supported stream/matrix aliases. No original
data/setup/final block executes on this path. Tests pass 6,912 strict states,
three independent identity/cache checks and 12 unchanged-state rejects;
200 production configuration cases and the full driver-entry models pass.
All five frozen configurations pass 19,508 jobs / 58,195 transfers each,
with all 5,018 enabled data roots native / zero fallback: 1,158 single-stream
and 3,860 two-stream calls, all strictly checked in the comparison arms.
BF7C has zero real calls; its real/live gates remain open despite synthetic
success. Live checking passes at least 1,125,000 single-stream and 680,000
two-stream comparisons, with normal actual-native menu and round readbacks. Final
performance/default gates remain open in the separate phase-3 results. B4C0
dispatch is now native within AF18; normalized C14C/C280/B5A0 and
cofactor/normal BB58/BB68 remain required, separate work.

Native AF18 orchestration now replaces palette transpose, the 672-byte frame,
stream selection, pool capacity, batch control, B4C0 direction dispatch and
palette publication sequencing. `BUZZ_GEOMETRY_JOB_SKIN_FLOW=0/1/2` defaults
to zero; JOB=2 also forces strict complete-function state/return comparison.
Explicit retained library routines remain separately accounted for. Tests
pass 6,600 full-state/all-GPR call-transcript cases, three independent numeric
palette/padding checks, three unused-input early exits and eighteen unchanged
rejects. Production configuration passes 208 cases; full driver-entry models
pass. All five frozen replay arms pass 19,508 exact jobs / 58,195 transfers;
all 1,628 active work roots are native / zero fallback and strict in the
three comparison arms. Empty roots and rigid/normalized/cofactor data have
zero real trace coverage; their real/live gates remain OPEN. Live/render
results and frozen bindings are recorded in the separate phase-3 report.
Final performance/default gates remain open, with JOB=0/leaf827/newflags0.

The complete rigid skin data suffix `B348/B3C0/B3C8` now has a native pair
pipeline, independently controlled by `BUZZ_GEOMETRY_JOB_RIGID_SKIN=0/1/2`,
default 0. Host AVX2 prefix pairs preserve next-position/index reads before
aliased stores; explicit final-pair reconstruction keeps all state and exact
B4C0/BB58 tails. The kernel supports arbitrary masks/shuffles, biased and
wrapped gathers, and position/index/palette/output aliases. Tests pass
7,681 strict states, three independent numeric/padding cases and ten unchanged
rejects, plus 384 strict AF18/actual-rigid-data integration cases. Runtime
configuration passes 216 cases; first-fallback durability is checked with an
abrupt process exit. Full driver-entry models and five frozen compatibility
replays pass 19,508 jobs / 58,195 transfers each. **Zero rigid real-data calls**
are present: real-corpus/native coverage and the >=256 live strict gate remain
OPEN. Menu/round compatibility of a flagged build cannot close those gates.
Cold data, final measurements/defaults and phase 3 remain open; the separate
results document records source/binary/test and scene bindings.

The complete `BB58/BB68` cofactor normal transform/normalization now has a
native host group loop with AVX2 pairs and full final-group ABI reconstruction.
`BUZZ_GEOMETRY_JOB_SKIN_COFACTOR=0/1/2` defaults to 0; mode 2 or JOB=2 compares
all state through the exact B4C0 queued boundary, without exclusions. Group
inputs are cached before stores, preserving matrix/normal aliases and wrapped
matrix reads. Standard and arbitrary masks/shuffles retain exact FMS/FM/FMA
ordering and SPU FRSQEST/FI normalization. Tests pass 7,808 strict states plus
three independent geometric/padding checks and ten unchanged rejects; 768
strict AF18/actual-cofactor composition cases use original and native rigid
backends. Numeric tolerance 5e-4 applies only to the additional independent
geometric check, never to the strict state/corpus/live comparisons. All
previous data/control integration and full driver-entry models remain green;
224 production configuration cases pass. Frozen compatibility/scene results
are recorded separately. **Zero real BB58 calls** means nonzero native corpus
coverage and the >=256 live strict gate remain OPEN; normal scenes or a
compatible replay cannot close those gates. Other data/stages, final
performance/default gates and phase 3 remain open; strict Stage F stays last.

Normalized direction data C14C/C280/B5A0 now has complete native host loops
for one/two/three streams, independently switched by
`BUZZ_GEOMETRY_JOB_SKIN_NORMALIZED=0/1/2`, default 0. Cached first primary and
matrix inputs, distinct first/nominal pointers, all lookahead and ordered
stream/matrix aliases remain exact. AVX2 prefix arithmetic is followed by the
original SPU FRSQEST/FI normalization and explicit per-variant final ABI.
Tests pass 9,219 strict cases plus 9 independent geometry/cache/padding checks
and 12 unchanged rejects. The additional geometry tolerance 5e-4 never applies
to strict state/corpus/live checking. AF18 composition adds 1,152 actual-data
cases with three original/native rigid/cofactor combinations and all three
normalized roots. Existing control/data composition, full driver-entry
models and 232 production configuration cases pass. Frozen replay/scene
compatibility is recorded in the separate results. All AF18 data families now
have native implementations, but **zero real calls** to these normalized
roots means real/native corpus and >=256 strict live gates remain OPEN.
Other enclosing stages/built-in inputs/cold coverage and final interpreter/
reversed A/B/normalized CPU/default decisions still keep phase 3 open. The later
Course correction cancelled phase 7 and stopped interpreter/cold coverage work.

### Native compressed-index delta reconstruction

`7730/7770` now has a complete native implementation, independently selected
by `BUZZ_GEOMETRY_JOB_INDEX_DELTAS=0/1/2`, default 0. It preserves all eight
u16 prefix chains, warmup/flush, input lookahead, aliases and the full final
GPR/LS/77F8 boundary without an original final iteration. Strict tests pass
5,763 cases plus three independent numeric checks and eight unchanged rejects;
five corpus arms each pass 19,508 exact jobs / 58,195 transfers. All 9,763 real delta
calls are native with zero fallback, comprising 2,999 one-group + 6,764 bulk calls.
Strict live menu checks exceed 235,000 one-group + 315,000 bulk calls. Frozen
native scene evidence is recorded in the [phase-3 results](native-geometry-phase3-results.md#complete-index-delta-reconstruction-77307770).
The [remaining index inventory](native-geometry-index-audit.md#native-implementation-inventory)
keeps the controller, relocation and triangle
final ABI explicit. This completes one data region, not 7390 or phase 3.
Final interpreter/reversed A/B/normalized worker/process CPU/default gates
remain open. JOB=0 / leaf 827 / all new flags=0 are unchanged; strict Stage F remains last.

### Native compressed-index bit unpack

`74F0/7640/7C20/7CD0/77E8` now has a complete native low/high implementation,
selected by `BUZZ_GEOMETRY_JOB_INDEX_UNPACK=0/1/2`, default 0. It keeps both
warmups, repeated final-block stores, backward progression, aliases and the
entire final GPR/LS/7730-or-77F8 boundary. Preflight simulates every write
using the real increment table and cached selector before any mutation.
No original setup, step or final iteration executes.

Tests pass 5,378 strict cases, including 256 independent packed-value cases,
plus 10 unchanged rejects and 248 configuration cases. The ordinary MSB-first
checks cover widths 1..15; width 16 has a separately derived
[pinned-lift behavior](native-geometry-index-audit.md#width-16-source-compatibility).
Five corpus arms each pass 19,508 exact jobs / 58,195 transfers; all 9,763
unpack roots are native with zero fallback (7,211 low + 2,552 high). Combined
arms retain all 9,763 native delta roots. Strict live menu checks exceed
545,000 low + 55,000 high calls. Frozen native scene evidence is in the
[phase-3 results](native-geometry-phase3-results.md#complete-lowhigh-index-bit-unpack-74f076407c207cd0).
7390 control, relocation and triangle final ABI remain
unfinished, alongside the other enclosing stages/builtins/cold coverage.
Final interpreter/reversed A/B/normalized worker/process CPU/default gates
remain open. JOB=0 / leaf 827 / new flags=0 are unchanged; strict Stage F is last.

### Complete native compressed-index flag expansion

`7838/78A8` now has a complete native implementation through 79A8 or 7DCC,
selected by `BUZZ_GEOMETRY_JOB_INDEX_EXPAND=0/1/2`, default 0. Host vectors
and prepared shuffle masks retain the explicit/sequential merge, warmup,
all lookahead, ordered writes and full final ABI. The capacity quad is read
after writes, preserving manager/output alias effects and signed comparison.
The older phase-2 bit6 prefix remains independent; no original setup, step or
final iteration executes in the new native region.

Tests pass 4,691 strict cases including 64 independent scalar merges and
two post-write capacity reversals, plus seven unchanged rejects and 256
configuration cases. Both tails are covered without executing reporting I/O
twice. Five corpus arms each pass 19,508 exact jobs / 58,195 transfers, all
19,508 expansion roots native with zero fallback (7,123 short + 12,385 bulk).
Combined arms retain 9,763 unpack and 9,763 delta roots. Strict live menu checks
reach at least 130,000 short + 330,000 bulk calls; native scene evidence is recorded in
the [phase-3 results](native-geometry-phase3-results.md#complete-flag-expansion-783878a8).
7390 control, relocation and triangle final ABI remain unfinished. Final
interpreter/reversed A/B/normalized worker/process CPU/default gates stay
open. JOB=0 / leaf 827 / new flags=0 remain unchanged; strict Stage F is last.

### Complete native compressed-index command relocation

`79A8/79C8` is completely native through queued 7A90, including zero warmup,
repeated first destination, store-before-next-read aliases and full final ABI.
`BUZZ_GEOMETRY_JOB_INDEX_RELOCATE=0/1/2` defaults to 0. All 5,777 strict cases,
including 1,152 independent byte models, eight unchanged rejects and 264
configuration cases pass. Five corpus arms each pass 19,508 exact jobs /
58,195 transfers, with all 19,508 relocation calls native / zero fallback
(0 warmup, 9,756 short, 9,752 bulk). Combined arms retain the preceding three native index
regions. Strict live menu comparisons reach at least 460,000; inspected
native menu and actual round frames are normal. See the
[phase-3 results](native-geometry-phase3-results.md#complete-command-relocation-79a879c8).
7390 control and complete triangle/final ABI remain required. Interpreter,
reversed same-EXE A/B, normalized worker/process CPU and default gates remain
OPEN; JOB=0 / leaf 827 / new flags=0 are unchanged. Strict Stage F stays last.

### Complete native triangle stitching and frame restoration

All of 7A90 is now native through queued 2738: cached inputs, full body/final
iteration, post-store LUT feedback and late 432-byte-frame restoration.
`BUZZ_GEOMETRY_JOB_INDEX_TRIANGLES=0/1/2` defaults to 0; the phase-2 bit3
fallback remains independent. Tests pass 4,993 strict cases, including 80
independent frame models and 32 mutable-LUT proofs, nine unchanged rejects,
separate fallback/reference routing and 272 configuration cases. Five corpus
arms each pass 19,508 exact jobs / 58,195 transfers, all 19,508 triangle calls
native / zero fallback (0 warmup, 6,833 short, 12,675 bulk). Combined arms retain the preceding
four native index regions. Strict live comparisons reach at least 465,000;
native menu and actual round frames look normal. See the
[phase-3 results](native-geometry-phase3-results.md#complete-triangle-stitching-and-frame-restoration-7a90).
Whole index control/setup/publication and other enclosing paths remain open.
Final interpreter/reversed A/B/normalized worker/process CPU/default gates
remain OPEN; JOB=0 / leaf 827 / new flags=0 are unchanged. Strict Stage F is last.

### Complete native compressed-index controller

AEA0/7390 now owns native control, frames, metadata/zero-width setup, 2738/end
and outer/original-pointer publication, and exact root return. Five data
switches remain independent; all enabled compose the complete native decoder.
`BUZZ_GEOMETRY_JOB_INDEX_CONTROL=0/1/2` defaults to 0. Eligibility checks all
writes and four error frontiers before mutation; unsupported inputs keep the
retained control without native credit. Tests pass 5,633 strict cases, all 32
child combinations, 256 independent byte models, 20 unchanged rejects, API
routing and 280 config cases. Five arms each pass 19,508 exact / 58,195 transfers,
all 19,508 controller roots native / zero fallback (9,745 zero + 7,211 low +
2,552 high). Combined arms retain every native data region. Strict live root
checks reach at least 455,000; native menu/round frames are normal. See the
[phase-3 results](native-geometry-phase3-results.md#complete-index-control-frames-and-publication-aea07390).
Other enclosing stages/builtins/cold functions and final interpreter/reversed
A/B/normalized worker/process CPU/default gates remain OPEN. JOB=0 / leaf 827 /
new flags=0 remain unchanged; strict Stage F stays last.

### Native transform result publication and frame return

Complete 6958/66C8 finalization is native opt-in, including shared 2738
publication, exact selected bytes and late restoration from aliased frames.
`BUZZ_GEOMETRY_JOB_TRANSFORM_FINISH=0/1/2` defaults to 0. Tests pass 4,929
strict cases including 320 independent byte models, six unchanged rejects,
API routing and 288 configuration cases. All five corpus arms pass 19,508
exact / 58,195 transfers; all enabled calls are native with zero fallback.
Strict live publication checks reach >= 465,000; actual-native menu and
round frames are normal. Count-only remains synthetic with real/live gates
OPEN. Enclosing 6608 control, vertex preparation, alternate data, other stages
and final interpreter/reversed A/B/CPU/default gates remain OPEN. See the
[phase-3 results](native-geometry-phase3-results.md#transformcull-audit-and-native-finalization-695866c8).
Strict Stage F reconstruction remains last; JOB=0 / leaf 827 / new flags=0.

### Native vertex matrix preparation

Complete 6188/65EC/65FC preparation is native opt-in through queued `63E0`.
`BUZZ_GEOMETRY_JOB_VERTEX_PREPARE=0/1/2` defaults to zero, with full-state
strict checks before the existing Stage F loop and no Stage F waiver.
8,513 strict cases cover every format/bias pair, 256 special FP cases,
64 independent matrix cases and six unchanged rejects. API routing and 296
configuration cases pass. Five corpus arms each pass 19,508 exact / 58,195
transfers, all 19,508 preparations native / zero fallback (10,699 base + 8,809
middle). Live strict handoffs reach >= 470,000; native scene frames are
normal. Five/other formats remain cold with open real/live gates. Enclosing
6608, alternate cull data, other stages and final interpreter/reversed A/B/CPU/
default gates remain OPEN. See the [phase-3 results](native-geometry-phase3-results.md#complete-vertex-matrix-preparation-618865ec65fc).
JOB=0 / leaf 827 / new flags=0 remain unchanged; strict Stage F stays last.

### Complete native enclosing transform control

Complete 6608/6618/6744/6790/bypass/mode dispatch is native opt-in. Own
`BUZZ_GEOMETRY_JOB_TRANSFORM_CONTROL=0/1/2` defaults to zero. The new checker
compares ALL state using identical public retained data callbacks on each arm;
no Stage F waiver is applied to controller state. Preflight rejects reporting/
unsafe frame/manager cases before writes. 5,505 strict cases, eight child/
baseline combinations, child-entry traces, 256 byte models, nine rejects,
API routing and 304 configuration cases pass. All five V2 corpus arms pass
19,508 exact / 58,195 transfers, all controller roots native / zero fallback
(6,970/10,467/2,071 in modes 1/2/3). Strict live comparisons reach >= 455,000;
actual-native frames are normal. Alternate 69E8/6B18 DATA remains original;
bypass/idle/alternate real/live and final interpreter/reversed A/B/CPU/default
gates remain OPEN. See the [phase-3 results](native-geometry-phase3-results.md#complete-enclosing-transform-control-6608).
JOB=0 / leaf 827 / new flags=0 remain; strict Stage F stays last.

### Complete alternate cull data: cold opt-in

69E8/6B18/6B10 setup, every iteration and final 6958 handoff are native.
`BUZZ_GEOMETRY_JOB_ALTERNATE_CULL=0/1/2` defaults to zero; no Stage F waiver.
5,448 strict cases include 288 independent geometry/order models, 288 composed
original-controller/data checks, 256 special FP cases and four unchanged
rejects. API routing, 312 configuration cases and build pass. Five compatibility
arms each pass 19,508 exact / 58,195 transfers, but the new function has ZERO
real/native corpus calls. Actual-native Stage F checked menu passes >= 45,000
whole-job state/I/O comparisons; native menu/round frames are normal, with
ZERO alternate live calls. Its real/256-live gates remain OPEN. See the
[phase-3 results](native-geometry-phase3-results.md#complete-alternate-cull-data-69e86b186b10-cold-opt-in).
Ring/atomic integration is recorded below. Other implementations/cold/final gates keep
phase 3 active; JOB=0 / leaf 827 / new flags=0 and strict Stage F last remain.

### Ring allocator: production opt-in

`37C0` now has its own binary `BUZZ_GEOMETRY_JOB_RING` switch, default 0.
JOB=2 replays exact root I/O; JOB=1 executes natively without a strict credit.
Six frozen-EXE corpus runs each pass 19,508 exact / 58,195 transfers. Enabled
arms have 7,201 native returns, zero fallback/reporting handoffs; JOB=2 checks
all 7,201 roots and 19,508 whole jobs. All older enabled stage totals match.
Live actual-native Stage F menu passes >=35,000 strict ring and >=45,000 whole-
job comparisons; native menu/round frames are normal. Reporting handoffs and
enabled interrupts retain original code and cannot earn complete native credit.
See the [results](native-geometry-phase3-results.md#output-allocator-37c0-production-integration).
Final interpreter, reversed frozen-EXE A/B, normalized CPU/FPS and default
gates remain OPEN. Next is complete `54E8`; phase 3 and the goal stay active.

### Phase 4: image 40 (2-4 sessions)
Reuse host algorithms for the shared library in images 40/43/45/47, starting
with image 40's existing 17,714 real captures. Audit job inputs/publications
and shared semantic operations before connecting an image-specific frontend;
shifted entry addresses alone do not prove equivalent data or job contracts.
Decode once into host SoA, compute eight lanes and emit final outputs once.
No new per-function register-bank transliterations or cold coverage work.
Unsupported/uncovered jobs retain whole-job lift fallback. Each increment
uses the corrected corpus/live/frame/reversed FPS and CPU gates above.
Initial [phase-4 audit](native-geometry-phase4-results.md) covers all 17,714
image-40 captures and their 115,162 exact baseline transfers. Image 40 has
the animation job layout; its 48/64-byte published record streams need a
separate frontend. Fresh menu hot blocks and code-only shared candidates
are recorded; native integration and all candidate acceptance gates remain open.
The host AVX2 affine producer now matches all 461,853 captured matrices
(22,168,944 published bytes), with independent original-producer numeric and
guarded-tail checks. It uses captured world matrices until native clip/pose
production is connected; this is not whole-job native acceptance.
Host quaternion sampling now matches 596,095 real results. Replacing the
original quaternion producer in the whole-job oracle preserves all 17,714
jobs / 115,162 transfers and exit controls, with numeric, guarded-stream and
publication-fault checks. Production integration and live/performance gates
remain open; the native kernel does not recreate volatile registers or frames.
Static quaternion decoding now reuses that kernel and matches all 388,032
captured values. Replacing both quaternion producers in the original whole-job
oracle preserves every captured transfer and exit control; production
pose/world integration and live/performance acceptance still remain.
FLOAT3 sampling now shares host temporal key selection and matches all 24,548
captured vectors. Replacing its 76,050 calls together with both quaternion
producers preserves all 17,714 jobs / 115,162 transfers and exit controls;
independent guarded numeric and publication-fault checks pass. Uncalled key
formats remain unimplemented. Production integration and live/performance
gates remain open.
The host Q/T/S-to-world/reference matrix chain now matches all 461,853
matrices in both published streams without captured matrix inputs. Replacing
both matrix producers together with the key producers preserves every
captured job/transfer/exit; numeric/tail and four publication-fault probes
pass. Blend and hierarchy remain original (17,714 nonempty calls each),
followed by production frontend integration and the live/performance gates.

### Phase 5: render thread (optional, 3-6 sessions)
After SPU is no longer the limit: GPU-side vertex decode (raw guest
buffers + per-format shader fetch), cheaper FIFO method dispatch. Exit:
frame dumps match, render drain reduced, A/B recorded.

### Phase 6: portability (optional)
Every native kernel gets a scalar reference + NEON path next to AVX2,
same corpus gate, for ARM targets.

### Phase 7: cancelled

Cancelled by the Course correction (2026-11-01). Do not reconstruct Stage F's
dead registers or scratch. Stage F follows the same job-boundary contract as
the other native routines; per-function full-state checks are optional debug.

## Gates for every step

- [ ] Exact job outputs, ordered DMA/atomic/channel transcript and exit state across the full corpus
- [ ] Live whole-job I/O: >=10,000 jobs compared in-game, PASS logged
- [ ] Frame dump of menu and round renders normally
- [ ] Reversed same-EXE JOB0/JOB1 menu and round FPS plus separate worker/process cycles and CPU time per completed frame
- [ ] Frozen build-vulkan-* manifest records renderer; every log confirms mailbox and asynchronous two-context batches
- [ ] Runtime switch documented; default-on only after all gates and JOB1 >= JOB0 (menu FPS parity is the 3a target)
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
Phase-2 results were committed in `ef906be`. The supported phase-3b image-41
profile now passes and defaults on in qualified builds; phase 4 remains.
Phase 7 is cancelled. The Course correction governs the historical rows below.

| Revised work | Status | Latest evidence |
| --- | --- | --- |
| 3a: remove overhead | structural implementation checkpointed; isolated result rejected | Host layout V1: menu FPS -2.207%; aggregate menu parity is first demonstrated with the 3b output candidate (+0.964%), not by structural 3a alone; native menu-and-round acceptance stays open |
| Job-boundary checker | exact; optional full-state debug retained | Four full-corpus arms, 12 real-job fault fixtures, >=1,415,000 completed live comparisons; concurrent-input skips excluded; normal native menu |
| 3b: host SoA / AVX2 pipeline | output increment exact, experimental/off; full pipeline wip | Eight-vertex float/half/CMP output: four full-corpus arms, >=1,395,000 live checks, normal native menu/round; menu FPS +0.964%, round -0.077%; round worker cycles/frame +1.734% mixed pairs. Continue persistent host input, skin/transform/cull and indices; no cold coverage |

Measurements and decisions: [native-geometry-phase3a-results.md](native-geometry-phase3a-results.md).
Host-pipeline increments: [native-geometry-phase3b-results.md](native-geometry-phase3b-results.md).
Checker validation: [native-geometry-job-boundary-results.md](native-geometry-job-boundary-results.md).

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
| External-bank output cores/all7 | n/a | standalone exact; formatter integrated opt-in; fresh game gates OPEN | internal API; existing switches/defaults unchanged | 50,811 poisoned-GPR original-oracle cases +50,811 ABI comparisons; enclosing suites pass | no new executable/live claim | not measured | implementation in git log |
| Formatter shared host bank | n/a | exact opt-in; measured small/mixed gains; default0;cold/final OPEN | `BUZZ_GEOMETRY_JOB_FORMATTER_REGISTERS=0/1`,default0 | 1,776 CPU roots/4 corruption probes;10 bc0 arms19,508exact/58,195transfers;7,201host roots/17,278children;old ledgers unchanged | >=45,000whole-job/I/O+35,000parents+85,833children;separateUNORM>=10,000;normal native menu/round;I16/U8 real0 | Menu25.616->25.845;round31.725->31.778;workerCPF-0.341%/-0.010%,process mixed;keep0 | a5fbf3d |
| Formatter column admission proof | 3.57% active native pool leaf samples for parent reader,not exact time | validated opt-in;mean/CPU gains;tail repetition complete;parent/final OPEN | `BUZZ_GEOMETRY_JOB_FORMATTER_SPANS=0/1`,default0 | 50,000quad models+1,908strict formatter;6arms19,508exact/58,195transfers;unchanged ledgers | >=35,000strict formatter+45,000whole-job/I/O;normal native menu/actual question on b95e457f | Same3df menu25.00 ->25.64,worker CPF-2.99%/process-1.84%;round30.93 ->31.52,worker-4.01%/process-2.82%;round tails open | c8e8e22 |
| Whole-job driver | n/a | ABI48 opt-in; measured menu CPU/FPS regression; cold/round/final gates OPEN | `BUZZ_GEOMETRY_JOB=0/1/2`, default0 | Five arms19,508exact/58,195transfers;enabled19,508native/0whole-job fallback;strict synthetic/routing proofs in results | >=45,000strict whole-job/I/O on current freeze;normal native menu/actual question | Menu27.74 ->25.30;worker cycles/frame+15.92%;default-off | 272a375 |
| 0x2DC0 index publication | n/a | native opt-in; final measurements open | `BUZZ_GEOMETRY_JOB_PUBLISH=0/1`, default 0 | 7,201 native publications, zero fallback in exact 19,508-job replay; 2,048 strict synthetic cases | >=370,000 executions inside strict whole-job checks; normal native menu/round intro dumps | not measured | |
| 0x2B80 output sizing | n/a | native opt-in; shared host layout; final gates open | `BUZZ_GEOMETRY_JOB_LAYOUT=0/1`, default 0 | 7,201 native sizes, zero fallback; independent/combined 19,508-job replay exact; 2,048 strict synthetic cases | >=375,000 inside strict whole-job checks; normal native menu/round intro dumps | not measured | |
| 0x35F0 vertex publication | n/a | native opt-in; final gates open | `BUZZ_GEOMETRY_JOB_VERTEX=0/1`, default 0 | 7,201 native publications, zero fallback; 7,201 host-layout reuses in combined runs; 2,048 strict synthetic cases | >=375,000 inside strict whole-job checks and layout reuses; normal native menu/round intro dumps | not measured | |
| 0x32C8 command-buffer preparation | n/a | native opt-in; final gates open | `BUZZ_GEOMETRY_JOB_COMMANDS=0/1`, default 0 | 19,508 native preparations, zero fallback; 58,195 exact transfers; 4,096 strict synthetic cases | >=450,000 inside strict whole-job checks; normal native menu/round intro dumps | not measured | |
| 0x27A0 command relocation/publication | n/a | native opt-in; final measurements open | `BUZZ_GEOMETRY_JOB_COMMAND_PUBLISH=0/1`, default 0 | 19,508 native publications, zero fallback; 58,195 exact transfers in four corpus arms; 8,256 strict cases and 20 unchanged-state rejects | >=450,000 inside strict whole-job checks; native frame checks in phase-3 report | not measured | |
| 0x0100/0x0020 command attribute binding/reset | n/a | native opt-in; final measurements open | `BUZZ_GEOMETRY_JOB_EMIT=0/1`, default 0 | 7,201 native each, zero fallback; four 19,508-job/58,195-transfer arms exact; 8,192 strict cases +28 unchanged-state rejects | >=375,000 each inside strict whole-job checks; render proof in phase-3 report | not measured | |
| 0x02E8 position-only command emission | n/a | native opt-in; full emitter body and shared writers; final gates open | shares `BUZZ_GEOMETRY_JOB_EMIT`, default 0 | 2,123 native, zero fallback; four full-corpus arms exact; 8,320 strict state/channel cases +26 unchanged-state rejects | >=120,000 inside strict whole-job checks; render proof in phase-3 report | not measured | |
| 0x12F0 full multipass command emission | n/a | native opt-in; remaining phase gates open | `BUZZ_GEOMETRY_JOB_FULL_COMMANDS=0/1`, default 0 | 5,078 native emissions, zero fallback; four full-corpus arms exact; 8,512 strict state/channel/packet-size cases +32 unchanged-state rejects | >=250,000 inside strict whole-job checks; normal native menu/round intro dumps | not measured | |
| 0x09F8 skin/rigid dispatch | n/a | complete native opt-in control/ABI; final gates open | `BUZZ_GEOMETRY_JOB_SKIN_DISPATCH=0/1/2`, default 0 | Five arms19,508 exact/58,195 transfers;1,628 weighted+17,880 rigid native/0fallback;6,144 strict synthetic+8 rejects | >=300,000 strict rigid+135,000 strict weighted;normal native menu/actual round | not measured | |
| 0xB880/B934/B940 complete weighted data | n/a | complete native opt-in; original setup/final pair replaced; final gates open | `BUZZ_GEOMETRY_JOB_WEIGHTED_SKIN=0/1/2`, default 0 | Five arms19,508 exact/58,195 transfers;6,113native/0fallback incl30actual aliases;6,912strict states+12rejects | >=430,000 strict data+256strict actual aliases;normal native menu/round;native round>=40,000aliases | not measured | |
| 0xBD4C/BE20 basic skin direction data | n/a | complete native opt-in; final gates open | `BUZZ_GEOMETRY_JOB_SKIN_DIRECTIONS=0/1/2`, default 0 | Five arms19,508 exact/58,195 transfers;1,158+3,860native/0fallback;family6,912strict states+3numeric+12rejects | >=1,125,000strict one-stream+680,000strict two-stream;normal native menu/actual round | not measured | |
| 0xAF18/B078/B100/B4C0/B868 skin orchestration | n/a | native opt-in control/palette; cold/final gates open | `BUZZ_GEOMETRY_JOB_SKIN_FLOW=0/1/2`, default 0 | Five arms19,508exact/58,195transfers;1,628work-native/0fallback;6,600strict+3numeric+3early+18rejects | See phase-3 report; empty/cold data gates OPEN | not measured | |
| 0xB348 data suffix/B3C0/B3C8 rigid skin data | n/a | complete native implementation; real/live gates OPEN | `BUZZ_GEOMETRY_JOB_RIGID_SKIN=0/1/2`, default0 | 7,681strict+3numeric+10rejects;384AF18/data integration;five compatibility arms19,508exact/58,195transfers;zero real calls | Real/live >=256 gate OPEN; native scene compatibility in report | not measured | |
| 0xBB58/BB68 cofactor normal data | n/a | complete native implementation;real/live gates OPEN | `BUZZ_GEOMETRY_JOB_SKIN_COFACTOR=0/1/2`, default0 | 7,808strict+3geometric+10rejects;768AF18/data integration;five compatibility arms in report;zero real calls | Real/live >=256 gate OPEN; scene compatibility only | not measured | |
| 0xC14C/C280/B5A0 normalized direction data | n/a | complete native implementations;all real/live gates OPEN | `BUZZ_GEOMETRY_JOB_SKIN_NORMALIZED=0/1/2`, default0 | 9,219strict+9geometric/cache+12rejects;1,152AF18 actual-data integration;five compatibility arms in report;zero real calls | Real/live >=256 gate OPEN in each variant;scene compatibility only | not measured | |
| 0x74F0/7640/7C20/7CD0/77E8 bit unpack | n/a | complete native opt-in; final performance/default gates OPEN | `BUZZ_GEOMETRY_JOB_INDEX_UNPACK=0/1/2`, default 0 | 5,378 strict incl256 numeric +10 rejects; five arms: 19,508 exact / 58,195 transfers; 9,763 native/0 fallback | >=545,000 strict low +55,000 strict high; native scenes in report | not measured | |
| 0x7838/78A8 complete flag expansion | n/a | complete native opt-in; final performance/default gates OPEN | `BUZZ_GEOMETRY_JOB_INDEX_EXPAND=0/1/2`, default 0 | 4,691 strict including 64 numeric merges + 2 capacity reversals; seven rejects; five arms: 19,508 exact / 58,195 transfers; 19,508 native/0 fallback | >=130,000 strict short +330,000 strict bulk; native scenes in report | not measured | |
| 0x79A8/79C8 command relocation | n/a | complete native opt-in; final performance/default gates OPEN | `BUZZ_GEOMETRY_JOB_INDEX_RELOCATE=0/1/2`, default 0 | 5,777 strict including 1,152 byte models; eight rejects; five arms 19,508 exact / 58,195 transfers; all 19,508 native / 0 fallback | >=460,000 strict live; native scene frames normal | not measured | |
| 0x7A90 complete triangles/frame | n/a | complete native opt-in; final performance/default gates OPEN | `BUZZ_GEOMETRY_JOB_INDEX_TRIANGLES=0/1/2`, default 0 | 4,993 strict including 80 frame models + 32 mutable-LUT proofs; nine rejects; five arms 19,508 exact / 58,195 transfers; all native / 0 fallback | >=465,000 strict live; native scene frames normal | not measured | |
| AEA0/7390 complete index control/setup/publication | n/a | complete native opt-in; final gates OPEN | `BUZZ_GEOMETRY_JOB_INDEX_CONTROL=0/1/2`, default 0 | 5,633 strict; 32 child combinations; 256 byte models; 20 rejects; five arms 19,508 exact / 58,195 transfers; all native / 0 fallback | >=455,000 strict live; native scene frames normal | not measured | |
| 6958/66C8 transform finalization | n/a | complete native opt-in; enclosing/cold/final gates OPEN | `BUZZ_GEOMETRY_JOB_TRANSFORM_FINISH=0/1/2`, default 0 | 4,929 strict incl320 byte models; six rejects; five arms19,508 exact/58,195 transfers; all native/0fallback | >=465,000 strict publish; normal native frames; count-only real/live0 OPEN | not measured | |
| 6188/65EC/65FC vertex preparation | n/a | complete native opt-in; enclosing/cold/final gates OPEN | `BUZZ_GEOMETRY_JOB_VERTEX_PREPARE=0/1/2`, default 0 | 8,513 strict; every format/bias; 256 special FP;64 matrix models;six rejects;five exact arms/all native | >=470,000 strict live;normal native frames;five/other real/live0 OPEN | not measured | |
| 6608 complete enclosing transform control | n/a | complete native opt-in; alternate data/cold/final gates OPEN | `BUZZ_GEOMETRY_JOB_TRANSFORM_CONTROL=0/1/2`, default0 | 5,505 strict;8 combinations;256 byte models;9rejects;five V2 exact arms/allnative0fallback | >=455,000 strict live;normal frames;cold paths OPEN | not measured | |
| 69E8/6B18/6B10 alternate cull data | n/a | implemented; real/live/final gates OPEN | `BUZZ_GEOMETRY_JOB_ALTERNATE_CULL=0/1/2`, default0 | 5,448 strict;288 numeric+288 composed;256 special FP;4 rejects;five compatibility arms exact;new real0 | new live0 OPEN;>=45,000 whole-job checks;normal compatible frames | not measured | |
| 37C0 output ring allocation | n/a | production opt-in; cold/final gates OPEN | `BUZZ_GEOMETRY_JOB_RING=0/1`, default0; strict JOB2 | 5,821 strict +7 rejects; six arms exact;7,201 native/strict roots,0 fallback | >=35,000 strict ring /45,000 whole-job;normal native menu/round | not measured | kernel `206eb5f`; integration in git log |
| A6A8/A750/A860/A864/A868 builtin input format2 | n/a | complete standalone decoder; production/cold/live/final gates OPEN | no production switch/routing yet | 5,678strict;256independent byte models;8,192CMP words/32,768float components;262,144signed16 components;1,024arbitrary controls;256special-FP;3,216full original93F0/3,168strict child roots;48late-frame aliases;14unchanged rejects;2input+7conversion audit tests | real/live0;no executable change | not measured | implementation in git log |
| A390/A5D0/A5D8 builtin input format3 | n/a | complete standalone decoder; production/cold/live/final gates OPEN | no production switch/routing yet | 5,710strict;256independent byte models;8,192CMP words/32,768float components;1,024arbitrary controls;256special-FP;3,216full original93F0/3,168strict child roots;48late-frame aliases;17unchanged rejects;2input+7conversion audit tests | real/live0;no executable change | not measured | implementation in git log |
| A8F8/AAF8/AB00 builtin input format1 | n/a | complete standalone decoder; production/cold/live/final gates OPEN | no production switch/routing yet | 5,646strict;256independent byte models;8,192CMP words/32,768float components;1,024arbitrary controls;256special-FP;3,216full original93F0/3,168strict child roots;48late-frame aliases;14unchanged rejects;2input+7conversion audit tests | real/live0;no executable change | not measured | implementation in git log |
| 54E8 / 4178 output conversion | n/a | native all7-path control/formatter/all7 storage/all4 output built-ins opt-in; cold/final OPEN | separate control/formatter/encoder flags, default0 | original73-block graph;strict native whole descriptor roots and data as below | cold/final gates OPEN | not measured | audit and implementations in git log |
| 5760/5970/5978 built-in output format2 | n/a | complete payload +native54E8 integration; cold/live/final gates OPEN | BUILTIN24_OUTPUT=0/1/2,default0;stage53 | 5,663strict;256independent byte models;32,772independent CMP encodings;131,088full-SNORM32 components;1,024arbitrary controls;256special-FP;828full original-controller/native-child;48late-frame aliases;12unchanged rejects;7audit tests | standalone576native/384strict +parent768native/384parent-strict/384child-strict routing;5exact compatibility arms/0real new roots;live/render OPEN | not measured | kernel in git log |
| 5A30/5D18/5D20 built-in output format3 | n/a | complete payload +native54E8 integration; cold/live/final gates OPEN | BUILTIN24_CMP_OUTPUT=0/1/2,default0;stage54 | 5,663strict;256independent byte models;32,772independent CMP encodings;1,024arbitrary controls;256special-FP;828full original-controller/native-child;48late-frame aliases;12unchanged rejects;7audit tests | standalone576native/384strict +parent768native/384parent-strict/384child-strict routing;5exact compatibility arms/0real new roots;live/render OPEN | not measured | kernel in git log |
| 5E28/6050/6058 built-in output format1 | n/a | complete payload +native54E8 integration; cold/live/final gates OPEN | BUILTIN20_OUTPUT=0/1/2,default0;stage52 | 5,663strict;256independent byte models;32,772independent CMP encodings;1,024arbitrary controls;256special-FP;828full original-controller/native-child;48late-frame aliases;12unchanged rejects;7audit tests | standalone576native/384strict +parent768native/384parent-strict/384child-strict routing;5exact compatibility arms/0real new roots;live/render OPEN | not measured | kernel in git log |
| 56C8/5718 built-in output format0 | n/a | complete payload +native54E8 integration; cold/live/final gates OPEN | BUILTIN_FLOAT3_OUTPUT=0/1/2,default0;stage51 | 6,178 strict;5,154 independent byte models;1,024 arbitrary masks;1,024 special-FP-bit;828 full original-controller/native-child cases;48 late-frame aliases;8 unchanged rejects;7 audit tests | standalone576native/384strict +parent768native/384parent-strict/384child-strict routing;5exact compatibility arms/0real new roots;live/render OPEN | not measured | kernel in git log |
| 54E8 builtin-path outer control | n/a | complete opt-in; cold/live/final gates OPEN | CONVERSION_CONTROL +used builtin0/1/2 switch, default0 | 15,988strict full controller/data;4,472unchanged rejects incl4,424padded metadata collisions;3,072native/1,536parent-strict/1,536child-strict routing;5exact full-corpus arms;ABI47/55stages | builtin real/live0 OPEN;descriptor coverage separate | not measured | implementation in git log |
| 54E8 descriptor-path outer control | n/a | complete opt-in; final gates OPEN | `BUZZ_GEOMETRY_JOB_CONVERSION_CONTROL=0/1/2`, default0 | 1,853 full native cases;4,096 stride;4,195 control;90 routing/60 strict;26 unchanged owned rejects;five exact arms19,508jobs/58,195transfers;7,201strict/0fallback | >=25,000 strict control/>=30,000 whole-job;normal native menu/round;existing/empty and cold coverage separate | not measured | implementation in git log |
| 46D0 complete float32 output storage | n/a | complete opt-in; final gates OPEN | `BUZZ_GEOMETRY_JOB_FLOAT_OUTPUT=0/1/2`, default0 | V2:6,177 strict;1,792 alias/wrap;256 byte models;9 rejects;five exact arms,7,201 native/0 fallback | >=256 single +30,000 mixed strict live;>=40,000 whole-job;normal native menu/round;skip/all-double cold OPEN | not measured | implementation in git log |
| 4880 complete binary16 output storage | n/a | complete opt-in; final gates OPEN | `BUZZ_GEOMETRY_JOB_HALF_OUTPUT=0/1/2`, default0 | 7,215 strict;1,024 mixed/alias;1,048,576 scalar inputs;9 rejects;five exact arms,4,513 native/0 fallback | >=25,000 single+256 mixed strict live;>=40,000 whole-job;normal native menu/round;skip/all-double cold OPEN | not measured | implementation in git log |
| 4FDC complete packed CMP output storage | n/a | complete opt-in; final gates OPEN | `BUZZ_GEOMETRY_JOB_CMP_OUTPUT=0/1/2`, default0 | 7,471 strict;1,024 mixed/alias;32,772 independent encodings;256 special-FP;9 rejects;five exact arms,4,424 native/0 fallback | >=20,000 single+180,000 mixed strict live;>=175,000 whole-job;normal native menu/round;skip/all-double cold OPEN | not measured | implementation in git log |
| 4478 complete SNORM16 output storage | n/a | complete opt-in; final gates OPEN | `BUZZ_GEOMETRY_JOB_SNORM_OUTPUT=0/1/2`, default0 | 7,471 strict;1,024 mixed/alias;131,088 independent components;256 special-FP;9 rejects;five exact arms,1,104 mixed native/0 fallback | >=256 mixed strict live;>=35,000 whole-job;normal native menu/round;skip/single/all-double cold OPEN | not measured | implementation in git log |
| 4B9C complete UNORM8 output storage | n/a | complete opt-in; final gates OPEN | `BUZZ_GEOMETRY_JOB_UNORM_OUTPUT=0/1/2`, default0 | 7,471 strict;1,024 mixed/alias;131,088 independent components;256 special-FP;9 rejects;five exact arms,36 mixed native/0 fallback | >=10,000 mixed strict live;>=35,000 whole-job;normal native menu/round;skip/single/all-double cold OPEN | not measured | implementation in git log |
| 4DD0 complete I16 output storage | n/a | complete implementation; real/live/final gates OPEN | `BUZZ_GEOMETRY_JOB_I16_OUTPUT=0/1/2`, default0 | 7,503 strict;1,048,576 independent FP-bit components;576 synthetic enclosing strict roots/192 mixed;9 rejects;five compatibility arms19,508exact/58,195transfers;new real0 | new live0 OPEN;>=35,000 whole-job;normal compatibility native menu/round | not measured | implementation in git log |
| 52B4 complete U8 integer output storage | n/a | complete implementation; real/live/final gates OPEN | `BUZZ_GEOMETRY_JOB_U8_INTEGER_OUTPUT=0/1/2`, default0 | 7,503 strict;1,048,576 independent FP-bit components;576 synthetic enclosing strict roots/144 mixed;9 rejects;five compatibility arms19,508exact/58,195transfers;new real0 | new live0 OPEN;>=35,000 whole-job;normal compatibility native menu/round | not measured | implementation in git log |
| 4178 shared output formatter control | n/a | complete opt-in; final gates OPEN | `BUZZ_GEOMETRY_JOB_FORMATTER=0/1/2`, default0 | 1,908 standalone strict;117 routing/78 strict;28 unchanged rejects;five arms19,508exact/58,195transfers;7,201 strict real roots/0fallback | >=30,000 strict formatter/>=35,000 whole-job;normal actual-native menu/round;empty/skipped/mixed and cold coverage separate | not measured | implementation in git log |
| Custom callback comparison boundaries | n/a | guarded; unaudited callback jobs skip paired replay | ABI37; existing driver switch | 53 runtime +320 configuration cases;3 full-corpus arms exact, all stage ledgers unchanged | callback real/live coverage OPEN | not measured | guard in git log |
| 0x7730/7770 index delta reconstruction | n/a | complete native opt-in; final performance/default gates OPEN | `BUZZ_GEOMETRY_JOB_INDEX_DELTAS=0/1/2`, default0 | 5,763strict+3numeric+8rejects;five arms19,508exact/58,195transfers;9,763native/0fallback | >=235,000strict one-group+315,000strict bulk;native scenes in report | not measured | |
| 0xBF7C basic three-stream data | n/a | native implementation; real/live gates OPEN | same switch, default 0 | strict synthetic/routing only;zero real calls | zero observed;OPEN | not measured | |

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
| 0x6DB8 loop | | exact | Historical local-register diagnostic, `BUZZ_IMAGE40_LOCALS=0` in the qualified profile; not the phase-4 host pipeline |
| (fill in from phase 0 ranking) | | | |

### Phase 7: cancelled

| Item | Status | Exit evidence |
|---|---|---|
| Strict Stage F register/scratch reconstruction | dropped | Cancelled by Course correction (2026-11-01); use the job-boundary gates above |

### Measurements log

| Date | Change | Scene | FPS before -> after | Median / p90 ms | Notes |
|---|---|---|---|---|---|
| 2026-11-02 | Eight-vertex host SoA float/half/CMP output; whole-job descriptor fallback | Same-EXE menu and round JOB0/1/1/0, separate FPS/CPU | Menu 28.181 -> 28.453 (+0.964%); round 33.142 -> 33.116 (-0.077%) | see 3b results | Worker cycles/frame -1.879% menu / +1.734% round (mixed); four exact corpus arms; >=1,395,000 live checks; native frames normal; original CPU round rejected for scene mismatch, full fixed-ready repeat retained; experimental/defaults off; full 3b remains open |
| 2026-11-02 | Host metadata layout, bank-free formatter/conversion/float admission | Same-EXE menu JOB0/1/1/0, separate FPS/CPU | 28.535 -> 27.905 (-2.207%) | see 3a results | Worker cycles/frame +4.035%, process +2.567%; four exact full-corpus arms; >=1,455,000 live checks; menu normal; rejected/defaults off; next 3b with performance target open |
| 2026-11-02 | Phase 3a V3, parent command admission reuse | Same-EXE menu JOB0/1/1/0, separate FPS/CPU series | 28.456 -> 27.664 (-2.784%) | see results document | Worker cycles/frame +4.418%, process +2.454%; four exact full-corpus arms; >=50,000 live checks; native menu normal; rejected/defaults off; round gates not attempted |
| 2026-11-01 | Shared-bank formatter full isolated matrix | Samebc0;16runs actualJOB1,REG0/1/1/0;FPS andCPU separate | Menu25.616->25.845;round31.725->31.778 | mixed p90/p99;all raw retained | FPS+0.894%(bothpairs)/+0.169%(mixed);workerCPF-0.341%/-0.010%;workerCPUms menu flat/round-1.148%(bothpairs);process cycles/time mixed;8normal roundimages;77pins/305sources unchanged;REGISTERS0 retained;host/interpreter/cold/aggregate/final OPEN |
| 2026-11-01 | Shared-bank formatter frozen corpus/live/render | Newbc0 EXE/dc3 MAP;functional only | not measured | not measured | 10arms each19,508exact/58,195transfers;enabled19,508native/0whole fallback;host7,201roots/17,278children;independent strict knobs +old ledgers exact;live45,000whole-job/35,000parents/85,833children +UNORM>=10,000;normal native menu+actual round;15 reporter tests+old report exact;FPS/CPU/interpreter/cold/final OPEN |
| 2026-11-01 | Shared-bank formatter production integration | CPU correctness/routing only | not measured | not measured | REGISTERS0/1 default0;1,776 original-state roots incl888 shared-bank/360 strict parents/822 host children/318 strict children;all7 crossing paths,48 frame-alias roots,20 rejects,4 child-corruption probes;488 runtime option cases;fresh game/full host pipeline gates OPEN |
| 2026-11-01 | External-bank cores/all7 output encoders | CPU correctness suites only | not measured | not measured | 50,811 poisoned-physical-GPR strict original-oracle cases plus50,811 existing ABI comparisons;formatter/conversion/routing pass;no production fusion/new-game acceptance or performance claim;next formatter integration;phase3 OPEN |
| 2026-11-01 | Complete 4880 binary16 output data | Menu/actual round (functional) | not measured | not measured | 7,215 strict/9 rejects;1,048,576 scalar inputs;five arms19,508exact/58,195transfers;4,513native/0fallback;>=25,000single+256mixed strict live;native frames normal;own0/1/2 default0;final performance/default gates OPEN |
| 2026-11-01 | Complete 4FDC CMP output data | Menu/actual round (functional) | not measured | not measured | 7,471 strict/9 rejects;32,772 independent encodings;256 special-FP;five arms19,508exact/58,195transfers;4,424native/0fallback;>=20,000single+180,000mixed strict live;native frames normal;own0/1/2 default0;final performance/default gates OPEN |
| 2026-11-01 | Complete 4478 SNORM16 output data | Menu/actual round (functional) | not measured | not measured | 7,471 strict/9 rejects;131,088 independent components;256 special-FP;five arms19,508exact/58,195transfers;1,104mixed-native/0fallback;>=256mixed strict live;native frames normal;own0/1/2 default0;final performance/default gates OPEN |
| 2026-11-01 | Complete 4B9C UNORM8 output data | Menu/actual round (functional) | not measured | not measured | 7,471 strict/9 rejects;131,088 independent components;256 special-FP;five arms19,508exact/58,195transfers;36mixed-native/0fallback;>=10,000mixed strict live;native frames normal;own0/1/2 default0;final performance/default gates OPEN |
| 2026-11-01 | Complete I16/U8 integer output implementations | Menu/actual round compatibility | not measured | not measured | Each7,503strict/1,048,576independent components;1,152synthetic enclosing cases/576native-strict each;five compatibility arms19,508exact/58,195transfers;bothnewreal/live0 OPEN;>=35,000whole-job;normal frames;two own0/1/2 flags default0;phase3open |
| 2026-11-01 | Formatter SPANS independent round FPS repetition +diagnostics | Same3df EXE;4FPSarms0/1/1/0;separate matched diagnostic profiles | prior25.00 ->25.64 | 31.04 ->31.73 | FPS+2.22%(pairs+2.61/+1.84),median/p90/p99bothlower;old54.80msp99retained/notreproduced;validatedopt-in/default0withparent;diag21132native/20601referencepoolleaves notcosts;nextsharedhostregisterformatter/fullhostdatastillOPEN |
| 2026-11-01 | Formatter SPANS same-EXE menu FPS+CPU | 3df8d057;actualJOB1 allarms;SPANS0/1/1/0;4FPS+4separateCPU;40..98s | 25.00 ->25.64 | priorround30.93 ->31.52 | MenuFPS+2.56%(pairs+1.07/+4.07),median/p90/p99bothlower;worker cycles/frame-2.99%(-2.94/-3.05),process-1.84%(-1.91/-1.78);CPUms-1.16%/-1.19%bothpairs lower;all4scene/metric series sameEXE;round tail/default/final OPEN |
| 2026-11-01 | Formatter SPANS isolated round CPU | Same3df8d057;actualJOB1 all4 arms;SPANS0/1/1/0;200..270s;separate fromFPS | new-EXE menu pending | CPU series contextual | Worker cycles/frame-4.01%(pairs-3.28/-4.74),process-2.82%(-2.48/-3.17);CPUms-4.45%/-3.38%,bothpairs lower;4normal native scenes;8runclockalignment corroborated;newMenu/FPS-tail/default gates OPEN |
| 2026-11-01 | Formatter SPANS isolated round FPS | New3df8d057;actualJOB1 all4 arms;SPANS0/1/1/0;200..270s;4normal native scenes | new-EXE menu pending | 30.93 ->31.52 | FPS+1.91%(pairs+2.87/+0.96);mixedtails nativep99=43.58/54.80vscontrols44.18/44.06ms;oneGPUcapture190s/continuousreadback0;CPU,newMenu,tail/default gates OPEN |
| 2026-11-01 | One-shot pre-window scene proof repair | New3df8d057 freeze;rejected black roundV1;13GPU+13report tests;normal native menu/actual round | not measured | not measured | 6arms19,508exact/58,195transfers;unchanged ledgers/SPANS0vs1;>=45,000strict whole-job/I/O+35,000formatter;one190sGPU capture/readback0 andlosslessRGB;new-EXE reversed timings/default gates OPEN |
| 2026-11-01 | Formatter SPANS isolated menu +native frames | JOB1 in all arms;SPANS0/1/1/0;4FPS+4separateCPU runs;40..98s | 25.01 ->25.73 | pending | FPS+2.88%(pairs+3.19/+2.58);worker cycles/frame-3.17%(-3.42/-2.91),process-2.07%(-2.36/-1.77);CPUms-4.25%/-2.40%;normal menu+actual-question readbacks;round/final/default pending;no aggregate JOB0 claim |
| 2026-11-01 | Formatter admission span proof | Separate native/reference diagnostic profiles;50,000quad models+strict suites+6corpus arms+checked menu | not measured | not measured | New0/1 default0;3.57%active pool samples identify reader;conservative signed/modular span proof,exact scan retained;all19,508native/0whole-job fallback;live>=35,000strict formatter/45,000whole jobs;frames/reversedAB/CPU/default OPEN |
| 2026-11-01 | ABI48 whole-job reversed menu FPS + separate CPU | Four FPS +four CPU runs,40..98s,same EXE/save | 27.74 ->25.30 | pending | FPS-8.78%(pairs-8.63/-8.92);pool cycles/frame+15.92%(+16.55/+15.29);process+9.19%(+9.81/+8.57);CPUms pool+16.02%/process+9.13%;combined default-off;profile next;round/cold/interpreter gates OPEN |
| 2026-11-01 | ABI48 interpreter diagnostics | Fresh JOB0/JOB1 menu+actual round;mask827/StageF1 | not measured | not measured | Menu15/60s absolute0 in both;round240s observed300,699,624/300,952,932,allimage2/same5pages;whole-run entries2:07B4C/A6C8+40:02A18 match;positive legacy stats non-atomic;roundcount/reversedAB/CPU/default OPEN;8observer tests |
| 2026-11-01 | ABI48 input routing live/render | Checked menu80s/native menu75s/visually verified solo round | not measured | not measured | >=45,000strict whole-job/I/O +45,000strict raw0 input returns;normal1280x720 menu+actual-question readbacks;native raw0 >=2,750,000menu/11,415,000round inclnavigation;no mismatch/first-fallback messages;periodic lower bounds;inputs1..5real/live0;interpreter/reversedAB/normalizedCPU/default OPEN |
| 2026-11-01 | Standard93F0 and5 builtin input production wrappers | CPU routing/parent/layout/getter tests +5 frozen corpus arms | not measured | not measured | ABI48/61stages;3,876production/1,938parent strict/1,440child strict;576parent roots/292layouts/877rejects;472getters;each19,508exact/58,195transfers/zero whole-job fallback;raw0controller19,508real;new1..5real/live0;parent scratch/frame guards;initial5,838coverage regression corrected;live/final/default OPEN |
| 2026-11-01 | Complete standard93F0 standalone control+native data | CPU root/prefix/frame-model tests | not measured | not measured | 23-block original oracle/all6 formats;8,391strict roots/7,796exact dispatch boundaries/8,391independent frame models;23unchanged rejects;40quad frame/metadata forecast+all5data regressions;production parent-safety/cold/final OPEN;app unchanged |
| 2026-11-01 | Builtin input5 standalone fixed12/three-unit decoder | CPU function/model/original-control tests | not measured | not measured | All2blocks+four publications/delayed pipeline/table-read timing/final9498ABI;5,838strict/288byte models/3,145,728position components/3,216full93F0;20unchanged rejects;shared helper extraction+full input4 regression;production/cold/final OPEN;app unchanged |
| 2026-11-01 | Builtin input4 standalone fixed12/packed-unit decoder | CPU function/model/original-control tests | not measured | not measured | All2blocks+delayed pipeline/table-read timing/final9498ABI;5,806strict/288ordered byte models/3,145,728independent position components/3,216full93F0;16unchanged rejects;production/cold/final OPEN;app unchanged |
| 2026-11-01 | Builtin inputs2/3 standalone 24-byte decoders | CPU function/model/original-control tests | not measured | not measured | All5/3blocks+ordered metadata/read-lookahead/final9498ABI;5,678/5,710strict;each256byte models/32,768CMP;input2 262,144signed16;each3,216full93F0;14/17unchanged rejects;split IDs read/store fix+input1 regression;production/cold/final OPEN;app unchanged |
| 2026-11-01 | Builtin input1 standalone XYZ+2CMP decoder | CPU function/model/original-control tests | not measured | not measured | All3blocks+metadata publication/pipeline/final9498ABI;5,646strict/256byte models/32,768numeric/3,216full93F0;14rejects;23block input inventory;production/corpus/live/render/performance OPEN;app unchanged |
| 2026-11-01 | Native54E8 all4 builtin paths | Strict integration/routing +5 frozen corpus arms | not measured | not measured | 15,988strict controller/data+4,472unchanged rejects;3,072native/1,536parent-strict/1,536child-strict routing;scalar padded footprint protects control metadata before mutation;ABI47/55stages;each19,508exact/58,195transfers;7,201real descriptor native/0fallback;builtin real/live0 OPEN;default0 |
| 2026-11-01 | All4 built-in output production wrappers | Synthetic routing +5 frozen compatibility arms | not measured | not measured | Each576native/384strict routing;12unchanged owned rejects;424config/7audit;ABI46/55stages;eachreplay19,508exact/58,195transfers;all4real new roots0;native54E8 parent/cold/live/final OPEN;default0 |
| 2026-11-01 | Built-in outputs2/3 standalone 24-byte kernels | CPU function/model tests | not measured | not measured | All3blocks each;raw XYZ+CMP+SNORM or3CMP;ordered stores/post-store prefetch/5648 ABI;each5,663strict/256byte models/32,772CMP/828full original-controller;format2 131,088full-SNORM32;12rejects each;production/corpus/live/render/performance OPEN;default app unchanged |
| 2026-11-01 | Built-in output1 standalone 20-byte kernel | CPU function/model tests | not measured | not measured | All3blocks;raw XYZ+2CMP;ordered padded stores/post-store prefetch/5648 ABI;5,663strict/256byte models/32,772numeric/828full original-controller;12rejects;production/corpus/live/render/performance OPEN;default app unchanged |
| 2026-11-01 | Built-in output0 standalone raw XYZ kernel | CPU function/model tests | not measured | not measured | All2blocks/final prefetch/ordered padded stores/5648 ABI;6,178strict/5,154byte models/828full original-controller cases;8rejects;production/corpus/live/render/performance OPEN;default app unchanged |
| 2026-11-01 | Native 54E8 descriptor conversion control | Menu/actual round (functional) | not measured | not measured | Own0/1/2 default0;ABI45/51stages;1,853full native +4,096stride +4,195control;90routing/60strict;five exact arms19,508jobs/58,195transfers;7,201strict/0fallback;>=25,000strict live;normal native frames;final gates OPEN |
| 2026-11-01 | Shared formatter4178 production control | Menu/actual round (functional) | not measured | not measured | Own0/1/2 default0;ABI44/50stages;117routing/78strict;28unchangedrejects;five exact arms19,508jobs/58,195transfers;7,201strict real/0fallback;>=30,000strict live;normal native frames;final interpreter/reversedAB/normalizedCPU gates OPEN |
| 2026-11-01 | Shared formatter4178 standalone control | CPU function/model tests | not measured | not measured | 1,908 strict/2,643 child entries/2,028 native data calls/all7;96 frame models;24 return-PC collisions;15 rejects;7 audit tests;production/corpus/live/render/performance gates OPEN;defaults unchanged |
| 2026-11-01 | Complete 46D0 float32 output data | Menu/actual round (functional) | not measured | not measured | V2:6,177 strict/9 rejects;256 byte models;five arms19,508exact/58,195transfers;7,201native/0fallback;>=256single+30,000mixed strict live;native frames normal;own0/1/2 default0;allIRQ contexts retained;final performance/default gates OPEN |
| 2026-11-01 | Complete alternate cull data | Menu/actual round (compatibility) | not measured | not measured | 5,448 strict;288 numeric+288 composed;256 special FP;312 configs;five exact compatibility arms;new real/live0 OPEN;>=45,000 strict whole-job native-StageF live;own0/1/2 default0;phase3open |
| 2026-11-01 | Complete native enclosing 6608 control | Menu/actual round (functional) | not measured | not measured | 5,505 strict;8 child/baseline combinations;256 byte models;9 rejects;304 configs;five V2 arms19,508exact/58,195transfers;allnative/0fallback;>=455,000strict live;own0/1/2 default0;alternate data/cold/final gates OPEN |
| 2026-11-01 | Complete native 6188 vertex preparation | Menu/actual round (functional) | not measured | not measured | 8,513 strict;all256formatsx16biases;256specialFP;64matrix models;six rejects;296config;five arms19,508exact/58,195transfers;allnative/0fallback;>=470,000strict live;own0/1/2 default0;enclosing/cold/final gates OPEN |
| 2026-11-01 | Native 6958/66C8 transform publication/frame return | Menu/actual round (functional) | not measured | not measured | 4,929 strict incl320 byte models; six rejects; 288 config cases; five arms19,508 exact/58,195 transfers; all native/0fallback; >=465,000 strict live; own0/1/2 default0; enclosing/cold/final gates OPEN |
| 2026-11-01 | Complete AEA0/7390 index control/setup/publication | Menu/actual round (functional) | not measured | not measured | 5,633 strict; 32 child combinations; 256 byte models; 20 rejects; 280 config cases; each replay 19,508 exact / 58,195 transfers, all native / 0 fallback; >=455,000 strict live; own 0/1/2 default 0; final gates OPEN |
| 2026-11-01 | Complete 7A90 triangles/frame | Menu/actual round (functional) | not measured | not measured | 4,993 strict including 80 frame models + 32 mutable-LUT proofs; nine rejects; 272 config cases; each replay 19,508 exact / 58,195 transfers, all native / 0 fallback; >=465,000 strict live; own 0/1/2 default 0; final gates OPEN |
| 2026-10-31 | Complete 79A8/79C8 command relocation | Menu/actual round (functional) | not measured | not measured | 5,777 strict including 1,152 byte models; eight rejects; 264 config cases; each replay 19,508 exact / 58,195 transfers, all native / 0 fallback; >=460,000 strict live; own 0/1/2 default 0; final gates OPEN |
| 2026-10-31 | Complete 7838/78A8 flag expansion | Menu/actual round (functional) | not measured | not measured | 4,691 strict including 64 scalar merges + 2 capacity reversals; seven rejects; 256 configuration cases; each replay: 19,508 exact / 58,195 transfers, all 19,508 native / 0 fallback; >=130,000 strict short +330,000 strict bulk live; own 0/1/2, default 0; final gates OPEN |
| 2026-10-31 | Complete 74F0/7640/7C20/7CD0/77E8 bit unpack | Menu/actual round (functional) | not measured | not measured | 5,378 strict incl256 independent numeric cases +10 rejects; 248 configuration cases; 9,763 native/0 fallback, each replay19,508 exact/58,195 transfers; >=545,000 strict low +55,000 strict high live; own 0/1/2 default0; width16 source behavior documented; final gates OPEN |
| 2026-10-31 | Phase 3 complete 7730/7770 index delta reconstruction | Menu/actual round (functional) | not measured | not measured | 5,763 strict +3 numeric +8 rejects; 240 configuration cases; 9,763 native/0 fallback per enabled replay, each 19,508 exact/58,195 transfers; >=235,000 strict one-group +315,000 strict bulk live; normal native frames; own 0/1/2 default 0; remaining index stages and final gates OPEN |
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
| 2026-10-31 | Phase 3 complete unit-vector decoder 9114/9148/92A0 | Four frozen corpus arms and same-EXE live/menu/round | functional evidence; no FPS/CPU claim | normal menu/round frames | 8,256 strict unit cases +23 rejects; 12,067 native regions /0 fallback; each replay19,508 exact /58,195 transfers; live>=350,000 strict unit +525,000 whole-job/I/O; own switch default0, leaf827 unchanged; phase 3 remains open |
| 2026-10-31 | Phase 3 complete halfword stream 8378/CAC0/CAD8/CB78 | Four frozen corpus arms and same-EXE live/menu/round | functional evidence; no FPS/CPU claim | normal menu/crowd/round frames | 8,192 strict cases covering all65,536half encodings +23 rejects; 12,431 native regions /0 fallback; each replay19,508 exact /58,195 transfers; live>=280,000 strict half +425,000 whole-job/I/O; independent default0, leaf827 unchanged; phase 3 remains open |
| 2026-10-31 | Phase 3 complete float32 stream 87CC/8800/8878/8910/8928 | Five frozen corpus arms and scene-specific same-EXE live checks | functional evidence; no FPS/CPU claim | normal native menu and actual round | 5,440 strict cases +25 rejects; all12corpus float calls native/0fallback; each replay19,508 exact/58,195 transfers; >=20,000 strict live float32 in round sequence and 70,000 whole-job/I/O in menu; final V3 parser +104 configuration checks; own0/1/2switch default0, leaf827 unchanged; phase3open |

| 2026-10-31 | Phase 3 fixed-point implementation 9058/C810/C830/C910 | Five frozen corpus arms, strict synthetic and scene checks | functional only; no FPS/CPU claim | normal native menu and question/round | 9,216 strict +33 rejects; all3corpus fixed calls native/0fallback; each replay19,508 exact/58,195 transfers; >=5,000 strict fixed live calls in round sequence; >=115,000 whole-job/I/O menu checks; own 0/1/2, default 0;112 configuration cases; phase3open |

| 2026-10-31 | Phase 3 remaining typed implementations 1/5/6/7 | Strict synthetic/routing and frozen replay/scene checks | functional only; no FPS/CPU claim | normal native menu/round | 30,208 strict +108 rejects;144 configuration cases;5 replay arms each19,508 exact/58,195 transfers;0new corpus dispatches;SNORM16 live gate OPEN; I16 live gate OPEN; CMP live gate OPEN; U8 integer live gate OPEN; own0/1/2 switches default0;phase3open |

| 2026-10-31 | Phase 3 complete C750/C7A8 weighted scatter | Strict synthetic, real corpus and same-EXE live/render | functional only; no FPS/CPU claim | normal native menu/round | 6,153 strict +14 rejects;2,326 native roots/0fallback;5 replay arms each19,508exact/58,195transfers;>=940,000 strict live scatter;own 0/1/2, default 0;152configuration cases;phase3open |

| 2026-10-31 | Phase 3 native BF8 additional-stream orchestration | Strict synthetic, four frozen corpus configurations and live/render | functional only; no FPS/CPU claim | normal native menu and actual round | 2,672 strict +15 rejects;328 native roots/0fallback per enabled corpus arm;each19,508exact/58,195transfers;>=40,000 strict BF8 live roots;160 configuration cases;own0/1 default0;7118data dependency and final performance gates open |

| 2026-10-31 | Phase 3 complete 7118/7298 run-list decoder | Strict/numeric tests, five frozen corpus configurations and live/render | functional only; no FPS/CPU claim | normal native menu and actual round | 10,774 strict/numeric cases inclall65,536encodings +17rejects;1,198native/0fallback =895expanded+303empty;each19,508exact/58,195transfers;>=580,000strict nonemptylive+260,000empty;168configcases;own 0/1/2, default 0;phase3open |

| 2026-10-31 | Phase 3 native 93F0 custom-descriptor dispatch | Strict synthetic, five frozen corpus configurations and live/render | functional only; no FPS/CPU claim | normal native menu and actual round | 5,000strict state/handoff +13rejects;11,414native/0fallback per enabled arm;each19,508exact/58,195transfers;>=410,000strict live handoffs;176configcases;own 0/1/2, default 0;built-in1..5 notcovered;phase3open |

## Risks

- **Undocumented formats** (phase 1) are the hardest part; budget extra.
- **Corpus coverage**: rare descriptors (skinned characters, multi-player,
  scene loads) must be in the corpus before a kernel goes default-on.
- **FP corner cases**: SPU single precision differs from IEEE (no
  denormals, no NaN/Inf semantics, RTZ); the lift encodes this, the native
  code must too.
- **Lift changes** (new extra entries, lifter updates) move function
  boundaries: hash pins catch it; re-audit before regenerating.
| 2026-10-31 | Phase 3 native 9F8 skin/rigid controller | Strict synthetic, five frozen corpus arms and same-EXE live/render | functional only; no FPS/CPU claim | normal native menu and actual question/round | 6,144 strict state/call-trace+8 rejects;each19,508exact/58,195transfers;1,628weighted+17,880rigid native/0fallback;>=300,000strict rigid+135,000strict weighted live;184configcases;own 0/1/2, default 0;AF18/B880 data stillrequired;phase3open |
| 2026-10-31 | Phase 3 complete B880/B934/B940 weighted position/matrix data | Strict synthetic, five frozen V3 corpus arms and same-EXE live/render | functional only; no FPS/CPU claim | normal native menu and actual question/round | 6,912strict states+12rejects;each19,508exact/58,195transfers;all6,113native/0fallback incl30actual palette aliases;>=430,000strict data+256strict aliases live;native round>=7,515,000data+40,000aliases;192configcases;own 0/1/2, default 0;AF18/other data stillrequired;phase3open |
| 2026-10-31 | Phase 3 basic skin direction data BD4C/BE20/BF7C | Strict synthetic, five frozen corpus arms and same-EXE scene checks | functional only; no FPS/CPU claim | normal native menu, actual round and checked round | 6,912strict states incl640aliases+3numeric+12rejects;all5,018real roots native/0fallback;each19,508exact/58,195transfers;>=1,125,000strict one-stream+680,000strict two-stream live;200configcases;own 0/1/2, default 0;BF7C real/live0 OPEN;phase3open |

| 2026-10-31 | Phase3 native AF18 palette/frame/batch/dispatch/publication | Strict synthetic,five frozen corpus arms,same-EXE live/render | functional only;no FPS/CPU claim | normal native menu and actual round | 6,600strict+3numeric+3early+18rejects;208configcases;all1,628work-native/0fallback per enabled replay;each19,508exact/58,195transfers;>=425,000strict live work;own 0/1/2, default 0;retained/cold data and final gates OPEN |

| 2026-10-31 | Phase3 rigid skin data suffix/B3C0/B3C8 | Strict synthetic/integration,five compatibility replays,scene checks | functional only;no FPS/CPU claim | normal menu/actual round;new data uncalled | 7,681strict+3numeric+10rejects;384 AF18/data integration;216config+abrupt-exit fallback logging;each replay19,508exact/58,195transfers;zero real/observed live rigid calls;real/256-live gates OPEN;own 0/1/2, default 0 |

| 2026-10-31 | Phase3 BB58/BB68 cofactor normal data | Strict synthetic/integration,five compatibility replays,scene checks | functional only;no FPS/CPU claim | normal menu/actual round;new data uncalled | 7,808strict+3geometric+10rejects;768AF18/data integration;224config+fallback durability;each replay19,508exact/58,195transfers;zero real/observed live cofactor calls;real/256-live gates OPEN;own 0/1/2, default 0 |

| 2026-10-31 | Phase3 C14C/C280/B5A0 normalized skin directions | Strict synthetic/integration,five compatibility replays,scene checks | functional only;no FPS/CPU claim | normal menu/actual round;new data uncalled | 9,219strict+9geometric/cache+12rejects;1,152AF18 actual-data integration;232config+fallback durability;each replay19,508exact/58,195transfers;zero real/observed live calls in all3;real/256-live gates OPEN;own 0/1/2, default 0 |
