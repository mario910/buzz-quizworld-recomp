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
| Host FLOAT3 producer | Shared temporal key selection; BE float3 keys decoded once into eight host lanes; two original fused roundings retained; all 24,548 real vectors match; 76,050 calls, 21,052 nonempty |
| FLOAT3 numeric/bounds proof | Each arm: 2,048 original-producer batches / 9,216 vectors; masks 0-128 bits, offsets/tails 1-8, random/extended-range FP, read-only streams at guard pages, output canaries; eight invalid recipes rejected |
| Uncovered key formats | `8688`, `7790`, `9608`: zero calls in all 17,714 jobs; observed only, no native implementation |
| Host pose/matrix chain | Q/T/S decoded once into eight host lanes; native world 4x4 plus reference 3x4 composition matches all 461,853 matrices in both streams; no captured world-matrix input in this chain |
| Pose numeric/tail proof | Each arm: 2,048 pinned original-producer batches / 9,216 matrices, random and extended-range FP; read-only source at guard page, tails 1-8, host/output canaries, exact BE row-major emission |
| Host pose blend | Shared quaternion interpolation plus eight-lane translation/scale FMA/FNMS; all 461,853 poses match; 541 strengths, 17,714 calls; all inputs have implicit full joint weights |
| Blend numeric/bounds proof | Each arm: 2,048 original-producer batches / 9,216 poses; endpoints, near/opposite quaternions, random/extended-range translation and scale, guarded read-only tails 1-8, output canaries; fixed RTZ weight normalization checked against original arithmetic |
| Native job metadata frontend | Read-only job256 parser: four input spans, two caches, ES01 skeleton/hierarchy, root pose, clip records and control handles; all 17,714 entries accepted; 17,714 operation-0 and 2,597 operation-1 nodes, 38,025 clip references |
| Publication recipe proof | All 39,112 ordered PUTs predicted from entry metadata: LSA, EA, byte count and DMA tag match the original in both arms, including 3,684 optional curve buffers |
| Metadata rejection proof | Each arm: 31 malformed family/span/control/clip/skeleton/publication cases rejected; admission output remains unchanged |
| Operation-1 pose producer | Covered full-strength flags-0/full-weight recipe; host quaternion composition plus shared interpolation; all 184,387 poses across 2,597 calls match |
| Scalar curve producers | Operation 0: 3,684 calls / 85,296 aligned words; operation 1: 2,597 calls / 62,328 aligned words; all match, including published tail words and flag-1 clamping |
| Additional numeric proofs | Each arm: 2,048 original-producer batches / 9,216 logical samples per operation; scalar checks include 12,288 aligned words per operation; read-only guarded source tails, host canaries, same/opposite quaternions and extended FP pass |
| Host FLOAT1 key sampler | Shared temporal selection and exact eight-lane interpolation; 38,025 calls (3,863 nonempty), 52,640 final channel owners match, including published alignment channels; repeated IDs publish only their final owner |
| FLOAT1 numeric/bounds proof | Each arm: 2,048 original-producer batches / 10,752 projected results; stored four-slot groups, guarded inputs/tails, duplicate IDs, offsets and masks through 128 bits, extended FP, output canaries; 12 invalid recipes/mappings rejected; compressed alternative has zero real calls and remains unimplemented |
| Native clip stream frontend | All 38,025 EA03 header/block pairs decoded into bounded static/animated Q/T/S/curve recipes and ID views; 60,605 nonempty sampler recipes match the original; native samplers now consume these views |
| Clip bounds proof | Each arm: 30 malformed span/count/offset/ID cases rejected with unchanged admission output; read-only guard-page checks with and without trailing input pass |
| Observable trailing input | 648 clips require 4-156 bytes beyond the fetched block for published curve padding; all match entry LS (zero in this corpus); explicit bounded input, no scratch-state reconstruction or zero assumption |
| Native time/block frontend | All 38,025 clips: selected block, local frame/fraction and body GET EA/LSA/size match; native time outputs feed stream sampling; real selected blocks 0-23 |
| Time numeric/bounds proof | Each arm: 4,096 pinned-original batches / 18,432 samples, eight-lane tails, block boundaries, signed zero, denormals, negative and past-end clocks; read-only guarded headers and output canaries pass; 36 invalid clocks/tables/GET spans rejected unchanged |
| Complete host clip | ES01 default pose decoded once/job; native static and sampled streams produce all 38,025 clips / 1,108,093 poses / 232,920 aligned curve words exactly; static T: 195,198 vectors, static curves: 153,941 words; static scale count is zero |
| Full-clip replacement | Third same-EXE `ClipReplace` arm skips the entire original sampling flow: 38,025 replacements, zero original key-producer calls; all 17,714 jobs / 115,162 transfers / exit controls match, with native blend/hierarchy/matrix producers retained |
| Default/clip bounds proof | Seven malformed ES01 headers rejected unchanged; default tails 1-8 at read-only guard pages preserve unused host slots; full clip uses guarded header/block/static T/static curve inputs and host canaries; invalid curve capacity/padding rejected |
| Host curve padding ownership | 2,597 third-leaf buffer reuses inherit produced second-leaf tail words in host arrays; first use reads explicit entry input, without a zero assumption or scratch reconstruction |
| Complete host output chain | Control blend/add -> hierarchy -> world/reference matrices remains LE SoA until final BE emission; all 39,112 publications / 52,068,720 bytes match across 17,714 jobs, with no captured intermediate matrix/pose inputs |
| Direct host publication | Fourth same-EXE `PipelinePublish` arm supplies every published byte from the host chain; original scheduling/channel scaffold retained; all corpus outputs and exit controls match |
| Control/output bounds | Eighteen malformed graphs/strengths/clamp spans rejected unchanged; guarded skeleton/reference inputs, unused control/world slots and final output canaries pass |
| Full channel boundary proof | Each arm: separate unmodified original replay per capture; all 1,766,060 channel write/read/count events match by direction/channel/raw 32-bit value/order; per-job counts/digests archived |
| Native whole-job driver | Fifth same-EXE `NativeJob` arm executes preparation, all GET/wait/PUT channels and return without lifted candidate blocks; all 17,714 jobs / 115,162 transfers / 52,068,720 bytes / 1,766,060 channel events and exit controls match |
| Pre-mutation fallback | Twenty-six unsupported/malformed/missing-input cases reject with unchanged input LS, channel position and output plan; enabled interrupt entries remain unsupported; all real corpus jobs admitted without exclusions |
| Whole-job producer replacement | Same EXE Compare/Replace: each 17,714 exact jobs / 115,162 ordered transfers and captured exit controls. Native arm replaces 38,025 animated quaternion, 38,025 static quaternion, 76,050 FLOAT3 and 17,714 calls each of pose blend, hierarchy, world-matrix conversion and reference composition; also 2,597 additive-pose, 3,684 scalar-blend, 2,597 scalar-add and 38,025 FLOAT1 sampler calls; no volatile GPR or ABI reconstruction |
| Host hierarchy | All 461,853 world poses match; 790,384 recorded entries canonicalized to unique joints; 167,608 dependency-safe batches, 14,979 full eight-lane batches; AVX2 masked SoA gathers and neutral inactive lanes |
| Hierarchy numeric/admission proof | Each arm: 2,048 original real-producer batches / 9,216 world poses, guarded read-only tails 1-8, non-unit root scales and both inheritance flags, extended-range translation/scale, output canaries; ten invalid/uncovered schedules rejected; empty plan touches no poses |
| Quaternion bounds/numerics | Each arm: 2,048 original-producer batches / 9,216 quaternion samples, masks through 128 bits, offsets/tails 1–8, read-only inputs ending at a guard page; six invalid recipes rejected; empty tracks touch no data |
| Publication fault probes | Thirteen output-corruption probes and two DMA-command-flag probes are rejected at the job boundary, including direct native whole-job output/command corruption |

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
host key producers, pose blending and the Q/T/S-to-world/reference matrix chain pass
whole-job I/O with their original producers replaced in the test oracle.
Hierarchy and the matrix chain now use native host pose arrays, without
captured matrix inputs. Nonuniform joint-weight streams and the small hierarchy
alternative have no real corpus coverage and remain unsupported. Production
job integration, live checks and candidate FPS/CPU gates remain. The new
metadata parser is not the complete native driver: original code still
is used only by the earlier observation/replacement arms. `NativeJob` now
executes the complete recognized job with native scheduling, host data and
final outputs; paired original replays validate all bytes, channel events
and exit controls. Runtime dispatch/guest-memory integration and its
live/frame/FPS/CPU gates remain; enabled interrupt entries fall back. FLOAT1 key sampling and operation-1
pose/curve arithmetic now use native producers in the replacement oracle.
There is no new runtime path, default change or FPS/CPU gain claim yet.
RIP shares are diagnostic estimates; helper time is outside image symbols.

Private evidence: `analysis/native-geometry/phase4/`.
