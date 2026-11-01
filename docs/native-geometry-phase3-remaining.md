# Phase3 completion audit and next implementation

Current native source:a5fbf3d,ABI48/61 stages,bc0d35df EXE/dc3e3a05 MAP.
Shared-bank formatter corpus/live/render gates now pass on this new freeze;
its measurements and default decision remain open. Older SPANS measurements
remain bound to their separate3df8d057 freeze.
This is a current-state audit against the phase3 objective;the chronological
proofs remain in [the results](native-geometry-phase3-results.md).

| Requirement | Current evidence | Remaining work |
| --- | --- | --- |
| Recognized outer control and exact job I/O | Ten bc0 frozen arms each19,508exact jobs/58,195transfers,zero divergent inputs/failures;enabled19,508native roots/zero whole-job fallback;old ledgers unchanged | Preserve these proofs through each data-path change |
| Host data between stages | Opt-in REGISTERS formatter shares its bank with all7 encoders;7,201 real roots/17,278 children per replay;other vertex streams still use LS | Measure this boundary and implement the remaining host-held pipeline streams |
| Strict state and live checks | New freeze:45,000whole-job/I/O +35,000shared-bank formatter +85,833child comparisons;separateUNORM>=10,000;full GPR/LS/control/continuation | Cold I16/U8 and other cold paths remain open;retain the narrow StageF exception until phase7 |
| Actual-native rendering | Normal native menu and actual question/green round arena onbc0;exact1280x720 PPM/PNG,one-shot GPU readback0,inspected after close | Repeat relevant visual checks after implementation changes |
| Interpreted count | Menu observations are absolutely zero;round has positive non-atomic legacy image2 observations and known image40 entry | Round count gate remains open;do not equate matching entry keys with exact instruction counts |
| FPS and CPU | Same-EXE menu/round reversals show isolated SPANS savings;independent round repetition does not reproduce the first p99 increase | Measure each next implementation and the aggregate native configuration;the older aggregate regression is still a separate result |
| Cold paths | Standalone strict cases exist for additional formats/variants | Their individual real/live gates remain open in the tracker |
| Defaults | JOB0/geometry827/newflags0,including SPANS0/REGISTERS0 | Keep the enclosing driver experimental until its full gates pass;use validated SPANS1 explicitly in the optimized native profile |

The phase3 exit remains unproven while the host-data path,round interpreted
count,cold required paths and aggregate/default decisions remain open.
Shared phase4 and final strict StageF7 remain in the continuing objective.

## Shared host register bank: functional gates pass, measurements pending

The retained ABI route in the [formatter](../port/native_geometry_formatter.h)
publishes and reloads its bank around encoder callbacks, which themselves
copy into and out of an encoder-local bank. REGISTERS1 passes the formatter's
bank directly to all seven cores, removing those four execution-bank copies
per nonempty unchecked field. Root copies and FLOAT admission's local bank
remain. These are source facts, not measured timings.

The implementation lets all seven native output cores
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
publishes their result. External-bank readers also produce identical plans.
REGISTERS0/1 defaults0;1,776 production roots/888 shared-bank roots,
360 strict parents/822 host children/318 strict children pass,including
all7 crossing paths,48 saved-frame-alias roots,20 unchanged rejects and
four child-checker corruption probes. Existing stage ledgers remain exact.
Ten new frozen replay arms now preserve full state/I/O and old ledgers,
including explicit REGISTERS0/1 and independent child/parent/mixed checks.
Live whole-job/parent/child comparisons and normal native menu/round images
pass; a separate UNORM2 run closes its initial live256 gap. I16/U8 real/live0
is explicitly retained. Next run separate reversed menu/round FPS and CPU
series on this same bc0 freeze,using the new reporter's real-coverage checks.
The parent admission must still prove eligibility before mutation;an unexpected
child rejection cannot turn partially changed state into a fallback.

This advances one boundary toward host data across the full
decompress/skin/transform/cull/emit path. Stream values across the other stages
remain further work. The new diagnostic profiles rank half/float/CMP output
work among the remaining native leaves,but do not predict a saving from this
change. The VCRUNTIME module samples cannot be attributed specifically to
these copies. Measure the implementation before choosing its default.
