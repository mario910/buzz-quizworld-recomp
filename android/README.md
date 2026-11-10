# Android ARM64: build, install and validate

The Android port currently supports **EU original-disc 1.00**, `arm64-v8a`,
Android 10 / API 29 or newer and Vulkan. The generated guest ELF must have
SHA-256 `6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8`.
US and EU 1.02 desktop support does not imply Android support: Gradle rejects
other generated profiles. The activity is landscape and uses SDL2.
The Android controls, accessibility labels, driver status and import errors use
English; this does not change the language of the supplied game content.

**The APK does not yet import ISO/PUP and prepare the game on-device.** Prepare
on Windows/Linux first, build the ARM64 APK on Linux/WSL, then stage the user's
runtime over ADB. Native preparation primitives exist in `preparation/`, but
are not an Android import UI. The APK contains locally generated translations;
retail assets, ELF, firmware, keys and saves are not committed or bundled for
publication. See [preparation](../docs/game-import.md) and
[source publication](../docs/publication.md).

## Build configurations and current result

A bare Gradle build leaves optional NEON/renderer experiments disabled. It is
useful as a conservative bring-up configuration, **not** the phone reference.
Use [profiles/reference29.properties](profiles/reference29.properties) for the
explicit configuration used in recent Turnip tests. Every optional property is
listed, including false values, so reused build caches do not silently select
a different experiment. See the [optimization catalog](../docs/optimization-status.md)
for source defaults, platform differences, dependencies and rollback switches.

On the tested Galaxy S25 Ultra / Adreno 830, the November 6 matched menu tests
measured 19.52 FPS for reference29 and 19.42 FPS for reference29 plus deferred
clip checks (build30), with overlapping run-to-run variation. Stable 30 FPS
was not reached. `buzzGeometryDeferredClip` stays false. Reference29 is a tested
comparison configuration, not proof that every enabled option independently
improves FPS. No gameplay/scoring or audio-listening validation is implied by
these menu tests.

## 1. Host tools and sources

Use Linux x86-64 or WSL2 with Git, patch, Python 3.10+, a C/C++ toolchain and
JDK 17. The tested Android tool versions are:

| Tool | Version / configuration |
|---|---|
| Gradle | 8.11.1 (invoke installed `gradle`; no wrapper is checked in) |
| Android Gradle Plugin | 8.9.2, pinned in `app/build.gradle` |
| SDK platform / target | android-35 / 35 |
| SDK Build Tools | 35.0.0 |
| NDK | 30.0.16248370 |
| CMake / Ninja | 4.4.4 / 1.13.2, pinned by `requirements.txt` |
| SDL source | release-2.32.10, used by both Java and native code |
| libadrenotools | Commit pinned in `cmake/BuzzAndroidGpu.cmake`; CMake fetches its submodules |

From a source checkout:

```sh
git submodule update --init --recursive
python3 tools/setup-linux.py
# Use your SDK command-line tools installation:
sdkmanager 'platform-tools' 'platforms;android-35' 'build-tools;35.0.0' 'ndk;30.0.16248370'
```

Accept the SDK licenses through its normal setup flow. Set `JAVA_HOME` to JDK
17 and put that installation's `java` plus Gradle 8.11.1 on PATH. Keep the full
SDL source tree, including `android-project/app/src/main/java`; a prebuilt SDL
library is insufficient. The tested upstream SDL tag tarball SHA-256 is
`03f9d7c191a837525c9cda6406af2f2e48be02b5e7eb03d949cc9f1e9ca41c8b`.

Create ignored `android/local.properties` in the checkout from which Gradle
will run (use Linux paths under WSL):

```properties
sdk.dir=/absolute/path/to/android-sdk
cmake.dir=/absolute/path/to/checkout/toolenv
```

`cmake.dir` is the directory containing `bin/cmake` and `bin/ninja`, not the
binary itself. Generated data and source can be large; use local filesystem
space for preparation, NDK intermediate objects and the APK. WSL builds work
best with the prepared workspace/build outputs on its Linux filesystem.

## 2. Prepare the EU 1.00 workspace

Use BuzzPrepare or `tools/prepare-game.py --dist` following
[the import guide](../docs/game-import.md). Supply your own disc/ISO, firmware
and local key file where required; do not select the EU 1.02 update for this
Android target. Successful preparation produces a distribution and an isolated
session `workspace`. They serve different purposes:

- `WORKSPACE`: generated `port/recompiled`, `port/spu`,
  `analysis/game-generation.json`, native completion, reviewed host adapters
  and lifted MultiStream modules; input to the APK compiler.
- `DIST`: prepared `runtime` with disc/firmware/ELF; input to the staging helper.
- `CHECKOUT`: maintained port/CMake/Android sources used by Gradle.

Use a workspace prepared from the same source revision as the checkout.
Do not copy EU 1.02 stubs over generated EU 1.00 host adapters. For deliberately
reused workspaces, `perf/sync-native-sources.py` synchronizes native headers and
CMake with fresh timestamps, but **does not** regenerate guest sources, copy
all generators or change the SDK. Re-prepare when the generators/profile change.

```sh
CHECKOUT=/absolute/path/to/checkout
WORKSPACE=/absolute/path/to/preparation/session/workspace
DIST=/absolute/path/to/prepared/dist
PYTHON="$CHECKOUT/toolenv/bin/python"
SDL_SOURCE=/absolute/path/to/SDL-release-2.32.10
```

## 3. Generate the selected ARM adapters

The following is the complete generator set for reference29, not a list of
optional cumulative upgrades. Run the generators located **inside the prepared
workspace** so their root-relative outputs belong to that workspace:

```sh
cd "$WORKSPACE"
"$PYTHON" tools/codegen/generate-image36-native.py
"$PYTHON" tools/codegen/generate-image7-native.py
"$PYTHON" tools/codegen/generate-image39-native.py
"$PYTHON" tools/codegen/generate-image40-native.py
"$PYTHON" tools/codegen/generate-geometry-phase3.py --arm64-host
"$PYTHON" tools/codegen/generate-video-idct.py --workspace "$WORKSPACE"
cd "$CHECKOUT"
```

The full geometry adapter is
`analysis/native-geometry/phase3-host-arm64/spu_recomp.c`. Both
`buzzNeonGeometryDriver=true` and `buzzNeonHostGeometry=true` are required.
Its own adapter metadata must agree with CMake's `PORT_GEOMETRY_HOST_NEON`;
CMake rejects a mismatch. `--arm64` produces the earlier LS driver;
`generate-geometry-phase2.py --arm64` produces only selected leaf kernels.
Neither substitutes for `--arm64-host` in the reference profile.

The full host pipeline includes input decode, skinning, projection, culling,
vertex/index output and command publication. Eight-lane AVX2 operations become
pairs of NEON registers, with bounded gathers and native table lookups.
Unsupported jobs use the original whole-job fallback before external writes.
SPU extended floating-point values retain exact repair; this is not unchecked
fast-math. The selected Image7/36 leaves, Image39/40 checked arithmetic and
video IDCT adapter are separate from the full geometry driver.

## 4. Build reference29 or one controlled experiment

Bash command, run from `CHECKOUT`:

```sh
build_flags=()
while IFS='=' read -r key value; do
  [[ -z "$key" || "$key" == \#* ]] && continue
  value=${value%$'\r'} # also accept a Windows checkout's CRLF property file
  build_flags+=("-P${key}=${value}")
done < android/profiles/reference29.properties

CMAKE_BUILD_PARALLEL_LEVEL=6 gradle --no-daemon --max-workers=6 -p android \
  -PbuzzWorkspace="$WORKSPACE" \
  -PbuzzPython="$PYTHON" \
  -PsdlSource="$SDL_SOURCE" \
  "${build_flags[@]}" :app:assembleDebug
```

Output: `android/app/build/outputs/apk/debug/app-debug.apk` in the checkout
passed to `gradle -p`. The debug build supports `run-as` for staging/profiling;
its name does not mean the entire runtime is unoptimized. Large PPU units use
`-O1`, while reviewed hot/runtime/SPU paths have their own flags.

For the conservative baseline, omit `"${build_flags[@]}"`; missing optional
properties select the bare defaults. To reproduce an experiment, copy the
property file outside tracked sources, change exactly one property, and load
that file instead. For example, build30 differs by
`buzzGeometryDeferredClip=true`. Do not enable every switch at once.

`buzzGeometryLazyAttributes=true` tests deferred rigid/unskinned normal and UV
preparation after culling. It is OFF in the reference profile and bare build.
The complete captured-output test passes on ARM64/QEMU; this is correctness
coverage, not a phone FPS result. Weighted skinning keeps its existing pass.

`buzzGeometryZeroVertexPadding=true` tests zero-filled, unindexed alignment
bytes after the last logical vertex in the full NEON host geometry pipeline.
This avoids evaluating a padding-only vertex with an out-of-palette index.
Logical palette/index checks remain active; paired strict audits retain the
original padding policy. The property defaults false and reference29 keeps it
OFF. See [the padding contract](../docs/geometry-vertex-padding.md); ARM64/QEMU
correctness does not establish Android hardware performance.

Shared native job mode 1 also supports experimental
[direct MFC argument setup](../docs/geometry-channel-setup.md). This defaults
OFF and has no Gradle switch. Startup `BUZZ_GEOMETRY_CHANNEL_SETUP=1` enables
shared C++ address/size/tag assignments; DMA, atomics and waits still use the
SDK. Modes 0/2 and channel auditing keep general handling. ARM64/QEMU checks
pass; no phone speedup is established.

Experimental native reciprocal projection may also change thin-triangle
culling. Paired job mode 2 uses reference projection for strict published-byte
audits, including on ARM64; that audit does not validate the approximate
mode-1 policy. Reference29 keeps native reciprocal OFF. See
[projection audit qualification](../docs/geometry-projection-audits.md).

`buzzSpuJobReturn=true` enables experimental [native SPURS job completion](../docs/spu-job-return.md).
It completes the SDK's existing job-manager return on the driver stack,
preserving the non-local halt for other execution paths. Real-runtime ARM64/
QEMU checks pass; Android hardware performance is unqualified. The property
defaults false and reference29 keeps it OFF.

Verify the resulting APK with the same SDK:

```sh
"$ANDROID_SDK_ROOT/build-tools/35.0.0/apksigner" verify --verbose android/app/build/outputs/apk/debug/app-debug.apk
"$ANDROID_SDK_ROOT/build-tools/35.0.0/zipalign" -c -P 16 4 android/app/build/outputs/apk/debug/app-debug.apk
sha256sum android/app/build/outputs/apk/debug/app-debug.apk
```

Set `ANDROID_SDK_ROOT` to the SDK used in `local.properties`. Alignment checks
cover packaging; inspect each native ELF's LOAD alignment for 16 KiB as well.
Retain the actual `.cxx/Debug/<id>/arm64-v8a/CMakeCache.txt`, matching unstripped
`libmain.so`, ELF build ID, source revision and APK hash with local test results.
The Java driver archive code and native hook libraries are part of this APK;
the selected Turnip ZIP is not.

## 5. Install and stage data

Unlock the phone and authorize ADB. For a fresh install/runtime:

```sh
adb devices -l
"$PYTHON" android/stage-runtime.py --check "$DIST"
"$PYTHON" android/stage-runtime.py "$DIST"   --apk android/app/build/outputs/apk/debug/app-debug.apk
```

Use `--serial` when several devices are connected. The helper verifies the
EU title/ELF, installs the debug APK, checks free space and streams data into
`/data/data/org.buzzrecomp.game/files/runtime`. It **refuses to replace an
existing runtime**; it is not an update/merge command. Windows staging uses a
temporary archive and `adb push`, requiring room for an extra runtime copy
on host and phone. Binary data must not be piped through Windows PowerShell's
text redirection; use the helper or Python byte streams.

For a new APK on an already staged phone, preserve data with:

```sh
adb shell am force-stop org.buzzrecomp.game
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n org.buzzrecomp.game/.BuzzActivity
adb shell run-as org.buzzrecomp.game tail -n 80 files/runtime/game-error.log
```

Use the same signing identity for updates; an incompatible signature cannot
update in place. Do not uninstall or clear app data merely to compare builds.
Startup failures before native logs are opened use the `Buzz` logcat tag.

## 6. Touch controls, Turnip and lifecycle

The right-hand local buzzer has the physical order **red BUZZ, blue, orange,
green, yellow**. `P1` selects a player from the configured 4 or 8. Multitouch
is supported; focus loss, pause and menu opening release held inputs. `Menu`
or Android Back opens game settings. `GPU` manages drivers and buzzer visibility.
The app keeps the display awake while foregrounded and uses landscape orientation.

Use `GPU → Options → Import driver from ZIP…` for an ADPKG with `meta.json`,
its ARM64 `libraryName` and dependencies at the archive root. The importer
validates metadata/API, ELF architecture, paths, duplicates and expanded size
(up to 256 MiB), then selects the validated private installation. Restart the
process to activate a new driver or return to the system driver. The app does
not download or bundle drivers. If a selected driver fails before its first
presented frame, the following launch falls back to the system driver; failures
after the first frame require manual selection of the system driver.

Custom loading uses pinned libadrenotools without root; SDL and RSX share the
same forwarding loader. Hook libraries are packaged separately from the app's
normal link namespace. Vulkan 1.1 instance negotiation falls back to 1.0 where
needed. Surface recreation follows SDL activity callbacks when returning from
the picker or Home; this is separate from a game-process restart.

The reference device used Turnip Gen8 V38, ZIP SHA-256
`642cb514534dc6b712ce0fb16aa76db566359b6c8542f0577f8ba6dd3aff8674`.
This identifies one tested driver, not compatibility with every Adreno/kernel
or other GPU. Keep driver choice constant when comparing code changes. Home /
resume and rendering worked on the tested phone; qualify other devices and
system dialogs separately. A successful import alone is not a driver/FPS test.

## 7. Correctness and performance validation

The public tests contain synthetic states. Captured game corpora and generated
oracles remain local and are not required to publish the source tree. Build
adapters and generate the corresponding oracles before private corpus tests.
AArch64 GCC/G++, `qemu-aarch64` and its glibc sysroot are required for the ARM
host tests; QEMU validates correctness, never Snapdragon FPS.

```sh
sh tests/scripts/test-image36-arm64.sh "$WORKSPACE"
sh tests/scripts/test-video-idct-arm64.sh "$WORKSPACE"
# In the prepared workspace, after its required oracle generation:
sh tests/scripts/test-geometry-driver-arm64.sh
# Complete private geometry corpus; use an ARM64 Linux libz.a:
sh tests/scripts/test-geometry-host-arm64.sh /path/to/image41-corpus /path/to/arm64/libz.a
# Inventory consistency, no SDK/device required:
python3 tools/document-optimizations.py --check
```

The broader CPU suite uses the CMake-generated renderer adapter rather than a
hand-recreated version; see [performance checks](../docs/performance.md).
The optional `-PbuzzNativeImage39Bounds=true` object-visibility experiment is
OFF by default and remains OFF in reference29. It requires `buzzNeonJobFp`,
updated native sources and both `generate-image39-native.py` and
`generate-image39-kernels.py` regenerated in the prepared workspace. It selects
the wrapped EU100 image39 source and checked NEON bounds driver. Runtime
`BUZZ_STAGEF=1` with mask bit 8 is also required; add it to the intended kernel
mask rather than replacing other selections. `BUZZ_IMAGE39_BOUNDS=0` rolls
back to the original function. Whole-image `SPURS_NATIVE_IMAGE39_FP` can stay
0. ARM64/QEMU checks arithmetic and output only; Android device performance
and native gameplay progression are not yet qualified. See the
[bounds contract](../docs/native-jobs.md#image-39-object-visibility-and-descriptor-requests)
and [offline checks](../docs/performance.md).

The optional `-PbuzzNativeImage39Request=true` experiment requires
`-PbuzzNeonJobFp=true` and freshly generated EU 1.00 image39 sources. It uses
the portable native descriptor-request plan; unsupported requests retain the
original implementation. Bare builds and reference29 leave it false. ARM64
oracle validation does not establish phone FPS; qualify it against the same
Turnip version, scene and thermal conditions before promoting a device default.
The geometry oracle compares 19,508 captured jobs with the original producers;
the deferred-clip test also compares ordinary, special and random-bit inputs.
Driver archive checks can run on JDK 17:

```sh
javac -d /tmp/buzz-java android/app/src/main/java/org/buzzrecomp/game/DriverArchive.java tests/DriverArchiveTest.java
java -cp /tmp/buzz-java org.buzzrecomp.game.DriverArchiveTest
```

For frame-time capture, create the marker and restart:

```sh
adb shell run-as org.buzzrecomp.game touch files/runtime/profile-frames
adb shell am force-stop org.buzzrecomp.game
adb shell am start -n org.buzzrecomp.game/.BuzzActivity
# After the chosen measurement interval (Bash binary-safe redirect):
adb exec-out run-as org.buzzrecomp.game cat files/runtime/frame-times.csv > frame-times.csv
# Disable diagnostics for normal play, then restart:
adb shell run-as org.buzzrecomp.game rm -f files/runtime/profile-frames
```

The marker enables frame CSV and Vulkan CPU/wait counters. A profiled launch
replaces the CSV; pull it before restarting. Removing the marker without a
restart does not disable already-cached settings. Run CPU `simpleperf` separately
from FPS qualification and symbolize with the matching unstripped ELF/build ID.

For comparisons: bind APK hashes and driver version, use the same saved-data
seed, scene, resolution and timing window, match starting temperature, record
thermal state and PID continuity, and repeat in counterbalanced order (ABBA).
Do not dismiss a failed/minimized/locked run silently. Compare whole-window
presentations/time plus P95/P99, not a screenshot's one-second counter. Menus
have camera-dependent costs. Movies may repeat decoded frames while presentation
FPS exceeds 60: check unique movie-frame progress and audio synchronization.
Full-round/scoring/input/audio validation is a separate step from menu FPS.

### Bounded debug-device diagnostics

Additional opt-in diagnostics (never shipping defaults):
`--ei buzz_test_job_workers 1..4` changes the existing SPURS list-worker count;
`--ez buzz_test_timeline true` captures the existing dependency timeline for
seconds 32–42, with a 500,000-span cap. The latter requires a bounded process
deadline of at least 50 seconds. The trace is app-private
`files/diagnostic-timeline.txt`; inspect it with the SDK's `tools/ftrace_report.py`.
Neither diagnostic is a clean FPS benchmark. CPU affinity is applied to the
foreground SDL/game thread after surface/focus readiness, not to Android's UI
or the Java watchdog; unavailable masks abort the diagnostic rather than
silently using a subset.

The whole image40 animation pipeline can now be compiled on Android ARM64,
using the shared NEON vectors, checked exceptional-lane repair and restored
FPCR/FPSR. It remains opt-in, outside reference29. In a prepared EU 1.00
workspace, first regenerate `generate-image40-native.py`, then run:

```sh
"$PYTHON" tools/codegen/generate-animation-job-runtime.py \
  --fallback analysis/native-image40/spu_recomp.c \
  --output analysis/native-image40 --in-place
```

Only the exact pinned private fallback may be wrapped in place. Configure the
existing NDK CMake build with `PORT_ANIMATION_JOB_DEFAULT=2` for native/original
whole-job I/O checks, or `1` for native execution without duplicate checking.
An explicit `BUZZ_ANIMATION_JOB` environment value still wins. Bare Android
builds do not acquire the qualified desktop native-job defaults. Regenerating
the ordinary image40 source restores the old adapter.

`generate-audio-compressor.py --include-atrac` additionally emits two private,
hash-bound ATRAC filter blocks. Select their `spu_recomp.c` with the optional
`PORT_AUDIO_ATRAC_SOURCE`; it requires the compressor wrapper configuration.
The original remains the fallback. This is not an approximate or replacement
audio decoder. The host sanitizer suite also checks these blocks; native ARM
checks include all GPR/LS state, tail continuations and FPCR/FPSR restoration.
Successful correctness checks have **not** established a whole-game FPS gain.

Fresh Android installations now start with a 30 FPS presentation limit. Saved
settings still take precedence, including an explicit uncapped selection.
Desktop defaults are unchanged. This limits presentation, not guest simulation
speed, and does not establish a sustained gameplay performance result.

Android also creates a private `files/runtime/spirv-cache` folder and enables
the SDK's existing `PS3RECOMP_SPIRV_CACHE`. This is separate from the D3D shader
cache. Shader text/stage, translator build and glslang version qualify disk
entries; invalid entries are recompiled. Writes are immediate and atomic, so
a diagnostic process kill does not discard all successful translations.
An explicit environment override (even an empty value to disable it) wins.
The cache is disposable, contains game-derived data, and must not be bundled
with distributions. A warm cache reduces repeated shader translation, not
driver pipeline creation or all first-visit shader stutter.

Debug APKs accept an optional process deadline, independent of SDL, the guest,
and the render loop:

```sh
adb shell am start -n org.buzzrecomp.game/.BuzzActivity --ei buzz_test_seconds 35
```

The process terminates at the deadline (5–60 seconds); ordinary launches and
non-debuggable APKs do not arm it. This is not a thermal monitor. Also run a
device-local thermal supervisor for stress tests, recording CPU/GPU sensors
rather than treating battery temperature as SoC temperature. Do not raise a
stop threshold just to finish a failed benchmark.

For a matched menu scene, `--ez buzz_test_skip_intro true` uses the existing
desktop startup-only video skip. It requires a bounded debug run; real media
calls return at the first menu. Do not use it to qualify movie playback.
`--ei buzz_test_idle_ms N` (0..16) selects the existing FIFO idle retry timeout
for that bounded debug process only. It does not change saved preferences or
normal defaults. Zero can busy-poll; do not use it as a battery-friendly preset.
Always wake/unlock the device before a visible-game test: starting an Activity
over ADB does not wake a sleeping handheld. On dual-screen devices explicitly
select the game's physical display when capturing, rather than relying on
`screencap`'s default display.

For a bounded debug run only, `--ei buzz_test_cpu_mask 7` constrains new native
workers to CPUs 0–2, **if those CPUs are available**. CPU numbering and capacity
are device-specific: this is a diagnostic comparison, not a shipping affinity
policy or a claim about unrestricted performance.

`--ez buzz_test_pc_samples true` records 100 CPU-Hz instruction-pointer samples
in internal `files/diagnostic-pc-samples.bin`, plus a `.maps` snapshot. It
requires the deadline and an unused SIGPROF/ITIMER_PROF, and adds overhead.
Symbolize against the exact matching unstripped `libmain.so`; preserve that ELF
before rebuilding. The binary has an 8-byte `BZWPC001` header, a little-endian
64-bit image base, then 16-byte `{uint64_t pc; uint32_t tid, reserved;}` records.
It also enables the POSIX `AUDIO_RATE` diagnostic (blocks/s and queue depth).
No device security settings or driver replacement are required.

`--ei buzz_test_audio_mode 0|1|2` selects original/local-native/compare mode
in an APK built with the experimental audio DSP adapters. It requires a
bounded debug run. Compare mode keeps original results and checks at most
16 complete contexts per entry; it is not a performance mode. PC sampling
also reports SPIR-V translation/memory-hit/disk-hit counters every five seconds.
Avoid live ADB screenshots during a performance/thermal measurement; capture
separately, preferably with the guest paused.

Android pause state combines surface readiness, resumed/focused Activity and
the native settings menu. Returning from the background cannot unpause an open
menu, and closing that menu cannot resume a hidden Activity. Surface lifetime
exclusion remains in place. This is not a generic suspend of every guest thread.

`BUZZ_ANDROID_LIMIT_EXPORTS` defaults ON for Android. The link version script
exports only `SDL_main` and this Activity's JNI entry points; SDL and GPU-hook
libraries keep their own exports. Audit the matching unstripped `libmain.so`
with `tests/check-android-exports.py --readelf PATH_TO_NDK_LLVM_READELF ELF`.

`BUZZ_ANDROID_SHADER_CONTEXT_CHECKS` defaults OFF. This selects SPIRV-Tools'
own `SPIRV_CHECK_CONTEXT` build option: its costly developer-only IR def-use
reconstruction/comparison was sampled on the render thread during first-visit
shader compilation in Debug APKs. Shader legalization/optimization and the
Vulkan binding contract remain enabled and unchanged. Set the option ON when
investigating the shader compiler itself. Desktop builds are unaffected.

The POSIX audio thread now waits for SDL queue space *before* mixing/notifying
the guest. If no audio device exists, it uses a monotonic 256/48000-second
clock instead of 2 ms polling, dropping missed deadlines rather than bursting.
Windows/WASAPI behavior is unchanged. The pure fallback-clock test is:

```sh
cc -std=c11 -Wall -Wextra -Werror ps3recomp/libs/audio/tests/test_audio_block_clock.c -o /tmp/audio-clock-test
/tmp/audio-clock-test
```

### Experimental scoped audio DSP adapters

These are **OFF by default**, separate from reference29. See
[the Thor investigation](../docs/thor-diagnostics.md) for qualification limits.
The generator accepts only the audited canonical 1.00 source hashes and leaves
all other functions unchanged. Nine selected leaves retain instruction/store
order, exact exceptional-lane repair, original reciprocal/interpolation helpers,
and original fallback for external LS or enabled LS debug hooks. Host FPCR/FPSR
is restored on every exit; no whole-image FP mode or audio disable is used.

Generate from a prepared EU 1.00 workspace, then select these CMake cache values
in that workspace's existing Android native build directory:

```sh
python3 tools/codegen/generate-audio-compressor.py --workspace "$WORKSPACE" --include-mixer
# Extra CMake arguments (keep the existing Android toolchain/profile arguments):
# -DPORT_AUDIO_COMPRESSOR_SOURCE="$WORKSPACE/analysis/native-audio-compressor/spu_recomp.c"
# -DPORT_AUDIO_MIXER_SOURCE="$WORKSPACE/analysis/native-audio-mixer/spu_recomp.c"
# -DPORT_AUDIO_COMPRESSOR_NEON=ON
# Complete-state host validation (ASan/UBSan; not ARM64 NEON qualification):
sh tests/scripts/test-audio-dsp-host.sh "$WORKSPACE"
```

`BUZZ_AUDIO_COMPRESSOR=0` rolls back to original functions without regenerating
inputs. Clear both source cache values and set `PORT_AUDIO_COMPRESSOR_NEON=OFF`
to remove the experiment from a rebuild. APKs built without these adapters
retain the original audio path regardless of diagnostic mode selection.
