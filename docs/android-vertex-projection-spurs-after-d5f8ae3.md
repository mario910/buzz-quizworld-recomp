# EU 1.00 Android projection and SPURS from d5f8ae3

The mobile CPU profile was evaluated on captured image-41 submissions with the original PS3 code and Android AArch64 replay binaries. The archive contains 19,508 geometry jobs from six menu, multiplayer and solo sessions. The same 750-job selection and 750-job confirmation sets as in the preceding pass were reused. They and the reserved 600-job timing set are disjoint by decompressed capture digest. The full corpus includes all three.

## Integrated instruction counts

Android NDK r30 Clang, Bionic, RTZ/FTZ and disabled FP contraction were used under QEMU. Geometry counts start after job entry parsing and cover host preparation plus commit for accepted jobs. Fallback execution, capture inflation, output hashing, later command publication, rendering and GPU work are outside the markers. DMA counts cover input staging. Context counts cover descriptor preparation **and** publication of ioBuffer and the four cache pointers, once per clear mode. These scopes have different boundaries and cannot be summed to predict frame time.

| Scope | Sample | d5f8ae3 | Integrated | Change |
| --- | --- | ---: | ---: | ---: |
| Geometry | selection · 750 | 40,110,652 | 39,275,540 | -2.08% |
| Geometry | confirmation · 750 | 43,382,898 | 42,467,348 | -2.11% |
| Geometry | unseen · 600 | 30,673,277 | 30,035,464 | -2.08% |
| Geometry | all · 19,508 | 960,475,788 | 938,681,688 | -2.27% |
| Input DMA | selection · 750 | 593,385 | 563,928 | -4.96% |
| Input DMA | confirmation · 750 | 642,301 | 611,023 | -4.87% |
| Input DMA | unseen · 600 | 507,872 | 483,454 | -4.81% |
| Input DMA | all · 19,508 | 16,306,376 | 15,494,778 | -4.98% |
| Descriptor and pointers | selection · 750 | 171,750 | 114,000 | -33.62% |
| Descriptor and pointers | confirmation · 750 | 171,750 | 114,000 | -33.62% |
| Descriptor and pointers | unseen · 600 | 137,400 | 91,200 | -33.62% |
| Descriptor and pointers | all · 19,508 | 4,467,332 | 2,965,216 | -33.62% |

On the selection set, the hottest projection symbol fell from 10,683,607 to 10,148,470 instructions. Viewport parameter preparation fell from 819,360 to 595,263. Counts are attributed to enclosing symbols and depend on inlining. The geometry result above includes both effects.

Ghidra imported the original PS3 SPU image 6eba as big-endian. The clip reference calls 0x6188. Its loop contains FREST at 0x6574 and FI at 0x6590. FREST supplies one of 32 fixed fractions. In every entry the 13-bit base is at least the 10-bit step. Consequently subtracting step times a 19-bit interpolation coordinate from base shifted left 19 cannot borrow. The profile preserves the exact result, including the zero-exponent case outside bounded projection. It also recognizes the default identity matrix after checking its actual 64 bytes and the range of all projected columns.

## Sequential qualification

Each round completed on both samples before its successor was chosen. A candidate had to reproduce admission and full-store checksums per job, or the entire staged workspace and cursor where applicable. The promotion threshold was a reduction greater than 0.1% on each sample. Thirteen of the 31 trials met it. A round's decision below identifies its experimental parent for subsequent rounds. Release decisions also considered measured CPU time.

| Round | Parent | Scope and trial | Selection instructions | Confirmation instructions | Trial decision |
| --- | --- | --- | ---: | ---: | --- |
| r01 | base | job · Reuse composed viewport parameters after checking all codec, viewport and matrix bytes | 40,110,652 → 40,125,477 (+0.04%) | 43,382,898 → 43,395,032 (+0.03%) | Drop |
| r02 | base | job · Use saturating ARM scaled conversion directly for proved-finite screen coordinates | 40,110,652 → 40,089,761 (-0.05%) | 43,382,898 → 43,358,005 (-0.06%) | Drop |
| r03 | base | job · Extend the first reciprocal table lookup with TBX for its upper half | 40,110,652 → 40,069,311 (-0.10%) | 43,382,898 → 43,328,195 (-0.13%) | Keep |
| r04 | r03 | job · Test FI interpolation borrow using a native unsigned comparison | 40,069,311 → 39,899,073 (-0.42%) | 43,328,195 → 43,141,662 (-0.43%) | Keep |
| r05 | r04 | job · Multiply bounded coordinates by the absolute reciprocal and repair zero with the W sign | 39,899,073 → 39,944,234 (+0.11%) | 43,141,662 → 43,087,667 (-0.13%) | Drop |
| r06 | r04 | job · Construct X and Y clip flags from a single SPU ordering key per coordinate | 39,899,073 → 39,909,100 (+0.03%) | 43,141,662 → 43,167,085 (+0.06%) | Drop |
| r07 | r04 | job · Unroll the fixed viewport codec fingerprint checks | 39,899,073 → 39,898,726 (-0.00%) | 43,141,662 → 43,141,315 (-0.00%) | Drop |
| r08 | r04 | job · Unroll the coarse position bounds scan by two blocks | 39,899,073 → 39,876,233 (-0.06%) | 43,141,662 → 43,113,442 (-0.07%) | Drop |
| r09 | r04 | job · Eliminate unreachable FI borrow when interpolating the exact FREST table | 39,899,073 → 39,511,154 (-0.97%) | 43,141,662 → 42,687,743 (-1.05%) | Keep |
| r10 | r09 | job · Omit the zero-exponent mask after bounded W admission | 39,511,154 → 39,426,313 (-0.21%) | 42,687,743 → 42,608,326 (-0.19%) | Keep |
| r11 | r10 | job · Specialize composition of the verified ordinary identity viewport matrix | 39,426,313 → 39,202,216 (-0.57%) | 42,608,326 → 42,382,696 (-0.53%) | Keep |
| r12 | r11 | job · Unroll all four composed output columns after viewport admission | 39,202,216 → 39,174,465 (-0.07%) | 42,382,696 → 42,354,753 (-0.07%) | Drop |
| r13 | r11 | job · Keep a local copy of the coarse projection parameters across bounds checks | 39,202,216 → 39,206,505 (+0.01%) | 42,382,696 → 42,386,985 (+0.01%) | Drop |
| r14 | r11 | job · Separate proved and checked projection loops once per submission | 39,202,216 → 39,208,574 (+0.02%) | 42,382,696 → 42,395,303 (+0.03%) | Drop |
| r15 | r11 | job · Use native absolute-zero comparison at the end of each finite dot product | 39,202,216 → 40,460,229 (+3.21%) | 42,382,696 → 43,543,953 (+2.74%) | Drop |
| r16 | r11 | descctx · Zero four cache pointers together for submissions without cache DMA | 171,750 → 159,750 (-6.99%) | 171,750 → 159,750 (-6.99%) | Keep |
| r17 | r16 | descctx · Clear only surviving context bytes before publishing the five live pointers | 161,250 → 160,500 (-0.47%) | 161,250 → 160,500 (-0.47%) | Keep |
| r18 | r17 | descctx · Fuse zero-cache pointer publication with descriptor clearing in the geometry family | 162,000 → 157,500 (-2.78%) | 162,000 → 157,500 (-2.78%) | Keep |
| r19 | r18 | descctx · Store the big-endian IO pointer as a single unaligned word | 159,000 → 157,500 (-0.94%) | 159,000 → 157,500 (-0.94%) | Keep |
| r20 | r19 | stage · Recognize 48-byte input stream descriptions in DMA slot one | 593,385 → 589,009 (-0.74%) | 642,301 → 638,235 (-0.63%) | Keep |
| r21 | r20 | stage · Recognize the 240-byte mesh payload in DMA slot eight | 589,009 → 587,133 (-0.32%) | 638,235 → 636,295 (-0.30%) | Keep |
| r22 | r21 | stage · Recognize 16-byte final DMA records directly | 587,133 → 586,899 (-0.04%) | 636,295 → 635,803 (-0.08%) | Drop |
| r23 | r21 | stage · Transfer full kilobytes in large SPURS segments before 512-byte chunks | 587,133 → 570,525 (-2.83%) | 636,295 → 616,676 (-3.08%) | Keep |
| r24 | r23 | stage · Use the fixed-block DMA copier starting at 512 bytes | 570,525 → 563,928 (-1.16%) | 616,676 → 611,023 (-0.92%) | Keep |
| r25 | r24 | stage · Recognize 32-byte secondary index descriptors in DMA slot two | 563,928 → 564,388 (+0.08%) | 611,023 → 611,483 (+0.08%) | Drop |
| r26 | r24 | job · Project four bounded vertices per native NEON register through reciprocal, flags and packing | 39,202,216 → 41,220,818 (+5.15%) | 42,382,696 → 44,271,800 (+4.46%) | Drop |
| r27 | r24 | job · Extract FI interpolation base with a single masked shift | 39,202,216 → 39,187,329 (-0.04%) | 42,382,696 → 42,367,616 (-0.04%) | Drop |
| r28 | r24 | job · Canonicalize the verified identity column directly in integer registers | 39,202,216 → 39,172,245 (-0.08%) | 42,382,696 → 42,352,520 (-0.07%) | Drop |
| r29 | r24 | job · Construct the bounded reciprocal exponent and fraction directly from the live W bits | 39,202,216 → 39,160,253 (-0.11%) | 42,382,696 → 42,343,445 (-0.09%) | Drop |
| r30 | r24 | job · Use two exact finite fused operations for the bounded reciprocal refinement | 39,202,216 → 39,212,728 (+0.03%) | 42,382,696 → 42,403,246 (+0.05%) | Drop |
| r31 | r24 | job · Admit all sixteen bounded projection coefficients with a vector magnitude reduction | 39,202,216 → 39,180,754 (-0.05%) | 42,382,696 → 42,361,234 (-0.05%) | Drop |

## Thread CPU and release checks

Timers use CLOCK_THREAD_CPUTIME_ID in Android executables on one pinned x86 host core under QEMU. Geometry runs warmed persistent processes and alternated arm order. Each series has two warmups and ten measured passes. DMA rows give per-pass time across the named captures, and repeats indicate the number of copies per submission. The complete-corpus DMA check transfers each input once per arm and crosses both order and destination. The context timing repeats each of the 600 descriptors 128 times in each of two clear modes. Microbenchmarks with repeated inputs have different cache behavior from a game submission.

| Scope | Baseline median ms | Candidate median ms | Median paired change | Paired range | Faster pairs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Geometry · 600, series 1 | 104.33 | 101.76 | -1.73% | -7.15% to +3.94% | 9/10 |
| Geometry · 600, series 2 | 103.76 | 102.44 | -0.87% | -5.56% to +2.29% | 7/10 |
| DMA · 600, 32 repeats | 31.50 | 31.76 | +0.27% | -3.06% to +1.59% | 4/10 |
| DMA · 750, 8 repeats | 10.42 | 10.53 | +0.74% | -1.16% to +3.31% | 4/10 |
| DMA · 19,508, one transfer | 53.23 | 52.79 | -0.77% | -2.78% to +2.97% | 9/10 |
| Context · initial fused implementation | 53.87 | 54.85 | +2.33% | +0.80% to +3.19% | 0/10 |
| Context · pointer stores only | 53.49 | 53.31 | -0.67% | -2.00% to +0.88% | 8/10 |
| Context · copy then clear | 53.42 | 53.18 | -0.60% | -1.23% to +0.46% | 8/10 |
| Context · inline constant clear, experiment | 54.05 | 7.80 | -85.57% | -85.77% to -85.32% | 10/10 |
| Context · final generated adapter | 54.59 | 7.91 | -85.49% | -85.71% to -85.41% | 10/10 |
The initial fused context implementation lost all ten crossed CPU timing pairs despite its instruction reduction. Reversing function definition order produced the same result. Three additional release checks compared publishing just the pointer stores, copying before clearing, and inlining both constant clears. The final adapter uses the inline clears only for image 41, 21 input slots, zero cache entries, a 256-byte descriptor and the verified 64 + 1024-byte layout. The SDK paths handle other families and layouts. With LS clearing disabled, full-corpus context counts went from 1,268,020 to 1,209,496. With clearing enabled, they went from 3,199,312 to 1,755,720. The much larger timing change for the repeated context microbenchmark depends on the emulator's execution of the libc clear versus inlined stores. The actual clear-mode mix and queue latency require a device measurement.

Geometry was faster in 16 of 20 timing pairs, with a pooled median paired change of −1.02%. DMA staging was faster in 9 of 10 full-corpus single-transfer passes. The repeated hot-buffer DMA runs had small median increases. The shipping DMA path specializes 48-byte slot-one and 240-byte slot-eight inputs and copies large transfers in 1024-byte blocks with a 512-byte tail. Each specialization validates the live length. The mobile profile activates the changes through its existing geometry and SPURS switches.

The late CPU timing result changed the shipping context implementation. An incremental native rebuild and repackage replaced the preliminary archive after validation. The artifact directory contains a single final APK.

## Correctness and artifact

* The host made identical admission decisions on all 19,508 entries. It admitted 18,487 jobs, including 12,204 fully culled jobs. The admission byte and all 262,144 local-store bytes matched the d5f8ae3 output for every entry. The 5,113,924,660-byte stream has SHA-256 `7db32f9991d7e020f22acbe83be5cf7e40ad49b925453f17c580b6e506d83a24`.
* The reciprocal test compares all 8,388,608 mantissas against the separate FREST and FI operations for both general and bounded exponents. The original clip producer matched 24,406,612 corpus component values, 628,992 special-value components, and all 64 viewport/default-matrix mutations. The coarse oracle checked 17,776,640 reciprocal lanes and 32 invalid offscreen cases. The desktop projection reference passed its 30,000-block check.
* DMA checks covered all 32,768 lengths with guard pages and 7,200 staging boundary cases. The context harness compared all 1,088 workspace bytes in both clear modes, plus 3,072 size, cache-count and family combinations. The integrated comparison used the generated Android SDK adapter.
* No Android device was attached. Geometry captures contain submission data and SPU local state. They do not record a time-ordered frame, queue contention, GPU work or thermals. The CPU timings describe this replay only.

The local deliverable is `analysis/android-mobile-cpu/eu100-vertex-spurs-after-d5f8ae3/Buzz-EU100-mobile-cpu-debug.apk`. It passed APK v2 signature verification, 16 KiB ZIP alignment, artifact checks and the check for 11 JNI/SDL exports. APK SHA-256 is `7b015ac08f776931217785878eba654d0d532be851f4a99cc5dc3be047bff294`. The packaged arm64 library build ID is `26cecda178796e67832ee4c9c72331d745b8bd58`. The adjacent replay directory includes source variants, capture manifests, measurements and reproduction instructions.
