# Deck exact index-remap cache, 2026-11-16

The private cache passes complete mapping comparisons but regresses live menu
performance. It remains disabled and is not installed or added to the production
renderer. Stable 30+ FPS is still unproven.

## Design and correctness

Following the [index-stream census](steam-deck-index-reuse-capture.md), this
prototype snapshots up to 4,096 big-endian indices, hashes them, and verifies all
bytes plus count and ordering mode before reuse. It stores packed vertex IDs and
occurrence mappings, applying the current base when copying results into owned
consumer buffers. Misses run the original mapper on the same snapshot. Hash
collisions therefore cannot substitute another mapping; caller mutation cannot
modify cached results. Invalid or oversized inputs retain the original path.

The LRU has 8,192 buckets, at most 16,384 entries and a 24 MiB requested-allocation
budget. That budget includes entry headers but excludes allocator overhead,
fragmentation, the bucket table and snapshot storage; it is not an RSS bound.
Eviction frees the oldest entry. Cache allocation failure keeps the computed
original result. The private x86 hash uses SSE4.2 CRC instructions; a portable
production version would also need CPU capability admission.

ASan/UBSan checks pass 6,754 sequence cases in each ordering mode, including
changed bytes and bases, split/empty spans, forced collisions, eviction, invalid
inputs, zero budget and cache allocation failure. All 62,944 captured mappings
match exactly. An integrated fixture repeats the capture three times per mode
(off, cache, cache plus comparison), checking every vertex reference and
occurrence, including base changes. All modes pass under ASan/UBSan. The folder
named `asan-final` was accidentally built without sanitizers; only the subsequent
`asan-verified` and `integrated-asan` runs support these sanitizer claims.

A 335-second live comparison run checks 10,633,216 mappings with zero mismatches.
It reports 9,116,265 hits, 1,516,951 misses and 1,511,880 evictions, reaching the
24 MiB entry budget. Captures show intact menus, a question with four answers,
and a scoreboard with one point. This establishes functional coverage, not
matched question/reveal FPS.

## Timing

Captured-stream replay initially suggested a benefit: two reversed comparisons
at 24 MiB reduce mapping CPU time from approximately 738–741 ns/draw to
522–536 ns/draw (about 28–29%). Each arm warms the complete captured sequence,
then times five repetitions. Identical checksums cover every result. Once warm,
this short captured working set fits entirely; that differs from the live game's
ongoing misses and eviction. A 32 MiB budget offers no meaningful further replay
benefit. These are standalone results without PGO, not game FPS.

The game candidate uses fresh renderer PGO trained with cache off and on. Final
compilation emits no profile hash mismatch warnings. Six clean runs compare the
unchanged reference (A), candidate off (C), and candidate on (B), in A/C/B/B/C/A
order. Each uses a fresh save/cache, 1280×720, 100% scale, uncapped mailbox
presentation, batch size 512, two draw contexts and zero pool spin. FPS covers
seconds 40–70; CPU time per frame covers 45–65; screenshots occur after the measurement window.
No builds, profilers, or other game runs overlap these measurements.

| Run | Mode | FPS | p95 frame ms | Minimum 1-second FPS | CPU ms/frame |
|---|---|---:|---:|---:|---:|
| A1 | Reference | 28.533 | 40.740 | 25 | 151.818 |
| C1 | Off | 28.267 | 41.678 | 24 | 152.639 |
| B1 | On | 28.000 | 41.087 | 25 | 155.417 |
| B2 | On | 27.967 | 41.583 | 25 | 154.082 |
| C2 | Off | 28.333 | 41.485 | 24 | 152.232 |
| A2 | Reference | 28.833 | 40.143 | 27 | 150.806 |

Mean FPS is 28.683 reference, 28.300 off and 27.983 on. Relative to the same
binary with caching off, caching loses 1.12% FPS and increases CPU time per frame
by 1.52%. Effective renderer settings match across all runs. Every run reaches
88 seconds, measured presentation deltas are one, screenshots show intact
selection menus, and the error scan finds no reported sanitizer errors, mapping
mismatches, crash signatures or Vulkan device loss. No optimization is promoted.

## Follow-up diagnosis

Separate off/on/off profiles sample seconds 45–65 and read thread runtime
counters. Renderer CPU time per presented frame is 25.372 / 26.956 / 25.602 ms;
whole-process thread runtime totals are 158.083 / 163.036 / 159.242 ms/frame.
Profiling adds overhead, so these figures are diagnostic and do not replace the
clean comparisons above. The added cost is not confined to the renderer.

In the cache-on profile, 101 samples land in the cache wrapper. Approximately
57% of its sampled cycle weight is attributed near bucket/entry loads and hash
comparison, and roughly 24% near the scalar vertex-reference copy loop. This
suggests investigating metadata locality and result materialization, but the
small sample and instruction skid do not prove a particular cache-miss mechanism.
Original mapping still accounts for work on cache misses, and hashing/library
copies have separate samples.

An offline hash-distribution check finds no severe concentration among the
6,478 distinct captured keys: 4,466 of 8,192 buckets are used, the largest has
seven keys, and the mean bucket size seen by a distinct key is 1.800. Mixing both
CRC halves changes that mean only to 1.785 or 1.752 in two simple alternatives.
This uses the enabled rank-mode setting, including inputs that internally fall
back to first-use ordering. It does not measure live probe depth, but provides
no evidence that a small hash change would solve the regression. A future prototype should address the
pointer-heavy metadata and copying cost, then repeat correctness and live checks;
the current implementation remains unpromoted.

## Reproduction and artifacts

Artifacts are under
`/home/deck/buzz/performance/optimization/codex-index-cache/`: cache and integration
sources, test generators, compiler commands/logs, sanitizer fixtures, replay
timings, fresh profiles, live scripts, frame CSVs, screenshots and result JSON.
`make.py`, `build.py`, `make-integration.py`, `integrated-build.py`, `game-build.py`,
`pipeline.py` and `live.py` preserve the experiment workflow. These are private
on-device artifacts, not portable production build commands.

Candidate SHA-256:
`dff25244e58fac9a60882e9727f9a05519c0776f33c18852351b4aba7fd70ad6`.
The frozen private renderer source, runtime archive and main object are restored
after building the candidate, reproducing reference SHA-256
`d6420f0b0905ed62958f6c42cfcbfe740d8851295484631d99460ccec49b5b56`.
The shared prepared executable remains the previously observed, unqualified
`9f244bf8e3c948d83e7eb5017b6f177e13c440f366cc3f908f551deb940cd763`.
The installed executable remains
`189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`;
the pinned SDK stays clean at `79efc280dad27271d5f68077acb0247df1e88dc7`.
`final-verification.json` records these checks and confirms all experiment
drivers have exited; `artifact-hashes.json` identifies the retained source,
profile and result files.

The [compact-table follow-up](steam-deck-index-cache-compact.md) implements
that investigation; its correctness checks pass but live FPS still does not
improve usefully. Neither cache is promoted.
