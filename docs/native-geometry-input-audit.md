# Image41 builtin input audit

This reference describes lifted behavior and retained diagnostic kernels. For
active routing, defaults and correctness requirements, use [native
jobs](native-jobs.md) and [runtime defaults](runtime-defaults.md).

The pinned original93F0 path has23 standard non-reporting blocks:17 data blocks,
five shared setup/return blocks and the2760 position-pointer helper. The
six-word95C4 table contains offsets relative to95C4:

| Format | Offset | Data root | Complete data blocks | Retained decoder |
| --- | --- | --- | --- | --- |
| 0 | 013C | 9700 | 9700,97A8 | Existing complete raw XYZ input; admission limits retained |
| 1 | 1334 | A8F8 | A8F8,AAF8,AB00 | Complete opt-in XYZ+2CMP |
| 2 | 10E4 | A6A8 | A6A8,A750,A860,A864,A868 | Complete opt-in XYZ+CMP+signed16x4 |
| 3 | 0DCC | A390 | A390,A5D0,A5D8 | Complete opt-in XYZ+3CMP |
| 4 | 08AC | 9E70 | 9E70,A170 | Complete opt-in fixed12 XYZ+2packed units |
| 5 | 0234 | 97F8 | 97F8,9BB0 | Complete opt-in fixed12 XYZ+3packed units |

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
`tests/test_geometry_input_audit.py` checks the closure, source mismatch, all
six corrupt table slots and snapshot size. The audit uses the captured 54E8 LS
only to pin image41 table constants, not as a real93F0/builtin capture.

## Built-in input format1: complete standalone decoder

Input format1 (`A8F8/AAF8/AB00`) now has a complete standalone native decoder in
`native_geometry_builtin20_input.h`. Each 20-byte source vertex contains raw XYZ
and two CMP11/11/10 words; each pair produces two float4 rows in each of three
destination streams. Position W comes from the actual CD60 constant; CMP retains
shift/mask, signed31 conversion and converted-input-first FMA operand order. The
complete setup, loop and final lookahead are native, ending at the exact
queued9498 PC/function before shared frame restoration. All128 final guest
register vectors, including setup-only survivors, remain exact. No lifted setup,
loop, tail or state repair runs on the native arm.

The shared `native_geometry_builtin_input.h` implements native metadata
reservation/publication and a CMP-vector decoder. Admission's read-only
eight-quad overlay models metadata writes in their actual order: read IDs, find
the matching/free slot with the actual selector, write the semantic ID, reload
the pointer table and publication quad, then publish the selected pointer using
the actual word mask. Three reservations publish semantics1/2/3. The first IDs
read uses incoming r3; subsequent reads use incoming r82.
Slot-table/ID/publication aliases and the no-free-slot original index16 fallback
are supported. Eligibility evaluates metadata only, without payload arithmetic
or an original-function clone. Successful admission leaves context unchanged;
rejection also preserves the plan.

The first four source quads are prefetched before metadata writes. A second
40-byte pair is aligned/prefetched after all six metadata stores. Each loop
decodes the cached next pair, writes six ordered destination quads (position
rows, first CMP rows, second CMP rows), then freshly reads four future source
quads, including on the final iteration. Source advances40 bytes, each output
advances32. Only nonfinal iterations copy the source backedge vector. The future
cursor is derived from actual incoming r84, which need not equal r4.

Admission requires owned LS, all enabled/saved/active IRQ contexts absent and a
terminating low r42 counter: negative bytes must be a nonzero multiple32 and at
most1,048,576. It checks every actual aligned/LS-masked metadata and data
destination against E680. Destination starts use the actual pointer plus
`r36+r42`, without assuming those vector values cancel. Upper counter lanes
retain their original arithmetic. Data/source/cross-stream and metadata aliases
are preserved, including fresh reads after earlier writes. No parent
frame/control-safety claim follows from this pure-data admission.

The generator pins the original source and captured54E8 anchor, emits three
private original data blocks plus a distinct9498 trap, and separately emits an
eight-block original93F0 graph with optional native-only data hook and95DC
custom-dispatch trap. The synthetic calibration explicitly supplies a format1
manager/three buffers, then runs original93F0 setup toA8F8. Its GPR/LS SHA256 is
`2e75ca50a627366b1ddb692b4f11b9b8742ad07b06f7e450b15140a737f0d81b`. It is not a
captured cold input root; architecture/depth are modeled. Raw captures and all
original-derived code remain private.

## Built-in 24-byte inputs: complete standalone formats 2 and 3

Both 24-byte input families now have complete standalone native decoders:

| Format | Original blocks | Input vertex / output streams | Header |
| --- | --- | --- | --- |
| 2 | `A6A8/A750/A860/A864/A868` | raw XYZ +CMP11/11/10 +signed16x4 /3float4 streams | `native_geometry_builtin24_input.h` |
| 3 | `A390/A5D0/A5D8` | raw XYZ +3CMP11/11/10 /4float4 streams | `native_geometry_builtin24_cmp_input.h` |

Each kernel replaces every setup/data/backedge block and queues the exact 9498
PC/function before common frame restoration. Host-local vectors retain the
pipeline, all128 final GPR vectors and all LS/architectural/channel/ control
state. No lifted setup, loop, tail, tolerance or state repair is used. Both
reuse the native metadata overlay and CMP-vector decoder from input1. Input2
adds exact signed16 conversion (`csflt139`) and converted-input-first FMA with
the actual scale/bias; all four components, including W, are decoded.

Each iteration reads a physical48-byte source pair from three aligned quads and
produces two float4 rows per stream. Source pointers retain their raw bias in
the final ABI; source data reads use the original quad alignment, without
input1's byte-alignment shuffle. Initial source quads are loaded before metadata
publication. The next pair is prefetched after all metadata stores; every
subsequent fresh pair follows all six (format2) or eight (format3) ordered data
stores, including on the final iteration. Input advances 48 bytes and each
output32 bytes. Nonfinal source-cache copies are explicit; format2's
intermediate nop block is absorbed without changing final state.

Admission reads only metadata/control footprints. It requires owned LS, all
enabled/saved/active IRQ contexts absent, and a terminating negative low r42
count of1..1,048,576 bytes divisible by32. Every actual quad-aligned, LS-masked
metadata/data store must stay at or above E680. Data destinations use the actual
slot pointer plus r36+r42; cancellation is not assumed. Metadata
source/destination addresses follow each original family:

- Input2: first IDs read/write through r3, second read through r3 **but write
  through r82**, third read/write through r82. Three reservations publish
  semantics1/2/3 via incoming r35/r37/r61.
- Input3: first two IDs read/write through r3, last two through r82. Four
  reservations publish semantics1/2/3/4 via r35/r37/r61/r39.

The shared reservation helper now accepts a distinct IDs store address; the
existing same-address overload retains input1 behavior. Input2's first suite
reached the split-manager case at trial5635 and detected incorrect GPR/LS output
from writing the second IDs quad to its read address. The helper and input2
caller now preserve the original separate read/store destinations. All input2
cases then pass, and the complete input1 regression also passes. The
numeric/checker contracts remain unchanged. The failed comparison log is
retained. Input3's complete first suite passes without runtime corrections.

Only the selected data family is native; original setup and9498 restoration
remain on both arms. Input2 has14 unchanged rejects; input3 has17, including
fourth-publication and fourth-stream protected writes. Rejection preserves
context, plan and incoming function queue. Two input-inventory and seven
conversion-audit tests also pass.

The private references are the five/three original data blocks with distinct
9498 traps and separate ten-/eight-block original93F0 graphs with native-only
data hooks and95DC traps. Calibrations mutate the synthetic manager format
argument and execute original setup; they are not captured cold roots.
Reproducible GPR/LS SHA256:

- Input2: `9a584efd407eebf6c93d85585d9b0943e88013ab8e2156ad2fc7c36fe71638b6`.
- Input3: `1dd49c03f02f3c348d863359498ab47c237cf833c586c106a50894e49bac485a`.

## Built-in input format4: complete standalone decoder

`native_geometry_builtin12_input.h` replaces both `9E70/A170` blocks, including
setup, every iteration, the delayed-unit flush and all128 final GPR vectors at
the exact queued9498 PC/function. It runs no lifted setup, loop, tail, state
repair or relaxed checker.

Each12-byte vertex contains three big-endian unsigned16 position fields
(bytes0..5) and two packed24-bit unit vectors (bytes6..8 and9..11). Each unit
word has two10-bit coordinates and a4-bit table selector. A pair consumes24
source bytes and produces two float4 rows in each of three streams. Source reads
align three quads then apply byte alignment, including LS wrap.

Position offsets are assembled from four prefetched parameter words. Loads use
incoming r5+0/4/8/12, but extraction rotates the latter three through incoming
r34+4/8/12. Actual stack-pointer CWD controls assemble the vector; biased stacks
and unequal r5/r34 remain supported. Each position field is added to its
signed32 offset modulo32 bits, then split into a signed whole part and unsigned
fractional part at12 fractional bits. Exact SPU conversions and addition
preserve the source rounding; the actual CD60 constant supplies W. Unit XY uses
scale-first FMA with actual coefficients; Z retains the original
square/add/subtract/frsqest/fi/multiply sequence. The16-entry table is read from
actual incoming r8, including reserved selectors, without canonicalizing table
contents or replacing estimates with square root.

Metadata semantics1/2/3 publish through incoming r35/r37/r61. The first two IDs
reads and stores use r3; the third uses r82. The shared read-only overlay models
all six metadata writes before checking the data footprint. Initial source and
parameter quads are cached before metadata; the next pair is prefetched after
publication. Source/future pointers may differ (r4/r84).

The unit pipeline is delayed one pair. Its initial stores use incoming r49
(normally0) relative to the end of each unit stream, even for a single pair.
Subsequent iterations publish the previous pair, and four final stores flush the
last valid normal/tangent pair. These extra writes are observable and are
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
interrupt context, a terminating negative low-r42 byte count divisible by32 and
at most1,048,576, and every actual LS-masked metadata/data/padding store at or
above E680. It performs no payload arithmetic. Context, plan and queue are
unchanged on rejection. This pure-data contract does not establish the safety of
native enclosing callers' saved control pointers.

## Built-in input format5: complete standalone decoder

`native_geometry_builtin15_input.h` now replaces both `97F8/9BB0` blocks through
queued9498 with strict full state. Each15-byte vertex contains unsigned16 XYZ
plus three24-bit packed-unit words at offsets6/9/12. Two vertices consume30
bytes and produce two float4 rows in four streams. The fixed12 position
arithmetic, unit fields, estimate reconstruction and ordered table composition
are shared with format4 in `native_geometry_builtin_fixed_input.h`. Format4's
complete regression passes after extracting those helpers; its arithmetic and
admission do not change.

Format5 retains its own parameter and pipeline contract. The first parameter
quad loads through incoming r5, while the other three load through incoming
r34+4/8/12. All four rotations use incoming r34+0/4/8/12, unlike format4's first
r5 rotation and r5-based loads. Actual stack CWD masks assemble the vector, so
biased stacks and unequal parameter pointers remain meaningful. Metadata has
four reservations: semantics1/2 read/write IDs through r3, semantics3/4 through
r82, with publications via r35/r37/r61/r39. Ordered read-only admission models
all eight metadata stores without payload math.

The initial source/parameter quads precede metadata writes. The next pair comes
from actual incoming r84+30 after all publications; future prefetch starts at
r84+60 and advances30. Initial source remains independent r4. Positions store
the current pair, while three unit streams delay their output one iteration.
Initial dummy stores use each stream's end plus incoming r49, and six final
stores flush the last valid pair. Admission checks these extra footprints
separately from every logical output row. The original LS order is:

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

## Complete standard93F0 control: standalone composition

`native_geometry_input_control.h` now composes the complete shared
93F0/9408/9518/9580/9498 control with all five new builtin decoders. Nonempty
raw0 reuses the established complete native raw-stream implementation and its
existing admission limits. Empty calls use the new common frame/return path,
ignoring unused format, source and callback arguments exactly as the original.
No original setup, data block, tail or state repair executes on the native arm.
The standalone implementation retains the original function boundary.

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
range, alignment, nonalias and standard-shuffle restrictions. Empty calls do not
inspect unused dispatch words or source/parameter ranges.

Execution materializes the full prefix ABI, including all upper vector lanes,
then directly runs the selected complete native decoder. It finally restores
r80..r104, r126 and r0 from actual late LS and computes the real return PC/queue
for the current host depth. It does not restore a cached entry state. Source,
parameter, manager/config, IDs, pointer-table, publication and return-link frame
aliases are therefore retained, including changes to the saved return address.

The frame byte model uses scalar big-endian writes and explicit LS masking,
without calling the native spill helper or SPU store primitive. Dispatch checks
compare every GPR/LS/architectural/channel/control field and PC/function before
any payload runs. Complete-root checks compare the same fields after the real
late tail; return PCs are not masked, normalized or repaired. The generator pins
all23 original standard blocks and a95DC trap, and also emits a closed private
original production reference for future wrapper integration.

Full standalone regressions remain:

| Format | Strict data cases | Byte models | Full original93F0 / strict native children | Rejects |
| --- | --- | --- | --- | --- |
| 1 | 5,646 | 256 | 3,216 /3,168 | 14 |
| 2 | 5,678 | 256 | 3,216 /3,168 | 14 |
| 3 | 5,710 | 256 | 3,216 /3,168 | 17 |
| 4 | 5,806 | 288 | 3,216 /3,168 | 16 |
| 5 | 5,838 | 288 | 3,216 /3,168 | 20 |

All earlier independent numeric component counts remain unchanged. Input4/5 byte
models still use SPU unit arithmetic and do not claim an independent unit
numeric oracle. The initial root fixture incorrectly combined biased manager
addresses with an unshifted pointer table and was correctly rejected at
trial108. No controller arithmetic or strict checker was corrected to make the
fixtures pass. Failed logs remain private alongside the final passing logs.

Run `tests/scripts/test-geometry-input-control.ps1`. The suite uses real54E8 LS
only as the pinned image-constant anchor, then supplies synthetic arguments,
manager/buffers and frames. This is not real cold coverage or a compiled-app
result.

Before production composition, audit and protect the assumptions of native
ATTRIBUTES and the whole-job caller. For example, ATTRIBUTES retains its manager
pointer in a host local, but a data/frame alias can change the r80 restored by
93F0; exact standalone93F0 behavior alone does not prove that enclosing native
control exact. Guards must run before unsafe parent mutation. Whole-job scratch/
table bounds and secondary-stream slot demand also need to account for the extra
delayed-unit rows.

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
it does not enable the new typed inputs. Controller1/2 requires only the
selected data switch and function plus9498 marker; unused data entries may be
disabled or null. Empty/raw0 calls require no typed library. A selected data
switch0 or missing dependency retains the original route before any frame
mutation. Data wrappers additionally require the enclosing native controller
scope: enabling a data switch alone cannot enter through an unprotected original
parent. Per-data2 checks the full data boundary; controller2 checks the full
return.

### Protect the native callers before their first write

The native job entry now validates scratch/table/control-frame bounds for both
existing and new input routing, including when the new controller switch is0.
This is necessary because native ATTRIBUTES and the outer driver retain host
values across calls. A standalone decoder can correctly alter a saved register
through an LS alias while invalidating such an enclosing host assumption.

`native_geometry_input_layout.h` forecasts the two pinned AC68 stream calls from
the21-input job entry. It uses actual input11 size_flags for secondary presence,
standard-format slot demand and descriptor semantics, including semantic0 slot
reuse. Both mixed and pure-custom pairs use their known metadata grammar. It
proves all declared scratch tables fit, includes32 bytes for delayed builtin4/5
rows and the typed64-byte trailing contract, and keeps their domain below the
relevant saved-control window (576 bytes for standard input calls, 1920 for
descriptor/codec calls). Empty calls protect their frame too. Unsupported
capacity, descriptor or assertion paths reject the whole job before preamble
writes; they do not execute and then count fallback as native.

The production93F0 reader additionally requires a bounded manager disjoint from
its frame and all actual data/dummy stores below that frame, outside manager
metadata. Standalone functions retain their broader exact alias support. Source
and parameter read aliases remain allowed where their original order is
retained. The speculative descriptor quad comes from actual LS: the declared
metadata is an8-byte header plus8-byte rows, not16 bytes plus rows.
