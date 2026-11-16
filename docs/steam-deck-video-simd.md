# Exact SIMD video conversion on Steam Deck

A guarded AVX2 path accelerates the image-7 YUV-to-RGBA conversion loop while
preserving complete guest output. The production menu comparison improves
28.47 to 28.85 FPS and reduces CPU time per frame by 3.36%. Exact arithmetic,
captured-state and live comparison checks pass. Stable 30+ FPS remains
unachieved; the roughly sevenfold isolated speedup is not a whole-game gain.

## Scope and admission

The current profile attributes about 2.98% of aggregate sampled user cycles
to `buzz_image7_8638_native`. This is CPU attribution, not critical-path wall
time. The pinned disc-100 leaf is `0x86B8`, exiting at `0x8FF0`; the existing
generated native symbol retains its older `8638` name.

The bulk path checks the seven coefficient/packing registers and all 34
loaded shuffle-table quads. It admits only internal local store, 1–1,024
iterations, aligned nonwrapping spans, disjoint output rows, and outputs
disjoint from all inputs and the table region. Every other case retains the
original native function. Production dispatch requires the pinned `0x86B8`
layout, the expected generated native callback, an AVX2/FMA build, and disabled
LS debug hooks. Older wrappers, other layouts, non-AVX2 and diagnostic builds
retain the original path. `BUZZ_IMAGE7_SIMD=0` disables the new path; eligible
builds enable it by default. Runtime/FPS qualification here is on Steam Deck.

Each bulk iteration converts 32 pixels from each of two rows, sharing 16
chroma samples. Eight pixels per vector retain the original U, V, then Y FMA
order under scoped RTZ/FTZ/DAZ. Final byte packing uses clamped integer
conversion, proven against the original SPU conversion below. Alpha is 255
for the admitted register constants.

The final iteration uses the original native body to reconstruct every
temporary register and the original PC/next-block publication. Source dataflow
identifies only registers 107–111 as loop-carried values; bulk execution
advances all four lanes with the original modulo-32-bit increments. The leaf
has no channel access, nested call or stop between these iterations.

## Capture and correctness evidence

A private recording wrapper captured 32 complete register/local-store inputs
between 40.004 and 46.660 seconds of a 90-second menu run. All captured calls
have 20 iterations. Presentation progressed through 89.805 seconds and the
80-second menu readback was visually intact. Recording FPS is excluded from
performance conclusions. The wrapper and build were then restored to the
byte-identical reference executable.

The prototype passed:

- All 16,777,216 possible YUV byte triples against the independent integer SPU
  FMA and unsigned-conversion oracle, checking all three RGB bytes exactly.
- All 32 real snapshots against the integer oracle and original native body,
  including complete context state, local store, PC and next-block target.
- 76 synthetic complete states with random pixels/registers, relocated buffers,
  counts 1–9, 20, 31–32, 63–65, 128, 255–256 and 512, and wrapping loop counters.
- 666 mutation-free rejections covering every guarded register/table byte,
  invalid counts, alignment, overlapping spans, range failures and external LS.
- ASan/UBSan on the new path and synthetic fixture, plus exact restoration of
  incoming MXCSR across the scoped calls. Original generated native/oracle
  objects provide the comparison and are not sanitizer-instrumented.

The first exhaustive fixture incorrectly passed zero to a nonzero-only
integer-packing primitive. That fixture failure was retained, corrected to
match the SDK conversion's explicit zero case, and the complete exhaustive
test rerun successfully. No candidate arithmetic change was needed.

## Isolated timing

The optimized test used the 32 captured calls, 1,000 repetitions each, in
original/candidate/candidate/original order. It times the scoped conversion;
context reset, capture loading and correctness comparisons are outside the
timer. Context copying warms memory, so this is not a cold-memory result.
No game, build or other test overlapped the timed series.

| Arm | ns/call |
|---|---:|
| Original A1 | 17,119.680 |
| Candidate B1 | 2,364.926 |
| Candidate B2 | 2,360.440 |
| Original A2 | 17,133.052 |

Means are 17,126.366 and 2,362.683 ns/call. These are complete leaf timings,
including admission and the final original iteration, but are not FPS gains.

## Prototype game qualification

The private game candidate enables the prototype with `BUZZ_IMAGE7_SIMD=1`;
`0` selects the original native path in the same executable. Only the prepared
image-7 wrapper translation unit and its compile flags change. The original
generated kernel and all existing PGO profiles remain unchanged; the wrapper
had no profile input and is compiled with O3, AVX2/FMA and strict RTZ settings.

The serial qualification sequence ran live image-7 comparison mode in a
90-second menu and a 335-second scripted round, then clean OFF/ON/ON/OFF menu
timing. Audit logs count actual optimized calls separately from fallback
calls. The sequence restored the prepared source/compiler settings and verified
the reference hash. The installed game is unchanged.

The completed menu audit reports 260,000 optimized calls, zero fallbacks and
zero mismatches, with presentation continuing through 89.799 seconds. The
335-second round audit also reports 260,000 optimized calls, zero fallbacks
and zero mismatches, with presentation through 335.107 seconds. Equal totals
do not establish conversion activity in every round phase. Both readbacks were
inspected and intact; comparison-mode FPS is excluded from performance claims.

Two clean same-executable menu series use opposite orders. FPS uses seconds
40–70, CPU uses seconds 45–65, with readback at 80 seconds of each 90-second run.
All eight menu captures and both clean-round captures were visually inspected
and intact. Effective renderer profiles match across both series and the round pair.

| Order | OFF FPS | ON FPS | OFF CPU ms/frame | ON CPU ms/frame | OFF p95 ms | ON p95 ms |
|---|---:|---:|---:|---:|---:|---:|
| OFF/ON/ON/OFF | 28.400 | 28.617 | 155.712 | 150.778 | 41.010 | 41.273 |
| ON/OFF/OFF/ON | 28.533 | 28.867 | 154.722 | 149.259 | 40.716 | 40.677 |

The repeated CPU saving is about 3.2–3.5%; the FPS leads are only 0.8–1.2%.
Frame-time tails do not show a consistent improvement, and both series remain
below the target. The clean scripted-round pair measures 36.183 FPS in the
240–300 second window with either setting. The 306–315 second window is 26.333
FPS OFF and 27.222 FPS ON. Readbacks at 330 seconds show different question
progress (Mona Lisa/zero correct versus Agatha Christie/one correct), so this
pair does **not** establish an equivalent-scene late-round gain. A fixed-time
input script and a later screenshot cannot certify equal work in every window.
The frozen prototype executable is
`3c5628584efc3d435c40e2b4aa07bf5c80da49f31988aff2ebf091c55f0a082f`.

## Production integration

The integration separates the AVX2 leaf from the wrapper,
passes the pinned layout explicitly, preserves the older wrapper ABI, and
retains original execution for other layouts, ISA targets and LS-probed builds.
The production tests now pass: exhaustive color arithmetic; legacy, old-layout
and alternate-callback routing; explicit ON/OFF and default selection; diagnostic
and non-AVX2 rejection; all 32 captures through the actual layout dispatcher;
76 synthetic full states; and 666 mutation-free rejections. ASan/UBSan covers the
new code and full-state fixture. The production CMake build succeeded without
stale-profile warnings. Existing per-object profile/tuning flags were retained;
the new leaf uses O3, AVX2/FMA and strict rounding, with no PGO input. The owner
keeps O1. The generated wrapper passes the pinned layout PC, while the legacy
three-argument ABI remains available and uses the original leaf.

The frozen production candidate is
`d6420f0b0905ed62958f6c42cfcbfe740d8851295484631d99460ccec49b5b56`.
Prepared source and compiler settings were restored before live qualification,
and the prepared executable rebuilt byte-identical to the reference. The SDK
and installed executable are unchanged.

A completed 335-second production comparison-mode round reports 280,000
optimized calls, zero fallbacks and zero mismatches. Presentation progressed
through 335.057 seconds. Its final readback shows an intact green scoreboard.

Clean production timing compares the frozen reference A against the new
candidate B in A/B/B/A order, using the same menu windows/settings as above.
The candidate uses its default enabled path; all runs explicitly use native
mode 1. No builds, profilers or tests overlap the clean measurements.

| Arm | FPS | Median ms | p95 ms | Lowest one-second FPS | CPU ms/frame |
|---|---:|---:|---:|---:|---:|
| A1 | 28.500 | 35.037 | 40.561 | 24 | 155.492 |
| B1 | 28.833 | 34.853 | 40.136 | 26 | 149.365 |
| B2 | 28.867 | 34.894 | 40.503 | 26 | 150.237 |
| A2 | 28.433 | 35.253 | 40.887 | 26 | 154.530 |

Means are 28.467 versus 28.850 FPS (+1.35%) and 155.011 versus 149.801 CPU
ms/frame (−3.36%). This is a modest repeated improvement. CPU time is aggregate process work
across parallel threads. Candidate p95 remains 40.320 ms on
average; about 66.1% of measured intervals exceed 33.34 ms. These results
support retaining the exact conversion optimization, not a stable-30 claim.
The completed clean candidate round measures 35.733 FPS in seconds 240–300
(p95 33.483 ms; lowest one-second bin 31 FPS), and 25.667 FPS in seconds
306–315 (p95 43.881 ms; lowest bin 24 FPS). Its 330-second readback shows an
intact scoreboard with a Legolas question and zero correct answers. This is an
unpaired production run, with different question progress from the prototype
pair; it documents the remaining performance gap, not a round gain or
regression. The later screenshot does not certify the camera phase throughout
the measured windows.

All six production captures (audit, four menus, clean round) were inspected
and intact. Effective renderer profiles match, with no settings errors. Final
hash checks confirm the prepared reference and installed executable are
unchanged. No 30+ FPS completion claim is made.

Reproduce the asset-free arithmetic/routing tests on an AVX2/FMA host with
Clang using `bash tests/scripts/test-image7-simd.sh`. For complete-state tests,
append a prepared workspace and a private image-7 capture directory. The runner
regenerates the pinned oracle in its own temporary directory and does not
rewrite the prepared build. Set `IMAGE7_TEST_OUT` to retain build artifacts.

Private artifacts under `/home/deck/buzz/performance/optimization/`:

- `codex-image7-capture/`: recording wrapper, source snapshots, inputs,
  inspected readback, dataflow summary, prototype, exact/synthetic fixtures,
  first failed fixture, pass logs, isolated timings and restoration hashes.
- `codex-image7-simd/`: opt-in game wrapper, source/compiler snapshots,
  qualification/comparison drivers, both completed menu series and the round pair.
- `codex-image7-integration/`: production fixtures/build logs, retained profile
  flags, source/binary hashes, restoration proof and live qualification evidence.

The frozen reference is
`630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d`;
the installed executable remains
`189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.
