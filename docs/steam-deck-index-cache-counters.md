# Deck index-cache counter and reuse diagnosis, 2026-11-16

The [compact cache](steam-deck-index-cache-compact.md) retires fewer renderer
instructions but consumes more renderer cycles in a live off/on/off comparison.
Generic cache-miss events also increase. This supports investigating the cache's
memory cost; it does not establish a specific cache level, latency mechanism,
or an optimization gain. The cache remains disabled and stable 30+ FPS unmet.

## Live hardware counters

Three 90-second runs use the same candidate, SHA-256
`48298587d7ebc336efde0ccc29939582bfe527eb95b58964fee10e6f8a4e696b`, with cache
off/on/off. Each has a fresh save/cache, 1280×720, 100% scale, uncapped mailbox,
batch size 512, two draw contexts, zero pool spin and the usual scripted menu
inputs. User-mode cycles, instructions, cache references and cache misses are
counted per thread for approximately seconds 45–65. No call stacks are sampled.
These are diagnostic runs, not the clean FPS qualification.

All four events run without multiplexing. A private copy of the harness adds
monotonic timestamps to its existing presentation-counter reads. Those map the
observer's invocation into the renderer's frame epoch. The centered 20-second
windows contain 547 / 542 / 546 frames; allowing observer setup/teardown time
at either end gives bounds of 546–549 / 542–544 / 545–548. Invocation overhead
is 36–41 ms. All measured frame deltas are one. Effective settings match, all
runs complete beyond 88 seconds, and their post-window captures are intact.

Millions of events per normalized frame, renderer thread:

| Event | Off 1 | On | Off 2 | On versus mean off |
|---|---:|---:|---:|---:|
| Cycles | 76.407 | 78.769 | 77.171 | +2.58% |
| Instructions | 132.747 | 121.278 | 132.709 | −8.63% |
| Cache references | 7.361 | 7.768 | 7.493 | +4.59% |
| Cache misses | 0.606 | 0.671 | 0.605 | +10.67% |

Allowing the frame-normalization bounds still gives +2.01% to +2.95% cycles,
−9.13% to −8.29% instructions and +10.06% to +11.07% cache misses. The renderer's
instructions per cycle fall from 1.737/1.720 in controls to 1.540 with caching.
These counter events are reported by their perf names; no cache level or DRAM
transaction count is inferred from them.

Across the four geometry lanes, instructions change only +0.05% and cycles
+0.17%, within normalization uncertainty. Their cache-miss counts vary between
the two controls and do not show a consistent cache-on increase. This study
does not demonstrate a general slowdown of all geometry workers.

A verbose perf probe confirms `inherit=1`. Auxiliary `ch 701F0D00` threads restart
during observation, so new child work can be attributed to an existing parent
rather than its new TID. The analysis records those unreported TIDs and does not
interpret auxiliary-chain or parent aggregates as direct per-thread work. The
renderer and four geometry TIDs remain stable across their windows. No claim
of complete whole-process attribution is made.

## Live reuse by size

A separate 90-second diagnostic counts hits and misses by index-list size while
comparing every cached result with the original mapper on the same snapshot.
It checks 3,833,856 mappings with zero mismatches and captures an intact menu.
The analysis takes cumulative-counter differences between renderer seconds
40.044 and 69.868: 2,113,536 eligible mappings, 1,781,658 hits and 591,275,220
indices. Only packed mappings with 4–4,096 indices are eligible; this is not a
census of all renderer draws. Instrumentation and compiler-profile differences
make this unsuitable for FPS comparisons.

| Indices per mapping | Calls | Misses | Hit rate |
|---|---:|---:|---:|
| 4–15 | 311,026 | 4,476 | 98.56% |
| 16–63 | 594,150 | 35,699 | 93.99% |
| 64–127 | 217,569 | 19,481 | 91.05% |
| 128–255 | 273,055 | 68,809 | 74.80% |
| 256–511 | 427,926 | 144,102 | 66.33% |
| 512–1023 | 147,388 | 30,033 | 79.62% |
| 1024–4096 | 142,422 | 29,278 | 79.44% |

The 128–511 range accounts for 33.17% of eligible calls, 34.58% of input indices
and 64.15% of misses in this window. The earlier short capture independently
contains 5,223 distinct keys in that range, occupying 12,626,976 bytes of compact
entries: about 71% of its captured entry working set. The remaining 1,255 keys
occupy 5,195,548 bytes. Those short-capture working-set sizes are not sustained
live memory bounds or hit-rate predictions.

This supports a private selective-cache experiment: keep lists below 128 or
at least 512 indices, bypass the middle range before hashing/copying, and test
an 8 MiB entry budget with 8,192 slots and at most 4,096 entries. Original mapping
would remain the fallback, and exact key/output semantics would be retained.
This is a proposed next experiment, not implemented or qualified here. Its
changed admission and eviction behavior need full correctness checks, fresh
profiling and clean live comparisons before any promotion.

The [completed selective-cache experiment](steam-deck-index-cache-selective.md)
passes correctness checks but shows no useful live gain and is not promoted.

The diagnostic executable has SHA-256
`8e6a43b6c7df7a3799d3e4fa78e194a09a0550dd4c5cad2004091a86a83588a2`.
Its source changes only statistics around the already qualified private cache;
no production source or optimization default changes. After capture, the private
header, runtime archive and main object are restored, and relinking reproduces
the retained `d6420f0b…` reference exactly. The shared prepared executable,
installed executable and pinned SDK remain unchanged.

Artifacts are under
`/home/deck/buzz/performance/optimization/codex-index-cache-counters/` and
`/home/deck/buzz/performance/optimization/codex-index-cache-census/`.
