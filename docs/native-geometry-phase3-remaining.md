# Phase3 completion audit and next implementation

Current baseline:2bf1ef7,ABI48/61 stages,capture-enabled3df8d057 EXE.
The subsequent standalone encoder-core refactor has CPU-test evidence only;
the frozen game's corpus/live/render/performance evidence is unchanged.
This is a current-state audit against the phase3 objective;the chronological
proofs remain in [the results](native-geometry-phase3-results.md).

| Requirement | Current evidence | Remaining work |
| --- | --- | --- |
| Recognized outer control and exact job I/O | Six frozen arms each19,508exact jobs/58,195transfers,zero divergent inputs/failures;enabled19,508native roots/zero whole-job fallback | Preserve these proofs through each data-path change |
| Host data between stages | Native library kernels exist;formatter has a local128-vector bank,but publishes/reloads it around encoder callbacks;vertex streams still use LS | Implement the host-held data path;SPU-ABI wrappers alone do not complete this requirement |
| Strict state and live checks | At least45,000strict whole-job/I/O and35,000formatter checks;new kernels compare full GPR/LS/control/continuation | New fused paths require their own independent strict checks;retain the narrow StageF exception until phase7 |
| Actual-native rendering | Normal native menu/round readbacks on3df;timing runs have lossless exact PPM/PNG with inspected round QA | Repeat relevant visual checks after implementation changes |
| Interpreted count | Menu observations are absolutely zero;round has positive non-atomic legacy image2 observations and known image40 entry | Round count gate remains open;do not equate matching entry keys with exact instruction counts |
| FPS and CPU | Same-EXE menu/round reversals show isolated SPANS savings;independent round repetition does not reproduce the first p99 increase | Measure each next implementation and the aggregate native configuration;the older aggregate regression is still a separate result |
| Cold paths | Standalone strict cases exist for additional formats/variants | Their individual real/live gates remain open in the tracker |
| Defaults | JOB0/geometry827/newflags0,including SPANS0 | Keep the enclosing driver experimental until its full gates pass;use validated SPANS1 explicitly in the optimized native profile |

The phase3 exit remains unproven while the host-data path,round interpreted
count,cold required paths and aggregate/default decisions remain open.
Shared phase4 and final strict StageF7 remain in the continuing objective.

## Next increment: shared host register bank in the formatter

The current [formatter](../port/native_geometry_formatter.h) copies its local
128-vector bank into `c.gpr` before a data callback and copies it back after.
Nonempty FLOAT/HALF/CMP encoders each copy that bank into another local bank
and publish it back. The same pattern exists in the other four output encoders.
For a nonempty field this can mean four2048-byte bank copies,apart from the
formatter's initial/final copies. These are source facts,not measured timings.

The next implementation should let the seven existing native output cores
operate on the formatter's current host bank directly. Preserve the existing
SPU-ABI entry points and unsupported-path fallback. Keep every arithmetic,
ordered LS store,prefetch,dummy iteration and final register bit unchanged.
Retain independent encoder switches and full checked boundaries;publish the
bank when a strict boundary needs a complete physical context. Never count a
skipped child comparison as a strict success.

Standalone register-bank cores for all seven encoders now pass 50,811 strict
original-oracle cases with a poisoned physical `c.gpr`; all 50,811 existing
ABI-wrapper comparisons and enclosing formatter/conversion/routing suites pass.
These cores preserve physical GPRs, PC and continuation until the caller
publishes their result. No production bank-copy removal is claimed yet.
Next integrate an independent default-off formatter path and validate
full formatter/job state and I/O,corpus/live coverage,rendering and measurements.
The parent admission must still prove eligibility before mutation;an unexpected
child rejection cannot turn partially changed state into a fallback.

This advances one boundary toward host data across the full
decompress/skin/transform/cull/emit path. Stream values across the other stages
remain further work. The new diagnostic profiles rank half/float/CMP output
work among the remaining native leaves,but do not predict a saving from this
change. The VCRUNTIME module samples cannot be attributed specifically to
these copies. Measure the implementation before choosing its default.
