# Image-41 transform and cull reference

This documents the original control flow, fields and retained diagnostic
kernels. The active host pipeline computes skin/clip/cull on host arrays; see
[native jobs](native-jobs.md). The frame/register details here describe
reference behavior and optional per-function checking.

## Reachable flow and observed coverage

The transform/cull region contains 18 reachable blocks. It calls the separate
five-block vertex-preparation region (`6188/63E0/6490/65EC/65FC`) and shared
`2738` publication. Normal flow uses LS/registers without channels or DMA. `6744
-> CC70` is a reporting frontier; that handler can return to `66B8`. Lexical
tails after unconditional exits are excluded from the graph.

| Region | Original real entries | Contract |
|---|---:|---|
| `6608/6618` | 19,508 each | Save r126, PIC link, 480-byte frame and r80..103 |
| `6744/6790` | 19,508 each | Validate flags, call vertex preparation, select cull form |
| `6188/63E0/6490` | 19,508 each | Prepare/transform vertex clip records; existing Stage F |
| `65FC` | 10,699 | Vertex-loop entry variant |
| `65EC` | 0 | Cold vertex-loop entry variant |
| `6DFC` | 10,467 | Mode 2 clears the sign control |
| `6E00` | 12,538 | Modes 2/3 enable the face-selection mask |
| `6E08` | 19,508 | Existing Stage F cull setup |
| `6EE8` | 1,689,111 | Software-pipelined cull iterations/continuations |
| `6EE0` | 1,669,603 | Continue cull with cached state |
| `6958/66C8` | 19,508 each | Publish kept indices/end pointer, restore frame and return |
| `66B8` | 0 | Bypass/report-return count path |
| `69B0/69E8/6B10/6B18/710C/7110` | 0 | Alternate cull/packing form |
| `6744 -> CC70` | 0 edges | Reporting path, not an ordinary no-op exit |

The observed numeric mode cohorts are 1: **6,970**, 2: **10,467**, 3: **2,071**.
Mode 1 reaches `6E08` directly; mode 2 goes through `6DFC/6E00`; mode 3 through
`6E00`. Numeric labels are retained rather than guessing geometric meanings. The
high-bit variants dispatch through `69B0` into `69E8/6B18`; they have no real
calls here. All 19,508 observed `6958` entries call `2738`; its count-only
branch has synthetic coverage separately from real coverage.

## Frames, fields and data boundaries

`6608` saves r126 at old SP-190, sets ILA(6618) and a preferred-slot-only
LINK(6618). `6618` subtracts these full vectors, so r126 has preferred word zero
and nonpreferred words -6618. It spills r0/r80..103 and the old SP, then
subtracts **480** from SP. The original r4 selects bypass versus active flow.
The reporting condition additionally depends on manager byte0's sign and bit4;
future whole-stage eligibility must handle it before native writes.

The manager's mutable index count is the BE u16 at +180. `6790` reconstructs
`floor(count/3)` with split unsigned 16-bit products and carry, equivalent to
the high-word multiply by `AAAAAAAB` followed by a right shift. The audit checks
all **65,536 u16 values**, not only multiples of three. It rounds the index
read/packing region to 16 bytes, uses 24-byte input batches, zeroes warmup
caches and selects an attribute-table pointer from manager+10 using byte0's low
nibble. Manager+50 supplies the original index base and work pointers.
Manager+140's leading byte can select the alternate data form; value 5 sets the
high mode bit in this dispatch. Table constants and every upper GPR lane remain
inputs to the exact ABI.

The existing clip/cull record is 16 bytes per vertex, with projected values,
clip flags and packed screen coordinates. Cull stores overlap successive index
windows and consume cached/prefetched records. `6EE8` and `6B18` are different
data paths. Their common finalization is described below; completing it does not
replace either loop. Vertex-preparation setup is separately native as described
below.

## Native finalization: 6958/66C8

`native_geometry_transform_finish.h` replaces both blocks and their `2738`
publication, including every final register and the entire frame return.
`BUZZ_GEOMETRY_JOB_TRANSFORM_FINISH=0/1/2` defaults to 0. Mode 2 or JOB=2
compares all 128 GPRs/all LS, architectural/channel/control state and actual
PC/FN/image/depth/policy/overlay. No Stage F exception is added or extended.

For kept triangles K in r50, every word of r80 becomes `3*K` modulo u32. CHD
inserts its preferred low halfword into manager+180 while preserving the other
14 bytes. With an aligned stack this is the first BE halfword. A biased stack
selects the corresponding halfword using the actual CHD mask. Manager byte0 bit4
decides whether to publish an end pointer. That publication inserts
`align16(originalIndexBase + 6*K modulo u32)` using CWD(SP,8), ordinarily word2
of manager+50, preserving the other words. Full temporary vectors are exact.

Both metadata writes occur before the final frame loads. They can overlap saved
r0/r126/nonvolatile values; restoration observes the modified LS. SP adds 480,
r3 receives the full kept-index vector, and r0/r80..103/r126 restore in source
order. Reads retain SPU wrap/floor semantics. The strict checker stops at the
actual loaded return link, final SP and depth-specific continuation; it does not
assume that the incoming r0 is the stage's saved return address. Read-only
eligibility bounds manager writes and rejects pending interrupts or foreign LS
before mutation, without native credit. The enclosing stage and the established
Stage F kernels remain separately controlled.

## Native vertex preparation: 6188/65EC/65FC

`native_geometry_vertex_prepare.h` computes the entire setup in host vectors,
then materializes the exact register ABI and queues `63E0`. It does not execute
the existing Stage F loop during its new strict comparison. The enclosing `6608`
stage has separate native control below, including frame/cull dispatch.

The three first-stage basis vectors pair with **Y/Z/X** coefficients from
manager+130/+120. Each uses the source's multiply followed by FMA. A second
four-term chain composes each result and the origin with manager+A0/+B0/+C0 and
LS CD60, preserving the original RTZ multiply/FMA order. Host columns and their
pre-translation partials supply every volatile final vector, including all upper
words and broadcast lanes. No reassociation or FP tolerance is used. Manager+D0,
D8D0/DA90/D960 selectors and the original SPU conversion/comparison rules
produce the clip bounds and scale. These tables remain arbitrary inputs.

Configuration byte0 bit4 chooses the input pointer, using the low nibble's
selected manager word when required. Scratch pointers retain full-vector
arithmetic and the rounded batch-count offset plus 192 bytes of lookahead.
Manager+140's leading byte selects base (0..2), middle (3..4), five (5) or other
(6..255) setup. The handoff r6 is respectively 0/1/2/0; other temporary vectors
differ by the actual source branch and are preserved as well. The source's
unreachable `65FC -> 6608` lexical tail is excluded.

`BUZZ_GEOMETRY_JOB_VERTEX_PREPARE=0/1/2` defaults to 0. Read-only eligibility
checks owned LS, pending interrupts and the manager span before any mutation. No
LS byte is written by this preparation. Mode 2 or JOB=2 compares all GPRs, all
LS and architectural/channel/control/PC/FN/image/depth/policy/overlay state
against the three original setup blocks, stopping at the queued `63E0`. There is
no Stage F exception in this new check.

## Complete enclosing native control: 6608

`native_geometry_transform_control.h` owns the full frame/PIC, active flag
checks, 6790 configuration, bypass and normal/alternate mode dispatch. Five
callbacks retain vertex preparation, normal cull, alternate cull, finalization
and the independently disabled footer's original restore continuation. With
preparation/finalization enabled, normal observed flow composes native
control/setup/finish with the existing Stage F loops. Alternate 69E8/6B18 data
has the separate cold native implementation below.

Before writes, eligibility checks owned LS, pending interrupts, manager and
480-byte-frame bounds, their disjointness and the reporting condition. This
prevents speculative CC70 execution or frame spills changing the initial
decision. Unsupported/reporting inputs execute retained flow once, without
native credit. The controller's own frame/manager writes are bounded; retained
data callbacks keep their own source-compatible read/write contracts.

Frame setup preserves the ordered spills and full-vector PIC subtraction. After
ordinary LINK(6798), depth/drain/image restoration of the vertex call, host
values construct the exact full cull ABI: count/quotient/carries, source and
output pointers, rounded batches, selectors, masks, tables, PIC addresses and
prefetch vectors. All temporaries and branch-specific upper lanes remain
observable inputs to the data callbacks. Modes 1/2/3 reach ordinary cull;
high-bit modes 1/2/3 reach alternate data; unrecognized modes finalize zero kept
triangles. Bypass either publishes the original pointer or skips that
publication based on the full sentinel word, then returns the existing count.

`BUZZ_GEOMETRY_JOB_TRANSFORM_CONTROL=0/1/2` defaults to 0. The new strict
checker compares ALL GPR/LS/architectural/channel/control/continuation state
against a private source-pinned original graph containing all 12 enclosing
blocks, 2738 publication and a reporting trap. Both arms deliberately use
identical public retained vertex/cull callbacks. The bridge only rebinds their
queued public footer to the private original footer. It adds no data algorithm,
register repair, waiver, exclusion or tolerance. The established Stage F
exception is neither extended nor applied to new controller state.

Returns require restored SP, the actual loaded link and depth-specific FN. The
disabled footer is drained only by its restore function identity, so a loaded
return PC equal to 66C8 cannot cause an extra restore. Cull-entry PC follows the
selected source branch even when normal/alternate callbacks share one function
address. Standalone checks also compare full state at every child entry, before
a data clobber or frame restoration could hide setup errors. Stage F strict
cleanup remains last.

## Alternate cull data: 69E8/6B18/6B10

`native_geometry_alternate_cull.h` replaces the complete setup, all iterations
and continuation thunk through queued 6958. It retains 47 software-pipeline
vectors in host state and materializes the full final ABI once. The source
iteration count is u32(r15+2), including two drain iterations and a valid
one-iteration wrap case. Eligibility checks owned LS, pending interrupts and
1..8,192 iterations before mutation; legitimate enclosing u16 counts fit.

The input stream contains BE u16 index triples, six bytes per triangle and 24
bytes per four-triangle batch. Two cached quads and the alternating D980/CFF0
alignment masks decode overlapping windows. Vertex records are 16 bytes: two
float coordinates, a clip-bit word and an auxiliary word used for halfword
equality/common-sign tests. Edge products/differences retain SPU RTZ order.
Converted bounds use the paired D000/D010 lookups, with exact source extent
ordering. Winding, shared clipping, halfword equality, area degeneracy, small
extents and tail/startup padding form four rejection masks. Their union drives
population counts and six-byte output advancement.

Mask/alignment lookups, old output and next index quads are read before all
three stores. Twelve vertex reads follow all stores. Output can alias input,
mask tables, constants and vertices, including low LS and wrapped addresses; the
next stage observes exact source feedback. Setup vectors already loaded remain
stable when their LS bytes change. No cross-iteration LS cache assumes that data
is immutable. Both sink-selection stores and all padding bytes are preserved,
even when no triangles are kept.

`BUZZ_GEOMETRY_JOB_ALTERNATE_CULL=0/1/2` defaults to zero. Mode 2 or JOB=2
compares every GPR/LS/architectural/channel/control/PC/FN/image/depth/policy/
overlay field against the raw three-block original closure. The retained
function stops before the actual footer function. Unsupported alternate inputs
are not qualified for the production host path.
