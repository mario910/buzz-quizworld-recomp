# 30 FPS frame lock: status and next steps

**Status: not solved. Keep the lock OFF (the default).** Uncapped play is the
smoother configuration on both renderers today.

## What was wrong

The original lock slept in the backend's present call until the 33.3 ms slot
was due. Present runs on the single RSX worker thread (`port/main.cpp`
`frame_clock`), which also drains the guest FIFO and ticks vblank. Buzz fences
every render pass on RSX labels the drain writes, so while that thread slept
the game's CPU work for the next frame stalled too: the frame became
*sleep + the whole next frame* instead of overlapping. Reported in play as
20-23 FPS and choppy, versus a stable 35+ uncapped.

The present-side sleep has been removed from both backends
(`rsx_d3d12_backend.c`, `rsx_vulkan_backend.c`). The menu toggle and the
`ps3_host_set_frame_limit(30)` setting therefore currently have **no effect**
on frame rate. D3D12 still presents without vsync when the lock is on, as
before. With the lock off, Vulkan presents in mailbox mode (no tearing, uncapped).

## Approaches tried (EU 1.00, Vulkan, main menu and solo round)

| Approach | Result |
|---|---|
| Sleep in present (original) | Serialises the frame: 20-23 FPS. Removed. |
| Hold the flip's present and the FIFO drain until due, vblank still ticking (`frame_clock`) | Same serialisation: 24.8 FPS, median 39.7 ms frame. Reverted. |
| Pace in `gcm_flip_request` for in-FIFO flips (`_cellGcmSetFlipCommand`) | No effect: Buzz writes its flip method straight into the FIFO, so no guest import runs per flip. Reverted. |
| Complete flips only every 2nd vblank (`cellGcmTickFlip`) | No effect: Buzz does not wait on flip status. Reverted. |
| `cellSysutilCheckCallback` as a per-frame point | Not per frame: ~67 calls/s regardless of FPS. |

## Where it stopped

A cap that touches the RSX thread in any way serialises this title. The cap
has to delay a **guest PPU thread once per frame**, like a PC main-loop
limiter, and leave the drain alone. The next step was finding that point: a
temporary probe (`HLE_RATE=1`, per-thread per-import call rates every 5 s in
`ps3_hle_call`) was being run to find an import whose rate tracks the FPS on
the render/main thread. One main-menu sample (title FPS ~30) showed guest
thread 1 calling `cellGcmGetFlipStatus` (0x72A577CE) ~22/s and a group of
imports (0x40E895D3, 0xA703A51D, 0x6D2D9339, 0xB2E761D4) at exactly 18/s:
likely per-frame or per-tick points on the main thread worth tracing first. The
probe has been removed again. Candidates to check:

- per-frame HLE imports on the main thread (the probe's goal);
- the guest's PPU wait-site polls on RSX labels (`ps3recomp` usleep/flag wait
  sites, `PS3_USLEEP_*`), where a frame-start label wait could be delayed;
- the flip word decode in the FIFO drain, with the wait moved to the *guest*
  thread that next blocks on a fence, instead of the RSX worker.

Once such a point exists, call a pacer there that uses
`ps3_host_frame_deadline()` (still in `host_controls.c`, covered by
`tests/host-controls-test.cpp`), and measure with `BUZZ_RENDER_PERF=<csv>`
(frame intervals: median, p90, p99, count over 40 ms).
