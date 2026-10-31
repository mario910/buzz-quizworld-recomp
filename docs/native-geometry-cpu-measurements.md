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

Tool status: synthetic normalization/identity/map checks and Windows API
self-tests pass. The first game CPU capture and matched CPU series are still
pending; no CPU-cycle reduction is claimed yet.
