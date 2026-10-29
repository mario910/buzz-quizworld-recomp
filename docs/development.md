# Developer entry points

Keep the root simple for players: **Prepare-Buzz.cmd → Launch-Buzz.cmd**.
Developer commands live below `scripts/`; old root-level script paths have moved.

```text
launcher/        Desktop UI, input/configuration models and preparation pipeline
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
| GUI and input setup | `Launch-Buzz.cmd` / `scripts/Launch-Buzz.ps1` |
| Direct native start | `scripts/run-windows.ps1 -Seconds 0 -MenuBootstrap` |
| Generation only | `scripts/generate.ps1 [-Jobs 6]` (ELF hash checked before writing) |
| Experimental compiler/kernel variants | `scripts/build-windows.ps1` |
| Performance experiments | `perf/` and `tools/inspect/` |

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

## Integration and measurement

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
