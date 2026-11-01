# Image41 builtin input audit

The pinned original93F0 path has23 standard non-reporting blocks:17 data
blocks, five shared setup/return blocks and the2760 position-pointer helper.
The six-word95C4 table contains offsets relative to95C4:

| Format | Offset | Data root | Complete data blocks | Current native status |
| --- | --- | --- | --- | --- |
| 0 | 013C | 9700 | 9700,97A8 | Existing complete raw XYZ input; admission limits retained |
| 1 | 1334 | A8F8 | A8F8,AAF8,AB00 | Complete standalone XYZ+2CMP decoder; production/cold gates OPEN |
| 2 | 10E4 | A6A8 | A6A8,A750,A860,A864,A868 | Complete standalone XYZ+CMP+signed16x4 decoder; production/cold gates OPEN |
| 3 | 0DCC | A390 | A390,A5D0,A5D8 | Complete standalone XYZ+3CMP decoder; production/cold gates OPEN |
| 4 | 08AC | 9E70 | 9E70,A170 | Complete standalone fixed12 XYZ +2 packed-unit decoder; production/cold gates OPEN |
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

## Built-in 24-byte inputs: complete standalone formats 2 and 3

Both 24-byte input families now have complete standalone native decoders:

| Format | Original blocks | Input vertex / output streams | Header |
| --- | --- | --- | --- |
| 2 | `A6A8/A750/A860/A864/A868` | raw XYZ +CMP11/11/10 +signed16x4 /3float4 streams | `native_geometry_builtin24_input.h` |
| 3 | `A390/A5D0/A5D8` | raw XYZ +3CMP11/11/10 /4float4 streams | `native_geometry_builtin24_cmp_input.h` |

Each kernel replaces every setup/data/backedge block and queues the exact
9498 PC/function before common frame restoration. Host-local vectors retain
the pipeline, all128 final GPR vectors and all LS/architectural/channel/
control state. No lifted setup, loop, tail, tolerance or state repair is used.
Both reuse the native metadata overlay and CMP-vector decoder from input1.
Input2 adds exact signed16 conversion (`csflt139`) and converted-input-first
FMA with the actual scale/bias; all four components, including W, are decoded.

Each iteration reads a physical48-byte source pair from three aligned quads
and produces two float4 rows per stream. Source pointers retain their raw
bias in the final ABI; source data reads use the original quad alignment,
without input1's byte-alignment shuffle. Initial source quads are loaded
before metadata publication. The next pair is prefetched after all metadata
stores; every subsequent fresh pair follows all six (format2) or eight
(format3) ordered data stores, including on the final iteration. Input advances
48 bytes and each output32 bytes. Nonfinal source-cache copies are explicit;
format2's intermediate nop block is absorbed without changing final state.

Admission reads only metadata/control footprints. It requires owned LS,
all enabled/saved/active IRQ contexts absent, and a terminating negative
low r42 count of1..1,048,576 bytes divisible by32. Every actual quad-aligned,
LS-masked metadata/data store must stay at or above E680. Data destinations
use the actual slot pointer plus r36+r42; cancellation is not assumed. Metadata
source/destination addresses follow each original family:

- Input2: first IDs read/write through r3, second read through r3 **but write
  through r82**, third read/write through r82. Three reservations publish
  semantics1/2/3 via incoming r35/r37/r61.
- Input3: first two IDs read/write through r3, last two through r82. Four
  reservations publish semantics1/2/3/4 via r35/r37/r61/r39.

The shared reservation helper now accepts a distinct IDs store address;
the existing same-address overload retains input1 behavior. Input2's first
suite reached the split-manager case at trial5635 and detected incorrect
GPR/LS output from writing the second IDs quad to its read address. The helper
and input2 caller now preserve the original separate read/store destinations.
All input2 cases then pass, and the complete input1 regression also passes.
The numeric/checker contracts remain unchanged. The failed comparison log is
retained. Input3's complete first suite passes without runtime corrections.

Validation passes **5,678 strict full-state/LS/PC/function cases for input2**
and **5,710 for input3**. Each suite includes4,096 broad metadata/source/output/
cross-stream aliases,1,024 arbitrary-control cases,256 special-FP coefficient
cases,32 split-manager cases, boundary counts up to2,048 pairs and three
depth anchors. Input3 additionally has32 fourth-publication/IDs aliases.
Each has **256 independent ordered byte models**, including source/metadata
overlap and post-store prefetch, plus **8,192 independent CMP words /32,768
float components**. Input2 additionally covers **all65,536 signed16 codes /
262,144 independent components**, checking both the full converted float and
scaled result. Its expected fused result uses the exact integer numerator
`0x800080*(2*signed16+1)*2^-39` with explicit24-bit RTZ packing, without SPU
numeric helpers or a floating-point tolerance.

For each format, **3,216 full original93F0 cases /3,168 native strict data
children** pass:48 empty roots, all sixteen source/stack/destination biases,
three host depths, ignored builtin arguments and48 late-frame/return-link
aliases. Only the selected data family is native; original setup and9498
restoration remain on both arms. Input2 has14 unchanged rejects; input3 has17,
including fourth-publication and fourth-stream protected writes. Rejection
preserves context, plan and incoming function queue. Two input-inventory and
seven conversion-audit tests also pass.

The private references are the five/three original data blocks with distinct
9498 traps and separate ten-/eight-block original93F0 graphs with native-only
data hooks and95DC traps. Calibrations mutate the synthetic manager format
argument and execute original setup; they are not captured cold roots.
Reproducible GPR/LS SHA256:

- Input2: `9a584efd407eebf6c93d85585d9b0943e88013ab8e2156ad2fc7c36fe71638b6`.
- Input3: `1dd49c03f02f3c348d863359498ab47c237cf833c586c106a50894e49bac485a`.

Run `tests/scripts/test-geometry-builtin24-input.ps1` and
`tests/scripts/test-geometry-builtin24-cmp-input.ps1`. No production routing,
runtime switch, ABI, executable, real corpus/live/render or FPS/CPU change is
claimed. Input1's full5,646-case/256-model/32,768-component/3,216-original-root
regression verifies the shared helper change. All builtin-control-v1 frozen
files, original source/generator/save/template pins and JOB0/geometry827/
newflags0 defaults remain unchanged.

Next: required input4/5 packed families, then production wrappers and native
93F0 shared control with admission that protects native callers' saved control
pointers before unsafe mutation. Pure-data frame aliases do not prove that
parent safe. Cold real/256-live/native-frame, interpreter, reversed frozen
A/B, normalized CPU and default gates remain OPEN. Phase3/continuing goal
stay active; shared phase4 follows, optional5/6 decisions are recorded and
strict StageF phase7 remains LAST.

## Built-in input format4: complete standalone decoder

`native_geometry_builtin12_input.h` replaces both `9E70/A170` blocks,
including setup, every iteration, the delayed-unit flush and all128 final
GPR vectors at the exact queued9498 PC/function. It runs no lifted setup,
loop, tail, state repair or relaxed checker. Production routing remains a
separate milestone.

Each12-byte vertex contains three big-endian unsigned16 position fields
(bytes0..5) and two packed24-bit unit vectors (bytes6..8 and9..11). Each unit
word has two10-bit coordinates and a4-bit table selector. A pair consumes24
source bytes and produces two float4 rows in each of three streams. Source
reads align three quads then apply byte alignment, including LS wrap.

Position offsets are assembled from four prefetched parameter words. Loads
use incoming r5+0/4/8/12, but extraction rotates the latter three through
incoming r34+4/8/12. Actual stack-pointer CWD controls assemble the vector;
biased stacks and unequal r5/r34 remain supported. Each position field is
added to its signed32 offset modulo32 bits, then split into a signed whole
part and unsigned fractional part at12 fractional bits. Exact SPU conversions
and addition preserve the source rounding; the actual CD60 constant supplies
W. Unit XY uses scale-first FMA with actual coefficients; Z retains the
original square/add/subtract/frsqest/fi/multiply sequence. The16-entry table
is read from actual incoming r8, including reserved selectors, without
canonicalizing table contents or replacing estimates with square root.

Metadata semantics1/2/3 publish through incoming r35/r37/r61. The first two
IDs reads and stores use r3; the third uses r82. The shared read-only overlay
models all six metadata writes before checking the data footprint. Initial
source and parameter quads are cached before metadata; the next pair is
prefetched after publication. Source/future pointers may differ (r4/r84).

The unit pipeline is delayed one pair. Its initial stores use incoming r49
(normally0) relative to the end of each unit stream, even for a single pair.
Subsequent iterations publish the previous pair, and four final stores flush
the last valid normal/tangent pair. These extra writes are observable and are
included in admission. Each iteration preserves this order:

1. Write both position rows, then both delayed normal rows.
2. Read the current pair's two normal-table quads.
3. Write both delayed tangent rows, then freshly prefetch three future source
   quads, including on the final iteration.
4. Read the current pair's two tangent-table quads.

Table/output/source, metadata/publication and cross-stream aliases therefore
retain the original visibility. Both the ordinary iteration and final flush
compose tangent row0 from cached r54 and row1 from r55; no selector or row
permutation is repaired. Admission requires owned LS, no enabled/saved/active
interrupt context, a terminating negative low-r42 byte count divisible by32
and at most1,048,576, and every actual LS-masked metadata/data/padding store
at or above E680. It performs no payload arithmetic. Context, plan and queue
are unchanged on rejection. This pure-data contract does not establish the
safety of native enclosing callers' saved control pointers.

Validation passes **5,806 strict complete GPR/LS/architectural/channel/control/
PC/function cases**:4,096 broad cases,1,024 arbitrary controls (all13 mask
quads, independent parameters/future/LUT pointers),256 special-FP coefficient
cases,288 byte models,32 split managers,64 nonzero/upper-lane delayed counters,
32 parameter/source/metadata aliases,11 boundary counts up to2,048 pairs and
three host depths. **288 ordered byte models** independently gather packed
bytes, assemble position results, model metadata, table-read timing and stores,
including32 table/output aliases. Their unit arithmetic explicitly uses SPU
primitives and is not claimed as an independent unit numeric oracle.

**3,145,728 independent position components** cover all65,536 unsigned16
codes at16 signed-offset/overflow boundaries in XYZ. Expected float bits use
integer dyadic numerators and explicit24-bit RTZ packing, without SPU numeric
helpers. **3,216 full original93F0 comparisons /3,168 strict native data
children** also pass, including48 empty roots, sixteen source/stack/destination
biases, three host depths, arbitrary LUT arguments and48 late-frame/return-link
aliases. Shared setup and9498 restoration remain original on both arms.
**16 unchanged rejects** include both delayed-padding protected footprints.
Two input-inventory tests pass.

The first compiled5,710-case full-state suite passed without kernel fixes.
The expanded byte model initially assumed aligned publication addresses in
its32 new LUT-alias fixtures; the original and native arms already agreed.
Those fixtures now explicitly supply the canonical metadata layout expected
by that model. Neither runtime arithmetic nor strict checker changed. The
initial compile error (mixed auto deduction) and failed model log are retained.

Run `tests/scripts/test-geometry-builtin12-input.ps1`. The generator pins the
original source/54E8 anchor and emits two private original data blocks with
a9498 trap, plus a separate seven-block original93F0 graph with native-data
hook and95DC trap. The synthetic format4 calibration has SHA256
`c3935ac4aaede08294f18c33871c8de8bb512c166bf36b2648eb5ad63f63d084`.
It uses original93F0 setup over the real54E8 LS plus a synthetic manager and
arguments; it is not a captured cold input root.

No production switch, ABI, executable, real corpus/live/render or FPS/CPU
claim changes. JOB0/geometry827/newflags0 and frozen app/source/save pins stay
unchanged. Next: input5, production wrappers/native93F0, then cold and final
acceptance. Phase3 remains open; shared phase4 and final strict StageF7 remain
in the continuing objective.
