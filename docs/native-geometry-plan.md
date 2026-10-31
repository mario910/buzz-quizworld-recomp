# Native geometry jobs: plan and progress

Goal: replace the lifted SPU code of the studio's geometry jobs (image 41,
then image 40) with native C++ that is **bit-exact** against the lift, so
the SPU job chain stops bounding the frame on this PC and the port gets
within reach of weaker CPUs (Steam Deck, laptops, Switch 2-class ARM).

This is a living document. Update the tracker tables after every session.

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
retain the explicitly accepted baseline contract. This is the final roadmap
phase, not an expansion of the currently active phase 2 implementation goal.

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

Phase 2's separate results document is
[native-geometry-phase2-results.md](native-geometry-phase2-results.md).
While implementation or gates remain open it is explicitly an interim report.
At actual phase completion, finalize that report and commit the results before
closing the current goal and creating the next phase's goal. Do not advance
to phase 3 while phase 2 is incomplete. Stage F strict cleanup remains the
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
| 0x4AB0 | 2.44 % | gated; default off, CPU decision pending | `BUZZ_GEOMETRY_KERNELS` 2 | 19,508 exact; 18,776 strict groups, no fallback | >=500,000 exact | 31.73 -> 31.93 | 81dadfb |
| 0x7A90 | 1.89 % | corpus/live passed; render/A-B pending, default off | `BUZZ_GEOMETRY_KERNELS` 3 | 19,508 exact; 12,675 strict groups | >=256 exact | pending | 7d0f413 |
| 0x51E0 | 1.75 % | corpus/live passed; render/A-B pending, default off | `BUZZ_GEOMETRY_KERNELS` 4 | 19,508 exact; 2,665 strict groups | >=256 exact | pending | e18619a |
| 0xCB78 | 1.44 % | on | `BUZZ_GEOMETRY_KERNELS` 5 | 19,508 exact; 9,063 strict groups | >=200,000 exact | 31.73 -> 32.10 | eb29523 |
| 0x78A8 | 1.15 % | corpus/live passed; render/A-B pending, default off | `BUZZ_GEOMETRY_KERNELS` 6 | 19,508 exact; 12,385 strict groups | >=256 exact | pending | 4cddd99 |
| 0x9148 | 1.01 % | on | shares bit 0 with 0x92A0 | same coupled unit-vector decoder | same checked group | same measurement | a7ad7b2 |
| 0x97A8 | 0.78 % | on; repeated CPU/process benefit | `BUZZ_GEOMETRY_KERNELS` 8 | 19,508 exact; 18,968 strict groups | >=256 exact | 32.52 -> 32.62 | cc9c877 |
| 0x4750 | 0.76 % | on | `BUZZ_GEOMETRY_KERNELS` 9 | 19,508 exact; 4,975 strict groups | >=256 exact | 32.32 -> 32.73 | eee46de |
| 0x0988 | 0.75 % | gated; default off, CPU decision pending | `BUZZ_GEOMETRY_KERNELS` 10 | 19,508 exact; 19,508 strict groups, no fallback | >=300,000 exact | 31.73 -> 31.94 | 635e2f2 |
| 0x4808 | 0.73 % | on; coupled with 4750 | shares bit 9 | same raw-float3 output group | same checked group | same measurement | eee46de |
| 0x28F0 | 0.67 % | gated; mixed process/FPS results, off | `BUZZ_GEOMETRY_KERNELS` 12 | 19,508 exact; 19,068 strict groups | >=256 exact | 32.32 -> 32.44 | f23c3c7 |
| Whole-job driver | n/a | phase 3; not started | | | | | |

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
The default mask is 803 (0x323), adding raw float3 input/output after their
gates and CPU/FPS decisions; checked/native default replay both pass all
19,508 jobs. Encoder/pointer switches remain off; the remaining phase 2
implementations/gates are pending. See the separate
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
| 2026-10-31 | Native 4AB0 encoder | Menu 40-98 s | 31.73 -> 31.93 (+0.65%) | mean medians 33.72 -> 33.44 / p90 40.32 -> 39.77 | `p2-batch1-ab`, masks 1/5; pairs -0.45%, +1.75%; normal native dump; CPU decision pending |
| 2026-10-31 | Native 4AB0 encoder | Round 150-220 s | 39.69 -> 40.16 (+1.18%) | mean medians 25.61 -> 25.35 / p90 29.49 -> 28.95 | Same EXE; pairs +1.94%, +0.44%; normal native dump |
| 2026-10-31 | Native CAD8/CB78 decoder | Menu 40-98 s | 31.73 -> 32.10 (+1.18%) | mean medians 33.72 -> 33.18 / p90 40.32 -> 39.76 | Same batch, masks 1/33; pairs +0.07%, +2.31%; normal native dump |
| 2026-10-31 | Native CAD8/CB78 decoder | Round 150-220 s | 39.69 -> 40.75 (+2.66%) | mean medians 25.61 -> 25.01 / p90 29.49 -> 28.76 | Same EXE; pairs +4.36%, +1.02%; normal native dump; final default validation pending |
| 2026-10-31 | Native 0988 pointers | Menu 40-98 s | 31.73 -> 31.94 (+0.67%) | mean medians 33.72 -> 33.67 / p90 40.32 -> 39.84 | Same batch, masks 1/1025; pairs +0.25%, +1.09%; normal native dump |
| 2026-10-31 | Native 0988 pointers | Round 150-220 s | 39.69 -> 40.26 (+1.44%) | mean medians 25.61 -> 25.30 / p90 29.49 -> 29.12 | Same EXE; pairs +3.10%, -0.17%; normal native dump; CPU decision pending |
| 2026-10-31 | Skinning CPU cost | Menu 40-98 s | n/a; separate CPU series | n/a | `p2-cpu-menu2`; pool cycles/frame -0.95% (pairs -1.23%, -0.66%); process cycles -0.73% (pairs -0.91%, -0.54%); process CPU time -1.20%; supports default-on |
| 2026-10-31 | Encoder CPU cost | Menu 40-98 s | n/a | n/a | Same reversed series; pool cycles/frame +0.78%, process +1.15%; no repeatable savings; default-off |
| 2026-10-31 | Pointer CPU cost | Menu 40-98 s | n/a | n/a | Same reversed series; pool cycles/frame +0.67%, process +0.70%; no repeatable savings; default-off |
| 2026-10-31 | Native 97A8 raw float3 input | Menu 40-98 s | 32.52 -> 32.62 (+0.28%) | mean medians 32.85 -> 32.69 / p90 39.24 -> 39.13 | Fresh four-run menu correction replaces contaminated original set; pairs +0.30%, +0.26%; normal native images |
| 2026-10-31 | Native 97A8 raw float3 input | Round 150-220 s | 41.19 -> 41.29 (+0.23%) | mean medians 24.70 -> 24.56 / p90 28.49 -> 28.35 | Original valid batch2 rounds; pairs +0.09%, +0.37%; CPU/final decision pending |
| 2026-10-31 | Native 46FC/4750/4808 float3 output | Menu 40-98 s | 32.32 -> 32.73 (+1.25%) | mean medians 33.08 -> 32.62 / p90 39.24 -> 39.07 | Valid original batch2, masks 35/547; pairs +0.97%, +1.55%; normal native images |
| 2026-10-31 | Native 46FC/4750/4808 float3 output | Round 150-220 s | 41.19 -> 42.06 (+2.09%) | mean medians 24.70 -> 24.27 / p90 28.49 -> 28.12 | Pairs +2.85%, +1.33%; CPU/final decision pending |
| 2026-10-31 | Native 28F0 word fill | Menu 40-98 s | 32.32 -> 32.44 (+0.36%) | mean medians 33.08 -> 33.08 / p90 39.24 -> 39.24 | Valid original batch2, masks 35/4131; pairs -0.55%, +1.28%; normal native images |
| 2026-10-31 | Native 28F0 word fill | Round 150-220 s | 41.19 -> 40.91 (-0.69%) | mean medians 24.70 -> 24.69 / p90 28.49 -> 28.51 | Pairs -0.04%, -1.33%; CPU/final decision pending |

## Risks

- **Undocumented formats** (phase 1) are the hardest part; budget extra.
- **Corpus coverage**: rare descriptors (skinned characters, multi-player,
  scene loads) must be in the corpus before a kernel goes default-on.
- **FP corner cases**: SPU single precision differs from IEEE (no
  denormals, no NaN/Inf semantics, RTZ); the lift encodes this, the native
  code must too.
- **Lift changes** (new extra entries, lifter updates) move function
  boundaries: hash pins catch it; re-audit before regenerating.
