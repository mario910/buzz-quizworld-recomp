# Sequential Android CPU qualification from 1331762

The EU 1.00 mobile profile went through 24 sequential rounds. Twenty passed the promotion criterion and four were dropped. Each candidate started from the latest accepted version. A round completed its selection replay, confirmation replay, output checks and decision before the following round started. The APK was assembled once after integration.

## Integrated results

These are dynamic ARM64 instructions counted under QEMU user mode, using Android NDK r30 Clang, Bionic, RTZ/FTZ and fixed FP contraction settings. Geometry markers enclose the production preparation and commit functions, including output of indices, visible vertices and updated manager fields. Admission runs inside the markers. Saved-job inflation, CRC and full-store comparisons run outside. Lifted fallback execution, later command publication, the renderer and the GPU are outside the measurement. SPURS markers cover only the 21-entry input DMA staging loop.

| Scope and captures | 1331762 | Integrated | Change |
| --- | ---: | ---: | ---: |
| Geometry · selection, 1,200 | 100,405,363 | 76,789,246 | -23.52% |
| Geometry · confirmation, 1,200 | 110,503,480 | 89,060,134 | -19.41% |
| Geometry · independent check, 600 | 51,205,119 | 33,886,464 | -33.82% |
| Geometry · complete corpus, 19,508 | 1,532,468,611 | 1,029,902,153 | -32.79% |
| SPURS staging · selection, 1,200 | 1,311,688 | 1,102,167 | -15.97% |
| SPURS staging · confirmation, 1,200 | 1,359,956 | 1,144,080 | -15.87% |
| SPURS staging · independent check, 600 | 731,047 | 613,559 | -16.07% |
| SPURS staging · complete corpus, 19,508 | 23,418,713 | 19,664,162 | -16.03% |

The selection and confirmation sets contain other captures from the same title and overlapping sessions. The 600-capture check was excluded from every promotion decision. Each of its decompressed captures is distinct from the two selection sets. The complete corpus includes the smaller samples. Dataset composition matters here because culled rigid objects benefit more from deferred work.

### CPU time under emulation

Two persistent Android binaries processed the same 600 captures on the same pinned x86 host CPU under QEMU. Each executed two warmup passes and ten measured passes, alternating which binary ran first. The timers sampled thread CPU time only around job preparation and output. Idle-host medians were **130.15 ms** for the previous profile and **103.93 ms** for the candidate per 600 jobs. The median of the ten paired changes was **−20.35%** (range −22.74% to −18.80%). An earlier series of fresh processes overlapped compilation and produced much wider variation. Both logs are in the replay bundle. This test measures emulator execution on the host. Sustained phone frame times and FPS remain unmeasured.

## Round journal

For geometry, both sets had to retain every admission and match each post-job local-store CRC. For staging, the harness checked both padding modes, the complete local store and the final cursor. Each promotion required over 0.1% fewer instructions on both samples. During rounds 21–24, a footprint tradeoff was also allowed for a reduction over 35% in code size with under 1% more instructions on both sets. All retained changes cleared the instruction threshold directly. Parent `base` means `1331762`. The joined sources were then measured again after integrating shared code and Android profile switches.

| Round | Parent | Change tested | Selection instructions | Confirmation instructions | Decision |
| ---: | --- | --- | ---: | ---: | --- |
| 1 | base | Defer non-position fields until rigid-job visibility is known | 100,405,363 → 90,303,684 (-10.06%) | 110,503,480 → 102,754,289 (-7.01%) | Keep |
| 2 | r01 | Validate deferred directions with one selector byte per vertex | 90,303,684 → 90,005,873 (-0.33%) | 102,754,289 → 102,401,267 (-0.34%) | Keep |
| 3 | r02 | Decode the short XYZ tail with zero-filled interleaved loads | 90,005,873 → 89,981,630 (-0.03%) | 102,401,267 → 102,378,138 (-0.02%) | Drop |
| 4 | r02 | Serialize full position-only blocks with two interleaved NEON stores | 90,005,873 → 89,464,691 (-0.60%) | 102,401,267 → 101,611,275 (-0.77%) | Keep |
| 5 | r04 | Use fused scale/bias and fixed-point conversion for bounded output normals | 89,464,691 → 87,346,327 (-2.37%) | 101,611,275 → 99,024,933 (-2.55%) | Keep |
| 6 | r05 | Pack sixteen-byte position and normal records directly with ST4 | 87,346,327 → 86,392,358 (-1.09%) | 99,024,933 → 97,934,808 (-1.10%) | Keep |
| 7 | r06 | Canonicalize signed zero at the end of each proved-finite position dot product | 86,392,358 → 84,954,986 (-1.66%) | 97,934,808 → 96,456,864 (-1.51%) | Keep |
| 8 | r07 | Reduce the three object direction bounds in NEON before normalization | 84,954,986 → 84,858,064 (-0.11%) | 96,456,864 → 96,344,032 (-0.12%) | Keep |
| 9 | r08 | Deinterleave contiguous two-half input fields with LD2 while retaining their guest prefix | 84,858,064 → 84,841,316 (-0.02%) | 96,344,032 → 96,353,960 (+0.01%) | Drop |
| 10 | r08 | Specialize the two frequent strided half-pair layouts outside the vertex loop | 84,858,064 → 83,664,730 (-1.41%) | 96,344,032 → 94,899,067 (-1.50%) | Keep |
| 11 | r10 | Transform each admitted object attribute in one contiguous pass | 83,664,730 → 82,712,472 (-1.14%) | 94,899,067 → 93,868,984 (-1.09%) | Keep |
| 12 | r11 | Try the existing whole-job coarse-clip proof together with deferred attributes | 82,712,472 → 77,928,297 (-5.78%) | 93,868,984 → 90,629,946 (-3.45%) | Keep |
| 13 | r12 | Use the existing proved-finite palette path for weighted skinning | 77,928,297 → 77,067,774 (-1.10%) | 90,629,946 → 89,395,113 (-1.36%) | Keep |
| 14 | r13 | Reduce each conservative bounding-box axis in NEON registers | 77,067,774 → 76,924,456 (-0.19%) | 89,395,113 → 89,243,472 (-0.17%) | Keep |
| 15 | r14 | Check palette indices before widening the eight packed bone numbers | 76,924,456 → 76,790,620 (-0.17%) | 89,243,472 → 89,061,908 (-0.20%) | Keep |
| 16 | r15 | Read a contiguous SPURS descriptor in one word with the SDK wrap fallback | 1,311,688 → 1,311,688 (+0.00%) | 1,359,956 → 1,359,956 (+0.00%) | Drop |
| 17 | r15 | Advance the DMA cursor directly when the source has no padding | 1,311,688 → 1,299,399 (-0.94%) | 1,359,956 → 1,347,471 (-0.92%) | Keep |
| 18 | r17 | Assemble uncommon short aligned DMA copies from constant-size pieces | 1,299,399 → 1,291,753 (-0.59%) | 1,347,471 → 1,340,006 (-0.55%) | Keep |
| 19 | r18 | Copy long geometry DMA sources in unrolled 128-byte blocks | 1,291,753 → 1,218,046 (-5.71%) | 1,340,006 → 1,262,295 (-5.80%) | Keep |
| 20 | r19 | Specialize the three stable viewport, transform and configuration slots with length guards | 1,218,046 → 1,172,904 (-3.71%) | 1,262,295 → 1,217,553 (-3.54%) | Keep |
| 21 | r20 | Share the uncommon-length copy code across the unrolled DMA slots | 1,172,904 → 1,126,621 (-3.95%) | 1,217,553 → 1,169,205 (-3.97%) | Keep |
| 22 | r21 | Use seven-slot groups to bound the instruction footprint of the SPURS loop | 1,126,621 → 1,175,289 (+4.32%) | 1,169,205 → 1,218,112 (+4.18%) | Drop |
| 23 | r21 | Share the whole size dispatcher while keeping the three fixed slots inline | 1,126,621 → 1,118,526 (-0.72%) | 1,169,205 → 1,160,911 (-0.71%) | Keep |
| 24 | r23 | Test the observed sizes of the optional command and mesh descriptor slots | 1,118,526 → 1,102,020 (-1.48%) | 1,160,911 → 1,143,922 (-1.46%) | Keep |

Rounds 1–15 targeted geometry. Deferred attributes saved the most on the initial two sets. Skipped fields still validate their packed selectors, including when every triangle disappears. The clip proof can reject a whole offscreen object before projecting each vertex. Palette and position bounds preserve the exact fallback for exceptional guest values. The output writers use the NEON layout only when the complete record and destination alignment match.

Rounds 16–24 targeted SPURS. The audited optional-slot lengths are live checks on every list. Unusual lengths take the general copy path. The fully unrolled experiment occupied 16,080 bytes of benchmark code in round 20. Outlining shared copies and adding guarded slot-specific branches reduced the selected round to 3,816 bytes. The tidied shipping benchmark occupied 3,816 bytes including both outlined helpers. The equivalent previous staging function occupied 1,880 bytes. This is a local code-size tradeoff for fewer executed instructions. The SPURS count does not include scheduling or synchronization elsewhere in the chain.

## PS3 reference and correctness

Ghidra decompiled the original `6eba` SPU image at `0x54e8` and `0x9114`. The vertex writer branches at `0x5528` and calls `0x2728` at `0x5558`. The vertex oracle executes the lifted guest writer starting at `0x54e8` and compares the bytes it publishes, including alignment tails.

- On the complete corpus, the normal production entry point accepted 18,487 jobs in each binary. Comparing admission plus the full 256 KiB local store for every capture matched all **5,113,924,660 bytes**. SHA-256 of the decompressed stream is `7db32f9991d7e020f22acbe83be5cf7e40ad49b925453f17c580b6e506d83a24`.
- The separate stage-level PS3 oracle compared 19,399 supported index jobs, all 5,280,336 index bytes and 24,406,612 clip values. The direct vertex oracle matched 30,927,600 bytes from 6,535 jobs and 320,256 vertex bytes across 96 additional FP cases. Stage-level and whole-job admission exercise different gates.
- The bounds test compared 1,600,032 scalar position components. The coarse-clip test reported zero false rejections on the selection set and rejected all 32 offscreen invalid-index mutations. Generic desktop position and lazy-attribute tests also passed with mobile switches disabled.
- The stage replay checked 409,668 descriptors from all 19,508 jobs, including 164,673 nonempty entries. Another 7,200 boundary cases covered cursor and local-store behavior. Source and destination guard pages checked all 32,768 representable copy lengths independently.

## Android package and reproduction

`mobile-cpu` enables lazy attributes, coarse clipping and bounded skin. `reference29` keeps those three settings off. The integrated APK passed v2 signature verification with the same debug certificate, 16 KiB ZIP alignment and all 11 JNI/SDL exports. Realignment retained the contents and compression of every ZIP member. The packaged and unstripped libraries have matching build IDs.

Local evidence, including the APK, symbols, Ghidra output and the replay instructions, is in `analysis/android-mobile-cpu/eu100-sequential-after-1331762`. The portable harness was rebuilt from the archived sources and reproduced the 600-job output checks and the selection staging counts. Fresh linking moved the 600-job geometry result by under 0.01%. APK SHA-256 is `84468acfa0cd16b98c51f19b2b34831b0497f3c22cb3a583c317467be8544117`. ELF build ID is `19e2916eef0c81b5002045de182fb3613ea260d4`.
