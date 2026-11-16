# Additional audio transform loops: Deck measurements

Two exact audio loop optimizations reduce isolated execution time by about
72%, but do not improve clean menu FPS over the unchanged reference. The
candidate remains private. Stable 30+ FPS is still unmet.

## Scope and standalone checks

The current cycle-sampled profile attributes about 0.64% and 0.17% of menu
user cycles to image-1 entries `0x5048` and `0x53DC`. Neighboring audio routines
already use localized registers and checked x86 RTZ arithmetic; these two
loops still use the emulated register bank and software arithmetic. Sampled
cycles identify CPU work, not a frame-critical dependency.

The private implementation localizes only each loop prefix. It retains
instruction order and uses the existing checked single-precision helpers.
Written registers and the incoming MXCSR are restored before the original
continuation executes. Debug-hook or externally backed local-store contexts
use the reference prefix. Calls, trampoline exits and the remaining function
body stay in the original generated source.

Both the initial whole-function prototype and the final split-prefix form
pass 2,048 complete-state cases per entry under ASan/UBSan. Checks cover all
registers, local store, context fields and trampoline destination, wrapped
and overlapping addresses, external-store fallback, varied loop counts,
floating-point boundaries and arbitrary bits, host rounding modes and sticky
flags. A state-observing continuation stub verifies that registers and host
FP state are restored before calls. This fixture does not replace live checks
of the actual continuation.

Four standalone timing passes reverse baseline/candidate order and use
representative 64-iteration inputs. Context resets are outside timing and
timestamps use integer nanoseconds, avoiding changes to host FP flags.

| Entry | Original ns | Candidate ns | Time reduction |
| --- | ---: | ---: | ---: |
| `0x5048` | 11,298.97 | 3,166.66 | 71.97% |
| `0x53DC` | 6,690.92 | 1,850.80 | 72.34% |

These are isolated whole-function fixture measurements with stubbed
continuations, not game FPS or final-build timings.

## Private build

The existing SIMD reference was reproduced byte-for-byte before changing the
mixer source, mixer kernel and audio wrapper. Only those three translation
units received refreshed PGO, trained with 90-second disabled/enabled menus
and a 335-second enabled round. Training timings are excluded. All three
captures were inspected and intact; the round reached a Prokofiev question
and green scoreboard with zero correct answers. Effective settings match.
The final candidate has no stale-profile warning or profile-generation ELF
sections.

| Artifact | SHA-256 |
| --- | --- |
| Reference | `d6420f0b0905ed62958f6c42cfcbfe740d8851295484631d99460ccec49b5b56` |
| Candidate | `90e5749a1a13c012262964f920f55a49142d6e68f7fba45543c50c6b6b7da3b8` |
| Audio profile | `3f2b77f05a9b6a711064bf66557ab52be906e96d7cbc1f58a79cb2930028a685` |

The private `BUZZ_AUDIO_EXTRA=0/1/2` switch selects reference, native or
comparison prefixes. Comparison clones the context, checks the native prefix
against the original, retains original results and executes the continuation
once. An initial 335-second comparison round passes 20,000 checks per entry.
Those checks finish during startup/menu; they do not establish later-phase
input coverage by themselves.

## Clean live results

The six menus follow A/C/B/B/C/A order: A is the untouched reference, C is the
new build disabled and B is the same build enabled. Each uses a fresh runtime
and shader cache, X11, 1280×720 at 100% scale, uncapped mailbox, 512-draw
batches, two contexts, zero worker spin, image-36 search and image-7 SIMD.
Persistent chain workers remain off. FPS covers seconds 40–70, CPU samples
45–65 and captures second 80. No build, test, profiler or other game overlaps
the clean runs.

| Arm | FPS | p95 ms | Lowest one-second FPS | CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 reference | 28.7333 | 40.639 | 26 | 151.9083 |
| C1 disabled | 28.7667 | 41.047 | 24 | 151.6206 |
| B1 enabled | 28.7000 | 40.783 | 25 | 150.6814 |
| B2 enabled | 28.7667 | 40.567 | 25 | 150.1101 |
| C2 disabled | 28.5667 | 41.660 | 25 | 152.7778 |
| A2 reference | 28.8667 | 40.418 | 24 | 150.4212 |

Mean FPS is 28.8000 reference, 28.6667 disabled and 28.7333 enabled. Enabled is
0.23% ahead of the same-build control but 0.23% behind the untouched reference.
CPU means are 151.1648, 152.1992 and 150.3957 ms/frame respectively: enabled is
1.18% lower than disabled, but only 0.51% lower than the reference. Its p95
frame time is 0.36% worse than the reference. About 65.6% of enabled intervals
still exceed 33.34 ms. These measurements do not establish an FPS improvement.

A separate enabled 335-second round measures:

| Window | FPS | p95 ms | Lowest one-second FPS | CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| Menu, 40–70 s | 27.7000 | 41.346 | 24 | 155.8757 |
| Question, 240–300 s | 35.5500 | 35.461 | 24 | 96.3555 |
| Late, 306–315 s | 28.1111 | 42.669 | 24 | 130.2242 |

This round is unpaired. Its final capture shows an active Salinger/Holden
Caulfield question over the green stage with one correct answer. The initial
comparison run instead ends on a football yellow/red-card question and green
scoreboard with zero correct. Question selection and progression differ, so
these rounds cannot establish a matched performance change. Later screenshots
also do not certify every timed frame.

All six clean menu captures and both initial rounds were inspected and intact.
Settings match across the clean runs, measured presentation deltas are one,
and presentation continues to the expected end. No searched crash,
device-loss or nonzero mismatch marker appears in their logs.

## Question and late-round comparison

After the clean series, a separate 335-second diagnostic run reopens the
bounded comparison budget at renderer times 240.017 and 306.102 seconds.
The parent harness validates the candidate hash and diagnostic symbol size,
then resets only the two aligned 32-bit call-budget counters in its own child.
It preserves checked, failed and external-store totals. Game results remain
the original prefix's results throughout. This run is excluded from timing.

Both entries have 20,000 checks before the question reset, 40,000 before the
late reset, and 60,000 at completion. All 120,000 comparisons pass, with zero
mismatches and zero external-store skips at the recorded reset snapshots.
Together with the initial comparison run, that is 160,000 live full-context
and local-store comparisons. The final capture shows an intact music question
and green scoreboard with zero correct answers. Settings match and presentation
continues through 335.115 seconds.

## Evidence and restoration

Private evidence is under
`/home/deck/buzz/performance/optimization/codex-audio-extra/`: pinned source
generation, standalone and split-prefix fixtures, sanitizer logs, timing
results, profile/build scripts, frozen executables, three training runs, two
comparison rounds, seven clean runs, phase-reset records and audits.

All build, comparison and timing drivers have completed and exited. The
prepared executable is restored exactly to `630007cb…`, installed executable
`189814d1…` is unchanged, and the SDK remains clean at `79efc280…`. The existing
SIMD executable stays the performance reference. This audio candidate is
retained privately for evidence, without a production switch or integration.
