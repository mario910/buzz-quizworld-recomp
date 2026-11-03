# Profiling and native-job validation

Profile the current executable before choosing another optimization. Keep
performance observations tied to the exact build, scene and environment.
Absolute FPS from different execution environments is not a shared baseline;
the observed Codex/Claude difference has no established causal explanation.
Do not infer an aggregate speedup by adding gains from overlapping changes.

## Job acceptance

For a native change require:

1. Exact corpus replay of ordered DMA/atomic/channel events, all published
   output bytes and job exit controls. Use `BUZZ_REPLAY_CONTRACT=1`.
2. At least 10,000 completed live whole-job I/O comparisons. Report skipped
   comparisons and fallbacks separately.
3. Inspected native menu and actual round frames. A settings screen is not
   round coverage; a black capture or a different scene invalidates the run.
4. Reversed same-EXE off/on/on/off menu and round FPS comparisons, plus a
   separate series for worker/process cycles and CPU time per completed frame.
5. Default-on only after correctness/render gates and repeatable performance
   benefit or parity. Flat FPS can still accompany useful worker CPU savings.

Internal register/LS diagnostics are optional; the job boundary defines
correctness. Unsupported inputs must select the whole lift before mutation.

## Reproducible runs

Freeze builds as `build-vulkan-*`; manifests must include `renderer` and the
EXE hash. Keep the matching linker map and source binding. Preserve existing
frozen builds, private evidence and original saves; give each run a fresh
private save copy. Keep rendering, audio, resolution, frame cap, input timing
and capture settings fixed between arms.

Every run log must contain both `window present mode mailbox` and
`two-context asynchronous draw batches enabled` for this Vulkan profile.
Never run another game, compilation, CPU test or independent profiler during
a measurement. Debug checkers and instruction counters are separate runs.

`perf/measure-vulkan-scene.ps1` drives matched menu/round scenes.
`perf/measure-geometry-job.ps1` compares whole-job modes;
`perf/measure-geometry-kernel.ps1` isolates retained leaf groups.
The associated `*-report.py` and `scene-window-stats.py` tools validate and
summarize their artifacts. Use each runner's configured renderer-relative
window; do not mix old round windows with later navigation schedules.

Captures introduce work. Place a one-shot GPU readback before the timed
window, verify the PPM/PNG pixels, and inspect it after the run. Disable
continuous readback and profiling in FPS runs. A rejected or incomplete arm
requires a fresh complete matched series, not selective replacement of an
unfavorable result.

## CPU and function profiling

`perf/measure-process-work.py` binds an explicit EXE/map/PID and reads OS
process/thread CPU time and cycle counters plus renderer epoch/presentation
counters. It does not suspend threads or write guest memory. It reports the
whole process and the four named SPURS pool lanes per completed frame.

Missing/recreated threads, changed identities, counter resets, late attachment
and unreliable frame denominators invalidate the measurement. Preserve raw
snapshots and endpoint uncertainty. CPU milliseconds and cycle counts are
independent metrics; do not convert cycles to time using nominal GHz.

`perf/geometry-rip-report.py` attributes sampled worker instruction pointers
to image/functions. Use image-specific attribution and a matching map to rank
current hotspots. Function call counts alone do not measure worker cost.

## Optional interpreter diagnostics

`SPU_INTERP_EXACT=1` arms cumulative atomic instruction-dispatch counters by
image and 1 KiB LS page. It counts actual executor dispatches, including
STOP/HALT, rather than entries or elapsed time. Disabled/unarmed counters
cannot prove zero activity. `perf/observe-spu-interpreter-exact.py` is the
read-only observer; keep it out of FPS/CPU series. The older sampled/entry
diagnostics are not interchangeable with exact instruction counts.

There is no outstanding round interpreter-count gate or cold-format coverage
campaign. Use these tools only when a current profile justifies them.

## Keeping documentation useful

Store run artifacts under ignored `analysis/`, with a compact table and
decision when needed. Keep code and a durable description of its behavior in
Git; completed session logs, phase trackers and benchmark narratives do not
belong in the maintained documentation. Commit each validated increment.
