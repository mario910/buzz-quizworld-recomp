# Phase 4: shared animation jobs

| Check | Result |
| --- | --- |
| Existing image-40 corpus | All 17,714 real captures inspected; seven input-size shapes; one descriptor layout: 256 bytes, four inputs, two caches, 160 user bytes |
| Published buffers | All jobs: input-2 length / 48 records, followed by two PUTs of 48 and 64 bytes per record; 3,489 jobs add 96 bytes, 195 add 32 bytes |
| Ordered I/O | 326 transfer-size sequences; retain runtime GETs and all PUTs in recorded order |
| Baseline whole-job replay | 17,714 exact jobs / 115,162 transfers; zero differences or concurrent-input exclusions; exit 0 |
| Frozen baseline | `build-vulkan-host-pipeline-default-v1`, Vulkan, EXE `2b0c34e6`; existing image-41 host profile preserved |
| Fresh menu diagnostic | 20 s RIP sample, four selected SPURS lanes, 41,664 active-interval samples; 1,117 exclusive image-40 samples; native menu frame inspected; mailbox and asynchronous batches confirmed |
| Hottest image-40 blocks | `6DB8_reference`: 233 samples (20.859% of image 40); `3460`: 114 (10.206%); `52A0`: 107 (9.579%) |
| Profiler validation | Five tests pass; existing image-41 report reproduces exactly; image-specific reports exclude other roles |
| Host affine producer | Eight joints per AVX2 batch; shared geometry BE/SoA decoder and exact FP helpers; final 3x4 stream written once; no SPU register bank or ABI frame |
| Captured-output proof | All 17,714 jobs: 461,853 matrices / 22,168,944 bytes match the first PUT exactly, using captured world matrices and staged reference matrices as inputs |
| Independent arithmetic/tails | 4,096 numeric batches / 18,432 matrices match the pinned original seven-block producer, including SPU extended-range values; guarded input/output tails 1–8 pass; inputs unchanged |
| Host quaternion producer | Bitset predecessor/successor selection with host popcount/bit scans, packed 6-byte keys, exact eight-lane decode and shortest-path interpolation; all 596,095 real quaternion results match the original |
| Static quaternion producer | Same eight-lane 6-byte decoder; all 388,032 static results match the original; 2,048 original-producer numeric batches / 9,216 static samples and guarded tails pass; truncated/empty stream checks pass |
| Whole-job producer replacement | Same oracle EXE, original-comparison and native-replacement arms each pass 17,714 exact jobs / 115,162 ordered transfers and captured exit controls; native arm replaces 38,025 animated and 38,025 static quaternion calls without restoring volatile registers or SPU frames |
| Quaternion bounds/numerics | Each arm: 2,048 original-producer batches / 9,216 quaternion samples, masks through 128 bits, offsets/tails 1–8, read-only inputs ending at a guard page; six invalid recipes rejected; empty tracks touch no data |
| Publication fault probe | Injected native quaternion sign change fails the first job's PUT byte comparison, as expected |

Code-only shared candidates retain register numbers, immediates and data
addresses; only code symbols/PCs are normalized:

| Image 40 | Image 43 | Image 45 | Image 47 |
| --- | --- | --- | --- |
| `6DB8` | `6F38`, exact | `5AE0`, similar only | `6E20`, exact |
| `3460` | `35E0`, exact | `2E90`, exact | `34C8`, exact |
| `52A0` | `5420`, exact | No exact candidate | `5308`, exact |

**Decision:** image 40 is the animation frontend, with a different job layout
from image 41. Establish logical clip/pose/matrix inputs and reuse host
algorithms; do not transplant the image-41 job descriptor or generate new
register-bank transliterations. Code matches do not establish job-contract
compatibility or live coverage for images 43/45/47. Phase 4 remains open;
host affine composition and quaternion sampling are implemented. The latter
also passes whole-job I/O with the original producer replaced in the test
oracle. The affine proof still consumes captured world matrices; remaining
pose/hierarchy/world production, production job integration, live checks and
candidate FPS/CPU gates remain. There is no
new runtime path, default change or FPS/CPU gain claim yet.
RIP shares are diagnostic estimates; helper time is outside image symbols.

Private evidence: `analysis/native-geometry/phase4/`.
