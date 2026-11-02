# Phase 3a measurements

Status: in progress. Default remains JOB0; new options remain off.
V1–V3 record the source state at each freeze. The implementation is now an
experimental checkpoint; these rejected measurements do not establish native
performance acceptance.

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

## V2: owner-local ledger updates and reused job setup admission

Frozen `build-vulkan-phase3a-v2`: EXE `3f4a745f`, MAP `7f580c1a`,
base `5fb24da` plus archived source patch and new inputs; implementation WIP.
Same menu protocol as V1, with fresh saves and JOB0/1/1/0 in each series.
Every run confirms Vulkan mailbox and two-context asynchronous draw batches.

| Metric | JOB0 | JOB1 | Change | Reversed pairs |
| --- | ---: | ---: | ---: | ---: |
| FPS, primary FPS series | 28.229 | 27.715 | -1.822% | -1.487% / -2.153% |
| Worker cycles/frame | 423.389M | 445.874M | +5.311% | +5.578% / +5.045% |
| Worker CPU ms/frame | 114.696 | 120.510 | +5.069% | +4.885% / +5.255% |
| Process cycles/frame | 774.329M | 801.735M | +3.539% | +3.698% / +3.381% |
| Process CPU ms/frame | 209.187 | 216.418 | +3.457% | +3.368% / +3.545% |

| Correctness/render gate | Result |
| --- | --- |
| Full corpus, four arms | Each 19,508 exact jobs / 58,195 transfers; zero divergent inputs/failures |
| Live whole-job I/O | >=50,000 comparisons pass |
| Actual-native menu frame | Inspected 1280x720 GPU readback; normal studio menu |
| Round frames/FPS/CPU | Not attempted for this rejected menu candidate |

**Decision:** reject for menu parity/default-on; continue 3a with a separate
JOB0/JOB1 diagnostic profile. Both FPS pairs and all CPU pairs regress.
V1 and V2 use different frozen executables; this is not a causal V1-to-V2
gain claim. Phase 3a remains incomplete and defaults remain off.

Private evidence: `analysis/native-geometry/phase3/phase3a-v2/seal.json`,
169 artifact proofs / 333 archived sources / 82 prior immutable pins verified.

## V3: reused parent command reservation/publication admission

Frozen `build-vulkan-phase3a-v3`: EXE `ba4c74ef`, MAP `506d1fc0`,
base `6541fcf` plus archived source patch and new inputs; implementation WIP.
Same menu protocol as V1/V2, fresh JOB0/1/1/0 FPS and CPU series.
Every run confirms Vulkan mailbox and two-context asynchronous draw batches.

| Metric | JOB0 | JOB1 | Change | Reversed pairs |
| --- | ---: | ---: | ---: | ---: |
| FPS, primary FPS series | 28.456 | 27.664 | -2.784% | -2.218% / -3.348% |
| Worker cycles/frame | 427.307M | 446.184M | +4.418% | +3.997% / +4.838% |
| Worker CPU ms/frame | 116.247 | 120.865 | +3.972% | +3.658% / +4.286% |
| Process cycles/frame | 782.540M | 801.743M | +2.454% | +2.214% / +2.693% |
| Process CPU ms/frame | 212.831 | 217.014 | +1.966% | +2.036% / +1.895% |

| Correctness/render gate | Result |
| --- | --- |
| Full corpus, four arms | Each 19,508 exact jobs / 58,195 transfers; zero divergent inputs/failures |
| Live whole-job I/O | >=50,000 comparisons pass |
| Actual-native menu frame | Inspected 1280x720 GPU readback; normal studio menu |
| Round frames/FPS/CPU | Not attempted for this rejected menu candidate |

**Decision:** reject for menu parity/default-on. Both FPS pairs and all CPU
pairs regress; this variant offers no demonstrated CPU saving. Continue 3a;
phase 3a remains incomplete and defaults remain off. Different frozen builds
do not establish a causal V1/V2/V3 comparison.

Private evidence: `analysis/native-geometry/phase3/phase3a-v3/seal.json`,
498 artifact proofs / 333 archived sources / 82 prior immutable pins verified.

## Host layout V1: semantic metadata, no forecast register banks

Frozen `build-vulkan-format-layout-v1`: EXE `1bdab921`, MAP `defb4613`,
base `6cc7513` plus archived patch/new inputs. Same menu JOB0/1/1/0 protocol,
fresh separate FPS/CPU series. Every run confirms Vulkan mailbox and
asynchronous two-context batches.

| Metric | JOB0 | JOB1 | Change | Reversed pairs |
| --- | ---: | ---: | ---: | ---: |
| FPS, primary FPS series | 28.535 | 27.905 | -2.207% | -2.650% / -1.761% |
| Worker cycles/frame | 424.670M | 441.807M | +4.035% | +6.086% / +2.031% |
| Worker CPU ms/frame | 115.213 | 119.471 | +3.695% | +5.257% / +2.169% |
| Process cycles/frame | 778.084M | 798.054M | +2.567% | +3.709% / +1.439% |
| Process CPU ms/frame | 211.618 | 214.847 | +1.526% | +2.103% / +0.958% |

| Correctness/render gate | Result |
| --- | --- |
| Metadata admission | 50,000 independent legacy-reader comparisons, including 25,000 parent forecasts; identical verdicts/counts and unchanged inputs |
| Encoder admission | 400,000 trials x seven encoders match independent old walks |
| Full corpus, four arms | Each 19,508 exact jobs / 58,195 transfers; zero divergences/failures; new job-boundary and optional full-state debug both pass |
| Live whole-job I/O | >=1,455,000 completed comparisons PASS; concurrent-input skips excluded (logging capped) |
| Actual-native menu frame | Inspected 1280x720 GPU readback; normal studio menu |
| Round frames/FPS/CPU | Not attempted for this rejected menu candidate |

**Decision:** reject for menu parity/default-on. Both FPS pairs and all CPU
pairs regress. The copy/vector/admission changes of 3a are checkpointed;
its FPS target remains unmet. Proceed with the required 3b host data pipeline
for known hot formats; native performance acceptance remains open. Defaults
stay off. Different freezes do not establish causal cross-build gains.

Private evidence: `analysis/native-geometry/phase3/format-layout-v1/seal.json`,
526 artifact proofs / 338 archived sources / 82 prior immutable pins verified.
