# Developer entry points

Keep the root simple for players: **Prepare-Buzz.cmd → Launch-Buzz.cmd**.
Developer commands live below `scripts/`; old root-level script paths have moved.

Repository documentation, comments, logs, errors and interface text use English.
Other interface languages are supported through an explicit language selection
with a complete English translation, as in Question Studio. Keep accessibility
labels and documented menu names consistent with the interface. Imported game
content, user-authored questions, Unicode test fixtures and legacy-setting
migration tokens retain their original text.

```text
preparation/     Qt wizard and native import/generation primitives
launcher/native/ Legacy C++ SDL3/ImGui launcher for developer builds
launcher/        Legacy Python UI/models and developer preparation pipeline
scripts/         Windows setup, generation, build and direct-start commands
config/          Public example configurations (personal configs stay ignored)
tools/codegen/   Version-bound generators and native-adapter generation
tools/inspect/   Read-only inspectors and game-version comparison tools
perf/            Profiling, sampling and benchmark runners
tests/           Synthetic fixtures and Python tests
tests/scripts/   PowerShell test runners
port/            Hand-written port and native kernels
ps3recomp/       Pinned SDK submodule (normal fork master, validated v0.17.0 code)
```

| Purpose | Entry point |
|---|---|
| Install local build/tool dependencies; create paths template without overwriting it | `scripts/Setup-Buzz.ps1` (`-SkipInstall` only checks an existing environment) |
| Read-only game/ELF/firmware validation | `toolenv/Scripts/python.exe tools/verify-game.py` |
| Supported generation + native build | `scripts/Build-Buzz.ps1 [-Jobs 6] [-SkipGeneration]` |
| Build and package the preparation wizard | Windows `scripts/build-preparation.ps1 -QtPrefix <Qt MSVC prefix>`, Linux `bash scripts/build-preparation-linux.sh` |
| Build and publish a standalone game without the wizard | `toolenv/Scripts/python.exe tools/prepare-game.py --dist ...` |
| Legacy developer launcher | `scripts/Launch-Buzz.ps1` |
| Native launcher only (no game/SDK needed) | CMake from `launcher/native/`, or `scripts/build-launcher.ps1` |
| Native launcher tests | `ctest --test-dir build-launcher --output-on-failure` |
| Runtime host settings prototype | `config/pc-settings.example.ini`, `port/pc_runtime.*`, SDK `ps3emu/host_controls.h` |
| Direct native start | `scripts/run-windows.ps1 -Seconds 0 -MenuBootstrap` |
| Generation only | `scripts/generate.ps1 [-Jobs 6]` (ELF hash checked before writing) |
| Experimental compiler/kernel variants | `scripts/build-windows.ps1` |
| Windows renderer architecture and diagnostics | [Vulkan and D3D12](renderer.md) |
| Performance experiments | `perf/` and `tools/inspect/` |
| Android ARM64 APK and device staging | [Android build guide](../android/README.md), `android/profiles/reference29.properties` |
| Optimization inventory consistency | `python tools/document-optimizations.py --check`; [status catalog](optimization-status.md) |
| 30 FPS cap checks | `tests/scripts/test-frame-limit.ps1`; `perf/measure-frame-limit.ps1 -LiveSwitches` |
| Renderer frame-time A/B (menu, solo round) | `perf/measure-vulkan-scene.ps1`; `perf/scene-window-stats.py`; [measurement rules](performance.md) |

The supported Build-Buzz configuration expands to:

```powershell
.\scripts\build-windows.ps1 -NativeCompletion -NativeImage40 -NativeImage39 -NativeImage36 -ResultGuardFP -StageF -NativeImage7 -MStreamModules
```

Do not treat a bare `scripts/build-windows.ps1` as the supported performance/audio
configuration. Other geometry flags are experiments, not cumulative upgrades.
`BUZZ_IMAGE36=0` selects the lifted reference; `2` compares against it.
Keep `SPURS_NATIVE_IMAGE39_FP=0` for the established correctness baseline.

## Synthetic tests

From the repository root, after Setup:

```powershell
.\toolenv\Scripts\python.exe -m unittest discover -s tests -p test_publication.py
.\toolenv\Scripts\python.exe -m unittest discover -s tests -p test_pc_launcher.py
```

After generation (these C++ harnesses also need the locally generated ABI
headers, but use synthetic states rather than captured game assets):

```powershell
.\tests\scripts\test-buzzer.ps1
.\tests\scripts\test-frontend.ps1
.\tests\scripts\test-pc-launcher.ps1
```

These are synthetic config/protocol/adapter checks. Additional tests may need
generated headers or local captured jobs: inspect their inputs before running.
Do not upload those derived fixtures. `tests/scripts/test-launcher.ps1` exercises process
creation through a fixture and expects a local paths config; it is not the
same as a full game integration run. `scripts/Launch-Buzz.ps1 -Smoke` opens a short
withdrawn GUI smoke test; it neither shows the window nor starts the game.

## What a build needs

The full native profile builds from a checkout or worktree, the build tools,
and the user's own game files. Every generated adapter is
derived from the lift that `scripts/generate.ps1` writes from the ELF; nothing
private needs to be copied from another checkout.

- Captured SPU entry snapshots under `analysis/` (`pc_*.bin`) only pin and
  seed private test oracles. Without them a generator prints `not present:
  production output only, test oracle skipped`; the tests that include those
  `test-*-reference.inc` files then need the snapshots.
- Generated sources include `port/` headers by name, resolved by the game
  target's include path, never by a checkout's absolute path.
- Hash pins on tracked generator sources use `text_sha256` from
  `tools/codegen/geometry_fixture.py`, so they hold under any `core.autocrlf`.

## Integration and measurement

See [native jobs](native-jobs.md), [runtime defaults](runtime-defaults.md)
and [profiling and validation](performance.md) for the maintained runtime
contract and workflow. [The documentation index](README.md) lists format
references. Completed phase reports remain in Git history.

Use `-Background` and internal `BUZZ_SCRIPT` for unattended integration. No
global desktop input or window activation is needed. Prefer the single-player
challenge path; profiles are local user data, not shared fixtures. Never stop
or overwrite a running instance that your test did not start.

Benchmark the same scene after warm-up with audio, video, shaders and effects
intact. Record CPU/GPU, compiler flags, environment, EXE hash, measurement
window, frame-time percentiles and repeated/reversed A/B order. Do not run
compilation, corpus replay or intrusive sampling alongside the timed run.
`tools/present_phase_report.py` reports completed host presents;
`tools/task_dependency_report.py` analyzes captured job/pool intervals.
The existing perf scripts are developer tools, not an unattended test suite
guaranteed to work with a fresh profile.

## Generated/private directories

`port/recompiled/`, `port/spu/` and `analysis/` hold game/firmware-derived lifts,
adapters, traces and captured inputs. `build-windows/` is native build output.
`runtime/` holds the private VFS, profiles and caches. Never add these to Git,
even if a diagnostic would be easier to share that way. Patch generators or
hand-written source instead of checking in generated translations.
