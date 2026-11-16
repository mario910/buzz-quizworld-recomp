# Image 36 constant shuffle selectors, 2026-11-16

Specializing six invariant shuffle masks made the isolated image 36 leaf
faster but did not improve clean Deck menu FPS or CPU time. The candidate
is archived without promotion. Stable 30+ FPS remains unachieved.

The EU 1.00 loop at guest PC `0x06C8` retains the historical native symbol
`buzz_image36_06d8_native`. Six control registers (`r23`, `r24`, `r30`, `r38`,
`r56`, `r57`) are unchanged throughout the loop and have identical values in
all 128 archived leaf captures. The prototype compares all 96 control bytes
at entry and substitutes their exact constants. This lets the compiler
simplify 37 shuffle operations per iteration. All arithmetic, local-store
accesses, instruction ordering, final registers, return state and scoped RTZ
handling remain intact. Unexpected masks, foreign local-store buffers or
local-store debug hooks use the original native implementation.

Two standalone variants were checked: constant selectors with the original
context register accesses, and selectors combined with register locals.
Each passed 6,912 synthetic full-state cases, including 1–64 iterations,
arbitrary FP bits, caller rounding/status settings, nested host-return state,
768 single-bit selector mutations, and external local-store buffers. A further
24,480 single-byte mutations all rejected admission. Each variant also passed
128 in-game leaf captures repeated 100 times, comparing every GPR and LS byte.
The fixture checked the integer FP oracle and detected all five injected
register, LS, metadata and trampoline faults. Both variants passed ASan/UBSan.
Per variant and build, counters recorded 8,448 specialized executions and
4,864 fallback executions; the remaining timed executions used the baseline.

Using Clang 18, AVX2/FMA, strict rounding, the production forced shuffle header
and existing PGO6 data, paired captured-leaf timing measured:

| Variant | Baseline us | Candidate us |
|---|---:|---:|
| Constant selectors | 31.03 | 20.75 |
| Selectors plus register locals | 30.70 | 22.78 |

These are isolated timings, not game FPS. The simpler variant was integrated
privately with `BUZZ_IMAGE36_SELECTORS=0/1`, default off. Production test
counters were excluded. The retained image-7 SIMD build was reproduced by
hash before adding the candidate. Existing profile data was used; there was
no fresh candidate PGO training and no integrated-build profile mismatch.
The standalone fixture's aborting indirect-branch stub had an unrelated
profile hash warning, retained in its build log.

Whole-job qualification passed 500 captured image-36 jobs repeated 20 times
per setting: 10,000 executions per arm. Every complete ordered transfer stream
and exit state matched, with 500 exact jobs and zero failed or concurrently
modified captures in each setting. An initial driver assertion expected the
wrong summary wording; after preserving that failure, it validated the
already-completed off replay and ran the on replay. No failed replay was
silently replaced.

Four clean 90-second runs used the same frozen candidate in off/on/on/off
order, fresh runtime directories and private shader caches. Frame measurement
was 40–70 seconds and CPU sampling 45–65 seconds. No builds, fixtures,
profilers or other game instances ran concurrently.

| Run | Selectors | FPS | Median ms | p95 ms | Min one-second FPS | CPU ms/frame |
|---|---|---:|---:|---:|---:|---:|
| A1 | off | 28.87 | 34.707 | 40.817 | 25 | 149.98 |
| B1 | on | 28.73 | 34.736 | 40.753 | 25 | 150.40 |
| B2 | on | 28.67 | 34.685 | 41.450 | 24 | 150.73 |
| A2 | off | 28.80 | 34.756 | 40.459 | 27 | 150.66 |

Mean FPS was 28.83 off versus 28.70 on (−0.46%); CPU was 150.32 versus
150.57 ms/frame (+0.16%). Each run progressed beyond 88 seconds with every
recorded presented-frame delta equal to one. Effective settings matched:
RADV VANGOGH, 1280×720, 100% scale, uncapped mailbox, 512-draw batches,
two draw contexts and zero lane spin. All four 80-second captures showed
the expected Russian game-selection menu and stage without evident damage.
Those post-window captures do not establish exact scene equality throughout
the timed windows. No round-performance or full promotion gate is claimed.

A separate 90-second counter build confirmed at least 32,768 actual live
specialized calls with owned LS and no logged fallback. It is excluded from
timing; the neutral result is not explained by universal fallback. Its results
and all private source, generators, binaries, hashes, fixtures,
replay outputs, logs, frame CSVs, telemetry and screenshots are retained in
`/home/deck/buzz/performance/optimization/codex-image36-selectors/`.
The prepared workspace is restored by exact executable hash after each build;
the installed executable and SDK are unchanged.
