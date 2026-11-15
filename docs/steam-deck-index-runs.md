# Steam Deck renderer index experiments, 2026-11-15

Neither candidate demonstrated a useful FPS improvement. The original renderer
header was restored; the installed executable remains SHA-256
`189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.
Stable 30 FPS remains unachieved in the measured heavy menu.

## Candidates and correctness

A separate on-device profile identified stores in the renderer's ranked index
remapping loop as a cost within `sink_end`. Two candidates processed the mark
array in eight-entry blocks:

- Dense: skip empty blocks and emit full blocks with SIMD stores; retain the
  original scalar loop for mixed blocks.
- Mixed: additionally extract a bit mask from mixed blocks and emit only marked
  entries, writing each vertex ID/base pair together.

Both retained the original occurrence-to-unique mapping and the all-unique
identity path. Unmarked rank entries were not consumed. Each candidate passed
5,096 standalone cases under AddressSanitizer and UndefinedBehaviorSanitizer,
including all eight-bit masks with tails, sparse/dense spans, and uint16 limits.
Separate live functional runs with `BUZZ_INDEX_RANK_CHECK=1` checked 3,280,896
mappings for Dense and 3,264,512 for Mixed, with no reported errors. Their menu
screenshots were inspected. These are renderer checks; captured SPU jobs do not
exercise this remapping code.

## Measurements

Runs were sequential, uncapped, 1280x720 at 100% scale, using the same input
script and a fresh shader cache. Each lasted 90 seconds; measurements cover
40–70 seconds and screenshots were taken at 80 seconds. Rank diagnostics and
sampling profilers were disabled during measurements. No concurrent builds or
correctness benchmarks ran. The script-relative windows are exploratory;
post-window screenshots do not certify every frame's scene.

An initial Dense same-binary OFF/ON/ON/OFF comparison measured
24.47 / 25.83 / 26.07 / 26.10 FPS. The final control overlapped the candidate;
the low first control did not establish a repeatable improvement. Both modified
builds also had one PGO hash mismatch, in `buzz_index_packed_spans`. Therefore a
second comparison used the unchanged installed executable as reference:

| Arm | Build | FPS | Median interval ms | p95 interval ms | CPU ms/frame |
|---|---|---:|---:|---:|---:|
| A1 | Installed | 25.97 | 38.090 | 46.639 | 164.62 |
| B1 | Dense | 25.83 | 38.424 | 46.266 | 165.47 |
| C1 | Mixed | 25.90 | 38.257 | 47.133 | 165.51 |
| C2 | Mixed | 26.00 | 38.171 | 45.920 | 165.35 |
| B2 | Dense | 26.07 | 38.016 | 45.795 | 164.17 |
| A2 | Installed | 25.63 | 38.547 | 47.497 | 166.02 |

Mean FPS was 25.80 installed, 25.95 Dense and 25.95 Mixed: a 0.6% difference
with overlapping arms. CPU work per frame was also effectively unchanged.
CPU values are aggregate process time over the telemetry samples within
45–65 seconds, not elapsed frame time. Across the runs, 88–90% of frame
intervals exceeded 33.34 ms. Neither candidate warrants promotion or a claim
of stable 30 FPS.

## Local artifacts

Under `/home/deck/buzz/performance/optimization/`:

- `codex-index-dense/`: dense-only source/patch, frozen executable, sanitizer
  fixture, functional check, and initial four-run comparison.
- `codex-index-mixed/`: mixed source/patch and fixture, frozen executable and
  installed reference, functional check, `compare-installed.py`, and complete
  six-run results in `installed-comparison.json`.
- `codex-dma-range/profile-heavy/`: independent installed-build CPU profile,
  including `sink-annotate.txt` and `leaf-summary.json`. DWARF unwinding was
  incomplete; the separately named unwind-only summary is not a whole-process
  CPU share. Samples indicate cost, not removable frame time.

The run directories retain frame CSVs, CPU telemetry, configuration, logs,
input scripts and screenshots. Experimental source and tests are archived
outside the production tree so the rejected candidates remain reproducible.
