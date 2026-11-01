# Phase 3a measurements

Status: in progress. Default remains JOB0; new options remain off.

## V1: shared job bank, bounded plans and reused formatter admission

Frozen `build-vulkan-phase3a-v1`: EXE `fd451798`, MAP `db12df3d`,
base `2f7ab9c` plus archived source patch; implementation still uncommitted.
Eight fresh menu runs: separate FPS/CPU series, each JOB0/1/1/0, same EXE,
save `92ed9caf`, window40..98s, mask827/StageF1. Every run confirms Vulkan
mailbox and two-context asynchronous draw batches.

| Metric | JOB0 | JOB1 | Change | Reversed pairs |
| --- | ---: | ---: | ---: | ---: |
| FPS, primary FPS series | 28.378 | 27.616 | -2.684% | -2.368% / -2.998% |
| Worker cycles/frame | 422.526M | 442.980M | +4.841% | +4.728% / +4.954% |
| Worker CPU ms/frame | 113.967 | 119.339 | +4.714% | +5.585% / +3.849% |
| Process cycles/frame | 777.585M | 797.811M | +2.601% | +2.568% / +2.635% |
| Process CPU ms/frame | 210.816 | 215.890 | +2.407% | +2.825% / +1.989% |

| Correctness/render gate | Result |
| --- | --- |
| Full corpus, four arms | Each19,508exact jobs/58,195transfers;zero divergent inputs/failures |
| Live whole-job I/O | >=50,000 comparisons pass |
| Actual-native menu frame | Inspected1280x720 GPU readback;normal studio menu |
| Round frames/FPS/CPU | Not attempted for this rejected menu candidate |

**Decision:** reject for menu parity/default-on. Continue3a; investigate the
remaining admission and ledger costs. No phase-completion or cross-build gain
claim. CPU-series FPS is contextual and does not replace the primary FPS row.

Private evidence: `analysis/native-geometry/phase3/phase3a-v1/seal.json`,
207 artifact proofs/329 archived sources/82 prior immutable pins verified.
