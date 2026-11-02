# Phase 3b: final host index bytes

`native_geometry_host_index_output.h` writes compacted host indices directly
as BE u16 values and computes their final alignment bytes. Culling records
only the number retained from the last source group of four.

| Check | Result |
| --- | --- |
| Complete original composed producer | 19,399 recognized jobs; 109 fixed-format jobs retain fallback |
| Final index allocations | 5,280,336 bytes exact, including 44,550 alignment bytes and all 465 nonzero padding bytes |
| Tail coverage | All 40 last-group count/alignment combinations observed |
| Buffer boundaries | Every real and modified FP output ends at PAGE_NOACCESS; prefix canaries intact; short buffers rejected before writing |
| Admission | Seven unchanged-destination rejects and an empty stream with a null destination pass |
| Existing composed checks | Logical input/skin/clip/cull checks and 160 hot-family FP cases remain exact |

**Decision:** retain for direct runtime integration. The original final
aligned store sometimes repeats bytes from the last retained group into
padding; a bounded byte rule reproduces them without the SPU pipeline.
Vertex output tails, full job I/O/exit controls, live comparisons, frames and
FPS/CPU gates remain open. Defaults and runtime routing are unchanged.

Private evidence: `analysis/native-geometry/phase3/host-index-output-v1/`.
