# Buzz! Quiz World — experimental native Windows port

An unofficial **PS3Recomp-based static recompilation** of Buzz! Quiz World for
Windows x64, rendering with Vulkan by default and Direct3D 12 as the fallback.
Startup films, menus, audio and single-player rounds have run in our tests.
Original-disc EU and US 1.00 use their own hash-bound generation/PPU-adapter
profiles and share byte-identical SPU images. The optional EU 1.02 update has
separate executable bindings.
Standard two-player gameplay has been tested on Polish assets in both EU releases
and on US 1.00, including podium names, avatars and scoring. Full game validation
is still in progress. This is a research
port, not a finished remaster: frame pacing, visual issues and online play need
more work.

**Source only. You must supply your own game and firmware; a game update is optional.**
No ISO, EBOOT, firmware, game assets, questions, decryption keys, generated
game/firmware translations or ready-to-play executable are provided. The
build generates translations locally; the resulting game is **not** covered
in its entirety by this repository's MIT license. See
[licensing and third-party notices](THIRD_PARTY_NOTICES.md).

## Highlights (2026-11-03)

- **Local preparation without RPCS3:** import your disc/ISO, firmware and optional
  update, validate exact executable hashes, then generate and build locally.
  Preparation sessions can be cancelled and resumed.
- **Native desktop launcher and PC settings:** select prepared copies, map/test
  controls, adjust volume, fullscreen, window size and the optional 30 FPS lock.
  Escape / START pauses gameplay while the settings menu is open on Vulkan.
- **Display and texture options:** internal rendering from 720p to 4K, bilinear
  or AMD FSR 1 presentation, PNG texture replacements, and optional automatic
  Real-ESRGAN texture upscaling. See [display and texture packs](#display-and-texture-packs).
- **Several ways to buzz:** keyboard, up to four XInput pads, optional phone
  browsers over local Wi-Fi with QR pairing, and experimental Windows Buzz HID.
  Physical Buzz receivers and real phone hardware remain unverified.
- **Native geometry and animation:** the qualified EU 1.00/Vulkan/AVX2 build
  enables reviewed host geometry, animation, completion and renderer defaults
  inside the EXE, with whole-job fallbacks for unsupported inputs. It builds
  from a clean checkout without private captured test data. See
  [measured results](docs/native-geometry-summary.md) and
  [runtime defaults](docs/native-runtime-defaults-results.md).
- **Custom question-pack tools:** question decoding/encoding, a JSON pack
  builder, picture sheets, an ATRAC3 speech encoder and Knowledge Is Power
  export/conversion. Custom packs register and appear in Pack Game; playing
  their questions and generated media is still unverified. See
  [custom question packs](#custom-question-packs).

## Supported game — exact match required

| Input | Requirement |
|---|---|
| Disc input | Your ISO, or a directory containing `PS3_GAME/USRDIR` and `PS3_GAME/PARAM.SFO` |
| EU original disc 1.00 | `BCES00647`, `BCES00644`, `BCES00645`; ELF SHA-256 `6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8` |
| US original disc 1.00 | `BCUS98209`; ELF SHA-256 `fb78b9e6d30ba5eaa76b7e31bc3afa838736dcb5cde40fc602a628f2305ebb5d` |
| Optional update 1.02 | `BCES00647` only; ELF SHA-256 `9303ccce9bc223ca6da2eb837256ef08be6f1f460dcc6d5191889de7a4eee241` |
| Firmware | Your PS3 PUP, or installed `dev_flash` including `data/font` and `sys/external/flashATRAC.pic` |
| Audio-module compatibility | The `.text` SHA-256 of `flashATRAC.pic` must be `c543ecc87e82beec3159d88fbb73dc4229424d357c0a48be3c4cb55d3fdefadc`; checked by the validator/generator |

The hash, not just the displayed version, identifies the supported executable.
The supported executable profiles have different bindings and native adapters;
they are not interchangeable. The three listed EU 1.00 executables are byte-identical,
but only Polish-release EU assets have been tested so far. The US executable is
different and has its own reviewed PPU bindings; all 24 SPU images match EU 1.00
byte for byte. See [US validation scope](docs/us-100-status.md).
Quiz TV and Brain Of are not adapted. Renaming
`EBOOT.BIN` to `.elf` does not decrypt it. Do not remove hash checks to force
another version to build. DLC is not required for the tested configuration.

**RPCS3 is not required for preparation or play.** The preparation wizard
extracts your firmware PUP and optional update PKG and decrypts your EBOOT
directly in the app, inside a private session. Encrypted SELF/PUP inputs need
your private local decryption key file; keys are not bundled or downloaded.
Already prepared `dev_flash` and `EBOOT.elf` can be used without a key file.
See [built-in game and firmware import](docs/game-import.md#where-to-find-the-keys) for key sources and formats,
command-line usage and validation. Still-encrypted disc images are
not supported by this importer. Use files you are legally
entitled to use under the rules in your jurisdiction.

## Windows build prerequisites

These are **preparation/development** requirements. Once your own copy has
been prepared, the native C++ launcher starts it without Python, a venv or
PowerShell. The launcher also builds on Linux; the game itself is not yet a
validated Linux port. See [native launcher](docs/pc-launcher.md).

- Windows 10/11 **64-bit** and a GPU/driver supporting Vulkan 1.0 (default renderer) and Direct3D 12 (fallback).
- An x64 CPU with **AVX2 and FMA** for the supported build. AVX-512 is not required.
- Git with submodule support.
- Python **3.11 or newer**, including Tcl/Tk (for the preparation wizard), with
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
-File .\scripts\Setup-Buzz.ps1` (and the corresponding build/start script). This is a
per-process option; changing the machine-wide execution policy is unnecessary.

## Build and play

```powershell
git clone --recurse-submodules https://github.com/mario910/buzz-quizworld-recomp.git
cd buzz-quizworld-recomp
.\scripts\Setup-Buzz.ps1
```

### Guided preparation (recommended)

For the guided route, double-click **Prepare-Buzz.cmd** (or run
`scripts/Prepare-Buzz.ps1`):

```powershell
.\Prepare-Buzz.cmd
```

Select your ISO/dump, PUP/dev_flash and private local key file when needed;
select an update only if you want to use that executable. The wizard identifies
the exact title/executable, generates its matching translations and builds in
an isolated `runtime/setup/sessions/` workspace. It supports cancellation and
resuming an interrupted session. Python, Visual Studio C++ tools and the Windows
SDK are prerequisites, not silently installed system-wide components. Preparation
builds both renderers and the native launcher inside that session's `workspace`.

Wait for **Ready: game build**, then click **Play** in the preparation wizard
to open that prepared copy's native launcher and start the game from it.
For later launches, use **Launch-Buzz.cmd inside the prepared workspace**:
`runtime/setup/sessions/<session-id>/workspace/Launch-Buzz.cmd`.
That workspace also contains `build-launcher/BuzzLauncher.exe`; playing the
prepared copy needs no Python or build tools. The repository-root shortcut
requires a launcher built at the repository root, as in the manual route below.

If you change game/executable paths in the launcher, it looks for a matching prepared build;
it does not run a 1.02 ELF on a 1.00 EXE or vice versa. Unsupported identities
or missing matching builds are rejected with a preparation prompt. No version
is hard-coded as the runtime default. See [1.00 migration status](docs/disc-100-status.md).

### Manual developer build

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
input. EU 1.02 also needs its matching `Patched` assets, not just the update ELF.
The preparation wizard stages these from your update PKG, or from a `Patched`
folder beside a supplied update EBOOT. Missing assets are rejected before play;
1.00 does not need them. Profiles, saves and plaintext caches are not imported.
Setup preserves an existing paths file and does not modify your dump,
firmware or global Python installation.

```powershell
# Optional read-only preflight (also performed by Build-Buzz).
.\toolenv\Scripts\python.exe .\tools\verify-game.py

# Validate, generate all PPU/SPU translations, build both renderers and launcher.
.\scripts\Build-Buzz.ps1 -Jobs 6

# Start the native desktop launcher (no interpreter at runtime).
.\Launch-Buzz.cmd
```

Build-Buzz also builds the native SDL3/ImGui launcher. Its standalone CMake
build needs no game files or Python; see [launcher build/package instructions](docs/pc-launcher.md#build-the-launcher-only).

| Build selection | Command | Game output |
|---|---|---|
| Both renderers (default) | `scripts/Build-Buzz.ps1 -Jobs 6` | Both directories below |
| Vulkan only | `scripts/Build-Buzz.ps1 -Renderer Vulkan -Jobs 6` | `build-vulkan-windows/BuzzQuizWorldRecomp.exe` |
| D3D12 only | `scripts/Build-Buzz.ps1 -Renderer D3D12 -Jobs 6` | `build-windows/BuzzQuizWorldRecomp.exe` |

Each command also builds `build-launcher/BuzzLauncher.exe`. Start through
`Launch-Buzz.cmd`, which opens the launcher, then start the game there; it
selects the matching executable, content and runtime preferences. Vulkan is
preferred unless saved settings select D3D12; make sure the selected renderer
was built. Reduce `-Jobs` if compilation runs out of memory.

The gameplay GUI lets you validate/select files, configure keyboard/controllers
and test buttons. The interface and preparation wizard are in English. The separate preparation
wizard builds locally; neither downloads game content. A background start is enabled by default: click the
game window before using keyboard controls. See [launcher guide](docs/pc-launcher.md).

The game also has an **Escape / controller START** PC settings menu for
volume, a 30 FPS lock, fullscreen, controllers and restart-only offline mode.
It shares per-build settings with the native launcher. On Vulkan the game is
paused while the menu is open (guest clock, vblank and audio held); on the
D3D12 fallback the quiz keeps running. The optional 30 FPS lock paces the game's
CPU thread and is off by default; see [frame-lock details and measurements](docs/frame-limit-status.md).
R1/RB is the red buzzer; D-pad Up/Right/Down/Left select
blue/orange/green/yellow. Windows physical Buzz HID is experimental and
hardware-unverified. `Build-Buzz.ps1` builds both renderers (`-Renderer Vulkan`
or `D3D12` for one); the launcher prefers Vulkan unless PC settings select
`renderer=d3d12`. The [Vulkan bring-up notes](docs/vulkan-bringup.md)
describe the earlier experiment; Vulkan is now the ordinary Windows default.

**Phone buzzers:** enable the optional local HTTP host in **Escape / START →
Phone buzzers**, then scan its QR code on phones connected to the same Wi-Fi.
Players get five virtual buttons, reserved player slots and reconnecting
sessions. The host can lock joining or remove phones. Hosting defaults to off;
see the [phone controller guide](docs/phone-buzzers.md).

For a direct native start from the built project/workspace, using saved settings
(or `buzz-paths.json`) and the
verified runtime defaults:

```powershell
.\build-launcher\BuzzLauncher.exe --project . --launch
```

The older script remains available for developer measurements:

```powershell
.\scripts\run-windows.ps1 -Seconds 0 -MenuBootstrap
# Optional: -InputConfig .\buzz-input.ini -Mute -Background
```

**Keep `-Seconds 0` for ordinary play.** The low-level script defaults to a
40-second diagnostic run and stops only the game instance it launches at the
timeout. `-SkipVideo` skips startup films for repeatable tests; normal play
keeps them and the studio movies enabled.

### Update and rebuild

Close this checkout's game before rebuilding. From the repository root:

```powershell
git pull --ff-only
git submodule update --init --recursive
.\scripts\Setup-Buzz.ps1
```

For a manual build, run `scripts/Build-Buzz.ps1 -Jobs 6` again, then
`Launch-Buzz.cmd`. For the guided route, run `Prepare-Buzz.cmd` and start a
**new preparation session** to build the updated source. Resuming an old session
continues its recorded stages and source snapshot; it does not refresh a
completed build to the latest checkout. Saves/settings belong to each build's
`runtime/`; keep the previous prepared copy if you need its existing data.

After editing only hand-written code, `scripts/Build-Buzz.ps1 -SkipGeneration`
can reuse local lifts. After updating lifters or generation scripts, run the
full build again. Use the **pinned SDK commit**, not the moving branch tip or
`git submodule update --remote`.

The SDK is a normal fork on `master`, retaining upstream and original Buzz
commit history. Its runtime/codegen remain on the validated **v0.17.0** baseline;
v0.18.0 is not yet integrated. See the [fork notes](ps3recomp/BUZZ_FORK.md)
and [inherited-history caveat](docs/publication.md#important-sdk-fork-caveat).

## Display and texture packs

The launcher separates **window size** from **internal resolution**. Internal
rendering offers 100% (1280×720), 150% (1920×1080), 200% (2560×1440) and 300%
(3840×2160), with a restart required after changing it. Window size/fullscreen,
the bilinear/FSR 1 filter and FSR sharpness apply live. FSR 1 scales the finished
frame when the output is larger than the internal image; rendering above the
output resolution uses bilinear downscaling.

Both renderers can dump eligible 2D colour textures as PNGs and load replacements
from the selected build's `runtime/textures/replace/`. In the launcher's Game
tab, optional **automatic texture upscaling** runs Real-ESRGAN in background
batches and keeps the results under `replace/auto/`. New replacements are picked
up on the texture's next upload; handmade replacements take priority. The
default 2048-pixel output limit bounds texture growth.

Install the optional upscaler by selecting **Download the Real-ESRGAN texture
upscaler** in Prepare-Buzz, or run `scripts/Install-Upscaler.ps1` for a developer
checkout. The installer downloads a pinned, size/hash-checked release only
when requested; `-Archive` accepts a local copy. See
[display settings](docs/pc-launcher.md#display-size-and-internal-resolution) and
[texture-pack options](docs/pc-launcher.md#texture-packs-upscaled-textures).

## Custom question packs

`tools/questions/` contains format inspectors, byte-exact round rebuilding,
the `buzzpack.py` JSON pack builder, SDI picture-sheet generation and a mono
ATRAC3 speech encoder. Knowledge Is Power exports can be converted to pack
sources; Buzz! Quiz TV formats have also been inspected. This does not make
either source game a supported executable for this port.

The tested EU 1.00 installation path is
`runtime/dev_hdd0/game/BCES00647/USRDIR/PACKnnnn/<LANG>PACKnnnn.edat`
inside the selected build. The archive is a plain stored ZIP with an `.edat`
name; a `.PAK` name on the HDD is ignored. Two custom packs have registered,
appeared in **Pack Game**, and reached player setup. Their titles currently
reuse disc strings. Full rounds, generated speech/pictures and other game
profiles remain unverified. See the latest
[in-game results and installation details](docs/question-packs.md#in-game-test-results-2026-11-02-eu100-vulkan-build-of-main-499b6d4).
No questions or media are bundled; the tools process your own local files.

## Buzzers and controllers

This game uses **Buzz! buzzers**, not a conventional DualShock. The port
emulates their USB protocol and supplies virtual handsets. Default keys:

| Player | Red / confirm | Yellow | Green | Orange | Blue |
|---|---|---|---|---|---|
| 1 | Space | 4 | 3 | 2 | 1 |
| 2 | Q | W | E | R | T |
| 3 | A | S | D | F | G |
| 4 | Z | X | C | V | B |

Configure mappings in the GUI, or copy `config/buzz-input.example.ini` to
`buzz-input.ini` and pass `-InputConfig`. New configurations enable XInput, with
up to **four physical pads**. Eight logical handsets/two virtual receivers
are experimental, not proof of a tested eight-player match. Original physical
Buzz receivers now have a default-enabled Windows HID provider, verified only
with synthetic inputs; see [hardware scope](docs/physical-buzzers.md). Phone
browsers are supported through the optional [local HTTP host](docs/phone-buzzers.md),
with real-device/full-round testing still pending. Other non-XInput controllers
are not supported input providers.
Keyboard ghosting/rollover can
limit simultaneous players. See [input/offline guide](docs/input-and-offline.md).

## Saves, offline mode and status

Local saves/profiles and caches are under `runtime/`; logs are under `logs/`.
Launcher paths/labels live in `runtime/launcher/`; shared gameplay preferences
are `runtime/pc-settings.ini` and `runtime/input-settings.ini` in the selected
build. They are not committed. Back up
saves before experimenting; this is not a verified RPCS3 save-transfer workflow.

The native frontend defaults to **offline/no-PSN-account**. The 1.02 frontend
can hide the Store entry; this Lua hook is not yet migrated to 1.00, so its Store
item may remain visible. This is not PSN emulation. Online and MyBuzz are not supported in this
source release; the earlier separate server experiments are not included.
Developer overrides (`BUZZ_OFFLINE=0`, `BUZZ_HIDE_PSN_STORE=0`) restore the
original frontend path, not functioning Sony services.

The latest qualified EU 1.00/Vulkan/AVX2 full-profile measurements at 1280×720,
uncapped, recorded **29.57 FPS in the menu and 34.29 FPS in a solo round**.
See [measurement scope and matched comparisons](docs/native-geometry-summary.md);
these are scene/build-specific observations, **not a stable 30 FPS guarantee
throughout the game**. The EXE owns the reviewed defaults for that qualified
profile; launchers preserve explicit developer overrides. Other versions and
renderers retain their own build profiles. Known limitations include
occasional long frame-time spikes on menu transitions, an intermittent green
monitor/white-square state, and limited multiplayer/long-session validation.
Windows with Vulkan (default) or D3D12 is the tested target; Linux and Steam Deck
are not yet a supported build of this port. Other game languages/content sets are unverified.

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
[mario910 SDK fork](https://github.com/mario910/ps3recomp/tree/master),
based on the [upstream toolkit](https://github.com/sp00nznet/ps3recomp).
Buzz! and PlayStation are their owners' trademarks. This project is unofficial
and is not endorsed by Sony or Relentless Software.
