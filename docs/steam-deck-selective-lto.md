# Selective geometry ThinLTO on Steam Deck

ThinLTO across the two geometry translation units did not improve the measured
frame rate. The candidate is not promoted; the stable 30+ FPS goal remains open.

## Build and correctness

The private experiment adds `-flto=thin` only to `native_geometry_job.cpp` and
`native_geometry_job_flow.cpp`. It links with LLVM 18 LLD, `--lto-O3`, four
ThinLTO jobs and a private cache. Original per-file PGO6, aliasing, wrapping
and floating-point flags remain intact. Source files and the SDK pin are
unchanged. A separate control changes only the linker to LLD, avoiding the
assumption that linker layout alone is neutral. No PGO mismatch was reported.

Both executables replayed all 8,196 entries in the image-38 question, image-39
menu, image-40 question and four image-41 corpora, with three requested
repetitions. Complete ordered DMA/exit files matched the retained baseline.
Recorded-capture verdicts also matched, including the existing image-39
concurrent-input cases and image-41 padding discrepancies documented in
[DMA experiments](steam-deck-dma-experiments.md). Overlapping blend/padding
corpora are not an independent-job count. The ThinLTO-built counter test
passed 7,200,000 concurrent updates, thread churn and overflow writers.

Disassembly confirmed that inlining occurred. The linker-only control kept
489 instructions in `buzz_geometry_job_native_eligible` and 2,513 in
`buzz_geometry_job_native_start`; ThinLTO produced 567 and 5,783 respectively.
These include cold branches, so static size/call counts are not CPU timing or
proof of the reason for the measured result.

## Matched live comparison

A is the frozen qualified build, C the linker-only control, B ThinLTO.
All enable image-36 search. Six private 90-second runs used FPS windows
40–70 seconds and CPU windows 45–65 seconds, in the order below.

| Arm | FPS | p95 frame ms | Lowest one-second FPS | CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 | 28.200 | 42.819 | 25 | 156.243 |
| C1 | 27.900 | 43.414 | 25 | 157.215 |
| B1 | 27.833 | 43.891 | 25 | 158.080 |
| B2 | 28.100 | 43.032 | 26 | 155.233 |
| C2 | 28.000 | 44.475 | 22 | 157.524 |
| A2 | 27.967 | 43.481 | 25 | 156.592 |

Means: reference 28.083, linker-only 27.950, ThinLTO 27.967 FPS.
Neither candidate demonstrates a net improvement. This narrow experiment
does not establish what whole-program or renderer-only LTO would do.

Effective renderer profiles matched: 1280×720, 100% scale, mailbox,
uncapped, 512-draw batches, two contexts, zero worker spin. All sampled
presentation deltas were one; B2/C2/A2 captures showed intact menus.
No builds, CPU tests or profilers overlapped the series. Elapsed windows
do not guarantee identical animated camera phases; do not pool these
figures with unrelated earlier sessions.

## Private evidence and restoration

Artifacts are in `/home/deck/buzz/performance/optimization/codex-selective-lto/`:
`configure.py`, `build.sh`, original Ninja backup, build logs, both frozen
executables, `qualify.py`, per-variant qualification JSON, output files,
`assembly-summary.json`, `compare-menu.py`, `menu-comparison.json`, captures
and `renderer-profiles.json`. No captures or generated code are committed.

SHA-256 identities:

- Reference/restored workspace: `591aa41c7d50bbccfb72f5a36632d3ff037e69128f0236fd3fe5a1d6971a39f8`.
- LLD-only control: `6170cfdc371113143177aa15beb6e3a0408efbd21575515c766d4d1f8babb440`.
- ThinLTO candidate: `f084749dc2fd229d69733d677351daece43d2f58aad787b530497fd111b47b1c`.
- Installed executable (unchanged): `189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.

The original Ninja file was restored and the executable rebuilt byte-identical
with the qualified reference. SDK `79efc280dad27271d5f68077acb0247df1e88dc7`
and installed files remain unchanged.
