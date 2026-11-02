# Phase 3b: host input data

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
| Job-owned SoA capture, all 19,508 real entries | 19,399 captures / 54,481,150 logical component values exact against the complete original attribute graph; 109 fixed-format entries retain whole-job fallback |
| Primary streams across DMA entries | All 1,186 streams exceeding entry 8 pass the original-attribute comparison |
| Job input admission failures | 24 metadata/payload rejects leave LS/GPR/control and the caller's recipe unchanged; no stale readiness after a late invalid selector |

**Decision:** retain the tested helpers and job-owned input model for integration.
Whole-job output corpus,
live I/O, native frames and performance gates remain open for host input;
there is no FPS or CPU saving claim. The input model remains outside the
runtime; existing defaults stay off. Capture decodes logical vertices once
into bounded host-owned blocks without a heap allocation. It validates unit
selectors during decoding and discards failed preparation before LS/GPR/I/O
mutation. Publication-tail provenance remains required before integration.

Half input retains the first byte of the field's aligned source quad in its
low bits; IEEE half conversion would lose that provenance. Input admission
joins primary DMA entries 8–10: 1,186 captured primary streams exceed
entry 8 alone. Unknown descriptors still require whole-job lift fallback
before mutation. Next: eight-vertex skinning, transform and culling on these
persistent arrays, publication-tail provenance and direct serialization.

Private evidence: `analysis/native-geometry/phase3/host-input-v2/` and
`analysis/native-geometry/phase3/host-input-job-v1/`; original
decoder extraction remains pinned to the audited lift. Earlier evidence in
`host-input-v1` and frozen output measurements is preserved.
