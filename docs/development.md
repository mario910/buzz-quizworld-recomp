# Developer entry points

Keep the root commands simple for players: **Setup-Buzz → Build-Buzz → Launch-Buzz**.
Low-level scripts retain their names so existing research workflows keep working.

| Purpose | Entry point |
|---|---|
| Install local build/tool dependencies; create paths template without overwriting it | `Setup-Buzz.ps1` (`-SkipInstall` only checks an existing environment) |
| Read-only game/ELF/firmware validation | `toolenv/Scripts/python.exe verify-game.py` |
| Supported generation + native build | `Build-Buzz.ps1 [-Jobs 6] [-SkipGeneration]` |
| GUI and input setup | `Launch-Buzz.cmd` / `Launch-Buzz.ps1` |
| Direct native start | `run-windows.ps1 -Seconds 0 -MenuBootstrap` |
| Generation only | `generate.ps1 [-Jobs 6]` (ELF hash checked before writing) |
| Experimental compiler/kernel variants | `build-windows.ps1` |
| Performance experiments | `perf/` plus `inspect-*.py`, `claude-*.py`, `tools/` |

The supported Build-Buzz configuration expands to:

```powershell
.\build-windows.ps1 -NativeCompletion -NativeImage40 -NativeImage39 -NativeImage36 -ResultGuardFP -StageF -NativeImage7 -MStreamModules
```

Do not treat a bare `build-windows.ps1` as the supported performance/audio
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
.\test-buzzer.ps1
.\test-frontend.ps1
.\test-pc-launcher.ps1
```

These are synthetic config/protocol/adapter checks. Additional tests may need
generated headers or local captured jobs: inspect their inputs before running.
Do not upload those derived fixtures. `test-launcher.ps1` exercises process
creation through a fixture and expects a local paths config; it is not the
same as a full game integration run. `Launch-Buzz.ps1 -Smoke` opens a short
GUI smoke test and should not be run during someone else's desktop work.

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
