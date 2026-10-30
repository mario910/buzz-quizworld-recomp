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
the 2026-10-30 profile: `func_000092A0`, `4AB0`, `B940`, `51E0`, `7A90`,
`CB78`, `78A8`, `9148`, `28F0`). One per step, each behind its own bit.
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
| Corpus image 41 (>= 3000 jobs, 4 scenes) | todo | |
| Corpus image 40 (>= 1000 jobs) | todo | |
| Per-function harness | todo | |
| Hot-function ranking (image 41) | todo | |
| Call graph + LS region map | todo | |
| Harness validated on stage F kernels | todo | |

### Image 41 functions

| Function | Lane time share | Status | Bit | Corpus | Live check | Menu FPS before -> after | Commit |
|---|---|---|---|---|---|---|---|
| 0x63E0 loop (stage F) | ~2.4 % | on | `BUZZ_STAGEF_KERNELS` 0 | yes | yes | (existing) | |
| 0xC518 transform (stage F) | ~1.0 % | on | 1 | yes | yes | (existing) | |
| 0x2C00 normalize (stage F) | ~0.6 % | on | 2 | yes | yes | (existing) | |
| cull (stage F) | ~3.4 % | on | | yes | yes | (existing) | |
| 0x92A0 | ~3.4 % | todo | | | | | |
| 0x4AB0 | ~2.0 % | todo | | | | | |
| 0xB940 | ~1.7 % | todo | | | | | |
| 0x51E0 | ~1.5 % | todo | | | | | |
| 0x7A90 | ~1.4 % | todo | | | | | |
| 0xCB78 | ~1.0 % | todo | | | | | |
| 0x78A8 | ~0.9 % | todo | | | | | |
| 0x9148 | ~0.8 % | todo | | | | | |
| 0x28F0 | ~0.8 % | todo | | | | | |
| Whole-job driver | n/a | todo | | | | | |

Shares are of all pool-lane samples (lanes are ~60 % busy), from one
10 s RIP sample of the menu; phase 0 replaces them with a proper ranking.

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

## Risks

- **Undocumented formats** (phase 1) are the hardest part; budget extra.
- **Corpus coverage**: rare descriptors (skinned characters, multi-player,
  scene loads) must be in the corpus before a kernel goes default-on.
- **FP corner cases**: SPU single precision differs from IEEE (no
  denormals, no NaN/Inf semantics, RTZ); the lift encodes this, the native
  code must too.
- **Lift changes** (new extra entries, lifter updates) move function
  boundaries: hash pins catch it; re-audit before regenerating.
