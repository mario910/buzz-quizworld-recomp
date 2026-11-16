# Deck SIMD prefix-rank experiment, 2026-11-16

The SIMD prefix-rank prototype is correct in the checked cases but does not
improve clean menu FPS. It remains private and unpromoted. Stable 30+ FPS is
not achieved.

Unlike the earlier [dense/mixed index experiments](steam-deck-index-runs.md),
this version computes eight exclusive ranks with packed prefix sums and uses
an 8 KiB permutation table to compact mixed blocks into vertex/base pairs.
It clears consumed marks, retains sorted unique vertices and restores occurrence
order when all indices are unique. Vector output stores require eight available
slots; a scalar path handles tight capacities. The integrated version retains
the original loop for ranges below 16. It does not cache guest memory or assume
that a partial block is fully populated.

An additional AVX2-gather variant maps eight occurrences at once. It usually
costs more than scalar occurrence lookup in the standalone measurements, so
only the prefix-rank variant was integrated. `BUZZ_INDEX_PREFIX=0/1` selects
original/candidate behavior in one private executable, default off. This is
an x86 Deck prototype, not a portable production build option.

## Validation and isolated timing

Both standalone variants and the integrated threshold version pass ASan/UBSan.
Each suite has 8,192 direct mask/tail/capacity cases and 4,096 whole-remap input
cases. Whole-remap cases compare three implementations with an independent
ordering and occurrence oracle. They cover every eight-bit mask, tight heap
buffers, random bases, repeated and unique indices, empty/multiple spans,
16-bit index boundaries, and counts up to 65,536. Persistent scratch is reused
between calls. Only ranks for marked entries and the live output prefix are
part of the consumed-output contract.

Standalone timing uses Clang 18 O3, SSE4.1, `znver2` tuning, strict FP contraction
and no PGO. The new helpers have an AVX2 target attribute. For each synthetic
layout, the order is original/prefix/gather/gather/prefix/original. Every arm
checks the same output checksum; allocation and initial equivalence checks are
outside the timer. Counts/ranges are informed by older live diagnostics, but
index bytes are synthetic and buffers are hot. This is not captured-draw timing.

For representative random layouts, mean original/prefix ns per call are
266.1/205.7 (120 indices, range 100), 298.3/229.6 (144/104),
288.6/206.2 (102/128), 363.1/238.6 (120/184), and 323.2/234.3 (117/150).
Tiny six-index calls regress, motivating the original-loop threshold.
These isolated gains do not predict the frame-time result.

The [private build](steam-deck-private-build.md) first recompiles the unchanged
renderer and reproduces retained reference `d6420f0b…` exactly. Only the renderer
translation unit receives fresh PGO from separate 90-second menu runs with the
switch off and on. Instrumentation counters are atomic; the training-only exit
hook flushes them before `_Exit`. The final build restores the original main
object and has no profile mismatch warning. Its SHA-256 is
`c6d5b098fcf2b214da00137b51056422a6b7b9c4e5292b52c706a00dce002e4c`.

A separate 335-second functional run enables the existing per-draw rank checker.
At least 10,698,752 eligible draw mappings are checked with zero reported errors.
Logs show the enabled switch and checked ranges above the prefix threshold. Saved images
show intact setup, timed questions and a zero-score results scene. The functional
run uses continuous readback and is excluded from FPS evidence. These renderer
checks do not establish successful scored answers, deterministic round identity,
or faster question/reveal scenes. SPU job implementations are unchanged.

## Clean menu comparison

Six 90-second runs use fresh save/cache directories, 1280×720 at 100% scale,
uncapped mailbox, 512-draw batches, two draw contexts and zero lane spin. The
reference is unchanged; off/on arms use the same frozen candidate. No compiler,
fixture, profiler, capture watcher or other game overlaps these timings.
FPS covers seconds 40–70; process CPU covers 45–65; screenshots are taken at 80.

| Run | Build/path | FPS | Median ms | p95 ms | Min one-second FPS | CPU ms/frame |
|---|---|---:|---:|---:|---:|---:|
| A1 | Reference | 29.13 | 34.324 | 39.971 | 27 | 149.30 |
| C1 | Candidate off | 28.47 | 35.169 | 41.204 | 24 | 150.71 |
| B1 | Candidate on | 28.80 | 34.886 | 40.126 | 27 | 150.13 |
| B2 | Candidate on | 28.47 | 34.968 | 41.813 | 22 | 151.22 |
| C2 | Candidate off | 28.73 | 34.696 | 40.494 | 25 | 150.27 |
| A2 | Reference | 28.43 | 34.923 | 40.584 | 26 | 153.12 |

Means are 28.78 FPS for the reference, 28.60 off and 28.63 on. Enabling the
prototype changes FPS by +0.12% and CPU per frame by +0.12% versus its control;
these differences do not establish an improvement. Enabled FPS is also below
the unchanged reference mean. Reference variability is substantial, and no
benefit is inferred from the first control alone.

All runs reach beyond 88 seconds, measured presented-frame deltas equal one,
and effective renderer settings match. Error scans find no searched sanitizer,
mismatch, crash or Vulkan device-loss messages. All six post-window screenshots
show intact game-selection menus. These images do not prove frame-by-frame
scene equivalence or establish round-performance gains.

The private reference source/archive/main object are restored and the executable
again matches `d6420f0b…` exactly. The shared unqualified prepared executable,
installed distribution and SDK remain unchanged. Sources, generators, fixtures,
profiles, compiler/link commands, state files, logs, timing data, hashes and
captures are retained under
`/home/deck/buzz/performance/optimization/codex-index-prefix/`.
