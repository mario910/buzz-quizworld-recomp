# Vulkan developer bring-up

This is an opt-in renderer experiment, **not a playable release or the default
backend**. The tested D3D12 build remains in `build-windows`; Vulkan has its own
`build-vulkan-windows` directory. The ordinary launcher does not discover the
experimental directory. Both use the existing pinned SDK baseline, not an
upstream runtime upgrade.

## What has actually run

On Windows, the asset-free fixture has passed real device creation, three GPU
clears with exact readback checks, and built-in HLSL-to-SPIR-V translation.
Its native Win32 swapchain test passed separately on an NVIDIA RTX 2070 and
AMD Radeon integrated graphics. Window failure is a test failure, not an
accepted headless fallback.

An isolated EU 1.02 game boot has rendered the language screen, including text,
flags and buzzer artwork, subsequent legal/logo screens, and the intro video.
Internal guest buzzer pulses selected Polish without desktop input. After
staging the matching update's `Patched` assets, a bounded startup-skip test also
reached the main menu and rendered its 3D audience/background, text and buttons.
A second fresh-content test (no imported plaintext caches) used the normal
startup-video path and the game's own red-button intro skip, then reached the
studio menu with all six monitor logos and the scrolling ticker visible.
This is not full render parity or a playable-performance claim. Other game
profiles and Vulkan gameplay are unverified. The integration fixture checks
windowed renderer initialization, not a successful complete boot.

Two test-setup problems were found during bring-up: `cellGame` truncated long
host paths to the guest ABI's 128-byte limit, and fresh EU 1.02 fixtures omitted
the update scripts (thus combining a 1.02 executable with 1.00 disc scripts).
The first produced wrong host directories; the second caused a null-object
menu initialization failure on **both** D3D12 and Vulkan. Host/guest path limits
are now separate and covered by a synthetic regression test. The launcher
rejects incomplete EU 1.02 content before starting, and the preparation pipeline
stages the update's assets, without copying user saves or plaintext caches.

The shared host audio/input hooks are present, including the hardware-unverified
Windows physical Buzz provider. **The PC overlay is deliberately disabled** in
this build: it is still D3D12-specific. An invisible menu must not capture input.
Vulkan host fullscreen/frame-cap settings and renderer selection in the normal
launcher are not integrated yet.

## Build locally

Use your own verified game and firmware inputs, prepared generated code, and the
normal Windows developer toolchain. No content is downloaded by these commands.
The optional shader toolchain sources are fetched at configure time, at pinned
revisions with SHA-256 checks; see the SDK's `cmake/VulkanDependencies.cmake` and
the third-party notices. The resulting binary uses the compiled-in translator,
not a runtime shader compiler subprocess.

```powershell
.\scripts\Build-Buzz.ps1 -SkipGeneration -VulkanExperimental -Jobs 6
.\tests\native-launcher-integration.ps1 -BuildDirectory build-vulkan-windows -ExpectVulkan -Seconds 30 -UpdateContent 'D:\YourPreparedUpdate\USRDIR\Patched' -PcSettings "version=1`nvolume=0`nfps_limit=0`nfullscreen=0`noffline=1`nrenderer=auto"
```

The second command requires the native launcher build. It copies the game binary
and manifest into a fresh ignored fixture, redirects all save writes there, and
stops only its own game child at the time limit. No desktop keys or focus changes
are used. It does not copy a player's existing saves. Do not publish its game
binary, derived shaders, captures, logs, or generated code.

Add `-SkipIntro` only for diagnostic matched-scene tests. It uses the existing
startup-only skip, does not change saved preferences, and is not evidence that
the normal intro-to-menu handoff works.

For the asset-free fixture, configure `tests/vulkan` into `build-vulkan-smoke`
with CMake 3.31+, Ninja and a configured C++ developer environment. Run:

```powershell
cmake --build build-vulkan-smoke
ctest --test-dir build-vulkan-smoke --output-on-failure
.\build-vulkan-smoke\buzz_vulkan_smoke.exe --window
```

The optional window fixture starts without activation. Device selection is
`PS3RECOMP_VK_DEVICE=N` (enumeration order, not a portable device identifier).
`PS3RECOMP_VK_DUMP=absolute-path.ppm` plus `PS3RECOMP_VK_DUMP_EVERY=N` enables
explicit developer captures. Disable captures for performance measurements.

## Experimental submission diagnostics

`PS3RECOMP_VK_DRAW_BATCH=64` opts into bounded small-draw batching (0, the
default, retains synchronous draws; maximum 512). Vertices, indices and constants
are copied into command-owned payloads; every queued draw has an immutable
descriptor set. Large or misaligned payloads fall back to synchronous draws.
Readbacks, resource retirement and full guest completion boundaries flush and
wait before reuse. With batching enabled, texture-input-read labels can retire
privately captured source data before rasterization finishes; set
`PS3RECOMP_VK_CAPTURED_LABELS=0` to restore full waits for comparison. This is not
a general override of guest GPU completion.

`PS3RECOMP_VK_DRAW_RING=1` gives each draw its own mapped coherent vertex,
index and aligned constant-buffer slices. Slices are reused only after the
batch fence; allocation failure retains the command-copy batch path.
`PS3RECOMP_VK_DRAW_PASSES=1` additionally allows consecutive ring draws using
the same framebuffer/render pass to share a pass. Target switches and external
operations end the pass; render-to-texture dependencies remain explicit.
Both options default off and require a nonzero draw batch size (pass sharing
also requires the ring). These are developer comparisons, not release presets.
The ring packs variable-sized geometry into bounded 8 MiB vertex and index
banks; reaching capacity completes the batch before reuse. The command-copy
path retains Vulkan's 65536-byte update limit and synchronous fallback.
`PS3RECOMP_VK_DRAW_CLEARS=1` additionally records color/depth clears in the same
command buffer after ending an active render pass. This requires ring and pass
sharing, and still flushes at readbacks, full completion labels and resource
retirement. Clear commands have their own bounded count per batch.

`PS3RECOMP_VK_DRAW_ASYNC=1` is the Windows performance experiment for two
in-flight draw batches. It requires nonzero batching, ring buffers and pass
sharing. Each context has a separate command buffer, fence, descriptor pool and
disjoint geometry/constant slices. A limit flush submits without immediately
waiting; reuse waits for that context's fence. Resource retirement, readbacks,
full guest completion and presentation still drain all contexts, including
when the current one is empty. This is overlap within a frame, not a queue of
frames presented ahead of input. Device timestamps are currently unsupported
with this option; combining them is rejected rather than silently reporting
incomplete GPU timings. All these options still default off.

The asset-free draw tests cover changing vertices/constants/descriptors, source
overwrites, render-to-texture, resource lifetime, depth/alpha ordering, queued
color/depth clears, MRT, geometry-bank capacity/reuse, size fallback and
guest completion labels. These tests passed separately on NVIDIA and AMD, but
do not prove complete retail-game correctness.

`PS3RECOMP_VK_TEXTURE_REUSE=1` separately experiments with recycling retired
private texture images. The pool holds at most eight images and 64 MiB of
actual device allocations. Only ordinary 2D/cube textures participate; render
targets, depth snapshots and borrowed views do not. Release still drains every
draw context before retiring an image. Reuse requires identical format, size,
mip count, cube status, remap and RSX format; uploads retain their barriers and
completion waits. Shutdown destroys the pool. The default is off.

Native synthetic tests exercise actual reuse after pending draws, changing
pixels/caller memory, incompatible image/view keys, count and byte pressure,
and cleanup with synchronous and asynchronous draws. Passing these tests alone
does not establish a title FPS improvement or full gameplay correctness.
Two native Windows EU 1.02 heavy-menu A/B pairs, reversing test order, measured
33.7–33.9 FPS without the pool and 37.2–38.0 FPS with it; the fresh D3D12
reference was 37.4 FPS. These used the async/ring/pass/clear and fast-row
experiments, mailbox presentation and disabled diagnostic readback, with
profiling/captures off. Average menu throughput is close in this comparison;
Vulkan's slow-frame percentiles remain worse. This is not gameplay parity,
stable-30 certification or a Steam Deck measurement.

`PS3RECOMP_VK_PRESENT_MODE=fifo|mailbox|immediate` chooses a diagnostic swapchain
mode. FIFO remains the default; unavailable alternatives fall back to FIFO.
Mailbox avoids a FIFO backlog without tearing; immediate may tear. Modes must
be compared explicitly when measuring: window/display scheduling is a separate
cost from guest geometry and GPU command submission. Do not describe a faster
swapchain mode alone as a rendering optimization or full D3D12 parity.

`PS3RECOMP_VK_PRESENT_READBACK=0` is a separate developer experiment that omits
the diagnostic GPU-to-host copy of the presented image. The default is 1, keeping
the existing SDK behavior. Explicit frame dumps force readback back on, and an
unavailable presented-center diagnostic returns failure rather than a fabricated
pixel. Actual window presentation and explicit surface readbacks are unchanged.
Native Windows tests cover both modes and the dump override. In the measured
EU 1.02 menu this reduced copying but did not establish a meaningful FPS gain;
do not treat it as a substitute for submission/CPU performance work.

For device-side timing, combine `PS3RECOMP_VK_PROFILE=1` with
`PS3RECOMP_VK_GPU_TIMESTAMPS=1`. Vulkan 1.0 timestamp queries bracket the walker's
command buffer, are read only after its existing fence, and are reset before
reuse. The chosen queue's valid bits and the device timestamp period are
respected. Unsupported devices keep CPU-only diagnostics and log the reason.
`gpu span_ms` is elapsed GPU command-buffer time, including pipeline stalls,
not pure shader busy time; it excludes the separate present/readback-to-window
command buffers. `draw_commands_ms` includes mixed draw/clear batches, while
`other_commands_ms` covers batches without draws. `backend-draw-cpu-ms` excludes
submission/wait/query-collection time, but does not cover the shared engine's
upstream geometry/shader lookup work. Compare timestamps on/off to check their
overhead before drawing conclusions from a performance run.

`PS3RECOMP_RSX_ENGINE_PROFILE=1` samples roughly 1/16 of completed shared-engine
draws. It reports estimated phase totals for geometry, targets, pipeline lookup,
textures, constants, bindings, backend callbacks and invalidation. Backend
callbacks can include GPU waits; these estimates overlap the backend/GPU
diagnostics and must not be added to them. Sampling avoids a clock query on
every method, and initialization caches the environment option. Normal runs
do not collect these timings.

Vulkan profiling also reports exact host-observed texture creation, upload
staging allocation, CPU copy and upload synchronization times. Creation and
synchronization can include existing draw-fence waits. These counters overlap
submission/fence and engine upload totals; do not add the groups together.

`PS3RECOMP_ENG_VERTEX_BULK=1` tests attribute-major fetching from prepared guest
vertex arrays. Stable mappings retain per-reference/divider bounds checks;
unsupported or unmapped cases use the original vertex-major implementation.
The engine profile includes bulk/reference list counts. Faster CPU microtests
are not evidence of a faster title; compare the real Windows Vulkan build.
Native `buzz_vertex_fetch_test` checks byte-equivalent output and bounds against
the reference implementation; `--bench` is an optional CPU-only microbenchmark.
The shared-engine reference/bulk/profile tests also run natively on Windows.

`PS3RECOMP_ENG_TEXTURE_ROWS=1` is an independent decoder experiment. Linear
32-bit color rows use format-specialized channel shuffles; swizzled/compressed
and other formats retain the reference implementation. Unaligned rows, pitch
padding, odd widths, alpha modes and fallback formats are checked against the
reference by native `buzz_texture_layout_test`. It defaults off pending title
gameplay validation; no texture updates or mutation checks are skipped.
In the native Windows EU 1.02 heavy-menu comparison, with async batching and
otherwise identical settings, this changed roughly 30.4 to 33.1 FPS versus
37.4 FPS for D3D12. This is a bounded menu result, not stable-30 gameplay or
complete D3D12 parity.

With engine profiling enabled, `texture-exact` reports inclusive upload time
(including backend calls/waits), its CPU decode subset, byte counts and cache
miss/change/eviction counts. Unlike sampled draw-phase totals these texture
counters time every upload/decode. They overlap those totals and backend waits;
do not add them together.

`PS3RECOMP_ENG_PROGRAM_PAIR_ONCE=1` is a separate A/B experiment: resolve the
guest vertex/fragment pair once within one draw, sharing that result between
input-layout analysis and pipeline lookup. It does not cache guest code or
constants between draws; the default remains the original duplicate walk.

## Remaining engineering work

- **Performance parity with D3D12 is the priority**, not merely reaching the
  menu. Optional `BUZZ_RENDER_PERF=path.csv` records actual successful window
  presentations (not host present calls), FIFO drain and host presentation
  timings. `PS3RECOMP_VK_PROFILE=1` separates submission/fence/readback/WSI CPU
  wait costs. Disable image captures and compare the same warmed scene with
  identical settings. UI work is secondary until the large performance gap is
  addressed.
- Validate the opt-in draw/clear batching in longer gameplay. Extend batching
  to further operations only with explicit storage/descriptors lifetime guarantees;
  removing waits alone is unsafe.
- Validate longer sessions and gameplay, including a complete unskipped intro,
  rather than only the bounded boot/button-skip paths.
- Verify real geometry, render-to-texture, texture formats, hair, avatars and
  question images against the D3D12 baseline.
- Add Vulkan overlay rendering and host presentation controls, with safe
  swapchain recreation and owner-thread window operations.
- Validate Linux title input/window integration and actual Steam Deck execution;
  the portable launcher passing Linux tests is not evidence of a Linux game port.

The current title creates/presents/pumps its Win32 window on the RSX worker.
The SDK's optional split-present API has not been validated with a Windows
window on a different owner thread. Keep those operations on the owner thread
until that lifetime/message-pumping contract is addressed.
