# Image-41 output conversion: remaining native inventory

Status: source/format audit and callback replay protection, 2026-11-01.
`54E8` and its shared output formatter are still unfinished native work.
The complete float32 storage-2 data region now has a separate native path;
the other six storage regions remain required work.
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
| Seven output storage payloads | 37 | Storage 2: complete opt-in; six others remain beyond phase-2 prefixes |
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
| 1: SNORM16 | `4478` | 1,104 | Pending: five blocks |
| 2: float32 | `46D0` | 7,201 | Five blocks, opt-in |
| 3: binary16 | `4880` | 4,513 | Pending: seven blocks |
| 4: UNORM8 | `4B9C` | 36 | Pending: five blocks |
| 5: I16 | `4DD0` | 0 | Pending: five blocks |
| 6: CMP 11/11/10 | `4FDC` | 4,424 | Pending: five blocks |
| 7: U8 integer | `52B4` | 0 | Pending: five blocks |

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
complete region is disabled. Shared formatter `4178`, six other storage data
regions, output built-ins and all final performance/default gates remain OPEN.

The final V2 admission also rejects enabled interrupts when no event is pending,
plus saved interrupt registers and active drain frames. The original drainer can
poll between data blocks; the fused kernel must not suppress a later interrupt.
V1 replay results and the interrupted V1 functional sequence are retained only
as diagnostic history. Final acceptance uses the rebuilt V2 executable.
