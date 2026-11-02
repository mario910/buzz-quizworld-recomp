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
| Reversed menu/round FPS and CPU | Pending same-EXE series; worker/process cycles and CPU time per completed frame |

The first two candidates skipped additional blend-stream DMA. Those jobs
now reject from their entry metadata before mutation; their complete lifted
flow is retained. All failed candidates and logs remain private evidence.

**Decision:** keep experimental/default off. Performance and switch
consolidation remain open; this does not complete phase 3b.

Private evidence: `analysis/native-geometry/phase3/host-pipeline-v1/`.
