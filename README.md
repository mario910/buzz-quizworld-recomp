# Buzz! Quiz World — native PC port experiment (PS3Recomp)

Experimental static recompilation of **Buzz! Quiz World** (PS3, BCES00647,
update 1.02) to native Windows x64 with a Direct3D 12 renderer, built on a
modified [ps3recomp](https://github.com/sp00nznet/ps3recomp) SDK.

> **No game content is included.** This repository contains only original
> port code, build/generation scripts, tests and documentation. The PPU/SPU
> code is *generated on your machine* from your own decrypted copy of the
> game, and the game's data files are read from your own dump at runtime.
> You need a legally obtained copy of the game to build or run anything.

## What you need

- Your own dump of Buzz! Quiz World **BCES00647** (folder containing `PS3_GAME`).
- The game's **update 1.02**, decrypted to `EBOOT.elf`
  (SHA-256 `9303ccce9bc223ca6da2eb837256ef08be6f1f460dcc6d5191889de7a4eee241`;
  the generators refuse any other file).
- A PS3 `dev_flash` (fonts, system modules), e.g. from an RPCS3 installation
  set up with your own firmware.
- Windows 10/11 x64, a D3D12 GPU, a CPU with AVX2/FMA, Visual Studio 2022
  (Build Tools are enough), Python 3.11+.

## Setup

```powershell
git clone --recurse-submodules https://github.com/mario910/buzz-quizworld-recomp.git
cd buzz-quizworld-recomp
python -m venv toolenv
toolenv\Scripts\python.exe -m pip install -r requirements.txt
copy buzz-paths.example.json buzz-paths.json   # then edit the three paths
.\generate.ps1                                  # lifts PPU/SPU code from YOUR EBOOT.elf
.\build-windows.ps1 -NativeCompletion -NativeImage40 -NativeImage39 -NativeImage36 -ResultGuardFP -StageF -NativeImage7 -MStreamModules
.\run-windows.ps1 -Seconds 0 -MenuBootstrap
```

Generation writes `port/recompiled/`, `port/spu/` and `analysis/`; all of them
are derived from the game and are ignored by git, as are `runtime/` (saves,
profiles, shader/pipeline caches), `logs/` and build output.

## Controls

For the desktop frontend, double-click **Launch-Buzz.cmd**, or run
`.\Launch-Buzz.ps1`. It provides game-file selection/validation, offline settings,
keyboard recording, player/pad mapping and a live input test without starting
the game. See [docs/pc-launcher.md](docs/pc-launcher.md). Launcher settings are
separate from existing command-line configs and game profiles.

The game expects Buzz! buzzers. A virtual USB buzzer maps the keyboard:
player 1 **Space** = red, **1** blue, **2** orange, **3** green, **4** yellow
(players 2–4: `Q/W/E/R/T`, `A/S/D/F/G`, `Z/X/C/V/B`).

For configurable keyboard mappings, optional Xbox-compatible controllers and
an experimental second receiver (players 5–8), see [docs/input-and-offline.md](docs/input-and-offline.md).
The native frontend now defaults to an explicit **offline/no-PSN-account** boot
and hides the PlayStation Store menu item. This is not PSN emulation; set
`BUZZ_OFFLINE=0` and/or `BUZZ_HIDE_PSN_STORE=0` to restore the original behavior.
No game files are modified.

The standard build includes the full-state-checked native SIMD image36 leaf
(`-NativeImage36`). `BUZZ_IMAGE36=0` selects its lifted reference; `2` compares
both while retaining reference output. The native leaf is enabled by default
only when built with that switch; the rest of image36 is unchanged.

## Status

Boots through the startup films to the menu and plays single-player rounds,
with sound. On a Ryzen 7 9800X3D / RTX 2070 the heaviest menu scene runs at
about 34 FPS and single-player rounds at 35+ FPS.

Working:

- sound (the game's SPU audio mixer, including the ATRAC and DSP modules it
  loads at runtime);
- startup films (Relentless logo, intro) and the in-game video monitors;
- loading screen clapperboard with its counter, avatars, player names on the
  podium display, the big screen, the studio's animated LED panels;
- virtual buzzers (keyboard, XInput pads), offline/no-PSN boot, PC launcher.

Known issues:

- a rare menu state shows a green monitor with a white square (no reference
  yet for what it should show);
- occasional ~1 s frame-rate drops when returning from the big Buzz logo to
  the menu monitors;
- only single-player has been tested in depth.

Useful launcher switches: `-SkipVideo` skips the startup films, `-Mute`
silences the audio device (the game still runs its audio).

## Layout

| Path | Content |
|---|---|
| `port/` | Hand-written port code (`main.cpp`, virtual buzzer, adapters, CMake) |
| `ps3recomp/` | SDK submodule, branch `buzz-quizworld` (MIT, see its `LICENSE`) |
| `third_party/zlib/` | zlib submodule (zlib license) |
| `generate*.ps1/.py` | Generators: lift code from your `EBOOT.elf`, build adapters |
| `build-windows.ps1`, `run-windows.ps1` | Build and launch |
| `tests/`, `test-*.ps1` | Unit/regression tests |
| `inspect-*.py`, `claude-*.py`, `perf/` | Trace analysis and performance tooling |
| `docs/` | Launcher and input/offline guides |
