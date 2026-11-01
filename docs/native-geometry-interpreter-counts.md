# Exact interpreted-instruction diagnostics

`SPU_INTERP_EXACT=1` enables cumulative atomic counters for actual instruction
executor dispatches, attributed to image and 1 KiB local-store page. The
default is 0. Startup explicitly arms the counters before workers launch,
including runs that never enter the interpreter. Disabled or unarmed counters
cannot prove zero activity. This does not replace the legacy diagnostics in
previously frozen executables.

The counter advances immediately before `spu_step`, including STOP/HALT
dispatches. Compiled rejoins, HLE transfers and a diagnostic step-cap return
before execution add no count. It measures executor dispatches, not elapsed
time, CPU cycles, function entries or retired hardware instructions. A live
snapshot can suspend a thread between committing its count and entering the
executor; after resuming, that dispatch continues normally.

The 4,096-bucket inventory has atomic publication and increments, no periodic
reset and no top-N truncation. Signed image IDs and all 256 LS pages are
supported. Full inventory or overflow sets an explicit invalidity flag;
unattributed increments are preserved separately and saturation never wraps
to zero. Any such flag invalidates an exact comparison. The C snapshot API
requires all interpreter writers to be quiescent; individually atomic cells
do not make a multi-cell read instantaneous.

For a new frozen game and its matching map, create a binding, then read an
explicit PID from a separate diagnostic run:

```powershell
.\toolenv\Scripts\python.exe perf/observe-spu-interpreter-exact.py bind --exe <frozen-build>/BuzzQuizWorldRecomp.exe --map <frozen-build>/BuzzQuizWorldRecomp.map --output analysis/<new-binding>.json
.\toolenv\Scripts\python.exe perf/observe-spu-interpreter-exact.py snapshot --pid <pid> --exe <frozen-build>/BuzzQuizWorldRecomp.exe --binding analysis/<new-binding>.json --output analysis/<new-snapshot>.json
```

The observer checks the executable digest/path, fixed map spans and the
runtime ABI descriptor. It suspends every target thread, repeats enumeration
until two unchanged inventories, reads the counters and restores every
suspension it added. Existing suspension counts are retained. Missing/exited
threads, partial reads, malformed counters or failed cleanup invalidate the
snapshot. Raw bytes, per-image/page totals, renderer epoch/frame serial and
query boundaries are preserved. A newly claimed bucket with zero increments
is permitted. Failed snapshots produce no successful output file.

Suspension and per-instruction atomic updates perturb the process. Use this
only in dedicated diagnostics, with no FPS/CPU-cost conclusions. The geometry
benchmark runner explicitly sets `SPU_INTERP_EXACT=0` in every arm. No build,
CPU test or independent profiler may run while a diagnostic game is active.
Offline corpus replay can report counters after synchronous replay returns,
before any game worker starts; an invalid diagnostic returns a nonzero exit.

Checked native modes may also execute reference code. Record those settings
and distinguish a checker run from an actual-native comparison. Counts from
different wall-clock workloads, especially audio/polling in image 2, need not
be identical. Exact counters alone do not establish matched work or close the
round gate. Preserve image/page attribution, input/save/build bindings and
work denominators; matching function-entry inventories is insufficient.

## Foundation validation

`tests/scripts/test-spu-interpreter-exact.ps1` compiles the production
interpreter and verifies a literal loop's 16 dispatches across two LS pages,
STOP, explicit/compiled rejoins, HLE and the step-cap boundary. All 268,816
bytes of the context, including every GPR, LS, control/channel and host field,
match with the environment unset, 0 and 1 after clearing the host LS pointer.

Four recorder threads exercise 800,000 atomic updates across a shared key and
four private keys, including a negative image ID. Separate processes test
first-arm races, full inventory, bucket/total overflow and invalid settings.
The process fixture also captures an intermediate positive total while all
four writers exist, then verifies the exact final 800,000/five-bucket result.
A deliberately wrong ABI read fails, restores suspensions and lets the fixture
exit normally. Fifteen observer tests cover rejected evidence, inventory
stability and cleanup failures; eight legacy observer tests still pass. The
full game links successfully with startup arming and offline reporting.

These establish the diagnostic tool. New-build corpus and menu results follow
below; the actual round comparison and phase-3 interpreted-count gate remain
separate work.
Geometry defaults, strict new-kernel checks and the narrow existing Stage F
exception are unchanged. Strict Stage F reconstruction remains phase 7 last.

## Frozen corpus and menu acceptance

Compiled root `f183f68` / SDK `d82523ad` is frozen as
`build-vulkan-phase3-interpreter-exact-v1`, EXE `9691ca43` / MAP `a6bb229f`.
Generated geometry code is unchanged. Four corpus arms each pass all 19,508
jobs and 58,195 transfers with zero input divergence or failures: JOB2 checked,
JOB0 reference, JOB1 native, and JOB1 native with the observer disabled. The
three enabled arms report explicit enabled=1, flags=0, unbucketed=0, total=0,
buckets=0 after replay. Each enabled native driver executes 19,508 jobs with
zero whole-job fallback; JOB2 strictly compares all 19,508. Native observer
0/1 stage/family/native ledgers are identical.

Fresh separate reference/native menu diagnostics use the same EXE, mask827,
StageF1 and initial private save `92ed9caf`. Both snapshots in both arms prove
exact all-image zero. The reference read intervals are15.000307..15.409342s
and60.000280..60.424438s; native intervals are15.001061..15.314237s and
60.000741..60.323045s from the renderer epoch. All65/51 target threads are
suspended at the two boundaries. Native mode and nonzero shared-bank execution
are confirmed in runtime logs. These diagnostics provide no performance data.

The first scheduled menu attempt failed the private scheduler's0.25s admission
window and has no accepted result. Its error log, run metadata and CSV remain
preserved. The controlled process fixture already measured approximately0.29s
query spans; the accepted menu reads take0.313..0.424s. The second private
scheduler saves raw snapshots and actual boundaries, with a3s diagnostic
admission window. The public counter implementation, exact-byte checks and
rejection of invalid counters are unchanged. The first failed read's exact
latency was not recorded and is not inferred from the successful repetitions.

All315 source bindings,81 existing/new freeze pins and459 archived parent
proofs pass the acceptance preflight. Final sealing adds the game-binding pin.
Private raw snapshots, complete logs, source hashes and the failed attempt are
retained under `analysis/native-geometry/phase3`. The remaining count work is
the round's positive image/page inventory with a justified matched-work
comparison; zero menu counts do not settle that gate.
