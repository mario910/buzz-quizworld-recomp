# Bounded DMA read prefetch on Steam Deck

Neither prefetch comparison established a net performance benefit. The
prototype was removed from the prepared build; stable 30+ FPS is unachieved.

## Mechanism and correctness

The [command-thread profile](steam-deck-chain-copies.md) identified small DMA
read callers. The candidate issues read-prefetch hints only for owned-LS jobs
in images 36 and 38–41, transfer lengths 1–128, and checked 32-bit guest spans.
It hints the current transfer after its existing range validation and warms
up to two list elements ahead. Descriptor lookahead stays within LS, skips
past an observed stall, and never substitutes prefetched data for a transfer.
The original list engine rereads descriptors for execution. Copies, fences,
range checks, observers, reservation handling, tag state and notifications stay.

Modes are 0=off, 1=list hints, 2=current-transfer hints and 3=both. ASan/UBSan
checks compared modes 1–3 against mode 0 for 3,000 differential list cases,
including stall/resume, LS wrap and GETs overwriting later descriptors. Full
context, LS, guest bytes, tag-engine state and observer counts agreed. Existing
SDK transfer, repark and concurrent fixtures also passed. The synthetic
coherency stubs are inert; those tests alone do not establish concurrency
correctness for the full game.

Both mode 0 and mode 3 reproduced all 8,196 ordered transfer/exit output files
from the retained reference. Capture verdicts were unchanged, including
348 concurrently changing image-39 inputs and the existing 51 question,
two blend and 49 padding failures documented in
[DMA experiments](steam-deck-dma-experiments.md). The blend/padding subsets
overlap other captures. Each arm executed 24,384 jobs: 23,238 exact-input,
1,044 concurrent-input, and 102 existing failures that stop on first attempt.
This is differential parity, not 8,196 exact original-capture matches.

## Initial profile scope and menu comparison

Fresh compiler-profile training covered the DMA-channel, geometry-job and
animation-runtime translation units: 90-second enabled menu, 335-second
enabled round and 90-second disabled menu. The training round's final capture
was a question screen, not a scoreboard. All three captures were intact;
training timings do not supply clean FPS evidence. The final build had no
stale-profile mismatch warnings and no training counters.

The six sequential 90-second menus used fresh private runtimes/cache state,
1280×720, 100% scale, uncapped mailbox, batch 512, two draw contexts, zero spin
and image-36 search. FPS uses seconds 40–70, CPU seconds 45–65, capture second
80. No build, benchmark or profiler overlapped the series. Every measured
frame had presented_delta=1 and all runs progressed beyond second 88.
Effective renderer settings matched, and all six final menu captures were
inspected and intact. These captures do not certify identical camera phase
throughout the timed windows.

A is frozen reference `630007cb…`; C is candidate with hints disabled; B is the
same candidate with both hint types enabled.

| Arm | FPS | p95 interval ms | Minimum 1 s FPS | Process CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 | 28.100 | 43.570 | 23 | 156.234 |
| C1 | 27.233 | 45.543 | 22 | 160.699 |
| B1 | 27.600 | 44.831 | 23 | 158.362 |
| B2 | 28.000 | 44.055 | 23 | 156.767 |
| C2 | 27.833 | 44.070 | 23 | 157.490 |
| A2 | 27.967 | 44.091 | 24 | 157.140 |

Mean FPS: reference 28.033, hints disabled 27.533, hints enabled
27.800. The small on/off difference overlaps run variation and the
enabled build remains below the frozen reference. This does not justify
promotion.

Only the two DMA functions initially discarded old PGO counts; the geometry
and animation units had no control-flow mismatch. A follow-up retains the
original PGO6 profile for those two unchanged units and uses the fresh profile
only for DMA channels. It has passed the same full replay checks. The first
menu series was interrupted by a user-requested pause during C1; those runs
are archived separately. After resumption, a fresh six-arm series completed:

| Arm | FPS | p95 interval ms | Minimum 1 s FPS | Process CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 | 28.667 | 43.283 | 23 | 153.835 |
| C1 | 27.933 | 43.319 | 23 | 155.446 |
| B1 | 28.233 | 43.056 | 26 | 155.466 |
| B2 | 27.600 | 44.292 | 24 | 155.963 |
| C2 | 28.100 | 42.559 | 26 | 156.541 |
| A2 | 27.900 | 43.275 | 23 | 155.502 |

Mean FPS: reference 28.283, hints disabled 28.017, hints enabled 27.917.
Narrowing the compiler-profile scope did not establish a gain. The enabled
build is slightly slower even than its disabled control. All six effective
renderer settings matched; final menu captures were inspected and intact.
No further gameplay qualification is warranted for this candidate.

## Evidence and disposition

Private artifacts are under
`/home/deck/buzz/performance/optimization/codex-dma-prefetch/` and
`codex-dma-prefetch-narrow/`: headers, integration and restoration scripts,
synthetic fixtures, compiler profiles, frozen executables, replay outputs,
frame CSVs, telemetry, setting comparisons and captures.

Initial candidate SHA-256:
`3a7e6ab9831f29327d321a9e2c075643013e69c68ac4cd1e188c2ff55a67c1c1`.
Narrow-profile candidate SHA-256:
`5010bc477b40d9b5ef63baf9bc0d3ac79d0d62249f4b37521d4e2c0633e98866`.
The generated header and original compiler profiles were restored after all
queued benchmarks finished. The rebuilt workspace is byte-identical to the
qualified reference (`630007cb…`), and SDK pin `79efc280…` is clean. The installed
executable remains unchanged (`189814d1…`). No prototype is promoted or installed.
