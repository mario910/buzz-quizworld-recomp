# Current Deck job sensitivity and translation preparation

Small post-job delays have a larger repeatable effect on current menu FPS for
image-41 geometry than for image-39 command jobs. This supports investigating
geometry work further; it does not identify an additive critical path or
predict an equivalent speedup. A separate exact translation-only shortcut
now passes complete-preparation checks and reduces standalone preparation
time by about 2–4%. No live FPS gain is established, and stable 30+ remains unmet.

Follow-up: the [private live translation trial](steam-deck-translation-live.md)
passes whole-job output comparisons but remains within menu timing variation.
The shortcut was not promoted.

## Current-build delay study

Ten 90-second runs use the same committed SIMD executable, SHA-256
`d6420f0b0905ed62958f6c42cfcbfe740d8851295484631d99460ccec49b5b56`.
The existing `BUZZ_CAUSAL_DELAY=image:microseconds` diagnostic busy-waits after
normal job execution and cleanup, before the host job function returns.
Original guest computations, transfers and publications still execute.
The option applies from startup; it is not a production optimization.

The sequence is control, 39/25, 41/1, 39/100, 41/4, then the reverse sequence.
Each run has a fresh runtime and shader cache, X11, 1280×720, 100% scale,
uncapped mailbox, 512-draw batches, two contexts and zero worker spin.
Image-36 search and image-7 conversion are enabled; chain workers are off.
FPS covers seconds 40–70, process CPU samples 45–65, and captures second 80.
No builds, other games, tests or sampling profilers overlap the series.

| Added delay per job | FPS, first / second | Mean FPS | Mean CPU ms/frame | Mean per-run p95 ms |
| --- | ---: | ---: | ---: | ---: |
| None | 28.5667 / 28.8333 | 28.7000 | 150.2199 | 40.8775 |
| Image 39, 25 µs | 29.0667 / 28.8000 | 28.9333 | 150.7632 | 40.9140 |
| Image 39, 100 µs | 28.7000 / 28.4333 | 28.5667 | 153.5888 | 41.5610 |
| Image 41, 1 µs | 28.4000 / 28.3333 | 28.3667 | 153.7431 | 41.1740 |
| Image 41, 4 µs | 27.1333 / 27.1333 | 27.1333 | 162.5439 | 43.5300 |

All runs present beyond second 88, all measured presentation deltas are one,
and effective renderer settings match. Startup logs confirm each requested
delay, with no delay marker in controls. All ten captures were inspected and
show intact menus. No searched crash, device-loss or nonzero native-mismatch
marker was found. Captures do not certify every timed frame or camera phase.

The image-41 response repeats at both doses. The image-39 FPS differences are
small relative to the control spread. The delay occurs after guest publication:
weak response here does **not** prove image-39 computations before publication
are unimportant. Busy waiting also changes contention, power and scheduling.
Delays can overrun when preempted, and per-image call counts were not collected
inside these windows. Do not convert these figures into predicted gains from
removing work or sum them with overlapping trace categories.

Older `cc-causal-*` captures used a different `2e9a2cf0…` executable, phase
instrumentation and menu controls near 21.7 FPS. They are historical context,
not controls for this current-build series.

## Complete translation preparation

The earlier [translation-only study](steam-deck-clip-operations.md) simplified
object transforms with exact +1 diagonal and signed-zero off-diagonal matrix
coefficients. It preserves SPU input canonicalization, fused translation and
direction normalization. The new fixture regenerates the shortcut from current
source and tests it inside complete preparation, including coarse clipping,
fully culled jobs, deferred attributes and final output formatting. The
standalone mutable-blend path stays unchanged and is excluded from execution.

ASan/UBSan checks pass 200,000 arbitrary point/direction blocks, including
extended values, denormals, signed zeros and inactive tails. Full preparation
checks compare admission/rejection reasons, active and padded host blocks,
readiness flags, clip/index metadata, every emitted vertex/index byte and
unchanged guest registers/LS. Each corpus also checks 128 codec mutations.
Both zero-padding and strict published-padding policies are covered:

| Corpus | Files | Zero-padding admitted | Strict-padding admitted | Translation object jobs | Translation / object vertices |
| --- | ---: | ---: | ---: | ---: | ---: |
| Current menu | 1,500 | 1,493 | 1,259 | 484 | 230,890 / 350,327 |
| Question | 1,500 | 1,349 | 1,076 | 405 | 133,757 / 237,087 |
| Reveal | 4,000 | 3,879 | 3,150 | 1,312 | 704,028 / 1,020,368 |

Original and candidate reject the same seven/five/twelve storage-4 jobs,
146/109 question/reveal mutable-blend jobs, and the same additional strict
padding cases. These are preparation/output checks, not whole-job DMA or live
rendering qualification. Coverage proportions are not live hit rates.

Complete-preparation timing uses 200/193/194 regularly sampled admitted cases,
including fully culled jobs, in original/candidate/candidate/original order.
Each case executes 500 times, with reused workspace. Warm timing repeats one
case; streaming timing cycles through all retained cases. Output comparisons
are outside timing. Compilation is Clang 18, O3, AVX2/FMA, strict native RTZ,
with no PGO. These tests start only after the live delay driver exits.

| Corpus | Warm original / candidate ns | Reduction | Streaming original / candidate ns | Reduction |
| --- | ---: | ---: | ---: | ---: |
| Menu | 7,459.111 / 7,201.326 | 3.46% | 9,533.599 / 9,232.814 | 3.16% |
| Question | 10,991.634 / 10,768.540 | 2.03% | 13,688.415 / 13,079.495 | 4.45% |
| Reveal | 9,182.465 / 8,867.673 | 3.43% | 11,425.891 / 11,154.714 | 2.37% |

Checksums match. The two streaming question controls vary from 13,984 to
13,392 ns, so the mean is not a precise prediction. The consistent direction
justifies a private game trial; it does not justify production promotion yet.

## Artifacts

Under `/home/deck/buzz/performance/optimization/`, `codex-current-job-sensitivity/`
retains the frozen executable, plan/driver, ten run folders, captures, settings
comparison, logs, frame/CPU data and summary. `codex-translation-preparation/`
retains current-source generation, source hashes, synthetic and full-preparation
fixtures, sanitizer logs, strict-padding checks, compiler commands and timings.
Both completed drivers have exited. No production source, SDK or installed
executable changed in these studies.
