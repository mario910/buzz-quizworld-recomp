# Phase 3b: host input arithmetic

Standalone preparation for the host pipeline; no runtime route or default
changes. Eight AVX2 lanes represent eight vertices. Helpers return host SoA
without an SPU register bank, call frame, scratch stores or lookahead.

| Check | Result |
| --- | --- |
| Packed unit vectors, complete original decoder | 57,344 vertices exact; all 12 admitted selectors, coordinate boundaries and mixed lanes |
| Edge half input, complete original decoder | 16,777,216 values exact: all 65,536 encodings x all 256 aligned-prefix bytes |
| Source fields, complete original descriptor decoder | 4,096 synthetic cases / 399,360 component-lane checks exact; odd/even strides, quad crossings, mixed prefixes and all eight-lane tails |
| Source bounds, inaccessible following page | 73,728 float shapes, 9 position shapes, 144 half shapes and 54 unit-vector shapes pass; no inactive-row or wide overread |
| Existing host output after shared SoA type extraction | 3,145,728 half/signed31 values, 1,048,576 CMP vertices, 678 formatter byte comparisons and 16 unchanged recipe rejects pass |

**Decision:** retain these tested helpers for integration. Whole-job corpus,
live I/O, native frames and performance gates remain open for host input;
there is no FPS or CPU saving claim. Existing runtime defaults stay off.

Half input retains the first byte of the field's aligned source quad in its
low bits; IEEE half conversion would lose that provenance. Future admission
must also join primary DMA entries 8–10: 1,186 captured primary streams exceed
entry 8 alone. Unknown descriptors still require whole-job lift fallback
before mutation. Next: persistent input SoA, then eight-vertex skinning,
transform and culling, with serialization consuming those arrays directly.

Private evidence: `analysis/native-geometry/phase3/host-input-v2/`; original
decoder extraction remains pinned to the audited lift. Earlier evidence in
`host-input-v1` and frozen output measurements is preserved.
