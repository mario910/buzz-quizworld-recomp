# Android geometry and SPURS from b1d8c63

On the complete Buzz EU 1.00 archive the mobile profile takes 6.74% fewer AArch64 instructions inside geometry preparation and commit. Input DMA staging takes 17.08% fewer and descriptor preparation takes 32.73% fewer across equally weighted clear modes. These are separate measurements. The ten paired runs under emulation showed little change in elapsed thread CPU time. Android device frame time remains unmeasured.

## Integrated results

Android NDK r30 Clang produced static Bionic AArch64 replay binaries. The harness sets guest-compatible RTZ/FTZ and disables FP contraction. A QEMU plugin counts executed guest instructions between markers. Geometry includes admission, preparation, index and vertex commit for accepted jobs. Capture inflation, verification, the lifted fallback on rejection, later publication, rendering and GPU work are outside the markers. DMA and descriptor comparisons each exercise both implementations on the same inputs. Scheduler waits and chain throughput are outside those scopes.

| Scope | Sample | b1d8c63 | Shipping | Change |
| --- | --- | ---: | ---: | ---: |
| Geometry | Selection · 750 | 43,990,854 | 40,110,652 | -8.82% |
| Geometry | Confirmation · 750 | 47,977,660 | 43,382,898 | -9.58% |
| Geometry | Independent check · 600 | 33,886,401 | 30,673,153 | -9.48% |
| Geometry | Complete · 19,508 | 1,029,900,827 | 960,476,897 | -6.74% |
| Input DMA | Selection · 750 | 710,221 | 593,385 | -16.45% |
| Input DMA | Confirmation · 750 | 779,208 | 642,301 | -17.57% |
| Input DMA | Independent check · 600 | 613,559 | 507,872 | -17.23% |
| Input DMA | Complete · 19,508 | 19,664,162 | 16,306,376 | -17.08% |
| Descriptor workspace | Selection · 750 | 206,250 | 138,750 | -32.73% |
| Descriptor workspace | Confirmation · 750 | 206,250 | 138,750 | -32.73% |
| Descriptor workspace | Independent check · 600 | 165,000 | 111,000 | -32.73% |
| Descriptor workspace | Complete · 19,508 | 5,364,700 | 3,608,980 | -32.73% |

The two fresh sets each contain 75 menu-only jobs, 75 skin-menu jobs and 150 from each of four multiplayer or solo sessions. Sampling excluded duplicates by decompressed SHA-256 across both sets and the reserved 600-job check. All three sets come from the same title and are included in the complete corpus. A job capture represents one submission, so individual jobs carry equal weight here regardless of the number submitted in any game frame.

On the selection set, instruction attribution by enclosing symbol put bounded projection first, with 11,053,452 instructions at baseline and 10,683,607 after integration. The uncached index decoder fell from 5,041,676 to 3,436,664 and vertex emission from 3,786,126 to 2,124,569. Inlining can shift attribution between symbols. The whole-job table includes those shifts.

All 19,508 captured SPURS descriptors had 256 bytes and 21 DMA slots. With the SDK clear flag off, descriptor staging took 1,482,608 → 838,844 instructions. With clearing on, it took 3,882,092 → 2,770,136. The combined descriptor result counts both modes once per capture. Integration guards on geometry image 41, the live DMA count and the live size. Other descriptors use the SDK implementation.

## Sequential experiments

Each round ran on both sets before choosing the next parent. Geometry required matching admission and local-store CRC for every input. DMA checked the store and end cursor. Descriptor trials checked the complete context, descriptor and spare area. Promotion required more than 0.1% fewer instructions on both sets. Sixteen rounds met that rule. One temporary cache was subsequently removed for its memory cost.

| Round | Parent | Scope and change | Selection instructions | Confirmation instructions | Decision |
| --- | --- | --- | ---: | ---: | --- |
| r01 | base | job · Compare proved-finite viewport coordinates directly in NEON | 43,990,854 → 45,320,559 (+3.02%) | 47,977,660 → 49,275,517 (+2.71%) | Drop |
| r02 | base | job · Canonicalize zero once after each bounded projection dot product | 43,990,854 → 43,621,396 (-0.84%) | 47,977,660 → 47,631,932 (-0.72%) | Keep |
| r03 | r02 | job · Specialize guest reciprocal seed exponent for bounded W | 43,621,396 → 43,694,321 (+0.17%) | 47,631,932 → 47,721,543 (+0.19%) | Drop |
| r04 | r02 | job · Validate all transformed coordinate bounds through the six NEON extrema | 43,621,396 → 43,123,984 (-1.14%) | 47,631,932 → 47,027,480 (-1.27%) | Keep |
| r05 | r04 | job · Reject a mixed or extreme W interval before constructing the other three rows | 43,123,984 → 43,105,564 (-0.04%) | 47,027,480 → 47,004,560 (-0.05%) | Drop |
| r06 | r04 | job · Use the eight-delta NEON unpacker for cached index misses | 43,123,984 → 42,667,435 (-1.06%) | 47,027,480 → 46,477,773 (-1.17%) | Keep |
| r07 | r06 | job · Accumulate the maximum explicit index separately from the monotone sequential stream | 42,667,435 → 42,443,332 (-0.53%) | 46,477,773 → 46,218,126 (-0.56%) | Keep |
| r08 | r07 | job · Consume reference flags from a shift register across calls | 42,443,332 → 42,296,288 (-0.35%) | 46,218,126 → 46,047,154 (-0.37%) | Keep |
| r09 | r08 | job · Decode four edge commands per packed byte and check the initial opcode once | 42,296,288 → 41,797,725 (-1.18%) | 46,047,154 → 45,468,096 (-1.26%) | Keep |
| r10 | r09 | job · Admit the complete reference-flag span once per triangle | 41,797,725 → 42,047,731 (+0.60%) | 45,468,096 → 45,756,676 (+0.63%) | Drop |
| r11 | r09 | job · Reuse decoded eight-direction blocks after checking every packed lane | 41,797,725 → 41,630,527 (-0.40%) | 45,468,096 → 45,386,383 (-0.18%) | Keep for trials |
| r12 | r11 | job · Transpose whole twenty-byte position, packed normal and UV records on ARM | 41,630,527 → 40,333,894 (-3.11%) | 45,386,383 → 43,694,931 (-3.73%) | Keep |
| r13 | r12 | job · Defer signed-zero repair in finite direction transforms before normalization | 40,333,894 → 40,084,839 (-0.62%) | 43,694,931 → 43,451,396 (-0.56%) | Keep |
| r14 | r13 | job · Compare the signed area word against the last positive denormal for backface rejection | 40,084,839 → 39,993,206 (-0.23%) | 43,451,396 → 43,364,754 (-0.20%) | Keep |
| r15 | r14 | job · Finish the projected area-range admission with an unsigned horizontal maximum | 39,993,206 → 40,008,672 (+0.04%) | 43,364,754 → 43,384,270 (+0.05%) | Drop |
| r16 | r14 | job · Unroll packed commands on the zero-width sequential mesh path independently | 39,993,206 → 39,945,383 (-0.12%) | 43,364,754 → 43,303,114 (-0.14%) | Keep |
| r17 | r16 | descriptor · Clear only the context and descriptor suffix surviving the live copy | 206,250 → 192,000 (-6.91%) | 206,250 → 192,000 (-6.91%) | Keep |
| r18 | r17 | descriptor · Inline the 256-byte SPURS descriptor transfer when its length matches | 192,000 → 132,750 (-30.86%) | 192,000 → 132,750 (-30.86%) | Keep |
| r19 | r18 | descriptor · Specialize the entire clear, copy and tail layout for the captured descriptor size | 132,750 → 129,750 (-2.26%) | 132,750 → 129,750 (-2.26%) | Keep |
| r20 | r19 | stage · Bulk-transfer large DMA segments in 512-byte chunks before the existing tail | 710,221 → 609,025 (-14.25%) | 779,208 → 658,101 (-15.54%) | Keep |
| r21 | r20 | stage · Reuse the short aligned DMA copier for the remainder of a long transfer | 609,025 → 629,579 (+3.37%) | 658,101 → 677,308 (+2.92%) | Drop |
| r22 | r20 | stage · Advance the cursor by the proven constant on each recognized DMA slot | 609,025 → 593,385 (-2.57%) | 658,101 → 642,301 (-2.40%) | Keep |
| r23 | r22 | job · Reduce the per-worker packed direction cache from 256 cells to 64 | 39,945,383 → 40,056,782 (+0.28%) | 43,303,114 → 43,410,465 (+0.25%) | Drop |
| r24 | r22 | job · Remove the direction cache after integration and measure its remaining contribution | 39,945,383 → 40,112,905 (+0.42%) | 43,303,114 → 43,385,151 (+0.19%) | Adopt at integration for memory |
| r25 | r22 | job · Write 20-byte vertex records from register transposes without a temporary array | 39,945,383 → 39,972,239 (+0.07%) | 43,303,114 → 43,338,086 (+0.08%) | Drop |
| r26 | r22 | job · Reuse the exact refined reciprocal for identical W blocks within one bounded projection | 39,945,383 → 40,086,078 (+0.35%) | 43,303,114 → 43,349,297 (+0.11%) | Drop |
| r27 | r22 | job · Allocate the direction cache only on threads that decode unit vectors | 39,945,383 → 40,167,208 (+0.56%) | 43,303,114 → 43,532,921 (+0.53%) | Drop |

The 256-cell direction cache cost 45,056 bytes of static TLS per worker. Removing it raised whole-corpus geometry from 957,777,494 to 960,535,337 instructions in the trial builds (0.29%). Smaller and lazily allocated variants failed qualification. The integrated profile omits that cache. It enables the existing batched index decoder and retains the 20-byte position, packed normal and half-UV writer for eligible AArch64 vertices. No additional graphics setting was needed.

## Original SPU behavior and integration checks

Ghidra headless imported the original PS3 image 6eba as SPU big-endian and produced pseudocode and assembly for 0x6608 and 0x09f8. The replay bundle contains the extracts and scripts. Numerical equivalence was checked with the original producer fixtures, including signed zero, denormals, extended exponents, sparse weights and mixed projection W.

* All 19,508 jobs made identical admission decisions against the baseline. The 18,487 admitted jobs included 12,204 fully culled jobs. One admission byte and all 262,144 local-store bytes per submission matched over the complete 5,113,924,660-byte output stream. SHA-256 was `7db32f9991d7e020f22acbe83be5cf7e40ad49b925453f17c580b6e506d83a24`.
* The original-producer stage tests matched 19,399 full skin producers, 24,406,612 clip component values, 5,280,336 bytes of index output and 30,927,600 vertex bytes across 6,535 emitting jobs. The index decoder reproduced 19,745,661 logical indices across all 19,508 entries, including inputs beyond full host-producer admission.
* The SPURS checks covered 7,200 staging boundary cases, all 32,768 representable DMA lengths, guarded descriptor input, both clear modes and 6,144 size/mode/family combinations. The coarse-clip mutation test requires 32 suitable offscreen jobs. The fresh selection provided too few, so the unchanged binary ran on the canonical 1,200-job fixture and passed all 32. The x86 default index path passed its sampled cache and collision checks.
* Reconstructing r09 and shipping from their archived sources reproduced the per-job output records. Rebuilt instruction counts can differ slightly. The tables use the original paired qualification binaries.

## CPU timing and Android package

Two persistent Android binaries processed the reserved 600-job set on one pinned x86 host CPU under QEMU. After two warmups, ten paired passes alternated execution order. Median thread CPU per pass went from 103.50 ms to 103.28 ms. Seven pairs favored the integrated build. The median paired change was −0.18%, with individual pairs ranging from −2.84% to +6.08%. The observed spread is larger than the median change. Device FPS, thermal behavior and GPU time require a phone measurement.

One offline debug APK was assembled after integration. The 16 packaged entries passed the content/compression check, the APK passed 16 KiB alignment and v2 signature checks, and the packaged arm64 library matched the verified build ID with 11 exported JNI/SDL symbols. APK SHA-256 is `289f6d6829a7a73a371d291ffd4623ba43caa9c2be36663296d4666cc5735873`. Library build ID is `60b7421b0c1631089fad6dfcdf3557d14f4c25b5`.

Local artifacts are in `analysis/android-mobile-cpu/eu100-projection-index-after-b1d8c63/`. The directory contains the APK, native library, checksums, Ghidra extracts, qualification log, variant sources, input manifests and replay instructions.
