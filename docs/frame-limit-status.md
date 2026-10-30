# 30 FPS frame lock

**Implemented on Vulkan and D3D12; off by default.** The launcher preference
and Escape menu toggle use the same live setting. A cap limits fast frames;
it cannot raise a demanding scene to 30 FPS.

## Where the wait belongs

`port/frame_limit.cpp` wraps the real `cellGcmResetFlipStatus` import
(NID `0xB2E761D4`). Buzz calls it once after its frame wait, before beginning
the next frame. On EU 1.00 this is the return PC `0x00268B00`, after the
`cellGcmGetFlipStatus` poll in `func_00268A90`. The hook is registered by NID,
so it needs no executable-address patch or release-specific binding.

The wrapper reads the SDK's `ps3_host_get_frame_limit()` setting, but uses
the port's `FramePacer` in `port/frame_pacer.h` for deadlines and sleeps on
**that guest PPU thread only**. Small overruns preserve the 30 Hz phase;
the SDK's deadline function rebased on every late frame, losing throughput
even when the average workload fitted within the frame budget. For example,
alternating 28/36 ms of work sustains 30 FPS with the new schedule but only
about 28.8 FPS with the previous schedule. This workload is a regression test.
Windows waits use a high-resolution timer with a maximum 200 us spin tail.
Each thread owns its pacer, accommodating threaded loading/display paths.
It then calls the original reset implementation. The RSX worker, FIFO drain,
vblank clock and audio mixer keep running. Disabling clears the deadline;
overruns of a whole period and menu pauses discard old deadlines rather than accumulating
catch-up debt. The Vulkan menu's existing host pause still holds the game.

When capped, the renderer also signals completion immediately after presenting
an executed flip. Waiting for the next synthetic vblank added another delay
on top of host pacing. `cellGcmTickFlip` still checks actual FIFO execution;
this does not complete unsubmitted frames or bypass rendering fences. Vblank
delivery itself remains unchanged.

The old present-side sleep was unsuitable: the RSX worker also writes the
labels that Buzz waits on throughout each frame. Sleeping there held up
rendering fences as well as presentation.

## Duplicate presentations fixed

The frame clock's post-drain flip-count path presented a frame without
consuming its pending-flip flag. The next iteration consumed that flag and
presented the same frame again. It now claims the flag in both paths.

This inflated the old title FPS/CSV counts and produced short duplicate
intervals between real frames. **Do not compare the previous reported
35+ FPS with the corrected count as a performance baseline.** After the fix,
the `buzz-progress` flip and presentation counters agree, including long
runs and repeated live cap changes. Paused menu redisplays are separate.

## Existing ps3recomp settings

The pinned fork already supplies host frame-limit controls. This change
connects those controls to Buzz's frame boundary using a port-specific pacer;
it requires no SDK update. The template's `config.toml` also lists `vsync`,
but it is reference configuration, not a file the runtime reads. Upstream
documents that limitation in its
[getting-started guide](https://github.com/sp00nznet/ps3recomp/blob/master/docs/GETTING_STARTED.md).
Vulkan mailbox presentation is retained; it does not itself impose 30 FPS.

## Revised pacer validation (2026-10-30)

Both renderers were rebuilt. `test-frame-limit.ps1` passes, including the
alternating 28/36 ms workload: 600 frames take approximately 20.003 seconds
with the port pacer versus 20.8 seconds with the SDK deadline function.
D3D12's language-screen live-switch check also passed: 62.51 FPS off,
30.00 on, 62.46 off (`analysis/frame-limit-phase-d3d12.csv`).

Vulkan, EU 1.00 solo challenge, same machine/settings as below. The ordinary
internal buzzer script reached the round in this run (background screenshot
confirmed the studio, Adam and score display). Live cap switches started at
180 seconds. No captures or compilation occurred in these measurement windows.
`analysis/frame-limit-phase-game.csv` contains the complete run, including
the slower sections, not just the successful cap interval.

| Window (s) | Cap | FPS | Median ms | p90 ms | p99 ms |
|---|---|---:|---:|---:|---:|
| 183–193 | Off | 31.78 | 30.92 | 34.70 | 37.16 |
| 198–208 | 30 | 30.00 | 33.13 | 37.65 | 40.79 |
| 213–223 | Off | 31.82 | 31.10 | 34.60 | 37.26 |
| 228–238 | 30 | 20.57 | 49.18 | 56.60 | 67.60 |
| 243–253 | Off | 26.42 | 34.73 | 50.71 | 60.32 |
| 258–268 | 30 | 29.17 | 33.89 | 37.65 | 40.76 |
| 273–283 | Off | 30.63 | 32.31 | 36.82 | 40.01 |
| 288–298 | 30 | 22.99 | 41.92 | 47.58 | 50.81 |

The first off/on/off sequence demonstrates 30 FPS with little spare throughput.
It is **not a claim of a stable lock throughout the game**: subsequent camera
and round changes still cause large drops. Mean FIFO/render work per frame
rose from 23.7 ms in the first capped window to 36.5 ms at 228–238 s. The
changing workload prevents attributing the later differences solely to the
limiter or using adjacent windows as identical-load comparisons. There are
also individual presentation intervals above 40 ms even in the 30 FPS window.

Revised EXE SHA-256:

- Vulkan: `bdc6304dbf90d7852b2c2def234287969baec1e05d7090e15304b0fceaa2fa2a`
- D3D12: `f453f830f7fae254f6fb595f5bbe8172b34fe8d966905c1484a89db0e23a85b6`

## Initial implementation measurements (2026-10-30)

The tables below describe the first CPU-side implementation using the SDK
deadline arithmetic, before the phase-preserving pacer and immediate flip
completion revision. They must not be presented as validation of that revision.

EU 1.00, Ryzen 9 7900, RTX 4060 Ti, 1280x720, supported AVX2/native build
with clang-cl. Normal audio/video/effects/jobs remain enabled; unattended
runs skip startup films and mute device output. No compilation or captures
run during the timing windows below. `BUZZ_RENDER_PERF` records completed
host presentations.

The language screen is deliberately stable and can exceed 30 FPS. The
internal menu test alternates off/on every 15 seconds through the actual
menu's preference action, without saving player settings. Boot and switching
edges are excluded; windows are seconds from the CSV epoch.

| Renderer / window | Cap | FPS | Median ms | p90 ms | p99 ms | Frames >40 ms |
|---|---|---:|---:|---:|---:|---:|
| Vulkan, 5–13 s | Off | 62.47 | 14.99 | 19.81 | 31.69 | 0 / 500 |
| Vulkan, 17–28 s | 30 | 29.96 | 32.69 | 36.43 | 38.78 | 1 / 329 |
| Vulkan, 32–43 s | Off | 62.45 | 14.94 | 19.54 | 32.01 | 1 / 687 |
| Vulkan, 47–58 s | 30 | 29.96 | 32.81 | 36.13 | 37.94 | 0 / 330 |
| D3D12, 5–13 s | Off | 62.55 | 16.92 | 17.83 | 30.71 | 0 / 500 |
| D3D12, 17–28 s | 30 | 30.00 | 34.01 | 35.10 | 37.00 | 0 / 330 |
| D3D12, 32–43 s | Off | 62.44 | 16.89 | 17.84 | 30.83 | 0 / 687 |
| D3D12, 47–58 s | 30 | 29.53 | 33.90 | 35.00 | 37.36 | 1 / 325 |

A further Vulkan run held the same 3D profile-load screen and switched
off/on/off in one process. This checks that a scene capable of 35+ FPS can
reach the target with the cap enabled, rather than dropping into the twenties:

| Window | Cap | FPS | Median ms | p90 ms | p99 ms | Frames >40 ms |
|---|---|---:|---:|---:|---:|---:|
| 123–133 s | Off | 39.14 | 24.72 | 29.10 | 37.78 | 3 / 391 |
| 138–148 s | 30 | 29.96 | 33.14 | 37.44 | 41.97 | 9 / 300 |
| 153–160 s | Off | 38.38 | 25.04 | 32.02 | 37.39 | 0 / 268 |

The same run then reached a solo challenge with profile Adam, confirmed by a
background window capture. During round entry, 228–232 s with the cap on
averaged 28.90 FPS (median 34.42 ms); 243–253 s uncapped averaged 30.50 FPS
(median 32.34 ms). Those moving camera/scene windows are not an identical-load
comparison. A separate 2.327 s transition stall at 234.762 s remains; the cap
does not solve scene-loading or shader hitches. Do not discard that stall
when reporting the whole-round experience.

The initial timed solo-navigation attempt stopped on round configuration;
it verified an eight-second host menu pause/resume there, not in a question.
The later run required three extra internal red-buzzer presses to complete
profile selection. Fixed-delay navigation is therefore not a reliable
gameplay oracle by itself.

Initial implementation EXE SHA-256 values (superseded above):

- Vulkan: `7edbdb381c7dba5af1fdebc1c078774769fbc5dcd58050a8dee0508a8e53b80a`
- D3D12: `ec636b5f8039610a00b96b5882c357cbd3f88c0dcebccece47dfe8907345c0b8`

The animated studio menu also ran through repeated live toggles for 125 s.
Its camera/load changes across the run: sampled windows were about 23–31
FPS. These are not a fixed-workload speed comparison. They demonstrate why
the cap should not be described as a guarantee of 30 FPS throughout play.

Synthetic checks exercise the registered hook, actual timed pacing, guest
register preservation, per-thread state, disable/re-enable and stall deadline
arithmetic. Run:

```powershell
.\tests\scripts\test-frame-limit.ps1
.\perf\measure-frame-limit.ps1 -Scene Language -LiveSwitches -Limit 0 -Seconds 65 -Tag vk-live
.\perf\measure-frame-limit.ps1 -Scene Language -LiveSwitches -Limit 0 -Seconds 65 -Tag dx-live -BuildDirectory build-windows
.\perf\measure-frame-limit.ps1 -Scene MainMenu -LiveSwitches -Limit 0 -Seconds 125 -Tag menu-live
```

Measurements, logs, private settings and metadata (including EXE hashes)
stay under `analysis/frame-limit-*`. Test scripts restore their environment
and do not replace the player's saved settings. The automated menu mode is
`BUZZ_PC_MENU_TEST=frame-limit`; normal launches never enable it.

## Earlier approaches

These remain useful negative results:

- Sleeping in present, or holding the FIFO drain until a flip deadline,
  stalls Buzz's rendering fences.
- Pacing `_cellGcmSetFlipCommand` has no effect: Buzz writes flip methods
  directly into the FIFO rather than calling that import per frame.
- Completing flips on every second vblank does not cap this game's loop.
- `cellSysutilCheckCallback` runs independently of the rendered frame rate.

The earlier SDK header comment describing a FIFO-side limiter refers to a
discarded attempt; the port's guest-thread hook is the active implementation.
