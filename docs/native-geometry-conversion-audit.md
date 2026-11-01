# Image-41 output conversion: remaining native inventory

Status: source/format audit and callback replay protection, 2026-11-01.
`54E8` and its shared output formatter are still unfinished native work.
All seven storage payloads now have complete native opt-in implementations.
I16/storage-5 and U8-integer/storage-7 still have open real/live gates; their
nonzero new coverage is explicitly synthetic. Shared formatter/control and
built-ins remain required.
This document does not promote existing phase-2 prefixes to complete data
replacements or count original-only observations as native acceptance.

## Complete standard-library boundary

The pinned original standard conversion graph contains **73 non-reporting
blocks**, plus reporting frontier `6158` and two arbitrary callback frontiers.
Computed jump tables are included; direct textual references alone miss most
data paths. A custom callback can leave this bounded graph, so it is not a
closed whole-function proof for arbitrary callback targets.

| Region | Blocks | Remaining work |
|---|---|---|
| `54E8/5500/5538/5558/5614/5648/56AC` | 7 | Frame, static/empty paths, conversion dispatch and cursor publication |
| Built-in output formats 0..3 | 11 | Complete data and speculative padding; all cold in captured jobs |
| Shared formatter `4178` and attribute control | 9 | Descriptor layout, output preparation, field iteration and dispatch |
| Seven output storage payloads | 37 | All seven complete opt-in; I16/U8 real/live gates OPEN |
| Stride getter `40C8` and its paths | 7 | Reuse/extend existing host stride materialization with exact boundaries |
| Getter `2728` / publication `2738` | 2 | Reuse their ordered full-vector operations |

The four built-in **output** formats are separate from the still-pending
built-in **input** formats 1..5 under `93F0`.

| Jump table | Meaning | Target PCs |
|---|---|---|
| `5604` | Output format 0/1/2/3 | `56C8`, `5E28`, `5760`, `5A30` |
| `445C` | Custom output storage 1..7 | `4478`, `46D0`, `4880`, `4B9C`, `4DD0`, `4FDC`, `52B4` |
| `4104` | Built-in output stride 0/1/2/3 | `416C`, `4164`, `415C`, `415C` (12/20/24/24 bytes) |

The captured LS bytes independently validate every table slot. The C text
after each unconditional computed return includes decoded table words that
look like STOP instructions; they are not executed instructions of that block.
The `2728` getter's lexical `2738` tail is also unreachable.

`54E8` takes the geometry manager in r3 and opens a 96-byte frame. It saves
r0/r80/r81/r82/r126, including every upper word. Configuration flag `0x10`
returns the existing output pointer without conversion. Otherwise it gets
the current cursor, publishes the conversion start in manager `+0x54` and
tests the vertex count. Output format is configuration byte 4; vertex count
is BE u16 at configuration `+8`. Manager `+0x5C` supplies the built-in position
stream; the routing table at `+0x160` selects attribute-table entries.

For output IDs above 3, `5614` checks manager `+0x8C` for a custom conversion
callback. Without one, `56AC` uses the custom descriptor at manager `+0x74`
and calls shared formatter `4178`; a missing descriptor reaches `6158`.
That report calls `CC70` and `0298`, then resumes at `5648`.
The common tail obtains the current cursor and stride, advances by
`align16(stride * logical_vertex_count)`, publishes at manager `+0x58` and
restores the actual late frame contents. Built-in data paths round their
processing counts separately (four or two vertices) and perform lookahead
reads/padded stores. Matching logical output bytes alone is insufficient.

## Original evidence

A fresh unchanged original tracer replay passes **19,508 exact jobs /
58,195 transfers**, zero input changes or failures. It observes 7,201
conversion roots, all through the descriptor fallback; all built-in output
entries, static returns, empty returns and report `6158` remain cold.

| Output storage | Root | Dispatches | Complete native region |
|---|---|---|---|
| 1: SNORM16 | `4478` | 1,104 | Five blocks, opt-in |
| 2: float32 | `46D0` | 7,201 | Five blocks, opt-in |
| 3: binary16 | `4880` | 4,513 | Seven blocks, opt-in |
| 4: UNORM8 | `4B9C` | 36 | Five blocks, opt-in |
| 5: I16 | `4DD0` | 0 | Five blocks, opt-in; real/live OPEN |
| 6: CMP 11/11/10 | `4FDC` | 4,424 | Five blocks, opt-in |
| 7: U8 integer | `52B4` | 0 | Five blocks, opt-in; real/live OPEN |

These total **17,278 attribute dispatches**, not inner-loop iterations or
worker-time shares. Unknown storage IDs take the original skip path and
must not be silently converted to another encoding.

Three independent scalar models match every GPR/LS byte at `54E8`, `5500`
and `5538` entry/exit boundaries. A separate publication model matches all
four words at manager `+0x50` and the full return vector: the first captured
job has 408 vertices, stride 16 and 6,528 output bytes. Snapshots include
GPR/LS/PC, not the complete architectural/channel state. Native correctness,
live/render and performance gates for conversion remain OPEN.

## Callback replay protection

The audit found that `5614` synchronously called the full indirect resolver,
bypassing the queued-branch wrapper. A callback entering unwrapped interpreted
or HLE code could perform effects outside the channel journal. Running a
second shadow through it would be an invalid comparison.

Adapter ABI **37** now guards three explicit callback boundaries:

- `5614`: custom conversion, through a guarded synchronous resolver call.
- `4114`: custom stride getter at manager `+0x88`, before its original queued branch.
- `95DC`: custom input callback, before the original `SPU_RET_REG` tail.

Callbacks have no independently audited contract yet. A retained recording
is marked unsupported and executes the original callback once; paired
checking declines the job and gives no native/checked credit. An unexpected
shadow callback fails before invoking a resolver. Unchecked execution retains
the original full resolver, including IRQ/HLE/interpreter behavior. Queued
and register-tail dispatch semantics are preserved. Being registered in the
lift is insufficient proof of an arbitrary callback's effects.

All three callback branches are absent in this corpus: `4114 -> 4128` occurs
21,603 times, `5614 -> 56AC` 7,201 times, and `95DC -> 9668` 11,414 times.
The guard is comparison machinery, not a native callback implementation or
a new exception for differing state. Stage F's accepted baseline is unchanged;
its strict reconstruction remains the final roadmap phase.

Seven audit/generation tests validate graph/table/frame boundaries, aliases,
wrapping, late return loads and preservation of all original guest operations
by the inserted guards. Runtime coverage adds direct/queued-boundary cases in
audit, checked-native and unchecked modes: retained callback effects execute
once, while deliberately incorrect shadow callbacks abort before effects.
The runtime suite now covers 53 controller cases plus 320 configuration cases.
Production build and frozen full-corpus compatibility checks are recorded in
the [phase-3 results](native-geometry-phase3-results.md#output-conversion-audit-and-callback-boundaries).

```powershell
.\toolenv\Scripts\python.exe -m unittest discover -s tests -p test_geometry_conversion_audit.py -v
.\toolenv\Scripts\python.exe tools/inspect/geometry-conversion-audit.py analysis/native-geometry/phase3/conversion-audit-v1 --output analysis/native-geometry/phase3/conversion-audit-v1/fresh-verdict.json
```

Raw snapshots, generated code, traces, reports and frozen executables remain
private under ignored `analysis/`/build directories. The next implementation
must cover the controller, shared formatter and payload inventory above;
completion of orchestration alone cannot close output conversion or phase 3.

## Native descriptor-control kernel: standalone

`native_geometry_conversion_control.h` implements the enclosing frame,
static/empty paths, descriptor dispatch setup and output publication. It uses
explicit formatter/stride callees; their data is not claimed as newly native.
Nonempty built-in outputs, custom conversion callbacks and missing-descriptor
reporting reject before mutation, alongside foreign LS, pending interrupts,
out-of-range spans and frame/manager aliasing. Static and empty paths do not
call a formatter. There is no production wrapper, new switch or ABI change yet.

The kernel preserves full-vector PIC arithmetic, all dispatch scratch vectors,
stack-bias-dependent word stores and actual late frame reloads. It reloads
configuration, count and pointers in the original order around each callee;
callee changes are not replaced by cached entry metadata. The original stride
getter remains an explicit call, including its guarded callback behavior.
The reader validates control spans, not the formatter's data ranges or the
effects of arbitrary stride callbacks. Production integration must admit those
callees and use the root I/O boundary; a pure cloned-state checker must not
repeat an unaudited callback.
Before production routing, its admission must also account for interrupt polls
between original blocks; the float-output region below conservatively rejects
all enabled/saved/active interrupt contexts. The standalone control tests model
fixed architectural inputs and do not establish asynchronous IRQ acceptance.

**4,195 strict standalone cases pass**, comparing every GPR/LS byte,
architectural/channel/control/PC state, final scheduled function and child
entry states. Sixteen callee recipes include volatile-register clobbers,
changed format/count/cursors/stride and overwritten saved frame vectors.
There are 96 independent static/empty frame/publication models across all
16 stack biases and three stack boundaries. Three additional cases execute
the first real captured input with original formatter data at host depths
0/1/2; they are control comparisons, not native-data corpus acceptance.
Eleven reader rejects leave the entire input context unchanged.

The private oracle contains the pinned original 73-block standard graph with
an explicit reporting trap and identical callee boundaries. Its first test
attempt exposed a missing stride hook in the test drainer; the hook now wraps
the synchronous call itself. The production checker and kernel were unchanged
by that harness correction. The diagnostic is retained privately.

```powershell
& tests/scripts/test-geometry-conversion-control.ps1
```

Production integration, native shared formatter/storage/output built-ins,
full-corpus native coverage, >=256 live checks, native frames and final
interpreter/reversed A/B/CPU/default gates remain OPEN. This standalone kernel
does not change the game's executable, defaults, saves or current routing.

## Complete float32 output storage: 46D0

`native_geometry_float_output.h` replaces all five blocks `46D0`, `46FC`,
`4750`, `47F0`, `4808`. It owns every column and iteration, including the
speculative final input/sentinel write. No final lifted iteration executes.
The resulting handoff is exactly `4610`, or `4618` when the original r77
test skips work. The formatter's field iteration remains outside this data
region. This completes one of the seven storage implementations.

The kernel keeps vectors in a host-local register array. It copies float
**bits**, preserving NaNs, signed zero and subnormals without FP arithmetic.
Both single-quad and straddled masked writes fetch all input/destination quads
before ordered stores. Overlapping destinations are not merged, and source
aliases observe earlier iterations' writes. Signed loop comparisons, full
upper vectors, cached masks, address wrap and final scratch vectors match the
original. On a sentinel route the mask still uses the nominal field's nibble.

Admission scans every eventual destination before any write. It rejects
foreign LS, enabled/saved/active interrupt contexts, more than 256 columns, more than 65,536 groups
per column or 1,048,576 total groups, and writes into protected physical LS
below `E680`. Source quad reads keep SPU wrap semantics. A zero-column limit
still executes the original first column; r77=0 skips without touching GPR/LS.
These are conservative admission limits, not changes to retained behavior.

The public `46D0` wrapper requires the owning image-41 pipeline. Own
`BUZZ_GEOMETRY_JOB_FLOAT_OUTPUT=0/1/2` defaults to **0**. Mode 2 or JOB=2
compares every GPR/LS/architectural/channel/control/PC byte plus the scheduled
function and image/depth/policy/overlay against a private pinned five-block
original oracle, stopping before the exact field continuation. Unsupported
entries fall back before native writes and receive no native-success credit.
Adapter ABI **38**, stage counter 42, distinguishes skip/single-quad/double-quad/
mixed roots with native and strict counts. No checker exception is added.

The standalone suite passes **6,177 strict cases**, including 1,792 alias/wrap
cases, 256 independent byte models, one real captured root and nine unchanged
reader rejects. Driver routing adds 24 native calls, 16 strict calls, ten
owned unsupported fallbacks and disabled/foreign/post-scope handling. Runtime
checks cover 53 controller and 328 configuration cases. Full production
replay/live/render results and their frozen artifacts are recorded in the
[phase-3 results](native-geometry-phase3-results.md#complete-float32-output-storage-46d0).

```powershell
& tests/scripts/test-geometry-float-output.ps1
$env:BUZZ_GEOMETRY_JOB = '2'
$env:BUZZ_GEOMETRY_JOB_FLOAT_OUTPUT = '1'
```

The old phase-2 float-output prefix and its mask bit remain available when this
complete region is disabled. Shared formatter `4178`, two other storage data
regions, output built-ins and all final performance/default gates remain OPEN.

The final V2 admission also rejects enabled interrupts when no event is pending,
plus saved interrupt registers and active drain frames. The original drainer can
poll between data blocks; the fused kernel must not suppress a later interrupt.
V1 replay results and the interrupted V1 functional sequence are retained only
as diagnostic history. Final acceptance uses the rebuilt V2 executable.

## Complete binary16 output storage: 4880

`native_geometry_half_output.h` replaces all seven blocks `4880`, `48B0`,
`492C`, `497C`, `4988`, `4A9C`, `4AB0`, through the exact queued `4610` field
continuation or the r77=0 `4618` skip. All columns and iterations run natively,
including the final sentinel write and speculative conversion. This is the
output encoder, separate from the existing halfword input-stream decoder.

The initial column prefetch converts four float vectors into cached packed
halfwords. Every iteration first loads all destination quads, writes them in
original order, then fetches fresh inputs from the already modified LS. This
post-store prefetch also runs on the final iteration. Source/destination and
destination/destination aliases therefore retain the original pipeline effects.
Both masked scatter arms, nominal-field alignment on sentinel routes and
wrapped LS/u32 addresses are preserved.

Conversion uses the original integer operations: high-halfword magnitudes
below `3800` flush to positive zero, magnitudes at/above `4780` saturate to
`7BFF`, retained mantissa bits truncate, and surviving signs are preserved.
This is the lift's bit contract, including exceptional float bit patterns.
Intermediate vectors remain host-local; final scratch vectors are computed
from the native final prefetch. The loop limit includes the sign-extended low
halfword of the input step. A zero column limit still performs one column;
the r77 skip changes no GPR/LS bytes.

Admission rejects foreign LS and every enabled/saved/active interrupt context.
It scans every write before mutation, rejecting protected physical LS below
`E680`, more than 256 columns, 65,536 groups per column or 1,048,576 total
groups. Inputs retain SPU quad-wrap semantics. Own
`BUZZ_GEOMETRY_JOB_HALF_OUTPUT=0/1/2` defaults to **0**, requiring the owning
image-41 pipeline. Mode 2 or JOB=2 compares all GPR/LS/architectural/channel/
control/PC state and scheduled function plus image/depth/policy/overlay against
the private pinned seven-block original oracle. Unsupported entries retain
the original path and earn no native-success credit. ABI **39** has 44 stage
counters; stage 43 reports skip/single-quad/double-quad/mixed native and strict
roots. The five-block float encoder and existing Stage F contract are unchanged.

**7,215 strict standalone cases pass**: 1,792 alias/wrap cases, 1,024 mixed-column
and alias combinations, 256 independent byte models, explicit 1/128/256-column
boundaries, signed-step boundaries and one real captured root. Nine admission
rejects leave context and plan unchanged. A separate independent scalar model
checks **1,048,576 float inputs**: every high halfword, all three retained low
mantissa bits and both discarded-tail extremes. These tests include saturation,
tiny values, signs and exceptional encodings without an IEEE rounding substitute.

Driver routing checks 25 native calls, 17 strict calls, ten counted unsupported
fallbacks and disabled/foreign/post-scope handling. The 256-column case crosses
the original reference drainer's earlier budget; 2,048 steps now cover all five
block transitions per admitted column. An initial routing test caught the new
counter being credited to float32; the counter now uses its own stage. The
initial scalar-sweep count also caught duplicated batches; the final test checks
the exact promised set. Neither correction weakens a state comparison.

Runtime tests cover 53 controller and 336 configuration cases. Full frozen
corpus, live and native-frame results are recorded in the
[phase-3 results](native-geometry-phase3-results.md#complete-binary16-output-storage-4880).

```powershell
& tests/scripts/test-geometry-half-output.ps1
$env:BUZZ_GEOMETRY_JOB = '2'
$env:BUZZ_GEOMETRY_JOB_HALF_OUTPUT = '1'
```

The old phase-2 `4AB0` prefix/mask remains available with this complete region
disabled. Remaining work includes cold integer-data acceptance, shared `4178`,
enclosing conversion integration, output built-ins and all final performance/
interpreter/default gates. Strict Stage F reconstruction stays LAST.

## Complete packed CMP output storage: 4FDC

`native_geometry_cmp_output.h` replaces `4FDC`, `5008`, `50C8`, `51C8` and
`51E0`: every column/iteration, initial conversion, both masked scatter arms,
final sentinel write, post-store speculative prefetch and full final ABI.
The exact queued continuation is `4610`, or `4618` for the original r77 skip.
This is the output encoder, separate from the existing CMP input decoder.

Each input record uses SPU FMA, signed conversion scaled by 2^31, lane-specific
logical shifts and the original shuffle/merge masks. Canonical constants encode
signed 11/11/10-bit fields in x/y/z order. General full-vector parameters retain
their exact semantics. The SPU primitive implementations preserve RTZ and
extended-range behavior, including exceptional input encodings.

Cached records are scattered only after every old destination quad is loaded.
Ordered stores precede fresh input reads, including the final iteration;
aliases therefore observe the modified LS at the original point. Terminal
scratch vectors are computed from that native final prefetch. Full-vector
sentinel choices, nominal-field alignment and LS/u32 wrap remain exact.

Admission checks every eventual write before mutation and rejects foreign LS,
all enabled/saved/active interrupt contexts, protected physical writes below
`E680`, more than 256 columns, 65,536 groups per column or 1,048,576 total groups.
The source's sign-extended low input-step halfword contributes to the loop limit.
Zero column limit still executes one column; r77=0 preserves all GPR/LS bytes.

Own `BUZZ_GEOMETRY_JOB_CMP_OUTPUT=0/1/2` defaults to **0** and requires the
owning image-41 pipeline. Mode 2 or JOB=2 checks all GPR/LS/architectural/channel/
control/PC state, scheduled function and image/depth/policy/overlay against the
pinned five-block original oracle. Unsupported entries retain the original
path and receive no native-success credit. ABI **40** has 45 stage counters;
stage 44 separates skip/single-quad/double-quad/mixed native and strict roots.
No checker field or existing Stage F exception changes.

**7,471 strict cases pass**, including 1,792 alias/wrap cases, 1,024 mixed/alias
combinations, 256 independent byte models, column/signed-step edges, 256
exceptional-FP roots, one real captured root and nine unchanged rejects.
An independent integer model checks **32,772 dyadic-grid encodings** with
different component/record permutations. The chosen FMA numerators fit the
single significand exactly; the model independently applies signed31 saturation
and 11/11/10 bit placement. General FP inputs use the original SPU primitives,
with no tolerance or excluded scratch vectors.

Driver routing covers 25 native calls, 17 strict calls, ten counted unsupported
fallbacks and disabled/foreign/post-scope handling, alongside 1,024 pipeline
models and 19,508 real entry models. Runtime checks pass 53 controller and 344
configuration cases; seven audit tests and the production build pass. Full
frozen corpus/live/frame evidence is recorded in the
[phase-3 results](native-geometry-phase3-results.md#complete-packed-cmp-output-storage-4fdc).

```powershell
& tests/scripts/test-geometry-cmp-output.ps1
$env:BUZZ_GEOMETRY_JOB = '2'
$env:BUZZ_GEOMETRY_JOB_CMP_OUTPUT = '1'
```

The old phase-2 `5008/51E0` prefix/mask remains available when this complete
region is disabled. Cold integer-data acceptance, shared formatter/control integration,
built-ins and final interpreter/performance/default gates remain required.

## Complete SNORM16 output storage: 4478

`native_geometry_snorm_output.h` replaces all five blocks `4478`, `44A8`,
`4538`, `4600` and `4630`. It covers every column and iteration, both scatter
arms, initial conversion and final post-store prefetch. Its exact queued
continuation is `4610`, or `4618` for r77=0, without a lifted final iteration.
This is the output encoder, separate from the existing SNORM input decoder.

The canonical operation is SPU FMA `x * 32767.5 - 0.5`, signed32 conversion
scaled by 2^16, then selection of each word's high halfword. Four components
occupy eight big-endian bytes; canonical padding selects zero. General r39/r40
vectors and shuffle masks preserve their full semantics. Native arithmetic
uses the same RTZ/extended-range SPU primitives as the lift, without C++ float-
to-integer casts or excluded exceptional values.

Cached integer vectors are scattered after all old destination quads have
been fetched. Ordered stores precede fresh input reads, even on the last
iteration. Aliases therefore see the original sequence of modified LS.
Final scratch vectors come from native final-prefetch arithmetic; they are
not copied from the reference. Full-vector sentinel selection, nominal-field
nibbles, signed loop limits and LS/u32 wrap remain exact.

Admission checks every write before mutation. Foreign LS, enabled/saved/active
IRQ contexts, protected writes below E680, more than 256 columns, more than
65,536 groups per column or 1,048,576 total groups reject to the original path.
A zero column limit still executes the original first column; r77=0 preserves
all GPR/LS bytes. Unsupported entries earn no native-success credit.

Own `BUZZ_GEOMETRY_JOB_SNORM_OUTPUT=0/1/2` defaults to **0** and requires the
owning image-41 pipeline. Mode 2 or JOB=2 compares all GPR/LS/architectural/
channel/control/PC state, scheduled function and image/depth/policy/overlay
against the private pinned five-block original graph. ABI **41** has 46 stage
counters; stage 45 distinguishes skip/single-quad/double-quad/mixed roots.
No existing checker field, older encoder or Stage F exception changes.

**7,471 strict cases pass**, including 1,792 alias/wrap cases, 1,024 mixed/alias
combinations, 256 independent byte models, column/signed-step and zero-work
edges, 256 exceptional-FP roots, one real captured input and nine unchanged
rejects. An independent integer model checks **131,088 dyadic components**:
it computes the FMA numerator, explicitly truncates its magnitude to the
single-precision significand for RTZ, then scales, saturates and selects the
high halfword. Component and record permutations vary independently.

The new real input comes from the unchanged original tracer replaying 376
selected captured jobs / 1,441 transfers, all exact. Its entry is job
`menu-both_job_41_0011`, with four columns and eight-byte fields. Snapshots
capture GPR/LS; architectural/channel controls are modeled in standalone
comparisons, not claimed as captured. Driver routing passes 25 native/17 strict
calls and ten unsupported fallbacks, plus 1,024 pipeline and 19,508 real-entry
models. Runtime passes 53 controller/352 configuration cases; seven audit
tests and the production build pass.

```powershell
& tests/scripts/test-geometry-snorm-output.ps1
$env:BUZZ_GEOMETRY_JOB = '2'
$env:BUZZ_GEOMETRY_JOB_SNORM_OUTPUT = '1'
```

Full-corpus/live/frame evidence is recorded in the
[phase-3 results](native-geometry-phase3-results.md#complete-snorm16-output-storage-4478).
Cold integer-data acceptance, shared formatter/control integration, built-ins and
all final interpreter/reversed A/B/CPU/default gates remain required.

## Complete UNORM8 output storage: 4B9C

`native_geometry_unorm_output.h` replaces all five blocks `4B9C`, `4BC8`,
`4C50`, `4D18` and `4D30`, including all columns/iterations, both scatter
arms, initial conversion and final post-store prefetch/full scratch ABI.
It queues exactly `4610`, or `4618` for the original r77 skip. No final lifted
iteration supplies the terminal state. This encoder is separate from input
color-codec routines and uses its own output switch.

Canonical conversion is SPU FMA `x * 255 + 0.5`, unsigned32 conversion scaled
by 2^24, then selection of each word's high byte. The canonical shuffle selects
four component bytes followed by zero padding; the real captured field uses
three bytes. General coefficient vectors and shuffle masks retain their full
semantics. Native arithmetic uses the same RTZ/extended-range SPU primitives
as the lift, without C++ float-to-integer casts.

All old destination quads are fetched before ordered first/second-quad stores.
Fresh input reads observe the modified LS after every store, even at the final
sentinel. The straddled terminal ABI differs from SNORM16: it retains one old
second-destination quad and the pre-increment source cursor. These vectors are
computed or retained natively, never repaired from the reference. Full-vector
sentinel selection, nominal-field mask alignment and address wrap stay exact.

Admission scans every eventual write before mutation. Foreign LS, all
enabled/saved/active IRQ contexts, protected writes below E680, more than 256
columns, 65,536 groups per column or 1,048,576 total groups reject to the
original path. A zero column limit still executes the original first column;
r77=0 preserves all GPR/LS bytes. Unsupported entries earn no native credit.

Own `BUZZ_GEOMETRY_JOB_UNORM_OUTPUT=0/1/2` defaults to **0** and requires the
owning image-41 pipeline. Mode 2 or JOB=2 compares all GPR/LS/architectural/
channel/control/PC state, queued function and image/depth/policy/overlay
against the private pinned five-block original graph. ABI **42** has 47 stage
counters; stage 46 separates skip/single-quad/double-quad/mixed roots.
No existing checker field, older encoder or Stage F exception changes.

**7,471 strict cases pass**, including 1,792 alias/wrap, 1,024 mixed/alias,
256 independent byte models, column/signed-step and zero-work edges, 256
exceptional-FP roots, one real captured root and nine unchanged rejects.
An independent integer model checks **131,088 dyadic components**. The FMA
numerator `255*n+1024` on the n/2048 grid fits the single significand exactly;
the model independently applies scale24, unsigned32 saturation and high-byte
selection. Every component/record has a distinct grid permutation.

The captured input is `multi-private_job_41_0088`, with sixteen columns and
three-byte fields; fixture SHA256 is
`cc422028e1cc75bb049cac44b1d38f760e85b17574a53ad7214bacd5c401d8f6`.
It captures GPR/LS, not channel/architectural controls; standalone controls are
modeled identical inputs. Initial test builds caught two stale helper/type
names in the new harness/wrapper; corrected names do not change a comparator
or kernel result. The initial diagnostics remain private.

Driver routing passes 25 native/17 strict calls, ten unsupported fallbacks,
1,024 pipeline models and 19,508 real entry models. Runtime passes 53 controller
and 360 configuration cases; seven audit tests and the production build pass.

```powershell
& tests/scripts/test-geometry-unorm-output.ps1
$env:BUZZ_GEOMETRY_JOB = '2'
$env:BUZZ_GEOMETRY_JOB_UNORM_OUTPUT = '1'
```

Full-corpus/live/frame evidence is recorded in the
[phase-3 results](native-geometry-phase3-results.md#complete-unorm8-output-storage-4b9c).
Cold integer-data acceptance, shared formatter/control integration, built-ins and
final interpreter/reversed A/B/CPU/default gates remain required.

## Complete integer output storage: 4DD0 and 52B4

`native_geometry_i16_output.h` replaces `4DD0/4E00/4E80/4F3C/4F50`;
`native_geometry_u8_integer_output.h` replaces `52B4/52E0/5368/5430/5448`.
Both implement all columns/iterations, initial conversion, both scatter arms,
final sentinel stores/post-store prefetch and every final scratch vector.
They queue exactly `4610`, or `4618` for r77=0, without a lifted final tail.

I16 converts float inputs with signed32 `cflts` scale16 and selects each word's
high halfword through r72. It performs no FMA. Its column tail overwrites r17
with the loop comparison, preserving r16's mask, unlike SNORM16. Negative
fractions near -2^-16 must follow the full scaled conversion before high-halfword
selection; a direct C++ int16 cast would change the result.

U8 integer adds r43 (canonical +0.5), uses unsigned32 `cfltu` scale24 and r71's
high-byte selection. It does not multiply by 255. General full-vector bias and
shuffle parameters preserve RTZ/extended-range semantics. Both kernels fetch
all old destination quads before ordered stores, then read fresh input after
all stores even on the final iteration. Final scratch vectors are computed
natively from those reads and retained values; no reference-state repair occurs.

Admission rejects foreign LS, every enabled/saved/active IRQ context, physical
writes below E680 and excessive columns/groups before mutation. Signed input
steps, nominal-field nibble masks, sentinel selection and LS/u32 wrap remain
exact. Unsupported entries retain original execution without native credit.
Separate `BUZZ_GEOMETRY_JOB_I16_OUTPUT=0/1/2` and
`BUZZ_GEOMETRY_JOB_U8_INTEGER_OUTPUT=0/1/2` both default to **0**, independent
of their existing input-decoder switches. Mode 2 or JOB=2 checks every GPR/LS/
architectural/channel/control/PC field, queued function and image/depth/policy/
overlay against each pinned five-block original graph. ABI 43 has 49 counters;
stages 47/48 distinguish each kernel's skip/single/double/mixed roots.

Each kernel passes **7,503 strict cases**, including 1,792 alias/wrap, 1,024
mixed/alias, 256 independent byte models, column/signed-step and zero-work
edges, 256 special-FP roots, 32 boundary roots and nine unchanged rejects.
Each has **1,048,576 independent FP-bit components**: all high halfwords with
16 distinct low patterns, permuted across every component/record. I16 checks
full signed32 conversion bits against an independent significand/exponent model.
U8 checks final bytes using exact integer rounding boundaries; positive RTZ
addition cannot cross an exactly representable integer boundary below 256.

There are no captured roots for these datatypes. Calibrated GPR/LS states and
canonical masks come from pinned SNORM/UNORM snapshots, but changing their
selected function makes the integer tests synthetic, not captured I16/U8 data.
Additional enclosing-conversion tests alter one field of the pinned `54E8`
input to storage 5/7, across four component counts, all 16 offsets, strides
16/23/48 and host depths 0/1/2. All **1,152 synthetic cases pass**: 576 native
strict roots per datatype, including 192 I16 and 144 U8 mixed roots, and identical full
root plus whole-conversion GPR/LS/control/function-pointer state. The controller
and other fields remain the original 73-block graph; this does not claim a
new native enclosing controller or a captured whole-job replay.

An initial integration coverage assertion found that three strides divisible
by 16 never mix column nibbles. Stride 23 adds that missing coverage; the state
comparisons and native kernels were unchanged. A subsequent mask review verified
that descriptor byte 4 holds the write width;
the final test varies that byte along with the component-count metadata.
The final 1,152-case run passes with the correct 192/144 mixed coverage.
Both coverage diagnostics remain private.

Driver routing separately covers each new switch's native/checked/off/foreign/
unsupported cases. Runtime configuration, frozen corpus and scene evidence is
recorded in the [phase-3
results](native-geometry-phase3-results.md#complete-integer-output-storage-4dd0-and-52b4).
No Stage F exception or compared field changes. Real/live integer acceptance,
shared formatter/control, built-ins and final interpreter/reversed A/B/CPU/
default gates remain required.
