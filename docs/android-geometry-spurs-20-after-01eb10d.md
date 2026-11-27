# Android mobile CPU qualification after 01eb10d

The baseline is EU 1.00 on `perf/android-mobile-cpu` at `01eb10d`. All numbers below count executed AArch64 instructions including callees under QEMU user mode. The binaries use Android NDK r30 Clang, Bionic, RTZ/FTZ and the same FP contraction flags in both arms. They measure CPU work. Device frame time, GPU time, thermals and Android scheduling require a phone.

| Workload and captures | Baseline instructions | Shipping instructions | Change |
| --- | ---: | ---: | ---: |
| Geometry selection · 1,200 | 92,816,267 | 88,449,045 | -4.71% |
| Geometry validation · 1,200 | 99,305,633 | 94,932,578 | -4.40% |
| Geometry fresh holdout · 600 | 52,123,650 | 49,500,666 | -5.03% |
| Production preparation and both output writers · 600 | 53,776,832 | 51,208,007 | -4.78% |
| SPURS input staging selection · 1,200 | 1,527,898 | 1,311,688 | -14.15% |
| SPURS input staging validation · 1,200 | 1,576,562 | 1,359,956 | -13.74% |
| SPURS input staging fresh holdout · 600 | 839,111 | 731,047 | -12.88% |

The geometry replay includes input preparation, skinning, projection, cached index decoding, culling and final index serialization. File inflation and verification run outside the markers. The separate production replay calls `prepare_geometry_host_job`, commits index data for admitted jobs, and commits vertices for visible draws. It also exercises the production publication-span and vertex-recipe checks. SPURS staging has its own markers. The rows cannot be added or converted to FPS.

A rebuild from the bundled reproduction scripts gave 49,500,985 geometry instructions on the fresh holdout, within 319 of the qualification run, with the same output digest. It reproduced both staging counts exactly. QEMU staging timings changed direction between repeats even with identical counts, so the instruction reductions do not establish an elapsed-time improvement on hardware.

## Twenty isolated iterations

Geometry candidates used the 1,200-job selection set. The second 1,200-job set checked whether the effects transferred. Selection included 800 menu captures and 100 from each of four gameplay sessions. The staging candidates compared old and new implementations in the same process on the same DMA lists. The last column records the decision after combination and correctness testing.

| Round | Candidate | Selection instructions | Selection change | Second set change | Decision |
| ---: | --- | ---: | ---: | ---: | --- |
| 1 | Borrow immutable indices on a cache hit until culling finishes | 92,575,772 | -0.26% | -0.24% | Keep. Also avoids copying indices on ordinary projection. |
| 2 | Validate index codec at the hit or inside the uncached decoder | 92,658,961 | -0.17% | -0.17% | Keep. A miss validates once. A warmed cache still checks all guest tables. |
| 3 | Two cache ways at the same capacity | 93,052,128 | +0.25% | +0.05% | Reject. More instructions on both workloads. |
| 4 | Shorter index fingerprint mixer | 92,811,505 | -0.01% | -0.00% | Leave out. Less than 0.01% on both sets. |
| 5 | Bulk validation of consumed sequential reference flags | 92,470,723 | -0.37% | -0.40% | Enable existing sequential decoder in mobile-cpu. |
| 6 | Seven independent BE word loads and an exact three-byte tail | 92,594,774 | -0.24% | -0.22% | Keep for full groups with strided unit vectors. |
| 7 | Deinterleave a contiguous group of eight 24-bit directions | 92,575,576 | -0.26% | -0.15% | Keep for stride three. Exactly 24 input bytes. |
| 8 | Use an unsigned NEON maximum to validate all eight selectors | 92,106,463 | -0.76% | -0.70% | Keep. Selectors above eleven still reject the job. |
| 9 | Skip inactive-lane clearing on a complete direction group | 92,047,202 | -0.83% | -0.76% | Keep. Partial groups still clear all unused lanes. |
| 10 | Test positive SPU W with one signed integer comparison | 91,842,102 | -1.05% | -1.02% | Keep. Threshold 0x007fffff excludes denormals and signed zero. |
| 11 | Reduce matrix coefficient bounds in NEON | 92,844,606 | +0.03% | +0.03% | Reject. Scalar admission used fewer instructions. |
| 12 | Reduce position bounds with one unsigned maximum | 92,571,468 | -0.26% | -0.25% | Keep. The proved magnitude limit and fallback stay identical. |
| 13 | Compare complete index codec quads as 16-byte blocks | 92,517,781 | -0.32% | -0.30% | Keep. The 512-byte edge table is still checked separately. |
| 14 | Compare complete projection codec quads as 16-byte blocks | 92,680,472 | -0.15% | -0.14% | Keep. Same big-endian patterns as the guest. |
| 15 | Compare complete cull codec quads as 16-byte blocks | 92,661,384 | -0.17% | -0.16% | Keep. Shares the compile-time byte expansion. |
| 16 | Memoize the last fully verified viewport and matrix | 92,708,033 | -0.12% | +0.08% | Reject. Regressed on the second set. |
| 17 | Move the empty I/O admission before the DMA loop | 1,516,809 | -0.73% | — | Retain the early return inside the selected fixed-length path. |
| 18 | Load descriptor size before the source address | 1,529,979 | +0.14% | — | Reject. The extra test increased staging work. |
| 19 | Dispatch image 41 once into its own 21-entry loop | 1,390,458 | -9.00% | — | Keep the specialization as part of round 20. |
| 20 | Unroll the specialized DMA loop by three entries | 1,311,642 | -14.15% | — | Keep. Measured again using the actual generated adapter. |

## Independent replay and guest checks

A third sample contains 600 captures selected after candidate selection with a fixed shuffle seed of 11027. The SHA-256 of each decompressed capture differs from every capture on the first two sets and from all other captures in the third. These are other jobs of the same title and can come from the same sessions. Baseline and shipping profiles produced the same serialized index digest on all three workloads.

| Replay | Jobs admitted by the geometry stages | Ordered index digest |
| --- | ---: | --- |
| combined | 1193 | `dd6ffcbdacfe46a2` |
| held-out | 1194 | `83dada2d078190e5` |
| unseen | 598 | `4bd1971fb9c023d0` |

Running the production entry point on the third sample admitted 575 of 600. Both binaries had the same decision for every job. A byte-for-byte comparison covered one decision byte plus the full 262,144-byte post-call local store for each capture. SHA-256 of the resulting stream was `2638ceec6100ea41f34c8bb69bf7654fc73f1328dabf98a71944fa7906646199`. The stage harness also compared the full local store and end cursor in both padding modes. Another 7,200 boundary cases covered empty descriptors, other job families, partial blocks and the end of local store.

Ghidra 11.0.3 decompiled the original PS3 SPU images with `SPU:BE:32:default`. In `6eba`, the branch at `0xaec4` handles the empty index case, `0xaf0c` calls the decoder at `0x7390`, and `0xaef4` publishes the pointer after the call. We also recovered projection at `0x6188`. In `14b4`, the helper at `0x0a40` writes the MFC address, size and tag for two ordered commands at `0x0aa0` and `0x0ab8`. The host change only specializes initial input staging for the recognized 21-descriptor job. The guest command path retains its ordering.

The original-code oracle completed on 19,508 real entries. It matched 19,399 supported jobs, 24,406,612 clip values and all 5,280,336 serialized index bytes, including 465 nonzero padding bytes. The oracle also checked special FP inputs, admission failures and unchanged guest state. Guard-page tests exercised 54 packed unit-vector shapes, all active-lane counts and the exact end of each source. Cache tests covered 4,103 mutations and 448 valid payload variants sharing fingerprints.

## Android artifact and reproduction

`mobile-cpu` enables `buzzGeometryMobileKernels`, the existing sequential index decoder and direct SPURS staging. The reference profile leaves those switches off. One offline Gradle assemble produced the EU 1.00 arm64 debug APK. After ZIP alignment and signing, every APK member matched the Gradle output byte for byte, including its compression method and compressed size. The checks covered the debug signing certificate, APK Signature Scheme v2, 16 KiB ZIP alignment, matching stripped/unstripped ELF build IDs and all 11 JNI/SDL exports.

The APK, matching symbols, Ghidra extracts, 20 variant definitions, baseline and final harnesses, manifests and logs are local under `analysis/android-mobile-cpu/eu100-geometry-spurs-20-after-01eb10d`. See `replay/README.md` there for reproduction. The APK SHA-256 is `2d27d6399c8fce55fcbea79e6c4e05c12b06586f1c5e995eb028d6df809511c7`. The ELF build ID is `59b2a48e635297ddafd7ca871a1c420e960be34c`.
