# Image38 shuffle-control experiment, 2026-11-15

Hoisting invariant shuffle controls from image38's 0x0138 loop reduced isolated
loop time by about 20%, but did not produce a repeatable game FPS benefit. The
private prototype was not promoted. The generated workspace was restored and
rebuilt byte-identically to the committed image36 candidate; the installed
executable was unchanged throughout.

## Contract and checks

The pinned loop has six single-source byte shuffles whose controls remain
unchanged for the entire call. The prototype prepares their SSSE3 indices once.
It admits ordinary selectors and zero selectors; selectors that produce FF or
80 constants retain the original function. Owned LS, disabled LS probes and
runtime diagnostic gates are required. Every temporary GPR, LS byte and return
control is preserved. LS read/write order and all other operations are unchanged.
The adapter retargets the incoming branch and entry table while keeping the
original function and its PGO identity.

ASan/UBSan passed 9,072 complete context/LS/exit comparisons and 6,144
mutation-free selector rejections. Inputs included every admitted selector in
every byte lane, arbitrary register values, wrapped LS addresses and nested
and top-level returns. A separate `restrict`-qualified copy of the original
loop also passed these state checks, but provided no isolated speedup.

The Clang `-O2 -mavx2` kernel benchmark alternated original/hoisted/restrict
and reversed that order. For 64 iterations, means were 19.34 ns per iteration
original and 15.25 ns hoisted; a single iteration was slightly slower after
admission. This benchmark used actual-format shuffle masks with synthetic
registers and memory; it is not a whole-job or game measurement.

Four whole-job replay arms used 1,500 question captures repeated 10 times in
off/on/on/off order: 60,000 executions. All 1,500 captures remained exact,
with zero concurrent-input and zero failed verdicts; all complete ordered
transfer streams and exit states matched between modes. Means were 12.39,
11.66, 12.45 and 12.11 us per job. These overlap, despite the isolated loop
reduction. An untimed diagnostic admitted all 4,096 sampled calls, comprising
176,704 loop iterations. The game build had no PGO mismatch warnings.

## Live results

Four uncapped 90-second runs used the same frozen executable in off/on/on/off
order, with the committed image36 search enabled throughout. Fresh private
runtimes and cold shader caches used 1280x720 at 100% scale, VANGOGH, mailbox,
512-draw batches, two contexts and zero lane spin. Effective-setting logs
matched. No compilation, replay or profiler overlapped the runs. FPS covers
40–70 seconds, CPU samples 45–65 seconds, and screenshots were taken at 80 s.

| Arm | Shuffle | FPS | p95 ms | Lowest one-second FPS | CPU ms/frame |
|---|---|---:|---:|---:|---:|
| A1 | off | 28.07 | 43.303 | 23 | 153.95 |
| B1 | on | 28.57 | 42.204 | 27 | 155.58 |
| B2 | on | 28.03 | 43.748 | 23 | 155.54 |
| A2 | off | 28.47 | 43.036 | 23 | 155.29 |

Enabled and disabled means were 28.30 and 28.27 FPS, respectively. CPU means
were 155.56 and 154.62 ms/frame. The first enabled run's apparent improvement
did not repeat. Inspected B2/A2 captures have the expected menu, geometry and
UI. Fixed-time windows do not certify identical camera phases, and a capture
outside the window does not resolve that uncertainty. Neither mode sustains
30 FPS.

Candidate SHA-256:
`45258e70d95fff010c972642111e41648c8d9e1415ad2da300d5b4affca7e011`.
Restored build SHA-256:
`591aa41c7d50bbccfb72f5a36632d3ff037e69128f0236fd3fe5a1d6971a39f8`.
Private generator, synthetic fixture, build and restore logs, replay outputs,
frame CSVs, telemetry and screenshots are retained under
`/home/deck/buzz/performance/optimization/codex-image38-shuffles/`.
