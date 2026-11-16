# Exact SIMD video conversion on Steam Deck

A private AVX2 prototype accelerates the image-7 YUV-to-RGBA conversion loop.
It has exact arithmetic and captured-state evidence, with a roughly sevenfold
isolated speedup. Game comparison-mode audits and clean FPS comparisons remain
the promotion gates. Stable 30+ FPS is not established by this kernel result.

## Scope and admission

The current profile attributes about 2.98% of aggregate sampled user cycles
to `buzz_image7_8638_native`. This is CPU attribution, not critical-path wall
time. The pinned disc-100 leaf is `0x86B8`, exiting at `0x8FF0`; the existing
generated native symbol retains its older `8638` name.

The prototype checks the seven coefficient/packing registers and all 34
loaded shuffle-table quads. It admits only internal local store, 1–1,024
iterations, aligned nonwrapping spans, disjoint output rows, and outputs
disjoint from all inputs and the table region. Every other case retains the
original native function. The tested build has LS debug hooks disabled, as
does the frozen native kernel; broader diagnostic/platform integration is
not yet qualified.

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
- 64 synthetic complete states with random pixels/registers, relocated buffers,
  counts 1–9, 20, 31–32, 63–65 and 128, and wrapping loop counters.
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

## Game qualification in progress

The private game candidate enables the prototype with `BUZZ_IMAGE7_SIMD=1`;
`0` selects the original native path in the same executable. Only the prepared
image-7 wrapper translation unit and its compile flags change. The original
generated kernel and all existing PGO profiles remain unchanged; the wrapper
had no profile input and is compiled with O3, AVX2/FMA and strict RTZ settings.

The serial qualification sequence runs live image-7 comparison mode in a
90-second menu and a 335-second scripted round, then clean OFF/ON/ON/OFF menu
timing. Audit logs count actual optimized calls separately from fallback
calls. The sequence restores the prepared source/compiler settings and checks
the reference hash on completion or failure. Results must be inspected before
promotion; the installed game is unchanged.

The completed menu audit reports 260,000 optimized calls, zero fallbacks and
zero mismatches, with presentation continuing through 89.799 seconds. The
round audit and clean timing are still pending; comparison-mode FPS is not
performance evidence.

Private artifacts under `/home/deck/buzz/performance/optimization/`:

- `codex-image7-capture/`: recording wrapper, source snapshots, inputs,
  inspected readback, dataflow summary, prototype, exact/synthetic fixtures,
  first failed fixture, pass logs, isolated timings and restoration hashes.
- `codex-image7-simd/`: opt-in game wrapper, source/compiler snapshots,
  qualification/comparison drivers and evolving run evidence.

The frozen reference is
`630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d`;
the installed executable remains
`189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.
