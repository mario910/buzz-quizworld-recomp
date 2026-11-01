# Image-41 output conversion: remaining native inventory

Status: source/format audit and callback replay protection, 2026-11-01.
Shared formatter `4178` and enclosing descriptor control `54E8` are now routed
as complete native opt-in controllers. Built-in output formats remain pending.
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
| `54E8/5500/5538/5558/5614/5648/56AC` | 7 | Complete opt-in descriptor control; final gates OPEN |
| Built-in output formats 0..3 | 11 | All4 complete payloads +native54E8 dispatch/admission, default0; cold/live/final gates OPEN |
| Shared formatter `4178` and attribute control | 9 | Complete opt-in controller: 1,908 strict cases; 7,201 strict real roots; final gates OPEN |
| Seven output storage payloads | 37 | All seven complete opt-in; I16/U8 real/live gates OPEN |
| Stride getter `40C8` and its paths | 7 | Complete standard native helper inside 54E8; 4,096 strict cases; custom callback retained |
| Getter `2728` / publication `2738` | 2 | Native ordered helpers inside 54E8 control |

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

## Shared formatter 4178: standalone native kernel

`native_geometry_formatter.h` implements all nine controller blocks:
`4178/4190/41F0/41C0/42EC/4350/43D0/4610/4618`. It owns the 128-byte frame,
static/empty paths, full-vector PIC arithmetic, stride alignment, constants,
semantic lookup, data dispatch and final return. Seven data callees remain
explicit boundaries; tests exercise both retained original and complete native
encoders. There is no production hook, new runtime switch or ABI change yet.

The source reloads descriptor count after a recognized data conversion; unknown
storage skips that reload and keeps the prior count. Native execution preserves
both paths, actual callee scratch vectors and late saved-frame reloads. It
stops each child at the exact queued `4610/4618` PC/function frontier, then
executes the controller tail natively. Restored-frame return matching includes
SP, link PC, host depth and queued function, even when return PCs collide with
internal block addresses. No final lifted control tail or state repair is used.

Read-only admission computes the same field layouts without executing original
code or data conversions. A local overlay models its own cursor publication;
frame writes remain suppressed during planning. It rejects foreign LS, every
enabled/saved/active IRQ context, invalid spans, frame/control overlap, data
writes into manager/descriptor metadata, altered known jump targets and work
budgets before mutation. Data may overlap input and frame storage; execution
preserves ordered writes and actual late frame contents. All successful and
rejected admission cases are checked for an unchanged complete input context.

**1,908 strict cases pass**, with **2,643 exact child-entry comparisons** and
**2,028 complete native data calls across all seven storage types**. They include
195 seven-family stride/count cases (strides0..63 and overlapping input/output),
a 255-field unknown-storage case, 96 independent frame/publication models,
24 return-PC collisions, 32 unknown-storage cases, three modeled host depths,
late-count/frame mutations and 15 unchanged admission rejects. One real captured
GPR/LS entry is tested at three host depths; architectural controls are modeled,
not claimed as captured. The private oracle contains the original nine control
and 37 data blocks, never native reference data.

The first frontier check found a live child continuation after a nested return.
The native frame tail now clears the already-consumed queue before `SPU_RET`,
matching the original queued-tail entry. All state and function-pointer checks
remain strict; the diagnostic is retained privately. Seven conversion-audit
tests also pass. The existing executable, defaults and prior proofs are unchanged.

```powershell
& tests/scripts/test-geometry-formatter.ps1
```

Production routing must add its own off/native/checked switch, an original
closed-graph root checker and admission for the actual child switches. A child
that is disabled or not admitted must not be silently counted as native.
Full real corpus/live/native-frame, interpreter, reversed A/B, normalized CPU
and default-on gates remain OPEN. This completes the standalone controller,
not production formatter acceptance or phase 3.

## Shared formatter 4178 production routing

The shared formatter is now routed through its own
`BUZZ_GEOMETRY_JOB_FORMATTER=0/1/2` switch, default **0**. It runs only for the
owned native image-41 pipeline context. The retained fallback is separate from
the private root reference, which executes the complete original 46-block graph,
including original data encoders. The root checker compares every GPR, LS byte,
architectural/channel/control field, PC and queued function, plus image, host
depth, policy and overlay. No reference execution occurs in native mode 1.
ABI 44 and stage 49 extend the ledger to 50 stages.

Read-only admission checks every eventual data destination and the same child
iteration bounds, with a stronger whole-formatter work budget. Immutable manager
and descriptor metadata make those layouts stable across fields. Every used
complete output encoder must also be enabled and present. An unused disabled
encoder does not block the root. A disabled or missing used child rejects the
whole formatter before frame/cursor/data writes. Per-encoder wrappers and their
own switches/checkers remain active during actual execution.

Production routing tests pass **117 native / 78 strict roots**, with 81 data
calls spanning all seven encoders; every native-mode exit is also checked
independently against the original. They exercise static/empty/no-field/skipped/
known/mixed paths, three host depths and both ways to enable strict checking.
**28 owned unsupported cases** preserve the entire input context and incoming
queue, including all seven disabled and all seven missing used-child cases.
Off, foreign owner, wrong image and post-scope calls do not count as unsupported
owned roots. The existing 1,024 mock-driver scenarios and 19,508 real entry
transcripts still pass; 53 runtime controller cases, 384 configuration cases,
seven audit tests and the 1,908-case standalone suite pass.

Five same-frozen-EXE corpus arms each pass **19,508 exact jobs / 58,195 transfers**
with zero input divergence or failures. Checked and independent arms each have
**7,201 strict native formatter returns**, and native mode has 7,201 native
returns, all with zero fallback. The independent arm retains exactly the seven
output encoder dependencies; all other newer stages are off. Its coverage is
identical to the coupled arm. Other stage ledgers match the parent integer-output
build. All captured formatter roots take the known-field path. Per-storage root
presence is 1:1,104 / 2:7,201 / 3:4,199 / 4:36 / 5:0 / 6:4,424 / 7:0;
these are root-presence counts, not sums of child calls or new cold coverage.

Live native-Stage-F menu checks reach at least **30,000 strict formatter
returns** and **35,000 strict whole-job comparisons**. Actual-native menu and
round frame readbacks are visually normal. Periodic live logs provide lower
bounds, not final fallback totals. Empty/skipped/mixed formatter paths and cold
I16/U8 integer real/live coverage remain unobserved; synthetic coverage is
reported separately. The first application build caught C++ syntax in the C
reference generator; it was changed to C pointer declarations/NULL and all final
checks pass. No native arithmetic or compared-field contract was changed.

[Phase-3 results](native-geometry-phase3-results.md#shared-formatter-4178-production-routing)
bind the frozen build and private proof chain. Final interpreter counts,
reversed same-frozen menu/round A/B and normalized worker/process CPU measurements
remain OPEN. No FPS or CPU benefit is claimed; the switch remains default off.
Next is production integration of 54E8 control, including complete IRQ and
synchronous formatter/stride admission, then built-in output/input formats.

## Native 54E8 descriptor control in production

The seven-block enclosing controller is now production opt-in under
`BUZZ_GEOMETRY_JOB_CONVERSION_CONTROL=0/1/2`, default **0**, with ABI 45 and
stage 50 (51 ledger stages). The complete standard stride getter and ordered
cursor/publication helpers run natively inside this control group. Nonempty
built-in output formats, reporting and arbitrary callbacks remain retained paths;
this milestone does not close their implementation or acceptance gates.

Admission rejects every enabled/saved/active interrupt context. It forecasts the
formatter entry using local GPR arguments and an overlay of the pending cursor
publication, without writing the context, executing lifted code or converting
data. Manager and descriptor metadata must avoid both frames, and all formatter
store destinations/work bounds are checked. Data must also avoid the formatter slot that restores the parent manager pointer,
so subsequent stride-callback admission stays valid. Other frame and
input overlaps remain supported: actual ordered writes and late frame reloads remain authoritative.
Descriptor work requires the actual formatter switch and every used encoder;
static/empty roots need no formatter dependency. All rejected dependencies or
unsupported paths fall back before outer frame/cursor writes.

The strict root reference is the pinned original **73-block** graph with its own
synchronous drainer and computed-target dispatcher. Nested reference calls stay
in that original graph; custom/reporting frontiers fail instead of executing an
unaudited callback. No reference callee uses a new native helper. Comparisons
retain all 128 GPRs, all LS bytes, architectural/channel/control state, PC, queued
function, image/depth/policy/overlay, without exclusions or repair.

A new one-vertex frame-alias regression failed before the saved-manager slot
guard and passes after it. An additional 96 positive cases cover late outer-frame
vectors and the saved formatter result vector; those aliases stay admitted.
V1 positive corpus/live logs are retained as historical
evidence; acceptance below uses the rebuilt V2 executable. No comparison field
was relaxed.

Validation passes:

- **1,853 complete native conversion cases**, including 1,341 native formatter
  calls/all seven encoders, all stack biases, descriptor biases, alias cases and
  return-PC collisions. Forecast and actual formatter admission agree; successful
  admission leaves the full context unchanged. Except three captured GPR/LS entry
  anchors at modeled depths, inputs are explicitly synthetic, with randomized
  GPR vectors and architectural controls. The production C oracle independently
  matches the separate original test oracle on every case.
- **4,096 complete stride cases** across all 256 format selectors, descriptor
  biases/null/wrapped pointers and modeled depths, preserving speculative loads
  and full-vector results. The native helper assumes a standard getter; production
  admission excludes the custom callback before any mutation.
- **4,195 standalone control cases**, 16 identical mock-callee recipes and
  96 independent frame/publication models; the stronger IRQ rule now has
  14 unchanged rejects. The composed reader separately has 21 unchanged rejects.
- **90 native / 60 strict routing roots**, all three paths/seven encoders and
  26 unchanged owned rejects, plus off/foreign/image/post-scope checks. Static and
  empty cases work with the formatter and all encoders disabled; used disabled
  dependencies reject descriptor work. The 1,908-case formatter regression,
  1,024 mock-driver scenarios/19,508 real entry transcripts, 53 runtime controller
  cases, 392 configuration cases and seven audit tests also pass.

Full corpus, live, native-frame and frozen-build evidence is recorded in the
[phase-3 results](native-geometry-phase3-results.md#native-54e8-descriptor-control-in-production).
Built-in output formats, cold acceptance and final interpreter/A/B/CPU/default
gates remain OPEN; the switch stays default off.

## Built-in float3 output: standalone kernel

Built-in output format 0 (`56C8/5718`, stride 12) now has a complete standalone
native data kernel in `native_geometry_builtin_float3_output.h`. It packs the
XYZ bits from four float4 rows into three ordered 16-byte stores, then advances
the output by 48 bytes. This covers both original blocks and the final iteration;
no lifted tail, float arithmetic, value repair or reduced comparison is used.
The exact boundary is queued PC/function `5648`, before shared conversion control.

The low 16 bits of entry r10 determine the vertex count. The controller normally
skips zero; a direct zero-count data entry would underflow the original do-loop,
so admission rejects it before mutation. Positive counts execute ceil(N/4)
groups and write **48*ceil(N/4)** bytes, including padding beyond logical N*12.
One 64-byte position group is initially cached. Every iteration loads the next
64 bytes before stores, even the last iteration. Store order is +32, +0, +16;
all LS accesses retain the original quad alignment/wrap and actual LS masks.
The final speculative vectors and full pointer/counter lanes stay exact.

Read-only admission rejects foreign LS, every enabled/saved/active IRQ context,
zero-count entries and any actual padded store into protected LS below E680.
Source and destination may alias; metadata inputs and input loads may wrap.
Successful and rejected admission preserve the entire context. This pure data
reader stops before shared control. Future enclosing-control admission must
also protect its manager/configuration/callback metadata from every padded
store before the first outer-frame or cursor write; a data-region comparison
must never repeat an unaudited stride callback.

Validation passes **6,178 strict GPR/LS/architectural/channel/control/PC/FN
cases**, including **5,154 independent ordered raw-byte models**, 1,024 arbitrary
LS-shuffle-mask cases and 1,024 special FP-bit cases (NaN payloads, infinities,
subnormals and signed zero remain raw bits). It covers biased/aligned pointers,
input/output and manager aliases, wrapped reads, three modeled host depths,
all count remainders and the largest admitted write span: 16,928 vertices from
E680 end exactly at LS size; 16,929 would touch protected wrapped LS and reject.
Eight unsupported cases leave both context and plan unchanged.

**828 full original-controller comparisons** also pass, with 828 strict native
data roots. Only format-0 data is replaced; original control and stride getter
remain on both arms. They include 48 late outer-frame aliases, all count
remainders, biased stacks/output and three depths. These are synthetic function
cases, not captured whole-job replay or a new native enclosing controller.

Run `tests/scripts/test-geometry-builtin-float3-output.ps1` to build the
private pinned oracle and strict/model/full-conversion tests. The
[phase-3 results](native-geometry-phase3-results.md#built-in-float3-output-standalone-kernel)
record the synthetic calibration and limitations. No production switch,
ABI/executable, corpus/live/render or performance change occurs. Formats1..3,
production routing/admission and all final gates remain OPEN.

## Built-in 20-byte output: standalone kernel

Built-in output format 1 (`5E28/6050/6058`, logical stride 20) now has a complete
standalone native kernel in `native_geometry_builtin20_output.h`. Each pair
contains raw XYZ from semantic 1 and CMP 11/11/10 attributes from semantics 2
and 3. A shared pair-packing routine preserves source operand order for FMA,
RTZ signed31 conversion, lane shifts, both packing shuffles and bit selection.
Host-local vectors hold the pipeline; all initial values that survive into the
final ABI are materialized explicitly. No lifted setup, loop or final tail is
used. The exact frontier is queued PC/function `5648`, before shared control.

The controller rounds the input count to an even value in r29. Admission
requires a positive even low-lane value <=65,536. Zero underflows the do-loop;
odd values cannot reach zero in 40-byte steps. Other counter lanes retain their
actual vector arithmetic. Each pair advances the output by 40 bytes but writes
four ordered quads at +32, +16, +0, +48 after quad alignment. The first prefix
preserves old destination bytes; final padding can include a repeated packed
block beyond logical output. Input/output pointers may be biased or wrap.
Checking only logical count*20 bytes would miss actual writes.

Three input pairs are initially prefetched. Two pairs of CMP results are
prepared ahead; each iteration quantizes the next cached pair before stores.
After all four stores it freshly reads the next destination quad and the next
input pair. That ordering also applies to the last iteration. Only a nonfinal
iteration executes the backedge copies; the final speculative inputs, output
fragment, cached conversions and all 128 guest register vectors remain exact.
Read-only admission checks actual padded destinations and rejects foreign LS,
all enabled/saved/active IRQ contexts, unsupported counts and protected writes.
Every successful and rejected admission preserves the complete context.

Validation passes **5,663 strict GPR/LS/architectural/channel/control/PC/FN
cases**, including 4,096 broad input/output and cross-stream alias cases,
1,024 arbitrary-control cases (including shuffle masks, scale/bias and shifts),
256 special-FP cases, wrapped source/manager reads, biased pointers and three
modeled host depths. **256 independent ordered byte models** combine raw XYZ
with an exact dyadic CMP model, including output/raw-position aliases and fresh
post-store prefetch. **32,772 independent dyadic CMP encodings** cover all 8,193
grid values with distinct component triples and signed saturation. Twelve
rejects preserve both context and plan. At aligned E680, 10,156 rounded vertices
fit the actual padded span exactly; 10,158 would wrap a padded store into
protected LS and reject.

**828 full original-controller comparisons** pass, with 828 native strict data
roots and 48 late outer-frame aliases. Only the format-1 payload is native on
that arm; original controller/getter remain on both sides. This is synthetic
function integration, not real whole-job replay, a newly native enclosing
controller or live acceptance. No arithmetic, checker or tolerance change was
needed after the first runtime suite; seven conversion-audit tests also pass.

Run `tests/scripts/test-geometry-builtin20-output.ps1` for strict/model/full-
conversion tests. [Phase-3 results](native-geometry-phase3-results.md#built-in-20-byte-output-standalone-kernel)
record the synthetic calibration and limits. No production/runtime/ABI/app
change occurs. Formats2/3, all four production wrappers/parent callback-safe
admission and cold/final gates remain OPEN.

## Built-in 24-byte outputs: standalone formats 2 and 3

Both remaining output built-ins now have complete standalone native kernels:

| Format | Original blocks | Per-vertex layout | Native header |
| --- | --- | --- | --- |
| 2 | `5760/5970/5978` | raw XYZ semantic1, CMP11/11/10 semantic2, SNORM16x4 semantic3 | `native_geometry_builtin24_output.h` |
| 3 | `5A30/5D18/5D20` | raw XYZ semantic1, CMP11/11/10 semantics2/3/4 | `native_geometry_builtin24_cmp_output.h` |

Each kernel replaces setup, backedge and loop, ending at the exact queued
PC/function `5648`. Host-local vectors hold the complete pipeline and all
128 final guest register vectors remain exact. Neither path executes a lifted
setup, data loop or tail. CMP retains scale-first FMA operand order, RTZ
signed31 conversion, the actual CF20 shift vector, both shuffles and bit
selection. Format2 also retains the full signed32 SNORM conversion before
packing high 16-bit halves; its intermediate integer results are tested too.
Format3 reuses the read-only stream lookup and 24-byte span admission from
format2, with its own native packing and data pipeline.

Each iteration consumes two vertices and writes three ordered quads at
`+32,+16,+0`, advancing 48 bytes. Every physical quad is aligned/masked as in
the original. Initial reads prefetch three pairs of every stream. Two pairs
are already converted; the next cached pair is converted before stores, then
fresh future pairs are read after all stores, even on the final iteration.
Only nonfinal iterations copy the backedge raw-XYZ cache. These details matter
for overlapping/wrapped streams, output aliases and final speculative state.

Admission rejects foreign LS, all enabled/saved/active interrupt contexts,
zero/odd/greater-than-65,536 rounded counts and every actual destination quad
below E680. It does not mutate context; rejection also preserves the plan.
The low r29 lane controls iteration while upper lanes keep their original
vector arithmetic. At E680, 8,464 rounded vertices end exactly at 40000;
8,466 wrap a padded store into protected LS and reject. Biased pointers and
source/manager wraps are tested. This is pure-data admission: a future parent
must additionally protect its manager/configuration/callback metadata from
every actual padded store before changing its frame/cursors.

Validation for **each format** passes:

- **5,663 strict full GPR/LS/architectural/channel/control/PC/function cases**:
  4,096 broad input/output and cross-stream alias cases, 1,024 arbitrary
  control cases, 256 special-FP cases, wrapped-manager reads, boundary counts,
  biased pointers and three modeled host depths.
- **256 independent ordered byte models**, with raw XYZ/output aliases,
  distinct quantized streams and fresh post-store prefetch.
- **32,772 independent dyadic CMP encodings**, across all 8,193 grid values
  with distinct component triples and signed saturation.
- **828 full original-controller comparisons / 828 native strict child
  roots**, including 48 late outer-frame aliases. Only the selected format's
  data is native; controller/getter remain original on both arms.
- **12 unchanged rejects**, plus **7 conversion-audit tests** for the group.

Format2 additionally passes **131,088 independent full-SNORM32 components**.
Its initial and expanded suites passed without kernel/checker changes. The
initial format3 suite correctly rejected missing setup survivors r76 and then
r79, with output LS already equal. Both survivors were implemented; the whole
suite then passed. Final expanded runs use distinct streams in independent
byte models and include output aliases to format3's fourth stream. Neither
the comparator nor arithmetic contract was weakened. Failure logs are retained.

Each oracle generator pins the original source and captured54E8 anchor, emits
the three original data blocks plus a distinct5648 trap, and separately emits
the full73-block original conversion graph with a native-only child hook.
These original-derived files and captured inputs stay private. Each format uses a
distinct full-graph filename, so generating one oracle cannot replace the other. Calibration
mutates the captured controller format byte and runs original setup to the
new data entry; it is **synthetic, not a captured cold root**. Architectural
controls and host depths are modeled. Reproducible GPR/LS anchor SHA256:

- Format2: `41d9c5666e38f734b77dcf5ea511e694ed704f489c744531e189a5d8cd25a76e`.
- Format3: `c163f89c17cfbe766359683d781c24c25f1e6ca6e96227e9e0774f14f58a4029`.

Run `tests/scripts/test-geometry-builtin24-output.ps1` and
`tests/scripts/test-geometry-builtin24-cmp-output.ps1` for the respective suites.
No production routing, runtime switch, ABI, executable, real corpus/live/render
or performance change is claimed. The conversion-control-v2 frozen files,
source/generator/save/template pins and JOB0/geometry827/newflags0 defaults
remain unchanged. Formats0..3 now all have complete standalone kernels.

Next: add the four production wrappers and independent switches, then native
54E8 built-in dispatch with callback-safe admission before outer mutation.
Real cold roots/live coverage, whole-job acceptance, normal actual-native
frames, interpreter checks, reversed same-frozen A/B and normalized worker/
process CPU gates remain OPEN. There is no new FPS or CPU benefit claim.
Phase3 and the continuing objective remain active, followed by shared phase4,
documented optional5/6 decisions and strict StageF phase7 LAST.

## Built-in output production wrappers (ABI46)

The four complete standalone kernels now have production root wrappers in
the generated image41 adapter. Each has its own strict 0/1/2 environment switch,
default **0**, and distinct stage/fallback ledger:

| Format | Switch | Stage | Root / final frontier |
| --- | --- | --- | --- |
| 0 | `BUZZ_GEOMETRY_JOB_BUILTIN_FLOAT3_OUTPUT` | 51 | `56C8` / `5648` |
| 1 | `BUZZ_GEOMETRY_JOB_BUILTIN20_OUTPUT` | 52 | `5E28` / `5648` |
| 2 | `BUZZ_GEOMETRY_JOB_BUILTIN24_OUTPUT` | 53 | `5760` / `5648` |
| 3 | `BUZZ_GEOMETRY_JOB_BUILTIN24_CMP_OUTPUT` | 54 | `5A30` / `5648` |

Mode0 retains the lift; mode1 runs the entire native payload; mode2 compares
all128 GPRs, all LS, architectural/channel/control state and PC/function at
the exact queued5648 frontier. Whole-job checked mode also makes an enabled
mode1 payload strict. Image41 and ownership by the current native pipeline
are required. Off/foreign/image/post-scope calls go straight to the original
fallback. Missing reference/continuation or rejected data admission records a
fallback without prior mutation. A fallback is never native coverage.

The generators embed private original two-/three-block references for these
checks. They retain the original public5648 continuation but never use native
payloads on the reference arm. Native mode1 never invokes that reference.
The separate closed73-block whole-conversion reference remains original
through every nested child. No checker exclusions, tolerances or state repair
were added. ABI46 and its CMake regeneration guard cover the new hooks;
the generic ledger grows from51 to55 stages.

Production-routing tests pass **576 native /384 strict roots per format**
(2,304 /1,536 total): native mode, whole-job checked mode and independent
payload checked mode, three host depths, sixteen destination biases and four
counts. Other builtin switches are disabled during each format's test, proving
independence. All128 GPR/LS/architecture/channel/control/PC/function and
image/depth/policy/overlay agree with the original. Each format has12 owned
admission rejects preserving the entire context and incoming function queue,
plus off/image/foreign/post-scope checks. Existing driver tests, including
1,024 mock-pipeline scenarios, remain green. Runtime tests pass **424** actual
production-getter cases (including32 new option cases),53 controller/ownership/
mismatch scenarios, durable fallback logging and seven conversion-audit tests.

A new application was compiled and frozen separately. Its hashes are:

- EXE: `7d0074ab97b60c96ff7678609894b8362f5dbe8d62602951bee35a60bfb02852`.
- MAP: `bd8562d96c6721001bdb034f3949cec3ef6379ad127b302e180f45bcf264ba88`.
- generatedC: `f6b24fa8d7ee110c2d318f6e4c768518423d8ab5292f404ffb9e8e99252b983c`.

Five same-frozen-EXE corpus arms pass: checked, off, independent, actual-native
and retained-builtins. **Each replays19,508 real jobs /58,195 transfers exactly**,
with zero concurrent input changes or failures. Checked/independent/retained
arms also have19,508 successful whole-job strict checks and zero whole-job
fallbacks. Every older stage ledger matches its conversion-control-v2 parent.
**All four new builtins have zero real native/strict/fallback roots**: this is
compatibility evidence, not acceptance of a real cold path. Synthetic routing
is recorded separately and is never promoted to captured/live coverage.

These wrappers are usable when the lifted conversion controller reaches a
builtin inside a native-owned job. The native54E8 enclosing controller still
rejects nonempty builtins before mutation. Next: extend its dispatch/admission
to all four payloads and protect manager/configuration/callback metadata from
all actual padded stores before outer frame/cursor changes. That native-parent
integration, expanded cold real inputs,256 actual live comparisons per new
path, actual-native frames, interpreter checks, reversed frozen A/B and
normalized worker/process CPU gates remain OPEN. No new live/render/FPS/CPU
benefit is claimed. Original source, phase2 generator, old frozen apps,
save/template pins and JOB0/geometry827/newflags0 defaults stay unchanged.
Phase3/continuing objective remain active; shared phase4 follows, optional5/6
decisions are documented, and strict StageF phase7 remains LAST.

## Native54E8 built-in output control (ABI47)

The native54E8 controller now executes all four output built-ins as well as
the existing-output, empty and descriptor paths. The complete payloads retain
their independent switches. A new function library supplies the four data
roots and their exact5648 frontier. The controller's own switch remains
`BUZZ_GEOMETRY_JOB_CONVERSION_CONTROL=0/1/2`, default0; all four payload
switches also remain0. ABI47 updates the generated caller/CMake guard;
the generic stage ledger remains55 entries, and conversion-control reporting
now distinguishes all seven paths.

Admission precedes every outer frame/cursor mutation. For nonempty built-ins,
it reads only the selected dispatch/stride table words, low count, output
cursor and actual scalar destination footprint. The selected table targets
must match the original built-in and standard stride; unselected words stay
free to differ and remain visible in the original speculative vector loads.
No original-function clone, data arithmetic, source-stream evaluation or
state repair participates in eligibility.

| Format | Groups | Byte advance | Actual quad offsets per group |
| --- | --- | --- | --- |
| 0 | ceil(count/4) | 48 | 32,0,16 |
| 1 | ceil(count/2) | 40 | 32,16,0,48 |
| 2/3 | ceil(count/2) | 48 | 32,16,0 |

Every actual destination is quad aligned and LS masked. Writes below E680 or
overlapping the live manager's 0x90-byte control region (`+00..+8F`)
reject before mutation. This protects configuration/count/format, cursors,
pointer table and callback fields, including padding-only collisions beyond
the logical output length. Late outer-frame aliases remain supported and
are restored from actual LS. The setup-only semantic-ID quad at manager+160
may be changed by outer-frame saves before data setup, or by data afterward;
the native payload observes the actual setup value, without forecasting it.
Wrapped source/semantic-ID reads and a manager near the end of LS are valid.

Builtin format/stride dispatch ignores custom descriptor/callback fields,
as does the original. Tests explicitly supply null descriptors and nonzero
callback pointers without invoking them. Descriptor paths retain their
callback restrictions. The mock-callee base reader retains its default
builtin exclusion; production explicitly opts into the complete data paths.

Execution preserves the original queued-jump semantics: data entry does not
change r0 or host depth. Its exact queued5648 PC/function is checked before
the controller consumes the frontier and performs the common cursor/stride/
publication/frame-return flow. Descriptor and data switches are independent:
builtin control needs only its used payload and frontier, not the formatter,
seven descriptor encoders or unused built-in function pointers. Disabled or
missing used dependencies fall back with original context/queue intact.

Validation passes **15,988 complete native builtin-controller roots /15,988
native data children**, with both the original73-block graph and separate
closed C reference cross-checking every exit. These cover counts1..65,
all destination/manager/stack biases, three host depths, overwritten late
frame/return values, return-PC collisions, ignored callbacks, selected and
unselected table changes, semantic-ID aliases, minimal stack, wrapped manager
and up to8,192 vertices replacing the whole late frame. **4,472 rejects**
preserve context and plan, including **4,424 padded metadata collisions**;
**1,356 safe near-metadata roots** execute exactly. Initial and expanded suites
passed without kernel, arithmetic or checker corrections.

Production tests additionally pass **3,072 native /1,536 parent-strict /1,536
child-strict roots**: all four formats, four switch combinations, three depths,
sixteen biases and four counts. There are56 unchanged owned dependency/layout
rejects plus12 off/image/foreign checks; rejects execute no native child.
Existing standalone payload routing remains green. Regressions pass1,853
descriptor-control roots with1,341 formatter calls/all7 native encoders,
4,096 stride cases and21 rejects;4,195 base-control cases/96 independent frame
models/14 rejects;424 production-getter cases,53 controller scenarios,
durable fallback logging,1,024 mock-pipeline cases and7 audit tests.

A separately frozen application has hashes:

- EXE: `ccff707a23826a9d1a746203a79fa0d5cdc18edb41650ad4f499f03f03fab36b`.
- MAP: `ed23814bd69d272abcab3d4fc960ddcc7327c540a78a8e3f35360865783877d2`.
- generatedC: `38d862e9c07ebc25b7ae371d710fc384b684107803bcb3cf6c01d3cc7f9b70c7`.

Five same-frozen-EXE arms (checked, off, independent, native and retained
conversion control) each replay19,508 real jobs /58,195 transfers exactly,
with zero concurrent input changes or failures. Enabled native controller
arms each have7,201 descriptor roots/zero fallback; checked and independent
arms have7,201 strict controller roots. Whole-job checked arms each have
19,508 successful strict jobs and zero fallback. Every older generic ledger
matches its previous-wrapper counterpart, accounting for the intentionally
disabled controller in the retained arm. **All four builtin real roots remain
zero**. Synthetic integration/routing is not captured cold or live coverage.

Next: remaining input built-ins1..5 and their shared93F0 entry, expanded real
cold coverage and final acceptance. New builtin real/256-live/native-frame,
interpreter, reversed same-frozen menu/round A/B, normalized worker/process CPU
and default gates remain OPEN. No new live/render/FPS/CPU benefit is claimed.
Original source, phase2 generator, all old frozen apps, saves/template pins
and JOB0/geometry827/newflags0 defaults stay unchanged. Phase3 and the
continuing objective remain active, followed by shared phase4, documented
optional5/6 decisions, and strict StageF phase7 LAST.


## Formatter column span proof

`BUZZ_GEOMETRY_JOB_FORMATTER_SPANS=0/1` defaults to0. It applies to formatter
admission in both the native formatter and its enclosing conversion forecast.
The existing FORMATTER/JOB checked modes continue comparing all guest state;
no checker field, tolerance or Stage F exception changes. The C driver ABI48
and61 stage ledgers remain unchanged; the new getter is not a new data stage.

For an eligible column, source cursors advance by the same positive signed
step. The proof derives the lane0 loop count, including its final dummy group,
and checks each lane for absence of signed wrap. Each lane's real-write prefix
and dummy suffix are handled separately. Destination advances must be quad
aligned. Modulo2^32 commutes with moduloLS, so high address/advance bits can be
removed before proving a non-wrapping physical interval. That interval must be
aboveE680 and disjoint from manager/descriptor metadata and the parent's
protected control slot. Cross-quad writes are checked for both bases.

An interval may contain unwritten gaps. If it overlaps protected bytes, or any
other proof condition fails, the existing per-quad enumeration decides the
column. This preserves admitted sparse/aliased cases rather than rejecting
them. The65,536 per-column and1,048,576 whole-root budgets are unchanged.
The proof and retained scan are read-only; only the existing native execution
performs conversion and late frame restoration.

Validation:

- 50,000 independent scalar-quad models:12,022 proven and37,978 delegated to
  the retained scan. Models enumerate each quad/byte rather than using spans
  or loop-count division. They cover per-lane tails, cross stores, high-bit
  modular addresses/advances, wrap, protected gaps and exact budget edges.
- All1,908 strict formatter roots,2,643 exact child entries,2,028 native data
  roots across all7 encoders and15 unchanged rejects pass. Both admission
  modes agree on every tested admitted plan and rejected context.
- Conversion regression:1,853 strict controller cases,1,341 formatter calls,
  4,096 stride cases/21 rejects;15,988 builtin roots/children and4,472 unchanged
  rejects (including4,424 metadata collisions),1,356 safe boundary cases.
- Production routing/1,024 modeled drivers pass. Runtime getters increase
  to480 cases; the new option accepts0/1, defaults0 and rejects2/malformed input.
- Six frozen corpus arms each pass19,508exact jobs/58,195transfers,zero input
  changes/failures and unchanged existing stage ledgers. Enabled whole jobs
  execute19,508 natively/zero whole-job fallback. The explicit span-off arm
  also passes the strict whole-job gate.
- A checked menu on the new freeze passes>=35,000 strict formatter returns
  and>=45,000 strict whole-job/I/O comparisons,with the new option1. Periodic
  counts are lower bounds after forced shutdown,not final fallback totals.

Freeze: `build-vulkan-phase3-formatter-spans-v1`, EXE
`b95e457fc251aff615fc284a94c60e827969611ba917ff95b595220e1792dc8b`, MAP
`7ae7077e2141dc60f8b26181164b0d0efcc2182db11b7be4a26549daf843cef1`.
Fifteen native source copies and the unchanged generated C bind the build.
The prior V3 sources/EXE/MAP, original lift/generator/saves and all evidence
remain preserved. Actual-native menu/round frames, own-switch reversed A/B,
CPU savings and default acceptance for this freeze remain OPEN. No performance
benefit is claimed; new input/cold data coverage is not created by this change.
