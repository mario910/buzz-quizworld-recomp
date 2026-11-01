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

These establish the diagnostic tool. Actual new-build corpus/menu/round
comparisons and the phase-3 interpreted-count gate remain separate work.
Geometry defaults, strict new-kernel checks and the narrow existing Stage F
exception are unchanged. Strict Stage F reconstruction remains phase 7 last.
