# Native geometry CPU-cost measurements

CPU-cost evidence complements the phase-2 FPS gates. Less work per completed
frame can matter when another stage limits FPS. Current kernel microbenchmarks
are elapsed-time samples, not proof of whole-worker cycle savings.

`perf/measure-process-work.py` is an opt-in, read-only observer. It never
suspends threads or writes target memory. It reads OS process/thread CPU time
and cycle counters, plus only two map-bound renderer counters: the existing
QPC epoch and actual presentation serial. This aligns the CPU window to the
same renderer-relative phase as the frame CSV, without its buffered-file lag.
The executable digest, map digest and reviewed symbol RVAs are bound before
measurement; attachment also checks the explicit PID's executable path.

CPU time and cycles are independent measurements. Do not divide cycles by
nominal GHz: [Microsoft's cycle-counter documentation](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime)
describes hardware-dependent timer behavior and forbids that conversion.
CPU time comes from the OS user/kernel-time counters instead.

The observer reports the whole process and all four named `SPURS pool lane`
threads, normalized per completed frame. Whole-process cost includes work
that a submitting thread may claim from the pool. Raw snapshots retain
per-thread identities, names and counters. Missing/duplicate/recreated/exited
pool lanes, counter resets, late attachment, missed boundaries and unreliable
frame denominators fail the measurement. Frame counts bracket both counter
queries, exposing endpoint uncertainty rather than silently rounding it away.

Create a binding from a matching frozen EXE and its linker map:

```powershell
.\toolenv\Scripts\python.exe perf/measure-process-work.py bind --exe build-vulkan-batch1/BuzzQuizWorldRecomp.exe --map build-vulkan-batch1/BuzzQuizWorldRecomp.map --output analysis/native-geometry/phase2/cpu-batch1-binding.json
```

For a fresh run, attach before the selected renderer window starts:

```powershell
.\toolenv\Scripts\python.exe perf/measure-process-work.py measure --pid <game-pid> --exe build-vulkan-batch1/BuzzQuizWorldRecomp.exe --binding analysis/native-geometry/phase2/cpu-batch1-binding.json --start 40 --end 98 --output analysis/native-geometry/phase2/<run>.cpu.json
```

Use 40..98 s for the established menu phase and 150..220 s for the round.
Run a separate matched, reversed-order series with identical observer settings
in every arm. Do not retrofit CPU collection into an ongoing FPS series.
Keep the EXE, initial save, scene, rendering/audio settings and counter method
constant; compare worker and process cost alongside FPS/frame times.

The scene runner can launch the observer automatically. Use a separate series,
with no renderer readbacks in any arm:

```powershell
.\perf\measure-geometry-kernel.ps1 -BuildDirectory build-vulkan-batch1 -Tag p2-cpu-menu2 -KernelMask @(2,4,1024) -BaselineMask 1 -Scenes Menu -NoDumps -CpuBinding analysis/native-geometry/phase2/cpu-batch1-binding.json -SaveTemplate analysis/native-geometry/private-gamedata
.\toolenv\Scripts\python.exe perf/geometry-cpu-report.py analysis/native-geometry/phase2/p2-cpu-menu2/progress.jsonl --output analysis/native-geometry/phase2/cpu-menu2-report.json
```

The report requires a complete symmetric single-scene series, fixed EXE,
initial save, binding and observer source hashes, and equal methods/windows.
It verifies each CPU file's completed-run hash and recomputes the normalized
cost from raw snapshots. A cached summary cannot conceal a counter reset.

Tool status: synthetic normalization/identity/map checks and Windows API
self-tests pass. First game trial `p2-cpu-menu1` failed the 50 ms boundary
gate because thread discovery ran on the boundary. That incomplete series
is invalid. Discovery now runs one second before the window; boundary
queries retain only the four named pool handles and the process. Endpoints
still require stable live thread identities and the same strict tolerance.
Matched series `p2-cpu-menu2` completed all eight menu runs. The report finds
skinning pool cycles/frame -0.95% (pairs -1.23%, -0.66%), process cycles/frame
-0.73% (pairs -0.91%, -0.54%) and process CPU ms/frame -1.20% (pairs -1.64%,
-0.75%). Pool CPU time is mixed. Encoder and pointer series show no repeatable
savings. Full results and default decisions are in
[the phase 2 report](native-geometry-phase2-results.md).


## Interpreter diagnostics are a separate series

`perf/observe-spu-interpreter.py` binds existing interpreter statistics to an
exact frozen EXE/MAP. Start a fresh game with `SPU_INTERP_STATS=3600` and
`SPU_INTERP_ENTRIES=1`; use the scene runner's `-InterpreterBinding`,
`-InterpreterOutput` and `-InterpreterAt` parameters. The runner rejects a
combined interpreter/CPU observation. These diagnostic runs do not count as
FPS or CPU-cost A/B runs. Raw counter bytes remain inspectable.

The absolute-zero case is useful below the logger's65,536-step threshold.
Positive legacy counters are non-atomic observations; they do not establish
exact per-image counts or unchanged workload by themselves. On ABI48, both
menu arms remain absolutely zero at15/60s. Actual-round arms have the same
five image2 pages at240s and matching whole-run image2/image40 entry keys;
this leaves the round instruction-count gate open. See the
[phase3 diagnostics](native-geometry-phase3-results.md#map-bound-interpreter-diagnostics-on-abi48).


## Phase3 combined whole-job result

The first reversed JOB0/JOB1 menu series finds **higher**, not lower, normalized
CPU cost: worker cycles/frame+15.92%, process+9.19%, worker CPU ms/frame+16.02%
and process+9.13%, with both pairs agreeing. Separate FPS pairs regress8.78%.
The [phase3 performance report](native-geometry-phase3-performance.md) binds
the actual EXE/MAP/settings and records the per-run data. Defaults remain off;
individual option savings cannot be inferred from this aggregate test.


## Isolated formatter span proof

A separate same-EXE option reversal keeps JOB1/all other native options fixed:
SPANS1 lowers menu worker cycles/frame3.17%,process2.07%,worker CPU ms/frame4.25%
and process2.40%,with both pairs lower. The separate FPS series improves2.88%.
This is a measured individual-option saving;it does not reverse or supersede
the earlier aggregate JOB0/JOB1 regression. Round checks remain open and the
option stays default0. Exact method and per-pair values are in the
[phase3 performance report](native-geometry-phase3-performance.md#formatter-span-proof-isolated-menu-measurements).


## Isolated span proof: round CPU on the capture-enabled EXE

Four separate SPANS0/1/1/0 round CPU runs on3df8d057/MAP9b027e64 keep actual
JOB1 and every other production option enabled. Worker cycles/frame fall4.01%,
process2.82%;worker CPU ms/frame falls4.45%,process3.38%. Both reversed pairs
agree for every metric. Raw OS counters,stable lane identities,boundaries,
completed-frame denominators and inspected native round readbacks validate.

This is normalized CPU evidence for one option within the native driver.
It does not supersede the separately bound V3 aggregate JOB0/JOB1 regression.
The primary separate round FPS series improves1.91% on average but has a mixed
p99;CPU-series frame times are contextual. Same-EXE menu FPS/CPU and round tail
follow-up remain required. SPANS stays default0. See the
[round CPU table](native-geometry-phase3-performance.md#isolated-formatter-span-proof-round-cpu).
