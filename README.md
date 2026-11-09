# Buzz! Quiz World — experimental native port

An unofficial **PS3Recomp-based static recompilation** of Buzz! Quiz World for
Windows and Linux x86-64, with an experimental EU 1.00 Android ARM64 target.
Vulkan is the standalone renderer, with Direct3D 12
available for Windows developer builds. Startup films, menus, audio and
single-player rounds have run in the Windows tests.
**Original-disc EU and US 1.00 are the primary development and optimization
targets. Start with 1.00; no game update is needed.** These releases use their
own hash-bound PPU adapters and share byte-identical SPU programs. The qualified
Windows/Vulkan/AVX2 builds enable full native geometry and animation for both.
EU 1.02 remains a separate compatibility profile: it has shared renderer
improvements and its own reviewed adapters, but not the full 1.00 native-job
profile. Linux and Android have separate build/optimization profiles; Windows
qualification does not establish feature or performance parity on those targets.
Standard two-player gameplay has been tested on Polish assets in both EU releases
and on US 1.00, including podium names, avatars and scoring. Full game validation
is still in progress. This is a research
port, not a finished remaster: frame pacing, visual issues and online play need
more work.

**You must supply your own game and firmware. A game update is optional.**
No ISO, EBOOT, firmware, game assets, questions, decryption keys, generated
game/firmware translations or ready-to-play executable are provided. The
build generates translations locally; the resulting game is **not** covered
in its entirety by this repository's MIT license. See
[licensing and third-party notices](THIRD_PARTY_NOTICES.md).

## Highlights

- **Android ARM64:** full host geometry through NEON, on-screen buzzer and
  optional imported Turnip driver. Build on Linux/WSL and stage prepared data
  over ADB; ISO/PUP preparation inside the Android app is not implemented yet.
  Current phone tests remain below stable 30 FPS. See the
  [build/install guide](android/README.md) and
  [enabled and disabled optimization catalog](docs/optimization-status.md).

- **Local preparation without RPCS3:** import your disc/ISO, firmware and optional
  update, validate exact executable hashes, then generate and build locally.
  Preparation sessions can be cancelled and resumed.
- **In-game PC settings and developer launcher:** select prepared copies, map/test
  controls, adjust volume, fullscreen, window size and the 30 / 60 / Uncapped frame limit.
  Escape / START pauses gameplay while the settings menu is open on Vulkan.
  The tabbed menu supports buzzer navigation and an optional top-right
  FPS/draw-count/resolution overlay, disabled by default.
- **Display and texture options:** internal rendering from 720p to 4K, bilinear
  or AMD FSR 1 presentation, PNG texture replacements, and optional automatic
  Real-ESRGAN texture upscaling. See [display and texture packs](#display-and-texture-packs).
- **Several ways to buzz:** keyboard, up to four XInput pads, optional phone
  browsers over local Wi-Fi with QR pairing, and experimental Windows Buzz HID.
  Physical Buzz receivers and real phone hardware remain unverified.
- **Native geometry and animation:** the qualified EU/US 1.00/Vulkan/AVX2 builds
  enable reviewed host geometry, animation, completion and renderer defaults
  inside the EXE, with whole-job fallbacks for unsupported inputs. They build
  from a clean checkout without private captured test data. See
  [native jobs](docs/native-jobs.md) and
  [runtime defaults](docs/runtime-defaults.md).
- **Custom question-pack tools:** question decoding/encoding, a JSON pack
  builder, picture sheets, an ATRAC3 speech encoder and Knowledge Is Power
  export/conversion. Custom packs register and appear in Pack Game; manual
  tests cover questions, answers and pictures. Full-game and speech-quality
  validation remain limited. See
  [custom question packs](#custom-question-packs).

## Question Studio (Windows / Linux)

[Buzz! Question Studio](docs/question-studio.md) is a separate native C++ / Qt Quick
editor for question libraries. The first version provides question editing,
undo/redo, preview, JSON import/export, KiP JSON import, text/speech import
from unencrypted Quiz World / Quiz TV packs, and local WAV TTS (Windows SAPI /
Linux eSpeak NG). It builds independently of the game.
The editor also builds native Quiz World EDAT packs with images and audio, and
installs them into a selected runtime with validation and backups.
Interface language (Polish/English), source import language and authored content
languages are independent. Projects preserve translations; imports skip questions
that do not contain the selected source language.
See the linked guide for the implementation roadmap, current limits and build
instructions for both platforms. On Windows, `Launch-Question-Studio.cmd` starts
the locally packaged editor after building it.

## Supported game — exact match required

Use an **original-disc 1.00** input for the main optimized path. In preparation,
leave **Game update (optional)** empty (or omit `--update` on the command line).
Do not combine a 1.00 executable with 1.02 update assets or reuse an EXE built
for another region/version. Preparation detects the executable and selects its
profile automatically; there is no manual address patch or optimization preset
to copy between regions.

| Input | Requirement |
|---|---|
| Disc input | Your ISO, or a directory containing `PS3_GAME/USRDIR` and `PS3_GAME/PARAM.SFO` |
| EU original disc 1.00 | `BCES00647`, `BCES00644`, `BCES00645`; ELF SHA-256 `6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8` |
| US original disc 1.00 | `BCUS98209`; ELF SHA-256 `fb78b9e6d30ba5eaa76b7e31bc3afa838736dcb5cde40fc602a628f2305ebb5d` |
| EU update 1.02 — compatibility profile | `BCES00647` only; ELF SHA-256 `9303ccce9bc223ca6da2eb837256ef08be6f1f460dcc6d5191889de7a4eee241`; not the full 1.00 optimization profile |
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

## Build prerequisites

The prepared game needs Windows 10/11 or Linux x86-64, a Vulkan-capable
GPU driver, and AVX2/FMA. Linux also needs the SDL2 runtime. Preparation needs
free disk space for generation, compilation and a local copy of the game.
Git is needed when preparing from a checkout.
On Windows it uses Python 3.11+ and Visual Studio 2022 C++ tools with a
Windows SDK. On Linux it uses Python 3.10+, Clang, CMake, Ninja and SDL2
development files. The wizard checks these and can help install them.

## Build and play

Choose **guided preparation** or **command-line preparation** below. Both
build a private, standalone Vulkan copy in `dist/` next to the build sources.
The separate [manual developer build](#manual-developer-build) is for working
on the port, not required for playing a prepared copy.

### Windows x64 quick start

1. Download **BuzzPrepare-windows-x64.zip** from
   [GitHub Releases](https://github.com/mario910/buzz-quizworld-recomp/releases),
   extract the entire ZIP to a short writable path and open **BuzzSetup.exe**.
   The package includes the compiled wizard and pinned build sources. It runs
   without Git or a separate source download.
2. Use the wizard's buttons for missing system tools. Select your ISO or
   disc dump, firmware PUP or `dev_flash`, and a private key file if the inputs
   are encrypted. Leave **Game update** empty for the optimized EU/US 1.00 build.
   See [game and firmware inputs](docs/game-import.md).
3. Click **Prepare game**. BuzzSetup installs any missing project-local build
   dependencies, identifies your game version, then builds the corresponding
   Vulkan executable. Use **Play Buzz!** to start the game, including on
   subsequent visits. **Open game folder** shows the portable copy in
   `build-files/dist/`.

If interrupted, open **Advanced options** and select the existing session in
`build-files/runtime/setup/sessions/`. Select the same inputs and click
**Prepare game** again. Advanced options also holds the optional game update
and the system tools checklist.

### Guided preparation from a checkout

1. Clone the source and its pinned submodules:

   ```sh
   git clone --recurse-submodules https://github.com/mario910/buzz-quizworld-recomp.git
   cd buzz-quizworld-recomp
   ```

2. On Windows, [build the wizard locally](preparation/README.md#developer-build)
   and open **`dist-preparation/bin/BuzzSetup.exe`**, or open the release app and choose your
   checkout under **Advanced options → Build sources**. On Linux run **`./bin/BuzzPrepare`** from
   the package or [build the wizard locally](preparation/README.md#linux-developer-build).
   On Ubuntu 22.04/24.04 install `libqt6widgets6` and the distro’s OpenSSL 3
   runtime if they are missing. Select your checkout under **Advanced options → Build sources**
   if the wizard did not locate it.
3. Resolve missing dependencies using the buttons in the wizard. Select your
   ISO or disc dump, firmware PUP or `dev_flash`, and, if needed, your local
   key file. **For EU/US 1.00, leave Game update empty.** Only select an update
   PKG/EBOOT when intentionally preparing the separate EU 1.02 profile.
   See [game and firmware inputs](docs/game-import.md).
4. Click **Prepare game**. The wizard installs any missing local build
   dependencies and builds the matching Vulkan
   executable in an isolated session under `runtime/setup/sessions/`. If
   interrupted, select that session and the same inputs to resume.
5. When preparation reports **Ready to play**, click **Play Buzz!**, or open
   **`dist/BuzzQuizWorldRecomp.exe`** on Windows or
   **`./dist/BuzzQuizWorldRecomp`** on Linux. Keep the `runtime/` directory
   and any packaged libraries beside the executable when moving the folder. Open PC settings with
   **Escape / controller START** to configure display, audio, textures and
   keyboard or pad bindings. Use **Save settings** to keep the changes.
   On Windows, the checkout's `Launch-Buzz.cmd` also starts
   `dist/BuzzQuizWorldRecomp.exe` when it exists.

The prepared game does **not** need Python, PowerShell, a compiler, RPCS3 or a
separate gameplay launcher. Build tools and interpreters are preparation
dependencies; RPCS3 is not used.
Move the **whole `dist/` folder**, not just the EXE. It contains your own game
and firmware data: **keep it private; do not upload it**. Saves and settings
travel with `dist/runtime/`. Before preparing another version or region, move
the existing `dist` aside and keep its saves with that copy.

### Command-line preparation (without the wizard)

Windows: install the [build prerequisites](#build-prerequisites), clone as
above, then run these commands from the checkout in PowerShell. Replace the
example paths with your own EU or US **1.00** disc and firmware inputs:

```powershell
.\scripts\Setup-Buzz.ps1 -BuildOnly
.\toolenv\Scripts\python.exe .\tools\prepare-game.py --dist `
  --game 'D:\Games\Buzz! Quiz World.iso' `
  --firmware 'D:\PS3\PS3UPDAT.PUP' `
  --keys 'D:\Private\ps3.keys' --jobs 6

# After preparation succeeds:
.\dist\BuzzQuizWorldRecomp.exe
```

For an already decrypted disc dump plus installed firmware, `--game` can name
the folder containing `PS3_GAME`, and `--firmware` can name `dev_flash`.
The local key file is needed only for encrypted inputs. Omit `--keys` if none
of your inputs need decryption. No update is passed in this example, so the
disc's 1.00 executable is selected.

Linux: after installing the [Linux preparation dependencies](preparation/README.md#developer-build),
use the same inputs and pipeline:

```sh
python3 tools/setup-linux.py
./toolenv/bin/python tools/prepare-game.py --dist \
  --game '/path/to/Buzz! Quiz World.iso' \
  --firmware /path/to/PS3UPDAT.PUP --keys /path/to/ps3.keys --jobs 6
./dist/BuzzQuizWorldRecomp
```

Lower `--jobs` if compilation runs out of memory. For interruption/resume,
reuse the same inputs with `--resume <session-directory>`; see
[import and resume options](docs/game-import.md). Source inputs are not modified.

### Manual developer build

This Windows route keeps source/build outputs and uses the native developer
launcher to supply the matching paths. It does **not** publish a portable
`dist/` copy. Run `scripts/Setup-Buzz.ps1` to create the tool environment and
paths template, then configure your own decrypted **disc 1.00** executable.

Edit the newly created **`buzz-paths.json`**, using absolute paths to your
own files. Forward slashes in JSON work on Windows and avoid backslash escaping:

```json
{
  "game_root": "D:/Games/Buzz! Quiz World [BCES00647]",
  "dev_flash": "D:/PS3/dev_flash",
  "eboot_elf": "D:/PS3/BCES00647-1.00/EBOOT.elf"
}
```

`game_root` is the **parent of `PS3_GAME`**, not `PS3_GAME` or `USRDIR` itself.
For US 1.00, point at your `BCUS98209` dump and its own decrypted disc ELF;
the profile is detected by title ID and hash. Do not rename encrypted
`EBOOT.BIN` to `.elf`. If testing EU 1.02 instead, keep its decrypted update ELF
separate from the disc dump. EU 1.02 also needs its matching `Patched` assets,
not just the update ELF.
The preparation wizard stages these from your update PKG, or from a `Patched`
folder beside a supplied update EBOOT. Missing assets are rejected before play;
1.00 does not need them. Profiles, saves and plaintext caches are not imported.
Setup preserves an existing paths file and does not modify your dump,
firmware or global Python installation.

```powershell
# Optional read-only preflight (also performed by Build-Buzz).
.\toolenv\Scripts\python.exe .\tools\verify-game.py

# Validate, generate translations, build the optimized Vulkan game and launcher.
.\scripts\Build-Buzz.ps1 -Renderer Vulkan -Jobs 6

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

If a standalone `dist/` already exists, `Launch-Buzz.cmd` prefers that copy.
To test a developer rebuild explicitly, run
`build-launcher/BuzzLauncher.exe --project . --launch` from that build's
checkout/workspace. Do not copy an EU EXE into a US build or mix manifests.

The gameplay GUI lets you validate/select files, configure keyboard/controllers
and test buttons. The interface and preparation wizard are in English. The separate preparation
wizard builds locally; neither downloads game content. A background start is enabled by default: click the
game window before using keyboard controls. See [launcher guide](docs/pc-launcher.md).

The game also has an **Escape / controller START** PC settings menu for
volume, a 30 / 60 / Uncapped frame limit, fullscreen, controllers and restart-only offline mode.
It shares per-build settings with the native launcher. On Vulkan the game is
paused while the menu is open (guest clock, vblank and audio held); on the
D3D12 fallback the quiz keeps running. The optional 30/60 FPS cap paces the game's
CPU thread and is off by default; see [frame pacing](docs/frame-pacing.md).
R1/RB is the red buzzer; D-pad Up/Right/Down/Left select
blue/orange/green/yellow. Windows physical Buzz HID is experimental and
hardware-unverified. `Build-Buzz.ps1` builds both renderers (`-Renderer Vulkan`
or `D3D12` for one); the launcher prefers Vulkan unless PC settings select
`renderer=d3d12`. See [renderer architecture](docs/renderer.md) for submission,
resource-lifetime and diagnostic behavior.

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
`Launch-Buzz.cmd`. For the guided route, open BuzzSetup and start a
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

The in-game PC menu separates **window size** from **internal resolution**. Internal
rendering offers 100% (1280×720), 150% (1920×1080), 200% (2560×1440) and 300%
(3840×2160), with a restart required after changing it. Window size/fullscreen,
the bilinear/FSR 1 filter and FSR sharpness apply live. FSR 1 scales the finished
frame when the output is larger than the internal image; rendering above the
output resolution uses bilinear downscaling.

Both renderers can dump eligible 2D colour textures as PNGs and load replacements
from the selected build's `runtime/textures/replace/`. Optional **automatic texture upscaling** runs Real-ESRGAN in background
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
name; a `.PAK` name on the HDD is ignored. Custom packs register their own
titles and category/round records. Manual tests cover question text, answers,
pictures and round progression; full-game/finale and speech-quality coverage
remain limited. See [pack installation and discovery](docs/question-packs.md#installation-and-discovery).
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

For a prepared distribution, saves, profiles, caches and standalone startup
logs live under `dist/runtime/`. Preferences live in `pc-settings.ini` and
`input-settings.ini` in that directory. Developer launchers retain their own
logs and path settings. Back up
saves before experimenting; this is not a verified RPCS3 save-transfer workflow.

The native frontend defaults to **offline/no-PSN-account**. The 1.02 frontend
can hide the Store entry; this Lua hook is not yet migrated to 1.00, so its Store
item may remain visible. This is not PSN emulation. Online and MyBuzz are not supported in this
source release; the earlier separate server experiments are not included.
Developer overrides (`BUZZ_OFFLINE=0`, `BUZZ_HIDE_PSN_STORE=0`) restore the
original frontend path, not functioning Sony services.

Performance depends on the build, scene, hardware and execution environment;
the port does not guarantee stable 30 FPS throughout the game. Follow the
[profiling and validation rules](docs/performance.md) for comparisons.
The EXE owns the reviewed defaults for the qualified
profile; launchers preserve explicit developer overrides. Other versions and
renderers retain their own build profiles. Known limitations include
occasional long frame-time spikes on menu transitions, an intermittent green
monitor/white-square state, and limited multiplayer/long-session validation.
The standalone Vulkan build targets Windows and Linux x86-64. D3D12 remains
a Windows developer build. Steam Deck hardware and other game languages/content
sets need separate validation.

## Troubleshooting and development

See the [documentation index](docs/README.md) for runtime architecture and file-format references.

- Missing submodule: `git submodule update --init --recursive --depth 1`.
- Wrong ELF/audio hash: obtain the supported files from your own installation;
  do not bypass the guard. The audio check hashes the ELF `.text`, not the
  whole `flashATRAC.pic` file.
- Missing compiler/SDK: use the dependency buttons in BuzzPrepare and recheck.
- Build already running/game EXE locked: close your game, then retry. Build
  scripts do not kill it. Avoid parallel builds against one output directory.
- No keyboard response: focus the game window, check the buzzer mapping and
  make sure no old `BUZZ_SCRIPT` test variable is set in your CLI shell.
- Launcher fails: inspect `runtime/launcher/launcher-error.log` and
  `runtime/launcher/last-start.log`. Game errors go to `logs/*.stderr.log`.
  Review/redact local paths and any game-derived data before sharing logs.

[Developer scripts and tests](docs/development.md) ·
[CI coverage and artifacts](docs/ci.md) ·
[Contribution rules](CONTRIBUTING.md) ·
[Publication audit/scope](docs/publication.md) ·
[Third-party licenses](THIRD_PARTY_NOTICES.md)

Thanks to **Ned Heller / sp00nznet and the ps3recomp contributors**, the FFmpeg,
PPSSPP, stb and zlib authors. The port pins the modified
[mario910 SDK fork](https://github.com/mario910/ps3recomp/tree/master),
based on the [upstream toolkit](https://github.com/sp00nznet/ps3recomp).
Buzz! and PlayStation are their owners' trademarks. This project is unofficial
and is not endorsed by Sony or Relentless Software.
