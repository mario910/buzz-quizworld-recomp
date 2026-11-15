# Image39 scan experiment, 2026-11-15

The private image39 0x0298 scan improved replay throughput but did not improve
live menu FPS. It was not promoted. The installed executable remains unchanged.

The pinned loop searches the first word of consecutive LS quadwords. A scalar
prototype and a four-record SIMD prototype find its final iteration, reproduce
every register update, and execute the original suffix. The bounded input span
must fit owned LS; unsupported inputs fall back without mutation. This is an
internal loop with no dispatch boundaries, channel operations or FP arithmetic.
Runtime diagnostics and interrupts retain the original function. Original
function bodies and PGO names were preserved by retargeting incoming entries.

Both variants passed 35,304 full-context, LS and exit comparisons under
ASan/UBSan, plus three mutation-free rejection checks. Cases included first
matches, duplicate matches, misses, all 16 address low bits, LS boundaries,
high address bits and independent values in every register lane.

Six sequential replay arms used 400 menu jobs repeated 25 times per arm:
60,000 executions, in off/scalar/SIMD/SIMD/scalar/off order. All 400 complete
ordered transfer streams and exit states matched across all arms. Capture
verdicts remained 52 exact, 348 with concurrent-input changes, zero failed;
this is not 400 exact captures. Replay means were 249.44 us off, 243.07 us scalar
and 246.47 us SIMD. An untimed counter run admitted all 28,672 sampled calls,
with 617,055 scanned records. No PGO mismatch warnings appeared in the build.

## Live comparison

Six uncapped 90-second runs used fresh private runtimes and cold shader caches,
1280x720 at 100% scale, VANGOGH, mailbox, 512-draw batches, two contexts and zero
lane spin. Effective-setting logs matched. No build, replay or profiler ran
concurrently. The 40–70-second window measured presentations; CPU samples were
inside 45–65 seconds, and screenshots were taken at 80 seconds.

All arms enabled the previously qualified image36 search. A is the frozen
image36 candidate, C is the new executable with image39 search disabled, and
B is the same new executable with scalar search enabled.

| Arm | FPS | p95 ms | Lowest one-second FPS | CPU ms/frame |
|---|---:|---:|---:|---:|
| A1 | 27.37 | 45.975 | 23 | 157.55 |
| C1 | 28.23 | 43.415 | 24 | 155.81 |
| B1 | 27.97 | 43.005 | 26 | 157.64 |
| B2 | 28.13 | 42.661 | 24 | 155.63 |
| C2 | 28.00 | 43.372 | 21 | 156.99 |
| A2 | 28.07 | 42.845 | 23 | 156.62 |

The same-executable means were 28.05 FPS on and 28.12 off. CPU means were
156.63 and 156.40 ms/frame respectively. The older executable averaged 27.72;
its two runs span 0.70 FPS. This does not support promoting the scan. Inspected
B2/C2/A2 captures show the expected menu, geometry and UI. Fixed elapsed-time
windows do not establish identical camera phases, and occasional faster
one-second bins near the window's end further limit cross-session comparisons.

Candidate SHA-256:
`9987a3bd6153d6ef083bef9b81f65b67fd42f0aab446a2b59d15380da89b46d4`.
Image36 reference SHA-256:
`591aa41c7d50bbccfb72f5a36632d3ff037e69128f0236fd3fe5a1d6971a39f8`.
Private source, synthetic fixtures, build logs, complete replay output files,
frame CSVs, telemetry and screenshots are retained under
`/home/deck/buzz/performance/optimization/codex-image39-search/`.

## Separate renderer packet probe

A private FLOAT3/CMP1/HALF2 fetch prototype combined adjacent position and
normal loads into one validated 16-byte read, preserving the exact decoding.
ASan/UBSan checks passed 24,120 interleaved plans, all attribute permutations,
unaligned inputs, guarded end pages, alias fallbacks, exhaustive half values,
CMP rounding modes and 10,000 mixed/fallback plans. The candidate was admitted
26,584 times. Kernel reductions were about 0–6%, depending on vertex count and
index pattern. No live renderer integration or FPS gain is claimed. Sources,
kernel timings and a summary are under `codex-renderer-packets/` beside the
image39 experiment.
