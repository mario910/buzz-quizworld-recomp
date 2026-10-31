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

The `0x7118/7298` in-place run-list format is now independently checked for
all 65,536 control encodings. Inputs are r3=aligned LS base, r4=encoded byte
count and r5=expanded halfword count. Each BE16 control `c` emits
`(c & 31) + 1` BE16 offsets, starting at `(c >> 5) * 16` and increasing by 16.
Runs are stored in their encoded order; expansion proceeds backward so it
can overwrite the encoded prefix safely. The native preflight checks that
the sum of run lengths equals r5 and that source/output spans and constant
tables are valid before any mutation. Encoded count zero returns without
reading base/count-dependent LS. The original clears padding in the final
output quad and performs five-quad masked writes plus speculative look-ahead;
the native root preserves those bytes, wrapped reads and full final ABI.
`BUZZ_GEOMETRY_JOB_RUN_LISTS=0/1/2` defaults off. Empty roots have separate
counters and do not establish nonempty data-path coverage.

The opt-in native `0xBF8` list/blend flow preserves two scratch-buffer command
quads at new SP+32/+48, with selector/size/start/capacity words. Weights are
GET into `0xDF00`, eight-byte list descriptors into `0xE100`; count<=128 keeps
these spans within their separate scratch regions. Reverse selection uses
SPU multiplication/comparison with threshold `0x3CCCCCCC`, and skips an entry
when its first two descriptor words are both zero. Selected payloads use
GETL command 68 and tag from manager+0x154. Initial buffer bounds come from
manager+0x58 and manager+0x17C. When the next payload does not fit, its fetch
is deferred until after processing the current payload and resetting buffers;
the optional BE counter passed in r8 is incremented for that reset.
The stream header's first four words define payload sections following its
32-byte header; word at +16 controls attribute processing. Manager+0x78 is
updated to payload+32 before calling `0x7118` then, when nonempty, `0x7DF8`.
The native enclosing loop preserves complete child-call ABI/spills and final
root state. Its `0x7118/7298` run-list decoder has a separate native switch
and the independently checked format described above.

For new synchronous roots that issue I/O, phase 3 additionally provides
`buzz_geometry_job_io_root_begin/end`. Under the JOB=2 native shadow, this
checks the original root from the same full input against a bounded fragment
of already-recorded channel/DMA operations. No guest transfer executes twice;
the parent transcript cursor remains unchanged by the reference recheck.
All GPRs/LS/architectural controls and root return/image/depth/dispatch state
must match before the caller continues. Outside a replay shadow, no ticket
or strict pass is produced. This supplements the final whole-job check and
does not replace a root's corpus, live, render or performance gates.

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
| 8 | `0x9058` -> `0xC810`, variable-bit fixed point plus fixed offsets | Not used by captured primary positions; runtime descriptor dispatch observes 3 additional-stream calls |
| 9 | `0x9114` -> `0x9148/92A0`, 24-bit unit vector | Input normals/tangents |

Storage dispatch is at `0x8354`. Routing IDs observed are 1=position,
2=normal, 3=tangent, 5=UV0, 6=UV1, 9=RGB. Shader slots are respectively
0, 2, 14, 8, 9, 3. UVs use two BE halfwords. Tangents have three or four
components; the fourth sign survives unit-vector decode. CMP output is a BE
word: signed x in bits 0..10, y in 11..21, z in 22..31, normalized by
1023/1023/511 (SDK `rsx_live_draw.c`). Built-in output IDs 0..3 have strides
12/20/24/24; all captured jobs use custom output instead.

All captured input positions are raw BE float3. The native phase-3 raw-stream
reader owns the complete `0x93F0` format-0 path, including small streams,
slot allocation, pointer publication and the return ABI. `0xAC68` calls it
for the primary stream and, when present, the secondary stream. Format bytes
are config bytes 2/3; vertex count is the BE halfword at config+8. The first
zero byte of context+`0x160..0x16F` reserves a table from context+`0x10+4*slot`
and becomes 1. `0x2760` publishes that table pointer at context+`0x5C`.

The raw decoder processes `ceil(vertices/4)` groups. Each consumes 48 bytes
of BE float3 and writes 64 bytes of float4 with exact `0x3F800000` W bits,
including padded vertices in the final group. Its pipelined loop loads an
additional 48 bytes after the final consumed group. Those lookahead values,
the final group shuffles, full vector counters, all stack spills and restored
registers remain observable and must match. The native loop uses integer
SIMD byte rearrangement, preserving NaN payloads, signed zeros and denormals.
Preflight checks the extra lookahead, all destination bytes, first free
slot, dispatch/selectors and disjoint context/stack/input/output ranges
before any mutation. A zero vertex count takes the original no-data path,
without inspecting unused format/source values.

`0xAC68` publishes the primary input at context+`0x54`. Normally its final
context+`0x58` value is the lower non-null input pointer. Config byte 0 bit
16 instead requires no secondary stream and uses the output stride selected
by config byte 4, times vertex count, aligned to 16. The stride getter stays
an explicit original callee in that increment. Initially, custom descriptor
decoding (`0x7DF8`) and built-in formats 1..5 retained their original complete
bodies; subsequent descriptor/data sections record their native increments.
Native leaves alone do not establish native whole-decoder coverage.
The `raw-stream` counter reports its retained paths as fallback, separately
from native `attributes` control coverage.

The subsequent native custom dispatcher covers the other input format seen
in the corpus: 255. Audited configuration-byte pairs are `(0,255)` in 18,936
jobs and `(0,0)` in 572; built-in formats 1..5 have no real corpus occurrences.
The former 11,414 `raw-stream` fallbacks are custom-dispatch calls, not evidence
for built-in formats 1..5. `BUZZ_GEOMETRY_JOB_CUSTOM_STREAM=0/1/2` independently
replaces the nonempty custom path when argument r8's callback is zero and r7
supplies a descriptor. It saves/restores the complete original 448-byte frame
and all ABI vectors, then queues `0x7DF8` with r6=original descriptor argument,
r7=vertex count and r8/r9=zero. All state and the exact pending callee are
checked at that tail handoff, before the descriptor frame can overwrite the
old spills. Descriptor iteration/data keep their own switches and checks.
Custom callbacks/error paths and unimplemented built-in formats retain their
original whole-stage fallback before mutation. With custom dispatch enabled,
the combined corpus has 19,508 native raw paths and 11,414 native custom
handoffs, both without fallback. Built-in 1..5 gates remain separate work.

In the static fixed-point
path, attribute byte 6 selects an extra 8-byte width record relative to
`descriptor+8`. Its four pairs are `{integerBits, fractionalBits}`; tables
`0xD230/0xD240` select odd/even bytes. Components consume integer+fractional
bits from the packed vertex. A separate 16-byte record supplies BE u32 fixed
offsets, added to each extracted unsigned component with word wrap before
separating integer/fractional parts. `0xC910` uses CSFLT
155 for the signed integer and CUFLT 123 for the shifted fraction, then FA.
Those instructions, including shifts and padding accesses, remain the
authoritative contract; the native region and its validation are described below.

### Descriptor iteration, routing and blend inputs

The native `0x7DF8` control saves/restores the original 1,056-byte frame and
all 48 nonvolatile vectors. Its descriptor header supplies attribute count
and interleaved stride; the vertex count is argument r7. The stride's first
four trailing-zero tests determine the number of processing columns and the
source/output group steps. These shapes are part of the typed-handler ABI,
not just a description of the logical vertex stream.

For ordinary decoding, the first free byte of context+`0x160` selects a
float4 table and later records its routing ID. Routing IDs 1..4 publish the
selected pointer at context+`0x5C`, `+0x60`, `+0x64`, `+0x68`; other IDs still
mark their slot without changing those pointer fields. The position-only
context bit skips attributes whose routing ID is not 1. Position attributes
with at most three components fill missing lanes with `(0,0,0,1)`; the
blend path uses zeros instead. Padding covers the rounded four-vertex range
and loads another 64 bytes at the end for the original return state.

A positive SPU blend factor in r8 selects the existing slot for each routing
ID and decodes into the temporary table selected by the context's low nibble.
Missing routing IDs are skipped. This path requires context bit `0x80`;
otherwise the original asserts. Argument r9 is a packed BE-u16 **byte-offset
remap**, not a component mask. `0xC750` scatters the weighted temporary data
into the existing geometry table. Its destination count comes from the
geometry context and may exceed the additional stream's r7 count. Preflight
validates the used remap entries, destination ranges, immutable descriptor/
context/frame data and remap/output separation before mutation. The blend
data kernel itself remains a separately tracked original callee.

The new runtime dispatch inventory covers the actual `0x7DF8` paths, including
additional/blend streams, rather than only the primary A/B format inventory:

| Storage ID | Observed handler selections in 19,508 jobs |
|---|---:|
| 1 / 5 / 6 / 7 | 0 each |
| 2, BE float32 | 12 |
| 3, binary16 | 12,431 |
| 4, normalized U8 | 109 |
| 8, variable-bit fixed point | 3 |
| 9, packed unit vector | 12,067 |

These are callback-selection counts, **not** native-codec coverage. They span
12,309 descriptor-control calls. The three fixed-point dispatches refine the
earlier primary-position-only observation: all primary positions remain raw
format 0, while additional streams also reach the fixed-point decoder.

### U8 normalized-color decoder and physical stores

The storage-4 descriptor handler starts at `0x89B0`, iterates columns at
`0x89E8`, chooses cross-quad/within-quad loops `0x8A60`/`0x8B08`, and exits to
the descriptor decoder's common continuation at `0x83C8`. It is a region of
`0x7DF8`, not a callable root: replacing it must preserve r0, host depth and
the scheduled continuation rather than issuing an SPU return.

The first column-group preload always gathers from two consecutive quads.
Subsequent groups use the descriptor byte-size boundary test. Within-quad
gathers repeat the same quad as both operands: an unused fourth RGB byte may
therefore wrap to byte 0 instead of reading the next quad. All four lanes
still undergo CUFLT 139 and the original FMA with the scale/bias vectors.
Their values and unused output lanes remain part of the exact contract.

Logical output cursors may carry a one-byte bias. Comparisons use that full
logical address, while STQD rounds the physical write down to 16 bytes.
There is a final iteration beyond the preferred output limit, whose writes
use the scratch/sentinel vectors in row order; the next input group is still
loaded. Preflight and native host batching must account for those reads and
the order of overlapping scratch writes, including columns with no ordinary
output destination. Per-column final ABI materialization is needed because
the two loop variants leave different volatile registers behind.

### Remapped weighted attribute scatter

The complete `0xC750/C7A8` root now has an opt-in native implementation in
`native_geometry_blend_scatter.h`. Arguments are remap base r3, destination
base r4, source end r5, component mask r6, negative source byte cursor r7 and
weight vector r8. A supported cursor is a negative multiple of 32, up to
4,096 pairs. The loop processes two source vectors per iteration until the
preferred cursor reaches zero; non-preferred cursor words still advance.

Each BE remap word contains two halfword byte offsets. The low halfword
selects source B's destination; the high halfword selects A's. The original
adds the full packed preferred word before LQD/STQD masks the physical LS
address. The native plan retains that behavior, pointer bias and wrapping.
Quad loads align down before the map's offset-dependent rotation. Source
reads extend two pairs (64 bytes) beyond the nominal source end; remap reads
include two trailing pairs, and the next destinations are also prefetched.
That final read-only destination prefetch may address any physical LS quad,
including the lower code/table area; it is not a store destination.

Destination B for the next pair is read before either current store. Current
B is stored first, then next A is read, then current A is stored. Therefore
repeated destinations and source/output overlaps retain their original
pipeline behavior. They cannot be replaced by independent sequential adds.
The native host loop carries the prefetched values and pending FMA results;
only its final iteration materializes the complete register ABI. It supports
in-place source/output aliasing, while writes overlapping any prefetched
remap quad reject before mutation so the predecoded map remains immutable.

`BUZZ_GEOMETRY_JOB_BLEND_SCATTER=0/1/2` selects original/native/strict root
comparison, independently and default-off. Mode 2 works with JOB=1 or JOB=2,
comparing all GPRs, LS and architectural controls plus host depth, image and
the exact r0 return/trampoline. This is a callable root, unlike the typed
regions' 0x83C8 continuation. The checker drains by original tail identity,
including when r0 itself equals 0xC7A8, and never repeats guest DMA or atomics.

### Remaining typed descriptor data: SNORM16, I16, CMP, U8 integer

The full storage-1/5/6/7 regions now have native payload implementations in
`native_geometry_{snorm,i16,cmp,u8_integer}_stream.h`. They share checked
layout and bounds in `native_geometry_typed_stream.h`; each keeps its own
register ABI, counter and default-off `BUZZ_GEOMETRY_JOB_*_STREAM=0/1/2`
switch (SNORM, I16, CMP, U8_INTEGER). Mode 2 compares all registers, LS,
architectural controls and exact 0x83C8 continuation, including with JOB=1.
They return immediately for preferred r121=0 without touching unused pointers.

SNORM16 uses D910 to generate a gather selector, then a second shuffle.
D910's 0xFF bytes become selector 0x80, so the final low halfword is **zero**.
The signed high halfword is converted with CSFLT 139, then FMA with r93/r92.
I16 uses that same high-halfword expansion and CSFLT 139, without FMA.
U8 integer uses D8C0 to place each source byte in bits 16..23 and CUFLT 139.
This is separate from the normalized U8-color handler.

The crossing predicate is `(columnOffset&15)+r25>16`. For I16 and U8 integer,
the within-quad path wraps every group's unused components in the source
quad. SNORM16's setup instead gathers the **first group across two quads in
both paths**; later groups of its within-quad path wrap. Setup-only and
path-specific volatile state from earlier columns must survive exactly.
All three implementations materialize each column's final ABI; there is no
original setup/loop/final-column callback. Source/lookahead, output, per-lane
sentinels, immutable stack metadata and nonaliasing are checked before writes.

CMP always gathers four consecutive packed bytes through DA80, including
across a quad boundary. The gathered overlapping halfwords are shifted by
r90, masked by r100, converted with CSFLT 124 and transformed with r99/r98 FMA.
The fourth gathered word is zero; its result still follows those parameters.
The initial r25 is unused and is not validated as a branch width. Its native
host data loop preserves arbitrary shift/mask/scale/bias vectors, current
and prefetched values, and each final register. Every family writes one
additional group after the preferred limit, with row store order 0,1,2,3
and the corresponding r82 sentinel lane for clipped rows.

### Variable-bit fixed-point descriptor attribute stream

Storage 8 uses the complete `0x9058/C810/C830/C910` region. The native
`port/native_geometry_fixed_stream.h` extracts four sequential MSB-first
components with per-component widths 0..32, adds the BE word offset with
32-bit wrap, then separates the signed integer and unsigned fraction.
CSFLT 155, CUFLT 123 and FA retain the exact SPU truncation behavior.
The first width byte is the integer part, the second the fraction.

The width record is selected through r84, the SP+0xE0 shuffle and the
SP+0x110 base. Its eight bytes rotate inside the loaded quad, including
records crossing its end. SP+0xD0 points at a separate sixteen-byte offset
record and advances by sixteen on entry; this LS update remains observable.
Metadata aliasing that update is rejected before mutation.

The original loop processes **r107 + 1 columns**, including r107=0. Each
column writes one group after the preferred output limit and prefetches the
following input. Stores occur in row order **2,3,1,0**, using the corresponding
r82 sentinel lane for clipped rows. There is no r121=0 shortcut. The return
link stays 0x9110 and the enclosing descriptor continuation is 0x83C8.

Host loops own all payload groups. Only the final column/group materializes
the complete register ABI; no original setup/loop/final-column callback is
used. `BUZZ_GEOMETRY_JOB_FIXED_STREAM=0/1/2` selects lift, native or strict
region comparison, independently and default-off. Mode 2 checks every GPR,
LS byte, architectural control and continuation with either JOB=1 or JOB=2.
Preflight bounds source/lookahead/output/sentinel ranges and protects stack,
width and offset metadata before mutation. Unsupported inputs retain the lift.

### Float32 descriptor attribute stream

Storage 2 uses the complete `0x87CC/8800/8878/8910/8928` region, separate
from the primary format-0 raw float3 path. Its native implementation in
`port/native_geometry_float32_stream.h` is independently controlled by
`BUZZ_GEOMETRY_JOB_FLOAT32_STREAM=0/1/2` (default 0): lift, native or strict
region comparison. Mode 2 also works with the native whole-job JOB=1, so rare
round paths can be checked without the whole-job checker slowing navigation.
The per-region fields and comparison remain identical to JOB=2. Host byte-vector batches
copy the raw payload without floating-point arithmetic, preserving zero,
denormal, extended and NaN-like bit patterns.

The descriptor byte width in r25 selects between two paths for each column:
`(offset&15)+width>16` copies sixteen bytes across adjacent input quads;
otherwise all sixteen result bytes rotate within the same aligned input
quad. Unused components also retain this circular behavior. Both paths
preload the next group, write one final group past the preferred limit and
preserve every surviving setup/loop register. Their disjoint volatile-register
sets require retaining state left by an earlier column's other path.

Unlike the color/unit/half paths, float32 shifts each row cursor before
clipping it against the same sentinel vector. Every clipped row therefore
uses r82's preferred word; the other sentinel lanes are not physical store
addresses. Row store order is 0,1,2,3. Logical byte bias is retained until
physical STQD alignment. Setup-only registers and unused following-quads on
the within-quad path remain anchored to group zero, not the final group.

Preflight validates shapes, gather registers, source plus lookahead,
physical output/sentinel bounds and immutable stack metadata before writes,
allowing the designated SP+0x20 sentinel. The r121=0 immediate continuation
preserves state without inspecting unused pointers. JOB=2 additionally
compares the complete region at 0x83C8 before common descriptor code can
hide a volatile-state difference.

### Halfword attribute stream

The complete storage-3 region (`0x8378` -> `0xCAC0/CAD8/CB78`) now has native
data decoding in `port/native_geometry_half_stream.h`, controlled by
`BUZZ_GEOMETRY_JOB_HALF_STREAM=0/1` (default 0). Four source rows are decoded
in eight-component AVX2 pairs; host loops own every column and group. The
last group reconstructs the entire pipeline ABI natively, without calling
the original setup, loop or last-column function.

Preserve the lift's table composition rather than using an IEEE half
instruction. For each component, let H be its BE16 source halfword and B the
first byte of that row's enclosing aligned input quad. The composed gather
forms `W=(H<<16)|(B*0x101)`. The decoded word is
`(signed_shift_right(W,3)&0x8FFFFFFF) + ((W&0x7FFFFFFF)>0 ? 0x38000000 : 0)`.
Thus low output bits depend on B, including nominal half zero/denormal and
special encodings. All 65,536 H patterns are compared against the original
pipeline with independently varied B and full observable state.

Each iteration preloads its next input group before stores. The final
iteration is one past the preferred output limit and writes clipped rows
to the sentinel; stores occur in row order 1,2,0,3, including aliases.
Logical cursor bias is retained until physical STQD alignment. Ten surviving
setup registers (r70..r79) still describe group zero, not the last group.
The native path reproduces r0's 0x83C8 link and the complete region exit;
JOB=2 checks it before the generic descriptor continuation overwrites state.
Preflight validates source/lookahead, tables, output/sentinel extents and
immutable stack metadata before writes, allowing the designated SP+0x20
sentinel. The handler uses the r107 column count; unrelated caller registers
do not justify skipping it. Zero/unsupported column counts retain the lift.

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

The complete storage-9 region (`0x9114/9148/92A0`) now has a native
implementation in `port/native_geometry_unit_vectors.h`, independently
controlled by `BUZZ_GEOMETRY_JOB_UNIT_VECTORS=0/1` (default 0). Host loops
process columns and batches of eight decoded vectors. The final two groups
materialize the pipeline ABI with native SPU math and loads/stores; no
original setup or final-column callback is used on the supported path.

Logical output cursors may be biased within an aligned quad. Compare them
against the signed preferred limit before applying physical STQD alignment;
out-of-range rows write the context's sentinel in the original row order.
The last group is one past the preferred limit, and its final iteration
reads two further input groups. Those padded bytes may contain reserved
selectors. They are not active unit vectors: native ABI reconstruction uses
the actual sixteen selector-table outcomes for these final reads, retaining
every speculative register and scratch-store byte. Only the bulk batches
require selectors 0..11. Five setup-only registers (2,23,46,47,48) retain the
first group's values even when the final group contains different data.

Preflight validates the complete source/lookahead range, math/gather and
selector tables, output/sentinel physical extents, per-lane steps and
immutable stack metadata before any mutation. The designated stack sentinel
at SP+0x20 is allowed; other stack metadata aliases retain the lift. Zero
vertices preserve the original immediate continuation without inspecting
unused pointers. JOB=2 additionally compares the complete region at 0x83C8,
before common descriptor code can overwrite volatile state.

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

The phase-3 [index implementation audit](native-geometry-index-audit.md) adds
the exact frame/continuation contract, the four error frontiers, observed
width cohorts and the eight independent u16 delta chains with warmup and
lookahead. Its scalar reconstruction matches all registers/LS of one captured
7770 call. This is format evidence; full native-decoder acceptance remains open.
The subsequent complete native `7730/7770` region uses eight u16 chains,
preserves warmup, cached lookahead, aliases and all final GPR/LS state, and
queues 77F8 without original setup/step/final execution. Its own
`BUZZ_GEOMETRY_JOB_INDEX_DELTAS=0/1/2` defaults to 0; full-state synthetic,
real-corpus and live evidence is in the [phase-3 results](native-geometry-phase3-results.md#complete-index-delta-reconstruction-77307770).
Other index data/control regions remain separate unfinished implementations.

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

## Skin/rigid controller boundary (phase 3)

`0x9F8` uses a 160-byte stack frame and saves r0 at old SP+16 and r80..87
at old SP-16..128. A zero preferred low byte in input r9 selects weighted
skinning, swaps r5/r6, restores the frame and queues `0xAF18`. Full-word
nonzero tests are incorrect for values such as `0x10000`.

The rigid path calls `0x2750/2778/2780` for position/normal/tangent pointers,
then uses the manager header's BE16 vertex count at +8. C518 transforms
positions using input r7 and secondary vectors using input r8. Input r10's
bit zero disables normal/tangent work when set. Non-null normal data gets a
synchronous 2C00 normalization; tangent normalization is the final queued
2C00 tail after frame restoration. A missing/disabled tangent returns through
r0. All argument lanes and original call-entry PCs remain observable.

The independent native controller/checker stops before AF18 or the final
normalization tail and compares all GPR/LS/control/continuation state. Its
weighted handoff count is **not** native palette/skin data coverage. AF18's
672-byte frame, palette transpose and batch/directional orchestration remain
explicit native implementation/validation tasks. The complete B880 data
replacement is recorded in the phase-3 results below the controller milestone. Stage F's
strict reconstruction remains the final roadmap phase.

### AF18 audit and complete weighted data kernel

Reproduce the pinned 38-block dependency map and six-way directional table:

```powershell
.\toolenv\Scripts\python.exe tools/inspect/inspect-geometry-skin.py
```

Private output: `analysis/native-geometry/phase3/skin-audit.json`. The tool
pins the original source and the real LS-table snapshot; excludes unreachable
C500/getter tails, includes the 19 indirect table entries, and verifies the
ordinary library functions have no channel sites or further calls.

The follow-up audit corrects the earlier palette/DMA placeholder: the valid
`0xAF18..0xC500` skin path performs **no channel/DMA operations**. Input
palettes and weights have already been staged by the job loader. Its only
ordinary direct calls are the four stream-pointer getters and `0x2738`
palette publication. CC70 is the rejected error/report path. The lexical
C518 tail after C500's unconditional B24C branch is unreachable.

AF18 saves a 672-byte frame, transposes each 48-byte palette matrix in place,
chooses output streams and a scratch capacity, then batches vertices through
position/matrix kernels and directional kernels. B4C0's 19-entry relative
LS table at B554 maps selectors 0/1/2 to BD4C/BE20/BF7C, selectors 3..15 to
B868 (batch continuation), and selectors 16/17/18 to C14C/C280/B5A0.
The six directional roots remain separate data regions. Basic BD4C/BE20/
BF7C now have native implementations (coverage below); normalized C14C/C280/
B5A0 and BB58/BB68 cofactor/normal data remain original. Replacing the outer
controller or B880 alone does not replace these dependencies.

The complete opt-in B880/B934/B940 native kernel uses a host AVX2 prefix
and a native final-pair calculation with explicit full GPR reconstruction.
It calls no original setup/iteration/final block. Each vertex consumes four
unsigned byte weights and four unsigned byte indices. All indices are read,
including those whose weights are zero. Accumulation is FM then three FMAs
in influence order with RTZ scale `0x3B808080`. Three palette row bases can
be distinct; original quadword-floor addressing is preserved for all pointer
biases. Positions are updated in place and three 16-byte matrix rows are
written per vertex. Row zero's W becomes 1; the other row W values become 0.

The pipeline reads a final pair of positions and 16 bytes of weights beyond
the active vertices; their raw bits, gathered coefficients, offsets and all
unused pointer/count lanes survive in the boundary ABI. A positive even
count includes a single pair. The region stops at B4C0 or BB58 according to
the exact XSBH result of r102. Both data tails are separate and never run
twice by the strict checker. Preflight rejects foreign LS, unsupported
constants/counts, out-of-bounds reads/writes and aliases of writable streams
with position/weight inputs before mutation. Palette/output aliases are
supported: all six rows for a pair are gathered before any write, and the
following pair sees the previous stores. Correctness/render and remaining gates
are tracked in the separate phase-3 results.

### Basic skin direction data

The checked phase-1 trace has BD4C=1,158 entries, BE20=3,860 entries, and zero
entries to BF7C, BB58, C14C, C280 or B5A0. The skin audit now records these
counts, their trace-entry CSV hash and the validated 19,508-job/58,195-transfer
trace denominator. The single/two/three-stream native family keeps distinct
counters; zero real BF7C calls do not satisfy its real/live gates.

BD4C/BE20/BF7C transform four vertices per group through three matrix rows,
using FM then two FMAs, without normalization. The first primary stream's
raw and shuffled vectors and first matrix group are **already in GPRs**.
r40 points to the next matrix group, not the first. Reloading these initial
values from LS would change the function contract. Secondary/tertiary streams
are read from r71/r93. Each iteration loads four following primary vectors
and twelve following matrix quads before any stores. These lookahead values
survive in registers even after the final group.

The native host loop caches these values explicitly, gathers secondary
streams before writes, uses AVX2 pairs for the arithmetic, and writes streams
in the original stream order. It supports aliases between streams/matrix
scratch and a following group's inputs, quadword-floor pointer biases,
arbitrary selector bytes and different first-write/nominal-primary pointers.
Each variant materializes its distinct FM/FMA intermediates, raw secondary
vectors, final pointers/count lanes, and the exact queued B868 continuation.
No original setup, data iteration, step or final group is called. The B868
batch continuation and AF18 caller were separate control work at this
milestone; the following section implements that control.

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


### Native AF18 palette and batch orchestration

`native_geometry_skin_flow.h` replaces AF18's frame/early-exit handling,
B078/B100 palette loop, stream compaction, scratch selection, batch clamps,
B4C0 indirect direction dispatch, B868 continuation and palette publication
sequence. Retained position/direction/getter functions are explicit library
callbacks. Native weighted and basic direction data run under their own
switches; rigid, cofactor and normalized data remain separately tracked
original implementations. Controller counts never imply native data coverage.

Each palette record has twelve words. With input words numbered 0..11, the
three output quads are `[0,5,10,3]`, `[1,6,8,7]`, `[2,4,9,11]`. The host loop
writes only the active records. The original pipeline additionally reads two
following matrices and computes the next unstored record; those values and
all unused vector lanes are reconstructed in the return/callee ABI. Pointer
biases retain the original quadword-floor addressing.

The 672-byte frame holds a backchain, stream table and saved registers.
Scratch defaults to frame+80 with four matrices; for rounded vertex counts
above eleven, the manager's selected pool pointer is used, with capacity
`(rounded_vertices/3)&~3`. Scratch writes cannot touch protected spills;
the lookahead may read those spills, as the original does. Preflight validates
actual read/write spans, manager/frame protection and the fixed B554 table
before mutation. CC70 error/report inputs remain original. Early exits do
not inspect unused palette, stream, weight or table fields.

Data trampolines stop by exact B4C0/B868 callee identity, with original entry
PC, register lanes, link and host depth. Rigid data begins after B348's clamp
and weighted branch, avoiding a second execution of original control.
The five ordinary pure-LS calls preserve their original calling convention.
`BUZZ_GEOMETRY_JOB_SKIN_FLOW=0/1/2` defaults to zero; JOB=2 also forces a
strict full-function clone comparison through the restored-stack/r0 return.
Full LS, all GPRs and architectural/control/continuation state are checked
without exclusions. Protocol 22 has 28 independent stage counters.

The independent tests compare all 128 GPRs at every library call as well as
full exit state, covering arbitrary palette shuffles/biases, all six direction
targets, rigid/weighted batching, inline/external scratch, early exits, return
PC collisions, three host depths and interrupt states. Separate numeric
checks validate the packed matrix word mapping and untouched padded records.
Real replay/live/render evidence is recorded in the phase-3 results.


### Native rigid skin position/matrix data

`native_geometry_rigid_skin.h` replaces the data suffix of B348 and the
B3C0/B3C8 pair loop, stopping at the exact B4C0 or BB58 data boundary.
B348's three clamp/weighted-branch operations remain caller control: native
AF18 owns them when SKIN_FLOW is enabled; the original caller can also use
the same independently switched data hook. No original setup, step, data
iteration or final pair executes on the native data path.

Each vertex uses one unsigned byte palette index. The original packs two
indices into 16-bit halves, multiplies by 48 and rotates the packed offsets
for its two gathers. Loads retain quadword-floor/LS-wrap semantics, including
odd pointer biases where ROTQBY wraps inside the loaded index quad. Three
row bases can differ. Packed palette rows are reconstructed with the incoming
masks/shuffle tables; position arithmetic is three ordered FMAs, starting
from the translation. Matrix row zero receives W=1 and the other rows W=0
under the standard masks. Arbitrary incoming masks/shuffles remain supported.

The host prefix keeps two positions/Y/Z shuffles and the packed next indices
cached. Every pair gathers all six palette rows and reads the next position
and index group before any write. This preserves overlapping position,
index, palette and matrix-output buffers, including feedback into later
pairs. Stores complete the six matrix quads before the position pair.
An explicit final-pair calculation materializes all volatile intermediates,
raw lookahead, pointer/count upper lanes and the exact queued data tail.
The index stream advances by the vertex count, not twice that count.

Preflight accepts positive even counts up to 4,096, physical LS data bases
and bounded actual writes/lookahead. Gather reads can wrap LS as the lift
does. The potential-alias counter conservatively covers the complete index
quad footprint, possible palette gathers and written spans; it is not an
exact count of dynamically aliased palette reads. Unsupported inputs return
to original data before mutation. The caller's control and cold cofactor or
normalized data do not become native data coverage through this hook.

`BUZZ_GEOMETRY_JOB_RIGID_SKIN=0/1/2` defaults to zero. Mode 2 or JOB=2 compares
all GPRs, all LS, architectural/control state, PC and exact queued boundary
against the original data closure. Runtime job execution establishes RTZ/
DAZ/FTZ in `spurs_job.c`; isolated numeric/integration tests use that same
host FP environment. New stage 28 / protocol 23 has 29 stage counters. Every
stage's first owned fallback is now explicitly logged and flushed immediately,
so abrupt scene shutdown cannot silently omit the first unsupported call;
this behavior is tested with `_Exit` before normal final summaries.

Synthetic and real-coverage distinctions, frozen artifacts and current
validation gates are recorded in the phase-3 results document.


### Native cofactor normal data (BB58/BB68)

`native_geometry_skin_cofactor.h` replaces the cofactor transform and normal
normalization through its exact queued B4C0 boundary. A group has four normals
and twelve matrix quads. All current-group inputs are loaded before stores;
there is no following-group lookahead. Normal/matrix overlaps and feedback
into later groups retain original ordering. Sequential matrix reads can wrap
LS; normal writes remain bounded physical data writes. Pointer biases and
all unused vector lanes are preserved.

With three packed matrix rows A/B/C and the incoming cyclic shuffles Y/Z,
the three packed cofactor rows are:

- `Y(A)*Z(A) - Z(C)*Y(B)`;
- `Z(B)*Y(B) - Y(C)*Z(A)`;
- `Y(C)*Z(C) - Z(B)*Y(A)`.

Each subtraction is an ordered FMS with a separately rounded FM subtrahend.
Normal transformation uses FM followed by two FMAs. Host AVX2 pairs use the
existing exact RTZ arithmetic/repair helpers, with sign-flipped C for FMS.
The four transformed normals are packed into the original length lanes,
squared with FM+two FMAs and normalized using the exact SPU FRSQEST/FI steps
and incoming broadcast tables. Arbitrary masks/shuffles remain supported.
Prefix groups keep intermediates in host locals; a fixed final-group
calculation materializes every original GPR, intermediate and pointer/count
lane. No original setup/body/final group executes on the native data path.

`BUZZ_GEOMETRY_JOB_SKIN_COFACTOR=0/1/2` defaults to zero. Mode 2 or JOB=2 runs
a complete original data closure on a clone and compares all GPR/LS bytes,
architectural/control state, PC, function continuation, image/depth/policy/
overlay. Stage 29 / protocol 24 has 30 stage counters, with separate one-group,
bulk and matrix/normal-alias native/strict counts. Unsupported inputs are
rejected before mutation and never credited as native success.

The tests retain strict byte-exact comparisons for arbitrary FP bits,
SPU extended values, zeros/denormals, aliases, wrapping and arbitrary tables.
Additional geometric checks compare an inverse-transpose of diag(2,3,4)
against independent double arithmetic, using 5e-4 numeric tolerance solely
for that additional semantic check because SPU reciprocal root is approximate.
This tolerance is not used by any strict state/corpus/live checker.
Synthetic AF18 composition also compares actual original/native cofactor
data after either original or native rigid data, including scratch/normal
feedback. Directional data remains explicitly modeled in that integration
harness; its own implementation/coverage is tracked separately.

The real BB58 trace is empty. A compatible corpus replay or normal scene
with no new data calls cannot satisfy this kernel's nonzero real-corpus or
>=256 strict live gate. Frozen evidence and open gates are recorded in the
separate phase-3 results.


### Native normalized skin direction streams

`native_geometry_skin_normalized.h` replaces C14C/C154/C158 (one stream),
C280/C288/C290 (two) and B5A0/B5A8/B5B0 (three), through exact queued B868.
The first primary raw/Y/Z values and twelve matrix quads are already in GPRs;
these caches are consumed as supplied, even when different from LS. r40 points
to the next matrix group. The initial primary write base r33 can also differ
from nominal r67. Following steps reset r33 to the advanced r67; native host
loops preserve both addresses and all unused pointer/count lanes.

Each group reads secondary/tertiary inputs and the next four primary values
and twelve matrix quads before any store. Primary, secondary and tertiary
streams are written in that order, preserving stream/matrix aliases and
feedback. Next primary/matrix reads can wrap LS; actual stream writes remain
bounded physical data writes. Arbitrary incoming masks/shuffles are supported.
The host prefix uses AVX2 FM+two FMAs per transformed vector, then packs four
lengths with the original tables and applies exact SPU FRSQEST/FI and scale
broadcasts. The final group explicitly materializes each variant's distinct
full ABI, including raw lookahead and result-valued scratch registers.
No original setup, step, body or final group runs on the native data path.

`BUZZ_GEOMETRY_JOB_SKIN_NORMALIZED=0/1/2` defaults to zero; mode 2 or JOB=2
compares all GPR/LS bytes, architectural/control state, PC, queued function,
image/depth/policy/overlay against the complete original region. No Stage F
exception or tolerance is added. Stage 30 / protocol 25 has 31 stage counters,
plus per-stream native/strict/alias categories. Unsupported inputs fall back
before mutation without native credit.

Strict tests cover arbitrary cached values, FP bits/tables, stream/matrix
aliases, wrapped lookahead and maximum 4,096 vertices in each variant. Separate
geometric checks verify normalized diag(2,3,4) transforms and deliberately
different first-group caches, with unchanged padded values. Their 5e-4 tolerance
is only an additional geometric sanity check; every strict checker remains
byte-exact. AF18 integration uses actual normalized data and three combinations
of original/native rigid and cofactor kernels, including scratch feedback.
The original real trace has zero calls to all three normalized roots; real
native-corpus and >=256 live gates remain OPEN until actual coverage exists.
