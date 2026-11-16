# Persistent SPURS chain workers on Steam Deck

The existing `SPURS_CHAIN_WORKERS=1` option did not improve the current build.
Same-executable menu comparisons averaged 28.92 FPS with the option off and
28.65 with it on. CPU time and frame-time tails also worsened. The option
remains off; no runtime or SDK source was changed for this experiment.
Stable 30+ FPS remains unmet.

## Scope and checks

The pinned SDK can dispatch each chain slot through a persistent host worker
instead of creating a host thread on each start. Each worker serializes its
slot's callbacks. The callback still performs completion signaling, job-cache
cleanup and thread-context release; this is not a new retained-context cache.
The test executable is the committed image-7 SIMD reference, SHA-256
`d6420f0b0905ed62958f6c42cfcbfe740d8851295484631d99460ccec49b5b56`.
Compiler settings, PGO and executable bytes are identical in all live arms.

The existing `tests/chain-worker-test.c` fixture passed normally and with
ASan/UBSan. Each run submitted 48,000 callbacks through concurrent producers,
checking serial ownership per slot, persistent thread identity, shutdown
draining and restart. These checks cover the worker queue, not every guest
chain lifecycle. Whole-job replay bypasses this queue and was not used as
evidence for this option.

A single `/proc` thread snapshot after the menu measurement window verifies
activation: all three enabled runs have twelve `cwN` workers, and both disabled
runs have none. Snapshots occur after 72 seconds of renderer progress and
75 seconds of wall time. Their full thread lists and renderer timestamps are
retained. They do not measure the cost of creating threads.

## Clean menu comparison

Four 90-second runs used off/on/on/off ordering, fresh private runtimes and
shader caches, X11, 1280×720, 100% scale, uncapped mailbox presentation,
512-draw batches, two contexts and zero geometry-worker spin. Image-36 search
and image-7 conversion were enabled throughout. Effective renderer settings
match. No build, replay, profiler or other game overlapped the runs.

FPS covers seconds 40–70, process CPU samples cover 45–65, and captures occur
at second 80. All runs continued presenting past second 88, with one
presentation per CSV record in the measured window.

| Arm | Workers | FPS | p95 ms | Lowest one-second FPS | CPU ms/frame |
| --- | --- | ---: | ---: | ---: | ---: |
| A1 | Off | 28.9333 | 40.732 | 25 | 150.0365 |
| B1 | On | 28.6333 | 41.454 | 22 | 150.8503 |
| B2 | On | 28.6667 | 41.217 | 24 | 151.2015 |
| A2 | Off | 28.9000 | 40.430 | 24 | 149.8905 |

Mean FPS falls 0.92%, from 28.9167 to 28.6500. Mean process CPU time rises
0.71%, from 149.9635 to 151.0259 ms/frame. The mean of the per-run p95 values
rises from 40.5810 to 41.3355 ms. This small series does not establish a
universal regression, but it provides no reason to enable worker reuse.

## Scripted round and rendering

A separate 335-second enabled run reached active questions and a scoreboard.
It is unpaired and cannot establish a round-performance change:

| Window | FPS | p95 ms | Lowest one-second FPS | CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| Menu, 40–70 s | 27.9667 | 41.930 | 24 | 155.9699 |
| Question, 240–300 s | 36.3167 | 32.924 | 32 | 95.5644 |
| Late round, 306–315 s | 26.7778 | 42.538 | 25 | 138.5377 |

All five captures were inspected: four intact menus and an intact green
question/scoreboard scene with the Jules Rimet question and one correct answer.
These later captures do not certify every timed frame or identical camera
phases. Logs contain no observed crash, device-loss or nonzero native-mismatch
marker. This is a functional smoke check, not exhaustive queue qualification.

## Evidence and disposition

Private artifacts are under
`/home/deck/buzz/performance/optimization/codex-chain-worker-reuse/`:
source hashes, normal/sanitized test commands and logs, `qualification.json`,
the frozen executable, `live.py`, all five run folders, frame CSVs, CPU
telemetry, worker snapshots, captures, effective-setting comparison,
`summary.json` and `final-verification.json`.

The live driver completed and exited. The prepared workspace executable
remains `630007cb…`; the installed executable remains `189814d1…`.
The SDK remains clean at `79efc280…`. The committed SIMD executable remains
the reference for further experiments; worker reuse is not promoted.
