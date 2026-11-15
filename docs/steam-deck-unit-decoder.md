# Packed-direction decoder experiments on Steam Deck

Exact lookup tables reduced warm-job decoder work but slowed a stream of
changing jobs. They remain private prototypes. No whole-job or live FPS gain
has been established, and stable 30+ FPS remains unachieved.

## Candidates and correctness

The storage-9 input contains two 10-bit coordinates and a four-bit selector.
Three variants preserve its original output words:

- Bounded arithmetic retains the operation order, RTZ/FTZ/DAZ environment and
  positive-zero repair, using finite hardware operations for the constant
  FMAs, squares, sum and subtraction. The original reciprocal-root estimate,
  interpolation and final multiply remain. Input and constant bounds keep
  these replaced intermediate operations below exceptional exponents.
- A full table stores the absolute dominant component for all 1,048,576
  coordinate pairs in 4 MiB. The original coordinate arithmetic and selector
  assembly remain; one AVX2 gather replaces the magnitude calculation.
- A triangular table uses the exact exchange symmetry of the two squared
  coordinates, requiring 524,800 words (2,099,200 bytes). Its index is
  `max(u,v)*(max(u,v)+1)/2 + min(u,v)`. Complementary coordinate codes are not
  symmetric: the constant arithmetic's zero crossing is not 511.5, so folding
  those codes would be incorrect.

Both immutable tables initialize from the original decoder with selector zero.
Construction masks floating-point exceptions, uses the required rounding and
flush modes, and restores the caller's complete MXCSR. C++ static initialization
provides thread safety. Initialization cost is excluded from the timings.

ASan/UBSan checks passed all 16,777,216 packed 24-bit inputs, including selectors
12–15 that the live input admission rejects, plus 80,000 arbitrary high-bit
inputs. All four output components matched for all three variants. The first
table initialization also preserved MXCSR exactly.

Captured input checks passed 1,349 supported question jobs and 3,879 supported
reveal jobs, comparing every active and padded SoA block and readiness/pending
fields. They excluded 146/109 mutable blend jobs and 5/12 unsupported-storage
jobs respectively. These input checks do not validate complete job outputs or
mutable blend handling.

## Captured workload timing

Each corpus contributed 200 regularly sampled non-coarse-culled jobs. The
benchmark measured input capture, skinning, projection, culling and active
index copies. It ran modes in original/bounded/full/triangular/triangular/full/
bounded/original order, with 100,000 calls per arm. Warm layout repeats each
case 500 times; streaming layout cycles through all 200 cases for 500 passes.
No game timing, build or profiler overlapped these benchmarks.

Mean elapsed ns/call across the two arms of each mode:

| Corpus/layout | Original | Bounded | Full table | Triangular table |
| --- | ---: | ---: | ---: | ---: |
| Question, warm | 8,496 | 8,375 | 7,892 | 7,867 |
| Question, streaming | 9,964 | 9,823 | 11,155 | 10,659 |
| Reveal, warm | 8,276 | 8,005 | 7,498 | 7,540 |
| Reveal, streaming | 9,495 | 9,471 | 11,396 | 10,758 |

The full and triangular tables regress streaming question time by 12.0% and
7.0%, and reveal time by 20.0% and 13.3%. This outweighs their warm-job gains
for proceeding with these designs. Bounded arithmetic has small, overlapping
streaming differences; it has no established live benefit. These are single
thread benchmarks and do not model simultaneous cache pressure from four lanes.

## Evidence and follow-up

Private source, generators, sanitizer fixtures, corpus checks and timing logs
are under `/home/deck/buzz/performance/optimization/codex-unit-decoder/`.
`benchmark-summary.json` contains the paired means and percentage calculations.
No production source or installed executable changed for this experiment.

A separate follow-up, `codex-unit-rsqrt/`, simplifies the exact composition
`fi(a, frsqest(a))`. Every fixed estimate-table entry has `base >= step`, so
`base*2^19 - step*y` cannot underflow for any 19-bit `y`. The estimate strips
sign and its exponent is always 62–255. This removes unreachable normalization
and zero-exponent handling without a new lookup table. Its standalone tests and timings are complete; it remains unintegrated.


The follow-up passed 17,577,216 packed inputs (all 24-bit words plus 800,000
arbitrary high-bit inputs), 1,242,368 direct composition checks across exponent,
sign and interpolation boundaries/random words, and all 64 algebraic table
obligations under ASan/UBSan. Both variants also passed the same 5,228 captured
input checks. Variant 1 changes only reciprocal-root interpolation; variant 2
combines it with the bounded decoding arithmetic above.

Mean ns/call, using the same two layouts and original/1/2/2/1/original order:

| Corpus/layout | Original | Interpolation only | With bounded decode |
| --- | ---: | ---: | ---: |
| question, warm | 8,700 | 8,567 | 8,398 |
| question, streaming | 10,058 | 10,077 | 9,780 |
| reveal, warm | 8,237 | 8,221 | 7,983 |
| reveal, streaming | 9,585 | 9,501 | 9,392 |

Interpolation alone has mixed/small differences. The combined decoder reduced
streaming question time by 2.76% and reveal time by 2.02%. This supports further
qualification, not a live FPS claim or proof of the 30 FPS objective.

## Bounded object-direction follow-up

`codex-object-directions/` independently checks object-normal transforms.
The existing caller admission proves matrix coefficients no larger than
`2^30`; the candidate also checks X/Y/Z against that bound, leaving rejected
inputs unchanged. Transformed components are bounded by `3*2^60`, and their
squared-length sum by `27*2^120 < 2^125`. The transform and length operations
therefore use exact-order finite arithmetic with the original zero repair.
Final normalized multiplies remain generic: zero/subnormal norms can still
produce extended reciprocal-root estimates. W is unused and unchanged.

ASan/UBSan passed 133,333 admitted object-direction blocks and 66,667 unchanged
rejections, including boundary magnitudes, arbitrary W, subnormals, zero norms
and inactive lanes. Both interpolation modes matched the original. All 5,228
supported captured jobs also matched every active/padded skin output block and
ready/pending fields; blend/unsupported exclusions remain as above.

Mean ns/call for input capture, skinning, projection/cull and index copies:

| Corpus/layout | Original | Bounded directions | With fused interpolation |
| --- | ---: | ---: | ---: |
| question, warm | 8,611 | 8,535 | 8,604 |
| question, streaming | 9,929 | 9,849 | 9,861 |
| reveal, warm | 8,174 | 8,124 | 7,983 |
| reveal, streaming | 9,496 | 9,374 | 9,302 |

The streaming differences are small: 0.81%/1.28% for bounded directions alone
and 0.69%/2.04% with fused interpolation, question/reveal respectively. These
are separate runs from the decoder experiment; their gains cannot be added.
Neither normal-transform variant has whole-job or live FPS qualification.


## Combined native integration

The bounded decoder, fused reciprocal-root interpolation and bounded object
normalization were integrated into the two existing prepared host headers,
without a runtime switch or a duplicate job pipeline. Tests compiled against
the actual candidate headers passed the exhaustive packed-input, direct
interpolation and bounded-direction checks above. The integrated direction
fixture invokes the same candidate twice; its inherited log wording does not
establish two different runtime modes.

Only `native_geometry_job_flow.cpp` received a fresh compiler profile, trained
with a 90-second menu and a 335-second scripted gameplay run. The training
captures were inspected and showed an intact menu and scoreboard. The clean
rebuild reported no stale-profile mismatch. Other translation units retained
PGO6, and training instrumentation was removed before replay and timing.

All 8,196 complete ordered transfer/exit files matched the retained reference,
with the same original-capture verdicts documented in
[DMA experiments](steam-deck-dma-experiments.md). Three requested repetitions
produce 24,384 executions, including the existing failures that stop on their
first attempt. This is differential parity, not proof that every retained
capture matches its historical output.

Four clean sequential 90-second menu runs compared frozen reference A against
candidate B in ABBA order. Settings were 1280×720, 100% scale, uncapped mailbox,
batch 512, two contexts, zero spin and image-36 search. FPS covers seconds
40–70; process CPU covers 45–65; captures are at second 80. All frame deltas
were one, all runs progressed beyond second 88, settings matched, and all four
captures were inspected and intact. No build, profiler, replay or benchmark
overlapped these measurements.

| Arm | FPS | p95 interval ms | Lowest 1 s FPS | CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 | 28.033 | 44.359 | 22 | 155.706 |
| B1 | 28.200 | 43.832 | 23 | 155.955 |
| B2 | 28.067 | 44.172 | 24 | 155.970 |
| A2 | 28.067 | 43.332 | 23 | 156.778 |

Means were 28.050 FPS reference and 28.133 candidate. About 67.8% and 66.7%
of intervals respectively exceeded 33.34 ms. The small difference does not
establish a useful gain, and the stable 30 FPS objective is unmet. No further
clean-round qualification or promotion was performed.

Private artifacts are in `codex-direction-combined/`. Candidate SHA-256 is
`93d01aabb26fef86fb0df513a917e410260e03dc4a0d685be5efedd840522ff2`.
The original headers and compiler settings were restored. The rebuilt prepared
executable is byte-identical to the qualified reference (`630007cb…`); the SDK
remains clean and the installed executable is unchanged (`189814d1…`).
