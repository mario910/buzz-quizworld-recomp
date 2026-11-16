# Geometry reuse with recorded worker assignments

This follows the [borrowed object-cache prototype](steam-deck-translation-live.md).
That study simulated four workers with round-robin job assignment. This study
captures actual executor IDs and uses each worker's recorded job subsequence.
The cache remains a private standalone prototype, with no game integration.

## Capture and its limits

`port/job_corpus.cpp` now adds `host_tid` to JSON sidecars. It records the host
thread ID at job entry, before any DMA observer can run on another thread.
The v1 binary corpus format is unchanged. IDs identify threads within one run;
they are not persistent lane identifiers across processes.

The committed image-7 SIMD reference was reproduced byte-for-byte before
changing only the recorder. Reference SHA-256 is
`d6420f0b0905ed62958f6c42cfcbfe740d8851295484631d99460ccec49b5b56`;
the diagnostic executable is
`96351a0551d409a2032c9368966b31ca4612ebd784b66745efce14ef10528ac0`.

| Capture | Consecutive jobs | Entry times after boot | Jobs on four main workers | Fifth thread |
| --- | ---: | --- | --- | ---: |
| Menu | 32,768 | 40.000–88.966 s | 8,195 / 8,256 / 8,143 / 8,163 | 11 |
| Late round | 16,384 | 308.000–333.985 s | 4,070 / 4,107 / 4,056 / 4,099 | 52 |

Every sequence number and sidecar is present. A pre-capture thread snapshot
identifies the four main late-round executors as `lane1`–`lane4`. Both images
were inspected: the menu is intact, and the late scene shows the Mona Lisa
question with a green stage and a zero score. Presentation continues through
89.84/334.98 seconds. Effective settings match: 1280×720, 100% scale, uncapped
mailbox, 512-draw batches, two contexts and zero worker spin.

Synchronous compression and file writing greatly slow these captures. They
record real worker assignments, but their scheduling and scene pacing differ
from an uninstrumented game. Their FPS is not performance evidence, and their
reuse rates cannot establish live hit rates. Later screenshots do not certify
every captured frame.

## Standalone checks and timing method

The fixture derives from the previous borrowed-cache study with the same
complete metadata/input-byte identity, object-only admission, bounded skin,
failure invalidation and uncached weighted-job fallback. Mutable blend jobs
remain excluded. It compares ready/pending state, every active and padded SoA
block, and object coefficients against uncached computation.

Five configurations pass ASan/UBSan and checked-library assertions. They admit
32,596 menu and 15,832 late-round jobs. Each configuration additionally performs
6,016/2,856 stream-byte and matrix-byte mutations, invalid packed-unit selectors,
and valid-input restoration checks. Borrowed views are consumed before the
next call to that cache; nested-job lifetimes and concurrent cache access are
not tested.

Files are ordered by numeric sequence, since four-digit minimum filenames
sort incorrectly as strings above 9,999. Each local cache follows the recorded
host thread ID, including the fifth executor. The serial shared-cache probe
uses global entry order and includes no synchronization cost.

Each timing arm starts with empty caches and traverses the sequence once.
A second fresh traversal reverses configuration order. Uncached/cached call
order alternates within each job; decompression, admission and output checks
are outside timing. Arithmetic wrappers are shared across capacities. No game,
build, sanitizer, profiler or replay overlaps the standalone timing runs.

## Results and decision

The table averages the two fresh-cache traversals after the first 1,000
captures. Positive changes mean the cache takes longer. Each row uses its
own uncached control; these are kernel times, not FPS measurements.

| Scene | Policy | Cells per cache | Placement | Uncached ns | Cached ns | Change |
| --- | --- | ---: | --- | ---: | ---: | ---: |
| Menu | Borrow | 64 | Recorded worker | 3,187.68 | 4,270.05 | +33.95% |
| Menu | Copy | 256 | Recorded worker | 3,148.69 | 3,977.01 | +26.31% |
| Menu | Borrow | 256 | Recorded worker | 3,078.41 | 3,779.58 | +22.78% |
| Menu | Borrow | 1,024 | Recorded worker | 3,106.77 | 3,044.69 | -2.00% |
| Menu | Borrow | 1,024 | Serial shared | 3,104.80 | 3,068.56 | -1.17% |
| Late | Borrow | 64 | Recorded worker | 3,906.32 | 4,888.07 | +25.13% |
| Late | Copy | 256 | Recorded worker | 3,910.39 | 4,517.51 | +15.53% |
| Late | Borrow | 256 | Recorded worker | 3,813.69 | 4,167.27 | +9.27% |
| Late | Borrow | 1,024 | Recorded worker | 3,780.20 | 3,572.91 | -5.48% |
| Late | Borrow | 1,024 | Serial shared | 3,778.91 | 3,394.63 | -10.17% |

The 1,024-cell worker-local cache has 11,821 menu hits, 8,342 misses and 12,433
bypasses; late-round counts are 4,570 / 3,088 / 8,174. Its retained payload
capacity is 44.1 MB / 28.8 MB. The smaller worker-local caches lose time even
without copying transformed results. Timing hit/miss counts repeat exactly.

The large worker-local cache saves only about 62 ns/menu job and 207 ns/late
job in this isolated capture-and-skin fixture. Recipe preparation, clipping,
publication and other game work remain outside those savings. The serial
shared-cache result excludes synchronization and safe concurrent borrowed-view
lifetimes. Together with the substantial capture-induced pacing changes,
these results do not justify a runtime cache integration. No cache variant is
promoted, and stable 30+ FPS remains unmet.

## Replay and retained evidence

All 49,152 new captures replay exactly against the unchanged reference:
32,768 menu and 16,384 late-round jobs, with zero concurrent-input exceptions
and zero failures. This also verifies that adding a sidecar field preserves
v1 corpus compatibility. No searched capture-write, crash, sanitizer,
device-loss or nonzero mismatch marker appears in the two capture logs.

Private artifacts are in
`/home/deck/buzz/performance/optimization/codex-geometry-worker-capture/`:
build/restoration scripts and hashes, frozen executables, both corpora,
thread snapshot, captures/settings audits, generated cache fixture, sanitizer
logs, reversed timing results, summaries and whole-job replay logs.
All drivers completed and exited. The prepared executable is restored exactly
to `630007cb…`; installed executable `189814d1…` and SDK `79efc280…` are unchanged.
Only the optional recorder metadata change and this evidence are retained.
