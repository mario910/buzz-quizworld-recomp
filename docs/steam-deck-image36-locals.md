# Steam Deck image 36 register locals, 2026-11-15

Keeping the pure image 36 leaf's registers in host locals reduced isolated
kernel and replay time, but did not establish a live FPS or process CPU gain.
The candidate is archived and the production source is restored. The installed
executable remains unchanged; stable 30 FPS is still unachieved.

The generator retained every SPU operation and LS access, initialized all 78
used registers and flushed all 49 written registers at the sole return. It
rejected unexpected control flow, partial register writes and context effects.
LS probes or foreign LS storage fell back to the original native function.
The experimental `BUZZ_IMAGE36_LOCALS` switch defaulted off.

Validation passed 4,000 synthetic full-state cases, including foreign LS,
arbitrary FP bits and caller FP controls, and 128 live leaf captures repeated
100 times under ASan/UBSan. Five deliberately injected faults were detected.
Each switch setting also passed 500 whole-job captures repeated 20 times:
10,000 executions per mode, not 10,000 distinct jobs. All 500 complete ordered
transfer streams and exit states matched between modes. There were no PGO
mismatch warnings in the integrated build.

With the production forced shuffle header and PGO profile, the isolated leaf
measured about 31.27 us original versus 27.78 us local. Whole-job replay means
were 177.52 versus 166.12 us, from sequential, non-alternating timing runs.
These measurements are not evidence of game FPS gains.

Four independent 90-second live runs used the same frozen executable, fresh
runtime directories and cold shader caches. Measurement was 40–70 seconds,
with process CPU sampled within 45–65 seconds. No builds, tests or profilers
ran concurrently. Effective renderer settings matched: VANGOGH, 1280x720,
100% scale, uncapped mailbox, 512-draw batches, two contexts and zero lane spin.

| Arm | Locals | FPS | Median ms | p95 ms | CPU ms/frame |
|---|---|---:|---:|---:|---:|
| A1 | off | 26.00 | 37.660 | 47.157 | 161.47 |
| B1 | on | 26.33 | 37.612 | 45.231 | 163.51 |
| B2 | on | 26.17 | 37.799 | 46.266 | 163.14 |
| A2 | off | 26.40 | 37.653 | 44.796 | 162.82 |

Means were 26.20 FPS off and 26.25 on, within run variation. Process CPU was
162.15 versus 163.33 ms/frame. The inspected B2 capture at 80 seconds showed
the expected menu without an evident rendering defect; it does not establish
scene equivalence throughout the measured windows. No actual-round gate was
claimed for this candidate, and it was not promoted or installed.

`/home/deck/buzz/performance/optimization/codex-image36-local/` retains source,
patch, frozen executable and hash, synthetic and captured fixtures, replay
outputs, raw frame CSVs, telemetry, screenshots and measurement scripts.
