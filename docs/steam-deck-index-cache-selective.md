# Deck selective index-cache experiment, 2026-11-16

This private experiment follows the [live size census](steam-deck-index-cache-counters.md).
It tests whether reducing cache admission and memory use can turn the compact
cache's standalone benefit into a live improvement. Correctness passes, but
the live comparison shows no useful gain. It is not installed or enabled in
the retained runtime; stable 30+ FPS remains unachieved.

## Policy and correctness

The existing exact cache now admits lists of 4–127 or 512–4,096 indices. Lists
of 128–511 indices bypass it before snapshotting, hashing or allocating. The
requested-entry budget falls from 24 to 8 MiB, the slot array from 512 to
128 KiB, and the entry limit from 16,384 to 4,096. At most half the slots can
be occupied. Entry budget excludes the table, snapshot and allocator overhead.
Full byte/count/ordering equality, owned caller outputs, current-base copying,
LRU eviction and backward-shift deletion remain unchanged. The private CRC
hash still assumes Deck SSE4.2 support; portable runtime admission is not added.

ASan/UBSan passes 26,806 complete sequences per ordering mode, including new
127/128/511/512 admission boundaries. Tests check bypass counters, forced hash
collisions, table wraparound, byte-budget eviction, allocation failures,
mutable caller buffers and invalid inputs. An 18,000-key case forces 13,904
evictions at the 4,096-entry limit. The first sanitizer run stopped at a fixture
assertion that incorrectly assumed the smaller admitted random set must evict;
that assertion was corrected, and explicit forced-eviction tests remain.

All 62,944 captured mappings match. An integrated sanitizer fixture checks
three calls per record in each off/on/compare mode, including changed bases
and mappings that bypass admission: 188,832 calls per mode. Compare-mode cache
counters cover only 127,104 eligible calls, with 125,849 hits and 1,255 misses.
Those counters are not the total fixture call count.

## Standalone timing

One process compares the original mapper, full compact cache and selective
cache in original/full/selective/selective/full/original order. Each arm warms
the entire captured sequence and times five repetitions using process CPU time.
This compares the complete policies: full cache at 24 MiB, selective at 8 MiB.
All arms produce checksum 73,566,865. No game, compiler or profiler overlaps.

| Implementation | First / reversed ns per mapping |
|---|---:|
| Original | 768.053 / 765.546 |
| Full compact cache | 486.350 / 481.667 |
| Selective cache | 534.177 / 540.725 |

Selective admission is slower than caching the full warm capture but faster
than the original mapper. Its captured entries peak at 5,195,548 bytes, versus
17,822,524 for the full cache. The captured working set fits after warmup;
these timings do not predict sustained live hit rate or FPS.

## Game validation

The frozen private build reproduces retained reference SHA-256
`d6420f0b0905ed62958f6c42cfcbfe740d8851295484631d99460ccec49b5b56`
before edits. Fresh renderer PGO uses separate 90-second off/on menu runs.
The final compiler log has no stale-profile warning. Candidate SHA-256:
`24803bbc791e437011336a75f20f48a2db608b3a58f046ce91a13e3ef26e2198`.

A 335-second scripted run compares 6,930,432 eligible cached mappings against
the original result with zero mismatches. It reports 6,146,780 hits, 783,652
misses and 782,170 evictions, reaching the 8 MiB entry budget. Bypassed mappings
use the original implementation. This run provides functional validation;
its instrumentation and unsynchronized question phases preclude FPS claims.
Inspected captures show intact timed questions and a zero-score display. They
do not establish a successful scored answer or a separately verified answer
reveal. All nine inspected late-run captures are recorded in the artifact list.

## Live comparison and disposition

Six 90-second runs use reference/off/on/on/off/reference order, fresh save/cache
directories, 1280×720, 100% render scale, uncapped mailbox presentation, batch
size 512, two draw contexts and zero pool spin. FPS covers seconds 40–70, CPU
time seconds 45–65, and captures second 80. No builds, profilers, other games
or standalone benchmarks overlap the measurements.

The first attempted reference run stalled during startup after 74 presentations,
before reaching the menu. It produced no usable timing window or screenshot;
its logs are preserved separately and excluded before any candidate arm. The
restarted series completed its first five runs before the user paused work.
The final reference run followed resumption, about 6.5 minutes after the fifth
run ended. This is not an uninterrupted six-run series; both same-binary off/on
pairs precede the pause.

| Run | Mode | FPS | p95 frame ms | Minimum 1-second FPS | CPU ms/frame |
|---|---|---:|---:|---:|---:|
| A1 | Reference | 28.167 | 43.426 | 24 | 152.233 |
| C1 | Off | 27.767 | 44.350 | 24 | 152.449 |
| B1 | On | 27.967 | 43.331 | 25 | 152.780 |
| B2 | On | 27.767 | 43.595 | 23 | 152.910 |
| C2 | Off | 28.200 | 42.878 | 26 | 152.467 |
| A2 | Reference, after pause | 28.600 | 42.041 | 26 | 150.000 |

Mean FPS is 28.383 reference, 27.983 off and 27.867 on. Cache-on is 0.42% slower
than same-binary cache-off and consumes 0.25% more CPU time per frame. The FPS
arms overlap; this is insufficient evidence of either a useful gain or a
repeatable regression. Cache-on p95 intervals exceed 43 ms, and neither average
FPS nor short-window minima reach the target. The prototype is not promoted.

All six runs complete beyond 88 renderer seconds, measured presentation deltas
are one, effective settings match, error scans are empty, and all six captures
show intact menus. The private header, archive and main object match their
original backups, and the restored executable exactly matches the retained
reference. Shared and installed executable hashes are unchanged; the SDK is
clean at `79efc280dad27271d5f68077acb0247df1e88dc7`. All experiment drivers and
measured game processes have exited.

Artifacts are under
`/home/deck/buzz/performance/optimization/codex-index-cache-selective/`, including
prototype sources, sanitizer fixtures, build scripts/profiles, original and
resumed run drivers, logs, frame CSVs, telemetry, captures, `live-results.json`,
`settings-comparison.json`, `final-verification.json` and `artifact-hashes.json`.
This commit records the completed experiment; prototype code remains in that
local artifact directory and does not change production behavior.
