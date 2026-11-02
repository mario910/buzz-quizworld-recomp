# Phase 3b: host triangle culling

AVX2 evaluates eight host triangles and compacts their u16 index arrays in
place, preserving order. It consumes host clip records directly, without LS
gathers, SPU registers, pipeline iterations or table-driven stores.

| Check | Result |
| --- | --- |
| Composed real pipeline | 19,399 input/skin/index/clip/cull jobs exact; 109 fixed-format jobs retain fallback |
| Retained indices | 2,617,893 logical indices exact in order; 12,232 jobs fully culled |
| Observed cull modes | Mode 1: 6,970 jobs; mode 2: 10,393; mode 3: 2,036 |
| Independent original flow | 191 original blocks, including index decoding and complete transform/cull; read-only observations verify winding and mask parameters |
| Preceding producer checks | 54,481,150 input/skin values and 24,406,612 clip values remain exact in the composed flow |
| Boundary checks | 160 modified hot-family FP cases pass; all eight mask-FNMS truth cases exact; 16 cull rejects preserve LS/GPR/control and owned index arrays |
| Original aligned index padding | 44,550 bytes observed; 465 are nonzero. Neither zero-fill nor copying entry LS is an established publication rule |

**Decision:** retain the logical culling implementation for integration.
The source's orientation selector uses configuration byte 6; correcting an
initial byte-3 interpretation resolved the first mismatch. The remaining
publication work must derive the nonzero index padding and vertex padding
before whole-job output validation. Runtime routing/defaults and performance
claims are unchanged; this does not close the publication/live/FPS/CPU gates.

Private evidence: `analysis/native-geometry/phase3/host-cull-job-v1/`.
Initial mismatch and rejected zero-padding hypothesis logs are preserved.
