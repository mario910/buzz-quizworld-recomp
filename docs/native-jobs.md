# Native geometry and animation jobs

The port replaces recognized SPU jobs with host C++ and AVX2 while retaining
the lifted implementation for unsupported inputs. The qualified EU
1.00/Vulkan/AVX2 build enables these paths inside the executable; it does not
need a launcher to set their environment switches. Other executable families
retain their own hash-bound adapters and defaults.

## Observable contract

A replacement must preserve the ordered DMA, atomic and channel operations,
every published vertex/index/RSX/animation byte, and the job's exit controls.
Published floating-point values retain SPU rounding toward zero, denormal
flush and the original fused versus separately rounded operation order.

Internal SPU registers, scratch LS, dummy/lookahead iterations, prefetches and
SPU-ABI frames need not be reconstructed. Full-state per-function checkers
remain useful for debugging retained kernels; they must not dictate the host
pipeline's representation. Strict dead-register reconstruction is not planned.

Admission happens before observable mutation. An unsupported descriptor takes
the entire lifted job; it must not execute a native prefix and then restart
the original job after DMA or publication has already occurred.

## Image 41: geometry

`port/native_geometry_job_flow.cpp` owns the native job driver;
`port/native_geometry_host_job.h` composes its host data path. Inputs remain
big-endian guest bytes until bounded decoders produce little-endian host SoA.
Each AVX2 lane represents one vertex or triangle.

| Step | Implementation | Behavior |
| --- | --- | --- |
| Input admission and decode | `native_geometry_host_input_job.h` | Resolves descriptors and staged streams once; invalidates partial readiness on failure |
| Skin/object transform | `native_geometry_host_skin_job.h` | Consumes host attributes and palettes directly, preserving SPU FP order |
| Clip coordinates | `native_geometry_host_clip_job.h` | Produces host coordinates, clip flags and packed bounds; supports admitted clip formats 0/3 |
| Index decode | `native_geometry_host_index_job.h` | Decodes deltas and flag-selected references into host u16 triangle arrays |
| Cull | `native_geometry_host_cull_job.h` | Evaluates triangles in eight-lane batches and compacts retained indices |
| Index serialization | `native_geometry_host_index_output.h` | Writes final aligned indices, including observable padding |
| Vertex serialization | `native_geometry_host_vertex_job.h` | Writes admitted float, Edge-half, CMP and SNORM16 fields at byte offsets, including odd record strides |
| Publication and commands | `native_geometry_job_flow.cpp` | Publishes final streams in the job's required order and prepares RSX commands |

The host path retains persistent arrays between stages rather than serializing
intermediate float4 tables through LS. Each final output stream is written in
the format and location from which the job publishes it. Padding is observable:
some positions require one additional transformed input, and aligned index
tails can repeat bytes from the final retained group.

Known whole-job fallback families include out-of-palette indices in that
padding position, additional blend streams and input storage 4. A discarded
padding value is not permission to read a different palette entry. Unknown
descriptors and invalid spans also retain the lift.

## Image 39: object visibility and descriptor requests

Image 39 (C0BC) handles object visibility and descriptor requests.
The optional checked Image 39 FP adapter replaces arithmetic helpers while
retaining the instruction flow, register bank and I/O. It is not a native
whole-job driver. The arithmetic and descriptor paths below are independent.

`BUZZ_IMAGE39_NATIVE_BOUNDS` is an experimental, default-OFF replacement for
the pinned EU 1.00 `0x05C0`/`0x0750` object-visibility loop. Its
[host implementation](../port/native_image39_bounds.cpp) reads object bounds
and matrices, transforms the center and projects each extent against five
planes. SIMD lanes hold four planes, followed by the remaining plane. Once
one group proves the object outside, the other group is unnecessary.
This replaces SPU register traffic, byte shuffles, software pipelining and
dead prefetches with a direct bounds algorithm.

Checked SSE/FMA or NEON arithmetic retains ordered round-toward-zero/flush
operations and integer repair of exceptional values. The function saves and
restores the caller's FP control and sticky flags. It does not depend on the
whole-image `SPURS_NATIVE_IMAGE39_FP` mode, which can remain 0.

Admission requires the hash-pinned EU100 wrapper, the known `0x2178` caller,
private LS, 1..4096 items, known shuffle constants and bounded aligned input
records. A preflight rejects output overlapping pointer tables, input records
or constants before any mutation. Interrupt/event/policy contexts, LS hooks
and active native-request diagnostics keep the entire retained function.
There is no cross-call input cache, allocation or fabricated completion.

The live result is full LS, the complete r3 return, preserved registers and
the return PC. The preferred r3 word is the retained count; its other words
still contain the number of tested objects. Every candidate writes its index
at the current output slot even when rejected, so the final rejected tail
word must remain observable. Scratch r2..r79 except r3 are not reconstructed.
Stage F paired mode 2 checks this contract while retaining the original
result. Full-register strict mode deliberately does not describe this ABI.

The standalone bounds suite uses separately compiled, unchanged functions
extracted from the original hash-pinned lift. It first reproduces captured
full-register/LS returns, then compares native live state and all LS bytes.
Synthetic coverage includes ordinary/extended values, culling boundaries,
4095/4096-item lists, all output word alignments, four incoming FP modes and
fallback without mutation. Windows, Linux x86 and ARM64/QEMU correctness
does not establish a game FPS or Android hardware gain. Native gameplay
progression, visible output and a matched locked-session comparison remain
required before enabling a default.

`BUZZ_IMAGE39_NATIVE_REQUEST` is a separate experiment, OFF by default. The
pinned EU 1.00 generator can route `0x3A10` descriptor requests through
`native_image39_request.h`. It plans the cache lookup, replacement, output
record and optional GET using local host values, then publishes in the
original order. Dead ABI spills and dispatch between the request's blocks are
omitted. The actual MFC command still performs validation, DMA, coherency,
wakeups and tag completion; it is never replaced by a fabricated completion.
The arithmetic `0x05C0` kernel and the rest of image39 remain separate paths.

The request admits only audited caller return addresses, private LS with
bounded disjoint descriptor/table/output spans, a nonempty cache and no
enabled interrupts. Empty caches, aliases, invalid arguments, LS hooks and
active drain/channel diagnostics retain the whole original request before any
mutation. The result is r3 plus the published cache/output records and ordered
DMA, rather than dead scratch registers or frame bytes. `BUZZ_IMAGE39_REQUEST=0`
rolls back an enabled build. `BUZZ_IMAGE39_REQUEST_STATS=1` enables diagnostic
admission counters; leave it unset during FPS/CPU comparisons.

This I/O path must use whole-job checking. Stage F's paired leaf runner must
not execute a DMA request twice. The standalone request test also compares
captured entry/exit states with an independently extracted, hash-pinned
retained oracle and models each observed GET payload. ARM64/QEMU verifies
correctness, not device performance. Other game revisions are not admitted.

## Image 40: animation

`port/native_animation_runtime.cpp` owns dispatch and the reusable worker-local
workspace. `port/native_animation_job.h` prepares and executes the recognized
256-byte-descriptor family without an SPU register bank or call frames.

| Step | Implementation | Behavior |
| --- | --- | --- |
| Job metadata | `native_animation_job_input.h` | Resolves control nodes, clip references, skeleton, root pose and output destinations |
| Clip selection | `native_animation_host_clip.h` | Selects bounded header/body data and combines defaults with static and sampled channels |
| Quaternion keys | `native_animation_host_quaternions.h` | Decodes six-byte keys, selects temporal neighbors and interpolates with original hemisphere/FP rules |
| Translation/scale keys | `native_animation_host_vectors.h` | Decodes BE float3 tracks into eight-lane host arrays |
| Control/blending | `native_animation_host_pipeline.h`, `native_animation_host_blend.h` | Evaluates the admitted graph and keeps poses/curves in host arrays |
| Hierarchy | `native_animation_host_hierarchy.h` | Builds dependency-ready joint batches, with parent scale and inheritance flags |
| Matrices and output | `native_animation_host_pose.h`, `native_animation_job.h` | Produces final reference/world/curve streams once, then emits the required job I/O |

The recognized graph has one/two control nodes and two/three clips. Host
preparation validates the immutable guest inputs and computes output before
LS/channel mutation. Runtime execution retains fenced GETs, tag waits, PUTs
and channel ordering. Publication is reference matrices (48 bytes/joint),
world matrices (64 bytes/joint), then optional aligned curve words.

Unsupported key encodings, explicit weights outside the admitted recipe,
enabled interrupt entries, allocation overrides and uncovered schedules use
the whole lifted job. Similar code in images 43/45/47 is not sufficient to
qualify those images for native dispatch.

The [data reference](native-animation-data.md#animation-job-metadata-frontend)
describes the descriptor fields, clip block selection, quaternion packing,
curve padding and hierarchy layout.

## Dispatch, controls and diagnostics

| Switch | Meaning |
| --- | --- |
| `BUZZ_GEOMETRY_JOB=0/1/2` | Whole image-41 lift / native host pipeline / native with whole-job checking |
| `BUZZ_ANIMATION_JOB=0/1/2` | Whole image-40 lift / native host pipeline / native-retained I/O comparison |
| `BUZZ_GEOMETRY_JOB_HOST_PIPELINE` | Host data preparation group |
| `BUZZ_GEOMETRY_JOB_PUBLICATION` | Geometry output-publication group |
| `BUZZ_GEOMETRY_JOB_COMMANDS` | Geometry command group |
| `BUZZ_GEOMETRY_FALLBACK_REASONS=1` | Diagnostic fallback-reason counts; off normally |
| `BUZZ_GEOMETRY_JOB_LEGACY=1` | Old stage-by-stage implementation and its diagnostic switches; off normally |
| `BUZZ_GEOMETRY_DEBUG_STAGES=1` | Optional internal full-state checks |

The three production geometry groups default on with the supported host
profile; disabling a required group selects the whole lifted job. Old stage
switches are not cumulative upgrades to the host path. `output_option()`
routes only its known production groups and returns zero for other names
outside legacy mode. New production switches need explicit routing and tests;
otherwise they can silently have no effect.

Whole-job checking records actual ordered I/O and replays it into an isolated
reference/shadow. It must not perform real DMA/atomics twice. Image-40 mode 2
retains the native result and gives the original shadow the actual native GET
payloads; this also detects inputs changing between preparation and DMA.
Unrecordable comparisons are counted separately as skipped, never as passes.

`port/job_corpus.cpp` implements capture/replay. `BUZZ_REPLAY_CORPUS` selects
offline input and `BUZZ_REPLAY_CONTRACT=1` selects the observable job contract.
An image-40-only replay does not require image-41 coverage; enabled image-41
paths still require native coverage when image-41 jobs are present.

## Retained code and maintenance

Stage F, image 7/36 leaves and image-39 native FP retain their reviewed
adapters. The geometry leaf mask is 827 (`0x33B`); the
[kernel reference](native-geometry-kernels.md) describes the groups.
These routines also serve the lifted fallback and diagnostics.

Stage F's image-39 paired checker compares the complete r3 return value in
addition to LS, return PC and preserved registers. Its volatile-register
poison mode also preserves r3 automatically for kernel 3. The image-39 caller
consumes that count; treating it as dead scratch would hide an incorrect
native result. `tests/stagef-image39-return-test.cpp` exercises the actual
paired runner with a wrong count and a permitted scratch-register difference.

Legacy JOB1 removal remains deferred. The next optimization starts with a
profile of the current executable, rather than expanding cold-format coverage.
Do not reintroduce per-function SPU transliterations or assume that unobserved
formats are qualified because synthetic cases pass.

See [runtime defaults](runtime-defaults.md) for configuration and
[profiling and validation](performance.md) for acceptance requirements.
