# Native geometry phase 3b results

## Host output V1: float / half / CMP

Frozen `build-vulkan-host-soa-v1`: EXE `8d28d4a6`, MAP `e7727886`,
base `5f7ed62` plus archived patch/new inputs. `BUZZ_GEOMETRY_HOST_OUTPUT`
defaults to `0`. Output conversion uses eight vertices in host SoA and one
final LS write per chunk; upstream producers still use LS float4 tables.
Unsupported descriptors fall back to the whole lifted job before mutation.

Same-EXE JOB0/1/1/0, fresh saves, separate FPS/CPU series. Windows:
menu 40–98 seconds; round 200–270 seconds. Every completed run confirms Vulkan mailbox and
asynchronous two-context batches.

| Menu metric | JOB0 | JOB1 | Change | Reversed pairs |
| --- | ---: | ---: | ---: | ---: |
| FPS, primary series | 28.181 | 28.453 | +0.964% | +0.428% / +1.500% |
| Worker cycles/frame | 428.349M | 420.301M | -1.879% | -2.109% / -1.650% |
| Worker CPU ms/frame | 116.037 | 114.546 | -1.284% | -1.488% / -1.083% |
| Process cycles/frame | 781.287M | 776.142M | -0.658% | -1.027% / -0.290% |
| Process CPU ms/frame | 211.472 | 210.761 | -0.336% | -0.891% / +0.219% |

| Round metric | JOB0 | JOB1 | Change | Reversed pairs |
| --- | ---: | ---: | ---: | ---: |
| FPS, primary series | 33.142 | 33.116 | -0.077% | -0.130% / -0.024% |
| Worker cycles/frame | 339.103M | 344.983M | +1.734% | -0.024% / +3.494% |
| Worker CPU ms/frame | 92.444 | 93.657 | +1.312% | -1.599% / +4.265% |
| Process cycles/frame | 588.976M | 598.102M | +1.549% | +0.013% / +3.086% |
| Process CPU ms/frame | 160.910 | 162.684 | +1.103% | -1.268% / +3.500% |

| Gate | Result |
| --- | --- |
| Full corpus, four arms | Each 19,508 exact jobs / 58,195 transfers; zero divergent inputs/failures |
| Host-enabled corpus | 17,719 native jobs; 1,789 whole-job fallbacks; checked arm compares all 17,719 |
| Live whole-job I/O | >=1,395,000 completed comparisons PASS; concurrent-input skips excluded |
| Actual-native menu frame | Inspected 1280x720 GPU readback; normal studio menu |
| Actual-native round and FPS frames | Inspected; normal green arena, host/player and Poprawne 0; FPS scene gate passes |
| Round CPU | Original entire series rejected: run 3 captured a question overlay; complete fixed-ready repeat inspected and reported; all captures show the same arena without question text, with camera/pose/score-panel variation recorded |

**Decision:** reject default-on/aggregate menu-and-round acceptance. Menu
FPS and worker cycles improve; round FPS is near zero and does not establish
parity, while round CPU results are mixed and worse on average. Keep the
validated output implementation experimental and off. Full 3b remains
incomplete: decode Edge streams once
into persistent host SoA, move skin/transform/cull to eight-vertex AVX2 and
continue the remaining index/shared work. Structural 3a alone remains a
rejected earlier measurement; this result does not establish cross-build
causality.

Private menu evidence: `analysis/native-geometry/phase3/host-soa-v1/menu-seal.json`,
868 artifact proofs / 342 archived sources / 82 prior immutable pins verified.
Round evidence: `analysis/native-geometry/phase3/host-soa-v1/round-seal.json`,
987 proofs / 342 sources / 82 prior pins verified; all four reports recompute.
Rejected CPU data and initial QA-schema failures are preserved. No causal
cross-build or isolated-output speedup is claimed.
