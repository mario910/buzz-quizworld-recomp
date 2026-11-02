# Image41 builtin input audit

The pinned original93F0 path has23 standard non-reporting blocks:17 data
blocks, five shared setup/return blocks and the2760 position-pointer helper.
The six-word95C4 table contains offsets relative to95C4:

| Format | Offset | Data root | Complete data blocks | Current native status |
| --- | --- | --- | --- | --- |
| 0 | 013C | 9700 | 9700,97A8 | Existing complete raw XYZ input; admission limits retained |
| 1 | 1334 | A8F8 | A8F8,AAF8,AB00 | Complete opt-in XYZ+2CMP; cold/live/final gates OPEN |
| 2 | 10E4 | A6A8 | A6A8,A750,A860,A864,A868 | Complete opt-in XYZ+CMP+signed16x4; cold/live/final gates OPEN |
| 3 | 0DCC | A390 | A390,A5D0,A5D8 | Complete opt-in XYZ+3CMP; cold/live/final gates OPEN |
| 4 | 08AC | 9E70 | 9E70,A170 | Complete opt-in fixed12 XYZ+2packed units; cold/live/final gates OPEN |
| 5 | 0234 | 97F8 | 97F8,9BB0 | Complete opt-in fixed12 XYZ+3packed units; cold/live/final gates OPEN |

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

## Built-in input format5: complete standalone decoder

`native_geometry_builtin15_input.h` now replaces both `97F8/9BB0` blocks
through queued9498 with strict full state. Each15-byte vertex contains
unsigned16 XYZ plus three24-bit packed-unit words at offsets6/9/12. Two
vertices consume30 bytes and produce two float4 rows in four streams.
The fixed12 position arithmetic, unit fields, estimate reconstruction and
ordered table composition are shared with format4 in
`native_geometry_builtin_fixed_input.h`. Format4's complete regression passes
after extracting those helpers; its arithmetic and admission do not change.

Format5 retains its own parameter and pipeline contract. The first parameter
quad loads through incoming r5, while the other three load through incoming
r34+4/8/12. All four rotations use incoming r34+0/4/8/12, unlike format4's
first r5 rotation and r5-based loads. Actual stack CWD masks assemble the
vector, so biased stacks and unequal parameter pointers remain meaningful.
Metadata has four reservations: semantics1/2 read/write IDs through r3,
semantics3/4 through r82, with publications via r35/r37/r61/r39. Ordered
read-only admission models all eight metadata stores without payload math.

The initial source/parameter quads precede metadata writes. The next pair
comes from actual incoming r84+30 after all publications; future prefetch
starts at r84+60 and advances30. Initial source remains independent r4.
Positions store the current pair, while three unit streams delay their output
one iteration. Initial dummy stores use each stream's end plus incoming r49,
and six final stores flush the last valid pair. Admission checks these extra
footprints separately from every logical output row. The original LS order is:

1. Write the two positions and the delayed normal pair; read normal tables0/1.
2. Write the delayed second unit pair; read its tables in order1 then0.
3. Write the delayed third unit pair; freshly prefetch source quads in order
   +16,+32,+0, including on the final iteration; read third-unit tables0/1.

Cached rows are used for all delayed compositions. In particular, second-unit
row0 uses r73 and row1 r62; third-unit row0 uses r78 and row1 r77. Output/table/
source aliases retain their original visibility. No selector, dummy output,
lookahead or final temporary GPR is omitted. Owned LS, all IRQ contexts absent,
terminating negative low-r42 count and E680 write protection retain the format4
rules, extended to all four streams and three dummy pairs. Rejection preserves
context, plan and queue. Enclosing native caller safety remains separate.

Validation passes **5,838 strict full-state/LS/architectural/channel/control/
PC/function cases**, including4,096 broad aliases,1,024 arbitrary controls and
all13 constant quads,256 special-FP coefficient cases,32 split managers,
32 fourth-publication/IDs aliases,64 delayed-counter cases,32 parameter aliases,
11 count boundaries and three host depths. **288 ordered byte models** include
source/metadata and table/output overlap for all three delayed unit streams.
They independently gather bytes and model writes/table reads, but explicitly
use SPU primitives for unit numeric operations. **3,145,728 independent fixed12
position components** cover all unsigned16 codes at16 offset/overflow edges,
using integer dyadic RTZ packing. **3,216 full original93F0 cases /3,168 native
strict data children** retain original setup/9498 on both arms, with48 empty
roots, sixteen source/stack/destination biases, three depths, arbitrary LUT
arguments and48 late-frame/return-link aliases. **20 unchanged rejects** cover
fourth-publication/data and all three dummy-output protected footprints.
The first compiled expanded suite passes without kernel/checker corrections.

Run `tests/scripts/test-geometry-builtin15-input.ps1`. The generator emits two
pinned private original blocks/9498 trap and a separate seven-block original
control graph/native-data hook/95DC trap. Synthetic format5 calibration SHA256
is `a581a27292c377580a5c22fc78d6c42d22bcc0ec5e64abc4f3f726bacde1ed89`.
It is original93F0 setup over real54E8 LS with a synthetic manager and arguments,
not a captured format5 root. Full format4 regression passes5,806 strict cases,
288 byte models,3,145,728 independent numeric components and3,216 original roots.

All17 builtin input data blocks now have native implementations (existing raw0
and complete standalone formats1..5). Their production wrappers and enclosing
native93F0 control remain to be composed and checked; standalone function
coverage does not close that milestone. Real cold/live/render/interpreter/
reversed frozen A/B/normalized CPU/default gates remain open. No app/ABI/switch/
default/FPS/CPU claim changes. Continue production integration next, then the
remaining phase3 gates and shared phase4, with strict StageF7 last.

## Complete standard93F0 control: standalone composition

`native_geometry_input_control.h` now composes the complete shared
93F0/9408/9518/9580/9498 control with all five new builtin decoders. Nonempty
raw0 reuses the established complete native raw-stream implementation and its
existing admission limits. Empty calls use the new common frame/return path,
ignoring unused format, source and callback arguments exactly as the original.
No original setup, data block, tail or state repair executes on the native arm.
This implementation is standalone; production routing is still open.

Admission caches the count/config quad before forecasting the28 actual frame
stores, in their original order. The read-only overlay now holds40 quads,
covering those spills plus all metadata publications. Shared metadata admission
is factored out of formats1..5, preserving each family's separate IDs read/store
bases, selected pointers, counters and dummy-output footprints. All five full
regressions pass unchanged. Root metadata is evaluated against the post-spill
overlay, so IDs, table pointers and publications can alias the saved frame.
Eligibility performs no payload arithmetic, original-function execution or
context/LS clone. It preserves context and incoming queue; rejection also
preserves the caller's plan.

Frame writes must individually remain at or above E680 after actual LS masking;
biased and wrapped stack addresses retain original semantics. For nonempty
calls, only the selected95C4 word must match its known target. Other table words
remain arbitrary and observable through speculative vector loads. Formats>=6
retain the existing custom path. Formats1..5 forecast complete metadata/data
footprints using the count cached before spills. Raw0 retains its existing
range, alignment, nonalias and standard-shuffle restrictions. Empty calls do
not inspect unused dispatch words or source/parameter ranges.

Execution materializes the full prefix ABI, including all upper vector lanes,
then directly runs the selected complete native decoder. It finally restores
r80..r104, r126 and r0 from actual late LS and computes the real return PC/queue
for the current host depth. It does not restore a cached entry state. Source,
parameter, manager/config, IDs, pointer-table, publication and return-link frame
aliases are therefore retained, including changes to the saved return address.

Validation passes **8,391 strict complete93F0 comparisons**, **7,796 exact
pre-data dispatch boundaries** and **8,391 independent frame byte models**.
Root totals are595 empty,54 raw0,1,549 each formats1/2 and1,548 each formats3/4/5.
Coverage includes2,560 broad alias cases across formats1..5,512 arbitrary-mask
cases across19 constant quads,240 properly encoded biased managers,352 empty
ignored-format/frame-alias/wrapped-stack roots,80 corrupt-unselected-target
cases,30 large-count roots up to4,095 vertices and three host depths.
All23 rejection cases preserve context/plan/queue, including every selected
95C4 slot and protected dummy output rows.

The frame byte model uses scalar big-endian writes and explicit LS masking,
without calling the native spill helper or SPU store primitive. Dispatch checks
compare every GPR/LS/architectural/channel/control field and PC/function before
any payload runs. Complete-root checks compare the same fields after the real
late tail; return PCs are not masked, normalized or repaired. The generator
pins all23 original standard blocks and a95DC trap, and also emits a closed
private original production reference for future wrapper integration.

Full standalone regressions remain:

| Format | Strict data cases | Byte models | Full original93F0 / strict native children | Rejects |
| --- | --- | --- | --- | --- |
| 1 | 5,646 | 256 | 3,216 /3,168 | 14 |
| 2 | 5,678 | 256 | 3,216 /3,168 | 14 |
| 3 | 5,710 | 256 | 3,216 /3,168 | 17 |
| 4 | 5,806 | 288 | 3,216 /3,168 | 16 |
| 5 | 5,838 | 288 | 3,216 /3,168 | 20 |

All earlier independent numeric component counts remain unchanged. Input4/5
byte models still use SPU unit arithmetic and do not claim an independent unit
numeric oracle. The initial root fixture incorrectly combined biased manager
addresses with an unshifted pointer table and was correctly rejected at
trial108. Ordinary fixtures now keep managers aligned;240 separate biased
manager fixtures explicitly rotate their table bytes. No controller arithmetic
or strict checker was corrected to make the fixtures pass. Failed logs remain
private alongside the final passing logs.

Run `tests/scripts/test-geometry-input-control.ps1`. The suite uses real54E8 LS
only as the pinned image-constant anchor, then supplies synthetic arguments,
manager/buffers and frames. This is not real cold coverage or a compiled-app
result. No production wrapper/switch/ABI/executable/default changes or FPS/CPU
benefit are claimed.

Before production composition, audit and protect the assumptions of native
ATTRIBUTES and the whole-job caller. For example, ATTRIBUTES retains its manager
pointer in a host local, but a data/frame alias can change the r80 restored by
93F0; exact standalone93F0 behavior alone does not prove that enclosing native
control exact. Guards must run before unsafe parent mutation. Whole-job scratch/
table bounds and secondary-stream slot demand also need to account for the
extra delayed-unit rows. These production protections, independent switches,
compiled-app replay, cold real/live/render/interpreter/reversed A/B/normalized
CPU/default gates remain open. Phase3 stays active and strict StageF7 stays last.

## Production standard93F0 and builtin input routing: ABI48

The complete standard input controller and all five new data decoders now have
production wrappers. The generated93F0 entry selects the new controller, which
uses only the selected data wrapper and queues the real9498 marker before its
native late restoration. Per-data strict mode checks every register/LS/control
field at that boundary, including nonvolatile temporaries that the subsequent
restore would overwrite. Parent strict mode separately checks the complete
original93F0 return. JOB2 forces both checks. No new tolerance, excluded state,
original native-arm block or state repair is introduced.

All new switches accept0/1/2 and default0:

| Environment option | Scope |
| --- | --- |
| `BUZZ_GEOMETRY_JOB_INPUT_CONTROL` | Complete standard input controller; custom nonempty calls retain their existing route |
| `BUZZ_GEOMETRY_JOB_BUILTIN20_INPUT` | Format1: XYZ+2CMP |
| `BUZZ_GEOMETRY_JOB_BUILTIN24_INPUT` | Format2: XYZ+CMP+signed16x4 |
| `BUZZ_GEOMETRY_JOB_BUILTIN24_CMP_INPUT` | Format3: XYZ+3CMP |
| `BUZZ_GEOMETRY_JOB_BUILTIN12_INPUT` | Format4: fixed12 XYZ+2 packed units |
| `BUZZ_GEOMETRY_JOB_BUILTIN15_INPUT` | Format5: fixed12 XYZ+3 packed units |

Controller0 retains the previous raw/custom routing and its original switches;
it does not enable the new typed inputs. Controller1/2 requires only the selected
data switch and function plus9498 marker; unused data entries may be disabled
or null. Empty/raw0 calls require no typed library. A selected data switch0 or
missing dependency retains the original route before any frame mutation. Data
wrappers additionally require the enclosing native controller scope: enabling
a data switch alone cannot enter through an unprotected original parent.
Per-data2 checks the full data boundary; controller2 checks the full return.

### Protect the native callers before their first write

The native job entry now validates scratch/table/control-frame bounds for both
existing and new input routing, including when the new controller switch is0.
This is necessary because native ATTRIBUTES and the outer driver retain host
values across calls. A standalone decoder can correctly alter a saved register
through an LS alias while invalidating such an enclosing host assumption.

`native_geometry_input_layout.h` forecasts the two pinned AC68 stream calls
from the21-input job entry. It uses actual input11 size_flags for secondary
presence, standard-format slot demand and descriptor semantics, including
semantic0 slot reuse. Both mixed and pure-custom pairs use their known metadata
grammar. It proves all declared scratch tables fit, includes32 bytes for delayed
builtin4/5 rows and the typed64-byte trailing contract, and keeps their domain
below the relevant saved-control window (576 bytes for standard input calls,
1920 for descriptor/codec calls). Empty calls protect their frame too. Unsupported
capacity, descriptor or assertion paths reject the whole job before preamble
writes; they do not execute and then count fallback as native.

The production93F0 reader additionally requires a bounded manager disjoint from
its frame and all actual data/dummy stores below that frame, outside manager
metadata. Standalone functions retain their broader exact alias support. Source
and parameter read aliases remain allowed where their original order is retained.
The speculative descriptor quad comes from actual LS: the declared metadata is
an8-byte header plus8-byte rows, not16 bytes plus rows.

### Validation and the corrected capacity regression

- **3,876 production wrapper roots**, **1,938 strict parent checks** and
  **1,440 strict child checks** pass. Coverage includes all six formats, empty
  calls, independent parent/data modes, three depths, sixteen biases, selected
  dependencies only and off/image/foreign/out-of-parent-scope rejection.
- **576 full original/native roots across all36 standard-format pairs** retain
  all26 restored nonvolatile vectors. **292 layout cases /877 capacity/control
  rejects** cover scratch limits, slot demand, saved frames, pure-custom pairs,
  mixed semantic0 reuse and empty-frame boundaries. Entry tests prove slot
  shortage rejects before preamble mutation with the new controller both0 and1.
- The **8,391-root /7,796-boundary /8,391-independent-frame-model** controller
  suite and all five complete data suites pass unchanged. The old1,024 mock
  pipeline scenarios and every older production-routing suite pass. Mock source
  descriptors now satisfy the actual metadata grammar instead of supplying random
  bytes for a callee the model never executes.
- **472 production-getter cases** validate all options, including the six new
  default0 switches and strict rejection of malformed values. All53 existing
  controller scenarios pass. Two input-inventory and seven conversion audits pass.

The first frozen integration retained exact output for every job but rejected
5,838 previously native jobs. This failed the coverage/ledger gate and was not
accepted. The dedicated C++ entry audit traced every rejection to the extra
eight bytes incorrectly required in declared descriptor spans. Correcting that
forecast to the actual header/rows restores19,508 admitted real entries, without
changing kernel arithmetic, decoder LS loads or any strict checker. The final
version also keeps these parent protections active on the retained stream route.
The failed V1 build/logs remain frozen; the intermediate V2 also passes five
full-corpus arms. V3 is the final source and executable binding below.

All five V3 frozen arms pass **19,508 exact jobs /58,195 transfers each**, with
zero concurrent-input divergences and zero failures. Enabled native arms execute
all19,508 whole jobs with **zero whole-job fallback**. The new controller wrapper
executes **19,508 raw0 roots /zero input fallback**; checked/independent arms also
perform19,508 strict whole-input checks. Raw0 reuses the established complete
native helper. Existing stage ledgers are identical after moving its19,508 counts
from raw-stream to input-control. The retained-controller arm preserves the old
raw-stream ledger. All five new data formats still have **zero real/live roots**;
their new common-frame/data composition remains synthetic coverage.

The frozen build is `build-vulkan-phase3-input-routing-v3`, with61 stage ledgers
and source/ABI48. Exact bindings:

- EXE: `bec8b85965eb662a171004af33060e30dc0e868a50e016d2c4cb45e2ea6efbd6`.
- MAP: `729a12aef82a51d8c8ddeb5810345681440886c734583ae5043725e55023da5d`.
- Generated C: `c8c91a05e5c483a4046782f6b47164b9a44e214215bfe672c63db7a6e8b4180f`.
- Build metadata: `4d548944ff3c76a42c92cf51e4d961889934deb1ca2a5d23b9d974426f4f8107`.

Thirteen native source copies bind this build; all older freezes, original
lift/generator/save/template pins remain preserved. The generated C is unchanged
between V1/V2/V3; the corrected host C++ admission and final parent protections
produce distinct frozen executables. No FPS or CPU gain is inferred from replay
timings. JOB0/geometry827/newflags0 remain unchanged.

This completes the standard input implementation and production-routing
milestone. Phase3 stays OPEN for expanded cold real coverage,256 live checks,
actual-native menu/round frames, interpreter-count checks, reversed frozen A/B,
normalized worker/process CPU and default decisions. Synthetic/unused formats
are not promoted to real coverage. The continuing objective proceeds through
shared phase4 and optional5/6 decisions, with strictStageF7 LAST.


### ABI48 live and rendered-scene follow-up

The unchanged production freeze passes at least45,000 strict live whole-job/I/O
comparisons and45,000 strict raw0 input returns. Native menu and visually
verified actual-question readbacks look normal, with exact PPM/PNG bindings.
The three completed functional runs have no first-fallback/unrecordable or
mismatch messages; periodic logs establish lower bounds rather than complete
fallback totals. New formats1..5 still have zero real/live calls. See the
[phase3 live results](native-geometry-phase3-results.md#abi48-live-whole-jobinput-and-actual-round-rendering).
Interpreter, reversed A/B, normalized CPU/cycles, cold real/live and default
acceptance remain OPEN; no performance/default or Stage F contract change.
