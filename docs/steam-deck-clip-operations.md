# Exact clip operations and translation transforms on Deck

Neither tested projection integration established a useful whole-game FPS
improvement. They remain private experiments. Stable 30+ FPS is unachieved.
A separate exact translation-only transform passed kernel tests but has not
been integrated or measured in-game.

## Fresh CPU attribution

The qualified reference is SHA-256
`630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d`.
A 90-second menu run sampled user cycles at 199 Hz during seconds 45–65,
without call stacks. It recorded 17,723 samples and no lost events. The
post-window menu capture was intact. An earlier attempt failed to allocate
its perf ring; only the retry with 64 mmap pages supplies these results.

Largest aggregate self shares included geometry preparation 12.36%, skinning
4.66%, `spu_wrch` 4.44%, `sink_end` 4.28%, geometry publication 3.50%,
image-7 native work 2.98% and FIFO processing 2.36%. These are sampled user CPU
shares, not removable elapsed frame time or an FPS prediction.

Thread attribution, weighted by recorded sample periods, found:

| Thread | Share of sampled user cycles | OS runtime utilization |
| --- | ---: | ---: |
| RSX FIFO worker | 16.07% | 63.19% of one CPU |
| SPURS chain `ch 701EB500` | 13.38% | 52.56% |
| Geometry lane 1 | 11.33% | 46.57% |
| Geometry lane 2 | 11.38% | 46.78% |
| Geometry lane 3 | 11.39% | 46.77% |
| Geometry lane 4 | 11.25% | 46.55% |

OS runtime spans seconds 44.052–67.235, bracketing the sampling interval.
It includes system execution and is a different measurement from user cycles.
The four lanes run concurrently; their costs must not be added to wall time.
Within the renderer's user samples, `sink_end` accounts for 26.61%, FIFO
processing 14.70%, unresolved RADV code 12.58%, unresolved libc code 10.31%,
`vk_eng_draw` 8.39% and RSX method processing 5.54%. Unresolved libc code also
accounts for 23.38% of the sampled chain thread. Those module shares do not
identify a particular libc operation or prove a critical dependency.

This supports renewed inspection of renderer/chain instructions alongside the
[existing dependency traces](steam-deck-critical-work.md). It does not prove
that eliminating worker arithmetic will shorten a complete frame.

## First projection candidate

The existing coarse-clip admission proves finite projection intermediates and
normal nonzero W with unbiased exponent between -30 and 30. The candidate
uses that proof to combine the original integer FREST/FI estimate, omit
impossible exponent repairs, use finite hardware comparisons under the existing
DAZ mode, and remove the extended-range integer-conversion fallback. General
inputs retain the original path. FMA order, reciprocal refinement, RTZ/FTZ,
signed-zero canonicalization and all job effects are unchanged.

ASan/UBSan checks compared 18,377,216 estimate words, 1,600,000 comparison and
conversion lanes, and 200,000 complete projection blocks. All matched.
Captured checks compared all logical/padded clip fields and complete cull
workspaces for 5,228 supported non-blend jobs. Bounded projection covered
546/1,349 question jobs and 2,784/3,879 reveal jobs. The fixture excluded
5/12 unsupported-storage jobs and 146/109 mutable-blend jobs respectively.

Each corpus benchmark saved 200 regularly sampled non-coarse-culled cases,
then ran 500 repetitions per case in original/enabled/enabled/original order.
Projection plus culling, including active index copies, improved from
3,174 to 2,941 ns for question cases (7.3%) and 3,665 to 3,236 ns for reveal
cases (11.7%). These are isolated kernel timings.

The six clean menu runs used A=frozen reference, C=candidate disabled and
B=same candidate enabled:

| Arm | FPS | p95 interval ms | Lowest one-second FPS | Process CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 | 28.200 | 44.061 | 24 | 155.207 |
| C1 | 28.367 | 43.074 | 24 | 155.664 |
| B1 | 28.200 | 43.189 | 23 | 156.623 |
| B2 | 28.333 | 42.949 | 24 | 155.204 |
| C2 | 28.400 | 42.791 | 24 | 155.656 |
| A2 | 28.300 | 43.246 | 24 | 156.742 |

Means were 28.250 FPS for A, 28.383 for C and 28.267 for B. There is no useful
net improvement. The build reported no PGO hash mismatch, but the new helper
names had no dedicated training. Symbol inspection also showed substantially
different inlining/placement. That observation alone does not establish the
cause of the flat result.

## General fused estimate and refreshed native profile

The follow-up proves that FI's normalization branch is impossible for every
FREST table entry, including general inputs. For lookup index `i`, the low
19-bit input `y` is in:

```
[(i % 2) * 2^18, ((i % 2) + 1) * 2^18 - 1]
```

All 32 entries satisfy `(base << 19) > step * maximum_y`; the smallest
difference is 263. Both quantities fit unsigned 32 bits. Therefore FI never
borrows an exponent and its fraction is exactly
`((base << 19) - step * y) >> 9`, masked to 23 bits. The general helper retains
FREST's zero/extreme exponent rules and FI's zero-result mask. The bounded
helper additionally omits only the already-excluded exponent cases.

ASan/UBSan checks added 150,994,944 estimate words covering every mantissa and
both signs at nine boundary/exceptional exponents, all table inequalities,
and 10,000 arbitrary unbounded projection blocks. The first candidate's
synthetic and captured checks also passed. The kernel means were 3,074 to
2,801 ns for question cases (8.9%) and 3,527 to 3,094 ns for reveal cases
(12.3%). No gain from separate experiments is added to these timings.

Only `native_geometry_job_flow.cpp` received refreshed PGO. Training used
90-second enabled/disabled menu runs and a 335-second enabled question route;
other units retained PGO6. The question capture was intact, but instrumentation
slowed progression and it had not reached the expected scoreboard. This is
question coverage, not successful reveal/scoreboard qualification. Training
runs are excluded from clean FPS comparisons. The rebuilt use-profile binary
contains no profile-generation flags and reported no stale-profile warning.

| Arm | FPS | p95 interval ms | Lowest one-second FPS | Process CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 | 28.000 | 43.110 | 26 | 157.264 |
| C1 | 28.133 | 42.184 | 26 | 156.983 |
| B1 | 28.133 | 41.834 | 26 | 157.476 |
| B2 | 28.167 | 41.832 | 26 | 155.728 |
| C2 | 28.400 | 42.256 | 22 | 155.989 |
| A2 | 28.233 | 42.295 | 25 | 156.000 |

Means were 28.117 FPS for A, 28.267 for C and 28.150 for B. Refreshed profiling
did not establish a useful end-to-end improvement. Neither version is promoted,
and no clean full-round FPS benefit is claimed.

For both integrations, whole-job replay compared all 5,696 ordered DMA/exit
files against both runtime modes and the retained reference. Every file
matched. Three repetitions were requested; the 51/0/2/49 existing capture
mismatches in question/reveal/blend/padding remain unchanged and stop after
their first failure. There were 16,782 exact captured comparisons per arm,
including overlapping blend/padding sets and fallback jobs. These are not
5,696 new exact matches to original captures.

All twelve clean menu runs completed with matching effective settings:
X11, 1280×720, 100% scale, mailbox, uncapped, 512-draw batches, two contexts,
zero worker spin and image-36 search enabled. FPS windows were seconds 40–70,
CPU windows 45–65 and captures at 80. Every sampled presentation delta was one.
All twelve captures were inspected and intact. Builds, tests and sampling
profilers did not overlap clean measurements. Elapsed windows still do not
certify identical camera phase in every measured frame.

## Translation-only object transform prototype

The captured input study found no exact-identity object jobs, but many had
identity rotation/scale with translation: 405/911 question object jobs
(133,757/237,087 vertices), and 1,312/2,457 reveal object jobs
(704,028/1,020,368 vertices). This excludes mutable blend and unsupported jobs;
it is corpus coverage, not a live hit rate.

The separate prototype admits exactly +1 diagonal coefficients and signed-zero
off-diagonal rotation coefficients. SPU multiplication by one canonicalizes
exponent-zero input words and preserves ordinary/extended words; subsequent
zero FMAs cannot change the canonical result. Points retain the same fused
W-times-translation plus coordinate operation. Directions retain the original
length, reciprocal estimate and normalization. Weighted transforms are unchanged.

It passed 200,000 arbitrary point/direction block comparisons under ASan/UBSan,
including denormals, extended values, signed-zero coefficients and inactive
tails, plus all active skin blocks/clip fields/cull data in the 5,228 captured
cases. Skinning plus projection/culling and active input/index copies improved
from 6,161 to 6,050 ns in question cases (1.8%) and 5,903 to 5,556 ns in reveal
cases (5.9%). It has no whole-job, live rendering or FPS qualification and
remains private. These savings cannot be added to the projection percentages.

The later [complete-preparation study](steam-deck-job-sensitivity.md) adds the
current menu corpus, eager/deferred output comparisons and strict-padding
checks. It measures roughly 2–4% less complete-preparation time. Live FPS
qualification remains pending; the earlier kernel figures are not additive.

## Artifacts and restoration

Private evidence under `/home/deck/buzz/performance/optimization/`:

- `codex-cpu-preparation/`: frozen reference, successful/failed profiler logs,
  self/instruction/thread attribution, runtime snapshots and intact capture.
- `codex-bounded-clip-ops/`: first prototype, tests, captured benchmarks,
  whole-job outputs, frozen candidate and six-run comparison.
- `codex-clip-ops-v2/`: general fused estimate, tests, scoped training source
  and profiles, frozen candidate, replay outputs and six-run comparison.
- `codex-translation-skin/`: separate generated helper, differential fixture,
  captured tests/benchmarks and remaining qualification notes.

First candidate SHA-256:
`6a38f5116c2ae012a085cbe8627ad0976e9a5948979e69ec947de7ee57fd6b82`.
Refreshed candidate SHA-256:
`cafbd7bd43ffe35f8b2053b42a225c79ef06231792e33bb79723b1c3781f0fc0`.
Its scoped profile SHA-256:
`7abbf7a5cbbc80e6a1dfd79a54be1f726f2ad8b03563c5409ddef1dd83581b59`.

Experimental workspace headers and compiler flags were restored with current
file timestamps. The rebuilt executable matches the qualified reference
byte for byte. The installed executable remains
`189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`;
SDK pin `79efc280dad27271d5f68077acb0247df1e88dc7` is clean. No binaries,
profiles, captures, generated game code or experimental runtime paths are
committed by this study.
