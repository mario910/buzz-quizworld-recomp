# Phase 3b: compact job profile

In a phase-3 build, `BUZZ_GEOMETRY_JOB=1` selects the host pipeline. Its
`HOST_PIPELINE`, `PUBLICATION` and `COMMANDS` group switches default to one;
disabling any group selects the complete lifted job before entry mutation.
`BUZZ_GEOMETRY_JOB_LEGACY=1` exposes the old diagnostic stage switches.
The master `BUZZ_GEOMETRY_JOB` still defaults to zero.

| Check | Result |
| --- | --- |
| Vulkan freeze | `build-vulkan-host-pipeline-compact-v2`, EXE `0c17df76`, explicit renderer manifest |
| Existing controller and diagnostic options | Pass; 504 option cases |
| Production groups | 51 routing, legacy isolation, cache, invalid-option and missing-coverage cases pass |
| Whole-job corpus | All eight arms: 19,508 exact jobs / 58,195 transfers; zero differences or concurrent-input exclusions |
| Explicit and implicit JOB1 / JOB2 | 17,437 native jobs; 2,071 whole-job fallbacks; JOB2 checks all 17,437 captured native I/O transcripts |
| Any required group off | 19,508 whole-job fallbacks; zero native jobs and zero errors |
| Live JOB2 | At least 1,275,000 completed whole-job I/O comparisons; concurrent-input skips excluded |
| Native menu and round | Both JOB1 GPU frames inspected; normal geometry |
| Measurement profile | Six recorded settings: JOB, three groups, LEGACY=0 and AUDIT=0; debug checkers off; 21 report tests pass; four prior reports reproduce unchanged |
| Reversed menu FPS | JOB0 28.342 → JOB1 29.336 FPS: +3.510%; pairs +2.139% / +4.906% |
| Reversed menu workers / completed frame | 423.600M → 385.658M cycles (−8.957%); 115.026 → 105.173 CPU ms (−8.566%); both pairs improve |
| Reversed menu process / completed frame | 776.125M → 737.942M cycles (−4.920%); 211.081 → 200.740 CPU ms (−4.899%); both pairs improve |
| Reversed round FPS | JOB0 32.760 → JOB1 33.694 FPS: +2.849%; pairs +4.735% / +1.030%; all four frames inspected |
| Reversed round CPU | Pending separate series for this profile |

**Decision:** controller/configuration, corpus, live I/O and native-frame
menu FPS/CPU and round FPS gates pass, with gains in both reversed pairs.
Keep the master default off pending round CPU and normal-build integration. The
[previous profile's gains](native-geometry-phase3b-host-pipeline-results.md)
remain separate evidence; validate this profile before default-on.

Private evidence: `analysis/native-geometry/phase3/compact-profile-v2/`.
