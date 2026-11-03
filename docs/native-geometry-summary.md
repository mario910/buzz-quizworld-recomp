# Native geometry: current status and measurements

2026-11-02. Qualified EU100/Vulkan/AVX2 profile: geometry image 41,
animation image 40, native completion, Stage F, scheduling and approved
Vulkan optimizations default on inside the EXE, before constructors cache
options. No launcher injection is required. Both launchers preserve explicit
OFF. See [default validation](native-runtime-defaults-results.md).
Experimental switches are not enabled as a
blanket policy; the host pipeline replaces several earlier stage routines.

2026-11-03 handoff: Claude's later same-session measurements were run outside
the Codex execution environment. The earlier absolute FPS below must not be
used as the current performance baseline. An execution-environment effect
(reported as possibly sandbox-related) is suspected; its cause has not been
isolated. Earlier within-series A/B comparisons remain valid for their own
builds and conditions; do not compare absolute FPS across these environments.

| Scene | Earlier Codex series | Claude baseline, same approved profile (`fb00eb54`) | Claude wider-coverage candidate |
| --- | ---: | ---: | ---: |
| Menu | 29.57 | 40.59 | 40.70 |
| Solo round | 34.29 | 49.95 | 49.26 |

The user reports approximately 40.6 FPS menu / 50 FPS round on the baseline
code. The candidate's separate CPU series has contextual round FPS
49.45 -> 49.54; its FPS-series round decline and scene variation remain
recorded in the [coverage results](native-geometry-phase3b-coverage-results.md).
That report's decision is to retain the coverage change for repeatable menu
CPU savings with neutral rounds. These are reported measurements, not a new
Codex benchmark.

Current image-41 corpus coverage: **18,487 native / 1,021 whole-job fallbacks**
out of 19,508 jobs, following `46ee0ff`. Native/checked/off replays are exact;
`499b6d4` also fixes image-40-only replay's unrelated image-41 coverage gate.

| Remaining fallback reason | Jobs |
| --- | ---: |
| Out-of-palette index in the padding vertex | 584 |
| Additional blend streams | 328 |
| Input storage 4 | 109 |

Next work starts with a fresh profile of the current build, before expanding
coverage. Legacy JOB1 removal remains deferred. `output_option()` currently
returns zero for any name outside its production routing list (unless legacy
mode is selected); a new option must receive explicit routing and a test, or
it can silently do nothing. This handoff records that limitation without
changing runtime behavior.

Historical phase gains, each measured against its own matched control:

| Increment | Menu FPS | Round FPS | Menu worker cycles/frame | Round worker cycles/frame |
| --- | ---: | ---: | ---: | ---: |
| Image-41 host job, phase 3b | +3.510% | +2.849% | −8.957% | −5.303% |
| Image-40 host job, phase 4 | +2.052% | +1.274% | −5.386% | −6.420% |
| Compounded estimate for 3b + 4 | ≈+5.6% | ≈+4.2% | ≈−13.9% | ≈−11.4% |

The last row multiplies ratios from separate sessions/builds. It is an
estimate for these two host jobs; it is not a measured whole-port speedup.
Earlier Stage F, scheduling, native FP, renderer and phase-2 gains are not
added: some overlap with the work replaced by the later pipelines. An exact
combined result needs a fresh matched comparison of a defined complete
baseline against the current enabled package.

Sources: [phase-3b qualified profile](native-geometry-phase3b-default-results.md),
[phase-4 runtime measurements](native-geometry-phase4-runtime-results.md).
