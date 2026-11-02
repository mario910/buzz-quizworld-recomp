# Phase 3b: host index decoding

The decoder consumes packed deltas, flags and triangle commands directly into
host integer SoA. Eight u16 delta carries replace the intermediate LS arrays;
triangle edge rules replace command relocation and shuffle stitching.

| Check | Result |
| --- | --- |
| Existing corpus | All 19,508 entries / 19,745,661 logical indices exact against the complete original decoder |
| Observed encodings | Width 0 and widths 2–11 all pass; absent widths remain unsupported |
| Index bounds | Maximum real index 1,695; every real index is below its job's vertex count |
| Boundary arithmetic | 264 modified hot-width cases / 461,952 indices exact; modulo-u16 bias/carry wrap, varied payload bits and exact final payload byte |
| Invalid input handling | 18 unchanged-state rejects; malformed sections, unknown encodings, altered codec tables and late reference overrun clear readiness before job mutation |
| Independent reference | 22 complete original normal blocks; four reporting frontiers trapped; no native/model callbacks |

**Decision:** retain for integration with host culling. The index helper
accepts every captured index stream; the composed vertex pipeline still
retains its 109 fixed-format fallbacks. Native preparation changes no LS,
GPR or I/O state and allocates no heap storage. Logical indices are covered;
published padding, culling, runtime integration and whole-job/live/FPS/CPU
gates remain open. Runtime defaults are unchanged; no speedup is claimed.

Private evidence: `analysis/native-geometry/phase3/host-index-job-v1/`.
Previous producer and frozen runtime evidence is preserved.
