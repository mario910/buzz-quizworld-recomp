# Steam Deck geometry stream loads, 2026-11-15

Replacing AVX2 gathers with ordinary unaligned word loads accelerated isolated
input decoders but did not demonstrate a useful game FPS improvement. The
candidate and tests are archived; production headers were restored. The
installed executable remains SHA-256
`189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.

## Change and validation

The candidate read seven complete words for packed directions, retaining the
exact three-byte final read, and eight complete words for paired halfword
attributes. It preserved unaligned access, halfword prefix-byte provenance,
inactive lanes and all arithmetic. Non-x86 code retained its gather path.

It passed 90,504 differential, guarded-page cases against the original headers
under ASan/UBSan. The integrated stream fixture also passed its 73,728 float,
9,216 position, 144 halfword and 54 direction cases, plus 32,640 new guarded
seven/eight-word cases. The build had no PGO mismatch warnings.

All 8,196 complete replay transfer streams and exit controls matched the
reference dumps retained in `codex-dma-range/outputs-0`. Original capture
verdicts were unchanged, including the existing 51 question, two blend and
49 padding discrepancies documented in [DMA experiments](steam-deck-dma-experiments.md).
These are differential checks, not 8,196 exact matches to original captures.

Alternating isolated decoder measurements on the Deck showed packed direction
reads around 2.9 ns versus 6.5 ns, and two-component halfword decoding around
11.5–12 ns versus 16.8 ns. These warm-input kernel results exclude job staging,
other geometry work and scheduling; they are not an FPS gain.

## Live results

Sequential uncapped runs used 1280x720 at 100% scale, cold shader caches and the
same scripted inputs. No builds, correctness benchmarks or sampling profilers
ran concurrently. The effective renderer settings matched: VANGOGH, mailbox,
512-draw batches, two draw contexts and zero worker spin time. The profile
checker needed a parser repair for the SDK's current `2-context` log spelling;
it still rejects missing, conflicting or non-two context counts.

The four 90-second menu runs measured the script-relative 40–70 second window:

| Arm | FPS | Median interval ms | p95 interval ms | CPU ms/frame |
|---|---:|---:|---:|---:|
| A1 installed | 26.07 | 37.943 | 46.241 | 162.32 |
| B1 ordinary loads | 26.30 | 37.998 | 44.943 | 163.30 |
| B2 ordinary loads | 25.97 | 38.290 | 46.530 | 162.71 |
| A2 installed | 26.17 | 37.972 | 45.440 | 164.08 |

Means: 26.12 versus 26.13 FPS. Aggregate process CPU time, sampled within
45–65 seconds, was 163.20 versus 163.00 ms/frame. Neither establishes a useful
whole-game improvement.

An additional exploratory 335-second run of each executable gave:

| Build | Heavy menu FPS | Question FPS | Reveal FPS | Reveal p95 ms | Lowest reveal one-second FPS |
|---|---:|---:|---:|---:|---:|
| Installed | 26.20 | 35.32 | 24.89 | 47.831 | 23 |
| Ordinary loads | 26.27 | 35.42 | 25.11 | 47.175 | 23 |

Windows were 40–70, 240–300 and 306–315 seconds. Screenshots at 330 seconds
showed correctly rendered scoreboards but different answer outcomes (zero
versus one correct answer). This pair is not an identical-scene comparison;
its small differences cannot be attributed to the code change. Screenshots
after the measurement windows also do not certify each measured frame's phase.
The candidate was not promoted or installed. Stable 30 FPS remains unachieved.

## Artifacts and next evidence

`/home/deck/buzz/performance/optimization/codex-geometry-streams/` retains the
source, patch, tests, frozen executable, build log, replay dumps and summaries,
measurement scripts, raw CSVs, telemetry, logs and screenshots.

`reuse-study.cpp` separately examines exact vertex-input identities before
projection; it never substitutes cached results. In 4,000 consecutive reveal
captures, 3,879 non-blend supported jobs passed decoding/skinning. Among them,
3,236 repeated their decoder inputs and 1,788 repeated the complete inputs to
skinning/object transformation. Counts are global across workers and include
arbitrarily old entries within that corpus: an upper bound, not a bounded-cache
hit rate or performance result. It excluded 109 mutable blend jobs and 12
unsupported-storage jobs. This differs from whole-job output reuse, which
previous experiments ruled out. Any cache still needs full-byte validation,
bounded memory, mutation tests and measurements including lookup/copy costs.

Two private bounded-cache prototypes were subsequently measured on the reveal
corpus. Both used four simulated workers assigned jobs round-robin, four-way
sets, sampled fingerprints followed by complete metadata/input comparisons,
and 64/128/256 cells per worker. Each cached result was compared byte-for-byte
with the uncached kernel. This assignment is not evidence of actual lane
placement or a live hit rate.

- Decoder-only caching, after the first 1,000 jobs: uncached 1.90–1.95 us
  versus cached 2.42–2.92 us per call. It retained about 9.9–16.7 MB of payloads
  across the four caches. Complete contiguous input spans included unused
  bytes, so its hit counts were lower than the canonical-input upper bound.
- Object-transform caching, including decode and transform costs: uncached
  3.84–4.08 us versus cached 4.56–4.72 us. Weighted and blend jobs did not enter
  the object cache; lookup, fill and copy costs outweighed the saved work.

Neither prototype entered the production tree or a live FPS test. The private
`decode-cache-bench.cpp`, `object-cache-bench.cpp` and their logs preserve the
implementations and measurements. These results reject these specific cache
designs, not every possible reuse strategy.

The later [borrowed-buffer study](steam-deck-translation-live.md) removes
transformed-result copies on both misses and hits. It still loses to
recomputation with simulated per-worker caches. A single serial cache shows a
modest, variable lead without synchronization costs; no live cache is promoted.

A later [packed-direction arithmetic and table study](steam-deck-unit-decoder.md)
validated all packed inputs but found that large exact magnitude tables slowed
streaming captured workloads. Those variants also remain unintegrated.
