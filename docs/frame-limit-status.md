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

## Vulkan frame-time work (2026-10-30)

Measured with corrected presentation counts only (`BUZZ_RENDER_PERF`,
after the duplicate-present fix). Same machine as below, EU 1.00, 1280x720,
uncapped, muted, background window, supported build flags. Scenes and
summaries are reproducible with:

```powershell
.\perf\measure-vulkan-scene.ps1 -Scene Menu  -Tag <tag> [-Env 'K=V']   # 100 s; window 40-98 s
.\perf\measure-vulkan-scene.ps1 -Scene Round -Tag <tag> [-Env 'K=V']   # round ~137 s after launch
.\toolenv\Scripts\python.exe perf\scene-window-stats.py [--per 10] 40:98 analysis\vk-scene-<tag>.csv
```

A/B pairs use the same EXE with a runtime switch, in reversed order; the
menu window covers the studio camera tour after warm-up, the round window
150-250 s covers questions with the full studio, avatars and score display
(checked with background screenshots). Adjacent menu/round windows have
different camera load, so compare only matching windows.

### Where the time went

Profile of the studio menu before the change (RIP sampler on the RSX
worker, `PS3RECOMP_VK_PROFILE=1`, `PS3RECOMP_RSX_ENGINE_PROFILE=1`): about
1000 draws and 100k FIFO methods per frame, FIFO drain 36 ms of a 46 ms
frame, i.e. the RSX thread was the bottleneck. Besides vertex fetch
(~16 % of the thread) and method dispatch (~12 %), one item stood out: the
depth-of-field pass samples the 2560x704 (2xAA) Z24S8 depth buffer as linear
A8R8G8B8. The engine handled that through guest memory **every frame**: a
full GPU wait, a 7 MiB depth readback, a CPU store, a content hash, a texture
decode and a 7 MiB re-upload with its own fence wait.

### 1. Depth sampled as A8R8G8B8 converted on the GPU

SDK `9f715799`. The Vulkan backend now builds those texels with
depth -> buffer -> compute -> RGBA8 image, recorded into the draw batch in
command order (no wait), and binds it through the usual crossbar view. The
result is bit-identical to the guest-memory bytes:
`PS3RECOMP_ENG_DEPTH_COLOR_CHECK=12` compared 12 menu frames x 1.8M texels
with 0 mismatches. `PS3RECOMP_ENG_DEPTH_COLOR=0` restores the old path (used
for the A/B below). D3D12 does not use the shared engine and is unchanged
(menu 26.5 FPS, median 40.0 ms, same window).

| Scene, window | Path | FPS | Median ms | p90 ms | p99 ms | Drain ms | >40 ms |
|---|---|---:|---:|---:|---:|---:|---:|
| Menu 40-98 s, run 1 | old | 24.68 | 42.06 | 51.51 | 60.23 | 29.89 | 57.9 % |
| Menu 40-98 s, run 4 | old | 23.64 | 41.53 | 56.33 | 76.45 | 31.57 | 56.1 % |
| Menu 40-98 s, run 2 | GPU | 26.84 | 39.21 | 47.77 | 55.23 | 20.35 | 46.5 % |
| Menu 40-98 s, run 3 | GPU | 25.76 | 39.15 | 51.17 | 73.19 | 21.25 | 46.6 % |
| Round 150-250 s, run 1 | GPU | 35.56 | 28.78 | 33.65 | 39.49 | 9.28 | 0.9 % |
| Round 150-250 s, run 2 | old | 29.11 | 33.56 | 41.21 | 53.82 | 21.40 | 12.6 % |
| Round 150-250 s, run 3 | GPU | 32.69 | 30.90 | 35.35 | 41.73 | 10.64 | 1.9 % |

The round's RSX work per frame halves (drain ~21 -> ~10 ms) and it now
stays above 30 FPS uncapped in this window. The menu's drain drops by
10 ms but its FPS rises only by ~2: the frame is now bounded by the SPU geometry
job chain, not the renderer. A 5 s `PS3_FTRACE` of the menu shows the main
PPU thread busy ~6 ms per frame and waiting on its event flag, four pool
workers each running ~21 ms of image 40/41 jobs per 39 ms frame at ~66 %
utilization inside 23 pool groups, and ~7 ms of serial chain work between
groups. `SPURS_JOB_LIST_WORKERS=5` halves the FPS (12.4) and is not an
option.

### 2. Pool lanes poll briefly before sleeping

SDK `0d6154fe`. Inside those pool groups each lane ran ~40 us jobs with
~14-25 us idle between them: the guest's SYNC commands split each frame
into ~165 stages of ~14 jobs, and at every stage boundary the lanes slept
on a condition variable and paid an OS wake-up. With
`SPURS_POOL_SPIN_US=n` an idle lane polls the queue for up to n us before
sleeping. Unset, the SDK picks 1000 with 12+ logical processors and 0
below (four polling lanes would compete with the guest PPU, RSX and audio
threads); a set value, including 0, is used as given. The startup log
says which (`[job-pool] idle lanes poll ... (automatic, N logical
processors)`). Polling costs CPU time on those four lanes; on this
24-thread machine both scenes still got faster.

| Scene, window | Spin | FPS | Median ms | p90 ms | p99 ms | >40 ms |
|---|---|---:|---:|---:|---:|---:|
| Menu 40-98 s | 0 | 27.51 | 37.91 | 47.04 | 54.02 | 40.6 % |
| Menu 40-98 s | 0 | 27.25 | 38.28 | 46.24 | 51.44 | 42.6 % |
| Menu 40-98 s | 200 us | 28.92 | 36.01 | 44.95 | 50.26 | 32.5 % |
| Menu 40-98 s | 200 us | 27.87 | 37.45 | 45.52 | 52.80 | 37.1 % |
| Menu 40-98 s | 1000 us | 29.98 | 35.63 | 43.03 | 48.90 | 23.9 % |
| Menu 40-98 s | 1000 us | 31.61 | 33.49 | 40.60 | 46.79 | 12.5 % |
| Menu 40-98 s | 3000 us | 30.28 | 34.44 | 43.24 | 49.40 | 23.8 % |
| Round 150-250 s | 0 | 31.86 | 31.56 | 36.36 | 41.85 | 2.5 % |
| Round 150-250 s | 0 | 32.67 | 30.92 | 35.63 | 40.86 | 1.4 % |
| Round 150-250 s | 1000 us | 36.33 | 27.84 | 32.01 | 37.45 | 0.5 % |
| Round 150-250 s | 1000 us | 35.85 | 28.12 | 32.36 | 37.65 | 0.6 % |

Run order: menu 200/0/0/200/1000/3000/1000, round 1000/0/0/1000. Every
10 s slice of the 1000 us round runs stayed at or above 33 FPS. D3D12
shares the SPURS runtime and gains as well: menu 26.53 -> 28.77 FPS
(median 39.97 -> 37.33 ms, same window).

### 3. Chain walkers poll briefly before parking on a JTS slot

SDK `3badb6a9`. The geometry chain's walker waits ~50 times per frame for
the other chain to publish its next JOBLIST (a JTS slot replaced by an SPU
DMA). `SPURS_JTS_SPIN_US=n` polls the slot for up to n us before parking on
its condition variable, with the same automatic default. Both arms below
have pool polling at 1000 us.

| Scene, window | JTS poll | FPS | Median ms | p90 ms | p99 ms | >40 ms |
|---|---|---:|---:|---:|---:|---:|
| Menu 40-98 s, run 2 | 0 | 30.68 | 34.63 | 41.60 | 45.76 | 18.9 % |
| Menu 40-98 s, run 3 | 0 | 30.38 | 35.13 | 41.83 | 47.00 | 20.0 % |
| Menu 40-98 s, run 1 | 1000 us | 31.23 | 33.63 | 41.07 | 45.36 | 14.2 % |
| Menu 40-98 s, run 4 | 1000 us | 31.39 | 33.59 | 40.91 | 45.37 | 13.7 % |
| Round 150-250 s, run 2 | 0 | 36.49 | 27.77 | 32.13 | 36.97 | 0.4 % |
| Round 150-250 s, run 1 | 1000 us | 37.94 | 26.59 | 30.99 | 35.76 | 0.3 % |

### 4. Job contexts clear only the job's declared regions

SDK `e47fe54f`. Each SPU job zeroed the whole local store past its binary
(~200 KiB) before staging, ~15 % of image 41 job time. With
`SPURS_JOB_LS_ZERO=1` (now the SDK default; 0 restores the full clear) a reused job context clears
only the regions the job is given (io/out/scratch/stack, cache padding and
the context/descriptor block); the rest keeps the previous job's bytes, as
on hardware. A round frame dump renders normally and the logs show no new
warnings. Mode 2 (context block only) is a diagnostic upper bound.

These runs (and the menu/round tables of section 5) fell in a slower
period on this PC: the same default build gave 25-26 FPS on the menu
instead of 30.5, PrintWindow captures came back black and the GPU idled
in P8 between runs. The cause was not identified (the screen lock was a
guess; `LockApp.exe` is also present during later, faster runs). Compare
only within each table.

| Scene, window | Mode | FPS | Median ms | p90 ms | p99 ms | >40 ms |
|---|---|---:|---:|---:|---:|---:|
| Menu 40-98 s | 0 | 25.09 | 43.02 | 50.98 | 54.60 | 55.6 % |
| Menu 40-98 s | 0 | 25.63 | 42.08 | 50.22 | 55.00 | 54.3 % |
| Menu 40-98 s | 1 | 26.37 | 40.59 | 48.86 | 53.46 | 51.4 % |
| Menu 40-98 s | 1 | 27.08 | 39.76 | 47.55 | 52.10 | 49.2 % |
| Menu 40-98 s | 2 | 28.63 | 34.12 | 44.79 | 51.88 | 25.3 % |
| Menu 40-98 s | 2 | 27.62 | 38.87 | 46.77 | 50.48 | 47.0 % |
| Round 150-250 s | 0 | 30.56 | 31.83 | 41.13 | 55.01 | 11.5 % |
| Round 150-250 s | 0 | 34.26 | 29.55 | 33.49 | 37.60 | 0.4 % |
| Round 150-250 s | 1 | 33.60 | 30.08 | 33.70 | 38.39 | 0.9 % |
| Round 150-250 s | 1 | 35.53 | 27.72 | 33.27 | 48.23 | 2.4 % |

Run order: menu 2/0/1/0/2/1, round 1/0/0/1.

### 5. Lift the geometry jobs' missed entry points

`SPU_INTERP_STATS=10` showed ~5.7 M SPU instructions per second still
interpreted on the pool lanes, in images 40 and 41 (LS 0x1000-0x3BFF).
`SPU_INTERP_ENTRIES=1` names the targets: function pointers and register
continuations the start heuristics cannot see. The 1.02 generator already
seeds such addresses (`EXTRA_STARTS`); the EU 1.00 generator had none.
`tools/codegen/build-disc100-reference.py` now seeds 37 addresses in
images 34, 35, 40, 41, 43, 45 and 47 (found in the menu and a round), each
guarded by its instruction word in the reviewed binary. Interpretation
drops to 0.1-0.3 M instructions/s (image 35 bootstrap only).

The image 40 and stage F native adapters pin their lifted sources by hash.
Their audited functions (image 40: 0x6DB8; image 41: 0x2C00, 0x63E0,
0xC518, probe 0x6E08) are byte-identical in the new lift, and the new
hashes were added beside the old ones. Regenerate the raw jobs (or the
whole 1.00 workspace) to get the new lift. A round frame dump renders
normally.

EXE A/B (previous build kept beside the new one), same slower period:

| Scene, window | Lift | FPS | Median ms | p90 ms | p99 ms | >40 ms |
|---|---|---:|---:|---:|---:|---:|
| Menu 40-98 s | old | 27.01 | 40.04 | 47.70 | 51.15 | 50.1 % |
| Menu 40-98 s | old | 27.06 | 39.70 | 47.66 | 52.42 | 48.9 % |
| Menu 40-98 s | new | 30.39 | 35.37 | 42.72 | 46.33 | 24.8 % |
| Menu 40-98 s | new | 30.65 | 34.90 | 42.15 | 45.77 | 21.7 % |
| Round 150-250 s | old | 32.95 | 30.63 | 34.20 | 37.78 | 0.5 % |
| Round 150-250 s | old | 33.80 | 30.27 | 34.18 | 37.85 | 0.4 % |
| Round 150-250 s | new | 36.46 | 27.57 | 31.24 | 35.08 | 0.4 % |
| Round 150-250 s | new | 32.05 | 30.88 | 37.65 | 52.95 | 4.9 % |

Run order new/old/old/new for both scenes. The last round run reached its
round on a shifted timeline (its first 10 s slice is the pre-question
transition at 27.9 FPS, ~45 in the others), so its window is not
phase-matched; its later slices run 32-33 FPS.

### 6. Direct-table vertex remap

SDK `49847626`. `rsx_vertex_remap_build` hashed every index of every
rebuilt draw (6.7 % of the RSX thread). When a draw's references share
one base index and span a compact range, a generation-stamped table
indexed by vertex id replaces the hash, with the same first-use order and
occurrence map (randomized check: 20000 cases, both paths). The PC was
back at full speed for this A/B; menu EXE A/B (new/old/old/new): FIFO
drain 20.06/19.82 -> 19.30/19.08 ms, FPS 35.99/36.45 -> 36.29/36.67. A
small render-thread saving, within FPS noise because the frame is not
render-bound here.

### Overall (all six changes)

Default launch settings after a full rebuild of both renderers
(`analysis/vk-scene-final2-*`), same scene windows:

| Scene, window | Session start | Now |
|---|---|---|
| Vulkan menu 40-98 s | 23.6-24.7 FPS, median ~42 ms, p90 51-56 ms, >40 ms 56-58 % | 36.26 FPS, median 29.55 ms, p90 34.83 ms, p99 39.20 ms, >40 ms 0.8 % |
| Vulkan round 150-250 s | 29.1 FPS, median 33.6 ms, p90 41.2 ms, >40 ms 12.6 % | 44.23 FPS, median 22.55 ms, p90 27.34 ms, p99 32.08 ms, >40 ms 0.2 % |
| D3D12 menu 40-98 s | 26.5 FPS, median 40.0 ms, p90 46.9 ms | 33.34 FPS, median 32.50 ms, p90 39.57 ms |

All six are on without any environment, including a direct EXE start:
the depth conversion, remap and lifts always; pool/JTS polling and the LS
regions mode through the SDK's own defaults above. A menu run with nothing
set gave 36.85 FPS (median 28.88 ms); setting `SPURS_POOL_SPIN_US=0`,
`SPURS_JTS_SPIN_US=0` and `SPURS_JOB_LS_ZERO=0` gave 31.93 FPS (median
32.50 ms) in the same session.

Every 10 s slice of the final round run is at or above 40.8 FPS. Both
heavy scenes now run above 30 FPS uncapped with headroom; the 30 FPS cap
itself was not re-measured with these changes.

### Overall (first three changes)

| Scene, window | Before | After |
|---|---|---|
| Vulkan menu 40-98 s | 23.6-24.7 FPS, median ~42 ms, p90 51-56 ms | 30.5-31.4 FPS, median 33.6-34.7 ms, p90 40.9-41.7 ms |
| Vulkan round 150-250 s | 29.1 FPS, median 33.6 ms, p90 41.2 ms | 37.9 FPS, median 26.6 ms, p90 31.0 ms |
| D3D12 menu 40-98 s | 26.5 FPS, median 40.0 ms, p90 46.9 ms | 30.6 FPS, median 34.7 ms, p90 41.9 ms |

Final rows are the default launch settings after a full rebuild of both
renderers (`analysis/vk-scene-final-*`), except the round, which is the
JTS A/B's polling arm. The round now holds 30 FPS uncapped in this window
with headroom, so the 30 FPS cap can keep it at a steady 30. The menu's
camera tour averages just over 30 but ~20 % of its frames still take more
than 40 ms. Remaining costs at that point (sections 4-6 then addressed
the first two and the remap), from the profiles:
SPU job setup zeroes each job's 256 KiB local store and copies its binary
(~15 % of image 41 job time); some pool-lane SPU code still runs in the
interpreter (~5 % of lane time); and on the RSX thread, per-draw vertex
fetch (`rsx_vertex_remap_build`, `rsx_vertex_decode_element`,
`rsx_vertex_fetch_one`, ~17 %) and FIFO method dispatch (~12 %).

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
