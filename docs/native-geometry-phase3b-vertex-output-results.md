# Phase 3b: final host vertex bytes

The direct serializer consumes persistent host SoA and writes each final
chunk once. A single extra position supplies observable alignment bytes,
using cached matrix/palette data and the shared point transform math.

| Check | Result |
| --- | --- |
| Visible real jobs | 5,801 final buffers exact: 5,561 object and 240 weighted |
| Final vertex allocations | 20,705,968 bytes, including 22,372 alignment bytes in 2,190 jobs |
| Observed strides | 12, 16, 20 and 24; dense float/Edge-half/CMP fields |
| Independent reference | 256 complete original blocks composing input, skin, indices, clip/cull and final conversion |
| Publication FP cases | 96 modified observed-family fixtures / 214,272 complete vertex bytes exact |
| Bounds and rejection | Guarded output ends and prefix canaries; short buffers plus 8 metadata, 9 tail and 7 output rejects preserve source/destination |
| Remaining visible rejections | 1,106 unsupported output-storage jobs; 260 padded positions with out-of-palette skin indices |
| Earlier composed proofs | All 19,399 recognized logical producers and aligned index allocations remain exact; existing FP/rejection checks pass |

**Decision:** retain for full-job integration. Rejected recipes/tails require
the whole lifted job before mutation. Runtime routing/defaults are unchanged;
whole-job corpus/I/O/exit, live, frame and FPS/CPU gates remain open.

Private evidence: `analysis/native-geometry/phase3/host-vertex-job-v1/`.
