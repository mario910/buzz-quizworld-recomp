# Image-40 animation data layouts

This reference describes the packed keys, host pose representation and
metadata of the admitted image-40 animation job. See [native jobs](native-jobs.md#image-40-animation)
for runtime ownership, whole-job fallback and observable correctness.
Internal SPU register/scratch layouts belong to the lifted reference, not
the native host arrays.

## Host animation keys

`native_animation_host_quaternions.h` consumes bounded byte streams and emits
host SoA, eight joints per batch. Each track has `interior_frames` packed
MSB-first key-presence bits. Bit `k` represents frame `k+1`; first/last keys are
separate, at frames zero and `interior_frames+1`. Interior keys follow track
order and then time order. Popcount and bit scans select neighboring keys and
their packed-stream offsets without an SPU register representation. Static
quaternion arrays reuse the same six-byte decoder directly, without the temporal
key masks or interpolation.

| Packed quaternion bits | Field |
| --- | --- |
| 47..32 | First unsigned component field |
| 31..17 | Second unsigned component field |
| 16..2 | Third unsigned component field |
| 1..0 | Omitted component index |

All three fields are scaled by `2^-15`, then decoded with the original fused
scale/bias. The omitted positive component uses the original SPU reciprocal-root
estimate, FI and refinement. Interpolation preserves the original hemisphere
choice, reciprocal refinement and acos/sine polynomial order; cosine greater
than `0x3F7FBE77` selects linear weights. Discarded near-parallel polynomial
intermediates are not evaluated. Stream, numeric and whole-job replacement
proofs are in [native job runtime](native-jobs.md).

`native_animation_host_vectors.h` reuses that temporal selection for raw FLOAT3
translation/scale tracks (`9A18` in image 40). Endpoint and interior keys are
12-byte BE float triples; output is host XYZ plus W = `1.0`. For each component,
preserve `sum = FMA(t, next, previous)`, then `result = FNMS(previous, t, sum)`.
Combining these into a single lerp changes rounding. The supported clip domain
has 0-128 interior mask bits and integer frame 0-`interior_frames`; the final
endpoint is at frame `interior_frames+1`. Bounded views reject truncated
selected keys before publication; host output is disposable if the recipe is
rejected.

`native_animation_host_pose.h` decodes 48-byte Q/T/S records: quaternion XYZW,
translation XYZW and scale XYZW, each four BE words. Only translation XYZ and
scale XYZ enter matrix production. It builds eight row-major host 4x4 matrices;
the final row is `(0,0,0,1)`. Preserve the original fused half-minus-square
diagonals followed by multiplication with twice the scale, and the separately
rounded quaternion cross products. Translation XYZ is copied bitwise, including
denormals, without FP arithmetic.

The original test caller consumes temporary column-major world matrices. Its
reference-composition producer also transposes them for the second,
64-byte-per-joint published stream while producing the first, 48-byte affine
stream. The native chain keeps row-major world SoA through composition and emits
both final streams directly; the LS layout bridge exists only in the
original-job test adapter.

`native_animation_host_blend.h` handles the observed implicit-full-weight
recipe: 541 clip strengths in [0,1], no explicit input joint-weight streams. It
reuses shortest-path quaternion interpolation; translation and scale each retain
`FMA(last, t, first)` followed by `FNMS(first, t, sum)`, including W. The
effective `t` includes the fixed SPU RTZ full-weight reciprocal refinement
(`factor * 0x3F7FFFFF`); exact division by one changes bits. Output joint
weights are 255. Explicit weights for this linear recipe need whole-job fallback
until covered.

`native_animation_host_hierarchy.h` admits the observed four-record schedule
groups (at least eight entries). Each BE32 entry contains the local joint in
high16 and the parent in low15; parent greater than `0x3FFF` selects the root.
Bit15 disables inheritance of the resulting joint scale. Local translation still
multiplies by the parent's scale before quaternion-vector rotation and addition
of the parent's translation. World rotation is the normalized parent/local
quaternion product. Preserve the original fused order and reciprocal-root
refinement; translation W is `1 + parent.W`.

Identical repeated definitions are canonicalized. The planner rejects conflicts,
missing joints, invalid spans/indices, cycles and uncovered small or partial
groups before pose mutation. Ready joints form batches of at most eight, and
children enter the queue after their parent batch completes. Local/world data
stays in little-endian host SoA; masked gathers give inactive lanes neutral
quaternions without reading their data. Inputs and outputs have no SPU
registers, scratch frames or dummy schedule iterations.

## Animation job metadata frontend

`native_animation_job_input.h` reads the observed job256 family without
modifying LS or materializing SPU registers. It currently admits one/two control
nodes and two/three clip references, matching every existing capture. It
validates metadata, not complete native job eligibility or execution.

| Source | Observed layout |
| --- | --- |
| Input 0 | 16 bytes/node: BE16 operation, left/right handles, reserved16, factor FP32 bits, reserved32; operation 0 or 1 |
| Handles | `0x8000` + clip index or `0x4000` + forward node index; node 0 is the root |
| Input 1 | 16 bytes/clip: EA32, header bytes16, flags16, sample-time FP32 bits, binding EA32 |
| Input 2 / 3 | Reference 3x4 matrices (48 bytes/joint) / empty |
| Cache 1 | ES01 skeleton; BE16 at +12 joints, +14 curve floats, +16 hierarchy quad count; schedule starts +48 |
| User +16 / +64 | Root Q/T/S (48 bytes) / BE16 node and clip counts |
| User +4 / +68 / +76 | World, reference and optional curve destination EAs |

Let `n = align4(joints)` and `world = context.output`. Reference output starts
at `world + n*64`; curve output at `reference + n*48 + 48`. Publication order is
reference `joints*48`, world `joints*64`, then optional curves
`align16(curve_floats*4)`, using the context DMA tag. All recorded publications
match this recipe. Runtime clip GET staging, static/default-pose setup and
control traversal still belong to the original oracle until integration.

`native_animation_host_additive.h` implements the observed operation-1 pose
recipe: flags 0, strength 1, explicit full weights on the base and implicit full
weights on the added pose. Compose base/additive quaternions in the original
fused order, then reuse shortest-path interpolation to that product. Translation
first adds the XYZ delta (delta W is zero); scale first multiplies by the delta
scale. Both then retain the original two fused lerp roundings, even at strength
1. Other strength/weight/flag recipes remain uncovered.

Scalar operation 0 uses the same fixed RTZ strength normalization and two fused
interpolation roundings as pose blending. Scalar operation 1 uses one
`FMA(delta, 1, base)`. Curve flag 1 clamps the final value to [0,1]; flag 0
leaves it unbounded. Other curve flags remain unsupported. Every word up to
`align4(curve_floats)` is part of the published contract: compute and compare
those tail values and weights explicitly. They are observable output, not
scratch or dead lookahead state.

`native_animation_host_curve_keys.h` samples raw FLOAT1 keys with four-byte BE
endpoints/interior values and the shared MSB-first temporal selection. Its two
fused interpolation roundings match the FLOAT3 sampler. Channel IDs are BE16 in
four-slot storage groups. Tail entries can repeat an aligned published channel;
only the final owner of each channel is decoded/evaluated and written. Integer
key-prefix counts still account for preceding stored entries, because they
locate the selected key bytes. Unowned values, SPU registers, lookahead state
and call frames are not reproduced.

The bounded recipe must include the stored bytes that determine published
alignment channels. Treating only the logical track count as observable loses
those output values. Compressed scalar keys have no corpus calls and remain
unsupported; static/default setup remains separate frontend work.

`native_animation_clip_input.h` reads the raw EA03 header and fetched block into
bounded static/animated stream and ID views. Header BE16 counts at 22-28 / 30-36
describe static / animated Q, T, S and curve tracks; flags at 38 select
compressed alternatives (unsupported). IDs start at 96: static quaternions
occupy groups of eight, other lists groups of four. Static payload offsets at
60-72 are relative to their own fields.

The block's first 16 bytes contain four endpoint sizes and four interior sizes.
First endpoints precede MSB-first temporal bits; interior payloads follow, with
curves aligned to four bytes. The next endpoint header starts at the following
16-byte boundary. The stream parser accepts selected frame, fraction and
interior-frame count as semantic inputs.

`native_animation_clip_time.h` supplies those inputs from header/time data. The
BE32 offsets at 52 and 56 are relative to their own fields: the first table
contains `(byte_count, clip_relative_offset)` BE32 pairs; the second contains
`(first_frame, interior_frames)` BE16 pairs. The last interval is an endpoint
sentinel, not a body GET candidate. Select the latest preceding nonterminal
interval from `floor(max(SPU_FM(time, rate), 0))`, with rate at header offset 8.
Subtract the interval start using SPU FS, truncate the local frame and subtract
it to form the fraction. If the local integer exceeds the interior count, use
that count and fraction 1. The eight-lane host implementation retains each FP
rounding and denormal rule.

Body EA is clip EA plus its selected record offset. With `table_lsa = header_lsa
+ data_table_offset`, body LSA is `table_lsa + ((body_ea - table_lsa) & 127)`.
All 38,025 real body GET recipes match. Header/table selection must happen
before body DMA can overwrite those table bytes. Uncovered clock ranges above
65,535 samples, compressed flags, malformed tables and invalid GET spans are
rejected. Runtime execution emits DMA/channel operations through the whole-job
driver.

Some published curve padding also depends on bytes beyond the fetched block. The
parser takes a separate bounded tail view and determines its required extent
from final-owner key selection. All 648 affected corpus clips use 4-156 trailing
bytes; these equal entry-LS bytes and are zero in this corpus. Preserve them as
explicit observable input, without assuming zero or reproducing scratch writes.
The maximum accepted view is 1,548 bytes (three extra slots * 128 interior keys
* 4 bytes + 12 endpoint bytes).

`native_animation_host_clip.h` assembles one complete raw clip in host Q/T/S SoA
arrays and aligned curve words. ES01 field 20 is an own-field-relative offset to
48 bytes of default Q/T/S per joint; decode this template once per job. Each
leaf copies the host template, zeros logical curves, applies static and sampled
streams, and retains explicit alignment words. Static T uses packed BE float3
with W=1; static curve words copy their BE32 payloads by ID. Static scale
streams have no real calls; no separate scale-format coverage is claimed.

Curve alignment belongs to its host buffer: first use starts with entry input
words, while the 2,597 third-leaf buffer reuses inherit the second leaf's
produced tail words. This state transfers between host arrays; it requires no
SPU register bank or scratch writes. The test-only `ClipReplace` bridge emits
the complete logical pose and aligned curves once for the remaining original
control driver. Native runtime integration must keep these arrays through
blending/hierarchy rather than round-trip them via LS.

`native_animation_host_pipeline.h` evaluates admitted control nodes in reverse
index order (node handles only point forward). Leaves and node results remain
host Q/T/S SoA plus aligned curve words. Operation 0 uses the shared full-weight
blend; covered operation 1 requires strength 1. ES01 own-relative field 40
points to per-curve clamp flags, including alignment slots; only 0/1 flags are
admitted. The resulting host root pose feeds the dependency-aware hierarchy,
world matrices and reference composition directly. Reference matrices are
decoded once and final BE reference/world/curve streams are each emitted once.
No intermediate pose is serialized through LS in this host chain.

The additional test-only `PipelinePublish` arm supplies every published byte
from that chain while retaining the original scheduling/channel scaffold. The
oracle now also runs a separate, unmodified original job for every capture and
compares each channel write/read/count tuple (direction, channel, raw 32-bit
value) in order. Per-job counts/digests are retained in `channel-checks.tsv`;
comparison checks every event, not just its digest. Changing a DMA command flag
while keeping its transfer data unchanged must fail this gate.

`native_animation_job.h` now prepares and executes the recognized job as a
whole. Preparation resolves bounded immutable guest header/body inputs, admits
all clips and computes host outputs before any LS/channel mutation. The executor
issues the required fenced GETs (command 65, tags 30/28), pose buffer waits,
final PUTs (command 32, context tag), and returns to the caller. It calls no
lifted blocks and restores no SPU register bank or stack frame.

Header arenas use an 8,448-byte pitch from scratch: clips 0/2 share slot 2, clip
1 uses slot 0. Pose-buffer entry padding uses the following allocation area,
with capacity for at least three slots; required raw key tails retain only input
bytes from entry/previous GETs, not a reconstructed scratch image. Worker index
at LS 0x1C8 selects one of six cache-0 allocation records; the covered records
have no override. Only the observed two/three-clip schedules are admitted.
Enabled interrupt entries, allocation overrides and unsupported metadata/streams
fall back before I/O. Production dispatch is owned by
`native_animation_runtime.cpp`.
