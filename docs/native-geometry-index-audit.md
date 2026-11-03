# Image-41 compressed-index reference

This refines the [physical index
format](native-geometry-data.md#compressed-indices) with the lifted control
graph and retained native diagnostic kernels. The active host pipeline decodes
directly to host triangles; its internal state is governed by the [job-boundary
contract](native-jobs.md#observable-contract).

## Frame, header and continuation

AEA0 saves r80/r81/r0 and creates a 64-byte frame. An empty index count returns
through AEF8. Nonempty flavor 2/3 calls 7390 with link AF10; other nonempty
flavors only publish the original pointer. AEE8 updates manager+50, then AEF8
restores the frame. AF0C's lexical AF18 tail is unreachable after its
unconditional AEE8 exit.

7390 creates a 432-byte frame and preserves r80..103, r126 and r0. Its PIC setup
subtracts repeated ILA(73A0) from a preferred-slot-only LINK(73A0). The
nonpreferred words of r126 therefore differ from its zero preferred word. Saved
LS words and all register lanes remain observable.

Let B be the aligned r4 stream base, I the BE u16 index count at manager+180,
T=I/3 for valid triangle lists, D the BE u16 header+0 delta count, Q the BE u16
header+2 bias, M the BE u16 header+4 one-bit flag byte count and W header+6.
Header+7 is reserved. M describes **flag bytes**, not packed-delta bytes.

The physical sections are header `[B,B+8)`, M flag bytes, C=ceil(T*2/8)
triangle-command bytes, then ceil(D*W/8) packed-delta bytes. The scratch base
S=B+align16(8+M+C) intentionally precedes the end of the expanded data. Backward
unpacking and exact read/write order protect the overlapping data. The capacity
pointer is BE u32 at manager+17C. SPU quad accesses wrap and floor addresses to
16 bytes; a host pointer calculation must not silently discard those semantics.

The decoder restores its frame, returns the manager in r3 and aligned final
output pointer in r4, then tail-calls 2738 to publish that pointer. The setter's
lexical 2750 tail after SPU_RET is dead. The ordinary AF0C call does not set PC
on entry: a 7390 snapshot may carry PC=AF0C. Compare actual PC/continuation
semantics, rather than assigning a function's address to every entry.

## Data stages and alias ordering

1. **Bit unpacking:** 7428/74F0 select the width-zero, low-width or high-width
   path. 7640 and 7CD0 decode backward, with cached pipeline values and
   lookahead reads. Width zero still writes a zero quad through 77D8. The
   zero-delta and warmup/padding cases require explicit tests.
2. **Delta reconstruction:** 7730 initializes zero caches and ceil(D/32)+1
   iterations. 7770 operates on **eight independent u16 prefix chains**. Four consecutive quads contribute, lane by lane, to the same carry. Each raw halfword subtracts Q modulo 65,536. All four next input quads are
   read before the four output stores. The first iteration writes zero with
   a zero output increment, so the next iteration overwrites that location. The final iteration flushes cached values while reading an extra 64 bytes
   of lookahead. Output size alone cannot describe its read footprint.
3. **Flag expansion:** 7838/78A8 merge explicit and sequential indices. Output
   workspace starts at B+align16(I*2); warmup, lookahead, output order and
   final temporary registers remain significant. Existing bit 6
   covers an inner prefix with a lifted final iteration. It does not replace
   this complete stage or its enclosing decoder.
4. **Command relocation and triangles:** 79A8/79C8 move command bytes using
   an initial zero write, followed by reads and a repeated first destination. Their ordering requires more than a memcpy replacement. 7A90 uses D690/
   D790 tables and overlapping quad reads/stores to stitch the final triangle
   list. Existing bit 3 retains a lifted final iteration; a complete
   native index implementation must also reconstruct that final ABI.

7D54 checks delta workspace, 7D7C the expansion workspace, 7DA4 initial
alignment, and 7DCC command workspace. The later checks occur after LS writes.
Full-decoder eligibility must reproduce their decisions before native writes,
while also accounting for metadata/frame aliasing that could change a later
decision. Error blocks report/assert and can continue if the handlers return;
they are not no-op exits. The lexical 7DF8 tail in 7DCC is dead.

## Retained implementation regions

| Region | Current implementation |
|---|---|
| AEA0/7390 frame/setup, zero width, 2738 and outer publication | Enclosing control and root return |
| 74F0/7640 and 7C20/7CD0 bit unpack | Bit unpack through 7730/77F8 |
| 7730/7770 delta prefix | Delta reconstruction |
| 7838/78A8 flag expansion | Expansion and 79A8/7DCC handoff |
| 79A8/79C8 command relocation | Relocation through 7A90 |
| 7A90 triangle reconstruction and frame restoration | Triangle reconstruction through queued 2738 |

The five native data regions call no original setup, step or final iteration.
The complete control implementation below composes them while preserving their
independent legacy switches. The production host route is described in [native
jobs](native-jobs.md).

### Flag expansion and post-write capacity

`7838/78A8` merges explicit halfwords (flag1) with incrementing sequential
indices (flag0). The iteration count is `(incoming_r82 >> 4) + 1`, including one
warmup. Its initial output increment is zero, then 32 bytes: the first
destination is overwritten on the following iteration. Each step reads flags and
all four explicit-input/lookahead quads before writing the high output quad
followed by the low quad. Native state retains full vectors, including the
masked popcount carried in r10 rather than the intermediate flag control.

The final manager+170 quad is loaded after every output write. Its +17C limit
therefore includes any output/manager alias effects. Two independent cases prove
both reversals: an initially failing capacity becomes valid, and an initially
valid capacity becomes invalid after decoding. The signed comparison and
79A8/7DCC continuation are exact. Selecting 7DCC completes this data region; its
reporting/assertion code is executed later on the retained context, never twice
by the pure-function checker.

Scalar merge checks use the caller's aligned compressed header, with the flag
base at B+2; varying that base arbitrarily would change the pinned source's
flag-word addressing. All 16 explicit-data byte alignments, MSB flag order,
mixed flag patterns and flag-quad crossings are checked independently. Arbitrary
flag bases and extraction/shuffle tables remain covered by strict source-state
comparisons. The legacy bit6 prefix is unchanged and independent; the full
native region has no original final-iteration callback.

### Command relocation and overlapping windows

`79A8/79C8` copies command bytes through a one-quad pipeline. For C in r87, the
region executes `ceil(C/16)+1` iterations, including one zero warmup. Even C=0
writes a zero quad and reads lookahead. The initial destination is r81; its
increment starts at zero, then becomes 16. Thus the first destination is
repeated, and the total physical write span is `max(1,ceil(C/16))*16`. The first
source window starts at incoming r80+16. Every iteration writes its cached quad
BEFORE reading the next high and low source quads. Shared input/ output
therefore has feedback that an ordinary memcpy or memmove would lose.

The complete native implementation keeps full input/output vectors and cached
quads in host variables, prepares the invariant byte-alignment shuffle once, and
reconstructs all changed GPRs and the complete queued-7A90 entry ABI. It handles
all input/output byte biases, wrapped input/lookahead, arbitrary alignment
selectors and GPR upper lanes. The output span is checked before mutation;
writes stay in bounded physical data LS, at or above E680. Oversized and
nonterminating unsigned-count cases fall back without native credit. The final
D160/D170/D180 table loads, relocated offsets, triangle count and LUT bases
remain exact; no original setup, step or final iteration runs. The 7A90 body and
enclosing controller remain separate unfinished work.

### Triangle stitching, mutable tables and final frame

The full `7A90` region now has a native implementation through queued 2738. It
consumes the incoming cached quads, previous output and selection masks as
supplied, without replacing them with LS reloads. Code/input/LUT reads retain
SPU wrap/floor semantics. The first output advance comes from r5; later steps
advance 48 bytes. Every physical output span is bounded at/above E680 before
mutation, including separate first and following spans and wrapped u32 sums.
Zero/oversized counts fall back before native writes, without native credit.

Each iteration reads five LUT quads and all next input quads before storing
output at +0, +16 and +32. It then reads the two high-nibble LUT controls AFTER
all three stores. Input/code/table/output feedback is retained. A tagged
32-entry lookup cache prepares byte selectors for protected physical quads below
E680; those quads cannot be changed by this region's writes. High-LS tables are
reread and prepared on every access, including post-store reads. Arbitrary
incoming masks/table bases/upper lanes remain supported.

The final native iteration reconstructs every changed volatile register. After
all stores, the footer adds 432 to r1, copies the old r95 manager to r3, forms
the aligned publication pointer in r4, then restores r126, r0 and r80..r103 from
the final LS frame in original order. Output/frame aliases therefore change the
restored values exactly. The checker stops at 2738; it executes neither
publication nor return twice. The older bit3 prefix remains independent when the
new switch is off or eligibility fails. New strict checks use the complete
original body rather than that prefix's wrapper.

### Width-16 source compatibility

The independent packed-value checks use ordinary MSB-first packing for widths
1..15, at all 16 byte alignments. At a synthetic width-16 entry with the
standard tables, the pinned lift has a distinct behavior. Both word shifts are
zero; selector32 starts every word with byte0 (`00000102`, `00040506`,
`0008090A`, `000C0D0E`), selector33 starts `00000203`, and SELB takes the first
pair's upper half. Each even output consequently combines the high byte of its
eight-value group's first value with its own high byte. Odd outputs retain the
full original halfword. For packed values `v[i]`, the even result is `(v[i & ~7]
& 0xFF00) | (v[i] >> 8)`.

The native implementation preserves this pinned-lift behavior, with no new
checker exception. A separate numeric/control proof covers width16 at all 16
alignments. The initial ordinary-u16 assumption and diagnostic failures are
retained privately; strict lifted/native state checks were exact. No width-16
real jobs were captured, so this is a synthetic source-contract finding, not a
claim about the retail format or real coverage of that width.

### Complete control and pre-mutation eligibility

`native_geometry_index_control.h` implements AEA0's empty/raw/decoded routes,
64-byte and 432-byte frames, 7390/73A0 setup, 7428 metadata/extraction ABI,
77D8/77E8 zero-width setup, 77F8 expansion setup, 2738 end publication, AEE8
original-pointer publication and AEF8 root return. Selected data callbacks
retain all five per-region switches. With those flags on, the recognized whole
decoder executes no original control/setup/publication/data-final block.

Before any write, eligibility bounds stack/manager/header accesses and every
actual output span, including unpack warmups, mutable D120/DA30 offsets, delta
padding, expansion/command warmups and triangle output. It checks all four error
frontiers and protects the metadata/header reads and return frames needed by
subsequent control. Manager/frame or output/manager/frame overlap, unsafe
writes, unsupported widths/counts, misalignment and pending interrupts fall back
before mutation, with no native-control credit. Data/input aliases remain
supported where these protected regions are unaffected. Individual data regions
still accept their wider documented alias contracts.

An independent pinned reference closure contains all 22 original normal blocks
with distinct traps at the four reporting frontiers; strict mode never executes
reporting I/O twice. It compares all GPR/LS/architectural/control state and the
actual root return PC/FN, stack, image, depth, policy and overlay. The original
callback and fallback are separate. The root return check uses stack/FN identity
as well as PC, including return links that name interior blocks. Native control
replaces a preexisting incoming continuation exactly. The separate [native job
runtime](native-jobs.md) track the frozen proof, actual coverage, cold early
paths and remaining gates.
