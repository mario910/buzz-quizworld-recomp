# Native geometry: current measured result

2026-11-02. Qualified EU100/Vulkan/AVX2 profile: geometry image 41,
animation image 40, native completion, Stage F, scheduling and approved
Vulkan optimizations default on inside the EXE, before constructors cache
options. No launcher injection is required. Both launchers preserve explicit
OFF. See [default validation](native-runtime-defaults-results.md).
Experimental switches are not enabled as a
blanket policy; the host pipeline replaces several earlier stage routines.

Latest measured full-profile FPS, uncapped, 1280×720:

| Scene | FPS |
| --- | ---: |
| Menu | 29.57 |
| Solo round | 34.29 |

Each phase's gain was measured against its own matched control:

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
