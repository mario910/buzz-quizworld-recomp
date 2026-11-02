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
| Reversed menu/round FPS and CPU | Pending for this profile |

**Decision:** controller/configuration, corpus, live I/O and native-frame
gates pass. Keep the master default off pending reversed FPS/CPU. The
[previous profile's gains](native-geometry-phase3b-host-pipeline-results.md)
remain separate evidence; validate this profile before default-on.

Private evidence: `analysis/native-geometry/phase3/compact-profile-v2/`.
