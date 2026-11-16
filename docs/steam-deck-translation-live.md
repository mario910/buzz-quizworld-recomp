# Exact translation shortcut: live Deck result

The exact translation-only object shortcut passed whole-job output checks,
but did not establish a useful menu FPS gain. It remains private and is not
promoted. A separate cache experiment that removes transformed-data copies
still loses time with simulated per-worker caches. Stable 30+ FPS remains unmet.

## Private integration and qualification

The [complete-preparation study](steam-deck-job-sensitivity.md) previously
validated the shortcut and measured about 2–4% less standalone preparation
time. The private integration changes only the host skin header, retaining
the original matrix admission, arithmetic for other transforms, normalization,
publication and fallback behavior. A thread-safe cached
`BUZZ_GEOMETRY_TRANSLATION=0/1` switch selects original/shortcut behavior.
This switch is not part of the production build.

The committed image-7 SIMD reference was reproduced byte-for-byte before the
header change. Only `native_geometry_job_flow.cpp` received refreshed PGO,
trained with 90-second disabled/enabled menus and a 335-second enabled round.
All three training captures were inspected and intact; the round reached a
green scoreboard with one correct answer. Effective settings match. Training
timings are excluded from performance evidence. The final build has no stale
profile warning or LLVM profile-generation sections.

| Artifact | SHA-256 |
| --- | --- |
| Reference | `d6420f0b0905ed62958f6c42cfcbfe740d8851295484631d99460ccec49b5b56` |
| Candidate | `6a7848c382dc1149b22a3267f017db2c8a888689d63630b7a8e59254e1b8ab77` |
| Geometry profile | `16e5e9a4d552eb532dec19d2e0fe5e0f3f594ab8944d590fb5a37d6e6078350d` |

Both switch settings match all 9,696 reference ordered-transfer/exit files
across image-38 question, image-39 menu, image-40 question, image-41
question/blend/padding/reveal and the fresh current-menu corpus. Each mode
executes 28,884 jobs, or 57,768 combined. The existing 348 concurrent-input
cases and 102 older capture failures remain unchanged; this does not claim
that those captures newly match their original recorded executions.

## Clean live measurements

Six 90-second menus use A/C/B/B/C/A ordering: A is the untouched reference,
C is the candidate disabled, and B is the same candidate enabled. Each run
uses a fresh runtime and shader cache, X11, 1280×720 at 100% scale, uncapped
mailbox, 512-draw batches, two contexts, zero worker spin, image-36 search and
image-7 conversion. Persistent chain workers are off. FPS covers seconds
40–70, CPU samples 45–65, and captures second 80. No build, replay, benchmark,
profiler or other game overlaps these runs.

| Arm | FPS | p95 ms | Lowest one-second FPS | CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 reference | 28.5000 | 41.153 | 23 | 151.5314 |
| C1 disabled | 28.7667 | 40.959 | 25 | 151.4917 |
| B1 enabled | 28.6667 | 40.412 | 27 | 151.0906 |
| B2 enabled | 29.0333 | 40.001 | 27 | 149.6000 |
| C2 disabled | 28.8000 | 40.681 | 23 | 151.6176 |
| A2 reference | 29.0000 | 40.672 | 26 | 150.0365 |

Mean FPS is 28.7500 reference, 28.7833 disabled and 28.8500 enabled. The
enabled lead is 0.23% over the same-build control and 0.35% over the reference,
within the observed run spread. CPU means are 150.7839, 151.5547 and 150.3453
ms/frame respectively: enabled is 0.80% lower than disabled but only 0.29%
lower than the reference. The better observed tails are insufficient evidence
of a repeatable net gain in this small series. About 64.5% of enabled intervals
still exceed 33.34 ms.

A separate enabled 335-second round measures:

| Window | FPS | p95 ms | Lowest one-second FPS | CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| Menu, 40–70 s | 27.8000 | 41.257 | 23 | 154.0037 |
| Question, 240–300 s | 36.3833 | 32.711 | 32 | 95.0047 |
| Late, 306–315 s | 25.8889 | 45.015 | 22 | 143.2039 |

It is unpaired and cannot establish a round-performance change. The final
capture shows an intact David Bowie question/green scoreboard with one correct
answer. All seven live captures were inspected and intact, effective settings
match, measured presentation deltas are one, and presentation continues to the
expected end. No searched crash, device-loss or nonzero native-mismatch marker
was found. Later captures do not certify every timed frame or camera phase.

## Borrowed object-cache results

The earlier [object cache](steam-deck-stream-loads.md) copied transformed data
into cache entries on misses and back into working storage on hits. A separate
private prototype fills an entry's dynamically sized buffer directly and
returns a const view on hits and successful fills. It invalidates the previous
key before filling, so failed decoding cannot leave stale data hittable.
Full metadata and input-byte comparisons remain mandatory. Weighted jobs use
the uncached path; mutable-blend jobs are excluded from execution in this
fixture. No game source was changed for this study.

Storage-generic capture/skin routines are derived from current source with
the same admission, loops and arithmetic. The copying control is rebuilt
alongside them with current bounded skin enabled; old timing logs are not
controls. Shared non-inlined arithmetic wrappers avoid duplicating the large
kernel for every cache capacity.

ASan/UBSan checks compare ready/pending state, all active and padded SoA blocks
and object coefficients against uncached computation. Menu/question/reveal
admit 1,493/1,349/3,879 of 1,500/1,500/4,000 files. Each of six configurations
also performs 272/261/751 stream/matrix mutations and invalid-selector checks,
including valid-input restoration after failed fills. Borrowed views are
consumed before the next access to the same cache. These checks do not prove
nested-job lifetime safety, concurrent cache access or whole-job publication.

Timing uses only the consecutive reveal corpus. Every arm starts with empty
caches and traverses it once; a second fresh-cache repetition reverses the
copy/borrow order. Within each traversal, uncached/cached call order alternates
per job. Decompression, recipe admission and comparisons are outside timing.
The table averages calls after the first 1,000 captures:

| Simulated layout | Cache policy | Own uncached control ns | Cached ns | Change |
| --- | --- | ---: | ---: | ---: |
| Four caches × 64 cells | Copy | 3,776.675 | 4,612.830 | +22.14% |
| Four caches × 64 cells | Borrow | 3,795.765 | 4,582.675 | +20.73% |
| Four caches × 256 cells | Copy | 3,795.345 | 4,222.735 | +11.26% |
| Four caches × 256 cells | Borrow | 3,793.400 | 4,121.965 | +8.66% |
| One serial cache × 1,024 cells | Copy | 3,842.805 | 3,735.630 | -2.79% |
| One serial cache × 1,024 cells | Borrow | 4,080.815 | 3,868.615 | -5.20% |

The four-cache configurations use round-robin assignment, not measured worker
placement. Their 106/384 hits, 1,881/1,603 misses and 1,892 bypasses are identical
between copying and borrowing in timing runs. Removing the copies is not
enough to beat recomputation. Retained payload capacity is about 9.0/14.9 MB.

The single-cache probe has 1,066 hits and 921 misses with the same 1,892
bypasses, retaining about 8.3 MB. It is serial and includes no synchronization
cost. Its borrowed arm varies from 2.30% to 8.52% faster than its own control;
the controls themselves vary substantially. This does not justify a shared
live cache or establish real per-worker reuse. No cache variant is integrated.

## Evidence and restoration

Private evidence lives under `/home/deck/buzz/performance/optimization/`:
`codex-translation-live/` contains generation/build/profile scripts, frozen
executables, replay outputs, three training runs, seven clean runs, audits and
summaries. `codex-object-cache-borrow/` contains source generation/hashes,
fixtures, sanitizer/compiler logs and fresh-cache timing results. The latter
starts only after the clean live driver completes and exits.

All drivers have completed and exited. The prepared executable remains
`630007cb…`, installed executable `189814d1…`, and SDK clean at `79efc280…`.
The committed SIMD build remains the reference; neither experiment is promoted.
