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
