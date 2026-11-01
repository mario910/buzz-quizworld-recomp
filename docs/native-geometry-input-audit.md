# Image41 builtin input audit

The pinned original93F0 path has23 standard non-reporting blocks:17 data
blocks, five shared setup/return blocks and the2760 position-pointer helper.
The six-word95C4 table contains offsets relative to95C4:

| Format | Offset | Data root | Complete data blocks | Current native status |
| --- | --- | --- | --- | --- |
| 0 | 013C | 9700 | 9700,97A8 | Existing complete raw XYZ input; admission limits retained |
| 1 | 1334 | A8F8 | A8F8,AAF8,AB00 | Complete standalone XYZ+2CMP decoder; production/cold gates OPEN |
| 2 | 10E4 | A6A8 | A6A8,A750,A860,A864,A868 | Required native implementation OPEN |
| 3 | 0DCC | A390 | A390,A5D0,A5D8 | Required native implementation OPEN |
| 4 | 08AC | 9E70 | 9E70,A170 | Parameterized packed family; exact byte/numeric map and native implementation OPEN |
| 5 | 0234 | 97F8 | 97F8,9BB0 | Parameterized packed family; exact byte/numeric map and native implementation OPEN |

Shared blocks are93F0/9408/9518/9580/9498. Setup saves25 nonvolatile vectors
r80..r104, r126, r0 and r1, reserves448 bytes, reads the16-bit vertex count,
prepares CMP masks/scales/bias and rounded-even output bytes, then jumps via
95C4. Count0 goes directly to9498. Formats>=6 enter the95DC custom-callback/
descriptor boundary; existing native custom/typed descriptor work is separate.
Builtin payloads finish at9498, which restores the actual late frame and
returns. Only raw format0 calls2760; formats1..5 inline metadata publication.

`tools/inspect/geometry-input-audit.py` verifies the original source pin,
normal-block closure, absence of channel/DMA/stop operations inside that
standard closure, all six selected table entries and its95DC frontier.
`tests/test_geometry_input_audit.py` checks the closure, source mismatch,
all six corrupt table slots and snapshot size. The audit uses the captured
54E8 LS only to pin image41 table constants, not as a real93F0/builtin capture.

## Built-in input format1: complete standalone decoder

Input format1 (`A8F8/AAF8/AB00`) now has a complete standalone native decoder
in `native_geometry_builtin20_input.h`. Each 20-byte source vertex contains
raw XYZ and two CMP11/11/10 words; each pair produces two float4 rows in each
of three destination streams. Position W comes from the actual CD60 constant;
CMP retains shift/mask, signed31 conversion and converted-input-first FMA
operand order. The complete setup, loop and final lookahead are native,
ending at the exact queued9498 PC/function before shared frame restoration.
All128 final guest register vectors, including setup-only survivors, remain
exact. No lifted setup, loop, tail or state repair runs on the native arm.

The shared `native_geometry_builtin_input.h` implements native metadata
reservation/publication and a CMP-vector decoder. Admission's read-only
eight-quad overlay models metadata writes in their actual order: read IDs,
find the matching/free slot with the actual selector, write the semantic ID,
reload the pointer table and publication quad, then publish the selected
pointer using the actual word mask. Three reservations publish semantics1/2/3.
The first IDs read uses incoming r3; subsequent reads use incoming r82.
Slot-table/ID/publication aliases and the no-free-slot original index16
fallback are supported. Eligibility evaluates metadata only, without payload
arithmetic or an original-function clone. Successful admission leaves context
unchanged; rejection also preserves the plan.

The first four source quads are prefetched before metadata writes. A second
40-byte pair is aligned/prefetched after all six metadata stores. Each loop
decodes the cached next pair, writes six ordered destination quads (position
rows, first CMP rows, second CMP rows), then freshly reads four future source
quads, including on the final iteration. Source advances40 bytes, each output
advances32. Only nonfinal iterations copy the source backedge vector. The
future cursor is derived from actual incoming r84, which need not equal r4.

Admission requires owned LS, all enabled/saved/active IRQ contexts absent and
a terminating low r42 counter: negative bytes must be a nonzero multiple32
and at most1,048,576. It checks every actual aligned/LS-masked metadata and
data destination against E680. Destination starts use the actual pointer plus
`r36+r42`, without assuming those vector values cancel. Upper counter lanes
retain their original arithmetic. Data/source/cross-stream and metadata
aliases are preserved, including fresh reads after earlier writes. No parent
frame/control-safety claim follows from this pure-data admission.

Validation passes **5,646 strict full GPR/LS/architectural/channel/control/
PC/function cases**:4,096 broad metadata/source/output/cross-stream aliases,
1,024 arbitrary controls (selector/broadcast, five LS masks, CMP scale/bias,
independent r84 cursor and noncancelling output offsets),256 special-FP scale/
bias cases, boundary counts up to2,048 pairs and three modeled host depths.
**256 independent ordered byte models** include source/metadata publication
overlap, output/source/cross-stream aliases, initial prefetch before metadata
and future prefetch after stores. **8,192 independent CMP words /32,768 float
components** cover every signed11/10-bit code. The expected FMA result is
computed with integer dyadic numerators and explicit24-bit RTZ packing using
the exact stored scale/bias bits; it does not call SPU numeric helpers.
**14 rejects** preserve context, queue and plan.

**3,216 full original93F0 comparisons** pass with **3,168 native strict child
roots**, including48 empty roots, sixteen source/stack/destination biases,
three depths, ignored builtin callback arguments and48 late-frame/return-link
aliases. Shared93F0 setup and9498 restoration remain original on both arms;
only format1 data is native. This is synthetic function integration, not a
newly native enclosing93F0 controller, compiled-app replay or live acceptance.
The first compiled4,099-case suite and expanded suites passed without kernel
arithmetic or checker corrections. The initial header build required replacing
a brace-list range with an explicit array; its failed build log is retained.
Two input-inventory tests and seven conversion-audit tests also pass.

The generator pins the original source and captured54E8 anchor, emits three
private original data blocks plus a distinct9498 trap, and separately emits
an eight-block original93F0 graph with optional native-only data hook and95DC
custom-dispatch trap. The synthetic calibration explicitly supplies a format1
manager/three buffers, then runs original93F0 setup toA8F8. Its GPR/LS SHA256
is `2e75ca50a627366b1ddb692b4f11b9b8742ad07b06f7e450b15140a737f0d81b`. It is not a captured cold input root; architecture/depth are
modeled. Raw captures and all original-derived code remain private.

Run `tests/scripts/test-geometry-builtin20-input.ps1`. The pinned input audit
records23 standard blocks,17 data blocks, the six95C4 routes and the448-byte
shared frame; see [input audit](native-geometry-input-audit.md).
No production routing/switch, ABI, executable, corpus/live/render or FPS/CPU
change is claimed. All builtin-control-v1 frozen files and source/generator/
save/template pins remain unchanged; JOB0/geometry827/newflags0 stay unchanged.
Next: remaining input formats2..5, then production wrappers/native93F0 entry
with parent-safe admission. Before production integration, prove the saved
control pointers used by native callers survive every accepted data write;
pure-data frame aliases alone cannot authorize that parent. Cold real/256-live,
native frames, interpreter, reversed A/B, normalized CPU and default gates
remain OPEN. Phase3/continuing goal stay active; strict StageF phase7 stays LAST.
