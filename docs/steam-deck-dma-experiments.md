# Steam Deck DMA experiments, 2026-11-15

The target remains stable 30+ FPS at 1280x720 and 100% render scale, including
heavy menu views and the answer reveal. An average over faster scenes does not
establish that result. The installed executable is still SHA-256
`189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.

## Submission diagnostics

An experimental adapter bypassed inactive diagnostics for job images 38–41.
It retained the original transfer engine, repeated-transfer backoff, tag state,
and synchronization. Any relevant diagnostic or policy-mode request selected
the original path. The SDK source was unchanged.

Sequential A/B/B/A runs used one frozen executable, switching only
`BUZZ_DMA_SUBMIT_FAST=0/1`. Each run lasted 90 seconds; the uncapped heavy-menu
window was 40–70 seconds, with the screenshot at 80 seconds. The input script,
1280x720/100% settings, and cold shader-cache setup were identical. No build,
replay, or CPU benchmark ran alongside these measurements.

| Arm | FPS | Median interval ms | p95 interval ms |
|---|---:|---:|---:|
| A1, original | 25.70 | 38.486 | 46.505 |
| B1, shortcut | 25.57 | 38.560 | 47.341 |
| B2, shortcut | 25.80 | 38.271 | 46.513 |
| A2, original | 25.60 | 38.649 | 46.936 |

Averages: 25.65 FPS original, 25.68 shortcut. This does not demonstrate a useful
performance gain. The candidate was not installed.

## Transfer diagnostics

A second candidate separately outlined the original diagnostic-heavy transfer
function, deriving a smaller hot function from the same transfer statements.
It retained range/alignment checks, acquire/release fences, overlay tracking,
reservation handling, PUT notifications, and the DMA observer. Submission
shortcuts were disabled for this test. The same 90-second ABBA protocol gave:

| Arm | FPS | Median interval ms | p95 interval ms |
|---|---:|---:|---:|
| A1, original transfer | 25.70 | 38.702 | 46.572 |
| B1, hot transfer | 25.83 | 38.370 | 46.901 |
| B2, hot transfer | 25.70 | 38.557 | 46.961 |
| A2, original transfer | 25.57 | 38.860 | 46.865 |

Averages: 25.63 versus 25.77 FPS, a 0.5% difference with overlapping arms and
no improvement in p95 frame intervals. This is insufficient evidence to promote
the added complexity. The candidate was not installed. Both experimental
adapters were removed from the source; `diagnostic-shortcuts.patch` and the
frozen generated sources preserve them for reproduction.

## Grouped reservation checks

A third candidate read eight reservation bits per bitmap byte instead of
calling the single-line query for each 128-byte DMA span. It retained acquire
loads, the locked copy on a hit, and the post-copy check/individual notifications
for reservations created during an unlocked copy. Only images 38–41 used it.

Validation passed 330,432 range/bit-mask comparisons under ASan/UBSan, 8,640
transfer cases using real reservation code, and another 11,520 comparisons
including 1,600 injected reservations immediately after a copy. Full replay
outputs matched for all 8,196 captured jobs, with the same existing capture
verdicts listed below.

| Arm | FPS | Median interval ms | p95 interval ms |
|---|---:|---:|---:|
| A1, per-line | 25.97 | 37.893 | 46.811 |
| B1, grouped | 25.63 | 38.628 | 47.542 |
| B2, grouped | 26.00 | 38.143 | 45.512 |
| A2, per-line | 25.70 | 38.401 | 46.467 |

This used the same sequential 90-second ABBA protocol. Means were 25.83 FPS
for the original and 25.82 for the candidate. The candidate was removed and
not installed. `codex-dma-range/` retains its patch, header, synthetic tests,
frozen generated sources, executable, qualification and live-run evidence.

## Replay qualification and existing capture differences

Both candidates produced identical complete ordered DMA streams and exit
controls with their switch disabled and enabled across 8,196 jobs:

| Corpus | Jobs | Exact to capture | Concurrent input changes | Existing capture failures |
|---|---:|---:|---:|---:|
| 38 question | 1500 | 1500 | 0 | 0 |
| 39 menu | 400 | 52 | 348 | 0 |
| 40 question | 600 | 600 | 0 | 0 |
| 41 question | 1500 | 1449 | 0 | 51 |
| 41 blend | 146 | 144 | 0 | 2 |
| 41 padding | 50 | 1 | 0 | 49 |
| 41 reveal sequence | 4000 | 4000 | 0 | 0 |

The old question/blend/padding captures contain output-padding differences
already present in the installed build. Do not describe these corpora as
fully exact to the original captures. The differential check establishes
that the candidate preserves the current reference's captured-case behavior;
it does not repair those discrepancies or prove correctness for all inputs.
The blend and padding sets overlap the question corpus.

`BUZZ_REPLAY_OUTPUT_DIR` now provides a complete replay output for this check,
before comparison to the capture. Create the output directory first and use
one replay iteration. Each `<capture basename>.output` contains native-endian
uint32 transfer count, then for each transfer four uint32 fields
(kind, LS address, EA, byte count) followed by the payload, followed by seven
uint32 exit fields (PC, status, stop code, mailbox valid/value, interrupt
mailbox valid/value). These are local same-architecture comparison artifacts,
not a portable capture replacement. Original capture verdicts remain active.

## Local artifacts

Under `/home/deck/buzz/performance/optimization/`:

- `codex-dma-submit/`: frozen candidates, qualification script/log/JSON,
  output dumps, four live runs, comparison JSON and benchmark script.
- `codex-dma-transfer/`: separate frozen candidate and equivalent qualification
  artifacts, live-run script, plus a synthetic transfer/reservation fixture.

The scripts retain executable hashes, settings, frame CSVs, CPU telemetry,
logs, and screenshots in the individual live-run directories. Timing windows
are script-relative; a screenshot after the window does not certify every
frame's scene. Neither average FPS nor these short runs alone establishes
stable 30 FPS.
