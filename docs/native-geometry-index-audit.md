# Image-41 compressed indices: implementation contract

Status: audited; bit unpack and delta reconstruction implemented separately; **native
whole-decoder implementation and acceptance OPEN**,
2026-10-31. This refines the phase-1 [physical format](native-geometry-data.md#compressed-indices)
for the next phase-3 implementation. The continuing objective remains active
through subsequent native phases and final strict Stage F reconstruction.

The pinned original source is SHA-256
`5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812`.
The normal AEA0/7390 graph contains 22 blocks, including the final 2738 setter.
Its normal path uses LS and registers, with no channel or DMA operations.
Four error branches call CC70/0298 and remain outside that normal graph.
An unsupported or error-producing native input must select the lift before
mutation; returning to the lift after partially decoding is insufficient.

## Observed real coverage

An unchanged private original tracer replayed all **19,508 exact jobs /
58,195 transfers**, with zero input changes and zero failures. Timings from
this instrumented tracer are excluded from performance claims.

| Stage | Original function entries |
|---|---:|
| AEA0 controller, AF0C call and 7390 decoder | 19,508 each |
| Width-zero initialization 77D8 | 9,745 |
| Width 1..8 unpack 7C20/7CD0 | 7,211 each |
| Width 9..16 unpack 7640 | 2,552 |
| Delta prefix 7730/7770 | 9,763 each |
| Expansion 7838/78A8 | 19,508 each |
| Command relocation 79A8/79C8 | 19,508 each |
| Triangle reconstruction 7A90 | 19,508 |
| Each error branch 7D54/7D7C/7DA4/7DCC | 0 |

These are function entries, not inner loop iterations. The 2738 setter is
shared by other stages, so its 106,369 global entries are not index coverage.
All width-zero inputs also have zero explicit deltas; all other inputs have
nonzero deltas. The headers contain 0..1,413 deltas and 3..6,537 output indices.

| Packed width | Jobs |
|---|---:|
| 0 | 9,745 |
| 2 | 15 |
| 3 | 186 |
| 4 | 1,174 |
| 5 | 1,570 |
| 6 | 973 |
| 7 | 1,199 |
| 8 | 2,094 |
| 9 | 1,506 |
| 10 | 975 |
| 11 | 71 |

Width 1 and widths 12..16 have no real coverage here. Synthetic checks must
cover any such supported paths; they cannot supply missing real/live gates.

## Frame, header and continuation

AEA0 saves r80/r81/r0 and creates a 64-byte frame. An empty index count returns
through AEF8. Nonempty flavor 2/3 calls 7390 with link AF10; other nonempty
flavors only publish the original pointer. AEE8 updates manager+50, then AEF8
restores the frame. AF0C's lexical AF18 tail is unreachable after its
unconditional AEE8 exit.

7390 creates a 432-byte frame and preserves r80..103, r126 and r0. Its PIC
setup subtracts repeated ILA(73A0) from a preferred-slot-only LINK(73A0).
The nonpreferred words of r126 therefore differ from its zero preferred
word. Saved LS words and all register lanes remain observable.

Let B be the aligned r4 stream base, I the BE u16 index count at manager+180,
T=I/3 for valid triangle lists, D the BE u16 header+0 delta count, Q the BE u16
header+2 bias, M the BE u16 header+4 one-bit flag byte count and W header+6.
Header+7 is reserved. M describes **flag bytes**, not packed-delta bytes.

The physical sections are header `[B,B+8)`, M flag bytes, C=ceil(T*2/8)
triangle-command bytes, then ceil(D*W/8) packed-delta bytes. The scratch base
S=B+align16(8+M+C) intentionally precedes the end of the expanded data.
Backward unpacking and exact read/write order protect the overlapping data.
The capacity pointer is BE u32 at manager+17C. SPU quad accesses wrap and floor
addresses to 16 bytes; a host pointer calculation must not silently discard
those semantics.

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
   iterations. 7770 operates on **eight independent u16 prefix chains**.
   Four consecutive quads contribute, lane by lane, to the same carry.
   Each raw halfword subtracts Q modulo 65,536. All four next input quads are
   read before the four output stores. The first iteration writes zero with
   a zero output increment, so the next iteration overwrites that location.
   The final iteration flushes cached values while reading an extra 64 bytes
   of lookahead. Output size alone cannot describe its read footprint.
3. **Flag expansion:** 7838/78A8 merge explicit and sequential indices. Output
   workspace starts at B+align16(I*2); warmup, lookahead, output order and
   final temporary registers remain significant. Existing phase-2 bit 6
   covers an inner prefix with a lifted final iteration. It does not replace
   this complete stage or its enclosing decoder.
4. **Command relocation and triangles:** 79A8/79C8 move command bytes using
   an initial zero write, followed by reads and a repeated first destination.
   Their ordering requires more than a memcpy replacement. 7A90 uses D690/
   D790 tables and overlapping quad reads/stores to stitch the final triangle
   list. Existing phase-2 bit 3 retains a lifted final iteration; a complete
   native index implementation must also reconstruct that final ABI.

7D54 checks delta workspace, 7D7C the expansion workspace, 7DA4 initial
alignment, and 7DCC command workspace. The later checks occur after LS writes.
Full-decoder eligibility must reproduce their decisions before native writes,
while also accounting for metadata/frame aliasing that could change a later
decision. Error blocks report/assert and can continue if the handlers return;
they are not no-op exits. The lexical 7DF8 tail in 7DCC is dead.

## Reproducible audit and remaining gates

`tools/inspect/geometry-index-audit.py` checks the source pin and reachable
graph, inventories every real header with the existing physical-format
reader, cross-checks width/delta cohorts against original trace entries, and
rejects observed error paths. Its independent scalar delta model reproduces
**all 128 registers and all 262,144 LS bytes** of the captured 7770 entry/exit
pair: 264,192 bytes, zero differences. Five portable regressions cover dead
tails, halfword overflow, eight independent prefix chains, warmup, aliasing
lookahead, malformed counts and the source pin.

That snapshot omits full architectural/channel state and represents one call.
It is a format proof, not acceptance of a new native kernel. The source/format
audit at `ca88254` changed no production switch, default, kernel, driver or
generated lift. Subsequent delta implementation and its full-state evidence
are tracked in the [phase-3 results](native-geometry-phase3-results.md#complete-index-delta-reconstruction-77307770).
The native whole-decoder implementation, full-state synthetic/real checks,
>=256 live calls, native menu/round rendering, interpreter checks, reversed
same-EXE A/B and normalized worker/process cycles/time measurements remain
OPEN. Repeatable CPU savings with flat FPS may justify a later default-on
decision after the correctness/render gates; no benefit is claimed here.

Private evidence resides under `analysis/native-geometry/phase3/index-audit-v1`.
The verdict binds captured states, trace CSVs, replay log, tracer and audit
sources. Raw corpus/lifts/snapshots stay ignored. Reproduce after the original
trace replay is terminal, with no game/performance run active:

```powershell
.\toolenv\Scripts\python.exe -m unittest discover -s tests -p test_geometry_index_audit.py -v
.\toolenv\Scripts\python.exe tools/inspect/geometry-index-audit.py analysis/native-geometry/replay-final/all/image41 analysis/native-geometry/phase3/index-audit-v1 --output analysis/native-geometry/phase3/index-audit-rerun.json
```

Choose a fresh output filename; preserve the finalized v1 verdict and archive.

## Native implementation inventory

| Region | Current implementation |
|---|---|
| AEA0 controller, 7390 frame/setup, 2738 publication | Complete native replacement OPEN |
| 74F0/7640 and 7C20/7CD0 bit unpack | Complete native implementation through 7730/77F8, own `INDEX_UNPACK=0/1/2`, default 0; final performance/default gates OPEN |
| 7730/7770 delta prefix | Complete native implementation, own `INDEX_DELTAS=0/1/2`, default 0; final performance/default gates OPEN |
| 7838/78A8 flag expansion | Phase-2 native prefix retains original final iteration; complete replacement OPEN |
| 79A8/79C8 command relocation | Complete native replacement OPEN |
| 7A90 triangle reconstruction and frame restoration | Phase-2 native prefix retains original final iteration; complete replacement OPEN |

The native bit-unpack and delta regions call no original setup, step or final
iteration. Completing those regions does not complete 7390 or the image-41 job flow.

### Width-16 source compatibility

The independent packed-value checks use ordinary MSB-first packing for widths
1..15, at all 16 byte alignments. At a synthetic width-16 entry with the
standard tables, the pinned lift has a distinct behavior. Both word shifts
are zero; selector32 starts every word with byte0 (`00000102`, `00040506`,
`0008090A`, `000C0D0E`), selector33 starts `00000203`, and SELB takes the
first pair's upper half. Each even output consequently combines the high byte
of its eight-value group's first value with its own high byte. Odd outputs
retain the full original halfword. For packed values `v[i]`, the even result is
`(v[i & ~7] & 0xFF00) | (v[i] >> 8)`.

The native implementation preserves this pinned-lift behavior, with no new
checker exception. A separate numeric/control proof covers width16 at all
16 alignments. The initial ordinary-u16 assumption and diagnostic failures
are retained privately; strict lifted/native state checks were exact. No
width-16 real jobs were captured, so this is a synthetic source-contract
finding, not a claim about the retail format or real coverage of that width.
