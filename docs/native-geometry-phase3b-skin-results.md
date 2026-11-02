# Phase 3b: host skin and object transforms

Eight AVX2 lanes process eight vertices in the existing job-owned SoA.
Palette words and influences are decoded once. Computation keeps SPU FP
ordering without a register bank, LS matrix scratch or ABI reconstruction.

| Check | Result |
| --- | --- |
| Existing corpus input families | 19,508 entries inspected; 19,399 producer comparisons exact; 109 fixed-format entries remain unsupported |
| Weighted skin | 1,628 real jobs / 716,781 logical vertices exact |
| Object transform and direction normalization | 17,771 real jobs / 5,384,872 logical vertices exact |
| Complete original producer reference | 147 original attribute/skin blocks, including full 9F8/AF18 paths; 54,481,150 logical component values exact; no native/model data callbacks |
| FP boundary inputs | 96 modified hot-family cases / 52,320 component values exact: signed zeros, denormals, extended SPU exponents, sparse/dense/unnormalized weights |
| Failure handling | 14 metadata, codec and payload rejects leave LS/GPR/control unchanged and invalidate host readiness; includes a late invalid index with zero weight |

**Decision:** retain this producer for integration. No additional real-family
fallbacks were introduced. It consumes persistent input arrays directly and
does not export temporary BE float4 tables. This is logical producer evidence;
publication padding, transform/cull/index integration, exact whole-job I/O,
live frames and reversed FPS/CPU gates remain open. Runtime routing and
defaults are unchanged; no performance improvement is claimed.

Private source/oracle/test evidence:
`analysis/native-geometry/phase3/host-skin-job-v1/`. The original lift stays
pinned; earlier input and frozen runtime evidence is preserved.
