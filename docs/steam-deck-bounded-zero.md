# Bounded projection and skinning zero canonicalization

Neither private integration established a reliable net FPS improvement or
sustained 30 FPS. Experimental code was not promoted.

This private experiment retains zero canonicalization at visible outputs and
at the boundary between palette blending and transforms, while omitting it
inside bounded linear arithmetic chains. It preserves operation order, native
RTZ/FTZ/DAZ, explicit fused operations, existing reciprocal/refinement, and
all admission and fallback rules. No reassociation or approximate reciprocal
is introduced. No game/renderer quality setting changes.

Only the bounded paths change. The existing bounds exclude exceptional
extended-range results. In these finite mul/FMA chains, an intermediate
zero sign cannot affect a nonzero result. Final canonicalization preserves
the observable zero representation, including projection’s subsequent W-sign
XOR. W is normalized before sign extraction and reciprocal operations.

## Correctness evidence

- 200,000 bounded projection blocks matched original bounded and generic
  projection, including all four clip fields. The three-FMA rows also matched
  6,400,000 independent integer SPU lane results.
- Object positions: 100,000 trials, 66,668 admitted and 33,332 unchanged
  rejections; 1,600,032 admitted components matched integer SPU arithmetic.
- Weighted transforms: 20,000 blocks/640,000 component words matched the
  generic path, including directions, points, tails and extended-input fallback.
- 5,228 captured non-blend jobs matched all active skin output blocks, all
  logical/padded clip fields and complete index data. Projection bounds admitted
  546/1,349 question jobs and 2,784/3,879 reveal jobs.
- Additional whole-skin mutations covered 96 object and 96 weighted cases per
  corpus: zero/dense/non-normalized byte weights, signed zero, denormals,
  cancellation, boundary values and extended palette/input values. ASan and
  UBSan checks passed.
- Whole-job replay used 1,500 question, 4,000 reveal, 146 blend and 50 padding
  entries with three requested repetitions. Every ordered DMA/exit file
  matched the retained baseline. The blend/padding sets overlap question.
  Existing recorded-output discrepancies (51/0/2/49 respectively) are unchanged;
  see [DMA experiments](steam-deck-dma-experiments.md).

## Standalone timing

With 200 regularly sampled non-coarse-culled jobs per corpus and 500
repetitions in ABBA order, projection/culling alone improved by 4.1% in
question jobs and 7.7% in reveal jobs. Skinning plus projection/culling,
including active input/index copies, fell from 6,151 to 5,934 ns/call for
question jobs (3.5%) and 5,902 to 5,465 ns/call for reveal jobs (7.4%).
These are kernel measurements, not FPS claims.

## First integration: startup-selected combined candidate

The first private integration duplicated the optimized skin/projection
helpers behind a startup option and combined them with bounded triangle area.
The latter is documented in [bounded area](steam-deck-bounded-area.md).
A is frozen reference, C is the candidate disabled, B is enabled.

| Arm | FPS | p95 frame ms | Lowest one-second FPS | CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 | 28.267 | 42.876 | 26 | 155.185 |
| C1 | 28.067 | 43.472 | 21 | 156.773 |
| B1 | 28.267 | 42.730 | 25 | 155.490 |
| B2 | 28.400 | 43.710 | 24 | 156.269 |
| C2 | 27.833 | 43.793 | 26 | 159.299 |
| A2 | 28.233 | 43.740 | 23 | 156.523 |

Means: reference 28.250, disabled 27.950, enabled 28.333 FPS.
The enabled-versus-disabled difference did not establish a net improvement
over the qualified reference. Disabled-path overhead is an inference from
the timings, not an independently localized cause. There was no PGO hash
mismatch, but the newly named helpers did not have dedicated training.
This integration was not promoted. B2/C2/A2 captures were visually intact.
All six effective renderer profiles matched and presentation deltas were one.

## Smaller integration

The follow-up changes only the existing bounded position, weighted skin and
projection implementations. It keeps original function names and call sites,
adds no runtime switch or duplicate pipeline, and leaves triangle-area culling
unchanged. Tests compile against the actual integrated headers; the scalar
object/projection and weighted comparisons above were repeated successfully.
Whole-job DMA/exit replay again matched all 5,696 corpus entries.

| Arm | FPS | p95 frame ms | Lowest one-second FPS | CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 | 28.433 | 43.279 | 23 | 154.832 |
| B1 | 28.667 | 42.023 | 26 | 153.860 |
| B2 | 28.433 | 43.718 | 23 | 155.037 |
| A2 | 28.167 | 44.032 | 23 | 154.954 |
| B3 | 28.333 | 44.744 | 22 | 154.015 |
| A3 | 28.033 | 44.142 | 23 | 157.213 |
| A4 | 28.167 | 44.066 | 25 | 156.922 |
| B4 | 28.067 | 44.449 | 22 | 154.125 |

Eight runs in A1/B1/B2/A2 then B3/A3/A4/B4 order gave means of
28.200 FPS for the reference and 28.375 for the candidate (+0.175 FPS, 0.62%).
The first four runs suggested +0.250 FPS; the reverse-order repeat reduced
it to +0.100 FPS, and its last candidate was slower than the preceding control.
The ranges overlap, and frame-time tails did not consistently improve.
Process CPU/frame averaged 155.980 ms for the reference and 154.259 ms for
candidate, a modest reduction that did not establish a robust FPS benefit.
The candidate is retained privately, not promoted or enabled. No full-round
FPS benefit is claimed.

Both series used private 90-second runs with fresh saves and application
SPIR-V caches. FPS windows were elapsed 40–70 seconds, CPU windows 45–65.
All effective profiles matched: 1280×720, 100% render scale, mailbox,
uncapped, 512-draw batches, two draw contexts, zero worker spin. Every sampled
presentation delta was one. B2/A2 and B4/A4 scene captures were intact.
Builds, tests and profilers did not overlap timed runs. The smaller build
also reported no PGO hash mismatch.

Elapsed windows do not guarantee identical animated camera phases. Do not
pool these sessions with unrelated earlier baselines or call the average
difference a proven stable-frame-rate gain. The 30+ FPS goal remains open.

## Private evidence

First prototype/integration: `performance/optimization/codex-bounded-zero/`.
Smaller integration: `performance/optimization/codex-bounded-zero-minimal/`.
Both contain generators, synthetic/captured tests, build logs, replay checks,
frozen candidate, FPS driver, per-run results and original-header backups.
Paths are relative to `/home/deck/buzz/`; no captures or generated code are
committed. All timed arms enable image-36 search.

SHA-256 identities:

- Frozen reference: `591aa41c7d50bbccfb72f5a36632d3ff037e69128f0236fd3fe5a1d6971a39f8`.
- Combined candidate: `9a7fd78aa6b2038daee37b2a4eecb874753b065f1cf2eadb3378697c0cd84c22`.
- Smaller candidate: `01dbca365e2cbb6a063d0a08967ba1ed2c9932592fab4ba422f6d4d30e598001`.
- Installed executable (unchanged): `189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.

The workspace headers were restored; the rebuilt executable matched the
qualified reference byte for byte. Installed files and the SDK pin
`79efc280dad27271d5f68077acb0247df1e88dc7` remain unchanged.
