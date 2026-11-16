# Full DMA publication barriers on Steam Deck

Replacing the post-DMA `MFENCE` with a store fence plus a locked private-stack
operation passes ordering and output checks, but does not improve clean menu
FPS. The direct replacement averages 28.93 FPS versus 28.98 for the committed
build, with about 1.2% less CPU time per frame. It is not integrated. Stable
30+ FPS remains unachieved.

## Scope and ordering

The loss-free menu profile retained by the [BOLT study](steam-deck-bolt-layout.md)
attributes 1.61% of sampled user cycles to `spurs_command_dma_written`.
Within that function, 83.36% land on the call immediately following `MFENCE`.
Sampling skid prevents treating this as an exact fence cost or critical-path
wall-time measurement, but it motivates an isolated barrier experiment.

Only the full fence before the PPU and command-walker notification checks
changes. Every transfer, notification, filter, waiter registration and timeout
remains. In particular, this does not select `SPURS_DMA_NOTIFY_FENCE=0`, whose
earlier regression demonstrates the importance of this ordering.

The private x86-64 helper uses a naturally aligned local word:

```c
unsigned word = 0;
__asm__ __volatile__("sfence\n\tlock; orl $0, %0"
                     : "+m" (word) : : "memory", "cc");
```

The locked operation supplies full memory ordering, the store fence explicitly
covers streaming stores, and the memory clobber constrains compiler movement.
The scratch word is private, not a contended global. The experiment is scoped
to ordinary host-memory DMA publication on the tested x86-64 runtime.
[AMD's architecture manual](https://docs.amd.com/api/khub/documents/68GKiN0gMEd6bMddsmhPwg/content)
documents locked read/write barriers; the
[GCC fence-lowering change](https://gcc.gnu.org/pipermail/gcc-patches/2020-July/550318.html)
also explains their use for sequential consistency and the separate streaming
store consideration. Hardware stress checks supplement that contract; they do
not prove every possible concurrent execution.

## Ordering and notification checks

Two producer threads pinned to different Deck cores each perform a relaxed
store, barrier and relaxed load. Across one million trials per mode, both
`MFENCE` and the alternative produce zero forbidden both-zero results. The
no-fence control produces 24, confirming that this run exposes the store-buffer
case. Both full barriers also pass 200,000 streaming-store publications with
exact 128-byte payload reads.

The repository's command-wait and PPU-word-wait fixtures were adapted to call
the real generated `spurs_command_dma_written` path. Both runtime selections
pass normally and under ASan/UBSan. Each command test covers 200 notified waits,
prepublication, chain-PC changes, cleanup and timeout. Each PPU test covers
20,000 producer/waiter races, partial and large ranges, eight simultaneous
waiters, slot reuse, admission rejection and timeout fallback.

The initial fixture link omitted the runtime's POSIX Win32 compatibility
implementation; adding that required source fixed the link. The failure log
is retained. No runtime behavior change was needed for that fixture repair.

## Isolated cost

Clang 18/O3 tests use one million copy-and-fence iterations per arm, in
original/alternative/alternative/original order on CPU 0. Copies cycle over
warm buffers. Mean nanoseconds per iteration are:

| Copy bytes | Original | Alternative |
|---|---:|---:|
| 0 | 23.465 | 5.462 |
| 128 | 33.722 | 7.066 |
| 512 | 37.965 | 11.471 |
| 4,096 | 68.169 | 43.696 |
| 16,384 | 192.587 | 168.783 |

The first zero-copy fixture allowed LLVM to coalesce the standard fence loop,
making that row invalid. An opaque compiler memory access fixed the fixture;
all sizes above were rerun, and the initial log was archived. These synthetic
costs are not whole-game FPS evidence.

## Whole-job and live comparison

The reference is the committed SIMD executable from `de3c39b`, SHA-256
`d6420f0b0905ed62958f6c42cfcbfe740d8851295484631d99460ccec49b5b56`.
Only the generated notification translation unit changes. The SIMD sources,
compiler flags and PGO input remain the same.

A switched prototype (`a940191c446f7263f182f9ce595e17fbe74968f4fb74fb8e37c8c956dd07db87`)
uses `BUZZ_DMA_LOCKED_FENCE=0/1` to select the original or alternative. Its new
branch invalidates the old PGO counts for this function. Both selections
reproduce all 9,696 reference output files; the enabled mode executes 28,884
jobs. A direct replacement removes the switch and preserves the existing
control-flow profile. It builds without a profile-mismatch warning, retains
the function in its original hot region, and contains the expected `SFENCE`
and locked operation without `MFENCE` in that function. This direct executable
(`ab7a754fe0ec6e55eb2b0d548316c63f4ebcc028ae9c1e5296dd049be74e896d`)
also reproduces all 9,696 files across 28,884 executions.

Those are complete ordered DMA/exit comparisons against the frozen reference.
Capture verdicts remain identical, including 348 known concurrent-input cases
and 102 known capture/reference failures. They are not 9,696 exact matches to
the original live captures. Replay alone cannot establish concurrent-wakeup
correctness; the notification fixtures and live runs provide separate checks.

Each clean menu run lasts 90 seconds, with FPS measured at 40–70 seconds,
CPU at 45–65, and capture at 80. All use X11, 1280×720, 100% scale, uncapped
mailbox, batch 512, two contexts, zero worker spin and image-36 search. Builds,
profiling, replays and other games do not overlap timing.

The first series uses A/B/C/C/B/A: A is the committed reference, B the switched
prototype with original fence, and C the same executable with the alternative.

| Arm, two-run mean | FPS | P95 ms | CPU ms/frame |
|---|---:|---:|---:|
| A: committed | 29.167 | 39.995 | 149.386 |
| B: switched original | 28.817 | 40.688 | 150.291 |
| C: switched alternative | 29.050 | 40.671 | 147.597 |

C improves 0.81% over B but remains 0.40% below A. Its CPU reduction versus A
is 1.20%. The additional untouched control prevents attributing the entire
same-binary difference to a net improvement over the committed build.

The direct replacement then uses A/B/B/A against the committed reference:

| Run | FPS | P95 ms | Minimum one-second FPS | CPU ms/frame |
|---|---:|---:|---:|---:|
| Reference A1 | 29.100 | 40.308 | 27 | 149.566 |
| Direct B1 | 28.967 | 40.785 | 25 | 147.628 |
| Direct B2 | 28.900 | 40.818 | 25 | 148.700 |
| Reference A2 | 28.867 | 40.816 | 26 | 150.495 |

Mean FPS is 28.983 versus 28.933 (−0.17%); CPU time is 150.030 versus 148.164
ms/frame (−1.24%). Mean P95 worsens from 40.562 to 40.802 ms. Neither experiment
establishes a useful frame-delivery gain, so the alternative is not promoted.

The switched prototype also completes a 335-second round with an intact
compass/orienteering question and zero-correct scoreboard at second 330.
Its question window averages 36.33 FPS (P95 32.958 ms, minimum one-second FPS
31), while the late 306–315-second window averages 25.89 FPS (P95 45.574 ms,
minimum 23). This round is unpaired; the later capture does not certify the
exact scene throughout the timed window. All seven switched-study captures
were inspected and intact, with matching settings and no crash, device-loss
or native mismatch marker in the checked logs.

The direct build's separate 335-second round also completes, with an intact
green zero-correct scoreboard at second 330. Its question window averages
36.40 FPS (P95 32.798 ms, minimum 33), and its late window 27.00 FPS (P95 44.188
ms, minimum 24). The different presentation at capture time prevents treating
these two unpaired rounds as a matched gain. All five direct-study captures
are intact; effective renderer settings match and the checked logs contain no
crash, device-loss or native mismatch marker. Stable 30+ FPS is still unmet.

## Retained evidence

Private artifacts live under
`/home/deck/buzz/performance/optimization/codex-full-fence/` and
`codex-full-fence-direct/`: source snapshots, build/restore scripts and logs,
the helper and fixtures, sanitizer and ordering results, instruction listings,
complete replay outputs, frozen executables, timing CSVs and screenshots.
No binaries, captures or profiles are committed. The prepared baseline and
installed game remain unchanged; the SDK remains clean at
`79efc280dad27271d5f68077acb0247df1e88dc7`.
