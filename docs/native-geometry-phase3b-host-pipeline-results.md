# Phase 3b: integrated host job pipeline

`BUZZ_GEOMETRY_JOB_HOST_PIPELINE=1` prepares persistent host SoA before job
mutation, then skips the legacy attribute, skin, index and transform/cull
calls. It writes final index/vertex streams directly and retains the ordered
publication/command flow. Each worker allocates reusable storage once.

| Check | Result |
| --- | --- |
| Vulkan freeze | `build-vulkan-host-pipeline-v1c`, EXE `5e1e355e`, explicit renderer manifest |
| Full corpus, JOB0/JOB1/JOB2 | Each arm: 19,508 exact jobs / 58,195 transfers; zero differences or input-change exclusions |
| Native dispatch, JOB1/JOB2 | 17,437 host jobs; 2,071 whole-job fallbacks |
| Live JOB2 | At least 1,275,000 completed whole-job I/O comparisons; concurrent-input skips excluded |
| Actual native menu | GPU frame inspected; normal studio/menu geometry |
| Actual native round | GPU frame inspected; normal solo arena and character geometry |
| Reversed menu FPS | JOB0 28.369 → JOB1 29.422 FPS: +3.712%; pairs +3.709% / +3.715% |
| Reversed menu worker CPU / completed frame | 424.638M → 387.830M cycles (−8.668%); 115.533 → 105.169 CPU ms (−8.971%) |
| Reversed menu process CPU / completed frame | 778.511M → 741.651M cycles (−4.735%); 210.286 → 202.232 CPU ms (−3.830%) |
| Reversed round FPS | JOB0 33.120 → JOB1 33.604 FPS: +1.460%; pairs +1.851% / +1.073%; all four frames inspected |
| Reversed round CPU | Initial incomplete series rejected; fresh full series pending. GPU navigation audit confirms correct entry and natural question/camera transitions |
| Measurement validation | 19 report tests pass; inspected-but-rejected frames invalidate the whole series; accepted reports reproduce unchanged |

The first two candidates skipped additional blend-stream DMA. Those jobs
now reject from their entry metadata before mutation; their complete lifted
flow is retained. All failed candidates and logs remain private evidence.

**Decision:** menu FPS/CPU and round FPS improve in both reversed pairs. Keep
experimental/default off pending round CPU and switch consolidation;
this does not complete phase 3b.

Private evidence: `analysis/native-geometry/phase3/host-pipeline-v1/`.
