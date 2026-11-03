# Renderer architecture and diagnostics

Vulkan is the normal Windows renderer; Direct3D 12 is the fallback. They use
separate build directories and the pinned SDK fork. The manifest's `renderer`
field identifies a build; renaming a directory must not change its runtime
profile. Preparation, host settings, resolution, fullscreen and texture packs
are described in [the native launcher](pc-launcher.md).

## Draw and resource lifetime

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
| `PS3RECOMP_VK_DEVICE=N` | Device selection by local enumeration order |

GPU spans include pipeline stalls and exclude separate present/readback
buffers. CPU callbacks can include waits. Texture upload/decode totals overlap
engine/backend counters; these groups must not be added together. Synthetic
microbenchmark gains do not establish a title-level improvement.

The current title owns and pumps its Win32 window on the RSX worker. Moving
presentation/window handling to another thread requires an explicit lifetime
and message-pumping contract. Linux/Steam Deck game execution and complete
render parity remain unqualified; launcher portability alone does not prove
game portability.

## Developer fixtures

`tests/vulkan` provides asset-free device, draw and swapchain fixtures. The
window fixture must actually create and present its window; headless fallback
is not acceptance. `tests/native-launcher-integration.ps1` exercises a bounded
prepared-game launch with private fixture storage. Neither fixture publishes
or supplies game assets. Follow [profiling and validation](performance.md)
for title-level comparisons.
