# Buzz! Quiz World — experimental native Windows port

An unofficial **PS3Recomp-based static recompilation** of Buzz! Quiz World for
Windows x64, using Direct3D 12. Version 1.02 has reached startup films, menus,
audio and single-player rounds in our tests. Original-disc EU 1.00 has its own
generation/native-adapter profile. Standard two-player gameplay has now been
tested on Polish assets in both releases, including podium names, avatars and
question images. Full game validation is still in progress. This is a research
port, not a finished remaster: frame pacing, visual issues and online play need
more work.

**Source only. You must supply your own game and firmware; a game update is optional.**
No ISO, EBOOT, firmware, game assets, questions, decryption keys, generated
game/firmware translations or ready-to-play executable are provided. The
build generates translations locally; the resulting game is **not** covered
in its entirety by this repository's MIT license. See
[licensing and third-party notices](THIRD_PARTY_NOTICES.md).

## Supported game — exact match required

| Input | Requirement |
|---|---|
| Disc input | Your ISO, or a directory containing `PS3_GAME/USRDIR` and `PS3_GAME/PARAM.SFO` |
| EU original disc 1.00 | `BCES00647`, `BCES00644`, `BCES00645`; ELF SHA-256 `6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8` |
| Optional update 1.02 | `BCES00647` only; ELF SHA-256 `9303ccce9bc223ca6da2eb837256ef08be6f1f460dcc6d5191889de7a4eee241` |
| Firmware | Your PS3 PUP, or installed `dev_flash` including `data/font` and `sys/external/flashATRAC.pic` |
| Audio-module compatibility | The `.text` SHA-256 of `flashATRAC.pic` must be `c543ecc87e82beec3159d88fbb73dc4229424d357c0a48be3c4cb55d3fdefadc`; checked by the validator/generator |

The hash, not just the displayed version, identifies the supported executable.
The two executable families have different bindings and native-adapter profiles;
they are not interchangeable. The three listed EU 1.00 executables are byte-identical,
but only Polish-release assets have been boot-tested so far. USA (`BCUS98209`),
Quiz TV and Brain Of are not adapted. Renaming
`EBOOT.BIN` to `.elf` does not decrypt it. Do not remove hash checks to force
another version to build. DLC is not required for the tested configuration.

An existing RPCS3 installation may be a convenient place to locate your
installed firmware and your own game/update files. **RPCS3 is not used to run
this port.** The preparation wizard can use a separately supplied local RPCS3
executable as a headless import helper to install your PUP/update and decrypt
your EBOOT in a private session; it does not modify your normal RPCS3 settings.
No game, firmware or disc keys are downloaded. Still-encrypted disc images are
not supported by this importer. Use files you are legally
entitled to use under the rules in your jurisdiction.

## Windows prerequisites

- Windows 10/11 **64-bit** and a GPU/driver supporting Direct3D 12.
- An x64 CPU with **AVX2 and FMA** for the supported build. AVX-512 is not required.
- Git with submodule support.
- Python **3.11 or newer**, including Tcl/Tk (for the desktop launcher), with
  `python` available on PATH.
- Visual Studio 2022 or its Build Tools, with **Desktop development with C++**
  and a Windows 10/11 SDK. The build uses Clang via `clang-cl`, with the VS
  headers/libraries and Windows SDK. Clang, CMake, Ninja and Python packages
  are installed into/for the local `toolenv` by the setup script.
- Substantial free disk space for generated C/C++, compiler objects and
  caches. Six concurrent compiler jobs are the default; reduce `-Jobs` if
  memory is limited. No minimum RAM/disk or low-end performance guarantee has
  been established.

PowerShell commands below work from the repository directory. If script
execution is blocked, use `powershell.exe -NoProfile -ExecutionPolicy Bypass
-File .\Setup-Buzz.ps1` (and the corresponding build/start script). This is a
per-process option; changing the machine-wide execution policy is unnecessary.

## Build and play

```powershell
git clone --recurse-submodules --shallow-submodules https://github.com/mario910/buzz-quizworld-recomp.git
cd buzz-quizworld-recomp
.\Setup-Buzz.ps1
```

For the guided route, double-click **Prepare-Buzz.cmd** (or run
`Prepare-Buzz.ps1`). Select your ISO/dump, PUP/dev_flash and local `rpcs3.exe`;
select an update only if you want to use that executable. The wizard identifies
the exact title/executable, generates its matching translations and builds in
an isolated `runtime/setup/sessions/` workspace. It supports cancellation and
resuming an interrupted session. Python, Visual Studio C++ tools and the Windows
SDK are prerequisites, not silently installed system-wide components.

**Launch-Buzz.cmd** then opens the last verified prepared build. If you change
game/executable paths in the launcher, it looks for a matching prepared build;
it does not run a 1.02 ELF on a 1.00 EXE or vice versa. Unsupported identities
or missing matching builds are rejected with a preparation prompt. No version
is hard-coded as the runtime default. See [1.00 migration status](docs/disc-100-status.md).

For the manual developer route:

Edit the newly created **`buzz-paths.json`**, using absolute paths to your
own files. Forward slashes in JSON work on Windows and avoid backslash escaping:

```json
{
  "game_root": "D:/Games/Buzz! Quiz World [BCES00647]",
  "dev_flash": "D:/PS3/dev_flash",
  "eboot_elf": "D:/PS3/BCES00647-1.02/EBOOT.elf"
}
```

`game_root` is the **parent of `PS3_GAME`**, not `PS3_GAME` or `USRDIR` itself.
Do not replace the executable in your disc dump: the update ELF is a separate
input. Setup preserves an existing paths file and does not modify your dump,
firmware or global Python installation.

```powershell
# Optional read-only preflight (also performed by Build-Buzz).
.\toolenv\Scripts\python.exe .\verify-game.py

# Validate, generate all PPU/SPU translations, then build the supported variant.
.\Build-Buzz.ps1 -Jobs 6

# Start the desktop launcher; Launch-Buzz.cmd is the double-click equivalent.
.\Launch-Buzz.ps1
```

The gameplay GUI lets you validate/select files, configure keyboard/controllers
and test buttons. The interface and preparation wizard are in English. The separate preparation
wizard builds locally; neither downloads game content. A background start is enabled by default: click the
game window before using keyboard controls. See [launcher guide](docs/pc-launcher.md).

For a direct start using `buzz-paths.json` and the verified runtime defaults:

```powershell
.\run-windows.ps1 -Seconds 0 -MenuBootstrap
# Optional: -InputConfig .\buzz-input.ini -Mute -Background
```

**Keep `-Seconds 0` for ordinary play.** The low-level script defaults to a
40-second diagnostic run and stops only the game instance it launches at the
timeout. `-SkipVideo` skips startup films for repeatable tests; normal play
keeps them and the studio movies enabled.

After editing hand-written code, `Build-Buzz.ps1 -SkipGeneration` reuses local
lifts. After updating lifters or generation scripts, run the full build again.
For an existing clone: `git pull --ff-only`, then
`git submodule update --init --recursive --depth 1`. Use the **pinned SDK
commit**, not the fork's `master` or `git submodule update --remote`.

## Buzzers and controllers

This game uses **Buzz! buzzers**, not a conventional DualShock. The port
emulates their USB protocol and supplies virtual handsets. Default keys:

| Player | Red / confirm | Yellow | Green | Orange | Blue |
|---|---|---|---|---|---|
| 1 | Space | 4 | 3 | 2 | 1 |
| 2 | Q | W | E | R | T |
| 3 | A | S | D | F | G |
| 4 | Z | X | C | V | B |

Configure mappings in the GUI, or copy `buzz-input.example.ini` to
`buzz-input.ini` and pass `-InputConfig`. XInput controllers are opt-in, with
up to **four physical pads**. Eight logical handsets/two virtual receivers
are experimental, not proof of a tested eight-player match. Physical USB
buzzer passthrough, Android/browser handsets and non-XInput controllers are
not implemented as supported input providers. Keyboard ghosting/rollover can
limit simultaneous players. See [input/offline guide](docs/input-and-offline.md).

## Saves, offline mode and status

Local saves/profiles and caches are under `runtime/`; logs are under `logs/`.
Launcher settings live in `runtime/launcher/`. They are not committed. Back up
saves before experimenting; this is not a verified RPCS3 save-transfer workflow.

The native frontend defaults to **offline/no-PSN-account**. The 1.02 frontend
can hide the Store entry; this Lua hook is not yet migrated to 1.00, so its Store
item may remain visible. This is not PSN emulation. Online and MyBuzz are not supported in this
source release; the earlier separate server experiments are not included.
Developer overrides (`BUZZ_OFFLINE=0`, `BUZZ_HIDE_PSN_STORE=0`) restore the
original frontend path, not functioning Sony services.

With 1.02 on a Ryzen 7 9800X3D / RTX 2070, the heavy menu scene has reached roughly
the low-to-mid 30 FPS range. This is a machine/scene-specific observation, **not
a stable 30 FPS guarantee throughout the game**. Known limitations include
occasional long frame-time spikes on menu transitions, an intermittent green
monitor/white-square state, and limited multiplayer/long-session validation.
Windows/D3D12 is the tested target; Linux, Vulkan and Steam Deck are not yet a
supported build of this port. Other game languages/content sets are unverified.

## Troubleshooting and development

- Missing submodule: `git submodule update --init --recursive --depth 1`.
- Wrong ELF/audio hash: obtain the supported files from your own installation;
  do not bypass the guard. The audio check hashes the ELF `.text`, not the
  whole `flashATRAC.pic` file.
- Missing compiler/SDK: install the C++ workload and Windows SDK; rerun Setup.
- Build already running/game EXE locked: close your game, then retry. Build
  scripts do not kill it. Avoid parallel builds against one output directory.
- No keyboard response: focus the game window, check the buzzer mapping and
  make sure no old `BUZZ_SCRIPT` test variable is set in your CLI shell.
- Launcher fails: inspect `runtime/launcher/launcher-error.log` and
  `runtime/launcher/last-start.log`. Game errors go to `logs/*.stderr.log`.
  Review/redact local paths and any game-derived data before sharing logs.

[Developer scripts and tests](docs/development.md) ·
[Contribution rules](CONTRIBUTING.md) ·
[Publication audit/scope](docs/publication.md) ·
[Third-party licenses](THIRD_PARTY_NOTICES.md)

Thanks to **Ned Heller / sp00nznet and the ps3recomp contributors**, the FFmpeg,
PPSSPP, stb and zlib authors. The port pins the modified
[mario910 SDK branch](https://github.com/mario910/ps3recomp/tree/buzz-quizworld),
based on the [upstream toolkit](https://github.com/sp00nznet/ps3recomp).
Buzz! and PlayStation are their owners' trademarks. This project is unofficial
and is not endorsed by Sony or Relentless Software.
