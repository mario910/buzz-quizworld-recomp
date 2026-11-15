# Vectorized index ranking on Steam Deck

The vectorized remapper passed exact-output checks and improved a captured
kernel benchmark by 28.1%, but the six clean menu runs did not establish a
useful FPS gain. It remains private. Stable 30+ FPS is not established.

## Mechanism and correctness

The prototype processes eight mark bytes at a time. A 4 KiB lookup table
provides prefix population counts and compacted vertex offsets for each
eight-bit mask. AVX2 stores emit ranks and ordered vertex/base pairs. Empty
blocks are skipped, consumed marks are cleared, and the original sparse-range
fallback and all-unique identity mapping are retained. The output allocation
reserves seven additional inactive entries for the final vector store; only
the logical unique count reaches vertex fetching.

This differs from the earlier [dense/mixed experiments](steam-deck-index-runs.md):
mixed blocks also use vector output stores, with a table replacing serial
set-bit extraction. The private x86 prototype requires AVX2/POPCNT and has no
portable runtime CPU feature gate. It is not suitable for promotion as-is.

AddressSanitizer and UndefinedBehaviorSanitizer checks passed:

- 8,192 exhaustive mask/tail/index-boundary cases with guarded buffers.
- Full remapping comparisons against the production ranked implementation,
  including 2,000 changing draws, SDK interleaving, generation wrap, allocation
  failures, maximal uint16 indices and guarded tails.
- 8,192 captured menu draws, comparing every ordered reference and occurrence
  map. These contain 2,629,638 indices and 960,704 unique references; 8,144 use
  the ranked path and none use the all-unique path.

An initial fixture incorrectly compared sorted production output with the
SDK's first-use ordering and failed. The corrected oracle uses the unchanged
production ranked header; both the initial failure and corrected tests are
retained. A separate live diagnostic run checked 3,878,912 draws with zero
mapping errors and an intact menu capture. The capture writer copies decoded
indices into owned storage and is absent from the timed candidate.

The captured benchmark includes byte swapping, min/max calculation, marking,
ranking, reference emission and occurrence mapping. Forty repetitions of the
complete corpus in original/new/new/original order measured 704.473, 505.307,
505.648 and 700.917 ns per remap, respectively. Means were 702.695 and
505.478 ns. All checksums agreed. This is kernel time, not a frame-rate gain.

## Scoped training and clean measurements

Only the renderer draw-engine translation unit received fresh PGO. Training
used enabled/disabled 90-second menus and an enabled 335-second round route;
the final round capture showed an intact scoreboard. Training timing is
excluded from comparisons. The resulting use-profile build reported no stale
profile warning. Other translation units retain the qualified PGO6 profile.

The six clean runs use A=frozen reference, C=candidate disabled and B=the same
candidate enabled, in A/C/B/B/C/A order. Each lasts 90 seconds at 1280×720,
100% scale, X11, uncapped mailbox, 512-draw batches, two contexts, zero worker
spin and image-36 search enabled. FPS spans seconds 40–70, CPU telemetry
45–65, and scene captures occur at 80. No build, correctness benchmark or
sampling profiler overlaps these runs. CPU time is aggregate process work,
not elapsed frame time. Captures do not certify camera phase for every frame.

| Arm | FPS | Median ms | p95 ms | Lowest one-second FPS | CPU ms/frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| A1 | 28.000 | 35.320 | 43.593 | 24 | 155.493 |
| C1 | 27.767 | 35.566 | 43.225 | 22 | 157.676 |
| B1 | 27.733 | 35.325 | 43.784 | 23 | 156.824 |
| B2 | 27.933 | 35.399 | 43.678 | 24 | 155.251 |
| C2 | 27.767 | 35.461 | 43.799 | 25 | 157.624 |
| A2 | 28.033 | 35.381 | 43.170 | 24 | 154.640 |

Means were A 28.017, C 27.767 and B 27.833 FPS. The candidate does not beat
the qualified reference. Of 1,670 enabled intervals, 1,202 (72.0%) exceeded
33.34 ms. All six effective renderer settings matched, presentation deltas
were one, and all six post-window captures were inspected and intact. No
clean full-round FPS qualification is claimed.

## Private artifacts

`/home/deck/buzz/performance/optimization/codex-index-vector/` retains the
prototype generator/header, original header, corrected and initial fixtures,
captured input, sanitizer and benchmark logs, scoped training scripts/profiles,
frozen executables, clean run data and restoration script.

- Qualified reference: `630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d`.
- Timed candidate: `4c49d5bf5caad3af859f8341537fc2ad91c8c8a8804f986209dc45fc00cf261b`.
- Prototype header: `a8f1581ff9a2c8c1d1477930a3c83fe30d7c3819997704fbdc6c216a43ff6965`.
- Scoped profile: `f7cd773188060d12be9563928fbbd7a08c9d74605e7819deb477d5d4675f826b`.

The original header and build rules were restored with current timestamps.
The prepared executable rebuilt byte-identical to the qualified reference.
The SDK remains clean at `79efc280dad27271d5f68077acb0247df1e88dc7` and the
installed executable remains
`189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.
