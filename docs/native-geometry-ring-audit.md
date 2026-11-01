# Image-41 output allocator: implementation contract

Status: source and original-replay audit, 2026-11-01. Native implementation,
strict native comparisons, live coverage and performance gates remain OPEN.
This is the next phase-3 implementation after alternate cull data.

## Audited boundary and control flow

`37C0` takes geometry context in r3, ring EA/flags in r4, requested bytes in
r5, result LS address in r6 and guest lane in r7. It creates a 1,344-byte
frame, preserves r80..r96/r126/r0 and returns through `38D0`. Full vector
register contents, ordered LS reads/writes and the actual return continuation
are part of the contract. Address wrapping and quadword flooring must match
the SPU helpers, including aliases with result, scratch or saved frame data.

The pinned lift contains **39 allocator blocks**, two reporting frontiers and
an unresolved computed continuation. This is a bounded inventory, not proof
that all paths have been exercised or that all computed targets are lifted.

| Region | Responsibility | Original corpus coverage |
|---|---|---|
| `37C0/3800/38D0` | Frame, direct-address branch and return | 7,201 roots/returns; direct branch absent |
| `3930/3940/3948` | Round request to 128, save machine state, disable interrupts, reserve lane line | 7,201 |
| `39E8..3B58`, `3CF8` | Candidate/free-space checks, wrap selection, result and PUTLLUC publication | Normal lane observed |
| `3E04` | Four-byte GET of GPU free cursor and tag wait | 20 |
| `3B88/3BA0/3BDC/3C78/3CC4` | Shared allocation when lane begin=end | Absent |
| `3D00/3D18/3D34/3D54/3E68` | Shared allocation after oversized/failed lane attempt | Absent |
| `3F10/3F40/3FA4/400C/4064/40AC/40B4` | Reconsider lane after fallback; poll GPU and decrementer | Absent |
| `3CE8/3CEC` | Restore initially enabled interrupts by computed branch | Absent; continuation unresolved |
| `3DD0`, `4078` | Report invalid cached cursor via `CC70` and `0298`, then resume allocator | Absent |

The textual range in this table groups related blocks; the audit tool emits
the exact edges and channel sites. `3BDC` has an intra-block atomic retry;
`40B4` has an intra-block loop reading `SPU_RdDec` 2,400 times per delay.
These loops are additional to queued function transitions. Replacing the
decrementer reads with a host sleep would change the channel/state contract.

The low r4 flag bit 1 selects direct output at `r4 & ~3`. This branch skips
`3930`: its end EA uses the **unrounded** requested byte count. It issues no
allocator I/O, records GPU offset `-1` and synchronization from r4 bit 0.
The lane/shared paths round the request to 128. See the
[ring format](native-geometry-data.md#dma-and-output-ring) for line/result fields.

## Computed interrupt-helper boundary

`3930` sets r2 to link `3940`. `3940` adds eight, clears interrupt enable and
queues the indirect dispatcher at `3948`, which is registered in image 41.
The corresponding restore helper sets link `3CEC`, adds eight and enables
interrupts, giving target **`3CF4`**. That address has neither a named function
nor a registry entry in the pinned original lift. The textual `3B28` tail
following the indirect return in `3CEC` is unreachable C.

Do not substitute that tail or silently add a native-only shortcut. The full
resolver/interpreter behavior at `3CF4`, its actual LS instruction and its
state/continuation must be audited before accepting this cold path. Existing
whole-job checking explicitly rejects computed branches outside the compiled
audited image. A first native subset may reject this case before any writes
or I/O, with explicit fallback and no native-success credit. In the current
runtime `SPU_RdMachStat` returns `int_enable & 1`; all 7,201 observed exits
branch directly from `3B18` to `3B28`, bypassing the enable helper.

## External effects and strict checking

Lane allocation uses GETLLAR `D0` followed by **PUTLLUC `B0`**, not a conditional
CAS. The shared paths use GETLLAR/PUTLLC `B4` retry loops. Preserve the full
128-byte line, reservation/control state, every status read, transfer bytes,
GPU-cursor GET, tag selection/mask/update and interrupt transition in order.
An allocator error can be discovered only after input DMA; restarting the
original root then would repeat effects. An explicit original continuation
handoff must preserve current state and be counted as partial/fallback, or
the native path must implement the remaining behavior exactly.

Use the existing channel journal/replay and `buzz_geometry_job_io_root_begin`
and `buzz_geometry_job_io_root_end` for strict root checks inside JOB=2. The
original arm performs external effects once; native and local-reference
shadows consume the recorded operations. Cloning a context and running both
arms against real DMA/atomics is invalid.

The existing I/O-root checker returns null outside the JOB=2 replay shadow.
Consequently a new allocator switch value 2 with JOB=1 **must not** be credited
as checked merely because begin/end were called. Independent checked mode
needs a correct record/replay owner boundary, or must explicitly require
JOB=2 and decline unsupported checked requests before effects. Keep the new
switch default-off; do not alter existing Stage F's accepted contract. All
new allocator checks require every GPR/LS/architectural/channel/control field
and return continuation, without exclusions or state repair.

## Original evidence and reproduction

The existing original-only transform audit covers **19,508 exact jobs /
58,195 transfers**, zero concurrent input changes or failures. Rechecking its
sealed trace finds **7,201 allocator roots and returns / 14,422 ring transfers**:
7,201 GETLLARs, 7,201 matching PUTLLUCs and 20 cursor GETs. Each publication
matches the reservation's job, EA, LSA and size; every refresh lies between
that job's reservation and publication. Commands and expanded transfers
match in order. These are original-implementation observations, not native
acceptance, additional live calls or a speed measurement.

```powershell
.\toolenv\Scripts\python.exe -m unittest discover -s tests -p test_geometry_ring_audit.py -v
.\toolenv\Scripts\python.exe tools/inspect/geometry-ring-audit.py analysis/native-geometry/phase3/transform-audit-v1 --output analysis/native-geometry/phase3/ring-audit-fresh.json
```

The tool pins the original lift and tracer dependencies, cuts unconditional
direct/indirect dead tails, inventories computed boundaries and intra-block
loops, validates reservation/publication order and writes a fresh private
report with input/source hashes. Seven tests include altered publication EA,
cross-job refresh, reordered reservations and failed replay rejection.
Raw traces, guest addresses, snapshots and reports remain under ignored
`analysis/`. No production code, default, frozen EXE or save changed in this
audit. Complete native allocator and phase-3 final gates remain unfinished.
