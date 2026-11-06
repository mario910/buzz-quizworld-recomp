# Runtime defaults and overrides

For the complete compile-time switch inventory, enabled/disabled Android
reference profile and experiments that remain off, see
[optimization status](optimization-status.md). This document describes runtime
environment defaults; it is not a list of every compiled optimization.

The qualified EU 1.00/Vulkan/AVX2 executable installs its approved runtime
profile before C/C++ constructors cache environment options. Missing variables
therefore select the intended defaults even when starting the EXE directly.
Explicit overrides, including `0`, take precedence.

The source of truth is [port/runtime_defaults.inc](../port/runtime_defaults.inc).
[port/runtime_defaults.cpp](../port/runtime_defaults.cpp) installs the profile;
`PORT_NATIVE_JOB_DEFAULTS` controls its inclusion in a qualified build. The
build script and CMake require the corresponding native adapters, AVX2/FMA,
native SPU FP/result guard and supported Vulkan/hot-code configuration.

Standalone builds without the qualified desktop native profile still install
the common section of that file. Android adds adapter-dependent defaults from
`runtime_defaults.cpp`, including its Image39 NEON FP and geometry outer-driver
routes. It does not inherit the entire desktop native-only section. A missing
desktop profile label is therefore not proof that ARM geometry is disabled.
Use the [Android profile and build guide](../android/README.md), generated
adapter identity and actual CMake cache together.

| Area | Approved profile |
| --- | --- |
| Geometry and animation | `BUZZ_GEOMETRY_JOB=1`, `BUZZ_ANIMATION_JOB=1` |
| Retained native methods | Stage F 1, image 7/36 1, geometry 1 with mask 827, native extra FP |
| Completion | Native completion mode 2 and completed-slot queue |
| Scheduling | Four workers, reviewed sequence/wake/merge options and cached environment reads |
| Vulkan | Batch 512, ring/pass/clear batching, asynchronous draw, mailbox, texture reuse and native texture rows |
| Presentation diagnostics | Continuous present readback off; explicit captures remain available |

Some optimizations already default inside the SDK: pool/JTS spin duration is
chosen from the logical CPU count, and reused job contexts clear their declared
regions by default. An absent environment variable does not imply a feature
is disabled. User preferences such as frame limit, fullscreen, volume and
input mappings are separate from the optimization profile.

For controlled scheduling experiments, `-DBUZZ_SPURS_IDLE_SPIN_US=50`
embeds a 50 microsecond poll before the SDK's existing worker/command waits.
Android uses `-PbuzzSpursIdleSpinUs=50`. The accepted range is 0..1000;
the default `-1` retains the SDK's CPU-count policy. Zero parks immediately.
This changes neither task ordering nor guest completion/fence semantics.
The two existing environment variables, `SPURS_POOL_SPIN_US` and
`SPURS_JTS_SPIN_US`, override their embedded values independently, including
explicit zero. Values are installed before C/C++ constructors and included
in `--runtime-defaults`. Longer polling consumes CPU even when no work is
available; qualify both frame times and CPU work on the target hardware.
`tests/scripts/test-spurs-wait-defaults.py` checks constructor order, normal
and standalone builds, explicit overrides and configuration bounds.

The manifest identifies `embedded_runtime_profile=eu100-vulkan-native-v1`
with an empty `runtime_defaults` map. Launchers preserve inherited overrides
and do not inject this profile. Older frozen executables retain their legacy
manifest/launcher defaults. Other executable families and experimental build
configurations do not automatically acquire the EU profile.

Use `BuzzQuizWorldRecomp.exe --runtime-defaults` to print the embedded profile
and its environment values without booting the game. Do not enable every
experimental switch at once. Unsupported job inputs retain the whole lift,
and legacy per-stage options do not control the new host pipeline.

Tests for constructor order, overrides and launcher boundaries live in
`tests/runtime-defaults-test.cpp`, `tests/launcher-native-test.cpp` and
`tests/scripts/test-*-defaults.ps1`. The runtime corpus checks live in
`tests/scripts/test-embedded-runtime.ps1` and
`tests/scripts/test-animation-runtime.ps1`.
