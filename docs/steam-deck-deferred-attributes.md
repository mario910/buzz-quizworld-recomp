# Steam Deck deferred-attribute experiments, 2026-11-15

Stable 30 FPS is still not achieved. Neither candidate below was installed or
selected by the build frontend. The production geometry sources were restored.

## Experiment

The existing lazy-attribute path only supports rigid geometry and is disabled.
Two experimental extensions were built against `deck-30fps` commit `325d5c3`,
using the existing PGO6 profile:

1. Skin positions first, cull, then decode/skin attributes only for surviving
   meshes. Save blended bone matrices within the job so visible meshes do not
   repeat palette blending. Preserve already-validated packed directions until
   their deferred decode instead of gathering them twice.
2. Use worker-local previous-visibility hints to select the deferred path only
   for meshes previously culled. The current job still computes all visibility
   decisions; a hint never reuses geometry or removes a visibility test.

All 4,000 reveal jobs replayed with exact output transfers for both candidates.
Standalone eager/deferred comparisons also passed, including malformed-selector
rejection, rejection of publishing incomplete attributes, and (for candidate 2)
forced incorrect visibility hints. AddressSanitizer passed the 4,000-job fixture
for candidate 1. These checks establish captured-case correctness, not universal
correctness or a performance gain.

## Live results

Sequential uncapped runs on the Deck used 1280x720, render scale 100, the same
input script, and the installed executable as control. No builds or replay
benchmarks ran concurrently. Each full run lasted 335 seconds. Windows are
script-relative: heavy menu 40–70 s, questions 240–300 s, reveal 306–315 s.
The final screenshots were captured at 330 s, outside measurement windows.

| Build | Heavy menu FPS | Questions FPS | Reveal FPS | Reveal median ms | Reveal p95 ms |
|---|---:|---:|---:|---:|---:|
| Installed control | 26.40 | 35.00 | 26.22 | 38.08 | 44.28 |
| Deferred attributes | 25.70 | 35.37 | 25.89 | 38.45 | 45.07 |
| Visibility-hinted deferral | 26.03 | 35.85 | 25.56 | 38.88 | 45.15 |

These are exploratory single runs, not a repeated ABBA qualification. Fixed-time
inputs can shift scene progression; the screenshots do not certify every frame
of a window. There is no evidence here to promote either candidate. In
particular, the higher question average did not solve the slow scenes.

A separate 90-second control-executable probe with `BUZZ_GCM_IDLE_MS=0` measured
26.30 FPS in the same heavy-menu window (median 37.16 ms, p95 46.14 ms), versus
26.40 FPS (37.37/44.75 ms) with the normal wait. Removing that timeout did not
provide a useful improvement either; the setting was not installed.

An initial control run was terminated by SIGTERM after about 53 seconds. It was
excluded and repeated to completion.

## Artifacts and reproduction

Local artifacts are under
`/home/deck/buzz/performance/optimization/codex-deck-30fps/`:

- `comparison.json` and `compare.py`: completed-run summaries and executable hashes.
- `live-control-retry`, `live-lazy2`, `live-adaptive`, `idle0-probe`: raw frame
  CSVs, settings, input script manifests, telemetry and screenshots.
- `adaptive-experiment.patch`: rejected candidate source, preserved for research.
- `lazy2-candidate`, `adaptive-candidate`: frozen experimental executables.
- `lazy2-reveal.log`, `adaptive-reveal.log`, `lazy-asan.log`: correctness evidence.

The production standalone lazy-attribute fixture had stopped linking after blend
support introduced guest-runtime references. It now provides aborting stubs for
that unreachable execution path, accepts an explicit expected corpus size, and
can report eager/deferred preparation times separately by skinning and visibility.
Its default expected size and admission threshold retain the original 19,508-job
fixture's coverage requirement. Kernel timings exclude corpus loading, alternate
execution order, and must not be interpreted as FPS.

From the repository, inside the existing build container:

```sh
clang++ -O2 -std=c++17 -mavx2 -mfma -frounding-math -ffp-contract=off \
  -DSPU_NATIVE_RTZ=1 -DSPU_RTZ_RESULT_GUARD=1 \
  -DBUZZ_GEOMETRY_BOUNDED_SKIN=1 -D_Thread_local=thread_local \
  -Ips3recomp/include -Ips3recomp/runtime/spu -Ips3recomp/libs/video -Iport \
  tests/geometry-lazy-attributes-test.cpp -lz -o /tmp/geometry-lazy-test
/tmp/geometry-lazy-test \
  /home/deck/buzz/performance/optimization/claude-experiments/corpus41-reveal-seq \
  --expected-jobs 4000 --bench
/tmp/geometry-lazy-test \
  /home/deck/buzz/performance/optimization/claude-experiments/corpus41-question \
  --expected-jobs 1500
```
