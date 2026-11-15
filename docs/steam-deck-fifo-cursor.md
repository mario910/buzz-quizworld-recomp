# Steam Deck FIFO cursor cache-line experiment

Isolating the FIFO cursor did not improve menu performance. The private
candidate was rejected and the prepared executable restored byte-for-byte to
the qualified reference. Stable 30 FPS remains unmet.

## Evidence and candidate

Instruction annotation of the retained loss-free
[CPU profile](steam-deck-clip-operations.md) attributed 9.42% of FIFO samples
to the comparison immediately after loading `s_fifo_getoff`. The FIFO function
accounted for 14.70% of renderer-thread user cycles. These are sampled
instruction costs, not proof of cache misses or removable frame time.

The reference's cursor is at `0x71fb210`, sharing a 64-byte cache line with
the flip lock (`0x71fb204`), pending callbacks (`0x71fb218`) and callback-pump
guard (`0x71fb21c`). This suggested possible false sharing. The candidate
replaced only the private cursor storage with a 64-byte aligned and padded
object, retaining the same u32 value, all references and all control flow.
It added no runtime switch or changed compiler-profile settings.

Two pre-run audits caught prototype defects. An earlier tentative declaration
and pump use required placing the replacement before all references. LLVM
then scalarized the unused padding, so a `used` attribute was needed to retain
the complete object. Neither defective build was run. Final ELF inspection
confirmed one 64-byte object at `0x71fb240`, no overlapping symbols, and no
remaining legacy cursor symbol. The build reported no stale-profile mismatch.

Three existing FIFO fixtures compiled against the actual generated candidate
source passed ASan/UBSan: readiness and parked-jump recovery, flip-stream
completion, and GET/semaphore-release ordering. They exercise the early pump
as well as the drain. Their renderer stub aborts if the pure-state packet
consumer is called; these fixtures do not validate graphics. Subsequent menu
captures supply the limited live rendering check below.

## Clean menu comparison

Four fresh 90-second runs used frozen reference A and candidate B in ABBA
order. Settings matched: X11, 1280×720, 100% scale, uncapped mailbox, batch 512,
two contexts, zero worker spin and image-36 search enabled. FPS covers seconds
40–70, process CPU covers 45–65, and captures occur at second 80. No build,
CPU test or profiler overlapped the measurements.

| Arm | FPS | p95 interval ms | Lowest 1 s FPS | CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 | 28.700 | 41.089 | 25 | 153.646 |
| B1 | 28.467 | 42.058 | 22 | 154.860 |
| B2 | 28.600 | 40.948 | 26 | 154.621 |
| A2 | 28.433 | 41.491 | 24 | 155.661 |

Reference and candidate means were 28.567 and 28.533 FPS. Mean process CPU
cost was 154.654 and 154.741 ms/frame. About 67.45% and 67.64% of intervals
respectively exceeded 33.34 ms. There is no demonstrated performance benefit.
All runs progressed beyond second 88 with presentation deltas of one, and all
four captures were inspected and intact. Fixed elapsed windows and later
screenshots do not certify identical camera phase for every timed frame.
No full-round qualification or promotion was warranted.

## Artifacts and restoration

Private evidence is under
`/home/deck/buzz/performance/optimization/codex-fifo-page/`: instruction
annotation, original/candidate generated source, rejected prototype sources,
build and sanitizer logs, ELF symbols, frozen candidate, ABBA logs and frame
CSVs, CPU telemetry, screenshots, settings comparison, summary and restoration
script/result. The directory name reflects the initial mapping investigation;
no page-translation optimization was integrated. The GCM translation unit uses
the SDK's inline direct-memory `vm_read32`, distinct from the loader's exported
diagnostic accessor.

Candidate SHA-256:
`fe8dd7d4e35885df9ab1d0aea3723595f283cfe96096fdcb0778096644c1f33b`.
Restored prepared reference:
`630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d`.
The installed executable is unchanged (`189814d1…`), SDK status is clean, and
root production source and optimization defaults are unchanged.
