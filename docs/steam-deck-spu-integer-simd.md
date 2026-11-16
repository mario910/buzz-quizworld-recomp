# Deck SPU integer SIMD experiment, 2026-11-16

Explicit SSE2 integer helpers pass instruction and hot-loop correctness checks,
but the measured loop gain is small. The prototype remains private, with no
game-level performance or promotion claim. Stable 30+ FPS is not achieved.

The SDK supplies NEON implementations for several packed integer instructions
but uses scalar reference C on x86. This experiment overrides 27 helpers after
the SDK header: word/halfword add and subtract, immediate arithmetic, bitwise
logic, signed/unsigned comparisons, equality and bit selection. It preserves
all 128 result bits, integer wraparound and the caller's floating-point state.
The overrides are gated on SSE2 and disabled by the scalar-helper switches.
There are no changes to floating-point helpers, local-store access, channel
operations or scheduling. Calls inside the already-defined SDK helpers continue
to use their original implementations.

## Correctness

Clang 18 ASan/UBSan with O2, AVX2, `-fwrapv`, disabled strict aliasing and
`-ffp-contract=off` passes 1,133,816 vector/immediate cases across all 27 helpers:
30,613,032 complete 128-bit comparisons. The reference is a separate scalar
translation unit with both loop and SLP vectorization disabled. Cases include
edge-value combinations, every byte pair, every signed 16-bit immediate,
one million random full-width inputs and arbitrary 32-bit immediates, plus
MXCSR rounding/FTZ/DAZ/sticky-state combinations. MXCSR remains unchanged.

The actual image-38 loop `buzz_job_39d9_spu_func_00000138` also passes 10,096
complete context, local-store and trampoline comparisons under ASan/UBSan.
These exercise every shuffle control byte in every destination lane, arbitrary
registers and local-store contents, 1–257 iterations and nested host-return
state. The same suite passes the plain and freshly profiled timing builds.
No native-job or complete-game qualification is claimed for these overrides.

## Fair compilation and timing

Keeping the original function symbol does **not** preserve the profile hash:
replacing inlined scalar helper loops changes the compiler's instrumented CFG.
The existing PGO6 profile is rejected for 27 functions in image 38, 82 in the
sampled image-36 generated unit and 170 in the sampled image-39 unit. Those
objects are retained for inspection, not used as a fair timing comparison.
Matched builds without PGO show only modest code-size changes in sampled hot
functions. Missing absolute-path profiles for internal functions are a further
reason not to interpret relocated old-profile objects as production evidence.

The plain image-38 timing is close: longer loops are around 19–20 ns per
iteration in either implementation. A separate training executable then profiles
both versions using the same deterministic correctness and timing inputs.
The merged fresh profile contains both CFG identities. Both use builds complete
with no profile mismatch warning, and stale-profile warnings are treated as
errors. Candidate symbols are renamed only after compilation for paired linkage.

Three standalone timing repetitions each use baseline/SIMD/SIMD/baseline order
for loop lengths 1, 2, 4, …, 1024. Each arm performs approximately one million
loop iterations. Input buffers are synthetic; six shuffle masks come from a
previously captured real call. Complete-state equivalence is checked before
each timing group. Every arm starts with identical local-store state and resets
all GPRs before each call. CPU-time measurements include that register reset and
a volatile result accumulation. No compiler, game or profiler overlaps timing.
These are isolated loop measurements, not captured whole-job or game timings.

| Iterations per call | Baseline ns/iteration | SIMD ns/iteration | Change |
|---|---:|---:|---:|
| 1 | 42.55 | 41.54 | −2.38% |
| 16 | 20.52 | 19.62 | −4.41% |
| 64 | 19.28 | 18.57 | −3.71% |
| 1024 | 19.33 | 18.39 | −4.86% |

Values average the two arms in each of three repetitions. They are not weighted
by the game's call distribution. The loop is only one component of SPU work;
these gains do not establish a useful FPS improvement or justify global helper
replacement. All private sources, generators, controls, sanitizer results,
profiles, command lines, snapshots, timing logs and hashes are retained under
`/home/deck/buzz/performance/optimization/codex-spu-integer-simd/`.
