# Steam Deck post-link code-layout experiment

The tested LLVM BOLT configuration regresses the animated menu from 28.90 to
27.65 FPS and increases CPU time per frame by 4.07%. It is not integrated into
the build. This result applies to the cycle-sampled configuration below, not
to every BOLT strategy. Stable 30+ FPS remains unachieved.

## Inputs and compatibility

The reference is commit `de3c39b`, including the qualified image-7 SIMD path,
with executable SHA-256
`d6420f0b0905ed62958f6c42cfcbfe740d8851295484631d99460ccec49b5b56`.
The private prepared workspace first reproduced that executable byte-for-byte.
Relinking with `-Wl,--emit-relocs` produced
`db2a4f6e2a17024b6eb6116b5ccddec518ec257775ff68afd9a90878adb7d0af`.
No compiler, arithmetic, PGO or game settings changed.

The relink preserves every nonempty allocated section's bytes, address and size,
except the build ID and dynamic symbol table. Canonicalized dynamic symbols are
identical: their raw section indices change when relocation sections are added.
An added `.tm_clone_table` section is empty. The original prepared workspace was
restored before profiling or live comparison.

Ubuntu LLVM BOLT 20.1.2 and its runtime package were downloaded and extracted
privately; no system package was installed. The Deck rejected hardware branch
stack sampling, so training used `cycles:u` at 999 Hz without call stacks or LBR:

| Training run | Sample window | Samples | Lost records |
|---|---|---:|---:|
| Animated menu, 90 seconds | 45–65 seconds | 87,207 | 0 |
| Scripted round, 335 seconds | 245–325 seconds | 290,693 | 0 |

Both training captures were visually intact. `perf2bolt -nl` converted each
profile, then `merge-fdata` combined them. The combined profile covers 3,147
of 49,079 functions. Out-of-range/unknown-region samples account for 20.5% and
22.7% of the two inputs. Nine profiled functions could not be optimized.
Cycle samples do not provide measured branch counts; BOLT's inferred dynamic
statistics are not hardware branch measurements or FPS evidence.

The tested command uses `-reorder-blocks=ext-tsp -reorder-functions=hfsort
-split-functions=0 -split-all-cold=0 -dyno-stats -thread-count=4`.
The runtime's `ppu_prof_resolve_host` maps host return addresses to the nearest
registered function entry, so each function must remain contiguous for those
diagnostic mappings. A first candidate with function splitting was generated
but never executed. The tested contiguous candidate has SHA-256
`29ff0b0807a0b53392eda7a97ea13ed15f6c32ff79a39da87496740f277d97ab`.

The eight reported unsupported relocations match two `R_X86_64_TLSLD` and six
`R_X86_64_DTPOFF32` entries in glslang's thread-local allocator. LLVM 20.1.2's
relocation classifier omits those TLS forms; the source and relocation census
are retained. This explains the count but does not replace runtime validation.
See the [LLVM relocation implementation](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.2/bolt/lib/Core/Relocation.cpp)
and [post-link optimization guidance](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.2/bolt/README.md).

## Captured-job validation

Fresh reference and candidate replays produced byte-identical complete ordered
DMA/exit output files for all 9,696 jobs: the existing seven corpora (8,196 jobs)
and 1,500 newer menu jobs. The candidate executed 28,884 jobs with up to three
passes per capture. Every capture verdict matched the reference, including
348 known concurrent-input cases and 102 known captured/reference failures;
these are not 9,696 exact matches to the original live captures. The comparison
establishes no new output difference between the two executables for this set.

## Clean menu timing

Runs used X11, 1280×720, 100% render scale, uncapped mailbox presentation,
512-draw batches, two draw contexts, zero worker spin, native image 7 and
image-36 search. Each run lasted 90 seconds, measuring presentation at 40–70
seconds and CPU ticks at 45–65 seconds, with a later capture at 80 seconds.
No profiler, build, replay or second game ran alongside these measurements.

| Run | FPS | P95 frame time (ms) | Minimum one-second FPS | CPU ms/frame |
|---|---:|---:|---:|---:|
| Reference A1 | 29.033 | 39.900 | 27 | 150.309 |
| BOLT B1 | 27.533 | 42.259 | 24 | 157.343 |
| BOLT B2 | 27.767 | 42.355 | 24 | 156.340 |
| Reference A2 | 28.767 | 40.687 | 27 | 151.105 |

Both candidate runs are slower than both reference runs. Mean FPS falls 4.33%,
mean CPU time per frame rises 4.07%, and mean P95 worsens from 40.294 to 42.307
ms. The proportion of frame intervals over 33.34 ms rises from 63.50% to 76.97%.
The variant is rejected despite passing captured-job comparisons.

All four menu captures were visually intact, and logged effective settings
matched. A separate 335-second candidate round completed with an intact green
question/scoreboard capture at 330 seconds (Elvis Costello question, zero correct
answers). Its timed question window averaged 34.53 FPS with P95 34.318 ms and
minimum one-second FPS of 28; the late 306–315-second window averaged 26.00 FPS,
P95 46.036 ms and minimum 22 FPS. This round is unpaired, and its later capture
does not certify the exact scene in every timed frame. It establishes neither
a paired gameplay change nor stable 30+ FPS. No crash, device-loss or native
mismatch marker was found in the five live-run logs.

## Evidence

Private files under `/home/deck/buzz/performance/optimization/codex-bolt-layout/`
retain tool/package hashes, reference and candidate binaries, source snapshots,
relink equivalence checks, loss-checked profiles, optimizer logs, complete replay
outputs, live settings, timing CSVs and captures. No game data, profiles or
binaries are committed. No post-link optimization is enabled in the production
build, and the installed executable is unchanged.

The prepared executable remains
`630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d`,
the installed executable remains
`189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`,
and the SDK stays clean at `79efc280dad27271d5f68077acb0247df1e88dc7`.
