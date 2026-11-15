# Bounded triangle-area arithmetic: exact, but neutral on Deck

The private triangle-area candidate did not improve the measured menu frame
rate. Its enabled and frozen-reference arms both averaged **27.883 FPS**.
The experiment is not enabled or included in the port. The 30+ FPS goal
remains open.

## Arithmetic and admission

The existing culler computes four coordinate differences, two products and
a final difference. Each operation repairs exceptional SPU float results and
canonicalizes zero. The experiment accumulated the maximum absolute X/Y
bits during projection. Only jobs whose projected magnitudes were all at
most `2^30` used raw native subtraction/multiplication, in the same order,
with final zero canonicalization. Other jobs retained the original path.

Differences are bounded by `2^31`, products by `2^62`, and the final difference
by `2^63`. Thus no intermediate reaches the SPU extended exponent range.
Under scoped RTZ/FTZ/DAZ, intermediate zero signs cannot affect a nonzero
result through these finite operations. FP contraction stayed disabled.
No reciprocal, culling rule, index ordering, output padding or publication
behavior changed. The private startup option was `BUZZ_GEOMETRY_BOUNDED_AREA=1`.

## Correctness evidence

- ASan/UBSan: 200,000 area vectors, split evenly between bounded and fallback
  inputs; 800,000 admitted lane results also matched integer SPU arithmetic.
  Cases covered signed zero, denormals, cancellation, repeated vertices,
  exact admission boundaries and extended exponents.
- Captured composition: 5,228 admitted non-blend jobs and 20,912 arbitrary
  clip-record mutations matched the original complete index arrays and
  metadata. All 5,228 original captured jobs admitted bounded area. Separate
  ASan/UBSan checks repeated this against the actual integrated headers.
- Whole-job replay: 1,500 question, 4,000 reveal, 146 blend and 50 padding
  entries, with three requested repetitions per arm. Both arms' complete
  ordered DMA/exit files matched each other and the retained baseline files.
  Blend/padding sets overlap question captures; these are 5,696 corpus
  entries, not a claim of that many unique jobs.
- Recorded-capture verdicts were unchanged: question 1,449 exact/51 failed,
  reveal 4,000 exact/0 failed, blend 144 exact/2 failed, padding 1 exact/49
  failed. The existing padding discrepancies are described in
  [DMA experiments](steam-deck-dma-experiments.md); this is not a claim that
  every older capture passed the recorded-output oracle.

Standalone projection-plus-culling timing used 200 regularly sampled,
non-coarse-culled jobs per corpus, 500 repetitions, active index copies and
ABBA order. Question means fell from 3,194 to 2,936 ns/call (8.1%); reveal
means fell from 3,540 to 3,340 ns/call (5.7%). These kernel results motivated
the live test, but did not predict its outcome.

## On-device comparison

Six private 90-second runs, in A1/C1/B1/B2/C2/A2 order. A is the frozen
qualified search build, C is the candidate with bounded area disabled, and B
is the same candidate enabled. All arms enable image-36 search. The FPS
window is elapsed 40–70 seconds; process CPU/frame uses 45–65 seconds.

| Arm | FPS | p95 frame ms | Lowest one-second FPS | Process CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 reference | 27.833 | 44.823 | 22 | 155.112 |
| C1 disabled | 27.867 | 43.190 | 24 | 157.081 |
| B1 enabled | 27.767 | 44.018 | 25 | 158.260 |
| B2 enabled | 28.000 | 43.754 | 25 | 156.386 |
| C2 disabled | 27.733 | 43.211 | 25 | 158.435 |
| A2 reference | 27.933 | 43.333 | 26 | 155.974 |

Means: reference 27.883, disabled 27.800, enabled 27.883 FPS. There is no
demonstrated net gain, nor sustained 30 FPS. Matching elapsed windows do not
prove identical animated camera phases. Do not pool these runs with earlier
sessions or claim improvements from differences between their baselines.

All six effective renderer profiles matched: 1280×720, 100% render scale,
mailbox, uncapped, 512-draw batches, two draw contexts, zero worker spin.
Every sampled presentation delta was one. B2/C2/A2 captures were inspected
and showed intact menus. No builds, tests or profilers overlapped the series.
The candidate build reported no PGO hash mismatch.

## Related private layout and skin studies

A separate four-word clip-record layout replaced four independent component
gathers with row loads and transposes. Its isolated lookup was 2.5–3× faster,
and 26,140 captured/mutated cull cases matched. However, writing rows directly
from projection made the complete projection/cull benchmark 3–4% slower.
It was not integrated into a game build or promoted.

An eight-vertex uniform-palette-index broadcast also passed 50,000 synthetic
ASan/UBSan groups. All eight indices matched in 41.1% of contributing question
groups and 27.8% of reveal groups. A scalar equality check improved isolated
row-fetch timing at those hit rates; a vector equality implementation was
slower on misses. No complete skin-job or live FPS benefit was established,
so neither implementation was promoted.

## Reproduction and restoration

Private evidence is under
`/home/deck/buzz/performance/optimization/codex-cull-area/`, including
`test.cpp`, `captured.cpp`, `integrated.cpp`, `qualify.py`, `qualification.json`,
`compare-menu.py`, `menu-comparison.json`, `renderer-profiles.json`, build
logs, header backups and frozen candidate. Related studies are in sibling
`codex-clip-rows/` and `codex-skin-uniform/` directories. No captured data or
generated code belongs in Git.

SHA-256 identities:

- Qualified reference/restored workspace executable:
  `591aa41c7d50bbccfb72f5a36632d3ff037e69128f0236fd3fe5a1d6971a39f8`.
- Private candidate:
  `fdde84916c3ee9f1fa3317d522918e32811278105c1f5f6ee56a8af5b0308ee2`.
- Unchanged installed executable:
  `189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.

The workspace headers were restored and the rebuilt executable matched the
qualified reference byte for byte. The SDK pin remains
`79efc280dad27271d5f68077acb0247df1e88dc7`.
