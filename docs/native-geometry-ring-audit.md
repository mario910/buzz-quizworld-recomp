# Image-41 output allocator: implementation contract

This reference describes lifted behavior and retained diagnostic kernels. For
active routing, defaults and correctness requirements, use [native
jobs](native-jobs.md) and [runtime defaults](runtime-defaults.md).

## Audited boundary and control flow

`37C0` takes geometry context in r3, ring EA/flags in r4, requested bytes in r5,
result LS address in r6 and guest lane in r7. It creates a 1,344-byte frame,
preserves r80..r96/r126/r0 and returns through `38D0`. Full vector register
contents, ordered LS reads/writes and the actual return continuation are part of
the contract. Address wrapping and quadword flooring must match the SPU helpers,
including aliases with result, scratch or saved frame data.

The pinned lift contains **39 allocator blocks**, two reporting frontiers and an
unresolved computed continuation. This is a bounded inventory, not proof that
all paths have been exercised or that all computed targets are lifted.

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

The textual range in this table groups related blocks; the audit tool emits the
exact edges and channel sites. `3BDC` has an intra-block atomic retry; `40B4`
has an intra-block loop reading `SPU_RdDec` 2,400 times per delay. These loops
are additional to queued function transitions. Replacing the decrementer reads
with a host sleep would change the channel/state contract.

The low r4 flag bit 1 selects direct output at `r4 & ~3`. This branch skips
`3930`: its end EA uses the **unrounded** requested byte count. It issues no
allocator I/O, records GPU offset `-1` and synchronization from r4 bit 0. The
lane/shared paths round the request to 128. See the [ring
format](native-geometry-data.md#dma-and-output-ring) for line/result fields.

## Computed interrupt-helper boundary

`3930` sets r2 to link `3940`. `3940` adds eight, clears interrupt enable and
queues the indirect dispatcher at `3948`, which is registered in image 41. The
corresponding restore helper sets link `3CEC`, adds eight and enables
interrupts, giving target **`3CF4`**. That address has neither a named function
nor a registry entry in the pinned original lift. The textual `3B28` tail
following the indirect return in `3CEC` is unreachable C.

The pinned real LS now confirms a `br 0x3B28` instruction at `3CF4`. This does
not make the unreachable C tail executable: the current full resolver halts on
an unregistered target unless interpreting unlifted code is explicitly enabled.
Do not silently add a native-only shortcut. Resolver/interpreter acceptance and
any original-adapter repair remain separate work. Existing whole-job checking
explicitly rejects computed branches outside the compiled audited image. The new
reader rejects this case before any writes or I/O, with no native-success
credit. In the current runtime `SPU_RdMachStat` returns `int_enable & 1`; all
7,201 observed exits branch directly from `3B18` to `3B28`, bypassing the enable
helper.

## External effects and strict checking

Lane allocation uses GETLLAR `D0` followed by **PUTLLUC `B0`**, not a
conditional CAS. The shared paths use GETLLAR/PUTLLC `B4` retry loops. Preserve
the full 128-byte line, reservation/control state, every status read, transfer
bytes, GPU-cursor GET, tag selection/mask/update and interrupt transition in
order. An allocator error can be discovered only after input DMA; restarting the
original root then would repeat effects. An explicit original continuation
handoff must preserve current state and be counted as partial/fallback, or the
native path must implement the remaining behavior exactly.

Use the existing channel journal/replay and `buzz_geometry_job_io_root_begin`
and `buzz_geometry_job_io_root_end` for strict root checks inside JOB=2. The
original arm performs external effects once; native and local-reference shadows
consume the recorded operations. Cloning a context and running both arms against
real DMA/atomics is invalid.

The existing I/O-root checker returns null outside the JOB=2 replay shadow.
Consequently a new allocator switch value 2 with JOB=1 **must not** be credited
as checked merely because begin/end were called. Independent checked mode needs
a correct record/replay owner boundary, or must explicitly require JOB=2 and
decline unsupported checked requests before effects. Retained per-function
checks compare full internal state; whole-job acceptance uses the observable
contract described in the runtime documentation.

## Retained allocation kernel

`port/native_geometry_ring.h` implements the frame, direct output, lane
selection/wrap/refresh/publication, both shared allocation loops, post-fallback
lane reconsideration, decrementer polling and result/frame restoration. It
caches all 128 vectors in host memory and materializes them once at the return
or reporting handoff. Every LS access remains ordered and observes quadword
flooring/u32 wrapping; frame restores read the actual late LS bytes. Channel
operations use the existing job wrappers, including every conditional atomic
attempt and all 2,400 decrementer reads in a delay. No host sleep, native-only
state repair or Stage F exception is used.

The reader rejects foreign LS/image/overlay/policy and enabled or saved
interrupt state before mutation. At either invalid-cursor frontier, the kernel
queues the supplied **original reporting continuation**, with its exact entry
PC/GPR/LS/control state. It does not execute the reporters or restart allocation
after DMA. Future production accounting must distinguish this retained
continuation from a fully native allocator return; it cannot count as complete
native coverage. The caller must supply both valid reporting functions before
admission.

The retained kernel is wired through the public `37C0` adapter. The original
entry body, internal blocks and reporting continuations remain available.
Runtime ownership is described below.

All 35 supported non-reporting blocks and both reporting-entry boundaries are
exercised; `3CE8/3CEC` and reporter bodies remain retained. The real fixture
comes from the unchanged original tracer replaying one captured menu job: **1
exact job / 6 transfers**, zero input changes or failures. It includes allocator
entry, post-GETLLAR input and allocator return from the same job. These
snapshots contain GPR/LS/PC, not complete architectural/channel state; the
full-state standalone comparison supplies modeled identical controls. This is
one real-data anchor, not full native corpus/live acceptance.

## Runtime ownership

The active geometry job owns ring/publication routing. The legacy
`BUZZ_GEOMETRY_JOB_RING` switch is mapped through the production publication
group outside legacy mode; its historical standalone default does not select the
current job policy. Whole-job mode 2 must replay effects into its shadow, never
issue real atomics or DMA twice. See [runtime
controls](native-jobs.md#dispatch-controls-and-diagnostics).
