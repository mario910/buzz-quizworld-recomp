# Current Deck dependency trace and exact GPU vertex decoding

Stable 30+ FPS remains unverified. Fresh traces redirect the next experiment
toward renderer vertex preparation. An exact GPU decoder has passed synthetic
readback checks, but it is not integrated into the live vertex-input path and
has no established FPS benefit.

Follow-up: the [live packed vertex experiment](steam-deck-packed-vertices.md)
subsequently passed vertex-input GPU parity and gameplay audits. Two menu
series, including refreshed renderer PGO, showed only small overlapping leads;
the integration was archived without promotion. The feasibility findings below
describe the earlier stage of that work.

The later [texture retirement investigation](steam-deck-texture-retirement.md)
identifies the synchronous upload/resource-release drains. A bounded deferred
retirement prototype removes those calls but still measures below 30 FPS.
Call-site attribution is now part of the optional renderer wait tracing.

The subsequent [exact clip-operation study](steam-deck-clip-operations.md)
includes a fresh loss-free CPU sample and per-thread attribution. Two exact
projection candidates, including refreshed native PGO, improved kernel timings
but remained flat around 28 FPS in matched menu tests. A translation-only
object-transform prototype is validated only at the kernel/captured-input level.

The [vectorized index follow-up](steam-deck-index-vector.md) also remained flat
despite a 28% kernel gain and refreshed renderer profiling. A subsequent
[command-thread profile](steam-deck-chain-copies.md) identifies small DMA-read
callers and lengths as another candidate for investigation.

A later [FIFO cursor isolation experiment](steam-deck-fifo-cursor.md) tested
possible cache-line sharing without changing command behavior. It passed the
targeted FIFO checks but remained neutral in clean menu timing and was removed.

The [skinning preparation study](steam-deck-skin-preparation.md) tests recipe
reuse and duplicate palette-store removal, and adds 1,500 exact current-menu
job captures. Neither preparation prototype established a substantial gain.

The [geometry output study](steam-deck-output-layout.md) proves an exact CMP
packing alternative and tests a dense vertex serializer. CMP arithmetic alone
was nearly neutral; the store-layout candidate passed whole-job checks but
did not improve clean menu FPS with refreshed geometry PGO and was removed.
The existing [MFC argument shortcut](geometry-channel-setup.md#steam-deck-measurement)
also remained neutral in a same-executable Deck menu comparison and stays OFF.

The [exact SIMD video conversion](steam-deck-video-simd.md) now has a guarded
production integration for image 7. Exhaustive byte, full-state and live checks
pass. Matched production menus improve 28.47 to 28.85 FPS with 3.36% less CPU
time per frame; stable 30+ FPS remains unachieved.

The subsequent [post-link code-layout experiment](steam-deck-bolt-layout.md)
passes 9,696 captured-job output comparisons but regresses matched menu FPS
from 28.90 to 27.65. This cycle-sampled BOLT variant is rejected; the committed
SIMD build remains the reference.

The [full-barrier study](steam-deck-full-fence.md) retains DMA publication
ordering with a faster isolated x86 barrier. Ordering stress, actual waiter
fixtures and captured outputs pass, but both switched and direct builds fail
to improve clean menu FPS. The original barrier remains in production.

## Current-build dependency evidence

The reference is the Wayland startup repair, executable SHA-256
`630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d`.
The diagnostic variant enables renderer waits, producer dependencies and
SPURS execution CPU timing, with `SPURS_JOB_TRACE_ALL=1`. Its SHA-256 is
`2e9a93e421c7bab6374019123cfa7231e52b782a3ebcebaf658aadc13ff248f7`.
Both runs enable image-36 search and use X11, 1280×720, 100% scale, mailbox,
512-draw batches, two contexts and zero worker spin.

The menu trace records seconds 45–48; the round trace records seconds 306–309
of the existing solo-question script. Both report zero dropped records.
Post-window captures show an intact menu and scoreboard, respectively.
Those captures do not certify every frame's camera phase within the trace.

| Diagnostic measure | Menu | Late round |
| --- | ---: | ---: |
| Complete FIFO flip intervals | 75 | 67 |
| Instrumented mean interval, ms | 39.390 | 44.042 |
| Renderer idle, ms/interval | 2.874 | 9.495 |
| Fence waits nested in FIFO drain, ms/interval | 10.357 | 7.970 |
| Image-41 execution CPU, aggregate ms/interval | 57.973 | 62.348 |
| Image-41 jobs/interval | 3262.1 | 3103.7 |

These are diagnostic timings, not clean FPS results. Aggregate execution CPU
spans parallel workers and must not be added to elapsed frame time. Fence
waits are nested inside FIFO drain; the presentation thread's separate waits
must not be added to them. Instrumentation changes scheduling and work cost.

Address matching identifies 23 parked-JUMP replacements by image 39 in each
trace; all matched written/consumed words agree. Mean publication-to-consumer
delay is 3.572 ms in the menu and 3.666 ms in the late round. Their overlap
with renderer idle after publication is 1.095 and 1.257 ms per interval.
Unmatched parks remain unknown (17 and 63). This does not establish the gain
available from waking earlier: the existing exact-park wake experiment has
already failed to demonstrate repeatable FPS improvement.

Matched chain command publications resume much sooner: 8.985 µs and 8.689 µs
on average. Most chain wait spans remain unmatched by the small-write tracer.
Pool queue-to-first-claim and final-completion-to-return overlap only about
0.040 ms/interval of menu idle and 0.206 ms/interval of late-round idle combined.
Overlapping categories are not a causal critical path or additive savings.

A separate instruction annotation of the retained image-36-search CPU profile
again locates renderer self time in ranked index mapping and vertex unpacking.
That profile lost eight chunks, so its samples identify candidate instructions;
they are not precise removable wall time. The trace and sampling profiles are
separate runs.

## Complete-range index prototype

A private candidate proves that every vertex ID in a draw's contiguous range
is present, then emits the same sorted references and computes occurrence
ranks directly as `index - minimum`. It bypasses the rank table only when the
range has at least 16 entries and is smaller than the occurrence count. Other
inputs retain the original path, including the all-unique identity contract.

It passed 141,328 AddressSanitizer/UndefinedBehaviorSanitizer cases covering
single-hole rejection, vector tails, uint16 boundaries, dense/sparse inputs,
empty/multiple spans, ordering and occurrence mapping. A separate 90-second
live check reported at least 3,805,184 checked mappings with zero errors; its
menu capture was intact. There were 473,771 fast-path hits, but they covered
only 39,158,295 of 792,570,652 scanned range entries (4.94%) and 137,877,162 of
1,108,536,033 occurrences (12.44%).

This is limited coverage, not evidence of an FPS gain or loss. No clean timing
series or round qualification was performed, and the prototype is not promoted.
Its build discarded the changed `buzz_index_packed_spans` PGO counts; it would
need a frozen-reference control as well as same-binary off/on comparisons.

## Exact GPU decoding feasibility

The next candidate can retain packed CMP normals and half-float texture
coordinates in immutable vertex uploads, then decode them in the vertex shader.
Moving the work requires actual vertex binding, shader/cache identity, source
bounds and live rendering validation; compute readback alone does not prove it.

The synthetic CMP helper uses integer operations to construct the same
binary32 values as CPU division by 1023 (X/Y) or 511 (Z). For a positive integer
`n`, division by `2^m - 1` has a repeating `m`-bit expansion. With
`h = floor(log2(n))`, it constructs 25 leading significant bits:

```text
t = n << (24 - h)
t |= t >> m
t |= t >> (2*m)
significand = (t >> 1) + (t & 1)
```

Nonintegral odd-denominator fractions have a nonzero tail and cannot be halfway
between binary32 values. The guard bit therefore determines rounding; normal
significand carry handles the endpoint cases. The exponent, mantissa and sign
are assembled as integer bits. Zero is handled separately. A CPU oracle checks
all 3,072 possible signed component values against rounded binary32 division.
No approximate GPU floating-point divide is used.

The half helper reproduces the SDK's signed-zero flushing for half subnormals,
normal exponent expansion, and infinity/NaN sign and payload handling.

Both helpers were compiled by the game's real HLSL-to-SPIR-V translator and
executed on the Deck's RADV VANGOGH device. Each test dispatched 1,048,576 packed
inputs and checked four output words per input. All 8,388,608 words matched the
CPU references exactly. Inputs exhaust every signed CMP component value and
all 65,536 half patterns, plus random packed combinations. The half reference
uses the SDK's existing vertex-element decoder. No game assets are needed.

This proves the tested compute helpers, not vertex-stage integration, frame
parity, shader-cache correctness or performance. Those gates remain open.

## Evidence and restoration

Private artifacts under `/home/deck/buzz/performance/optimization/`:

- `codex-critical-current/`: frozen reference/trace executable, build logs,
  traces, analyzer JSON, captures, instruction annotation and restoration log.
- `codex-index-complete/`: prototype header, sanitizer fixture, frozen candidate,
  live-check configuration and capture. Candidate SHA-256:
  `d05b7795c55d46c841552bfb00b9650b2eccb65a95b551b19c3c9975f61dedfa`.
- `codex-gpu-cmp/`: exact HLSL helpers, CPU proof script, synthetic Vulkan fixture,
  reproducible build script, both GPU pass logs and integration notes.

All diagnostic CMake options and the prototype header were restored. The
workspace executable rebuilt byte-identical to the reference above. SDK pin
`79efc280dad27271d5f68077acb0247df1e88dc7` is clean. The installed executable
remains `189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.
No captures, generated game code or binaries are committed.
