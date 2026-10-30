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
   comparisons. See [data map and harness](native-geometry-data.md).
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

## Gates for every step

- [ ] Corpus replay bit-exact (all captured jobs of the image)
- [ ] Per-function harness: 0 differences
- [ ] Live checked mode: N >= 256 calls compared in-game, PASS logged
- [ ] Frame dump of menu and round renders normally
- [ ] Menu A/B (2 pairs, reversed) and round A/B recorded below
- [ ] Runtime bit documented; default flipped only after all of the above
- [ ] SDK/main commits, docs updated

## Tracker

Status: `todo`, `wip`, `exact` (bit-exact, off by default), `on` (default
on), `dropped` (with reason).

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
| 0x92A0 | 4.60 % | todo | | | | | |
| 0xB940 | 2.48 % | todo | | | | | |
| 0x4AB0 | 2.44 % | todo | | | | | |
| 0x7A90 | 1.89 % | todo | | | | | |
| 0x51E0 | 1.75 % | todo | | | | | |
| 0xCB78 | 1.44 % | todo | | | | | |
| 0x78A8 | 1.15 % | todo | | | | | |
| 0x9148 | 1.01 % | todo | | | | | |
| 0x97A8 | 0.78 % | todo | | | | | |
| 0x4750 | 0.76 % | todo | | | | | |
| 0x0988 | 0.75 % | todo | | | | | |
| 0x4808 | 0.73 % | todo | | | | | |
| 0x28F0 | 0.67 % | todo | | | | | |
| Whole-job driver | n/a | todo | | | | | |

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

### Image 40 and shared routines

| Function | Lane time share | Status | Notes |
|---|---|---|---|
| 0x6DB8 loop | | on | `BUZZ_IMAGE40_LOCALS` |
| (fill in from phase 0 ranking) | | | |

### Measurements log

| Date | Change | Scene | FPS before -> after | Median / p90 ms | Notes |
|---|---|---|---|---|---|
| 2026-10-30 | Baseline (all session fixes) | Menu 40-98 s | 36.3 | 29.6 / 34.8 | `vk-scene-final2-vk` |
| 2026-10-30 | Baseline | Round 150-250 s | 44.2 | 22.6 / 27.3 | `vk-scene-final2-rvk` |
| 2026-10-30 | Phase 0 capture / harness / profile | Menu, solo, two-player, loads | n/a (no optimization) | n/a | 37,222 real jobs replay exactly; native Stage F established contracts reproduced; native defaults unchanged |
| 2026-10-30 | Phase 1 formats / LS / DMA map | Entire image 41 corpus | n/a (no optimization) | n/a | 19,508 format inventories and 58,195 DMA transfers checked; includes 19 ring wraps; no new kernel/default |

## Risks

- **Undocumented formats** (phase 1) are the hardest part; budget extra.
- **Corpus coverage**: rare descriptors (skinned characters, multi-player,
  scene loads) must be in the corpus before a kernel goes default-on.
- **FP corner cases**: SPU single precision differs from IEEE (no
  denormals, no NaN/Inf semantics, RTZ); the lift encodes this, the native
  code must too.
- **Lift changes** (new extra entries, lifter updates) move function
  boundaries: hash pins catch it; re-audit before regenerating.
