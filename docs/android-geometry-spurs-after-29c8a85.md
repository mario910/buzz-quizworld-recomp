# Mobile geometry and SPURS CPU replay from 29c8a85

The EU 1.00 mobile CPU profile incorporates nine of 26 measured changes through the existing buzzGeometryMobileKernels, buzzGeometryIndexBatched and buzzSpursDirectInputs settings. Android NDK r30 compiled the baseline and candidate for AArch64/Bionic, API 29. QEMU user mode ran both on the same x86 host with the job’s RTZ/FTZ settings. The six capture sources cover menus, solo and multiplayer. Each tuning and confirmation set contains 480 jobs, with 80 from each source. A separate 600-job holdout uses seed 0x29c8a85 and 100 jobs per source. All 1,560 decompressed capture identities are distinct. The full archive contains these samples.

## Whole-job measurements

Dynamic instruction counts cover job preparation and both applicable commit operations, including admission checks for attempts that fall back. The replay stops at rejection and excludes execution of the fallback implementation. The staging counts cover traversal of the input DMA list and copying its payload into SPU local store. The two scopes have different costs and run separately.

| Scope | Set (jobs) | 29c8a85 instructions | Mobile instructions | Change |
| --- | --- | --- | --- | --- |
| Geometry | selection (480) | 29,710,295 | 28,870,123 | -2.83% |
| Input DMA | selection (480) | 398,088 | 392,556 | -1.39% |
| Geometry | confirmation (480) | 26,105,483 | 25,321,606 | -3.00% |
| Input DMA | confirmation (480) | 356,130 | 350,361 | -1.62% |
| Geometry | holdout (600) | 28,466,442 | 27,655,846 | -2.85% |
| Input DMA | holdout (600) | 452,276 | 445,268 | -1.55% |
| Geometry | all (19,508) | 897,659,381 | 880,235,223 | -1.94% |
| Input DMA | all (19,508) | 15,494,778 | 15,264,203 | -1.49% |

Pinned, interleaved A/B runs used thread CPU time around the production operations. File I/O, capture inflation and output checks run outside the timers. Geometry alternated the two persistent processes in AB/BA order. DMA alternated the order and destination at job level, with one transfer per submission. Each series discarded its first two passes. The change column is the median of the paired changes. The time columns are separate medians.

| Scope | Set | 29c8a85 ms | Mobile ms | Paired change | Faster pairs |
| --- | --- | --- | --- | --- | --- |
| Geometry | confirmation | 84.932 | 82.464 | -3.12% | 10/10 |
| Geometry | holdout | 94.232 | 93.638 | -0.53% | 8/10 |
| Input DMA | confirmation | 1.133 | 1.115 | -1.89% | 10/12 |
| Input DMA | holdout | 1.526 | 1.490 | -0.88% | 9/12 |

The geometry timing gain was smaller on the holdout. Each source also received an independent replay of its 100 holdout captures, starting with an empty per-worker index cache.

| Source | 29c8a85 instructions | Mobile instructions | Change |
| --- | --- | --- | --- |
| menu-only | 8,574,698 | 8,252,686 | -3.76% |
| multi-private | 5,265,241 | 5,128,752 | -2.59% |
| multi-session | 3,595,058 | 3,498,780 | -2.68% |
| multi-verified | 3,189,919 | 3,112,507 | -2.43% |
| skin-menu | 5,748,380 | 5,535,788 | -3.70% |
| solo-session | 4,303,241 | 4,200,553 | -2.39% |

## Remaining CPU costs

Five alternating pinned passes of an instrumented build measured each geometry stage on the 480-job selection set. Each value is the median thread CPU time across passes. The extra timers affect function layout and inlining.

| Stage | 29c8a85 ms | Mobile ms |
| --- | --- | --- |
| Skinning and rigid transforms | 34.12 | 33.66 |
| Vertex projection | 33.44 | 32.40 |
| Coarse homogeneous bounds | 14.47 | 14.38 |
| Index decode and cache lookup | 11.04 | 10.74 |
| Input decode | 8.58 | 8.33 |
| Triangle culling | 6.40 | 6.53 |
| Vertex emission | 4.84 | 4.65 |
| Viewport parameters | 1.75 | 1.72 |

Skinning and projection still account for most measured geometry CPU time. In the ordinary build’s instruction profile, the largest single projection symbol fell from 7,565,061 to 7,048,252 instructions. Uncached index decoding fell from 3,232,440 to 3,173,414 in its own symbol. Inlining changes how instructions are attributed to symbols.

| Mobile SPURS operation on 480 confirmation jobs | Median ms |
| --- | --- |
| Full 59,008-byte image copy | 2.448 |
| 640-byte suffix after validated prefix reuse | 0.337 |
| 384-byte pre-IO clear in lazy mode | 0.289 |
| 256-byte lookahead clear in lazy mode | 0.281 |
| Input DMA staging | 1.115 |

The cold image copy is the largest measured SPURS memory operation. A worker with a validated reusable prefix copies only the suffix, making input DMA staging the largest of the replayed operations in that case. The full and suffix copies are mutually exclusive. Worker history, queue contention, scheduling and frame presentation are outside these traces. The replay cannot give live reuse frequency, device CPU utilization or game FPS.

## Experiments

Each trial used the last retained parent shown in the table. A variant advanced when outputs matched and instructions decreased by more than 0.1% on both 480-job sets. Geometry compared admission and CRC32 over every byte of local store per capture. Staging compared actual bytes and the end cursor under both clear modes. The joined implementation then underwent timing and full-archive verification.

| Trial | Parent | Change tested | Selection | Confirmation | Retained |
| --- | --- | --- | --- | --- | --- |
| r01 | base | Use NEON finite FP comparisons for admitted clip planes | +3.429% | +3.687% | no |
| r02 | base | Convert bounded projected coordinates directly with saturating FCVTZS | -0.547% | -0.594% | yes |
| r03 | r02 | Canonicalize finite XYZ zeros only after multiplying by the reciprocal | -0.383% | -0.412% | yes |
| r04 | r03 | Refine the normal finite reciprocal with two NEON fused operations | -0.304% | -0.330% | yes |
| r05 | r04 | Reduce maximum projected XY magnitude directly with UMAXV | -0.012% | -0.014% | no |
| r06 | r04 | Accumulate separate X and Y half-vector bounds | +0.000% | +0.000% | no |
| r07 | r04 | Store clip outputs as explicit NEON register pairs | +0.001% | +0.002% | no |
| r08 | r04 | Insert W flag as the top bit with NEON SLI | -0.299% | -0.312% | yes |
| r09 | r08 | Predict empty descriptors in eight selected slots | +0.000% | +0.000% | no |
| r10 | r08 | Advance by a single aligned length after variable DMA copies | +1.721% | +1.823% | no |
| r11 | r08 | Inline the 16-byte trailer in DMA slot 20 | -0.037% | -0.007% | no |
| r12 | r08 | Copy 32-byte slot-2 payloads without the shared size switch | +0.048% | +0.069% | no |
| r13 | r08 | Specialize short 48-byte material and vertex input slots | +0.055% | +0.074% | no |
| r14 | r08 | Extend fixed piece copies to aligned 272 through 496 byte inputs | -1.200% | -1.445% | yes |
| r15 | r14 | Split aligned tails of large DMA copies into 64, 32 and 16 bytes | +1.919% | +2.541% | no |
| r16 | r14 | Dispatch 80, 96 and 112 byte DMA copies in the shared switch | +0.388% | +0.387% | no |
| r17 | r14 | Check the DMA range with a subtraction-based bound | +0.157% | +0.183% | no |
| r18 | r14 | Use a single optional half-block after 1 KiB chunks | -0.192% | -0.178% | yes |
| r19 | r18 | Form the FREST exponent directly for proven finite nonzero W | +0.053% | +0.052% | no |
| r20 | r18 | Use a fused integer subtract-product and insert the fraction via SRI | -0.367% | -0.391% | yes |
| r21 | r20 | Validate a whole reference flag byte at refill | -0.516% | -0.535% | yes |
| r22 | r21 | Inline the eight-lane delta decoder into the batch loop | -0.448% | -0.468% | yes |
| r23 | r22 | Give final short delta loads a cold branch hint | -0.001% | -0.000% | no |
| r24 | r22 | Use the admitted source span for safe 16-byte delta loads | +0.265% | +0.277% | no |
| r25 | r22 | Advance a byte pointer when refilling edge-reference flags | -0.091% | -0.094% | no |
| r26 | r22 | Prefetch the declared index payload before fingerprinting | +0.006% | +0.006% | no |

The bounded projection keeps the PS3 reciprocal estimate and refinement. The archived Ghidra listing of guest image 41 shows the FREST, FI and FNMS operations at 0x6574, 0x6590 and 0x6490 respectively, and CFLTS with three fractional bits at 0x6568 and 0x6570. The ARM implementation uses the admitted finite range to eliminate redundant zero repair and clipping before saturating conversion. Packed estimate interpolation preserves the seed’s sign and exponent. The checked path handles jobs outside the admitted range.

Index decoding validates the flag span on each byte refill and inlines the eight-delta block. DMA staging copies aligned lengths from 272 through 496 bytes in fixed pieces and consumes at most one 512-byte piece after the 1 KiB loop. Predictions tied to specific descriptor slots and extra small-copy cases failed the two-set threshold.

## Correctness and build

All 19,508 captures produced the same admission result and all 262,144 bytes of local store. Both streams contain 5,113,924,660 bytes with SHA-256 `7db32f9991d7e020f22acbe83be5cf7e40ad49b925453f17c580b6e506d83a24`. Both builds admitted 18,487 jobs. The staging replay also matched all local-store bytes and the final cursor on all 19,508 descriptor lists in both clear modes.

An independent build from the checkout reproduced the 600 holdout admissions and local-store checksums, executing 27,656,261 instructions. The PS3 index reference matched 19,745,661 indices. Its malformed-stream checks include truncated spans and a late invalid reference. The projection tests cover all 8,388,608 mantissas, 30,000 general projection blocks and 2,832 bounded blocks with signed zero and fixed-point saturation. Guard tests cover all 32,768 possible DMA lengths and 7,200 additional staging boundary cases. Desktop projection, original vertex/skin references and the sampled coarse clipping oracle also passed.

The single debug APK is stored locally in `analysis/android-mobile-cpu/eu100-geometry-spurs-after-29c8a85/Buzz-EU100-mobile-cpu-debug.apk`. Its SHA-256 is `48f2c81b72fae795e4609b397855487456086e64ff97ebc209639cc0a9437524` and its AArch64 library build ID is `4873fe39f92fc1c96aa27b442753200bbc6650f4`. Verification covered ZIP integrity, 16 KiB alignment, v2 signing and all 11 JNI/SDL exports. The adjacent `replay` directory retains input manifests, each trial’s parent-relative patch, build and replay harnesses, instruction counts, per-pass CPU timings and correctness logs.
