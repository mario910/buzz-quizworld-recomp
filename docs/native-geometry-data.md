# Image 41 data map

Status: phase 1 map complete for the audited lift. The descriptor, staged
buffers, observed DMA stream and formats below have source/corpus evidence.
Coverage is distinguished below from static analysis: formats absent from the
corpus and alternate allocator paths retain the lift as their future fallback.

The audited EU 1.00 lift has SHA-256
`5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812`.
Derived code, memory dumps, corpus bytes and detailed traces remain under
ignored `analysis/native-geometry/` and must not be committed.

## Groundwork and strictness

`port/native_function_check.h` runs a synchronous, DMA-free reference and
native function from identical registers, active LS and channel state. The
reference result is retained. It compares all 128 registers, every LS byte,
PC/status/stop, architectural channel/MFC/reservation/interrupt state, and
the trampoline continuation. It reports the first register byte, LS byte
and control field that differ. Host pointers, locks, instrumentation counters
and host call bookkeeping are excluded. Native fallback is not a comparison.
Functions issuing DMA/atomics must additionally use whole-job replay; the
leaf runner must not execute their external side effects twice.

`BUZZ_FUNCTION_CHECK=strict` forces checked stage F and fails replay on any
full-state difference. `BUZZ_FUNCTION_CHECK=stagef` also records full-state
differences, but uses the existing stage F contracts for its exit verdict.
This exception for existing stage F was explicitly accepted on 2026-10-30;
new native kernels must pass strict checks. Existing stage F leaves deliberately
do not reproduce dead r2..r79; its cull region uses its established kept-index
contract. These are not full-state passes. No native kernel/default was added.

The initial 800-job menu cohort reproduced all established checks: 800 vertex,
1,045 transform, 369 normalize and 800 cull comparisons, zero contract failures.
All three leaves differed in volatile registers under strict comparison;
their LS/control state matched. Strict replay returned exit code 1 as required.
The final cohort passed 19,508 image 41 and 17,714 image 40 whole-job replays,
with zero concurrent-input divergences. The stage F checker passed 19,508
vertex, 27,561 transform, 9,877 normalize and 19,508 cull comparisons, with
no fallback in those three leaves. A deliberately corrupted exit-LS byte was
also rejected at `0x3FFFF`, proving the whole-job checker detects dead LS
differences rather than silently using output-only comparison.

## Descriptor and job context

All LS fields and DMA payload words are big-endian. Serialized register words
in the v1 corpus are host little-endian; preferred slot is `_u32[0]`.
See `ps3recomp/runtime/spu/spurs_job.c` for the staging implementation.

| Descriptor offset | Field | Encoding |
|---|---|---|
| `0x00` | binary EA, cache-inhibit flag | u64, low bit is flag |
| `0x08` | binary size | u16, multiply by 16 |
| `0x0A` | input DMA list length | u16 bytes, divide by 8 |
| `0x0C` | high input EA | u32 |
| `0x10` | in/out flag | u32 |
| `0x14` | input/in-out region size | u32 bytes |
| `0x18` | output region size | u32 bytes |
| `0x1C` | stack size | u16 quadwords; zero selects 8 KiB |
| `0x1E` | scratch size | u16 quadwords |
| `0x20` | high cache EA | u32 |
| `0x24` | cache DMA list size | u32 bytes |
| `0x2C` | job type | u32 |
| `0x30` | input DMA list, then cache list, then user data | variable |

An input list entry is two BE words: size/flags (`word0 & 0x7FFF` bytes),
then low EA. Each nonempty staged entry advances the LS cursor by
`align16(size)`. Empty entries are retained in the pointer table but do not
advance the cursor. Staging happens **before capture begins**; these input
copies are present in entry LS, not in the recorded job-issued DMA stream.

User data begins at `0x30 + inputListBytes + cacheListBytes`, not `0x80`.
The common 21-entry, no-cache descriptor has user data at `0xD8`, extending
through its 256-byte descriptor. `corpus-inspect.py` now decodes the context's
descriptor-size bitfield and uses this boundary.

| Context offset | Field |
|---|---|
| `0x00` | ioBuffer LS base |
| `0x04..0x13` | four cache-buffer LS bases |
| `0x14` | descriptor size in the high nibble: 0 = 64 B, otherwise n * 128 B |
| `0x18`, `0x1A` | u16 input/cache counts |
| `0x1C` | output LS base |
| `0x20` | unified scratch/stack base |
| `0x24` | DMA tag (staging uses 20) |
| `0x28` | descriptor EA, u64 |

Entry r3 points at this context, r4 at the descriptor. The copied context is
at LS `0x3FBC0`; the descriptor starts at `0x3FC00`. The job-manager return
sentinel is `0x3FF00`.

## LS allocation and access map

Staged regions have 1 KiB alignment in this order: binary, input/in-out,
output, unified scratch+stack, then each cache buffer. Empty region bases
remain valid pointers. Addresses passed to 128-bit LS operations are masked
with `0x3FFF0`; unaligned scalar selection is implemented with rotates and
shuffles around those aligned loads/stores. A native path must preserve this
including final padded groups and read/modify/write spill quadwords.

| Region | Observed or derived role |
|---|---|
| `0..sizeBinary` | lifted text, constant tables and writable globals |
| `0x1C8` | guest lane ID in the CRT metadata, read by `0x2718` |
| `0xDF00..0xE0FF` | additional/blend input weights, loaded by the first GET at `0x0D40` |
| `0xE100..0xE4FF` | additional/blend input list entries, loaded by the second GET at `0x0D40` |
| `0xE500..0xE553` | input pointer table populated by setup `0x08A8` and following blocks |
| `0xE570` | saved job-context pointer |
| `0xE580` | saved descriptor pointer |
| `0xE590` | descriptor input-list pointer (`descriptor + 0x30`) |
| `0xE5A0` | input DMA entry count |
| `0xE600..0xE67F` | 128-byte allocation/reservation-line scratch |
| `align1024(sizeBinary)` | ioBuffer; observed image 41 binary size `0xE680`, io base `0xE800` |
| after aligned ioBuffer | output region (commonly zero bytes in image 41) |
| next aligned region | scratch plus stack; variable per descriptor |
| above stack | cache regions, if declared |
| remaining gap | unallocated/preserved LS; not implicitly zero on real pooled entry |
| `0x3FBC0..0x3FBFF` | context and CRT job-manager interface |
| `0x3FC00..` | copied descriptor; size comes from context |

This table does not assign fixed addresses to variable scratch arrays. The
per-job descriptor-derived boundaries are in `corpus-audit-final.json`; each
observed CPU read/write range, attributed to its issuing block, is in
`final41-trace.ls.csv`. Access flags 1/2 mean CPU read/write; endpoints are
exclusive and quadword aligned. This includes stack saves and descriptor
reads, not only output buffers.

## DMA and output ring

The instrumented lift records original MFC commands in `*.commands.csv`,
and expands list elements into `*.dma.csv`. List word0 carries size in low
15 bits and stall notification in bit 31. Each nonempty list element uses
`(cursor & ~15) | (EA & 15)` as LS destination and advances the cursor by
`align16(size)`. Stalled lists fail closed in the trace tool until explicit
resume instrumentation exists.

The final all-image-41 replay matched **58,195 ordered transfers across
19,508 jobs**, including expanded lists, with zero shape/order differences.
The whole-job replay separately matched their bytes and exit state.

| Issuing block | Command | Observed role |
|---|---|---|
| `0x0D40` | two GETs `0x40` | `align16(count*4)` weights to `0xDF00`; `align16(count*8)` list entries to `0xE100` |
| `0x0EA8`, `0x126C` | GETL `0x44` | additional/blend input, one 8-byte list entry per issue, into alternating work buffers; tag from context `+0x154` |
| `0x3948` | GETLLAR `0xD0` | reserve allocation line into `0xE600` |
| `0x3AFC` | PUTLLUC `0xB0` | publish updated allocation line |
| `0x2EC8` | PUT `0x20` | chunked index/output allocation writes |
| `0x36D8` | PUT `0x20` | chunked vertex/output allocation writes |
| `0x35A8` | GETF `0x42` | 4-byte ordered read following output writes |
| `0x2968` | PUTF `0x22` | publish RSX command block |
| `0x3E04` | GET `0x40` | refresh 4-byte GPU free cursor (20 calls) |

`0x3948` computes allocation-line EA as `ringBase + 128 + 128*lane`.
The guest lane ID comes from LS `0x1C8` through getter `0x2718`; it is not
the host worker-thread index (captured jobs also use guest ID 4).
The descriptor's first user word supplies the ring base; the second supplies
the command-output EA. Observed allocation lines have begin EA at `+0x00`,
the corresponding GPU-local offset at `+0x04`, end EA at `+0x08`, and cursor
at `+0x0C`. `+0x10` is copied to the allocation result's synchronization word;
`+0x14` is the EA of the GPU free-cursor word; `+0x18` caches that cursor
(`cachedFree` in the lift's assertion). Other bytes of the 128-byte line must
be preserved. A zero cached cursor forces the 4-byte refresh. The low seven
bits of the cursor-word EA select its position in an aligned stack temporary.

Allocator `0x37C0` takes r3=geometry context, r4=ring EA plus low-bit flags,
r5=requested bytes, r6=result LS pointer, r7=lane. It rounds bytes to 128.
Result words are CPU EA, GPU offset, synchronization word, end EA, followed by
auxiliary synchronization state. GPU offset is `gpuBase + (chosenEA-begin)`.
With r4 bit 1 set, r4 masked by `~3` is already a direct output address;
that path does not issue atomics. Otherwise it reserves its per-lane line.
`0x3A24` checks the candidate interval against end and cachedFree, tries begin
on wrap, and polls `0x3E04` when necessary. It publishes with PUTLLUC, not a
compare-and-swap retry: the reservation and interrupt-disable sequence is part
of the original contract. Interrupt state is restored before returning.

When begin=end, `0x3B88` selects the shared fallback line at ringBase. When
twice the rounded request exceeds lane capacity, `0x3D00` selects fallback.
Both paths use GETLLAR/PUTLLC retry loops; exhausted shared allocation records
the requested amount at `+0x14` and returns zero. They must not be replaced
with the ordinary lane path. The following completes the lexical DMA-site map:

| Block | Command / size | Coverage and purpose |
|---|---|---|
| `0x1114` | GETL `0x44`, 8-byte list | Unreachable C tail after unconditional interrupt-disable/indirect return; retained as lexical evidence, not a reachable issue site |
| `0x24F0` | GET `0x40`, 32 B | Optional driver input block into stack `+0x70`; absent in corpus |
| `0x2630` | PUT `0x20`, 32 B | Optional driver output summary from stack `+0x70`; tag = context tag + 5; absent |
| `0x3BA0`, `0x3BDC` | PUTLLC `0xB4`; GETLLAR `0xD0` in retry | Shared fallback reservation when lane ring is disabled; absent |
| `0x3D34`, `0x3D54` | PUTLLC `0xB4`; GETLLAR `0xD0` in retry | Shared fallback for oversized/failed lane request; absent |
| `0x3F40` | GET `0x40`, 4 B | Second free-cursor refresh while reconsidering lane ring after fallback; absent |

There are 18 lexical blocks with MFC command writes, 17 with a reachable
write in the lifted C, and 10 exercised in this corpus. No unsupported path
is declared dead merely because replay did not enter it.

## Geometry configuration and staged slots

These are the 21 *descriptor* input entries, before any job-issued DMA:

| Slot | Role | Layout / use |
|---|---|---|
| 0 | Command template / optional extra-input driver list | BE words; can be empty |
| 1 | Custom output stream descriptor | Header plus attribute records below |
| 2 | Compressed indices | Header, one-bit flags, triangle codes, packed deltas |
| 3..5 | Skin matrix palette chunks | Format 0: 48 B per matrix, BE float3x4 |
| 6 | Skin weights and bone indices | Flavor 1: four interleaved u8 pairs per vertex |
| 7 | Auxiliary stream | Empty in audited corpus; preserve as opaque |
| 8..10 | Input stream A chunks | Concatenate nonempty chunks, commonly split at 16 KiB |
| 11..13 | Input stream B chunks | Same chunk rule; optional |
| 14 | Viewport / camera transform input | 128 B |
| 15 | Object transform input | 64 B |
| 16 | Geometry configuration | 16 B below |
| 17,18 | Fixed-offset tables A/B | Decoder `0xAC68` alignment checks; empty here |
| 19,20 | Custom stream descriptors A/B | Selected by format ID `0xFF` |

| Config byte | Meaning | Observed |
|---|---|---|
| 0 | low nibble + 1 = allocated float4-table count; `0x80` = extra table for skin/blend; `0x10` = static fast path | Table count varies; 0x2F80/30E8 allocates at least one |
| 1 | Command/routing control | Keep numeric; do not infer a new vertex format from it |
| 2,3 | Input format IDs A/B | A=0 (BE float3 position), B=0 or `0xFF` |
| 4 | Output format ID | `0xFF` (custom) |
| 5 | Additional format selector | `0xFF`; unused values not characterized |
| 6 | high nibble = index flavor; low nibble = skinning flavor | index 3; skin 0/1 |
| 7 | Matrix format | 0 (float3x4) |
| 8..9 | BE u16 vertex count | Logical count, excluding padding |
| A..B | BE u16 index count | Triangle-list count |
| C..F | Opaque control/reserved bytes | `0xFF`; preserve |

These selectors are read by `0xACD0`, `0xAD88`, `0xAEA0` and `0xAF40`.
The 34 distinct config prefixes and 19 distinct role/descriptor combinations
are inventoried privately. Descriptor user word at `+20` (descriptor `+0xEC`
for 21 entries) supplies flags. **Bit 7 selects position-only output**:
`0x1F30` rewrites the first three bytes of the staged output descriptor to
`{1,12,1}`. Inactive attribute records remain present. Bit 8 gates secondary
stream processing; an empty B stream is also permitted. Consumers must use
the effective header at the processing boundary, not immutable entry bytes.

## Stream descriptors and codecs

A custom descriptor has an 8-byte header: attribute count at byte 0, stride
at byte 1, a control/count byte at byte 2 (equal to count in this corpus),
then five reserved bytes. Each attribute occupies eight bytes:
`{offset, storage, components, routing, byteSize, shaderSlot, reserved, reserved}`.
Offsets are within one interleaved vertex. The last two bytes are zero in
observed records. Attribute byteSize is not necessarily components times a
scalar width (unit vectors occupy three bytes; CMP occupies four).

| Storage ID | Decoder / encoding | Corpus use |
|---|---|---|
| 1 | `0x85E8`, signed normalized 16-bit components | Output tangents |
| 2 | `0x87CC`, BE float32 components | Positions |
| 3 | `0x8378` -> `0xCAC0/CB78`, binary16 components | UV sets |
| 4 | `0x89B0`, normalized unsigned bytes | RGB color |
| 5 | `0x8B94`, signed 16-bit integer components | Unexercised input selector |
| 6 | `0x8D34`, signed CMP 11/11/10 | Output normals |
| 7 | `0x8EA8`, unsigned byte integer components | Unexercised input selector |
| 8 | `0x9058` -> `0xC810`, variable-bit fixed point plus fixed offsets | Compressed-position-capable path; **not used by captured positions** |
| 9 | `0x9114` -> `0x9148/92A0`, 24-bit unit vector | Input normals/tangents |

Storage dispatch is at `0x8354`. Routing IDs observed are 1=position,
2=normal, 3=tangent, 5=UV0, 6=UV1, 9=RGB. Shader slots are respectively
0, 2, 14, 8, 9, 3. UVs use two BE halfwords. Tangents have three or four
components; the fourth sign survives unit-vector decode. CMP output is a BE
word: signed x in bits 0..10, y in 11..21, z in 22..31, normalized by
1023/1023/511 (SDK `rsx_live_draw.c`). Built-in output IDs 0..3 have strides
12/20/24/24; all captured jobs use custom output instead.

All captured input positions are raw BE float3. In the static fixed-point
path, attribute byte 6 selects an extra 8-byte width record relative to
`descriptor+8`. Its four pairs are `{integerBits, fractionalBits}`; tables
`0xD230/0xD240` select odd/even bytes. Components consume integer+fractional
bits from the packed vertex. A separate 16-byte record supplies BE u32 fixed
offsets, added to each extracted unsigned component with word wrap before
separating integer/fractional parts. `0xC910` uses CSFLT
155 for the signed integer and CUFLT 123 for the shifted fraction, then FA.
Those instructions, including shifts and padding accesses, remain the
authoritative contract for this unexercised path; it has no corpus-exact claim.

### 24-bit unit vectors

Read three bytes as BE u24. `qU=(word>>14)&1023`, `qV=(word>>4)&1023`;
the low nibble selects axis/sign. Axis is `selector>>2` (0=x,1=y,2=z);
bit 0 selects dominant component sign (0 negative, 1 positive), bit 1 selects
W (-1/+1). Axis 3 is reserved. All 1,070,859 active packed attributes in the
inventory select axes 0..2.

The two nondominant components, in axis order, are
`u=FMA(bitcast(0x3F800000|qU), bitcast(0x46352E7C), bitcast(0xC6353150))`
and the same expression for v. Compute `s=FA(FM(u,u),FM(v,v))`, `t=FS(1,s)`,
then dominant magnitude `FM(t,FI(t,FRSQEST(t)))`. Insert its selected sign
at the chosen axis and append W. Use the SPU operations: `sqrt(1-u*u-v*v)`
is an explanatory approximation, not a bit-exact replacement. Four decoded
sample vectors were checked at paired `0x9148` entry / `0x92A0` exit snapshots;
their source offsets and float4 destinations agree with this bit layout.

### Skinning

Flavor 1 uses 8 bytes per vertex:
`{weight0,index0,weight1,index1,weight2,index2,weight3,index3}`.
Each weight is an unsigned integer. All 716,781 records across 1,628 skinning
jobs sum to 255, and every nonzero-weight bone index is in the staged palette.
The lift uses CUFLT 155 followed by FM with float bits `0x3B808080`
(RTZ representation of 1/255), not a host double division. Matrix offset is
`index*48`, formed as `index*16 + index*32` in `0xB880`. The first paired
`0xB940` snapshot confirms that coefficient. Preserve accumulation order and
the separate position/direction treatment when implementing this later.

### Compressed indices

Index flavors 2/3 enter `0x7390`; captured flavor is 3. The eight-byte header
contains BE u16 explicit-delta count at +0, delta bias at +2, one-bit flag
byte count at +4, packed delta width at +6, and a reserved byte at +7.
One-bit flags start at +8, followed by `ceil((indexCount/3)*2/8)` bytes of
triangle codes, then `ceil(deltaCount*width/8)` packed delta bytes. These are
physical section boundaries, not three independent index arrays.

`0x7428..0x7640` expands fixed-width deltas backward into halfwords after the
aligned packed sections; `0x7730/7770` prefix-sums them after subtracting
bias. `0x7838/78A8` uses one-bit flags to merge explicit values with sequential
indices, and `0x79C8/7A90` uses two-bit triangle codes plus tables at
`0xD690/0xD790` to reconstruct BE u16 triangle lists at the original stream
base. Scratch capacity is checked against geometry context `+0x17C` before
expansion. The final aligned spill is significant. This is why slot 2's
small compressed entry payload can later hold much larger index intermediates.

## Vertex and index streams

The existing, validated intermediate stage F representation is a BE float4
array with 16-byte records. Transform inputs are r3=count, r4=input LS,
r5=output LS, r6=matrix LS. Processing covers `ceil(count/8)` groups of eight,
including padding. For each output component, the cyclic accumulation order
in `port/native_stagef_loop.h` is part of the bit-exact contract.

Normalization uses r3=array LS, r4=count; it covers `ceil(count/4)` groups of
four. For `(x,y,z,w)`, the established arithmetic is
`s=fma(z,z,fma(y,y,fm(x,x)))`, `r=fi(s,frsqest(s))`, followed by
`(fm(x,r),fm(y,r),fm(z,r),fm(w,1))`. Preserve the SPU helper behavior and RTZ
environment rather than replacing this with an IEEE `sqrt` division.

The vertex clip/cull record is 16 bytes per vertex: projected x, projected y,
clip flags in the high byte of a word, and packed fixed-point x/y plus the
W-sign flag. It is an intermediate record, not a declaration that every final
RSX vertex attribute has this format. Cull output stores three BE u16 indices
per retained triangle and includes the final aligned quadword spill.

### Dynamic work areas

The geometry context is a stack object, not the SPURS context. Its first 16
bytes copy slot 16. At `+0x10` starts an array of up to 16 float4-table pointers;
each allocated table consumes `vertexCount*16` bytes plus alignment/padding.
`+0x5C/+0x60/+0x64/+0x68` select position, normal, tangent and an additional
attribute table (the latter is kept numeric where absent). `+0x54/+0x58`
are intermediate-work/output pointers; `+0x50` is the decoded index pointer;
`+0x6C/+0x70/+0x74` hold custom A/B/output
descriptors. The uniform routing byte array starts at `+0x160`.

Context `+0xA0..0xDF` holds viewport/object setup, `+0xE0..0x11F` the combined
matrix, `+0x120..0x13F` viewport scale/bias, and `+0x150/+0x154` DMA tags.
Allocator boundaries are `+0x170` (uniform/scratch start), `+0x174` (uniform/scratch end),
`+0x178` (input workspace start), `+0x17C` (input workspace end); `+0x180`
starts the mutable count block. Unnamed/reserved context fields are preserved.

At initialization, the input workspace starts at the I/O base plus the whole
first word of input-list entry 0 (the retained command-template prefix size),
not at the end of the staged input list. Its end is the declared I/O end.
The float4-table allocation count is `(config[0] & 15) + 1`; each table has
stride `align_up(vertexCount, 8) * 16`, including zero-sized tables when the
vertex count is zero.

Initialization copies 128 viewport bytes to `+0xD0..+0x14F` and 48 object
bytes to `+0xA0..+0xCF` when supplied. The custom-state pointer supplies words
0/4/8/16/20/24/28/32/36 for context `+0x6C/70/74/7C/80/84/88/8C/90`;
`+0x78` is zero, and unused `+0x94..+0x9F` bytes retain their previous values.
The null-custom case zeroes those selected fields, not the whole region.
Only the first halfword of mutable counts at `+0x180` is set to index count;
its remaining 14 bytes are preserved. Full-LS comparisons against original
`0x2F80..0x3268` cover these boundaries and optional copies.

The main driver's `0x520`-byte frame saves all GPRs 80..109 at old-SP
`-0x10..-0x1E0`, GPR 126 at `-0x1F0`, the caller link at old-SP `+0x10`, and
the old SP quadword at the frame base. The native preamble preserves all
words of these values and the globals at `0xE570..0xE5AF`, not just their
preferred scalar words. It builds custom state at frame `+0xB0`, clears
`+0xC0..+0xCF` and only the first eight bytes of `+0xD0..+0xDF`.

The blend flag (`0x10000000` in descriptor `+0xEC`) first saves the original
48-byte object input in frame `+0xE0..+0x10F` and creates additional records
at `+0x110..+0x15F`. It leaves the default records at `+0x160..+0x18F`
intact; those defaults overwrite the first 48 bytes of the staged object
input on both branches. The position-only bit (`0x80`) edits the output
descriptor's first three bytes to `1,12,1`. Full-LS preamble tests cover both
flags and preserve the unused frame/pointer-table padding.

For the first menu sample, SPURS scratch starts at `0x11000`, float4 tables
are `0x11000`, `0x12980`, `0x14300` (408 vertices each), and geometry context
is `0x18460`. Compressed indices start at `0xE8F0`; their decoded triangle
list overwrites that input workspace. Output vertex records reuse the free
input workspace after the kept indices; RSX command assembly uses its later
portion. Stack frames grow down from the descriptor-derived stack top; the
allocator temporarily reserves `0x540` bytes and aligns four 128-byte slots.
No fixed absolute scratch address is a portable ABI. The region table plus
descriptor-derived boundaries covers the entire LS; per-block trace spans
provide the read/write cross-check, including untouched preserved gaps.

## RSX command stream

Published command blocks contain BE method headers and parameters. The SDK
decoder uses `(header >> 18) & 0x7FF` for parameter count; method byte offsets
identify vertex buffers (`0x1680`), formats (`0x1740`), primitive begin/end
(`0x1808`), index-buffer offset/format (`0x181C`/`0x1820`) and indexed batches
(`0x1824`). See `ps3recomp/libs/video/rsx_dispatch.c` and `cellGcmSys.c`.
Do not treat alignment or cache-invalidate commands as vertex payload bytes.

Bit 30 selects nonincrementing methods. A batch word contains first index in
bits 0..23 and count minus one in bits 24..31. Vertex formats encode type in
bits 0..3, components in 4..7, stride in 8..15, frequency in 16..31; buffer
methods encode location in bit 31 and GPU offset in 0..30. Output attributes
bind to the descriptor's shaderSlot; vertex offsets add each attribute's byte
offset to the reserved vertex GPU address. Index type is BE u16. The job may
emit multiple begin/end passes drawing the same kept indices, so logical
index-count bounds apply per pass, not to their sum.

The tail disables active vertex formats (size zero), then writes methods
`0x1D6C/0x1D74` with synchronization data from the allocation line and the new
ring cursor. The 4-byte GETF before the command PUTF orders output publication;
preserve its command/tag ordering even when its read value is otherwise dead.

### Command-buffer preparation and finalization contract

The phase-3 strict function oracles additionally verify the following
`0x32C8`/`0x27A0` contract; see the separate phase-3 results document for
implementation coverage and remaining gates.

| Command record offset | Meaning |
|---|---|
| `+0` | Start LSA |
| `+4` | Logical end LSA, before 128-byte allocator rounding |
| `+8` | Mutable write cursor |
| `+12` | Reserved word, initially zero |

Preparation takes its current workspace cursor from geometry context `+0x58`,
reserves 16 temporary bytes, aligns the new cursor to 128 and adds the command
EA's low seven bits. The logical limit is start plus `16*config[1]`, or four
bytes when the byte is zero; the allocator cursor rounds that limit to 128.
If any allocation record has nonzero word `+16` and byte `+20`, preparation
emits a 32-byte cache-control preamble (`0x1710`, then three `0x1714` methods,
each with a zero parameter). This is separate from final semaphore release.

Finalization skips all LS and channel access when its destination EA is zero.
Otherwise each 32-byte allocation record with nonzero word `+16` contributes
four command words: header `0x00041D6C`, `word[+16] & 4080`, header
`0x00041D74`, and word `+12`. It then splits packets crossing 128-byte
boundaries. Header parameter bytes are `(header >> 16) & 0x1FFC`; mask
`0xE003FFFF` preserves the method/control bits while replacing that length.
Incrementing packet continuations add the consumed byte count to the method;
bit-30 nonincrementing packets retain their method. A split inserts one word
by moving the physical tail four bytes toward higher addresses, in reverse
block order, including the full last 128-byte block and its padding. A first fragment with no parameters becomes a NOP.

Traversal performs a final unconditional look-ahead load even after reaching
the logical end. That load can lie outside the command allocation and uses
the SPU's wrapped LS address; it still contributes to strict ABI state.
Finalization zeros remaining logical words, sets the record cursor to its
limit and submits PUTF with start LSA, destination EA, context `+0x150` tag,
and the configured byte count (`16*config[1]`, or four). The independent
oracles compare physical LS bytes and complete state as well as DMA payloads;
packet-level semantic equivalence alone is insufficient.

### Command attribute packets and position-only emitter

The shared packet writers (`0x0100`, `0x0020`) use the descriptor pointer at
geometry context `+0x74`. Descriptor byte 0 is attribute count, byte 1 stride;
each eight-byte attribute record starts at descriptor `+8`: byte offsets
0/1/2/5 hold output offset, type, component count and shaderSlot. The selected
mask is its low 16 bits. Binding emits one format packet and one address
packet for each selected attribute; reset emits the same format method with
parameter 2 (zero size, type 2).

The format word is `(stride << 8) | (components << 4) | type`. Address packets
use the output record's word `+4` plus the attribute's byte offset, OR'd with
word `+8` shifted left 31. Methods select `0x1740 + 4*shaderSlot` for format
and `0x1680 + 4*shaderSlot` for address. Descriptor/count/unused-pointer and
non-preferred-lane behavior is checked by the strict phase-3 writer oracle.

The position-only emitter (`0x02E8`) surrounds these bindings with optional
raster/cull state, cache invalidation, index-buffer setup, primitive begin/
end and indexed batches. A leading batch handles the indices before the
next 128-byte index-address boundary when that prefix is nonempty and less
than the full count. Subsequent batch parameters each describe up to 256
indices. A full nonincrementing packet contains 2,047 batch parameters and
occupies 8,192 bytes; the remaining packet contains the final batches.

Zero command destination skips finalization/PUT but still assembles the
stream. Such a stream may extend past its logical reservation; the native
preflight bounds its actual physical writes instead. Published streams must
fit their logical reservation after all 128-byte packet insertions. Root and
nested preparation spill areas, descriptors, allocation records and optional
metadata are checked before the synchronization GET can occur. These are
phase-3 native implementation contracts, with results and coverage limits in
[native-geometry-phase3-results.md](native-geometry-phase3-results.md).

### Full multipass command-template state (`0x12F0`)

A non-null command template has a BE word pass count, followed at `+16` by
one 64-byte record per pass. A null template selects a single pass without
optional state fields; its original initial LS-zero quadword read remains
part of strict ABI reconstruction. Each pass draws the same logical index
range. The native reader bounds the complete stream before the first GET.

Disable bits come from `_u32[1]` of the full flags argument. The lift masks
selected bytes, compares four words, gathers their bits and tests the result
against 11; this selects word 1 rather than the preferred word. A marker of
8 skips its state packet; other marker values emit the fixed method below.

| Disable bit | Record marker offsets | Methods emitted |
|---|---|---|
| `0x2` | `+0`, `+8` | `0x0304`, `0x0308` |
| `0x1` | `+48`, `+56` | `0x0A74`, `0x0A70` |
| `0x4` | `+24`, `+32` | `0x0310`, `0x0314` |
| `0x8` | `+16` | `0x183C`, optionally `0x1830` |

Payloads normally follow the marker by four bytes. Method `0x0308` splits
its word into low 24 bits and high eight bits. Method `0x0314` duplicates the
low halfwords from record `+36` and `+44` into both halves of its two
parameters. The raster value at `+20` selects disabled/normal/reversed cull
state, with 2305 selecting the reversed order. Reserved words are read or
preserved exactly as in the lift, without assigning them new meanings.

The phase-3 function oracle independently parses published packet lengths
and compares their final non-NOP end with the preflight's relocated size.
Unpublished streams instead check their raw cursor, including the legal
zero-destination case that assembles beyond the logical reservation.

## Corpus cross-check and coverage limits

`geometry-format-inventory.py` rejects malformed layouts and checks
the following independently of whole-job byte replay:

| Check | Final result |
|---|---|
| Config/descriptor inventory | 19,508 jobs; 34 config prefixes; 19 role/descriptor combinations |
| Position A and compressed-index section bounds | 19,508 each |
| Secondary stream bounds | 1,876 present; 17,384 flag-disabled; 248 permitted absent |
| Skin payload, weight sums, active palette indices | 1,628 jobs / 716,781 vertices; zero failures |
| Packed-unit-vector selector axes | 1,070,859 attributes; zero reserved axes |
| Lane reservations / contiguous output / 128-byte rounding | 7,182 linear, 19 wrapped; 12,307 jobs produce no vertex/index output |
| Vertex DMA size vs effective output stride | 7,201 drawn jobs |
| Index DMA bound vs original count | 19,508 jobs |
| RSX packet bounds / per-pass draw counts | 19,508 published blocks |
| Active RSX formats vs effective descriptor | 16,276 bindings |

The checker also compares command vertex/index GPU addresses with the ring
allocation's CPU-to-GPU mapping. Paired snapshots have a JSON sidecar naming
the exact source job, block and exit state; they are block boundaries, not
automatically a subroutine ABI. The latest wrapper-instrumented full replay
again matched all 19,508 jobs and 58,195 ordered transfers.

Static-only coverage remains explicit: fixed-point input positions, integer
input selectors 5/7, nonzero fixed-offset tables, auxiliary slot 7, direct output
addresses, shared allocator fallback and second polling path are absent from
this corpus. Opaque/reserved bytes are identified and preserved. This finishes
the phase 1 annotated map; it does **not** certify future native implementations
of those paths. Any replacement reaching an unverified selector must retain the
lift until dedicated capture, strict comparison and the live/render/A/B gates
pass. No native kernel or default switch changed during phases 0/1.

## Reproduction

Run from the repository root with private, locally generated inputs:

```powershell
.\tests\scripts\test-native-function-check.ps1
.\toolenv\Scripts\python.exe -m unittest discover -s tests -p test_native_geometry_tools.py
.\toolenv\Scripts\python.exe tools/inspect/native-geometry-map.py
.\toolenv\Scripts\python.exe tools/inspect/native-geometry-corpus.py analysis/native-geometry/corpus --output analysis/native-geometry/corpus-audit-final.json --index analysis/native-geometry/replay-final
.\tests\scripts\build-geometry-trace.ps1
```

The build helper temporarily overrides `PORT_GEOMETRY_SOURCE`, saves the
private tracer, and restores the selected source/build in a finally block.
Set `BUZZ_REPLAY_CORPUS` to one indexed image directory and
`BUZZ_GEOMETRY_TRACE` to a private output prefix. Run the private tracer with
the prepared ELF from `buzz-paths.json`. Optional `BUZZ_GEOMETRY_SNAPSHOTS`
names an existing directory and `BUZZ_GEOMETRY_SNAPSHOT_PCS` a comma-separated
list such as `0x9148,0x92A0`. A trace build is replay-only; its timings are not
performance measurements.

```powershell
.\toolenv\Scripts\python.exe tools/inspect/verify-geometry-trace.py analysis/native-geometry/replay-final/all/image41 analysis/native-geometry/p1-all --map analysis/native-geometry/image41-map.json --output analysis/native-geometry/p1-all-verdict.json
.\toolenv\Scripts\python.exe tools/inspect/geometry-format-inventory.py analysis/native-geometry/replay-final/all/image41 --dma analysis/native-geometry/p1-all.dma.csv --output analysis/native-geometry/format-inventory-final.json
```

The corpus inventory excludes one second around scripted phase boundaries.
Script milestones require native renderer frame verification: a profile
selection screen must never count as multiplayer-round coverage. Capture
uses absolute paths, creates parent directories, refuses to overwrite an
existing corpus directory, and writes elapsed-time sidecars per job.
