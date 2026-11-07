# Content-verified geometry index cache

`BUZZ_GEOMETRY_INDEX_CACHE` is an experimental, default-OFF producer-side
optimization. It reuses decoded image41 triangle indices when the complete
staged source bytes and index count match an owned cache entry. It changes no
triangle order, indices, padding, admission decision or guest-visible state.
Projection, skinning, culling, allocation, DMA and command publication still
execute normally. Vertex data is never cached by this option.

Each worker owns 256 cells in 64 four-way sets. Only sources of 8–8192 bytes and
1–4096 triangles participate. Each cell retains owned source bytes and three
decoded index components. Larger jobs use the ordinary decoder. Allocations
occur on cache population/growth; warmed hits only compare and copy bytes.
Within a set, the least recently used entry is replaced after a successful
decode. Failed decodes leave previous entries intact. Recency is only an
eviction policy; equality checks remain mandatory even after a counter wrap.
Memory use depends on worker count, retained source sizes and vector capacity;
the logical source/index payload is at most 8 MiB per worker, with allocator
capacity potentially higher. This is larger than the original 64-cell cache.

The sampled fingerprint only selects a cell. Every hit requires full source
byte equality, index-count equality, current source-span/family validation and
current guest codec-table validation. A source pointer, LS bank address or
previous successful admission is insufficient. Invalid data follows the
ordinary decoder and its rejection reason; failed decodes cannot populate a
cell. No cache state is shared between workers or persisted to disk.

## Enabling and disabling

- CMake, Windows or Linux: `-DBUZZ_GEOMETRY_INDEX_CACHE=ON`.
- Windows build helper: `-GeometryIndexCache` with the existing native geometry
  build arguments. Omitting the switch explicitly resets the CMake option OFF.
- Android: `-PbuzzGeometryIndexCache=true` with the existing NEON host geometry
  profile. The property defaults false and reference29 explicitly keeps it OFF.
- A compiled-in cache can be disabled before launch with `BUZZ_INDEX_CACHE=0`.
  `BUZZ_INDEX_CACHE=1` enables it; unset also enables a compiled-in cache.
- `BUZZ_INDEX_CACHE_STATS=1` prints cumulative per-worker eligible lookup/hit
  counts every 65536 lookups. Use it only for diagnostics, outside FPS cohorts.

## Correctness evidence

The 19508 captured jobs pass cold/warm cache comparisons on Windows clang-cl,
Linux x86 and ARM64/QEMU, including 67110 input/codec/span mutations per
architecture and poisoned output buffers. The original lifted index decoder
also matches all 19745661 real indices with the cache compiled ON on Linux x86;
264 payload/bias boundary cases and 18 malformed-input rejections pass. This
does not constitute Android hardware speed or full-gameplay qualification.
The four-way variant also passes 448 valid delta-payload variants across 32
same-fingerprint sets on each architecture, exercising all ways, repeated
evictions and warmed lookups against the uncached decoder.

`tests/geometry-index-cache-test.cpp` uses the same private image41 corpus as
the other producer tests. It is included in `tests/scripts/test-offline-cpu.py`
when `--corpus` is supplied. The original-decoder oracle test remains
`tests/geometry-host-index-job-test.cpp`.

## Performance qualification

The original 64-cell locked-console diagnostic run shows approximately 55% eligible cache hits
in the fixed animated menu workload. This is a hit-rate observation, not an
FPS result. The same immutable reference14 EXE remains the reference; a separate
incremental control is the maintained native image39-request build.
The measurements below qualify that original implementation. Four-way/256-cell
performance must be qualified separately; an offline ownership simulation or
larger hit rate alone is not proof of higher game FPS.

Six matched PC runs pass all executable/profile/window/locked-session guards;
all six Vulkan readbacks show the intended menu without an obvious defect.
The same private save seed, scripted inputs, actual 1024x576 client extent,
1280x720 render extent, mailbox mode, two contexts and warmed shader cache are
used. No build, correctness test, profiler or second game overlaps the FPS
cohort. The 35-second readback is outside the timed windows.

| Build | Heavy 45–60s FPS, two runs | Pooled 45–90s FPS |
|---|---|---|
| Immutable reference14 | 38.0918 / 37.7481 | 45.1329 |
| Previous maintained request build | 39.0205 / 38.9309 | 47.4768 |
| Index cache candidate | 39.4106 / 39.2842 | 47.8464 |

This is a small improvement: about 0.37 FPS in the heavy window and 0.78% over
the longer window relative to the maintained control. The longer-window
difference is comparable to run-to-run variation; do not treat it as a large
or universally established FPS gain. Candidate P99 frame times are 30.467 and
30.807ms, and about 47% of longer-window intervals exceed 22.222ms. Stable
45 FPS has **not** been demonstrated. These measurements cover the animated
menu, not question/answer/scoring progression or Android hardware.

Frozen executable SHA256 values (private artifacts under `analysis/`):

- reference14: `d66601b4c612eb4006597d285e9d247ed538692d269cadb1448219179fb903fb`
- maintained request: `97ea57736969a567830466878525a38a254a3b1d20775c4fa97e71bcd2296d9f`
- index cache: `0c478568f2a02c658312984a9f6b2fb68e742654070792e76903c8cf32f6dfa8`

Separate CPU measurements use the same index-cache EXE with the runtime cache
OFF/ON, so compiler layout is held constant. Four 72-second runs use OFF/ON/ON/OFF
order and owned persistent process/thread handles during 45–65s. All four
profile/window/session/hash guards pass; no compilation, correctness test, sampling profiler or
other game overlaps those windows. There is no thread suspension or enumeration
inside the measured interval. Summed CPU time is not frame wall time.

| Measurement | Cache OFF | Cache ON | Reduction |
|---|---|---|---|
| Process CPU ms/frame | 133.1265 | 130.0291 | 2.33% |
| Process cycles/frame | 487.787 million | 480.357 million | 1.52% |
| SPURS pool CPU ms/frame | 63.4516 | 61.9339 | 2.39% |
| SPURS chain CPU ms/frame | 22.5716 | 22.2203 | 1.56% |
| Process busy cores, uncapped | 5.4648 | 5.3832 | 1.49% |

Per-run process CPU ms/frame: OFF 133.9667 / 132.2904, ON 131.0764 / 128.9817.
The RSX aggregate falls from 16.0818 to 15.0211 CPU ms/frame, but the individual
runs overlap; do not attribute a repeatable RSX improvement to this SPU cache.
Estimated non-game load is 2.77–2.98 busy cores in OFF runs versus 3.01–3.09 in
ON runs. These are CPU-work measurements, not electrical power measurements.

Private raw artifacts and analysis scripts remain under `analysis/pc45-image39/`:
`cache-{current,previous,word}-{1,2}`, `cache-cpu-{previous,power}-{1,2}`,
`cache-{pooled,phase-results,health-check,profile-check,cpu-summary}.json`.
`previous`/`power` in the CPU files mean runtime OFF/ON on the same EXE; the FPS
`previous` group is the separate frozen maintained control. Do not interchange
these denominators or replace the immutable reference14 artifact.

The cache remains OFF in source defaults and reference29.

## Four-way qualification

The current four-way cache is compared with the frozen host-command build
using the same reference14, save seed, script, warmed shader cache, mailbox
presentation, two contexts, 1024x576 client and 1280x720 render extent. All
eight 100-second runs retain the same locked Windows session and pass the
effective-setting, executable/map, presentation and runtime-mode checks.
Their 35-second Vulkan readbacks show the intended menu. Timed windows contain
no build, correctness test, sampling profiler or second game.

| Build | Heavy 45–60s FPS, two runs | Pooled 45–90s FPS |
|---|---|---|
| Same immutable reference14 | 38.0174 / 37.3422 | 44.9892 |
| Frozen host-command control, 64-cell cache | 40.3896 / 40.1711 | 48.8675 |
| Shared clip preparation only, 64-cell cache | 39.9589 / 40.1071 | 48.4870 |
| Shared clip preparation and four-way cache | 40.4686 / 41.1128 | 49.7397 |

The combined candidate gains about 0.51 FPS in the heavy window and 1.78%
over the longer window relative to the frozen control. Two repeats establish
only a small workload-specific gain. Sharing clip parameters alone does not
establish a throughput improvement; its smaller preparation work must not be
reported as an FPS win. Candidate longer-window P99 is 30.805/30.091ms, so this
is **not stable 45 FPS**. These are animated-menu measurements, not Android
hardware or question/answer/scoring qualification.

A separate pair of 60-second diagnostic runs changes eligible cache hits from
54.6655% to 87.0234%, over 3407872 reported lookups per build. Statistics are
disabled in the FPS/CPU runs. The higher hit rate is evidence of fewer decoder
invocations, rather than a substitute for end-to-end timing.

Four separate 72-second CPU runs compare the frozen host-command control and
the combined candidate in control/candidate/candidate/control order. Both
builds have host commands and index caching ON; this is **not** a runtime
cache OFF/ON comparison. Owned persistent handles measure 45–65s without
suspension, timed thread enumeration or sampling. All mode/window/session
checks pass.

| Measurement | Frozen control | Combined candidate |
|---|---|---|
| Process CPU ms/frame | 125.7123 | 125.2439 |
| Process cycles/frame | 471.185 million | 461.661 million |
| SPURS pool CPU ms/frame | 57.3842 | 56.5210 |
| SPURS chain CPU ms/frame | 22.0545 | 21.8569 |
| RSX CPU ms/frame | 15.0699 | 15.4986 |
| Process busy cores, uncapped | 5.3081 | 5.4167 |

Process CPU time per frame is effectively flat (0.37% lower); recorded cycles
per frame are 2.02% lower. Total uncapped CPU utilization rises slightly, and
there is no claim of lower power or a measured device benefit.

Frozen executable SHA256 values are reference14
`d66601b4c612eb4006597d285e9d247ed538692d269cadb1448219179fb903fb`,
control `1cdddd62a3e93bca31bdb7ee95e32e2fb476bf15509b885ff946447de55662a3`,
clip-only `c36411a3f48c420ed031c3a71557d2bd76f69983dde85e9e93439251e926979f`,
and combined `875017011972f68eea90d0e68d04b3f89f3b862f3aec84af0144a35b169840cf`.
Private `critical-*` artifacts under `analysis/pc45-image39/` retain the raw
frames, CPU counters, source bindings and guards. Defaults and reference29
remain OFF.
