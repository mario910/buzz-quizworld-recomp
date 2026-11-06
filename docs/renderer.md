# Renderer architecture and diagnostics

Vulkan is the normal Windows renderer; Direct3D 12 is the fallback. They use
separate build directories and the pinned SDK fork. The manifest's `renderer`
field identifies a build; renaming a directory must not change its runtime
profile. Preparation, host settings, resolution, fullscreen and texture packs
are described in [the native launcher](pc-launcher.md).

## Draw and resource lifetime

`BUZZ_RENDERER_DIRECT_DRAW=ON` removes two redundant CPU passes before backend
submission. A single array batch already has distinct vertex IDs, so it skips
the duplicate-remapping pass. A remapped triangle list without restart cuts
passes its existing occurrence map directly to the draw callback, including
the original truncation of incomplete triangles. Other topologies, mixed
sources and restart cuts retain index expansion. The backend consumes the
borrowed indices during its draw callback, before the next draw can overwrite
the remap. No guest vertex data or GPU resources are cached.

`BUZZ_RENDERER_CONSTANT_RANGES=ON` independently enables the live-range
fragment-constant collector previously bundled with CPU refinements. It
refreshes and sanitizes every active constant and clears the old live suffix,
preserving the zero-filled unused tail without clearing all 32 KiB each draw.
Fixed-function transitions clear the previous live range. Constant-buffer
assembly also omits zeroing bytes immediately overwritten by constants and
the alpha epilogue. This adds no cache and does not change shader arithmetic.

Both options default to OFF for controlled qualification. Android test builds
can opt in with `-PbuzzDirectDraw=true -PbuzzConstantRanges=true`. The program
trace compares complete draw inputs and shader constants against the pinned
SDK, including source mutations, restart cuts, index widths and incomplete
triangles. CPU savings and offline ARM64 correctness do not establish phone FPS.

`BUZZ_VK_UNIFORM_REUSE=ON` enables exact reuse of identical vertex/fragment
constant blocks within one Vulkan draw batch (`-PbuzzUniformReuse=true` on
Android). The runtime environment variable of the same name accepts `0/1`.
Each stage checks input bytes while updating the existing CPU shadow; changed
bytes invalidate that stage's reusable slice. Matching lengths and unchanged
data let draws reference the existing immutable GPU slice. Different blocks
use the new draw's slot. Flush, shutdown and buffer/context changes invalidate
the cache, preserving the existing fence-before-reuse boundary. No GPU memory
is read back and no constants are shared across submitted batches. This path
is opt-in in generic builds. Prepared test builds enable it explicitly; a
small gain in the fixed PC menu benchmark still needs gameplay and physical
Android validation before changing general defaults.

This mode also shares the previous immutable descriptor set when all buffer
ranges, sampler handles, image views and layouts match within the same batch.
Resource destruction epochs and batch generations prevent reuse after handle
recycling or pool retirement. `BUZZ_VK_UNIFORM_SETS=0` disables this second
step for independent comparisons. Run `tests/scripts/test-vulkan-uniforms.ps1`
with a configured Vulkan workspace to check byte identity, lifetime boundaries
and rendered pixels with each descriptor recycling/write-cache combination.

`BUZZ_VK_DRAW_STATE=ON` builds an optional cache of exact Vulkan draw state
(`-PbuzzDrawState=true` on Android). It only skips repeated commands for
consecutive draws within the same active render pass, command buffer, batch
generation and pipeline. Pipeline changes resend all state; viewport and depth
values retain exact float bits. Geometry, draw order and synchronization are
unchanged. With the build option OFF, the generated backend has no cache or
extra runtime checks. An enabled build accepts `BUZZ_VK_DRAW_STATE=0/1` for
comparison. This remains experimental: CPU observations have not established
a repeatable FPS gain or a physical Android benefit. Run
`tests/scripts/test-vulkan-state.ps1` against an enabled Vulkan workspace for
field/lifetime tests and actual viewport/scissor pixel checks.

The approved Vulkan profile uses batch size 512, mapped geometry rings,
shared render passes, batched clears and two asynchronous draw contexts.
These defaults belong to the [qualified EXE profile](runtime-defaults.md),
even when the underlying SDK comparison defaults differ.

Queued draws retain immutable vertices, indices, constants and descriptor
sets. Ring slices are reused only after their owning fence. Capacity limits
complete a batch before reuse; unsupported payloads retain the synchronous
path. Target changes and external operations end a shared render pass.

Each asynchronous context owns its command buffer, fence, descriptor pool
and disjoint geometry/constant slices. A limit flush submits without an
immediate wait; reuse waits for that context. Presentation, full guest
completion, readbacks and resource retirement drain all contexts, including
when the current context is empty. This overlaps work within a frame; it
does not queue multiple presented frames ahead of input.

Texture-input labels can retire privately captured source bytes before
rasterization completes. `PS3RECOMP_VK_CAPTURED_LABELS=0` restores full waits
for comparison; this is not permission to bypass general guest GPU completion.

Retired private texture images can be reused only with matching format, size,
mips, cube status, remap and RSX format. The bounded pool excludes render
targets, borrowed views and depth snapshots. Release still drains draw
contexts, and uploads retain barriers and completion waits.

The shared engine's linear 32-bit texture-row decoder specializes channel
shuffles while retaining reference paths for other layouts. Depth sampled
as A8R8G8B8 is converted through depth-to-buffer-to-compute-to-RGBA8 in command
order. The indexed-geometry remap uses a generation-stamped direct table for
compact ranges while preserving first-use order; other ranges use the hash
path.

The port's optional vertex preparation refinements are part of
`BUZZ_RENDERER_CPU_REFINEMENTS` (default OFF). `BUZZ_VERTEX_REMAP_SIMD=1`
enables a four-reference unsigned range scan using ARM64 NEON or x86 SSE2;
the compact-range remap retains the SDK's first-use order, occurrence map,
generation rollover and fallback rules. It has no cross-draw cache.
`BUZZ_VERTEX_PLAN_ACTIVE_ONLY=1` decodes only the attributes selected by a
narrow shader input layout. Layouts with eight or more attributes keep the
SDK initializer. Inline packing still accounts for every declared attribute,
including those the shader does not consume. `0` disables either experiment;
an unset/empty override follows the build option.

Correctness gates include all 65,536 attribute masks, randomized remap inputs,
guard-page tails, reference-backend draw traces and replay of a private 10,000
draw remap corpus on x86 and ARM64/QEMU. QEMU validates output, not speed.
CPU-kernel savings alone do not qualify these switches for default-on:
the initial matched PC menu series was slightly slower with both enabled.
`tests/scripts/test-offline-cpu.py --engine-source <generated-engine.c>` runs
these checks; add `--arm64` for correctness under QEMU, or
`--remap-corpus <private-remaps.bin>` for exact title-input replay. `--bench`
is accepted only for native host execution.

Specialized vertex conversion (`BUZZ_RENDERER_VERTEX_FETCH`, runtime override
`BUZZ_VERTEX_FETCH=0|1`) combines FLOAT3 position, CMP1 normal and HALF2 UV
conversion when those three attributes form a 48-byte output vertex. It
computes the element index once and writes the complete vertex together,
supporting any attribute order. The whole draw's stable spans and frequencies
are checked before writing; other layouts retain the existing conversion
paths. Tests compare exact bytes, callback behavior, all six attribute orders,
nonconsecutive attribute slots, rounding modes and wrapped indices. This
remains part of the existing optional specialized-fetch path.

## Presentation and capture

`PS3RECOMP_VK_PRESENT_MODE=fifo|mailbox|immediate` requests the swapchain mode;
unavailable modes fall back to FIFO. Mailbox avoids tearing and FIFO backlog;
immediate may tear. The approved profile selects mailbox.

`PS3RECOMP_VK_PRESENT_READBACK=0` disables continuous diagnostic image copying.
Explicit surface readbacks remain available. `PS3RECOMP_VK_DUMP` names a PPM
capture; `PS3RECOMP_VK_DUMP_EVERY` requests repeated captures.
`PS3RECOMP_VK_DUMP_ONCE_AT_MS` instead requests one GPU capture at or after the
specified backend-relative time (1..3,600,000 ms). With continuous readback
off, it allocates staging at setup, records one copy, waits for its fence and
retires the request, including on write failure.

Use GPU readback for native content inspection; a black `PrintWindow` result
is not a valid scene capture. Keep captures outside timed windows.

## Profiling and experiments

| Option/tool | Meaning |
| --- | --- |
| `BUZZ_RENDER_PERF=path.csv` | Successful window presentation and FIFO/present timings |
| `PS3RECOMP_VK_PROFILE=1` | Submission, fence, readback and WSI CPU costs |
| `PS3RECOMP_VK_GPU_TIMESTAMPS=1` | Device command-buffer spans; requires profiling and is incompatible with async draw |
| `PS3RECOMP_RSX_ENGINE_PROFILE=1` | Sampled shared-engine geometry, pipeline, texture and callback phases |
| `PS3RECOMP_ENG_VERTEX_BULK=1` | Experimental attribute-major fetching, with bounded reference fallback |
| `PS3RECOMP_ENG_PROGRAM_PAIR_ONCE=1` | Experimental once-per-draw program-pair resolution |
| `BUZZ_FP_HASH_CACHE=0` | Disable the port's fragment-program structural-hash cache for comparison |
| `PS3RECOMP_VK_DEVICE=N` | Device selection by local enumeration order |

The port-owned CMake adapter caches fragment-program structural hashes in
32 bounded slots (programs up to 8 KiB). By default, a hit requires the same
seed, length and all program bytes, including constants. The experimental
`BUZZ_RENDERER_CPU_REFINEMENTS=ON` build option enables the vertex refinements
above, fused metadata, incremental constant clearing and extra caches below. It defaults
to OFF: lower synthetic CPU cost has not established better title pacing.
Android test builds can opt in with `-PbuzzRendererCpuRefinements=true`.

With refinements enabled, a hit requires the same hash seed,
length and every instruction byte. Inline constant values do not affect the
SDK's structural hash; a validated instruction-offset list lets them change
without rehashing the program. Malformed programs require full-byte equality.
A sampled key only selects the slot. Vertex-program byte hashes have a
separate 16-slot cache that checks every byte and the seed. Guest addresses
never establish immutable identity; oversized programs use the reference path.

All caches belong to the serialized FIFO renderer. Constant collection still
reads and sanitizes every active value on every draw, including diagnostic
counter updates. Its private block starts with a zero tail; shrinking programs
clear the old live suffix instead of clearing 32 KiB for each draw. Fixed
programs retain the full clear. Texture usage, gamma eligibility and expansion
eligibility share one instruction walk while preserving their different
branch, opcode and END rules. Sampled texture state supplies shadow, gamma
and expansion metadata in one read per used unit.

The pinned SDK remains unchanged. `tests/scripts/test-offline-cpu.py` checks
the generated engine with fragment caching enabled and disabled, and tests
in-place mutations, malformed programs and guarded ends on x86-64 or ARM64.
It also compares binary traces of shader sources, layouts, constants, vertices
and texture bindings against the unmodified SDK on synthetic draw sequences.

GPU spans include pipeline stalls and exclude separate present/readback
buffers. CPU callbacks can include waits. Texture upload/decode totals overlap
engine/backend counters; these groups must not be added together. Synthetic
microbenchmark gains do not establish a title-level improvement.

`BUZZ_RENDER_PERF` also records accumulated RSX idle time and requested idle
budget, the longest individual idle wait, maximum vblank lateness and the
number of waits/ticks per presented frame. These are wall-clock measurements,
not CPU cycles. A requested timeout is not an observed sleep duration: compare
both before attributing long frames to shader or geometry work. CSV collection
is opt-in and must be kept identical in both arms of a comparison.

On Windows the RSX worker waits on its existing FIFO kick event and a
high-resolution one-shot timer when the 30 FPS cap is disabled. The capped
path retains its SDK wait alongside the existing guest/present pacing.
A queued kick is consumed before arming the
timer. This avoids extending a short idle budget to a coarse wait timeout;
there is no spin loop or process-wide timer-resolution change. Unsupported
timer creation/arming/wait operations fall back to the SDK wait. Disabling
`GCM_FIFO_KICK` preserves timer-only waiting. Linux/Android retain their
native SDK wait implementation.

Vulkan also marks an executed flip complete after submitting presentation,
without waiting for another synthetic vblank. The SDK still defers callback
delivery to the guest, checks execution and rejects duplicate completions.
The rational 60 Hz vblank clock and FIFO/fence ordering are unchanged.

| Diagnostic override | Default | Effect of `0` |
| --- | --- | --- |
| `BUZZ_RSX_PRECISE_WAIT` | Windows, without the 30 FPS cap: 1 | Restore the SDK idle wait |
| `BUZZ_RSX_POLL_BEFORE_ARM` | Windows: 1 | Arm the timer even when a kick is already queued |
| `BUZZ_RSX_EARLY_FLIP` | Vulkan: 1, other backends: 0 | Use the previous completion policy; the existing 30 FPS path still completes after present |

`tests/rsx-idle-wait-test.cpp` checks prequeued and concurrent kicks, timeout,
zero-budget polls, rearming and disabled kicks; its optional `--bench` measures
the queued-kick path only. `tests/rsx-present-completion-test.c` exercises
10,000 executed requests against the real SDK completion and guest-pump code.

The current title owns and pumps its Win32 window on the RSX worker. Moving
presentation/window handling to another thread requires an explicit lifetime
and message-pumping contract. Linux/Steam Deck game execution and complete
render parity remain unqualified; launcher portability alone does not prove
game portability.

## Developer fixtures

`BUZZ_RENDERER_INDEX_FUSION=ON` enables experimental fused u16 index decoding
and remapping. It defaults to OFF; `BUZZ_INDEX_FUSION=0/1` overrides it at
startup. Android test builds can use `-PbuzzIndexFusion=true`. A single
contiguous, restart-free u16 index batch (4–65536 references) is decoded
directly into first-use unique references and its occurrence map. There is no
full temporary reference expansion or range scan, and nothing is cached
between guest draws. SSE2 and NEON decode eight indices at a time with exact
scalar tails. The shared dense table is bounded to 65536 entries (512 KiB),
unless an earlier ordinary SDK draw already required a larger table.

Mixed array/index draws, multiple batches, u32 indices, restart handling,
unsupported topologies and incomplete memory mappings retain the ordinary
path. Allocation failure falls back before emitting any unique references.
Generation wrap and transitions between fused and ordinary remaps use the
same SDK table lifecycle. The optional engine profiler reports eligible and
fused draw/reference counts. `renderer-index-fusion-test.c` checks unique
ordering, occurrence maps, source bounds and allocation failures; the full
renderer trace also covers topology, restart and fallback combinations.

`BUZZ_VK_DESCRIPTOR_BATCH=ON` enables experimental reuse of completed draw
descriptor sets. It defaults to OFF;
`BUZZ_VK_DESCRIPTOR_BATCH=0/1` overrides the build default at startup. Android
test builds can use `-PbuzzDescriptorBatch=true`. Each context has at most two
sets per ordinal draw slot, for ordinary and cube/volume layouts. Draw pool
capacity doubles to cover both without exhausting the pool early. Allocation
still uses single-set calls; completed sets are reused on later batches.
Every live draw has its own set; by default all bindings are rewritten normally.
At the SDK's fence-before-reset boundary, the wrapper resets the slot cursor
and retains sets. Allocation failure forces a real pool reset at that boundary;
reset failure or pool destruction discards the handle cache. Host allocation
failure retains ordinary allocation/reset behavior. This follows the
[Vulkan descriptor reuse approach](https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html).
`tests/scripts/test-vulkan-descriptors.ps1 -Workspace <prepared-workspace>`
checks lifetime, layout isolation and allocation failures, then runs real GPU
payload/descriptor/RTT/texture-reuse fixtures with the option OFF and ON.

`BUZZ_VK_DESCRIPTOR_WRITES=ON` additionally skips unchanged bindings in these
retired sets. It defaults to OFF; the `BUZZ_VK_DESCRIPTOR_WRITES=0/1`
environment variable overrides it at startup. Android test builds can use
`-PbuzzDescriptorWrites=true` together with `-PbuzzDescriptorBatch=true`.
Without set reuse, or if allocating the contents cache fails, updates use
the ordinary path. Keys compare each buffer handle, offset and range, each
image view and layout, and each sampler handle exactly. Adjacent changed
array elements are updated together, including partial sampler/image arrays;
split cube/volume bindings retain their individual writes. Buffer and image
contents are never cached or omitted from uploads.

Destroying any buffer, image view or sampler invalidates cached contents of
that type via separate atomic resource epochs, even when a driver later
recycles the same handle. A temporary upload buffer cannot invalidate image
or sampler descriptors.
Epoch overflow permanently disables write filtering. Set reset/failure or
destruction frees the associated host metadata; all existing GPU fences and
barriers stay in place. At 512 slots and two contexts this metadata is bounded
to 816 KiB, in addition to the set-reuse handle arrays. The rules for updates
and pending sets follow [vkUpdateDescriptorSets](https://docs.vulkan.org/refpages/latest/refpages/source/vkUpdateDescriptorSets.html).

`BUZZ_VK_DESCRIPTOR_WRITE_PROFILE=1` logs cumulative requested/written
descriptor counts for diagnosis; keep it unset during FPS and CPU comparisons.
The descriptor fixture tests exact partial writes, handle reuse after resource
destruction, metadata allocation failure and cache isolation on CPU/ARM.
Real GPU tests also verify that changing constant-buffer contents preserves
pixels while identical retired bindings actually omit driver update calls.

`BUZZ_RENDERER_WAIT_TRACE=ON` adds FIFO stop reasons and producer kick events
to the existing RSX/Vulkan wait timeline. `perf/renderer-waits.py` attributes
idle spans to the last observed FIFO stop: caught up, flip, parked jump,
semaphore, budget, unmapped or other. Older captures report unknown. This is
diagnostic instrumentation only; keep it OFF for FPS measurements. A short
instrumented trace can change scheduling and cannot replace a matched FPS run.

For CPU attribution, `perf/measure-process-work.py measure --include-rsx`
adds the named FIFO/renderer thread to the existing process and SPURS pool
counters. It rejects missing, duplicate, exited or replaced RSX threads.
Cycles and CPU time remain separate; a long FIFO wall span alone does not
establish CPU utilization. Run this observation separately from the FPS series.

`BUZZ_RENDERER_STATE_PACKETS=ON` enables an experimental bounded FIFO path
for pure register writes: transform constants/LOAD, vertex array formats and
offsets, fragment program address and the SDK's vertex-cache invalidation
register. It defaults to OFF; `BUZZ_STATE_PACKETS=0` restores the original
path in an enabled build. Android builds can use `-PbuzzStatePackets=true`.
Only subchannel 0 and packets of at most 33 words wholly within one supported
register range qualify. Both state consumers, their bounds, dirty ranges,
seen counters and non-incrementing semantics are preserved. Each payload word
retains its IO mapping check, including a partially unmapped packet.
Draws, fences, notifications and other methods keep the existing path.
Per-method diagnostics and the legacy live renderer also force that path.
`tests/renderer-state-packets-test.c` compares complete state against both SDK
consumers, including counter wrap, invalid LOAD, raw float bits and Release
behavior. This option is not a claim of an Android FPS gain.

`BUZZ_RENDERER_TEXTURE_SNAPSHOT=ON` enables an experimental per-draw snapshot
of decoded fragment texture registers for pipeline selection. It defaults to
OFF; `BUZZ_TEXTURE_SNAPSHOT=0/1` overrides it at process startup. Android test
builds can opt in with `-PbuzzTextureSnapshot=true`. The same
decoded fields feed cube/volume masks, unnormalized dimensions, shadow,
gamma and expansion checks. Each pipeline decision rebuilds the snapshot,
including disabled units; it retains no guest pixels or GPU resources.
`tests/renderer-texture-snapshot-test.c` compares all masks against the pinned
SDK helpers across 10,000 changing register sets and all combinations of 3D
backend capabilities. The full renderer trace also changes texture dimensions,
format, enable, filtering and offset between draws.

The opt-in `BUZZ_RENDERER_VERTEX_FETCH=ON` selects specialized conversion of the common
FLOAT, HALF and compressed-normal vertex formats, independently of the other
CPU refinements. `BUZZ_VERTEX_FETCH=0/1` overrides it at runtime. Android test
builds can opt in with `-PbuzzSpecializedVertexFetch=true`.
It validates a complete ordinary-frequency draw before writing any output,
then uses SSE/NEON integer operations for byte order and half expansion.
Half subnormals still become signed zero and NaN payloads remain exact; native
half conversion instructions would change those rules. Compressed normals
retain division and its rounding behavior. Unsupported, instanced or invalid
ranges use the existing SDK path, preserving resolver calls and failure output.
No vertex bytes are cached between draws.

`BUZZ_RENDERER_FETCH_PROFILE=ON` adds diagnostic vertex-format counters.
It defaults to OFF and must stay disabled during FPS comparisons.
`tests/renderer-vertex-fetch-test.c` covers all half encodings, compressed
normal rounding modes, mixed descriptors, fallback, guarded reads and exact
output bytes on x86 and ARM64. Renderer differential traces also contain
twelve-vertex draws so they exercise the bulk conversion path.

`BUZZ_RENDERER_VERTEX_STREAM=ON` converts the common FLOAT3/CMP1/HALF2
three-attribute layout four vertices at a time with SSE2 or ARM64 NEON.
It uses three vector divisions for four normals and expands both half UV
components across four vertices, instead of filling unused vector lanes per
attribute. Integer byte swapping, half subnormal/NaN rules, exact division
rounding and output attribute order remain unchanged. Source loads read only
12/4/4 validated bytes. Unaligned streams and final groups of 1–3 vertices
are supported; unsupported layouts use the existing specialized converter.
All bounds are checked before output writes, preserving failure/resolver
behavior. There is no cross-draw vertex cache or guest data mutation.

This option is independent of `BUZZ_RENDERER_VERTEX_FETCH` and defaults to OFF.
An enabled build accepts `BUZZ_VERTEX_STREAM=0` to restore the previous fetch
selection. Android test builds can use `-PbuzzVertexStream=true`. The fetch
fixture checks all attribute permutations, all half encodings, normal rounding
modes, guard pages and short tails. Whole-engine traces also use three-input
programs with indexed/non-indexed draws, source mutations and every tail size.

`BUZZ_RSX_READY_CHECK=ON` adds a consumer-side check before the RSX worker's
scheduled idle wait. After a caught-up drain or a flip, a newly published PUT
can make the next drain runnable immediately. The check takes the FIFO lock
without blocking and compares PUT with the actual consumer head; a guest GET
snapshot alone is insufficient. Parked jumps, semaphore acquires, an unfinished
budget-limited drain and lock contention retain their normal retry interval.
The next drain still performs the SDK's synchronization and command checks.
Host pause and the separate 30 FPS pacing path also retain their waits.

This option defaults to OFF. Enabled builds accept `BUZZ_RSX_READY_CHECK=0`
for comparisons using the same binary. Android uses `-PbuzzRsxReadyCheck=true`.
The adapter is applied to generated SDK sources, after optional wait tracing
and state-packet adapters. `tests/scripts/test-fifo-ready.py --source ...`
tests the generated FIFO implementation, including publication, lock contention,
park recovery, semaphore blocking, flip ordering and release visibility;
`--arm64` runs the fixtures under QEMU for correctness only.

`BUZZ_VK_BATCH_UPLOAD=ON` enables an experimental port-owned Vulkan upload path.
It defaults to OFF: shorter upload waits have not established better title FPS.
The `BUZZ_VK_BATCH_UPLOAD=0/1` environment variable overrides the build default;
Android test builds can use `-PbuzzBatchedTextureUploads=true`. It requires
asynchronous draw contexts and otherwise retains synchronous uploads.
Each context lazily allocates a coherent 4 MiB staging arena. Input bytes are
copied immediately, render passes end before transfers, and image barriers
order previous readers and subsequent sampling. Only a completed context
fence resets its arena. Full arenas submit a batch; oversized uploads and
allocation failures use the original path. Resource release, readback and
guest completion still drain pending commands. The total arena bound is 8 MiB.

`tests/scripts/test-vulkan-upload.ps1 -Workspace <prepared-workspace>` checks
the generated adapter on a real Vulkan device, including caller-buffer reuse,
padded rows, arena rollover, fallback and texture replacement during draws.

`tests/vulkan` provides asset-free device, draw and swapchain fixtures. The
window fixture must actually create and present its window; headless fallback
is not acceptance. `tests/native-launcher-integration.ps1` exercises a bounded
prepared-game launch with private fixture storage. Neither fixture publishes
or supplies game assets. Follow [profiling and validation](performance.md)
for title-level comparisons.
