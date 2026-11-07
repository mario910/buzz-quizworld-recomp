# Content-verified geometry index cache

`BUZZ_GEOMETRY_INDEX_CACHE` is an experimental, default-OFF producer-side
optimization. It reuses decoded image41 triangle indices when the complete
staged source bytes and index count match an owned entry. Triangle order,
padding, admission and guest-visible state are preserved. Projection,
skinning, culling, allocation, DMA and command publication still run normally.
Vertex data is never cached by this option.

Each worker owns 1024 cells in 256 four-way sets. Only sources of 8–8192 bytes
and 1–4096 triangles participate; larger jobs use the ordinary decoder. Cells
own the complete source bytes and three decoded index components. Population
and growth can allocate; warmed hits compare and copy existing storage.
The least recently used cell in a set is replaced only after successful
decoding. Failed decodes retain previous entries. Recency affects eviction
only, including after a counter wrap.

The sampled fingerprint selects a set. Every hit still requires complete
byte equality, index-count equality, fresh source-span/family validation and
fresh guest codec-table validation. A source pointer, LS bank or previous
admission cannot establish a hit. Invalid inputs retain the ordinary decoder
and rejection reason. Entries are private to each worker and are never
persisted to disk.

Memory depends on worker count, retained inputs and vector capacity. Logical
source/index payload is bounded to 32 MiB per worker; retained allocation
capacity can be higher. The observed four-worker menu workload retained about
5.5 MiB of vector capacity in total. That snapshot excludes TLS metadata,
allocator overhead and the rest of the process and is not a universal limit.

## Enabling and disabling

- CMake, Windows or Linux: `-DBUZZ_GEOMETRY_INDEX_CACHE=ON`.
- Windows build helper: `-GeometryIndexCache` with the native geometry build
  arguments. Omitting it explicitly resets the CMake option OFF.
- Android: `-PbuzzGeometryIndexCache=true` with the NEON host geometry profile.
  The property defaults false; reference29 explicitly keeps it OFF.
- `BUZZ_INDEX_CACHE=0` disables a compiled-in cache before launch. Value `1`
  or an unset variable enables a compiled-in cache.
- `BUZZ_INDEX_CACHE_STATS=1` prints cumulative per-worker eligible lookup/hit
  counts every 65536 lookups. Use it for diagnostics outside FPS cohorts.

## Validation

Windows clang-cl, Linux x86 and ARM64/QEMU pass the complete 19508-job cache
comparison, 67110 source/codec/span mutations and poisoned-output checks.
Another 448 valid payload variants across 32 identical-fingerprint sets check
all ways, repeated eviction and full byte comparison against the uncached
decoder. The composed host producer also retains identical published indices
and vertices on all three architectures, including malformed offscreen-index
rejection. These checks cover cache correctness, not Android hardware speed
or full question/answer/scoring progression.

The existing tests are `tests/geometry-index-cache-test.cpp` and
`tests/geometry-coarse-clip-test.cpp`, included in the offline CPU suite's
`--corpus` mode. The unchanged original-decoder oracle is
`tests/geometry-host-index-job-test.cpp`.

## Performance qualification

Increasing the four-way cache from 256 to 1024 cells addresses observed set
contention. Separate non-suspending counter diagnostics in the animated menu
show 87.11% versus 99.87% eligible hits over about 2.79 million lookups per
build. Fewer decoder invocations are evidence of the mechanism; they are not
an FPS measurement or a reason to enlarge the cache further without evidence.

Six matched 72-second PC runs compare the same immutable reference14 EXE,
the previous bounded-projection build and the larger cache. The save seed,
script, warmed shaders, mailbox mode, two contexts, 1024x576 client and
1280x720 render extent are fixed. All executable/map, effective-mode,
presentation and locked-session checks pass. All six Vulkan readbacks show
the intended menu. No build, correctness test, profiler or second game
overlaps the timed windows; the 35-second readback is outside them.

| Build | Pooled heavy 45–60s FPS | Pooled 45–65s FPS |
|---|---:|---:|
| Same immutable reference14 | 37.7279 | 39.3329 |
| Previous build, 256 cells | 41.7881 | 43.7157 |
| Same pipeline, 1024 cells | 42.2535 | 44.0829 |

This is a small workload-specific improvement: about 0.47 FPS in the heavy
window. Candidate repeats are 41.9652/42.5420 FPS; two repeats do not establish
a universal gain. The candidate's 45–65s P99 is 30.527/29.597 ms, so stable
45 FPS remains unproven. These are menu results, not gameplay or device tests.

Separate persistent-handle CPU counters during 45–65s, with no suspension or
timed thread discovery, show effectively flat process CPU time per frame
(123.1061 → 122.9515 ms) and 0.65% fewer process cycles per frame. SPURS pool
CPU time is 55.1655 → 55.5831 ms/frame. Total uncapped utilization rises
slightly; no electrical-power or Android hardware saving is claimed. CPU
milliseconds sum work across threads and are not frame wall time.

Private `index-wide-*` and `cache-{only,wide}-*` artifacts under `analysis/`
retain the raw frames, counters, source bindings and guard results. Frozen
EXE SHA256 values are:

- reference14: `d66601b4c612eb4006597d285e9d247ed538692d269cadb1448219179fb903fb`
- previous: `4fea5a34e400c50ff44bb430b6a3d4cfe0101d740046fec81f0557fb7a3a0fc9`
- 1024 cells: `9bc03592516babdbccbd693f5594ca2589faa9c8aca3b2a945252612870c9017`

Source defaults and Android reference29 remain OFF.
