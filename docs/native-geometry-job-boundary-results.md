# Job-boundary gate validation

Frozen `build-vulkan-job-boundary-v1`: EXE `19f40c59`, MAP `2c06e8d3`,
base `38972bd` plus archived patch/new inputs; renderer Vulkan.

| Setting | Contract |
| --- | --- |
| `BUZZ_GEOMETRY_JOB=2`, `BUZZ_GEOMETRY_DEBUG_STAGES=0` (default) | Exact ordered DMA/atomic/channel I/O, published bytes, final architectural controls, PC and host continuation/state |
| `BUZZ_GEOMETRY_DEBUG_STAGES=1` with JOB2 | Also retain the existing full GPR/LS and stage diagnostics |
| Explicit per-stage mode 2 | Existing optional stage diagnostics remain available |
| `BUZZ_REPLAY_CONTRACT=1` | Corpus compares published transfers and captured job exit controls |
| `BUZZ_REPLAY_CONTRACT=0` or unset | Also compare full exit GPR/LS; invalid flag values fail |

| Validation | Result |
| --- | --- |
| Corpus JOB2/contract1/debug0 | 19,508 exact jobs / 58,195 transfers; all jobs compared natively, no fallback |
| Corpus JOB0/contract0, JOB1/contract1, JOB2/contract0/debug1 | Each 19,508 exact jobs / 58,195 transfers; zero input divergences/failures |
| Real-job fault fixtures | 12 pass: internal exit GPR/LS allowed only by contract1; altered PUT/exit PC still rejected; invalid values rejected |
| Controller/configuration tests | 496 getter cases; output/control/host faults rejected; optional debug catches GPR/LS faults |
| I/O and production driver fixtures | Pass; legacy formatter debug checks and corruption probes preserved |
| Live JOB2/contract boundary | >=1,415,000 completed I/O comparisons PASS; concurrent-input skips excluded (skip logging capped) |
| Native JOB1 menu frame | Inspected 1280x720 GPU readback; normal studio menu |
| Runtime conditions | Both live logs confirm mailbox and asynchronous two-context batches |

**Decision:** the job-boundary checker is functionally verified and full-state
debugging remains available. The first live script rejected concurrent-input
skips; its log is retained. Skipped jobs are not comparisons and do not enter
the PASS count. There were no live output/control differences.

This is an experimental implementation checkpoint, not native performance
acceptance. Phase 3a's menu parity target remains unmet; defaults stay off.
No new FPS/CPU or round acceptance is claimed for this frozen EXE.

Private evidence: `analysis/native-geometry/phase3/job-boundary-v1/seal.json`,
975 artifact proofs / 334 archived sources / 82 prior immutable pins verified.
