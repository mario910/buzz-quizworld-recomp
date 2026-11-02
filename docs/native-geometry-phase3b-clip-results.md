# Phase 3b: host clip transform

Clip parameters are computed once from the viewport and job default matrix.
AVX2 projects eight skinned host vertices into host SoA clip records for
culling. No register bank, LS scratch or SPU pipeline state is reconstructed.

| Check | Result |
| --- | --- |
| Existing corpus | All 19,508 entries inspected; 19,399 input/skin/clip producer comparisons exact; 109 fixed-format entries remain unsupported |
| Clip records | 6,101,653 logical vertices / 24,406,612 component values exact against complete original setup and projection |
| Composed input and skin | 54,481,150 logical component values remain exact before original projection |
| Actually observed clip formats | Format 0: 10,664 jobs; format 3: 8,735; other formats rejected before job mutation |
| FP corners | 160 modified hot-family cases / 27,840 clip component values exact; covers source and matrix special values, viewport sums/wrap and reversed/denormal depth bounds |
| Failure handling | 9 clip readiness/metadata/codec rejects preserve LS/GPR/control; prior 14 skin rejection checks also pass |
| Independent reference | 152 complete original attribute, skin and clip blocks; no native/model data callbacks |

**Decision:** retain for integration. Clip results stay in flat host SoA
arrays for indexed culling; input and skin arrays are consumed directly.
No additional real-family fallback was introduced. These are logical producer
checks, not the whole-job publication gate. Index decoding, culling, published
padding, runtime integration and all live/FPS/CPU gates remain open. Runtime
routing and defaults are unchanged; no performance improvement is claimed.

Private evidence: `analysis/native-geometry/phase3/host-clip-job-v1/`.
Earlier input, skin and frozen runtime evidence is preserved.
