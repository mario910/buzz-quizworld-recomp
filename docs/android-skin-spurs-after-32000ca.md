# Android geometry and SPURS from 32000ca

The EU 1.00 mobile CPU profile went through 29 sequential trials on saved image-41 geometry submissions. The Android build was assembled once after release qualification. All CPU measurements here use Android NDK r30, Bionic AArch64 code and QEMU user mode on x86 with the original RTZ/FTZ settings. Neither queue wait nor rendering is present in the captures.

## Where CPU time goes

The geometry stage timers use CLOCK_THREAD_CPUTIME_ID around each function in a separate diagnostic build. The table gives median milliseconds per 480 submitted jobs over five pinned passes per arm with alternating order. A call is charged only when its stage runs. Instrumentation affects inlining and adds timer overhead. The ordinary integrated geometry replay later in the report times only the enclosing production preparation and commit.

| Geometry stage | 32000ca ms | Integrated ms |
| --- | ---: | ---: |
| Skinning and rigid object-space transforms | 34.62 | 34.76 |
| Vertex projection | 32.26 | 34.19 |
| Coarse homogeneous bounds | 14.70 | 14.93 |
| Index decode and cache lookup | 12.49 | 13.01 |
| Vertex input decode | 9.93 | 9.37 |
| Triangle culling | 6.82 | 7.13 |
| Vertex emission | 4.99 | 5.00 |
| Viewport parameters | 2.14 | 2.14 |

The two dominant geometry stages remained skinning/object transforms and projection. The instrumented projection median increased in the joined build, while the primary whole-job timing improved on both timing cohorts. Individual probe timings and whole-job timings come from different binaries.

For SPURS, the source image was the actual 59,008-byte PS3 job binary. The archive contains 19,508 matching binary lengths, 384-byte gaps before IO and 256-byte lookahead spans. The SDK permits a reused-prefix offset of 0xe400 after its existing validation. A complete image copy and a 640-byte suffix copy are mutually exclusive. The two zero operations apply when lazy clearing is active. The captures do not contain the worker's preceding reuse state or the live frequency of each clear mode.

| SPURS scope | Baseline ms per 480 submissions |
| --- | ---: |
| Full image transfer | 2.701 |
| Image suffix after validated reuse | 0.777 |
| Pre-IO zero in lazy mode | 0.752 |
| Post-DMA lookahead zero in lazy mode | 0.716 |
| Input DMA staging | 1.357 |
| Descriptor and pointers, average of both clear modes | 0.254 |

The largest measured SPURS data movement was the full image transfer when required. With a reusable prefix, the input DMA list took the most time. These replay boundaries omit scheduling, prefix validation, source inflation and command publication. The original SDK still chooses the prefix, validates it and advances the lazy-zero cursor.

## Joined build

The fresh selection and confirmation sets each contain 80 captures from each of six menu, solo and multiplayer sources. Sampling used seed 32000 and excluded decompressed SHA-256 identities from both previous 750-job cohorts and the reserved 600-job set. Both fresh cohorts also exclude duplicates within and across them. The complete 19,508-job archive includes those sets.

| Production scope | Jobs | Baseline ARM64 instructions | Integrated | Change |
| --- | --- | ---: | ---: | ---: |
| Geometry prepare and commit | 480 (selection) | 30,948,339 | 29,710,400 | -4.00% |
| Geometry prepare and commit | 480 (confirmation) | 27,243,981 | 26,105,588 | -4.18% |
| Geometry prepare and commit | 600 (unseen) | 30,035,534 | 28,819,173 | -4.05% |
| Geometry prepare and commit | 19,508 (all) | 938,680,464 | 897,659,387 | -4.37% |
| Full image | 19,508 (all) | 144,924,932 | 145,022,472 | +0.07% |
| Reused image suffix | 19,508 (all) | 2,594,564 | 1,385,068 | -46.62% |
| Lazy pre-IO gap | 19,508 (all) | 1,794,736 | 975,400 | -45.65% |
| Lazy post-DMA lookahead | 19,508 (all) | 1,599,656 | 838,844 | -47.56% |

Only the measured host admission, preparation and geometry output commit are inside the geometry markers. Fallback execution, frame time and GPU time are outside them. The SPURS transfer rows are measured separately; the full and reused image counts describe alternatives.

| Pinned CPU comparison | Baseline median ms | Integrated median ms | Median paired change | Paired range | Faster pairs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Geometry · 480 confirmation jobs | 86.278 | 85.849 | -0.63% | -11.67% to +1.74% | 6/10 |
| Geometry · 600 independent jobs | 104.253 | 102.453 | -2.03% | -3.51% to -0.22% | 10/10 |
| Full image · 600 jobs | 3.163 | 3.124 | +0.43% | -2.76% to +6.10% | 5/10 |
| Reused image suffix · 600 jobs | 0.806 | 0.455 | -42.97% | -45.24% to -39.43% | 10/10 |
| Lazy pre-IO gap · 600 jobs | 0.818 | 0.401 | -50.18% | -55.83% to -40.52% | 10/10 |
| Lazy post-DMA lookahead · 600 jobs | 0.774 | 0.369 | -50.90% | -55.88% to -43.90% | 10/10 |

Each series discarded two warmups and retained ten paired passes. Geometry used two persistent processes with alternating order. Each SPURS pass transferred each capture once per arm, crossed arm order and destination buffers, and compared the whole local store. Thread-clock overhead is material for small copies. The results identify candidates for a device build and do not establish a frame-rate delta.

## Sequential experiments

Candidates inherited the last version that passed both cohorts. The instruction threshold was a reduction greater than 0.1% on each cohort. Geometry required identical admission plus an identical local-store CRC for every job. Transfer experiments required matching all local-store bytes, including untouched prefixes and suffixes. Fourteen trials met those criteria. Release timing separately vetoed the cold-copy implementations. Trial 26 produced different output and was rejected. Trial 27 corrected the Z-axis mapping and passed correctness but missed the instruction threshold.

| Trial | Parent | Change | Selection instructions | Confirmation instructions | Decision |
| --- | --- | --- | ---: | ---: | --- |
| r01 | base | Interpolate FRSQEST directly for object direction normalization | 30,948,339 → 30,858,882 (-0.29%) | 27,243,981 → 27,153,574 (-0.33%) | Keep |
| r02 | r01 | Replace scalar inverse-square-root gathers with two NEON register table lookups | 30,858,882 → 31,497,743 (+2.07%) | 27,153,574 → 27,765,849 (+2.25%) | Drop |
| r03 | r01 | Test the object matrix bound in three NEON vectors | 30,858,882 → 30,857,596 (-0.00%) | 27,153,574 → 27,148,914 (-0.02%) | Drop |
| r04 | r01 | Separate full-block transforms from the final active-lane mask | 30,858,882 → 30,635,328 (-0.72%) | 27,153,574 → 26,960,077 (-0.71%) | Keep |
| r05 | r04 | Share the two double reciprocal endpoints across all coarse clip planes | 30,635,328 → 30,636,262 (+0.00%) | 26,960,077 → 26,960,986 (+0.00%) | Drop |
| r06 | r04 | Precompute absolute coordinate bounds for the four row error estimates | 30,635,328 → 30,631,692 (-0.01%) | 26,960,077 → 26,956,789 (-0.01%) | Drop |
| r07 | r04 | Recognize zero or subnormal triangle area from the SPU exponent bits | 30,635,328 → 30,635,328 (+0.00%) | 26,960,077 → 26,960,077 (+0.00%) | Drop |
| r08 | r04 | Reduce the surviving maximum index directly with UMAXV | 30,635,328 → 30,630,248 (-0.02%) | 26,960,077 → 26,954,937 (-0.02%) | Drop |
| r09 | r04 | Broadcast projection coefficients from lanes of a single row register | 30,635,328 → 30,433,351 (-0.66%) | 26,960,077 → 26,769,619 (-0.71%) | Keep |
| r10 | r09 | Fully unroll the four coarse homogeneous bounds | 30,433,351 → 30,433,008 (-0.00%) | 26,769,619 → 26,769,341 (-0.00%) | Drop |
| r11 | r09 | Accumulate two independent homogeneous interval rows in FP64 NEON lanes | 30,433,351 → 30,404,265 (-0.10%) | 26,769,619 → 26,743,316 (-0.10%) | Drop |
| r12 | r09 | Hoist the contiguous position stream base out of the coarse scan | 30,433,351 → 30,396,147 (-0.12%) | 26,769,619 → 26,738,897 (-0.11%) | Keep |
| r13 | r12 | Copy the three index vectors directly when a complete eight-triangle group survives | 30,396,147 → 30,383,739 (-0.04%) | 26,738,897 → 26,724,557 (-0.05%) | Drop |
| r14 | r12 | Advance the projection input from one precomputed stream base | 30,396,147 → 30,333,470 (-0.21%) | 26,738,897 → 26,681,246 (-0.22%) | Keep |
| r15 | r14 | Unroll the projection and flag-reduction loop by two vertex blocks | 30,333,470 → 30,270,719 (-0.21%) | 26,681,246 → 26,623,521 (-0.22%) | Keep |
| r16 | r15 | Inline the 640-byte live image suffix after SDK binary reuse admission | 63,840 → 34,080 (-46.62%) | 63,840 → 34,080 (-46.62%) | Keep |
| r17 | r16 | Inline the 384-byte lazy zero gap between the binary and IO region | 44,160 → 22,080 (-50.00%) | 44,160 → 22,080 (-50.00%) | Keep |
| r18 | r17 | Inline the 256-byte lazy zero lookahead after the staged DMA range | 40,320 → 20,160 (-50.00%) | 40,320 → 20,160 (-50.00%) | Keep |
| r19 | r18 | Stream the full cold image with a fixed 512-byte block and exact tail | 3,567,840 → 2,067,360 (-42.06%) | 3,567,840 → 2,067,360 (-42.06%) | Trial only · slower CPU time |
| r20 | r19 | Increase cold image transfer blocks to one KiB | 2,066,880 → 1,943,520 (-5.97%) | 2,066,880 → 1,943,520 (-5.97%) | Trial only · slower CPU time |
| r21 | r20 | Delegate the known full image span to the Android libc copy | 1,943,040 → 3,568,320 (+83.65%) | 1,943,040 → 3,568,320 (+83.65%) | Drop |
| r22 | r20 | Apply exact no-borrow FRSQEST interpolation to packed unit-vector decoding | 30,270,719 → 30,128,694 (-0.47%) | 26,623,521 → 26,490,730 (-0.50%) | Keep |
| r23 | r22 | Avoid repairing the zero sign of nonnegative squared coordinates and their sum | 30,128,694 → 30,038,994 (-0.30%) | 26,490,730 → 26,406,862 (-0.32%) | Keep |
| r24 | r23 | Use a finite product after masking the zero-remainder inverse estimate | 30,038,994 → 29,694,557 (-1.15%) | 26,406,862 → 26,091,411 (-1.19%) | Keep |
| r25 | r24 | Use one read-only 1024-value table for the exact packed direction coordinates | 29,694,557 → 30,136,232 (+1.49%) | 26,091,411 → 26,497,782 (+1.56%) | Drop |
| r26 | r24 | Select the unit-vector dominant axis directly from selector bits | 29,694,557 → 29,637,227 (-0.19%) | 26,091,411 → 26,050,359 (-0.16%) | Drop · output mismatch |
| r27 | r24 | Compare the masked selector against both unshifted Y and Z encodings | 29,694,557 → 29,698,402 (+0.01%) | 26,091,411 → 26,095,450 (+0.02%) | Drop |
| r28 | r24 | Construct the direction handedness sign directly from the selector | 29,694,557 → 29,680,695 (-0.05%) | 26,091,411 → 26,078,593 (-0.05%) | Drop |
| r29 | r24 | Return complete decoded direction blocks before tail masking | 29,694,557 → 29,694,585 (+0.00%) | 26,091,411 → 26,091,439 (+0.00%) | Drop |

Ghidra decompilation and disassembly covered SPU routines 0x2c00, 0x9148 and 0x92a0 in image 6eba. FRSQEST at 0x2ce4 and FI at 0x2cf0 illustrate the estimate/interpolation sequence. Every fraction in the 64-entry FRSQEST table has a base at least as large as its step, and the estimate always has a nonzero exponent. The shared interpolation implements that invariant for rigid direction normalization and packed direction decoding. The separate FREST path retains its exponent guard. The profile also uses lane FMAs for bounded projection, a separate tail mask for rigid transforms and fixed-length guard-checked SPURS transfers. Other image families and lengths use the original libc calls.

The 1 KiB cold-copy variant reduced dynamic instructions but took 19.36% more thread CPU on the median single-transfer qualification pass. The release leaves the original full-image copy in place. The warm suffix and lazy zeros improved both metrics. The new code runs through the existing mobile profile switches.

## Output checks and package

* All 19,508 jobs matched their baseline admission decision and all 262,144 local-store bytes. The 5,113,924,660-byte record stream has SHA-256 `7db32f9991d7e020f22acbe83be5cf7e40ad49b925453f17c580b6e506d83a24`.
* The exhaustive packed-direction grid compared all 1,048,576 coordinate pairs across 12 supported selectors. All 201,326,592 output bytes matched 32000ca. A separate test matched 57,344 vertices against the original lifted decoder. The interpolation test checked all 8,388,608 mantissas against the separate estimate and FI operations, including varied exponents and signs.
* The full reference-producer test matched 19,399 original full-skin jobs and 24,406,612 original clip component values. Corner and mutation tests covered exceptional arithmetic, invalid indices, viewport changes and guarded ends. The Windows-style copy/guard test ran on arm64 Bionic, including the 255/256/257, 383/384/385 and 639/640/641 boundaries. Transfer replays compared 160 additional size and family combinations with the source ending at a protected page. The desktop projection reference passed its 30,000-block comparison.
* Independent reconstruction reproduced the per-job outputs of r09 and the final build, plus the instruction comparisons for r20 and the released warm copy. The local replay directory contains input manifests, variant overlays, profiles, timings, Ghidra extracts in the adjacent directory and the raw qualification log.

The single final APK is `analysis/android-mobile-cpu/eu100-skin-spurs-after-32000ca/Buzz-EU100-mobile-cpu-debug.apk`. SHA-256 is `eb9c4c86ef0af1f300acd0787c1488d0a49ebfccb2c46d8f8cc412e77afec129` and the native library build ID is `937eeda17b775a2b34b387d476795a2436499250`. Package checks verified all 16 ZIP entries, compression, 16 KiB alignment, v2 signing and the 11 JNI/SDL exports.
