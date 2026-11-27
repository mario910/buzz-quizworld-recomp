# ARM64 geometry and SPURS experiments after 46f2a8b

## Integrated result

The comparator is the previous EU 1.00 `mobile-cpu` build at `46f2a8b`.
The timing harness replayed image-41 inputs through host input preparation,
skinning, projection, index decoding with its normal cache, triangle culling
and final index serialization. Job admission runs inside the instrumented
region. Inflation, allocations for the workspace and output hashing run
outside it. Vertex serialization, publication, the rest of the frame, GPU
execution and device scheduling are outside this measurement.

The integration binaries were compiled with Android NDK r30 Clang at O2 and
linked statically to Android Bionic. QEMU 6.2 counted dynamic AArch64
instructions between entry and exit markers, including callees. The 1,200
jobs used to screen candidates contain 800 menu jobs and 100 evenly spaced
jobs from each of four gameplay sessions.

| Android/Bionic measurement | Previous | Candidate | Change |
| --- | ---: | ---: | ---: |
| Replayed geometry stages | 105,153,773 | 92,816,291 | -11.73% |
| Projection | 37,922,499 | 30,010,831 | -20.86% |
| Culling | 12,706,072 | 8,335,896 | -34.39% |
| Index serialization | 252,476 | 203,727 | -19.31% |
| SPURS input staging | 1,736,427 | 1,527,898 | -12.01% |

The individual rows are separate measurements with their own markers.
The geometry digest includes the ordered output bytes and retained triangle
counts. On the selection sample both binaries produced `dd6ffcbdacfe46a2`.

| Capture group | Jobs | Replayed geometry change |
| --- | ---: | ---: |
| menu | 800 | -11.12% |
| solo-session | 100 | -10.87% |
| multi-session | 100 | -11.74% |
| multi-private | 100 | -11.92% |
| multi-verified | 100 | -11.65% |
| held-out | 1,200 | -11.46% |

The held-out group used all 800 captures from the separate menu/image41
directory and 400 additional gameplay captures excluded by filename from
candidate selection. These are other captures from the same game and some
of the same sessions. Phone frame time and sustained power remain unmeasured.

## What changed

- The mobile profile enables the existing deferred exact projection and
  eight-lane stable compaction. A block that needs exceptional FP repair
  runs again on the checked path.
- The NEON clip kernels clamp guest float magnitudes as integer bits before
  scaled saturating conversion. They also compute the union and intersection
  of projected clip flags. No flagged vertices means culling can omit flag
  gathers. One plane shared by every active vertex rejects the whole job's
  triangles after admission. Inactive tail lanes are neutral for each reduction.
- The index writer interleaves a remaining four triangles in one NEON store.
  One to three triangles, odd destinations and final SPU padding retain their
  scalar handling.
- The SPURS adapter inlines copies of the seven frequent sizes only for the
  admitted 21-entry image-41 list. Other lengths use the existing memcpy.
  The first integration with ordinary memcpy in a helper regressed to
  1,765,217 Bionic instructions. Explicit constant-length inline copies
  restored the screened result. In five warm x86-64 Linux runs of 300
  repetitions per job, median thread CPU time was 28.78 ms before and
  29.41 ms after. This change ships only in the Android adapter.

## Guest code and validation

Ghidra 11.0.3 decompiled the original big-endian Cell images from the EU
1.00 game. In image 39 (`c0bc`) we inspected `0x33f8`, `0x3470` and
`0x3530`. The guest sets MFC address, size and tag before issuing the command
at `0x3468`. In image 41 (`6eba`) we inspected `0x66c8`, `0x6958` and
`0x69e8` alongside the previously identified projection and cull loop.
The SPURS copy change concerns preparation of geometry inputs before guest
entry. Image 39 had no timed entry corpus in this comparison.

The full Android/Bionic oracle replayed 19,508 jobs. The 19,399 admitted
jobs matched all 5,280,336 bytes of the original SPU index output, including
2,617,893 logical indices and 44,550 padding bytes. The remaining 109 jobs
had the existing unsupported input storage. The checks include guard-page
ends, odd destinations, all 40 last-group/alignment shapes, all 4,096
quantization probes and scalar clip-flag reductions over active vertices.
A separate test compared complete 256 KiB local stores and end cursors for
1,200 DMA sequences in both clear modes and for 7,200 generated boundary cases.

## Individual experiments

Each row is one compiled intervention on the same captures, in addition to
the control binaries. The screening executable used NDK Clang and AArch64
Linux libc. The integrated numbers above use Bionic. For the two refinements
of eight-lane compaction, the comparator already has compaction enabled.
The combined-flags experiments report the sum of projection and cull
instructions so the extra producer work is charged.

| Experiment | Measured stage | Candidate instructions | Change |
| --- | --- | ---: | ---: |
| 1. Eight-lane stable compaction | cull | 11,104,990 | -11.97% |
| 2. Gather directly from scalar u16 mesh indices | cull | 13,468,074 | +6.77% |
| 3. Defer exceptional projection repair to block exit | projection | 34,023,818 | -10.28% |
| 4. Track XY bounds using shifted unsigned magnitudes | projection | 37,871,784 | -0.13% |
| 5. Accumulate flag union to skip unnecessary flag gathers | both | 49,867,546 | -1.32% |
| 6. Intersect flags for a whole-job common-plane reject | both | 48,264,881 | -4.49% |
| 7. Read survivors directly from the corner arrays | cull | 12,515,287 | -0.79% |
| 8. Use a horizontal reduction for complete eight-lane early rejects | cull | 12,472,121 | -1.13% |
| 9. Use a raw one-table byte shuffle in the compact writer | cull | 11,121,779 | +0.15% |
| 10. Compare signed area directly against the denormal threshold | cull | 12,400,301 | -1.70% |
| 11. Test area exponent directly for degeneracy | cull | 12,614,584 | +0.00% |
| 12. Clamp magnitudes before the fixed-point NEON conversion | projection | 33,896,726 | -10.61% |
| 13. Convert first and replace only extended lanes | projection | 34,023,818 | -10.28% |
| 14. Use unsigned saturating subtraction for FREST exponent | projection | 37,921,306 | +0.00% |
| 15. Test FI borrow with an unsigned vector comparison | projection | 37,709,486 | -0.56% |
| 16. Reduce the maximum magnitude before FP repair | both | 48,955,461 | -3.13% |
| 17. Gather reciprocal table entries instead of byte-table shuffles | projection | 37,582,394 | -0.89% |
| 18. Bypass the compaction shuffle for all-live batches | cull | 11,083,167 | -0.20% |
| 19. Emit a four-triangle interleaved tail | output | 202,326 | -19.86% |
| 20. Unroll the eight-triangle serializer twice | output | 237,094 | -6.09% |
| 21. Specialize DMA copies of 16 bytes | spurs | 1,785,998 | -4.17% |
| 22. Specialize DMA copies of 32 bytes | spurs | 1,790,426 | -3.94% |
| 23. Specialize DMA copies of 64 bytes | spurs | 1,797,478 | -3.56% |
| 24. Specialize DMA copies of 128 bytes | spurs | 1,791,408 | -3.88% |
| 25. Specialize DMA copies of 16, 32, 64, 128 | spurs | 1,754,526 | -5.86% |
| 26. Specialize DMA copies of 16, 32, 48, 64, 128 | spurs | 1,751,716 | -6.01% |
| 27. Specialize DMA copies of 16, 32, 48, 64, 128, 208, 240 | spurs | 1,704,814 | -8.53% |
| 28. Specialize DMA copies of 32, 64, 128 | spurs | 1,775,968 | -4.71% |
| 29. Specialize DMA copies of 16, 32, 64 | spurs | 1,772,958 | -4.87% |
| 30. Loop over 16-byte chunks up to 128 bytes | spurs | 1,815,330 | -2.60% |
| 31. Overlapping first and last chunks for 16–64 bytes | spurs | 1,777,385 | -4.64% |
| 32. Fall-through chunk stores up to 64 bytes | spurs | 1,794,129 | -3.74% |

Ten geometry combinations were then compared. The combination of flag union
and intersection, stable compaction, fixed-point clamping and deferred repair
was selected. In that combination the modified global FP repair predicate
saved only 21,474 further projection instructions. Reciprocal gathers and
smaller arithmetic refinements also expand the changed code surface. They
remain experiments. Adding loop unrolling to the four-triangle tail took
213,102 instructions versus 202,326 for the four-triangle tail alone.

## Local artifacts

The ignored directory
`analysis/android-mobile-cpu/eu100-geometry-spurs-iterations/` contains the
APK, matching unstripped library, guest decompilations, cohort manifests,
trial patches, replay code and counter logs. Apply a single trial patch to
the control sources for an isolated comparison. Preserve the incoming FP
mode and compile geometry with `-frounding-math -ffp-contract=off`.

The final APK passed the v2 signature, 16 KiB ZIP alignment and 11 JNI/SDL
export checks. The incremental packager had left an unused compressed-library
gap. Realigning and signing with the same debug key reduced the file from
57,527,744 to 29,172,914 bytes. Every ZIP entry retained its original content,
compression method and compressed size. The final APK SHA-256 is
`ffabf495f6bbaf0b4d727fdda94fa29660e498eaf7b071ce7cb1a87c59046a7d`.
The packaged library and its symbols have build ID
`d5ccaa0c0e07bfdbd70e238476335cc290d8842e`.
