# Deck compact index-cache experiment, 2026-11-16

This private follow-up addresses lookup and copying costs observed in the
[original cache](steam-deck-index-cache.md). It is not a production feature.
Correctness passes, but six clean runs show no useful FPS improvement. It stays
disabled and uninstalled; stable 30+ FPS remains unachieved.

## Implementation and correctness

The candidate replaces 8,192 linked hash buckets with 32,768 contiguous slots,
each containing a full 64-bit hash and entry pointer. Linear probing rejects
other hashes before dereferencing their heap allocations. Backward-shift deletion
preserves probe reachability without tombstones. The existing 16,384-entry limit
keeps occupancy at most one half. Entries retain the same LRU policy and full
byte/count/ordering comparisons. Caller buffers remain owned and writable.

On x86-64, SSE2 interleaves four cached vertex IDs with the current base into
four complete references; a scalar tail handles the remainder. Compile-time
layout assertions guard the eight-byte reference representation. Other targets
retain scalar copying. As before, the private x86 CRC hash requires SSE4.2;
portable production admission is not implemented by this experiment.

The slot array occupies 512 KiB, compared with the previous 64 KiB bucket table.
Entry headers shrink by eight bytes. The 24 MiB requested-entry allocation budget
still excludes the slot array, snapshot, allocator overhead and fragmentation.
It is not a whole-process memory bound.

ASan/UBSan checks pass 26,802 complete mapping sequences in each ordering mode.
They cover byte/base changes, split and empty spans, collisions forced at both
the beginning and end of the probe table, eviction across wraparound, cache
allocation failure, zero budget and invalid inputs. An 18,000-key case exceeds
the entry limit while leaving ample byte budget. Audits check LRU linkage,
allocation totals, slot ownership and reachability from each entry's home slot.
Consumer-buffer mutation must not change subsequent cached results.

All 62,944 captured records also match exactly. An integrated sanitizer fixture
performs three calls per record in each runtime mode (off/on/compare), including
changed bases and split spans. All vertex references and occurrences match.
The fixture generator initially omitted an assert header; compilation failed
before execution, and the corrected generator/run logs are retained.

## Standalone comparison

One process compares the original mapper, previous linked cache and compact
cache in original/linked/compact/compact/linked/original order. Each arm warms
the complete captured sequence, then times five repetitions at a 24 MiB budget
using process CPU time. All arms produce checksum 73,566,865. No concurrent
game, compiler or profiler runs during this comparison.

| Implementation | First / reversed ns per draw |
|---|---:|
| Original mapper | 739.271 / 744.747 |
| Previous linked cache | 590.876 / 615.824 |
| Compact cache with SSE2 copies | 490.260 / 491.089 |

Both caches report 371,186 hits and 6,478 initial misses across warmup and timed
repetitions. The short captured working set fits completely after warmup; this
is not evidence of sustained live hit rate or game FPS. The compact payload
peak is 17,822,524 bytes versus 17,874,348 for the previous cache, excluding each
implementation's table and allocator overhead.

## Game validation

The experiment uses the frozen private workspace. Recompiling and linking the
original renderer first reproduces retained reference SHA-256
`d6420f0b0905ed62958f6c42cfcbfe740d8851295484631d99460ccec49b5b56`.
The changed renderer receives fresh PGO from separate 90-second off/on menu
workloads; other translation units retain their original profiles. The final
compile reports no stale-profile warning (an unused reset-helper warning is
retained). Candidate SHA-256:
`48298587d7ebc336efde0ccc29939582bfe527eb95b58964fee10e6f8a4e696b`.

The 335-second live comparison completes with 10,747,904 checked mappings and
zero mismatches. It reports 9,186,210 hits, 1,561,694 misses and 1,556,316
evictions, reaching the requested-entry budget. Captures show an intact timed
question, answer reveal and scoreboard with zero points. This is functional coverage; it does
not establish a successful scored answer or matched question/reveal FPS.

The private header, archive and main object are restored afterward, and relinking
again reproduces the retained reference exactly. Six sequential clean runs use
reference/off/on/on/off/reference order, fresh save/cache directories, 1280×720, 100% scale, uncapped mailbox presentation,
batch size 512, two draw contexts and zero pool spin. FPS covers seconds 40–70,
CPU measurements 45–65, and captures second 80. No builds, profilers, other game
runs or standalone benchmarks overlap these windows.

| Run | Mode | FPS | p95 frame ms | Minimum 1-second FPS | CPU ms/frame |
|---|---|---:|---:|---:|---:|
| A1 | Reference | 28.600 | 41.664 | 23 | 150.367 |
| C1 | Off | 28.667 | 40.425 | 25 | 151.468 |
| B1 | On | 28.367 | 41.793 | 24 | 152.292 |
| B2 | On | 28.533 | 40.155 | 26 | 152.615 |
| C2 | Off | 28.433 | 43.159 | 22 | 150.804 |
| A2 | Reference | 28.833 | 39.578 | 26 | 151.156 |

Mean FPS is 28.717 reference, 28.550 off and 28.450 on. Cache-on is 0.35% slower
than same-binary cache-off and consumes 0.87% more CPU time per frame. Differences
are small and overlapping, and provide no useful gain. The second cache version
cannot be compared directly with the first version's means from a different
series. All six runs progress beyond 88 seconds with measured presentation
deltas of one; effective settings match, error scans are empty, and all six
captures show intact menus. The prototype is not promoted.

## Replay limitations and further diagnosis

The original capture stores every occurrence separately in a 134 MB record
buffer. A follow-up interner gives identical logical input bytes the same owned
storage while preserving draw order, count, base and expected outputs. This
reduces input payload storage to 4,579,176 bytes across 6,478 unique streams.
The complete captured replay still passes ASan/UBSan. It changes memory locality,
not the cache's equality rule; this is a diagnostic variation, not a claim that
the live game's addresses behave identically.

The same reversed comparison with shared input storage gives 566.651/595.649
ns/draw for the original mapper, 476.271/464.253 for the linked cache and
397.136/397.075 for the compact cache. All checksums remain equal. All three
become faster, while the relative cache advantage remains; input storage alone
does not reproduce or explain the live regression.

A separate fixture builds all three implementations with fresh PGO trained on
both storage layouts, then validates all captured outputs before timing. No
stale-profile warnings occur. These are standalone fixture profiles, not game
profiles. The reversed results are:

| Input storage | Original ns/draw | Linked cache ns/draw | Compact cache ns/draw |
|---|---:|---:|---:|
| Separate occurrences | 774.942 / 776.298 | 524.504 / 526.487 | 468.763 / 467.730 |
| Shared identical inputs | 548.239 / 552.662 | 452.825 / 462.085 | 393.588 / 394.921 |

Fresh fixture profiles also preserve the relative replay advantage. Neither
follow-up isolates the reason for the live result. The next diagnostic should
measure the live working set and hardware counters before another cache change.

The installed perf tool advertises AMD IBS load/store sampling, but rejects
per-thread recording for this PMU. A noninteractive system-wide probe could not
start because sudo requires a password; no settings were changed and no IBS
samples were collected. Ordinary per-process user-mode cycles, instructions,
cache-reference and cache-miss counters work on an owned replay process. These
capability checks are not performance results or proof of a cache-miss cause.

Private artifacts are in
`/home/deck/buzz/performance/optimization/codex-index-cache-compact/`.

`final-verification.json` confirms restored private inputs, the unchanged
installed executable, unchanged shared prepared executable and clean pinned SDK.
`artifact-hashes.json` records source/profile/result identities. The shared
prepared executable remains unqualified; no runtime candidate was installed.

The [live counter and size-reuse diagnosis](steam-deck-index-cache-counters.md)
finds fewer renderer instructions but more cycles and cache-miss events. It
identifies a narrower admission policy to test; no FPS gain is claimed.
