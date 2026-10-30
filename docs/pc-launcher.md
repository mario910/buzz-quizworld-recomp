# Native PC launcher

The player-facing launcher is **C++17 + SDL3 + Dear ImGui**. It starts an already
prepared game directly through SDL's process API. Playing needs no Python,
virtual environment, PowerShell, compiler, or automatic downloads.

The separate Python preparation wizard remains a developer tool for importing
the user's ISO/PUP and generating/building the game. It is not bundled into, or
called by, the native launcher. The [old Python gameplay UI](legacy-python-launcher.md)
remains available explicitly with `scripts/Launch-Buzz.ps1 -PythonLauncher`.

## Start

On Windows, use `Launch-Buzz.cmd` after building, or start `BuzzLauncher.exe`
from an installed launcher folder. The root shortcut now selects the native
binary only; it does not silently invoke Python or PowerShell.

The Windows EXE statically links SDL3 and the C++ runtime. The initial checked
build is approximately 3.8 MB and imports only Windows system DLLs. Keep the
installed `licenses/` directory with it.

On Linux, run the installed `BuzzLauncher`, or for development:

```sh
./build-launcher/BuzzLauncher --project "$PWD"
```

Windows and Linux are separate binaries built from the same sources. The
launcher itself builds and passes headless tests on Ubuntu 24.04/Clang 18.
**This does not establish a Linux/Steam Deck game port.** Linux selects a native
`BuzzQuizWorldRecomp` ELF; it does not invoke Wine/Proton for a Windows EXE.
The game's Linux renderer/input integration and a finished AppImage remain
separate work. Physical controller and desktop/Wayland interaction tests are
not yet complete.

## Build the launcher only

No game, firmware, SDK update, generated translations or Python is needed to
build this component. Use CMake 3.31+, network access for initial dependency
fetching, a C++17 compiler and platform development libraries. Sources are pinned
by archive SHA-256 in `launcher/native/CMakeLists.txt`; builds do not follow
moving branches.

CMake 3.31+ is required even if a distribution ships an older CMake: older
Ninja generators can silently lose header dependencies for object paths needing
shell quotes. This was reproduced under our source path containing parentheses,
producing mixed old/new object layouts rather than a valid incremental build.
See the [upstream fix](https://fuchsia.googlesource.com/third_party/cmake/+/b11323f1e415706b6477c0384ddc9dea83bbac5b).
After upgrading an affected build, clean its compiled artifacts once and rebuild.
This changes the build tool, not the tested v0.17 SDK baseline or player runtime.

Windows: from a VS C++ developer shell with CMake and Ninja:

```powershell
cmake -S launcher/native -B build-launcher -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-launcher --parallel 6
ctest --test-dir build-launcher --output-on-failure
cmake --install build-launcher --prefix dist-launcher/windows
```

`scripts/build-launcher.ps1` is an optional developer convenience that finds the
VS tools and existing CMake. It is not a runtime dependency. Standard
`scripts/Build-Buzz.ps1` now builds the native launcher too; `-SkipLauncher`
is available for isolated game-only experiments.

Linux uses the same CMake commands with a C/C++ compiler and the SDL development
dependencies documented in [SDL's Linux build guide](https://wiki.libsdl.org/SDL3/README-linux#build-dependencies).
For example, configure with `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`.
Install to a private folder or produce a launcher-only archive with
`cpack --config build-launcher/CPackConfig.cmake -G TGZ`. This is not an AppImage
or a universal glibc-compatibility guarantee. The installed launcher contains
no game or firmware files.

## Prepared-copy layout

The launcher recognizes the developer `build-windows/` / `build-linux/` layout,
matching builds in this project's `runtime/setup/sessions/*/workspace`, and a
portable `game/` directory beside the installed launcher:

```text
BuzzLauncher.exe                 # Windows; BuzzLauncher on Linux
licenses/
game/
  BuzzQuizWorldRecomp.exe         # Windows; no extension on Linux
  game-build.json                # Must match this exact binary and EBOOT
buzz-paths.json                  # User's own paths; optional until configured
runtime/                        # Private settings, saves, caches and logs
```

Never mix an EBOOT from one version with another version's compiled EXE.
`game-build.json` is mandatory for native launching; legacy unbound builds
must be rebuilt. The exact title ID, ELF hash, adapter family, EXE hash and
version-bound runtime defaults are validated before every start. These are
consistency checks, not a signed security boundary for untrusted packages.

Game/firmware inputs remain user-supplied. A local portable folder can use
relative paths in `buzz-paths.json`; see `config/portable-paths.example.json`.
They are resolved against the chosen project/package root, not the current
working directory or a sibling prepared build. No game-file copy or modification
is performed by the launcher itself.

## Three tabs

For new configurations, XInput is enabled and the default buzzer layout is
**R1/RB = red, D-pad Up = blue, Right = orange, Down = green, Left = yellow**.
START is no longer a buzzer action in this preset. Existing saved mappings and
an explicit disabled-controller setting are retained; choose **Use R1 + D-pad
preset** in Controls, then Save, to opt into the new defaults.

- **Game:** choose the dump folder, `dev_flash`, and decrypted `EBOOT.elf`;
  check identity/build hashes; toggle offline mode, Store visibility and
  background game start. Hash checks run off the UI thread; editing is disabled
  while a check is in progress.
- **Controls:** 4/8 logical buzzers, player labels, five keyboard buttons,
  transactional recording of five keys, tap retention, and four controller-slot
  assignments. Duplicate buttons within a handset are rejected; incomplete or
  shared-key mappings require confirmation before Play.
- **Button test:** show raw held buttons and identify a controller with START.
  No desktop injection/global keyboard hooks; input is observed only while the
  launcher is focused. Recording/identification is canceled on focus loss.

Player labels do not rename in-game profiles. Eight logical handsets do not
imply eight physical gamepads or force eight contestants into the selected mode.
Windows mappings intentionally retain the game's **four XInput slots**; SDL
HID devices that the game's current adapter cannot consume are not advertised
as working controllers. Linux SDL input preview is not yet a game input adapter.

## Settings and process isolation

The native launcher reads/writes the same version-1
`runtime/launcher/settings.json` schema as the Python UI, retaining existing
paths, mappings and custom labels. Only old automatic `Gracz N` labels are
normalized to `Player N`. Missing settings fall back to `buzz-paths.json`.
Per-build `pc-settings.ini` and `input-settings.ini`, when present, are the
authoritative audio/video/network and controller settings shared with the game.
Malformed settings block Save/Play and are preserved for repair; defaults are
not silently written over them.

Every start creates a unique `runtime/launcher/sessions/<id>/` in the selected
build, with `paths.json`, `input.ini`, `stdout.log` and `stderr.log`. Settings
are atomically replaced; a running game's configuration snapshot stays unchanged.
The existing `runtime/dev_hdd0`/`dev_hdd1` save locations are retained per build.

The C++ launch plan carries over the verified renderer/audio/SPURS defaults.
Explicit developer performance overrides remain supported, including the
old distinction between unset and intentionally empty list options. Stale
`BUZZ_SCRIPT`, `BUZZ_KEYS*` and `BUZZ_PULSE_MS` are removed from the child
environment so GUI input configuration wins. Inherited environment variables
are never dumped by diagnostics. Automatic SPU capture/dump directories from
the old diagnostic script are not enabled for ordinary play.

Arguments are passed as an array to SDL, not assembled into a shell command.
The launcher never changes its parent process's environment or current working
directory. It checks for an already-running copy of the selected game binary.
Closing the launcher does **not** terminate the game. Logs/settings may contain
private local paths; review them before sharing.

## Validation and diagnostics

The Windows/D3D12 game now includes a PC settings overlay. Open/close it with
**Escape** or **START** on an XInput controller. R1/RB remains the red buzzer;
D-pad Up/Right/Down/Left are blue/orange/green/yellow. Custom legacy mappings
that use START as a buzzer keep that binding; use Escape for the menu until
you select the new preset. The keyboard uses arrows, Enter/Space and Backspace;
controllers use D-pad, A/R1 to select and B to go back. Mouse buttons also work.

The purple/navy overlay has volume, 30 FPS lock, borderless fullscreen,
restart-only offline mode, controller assignments, the R1/D-pad preset, tap
retention and a restart-only 4/8-buzzer count. It **does not pause the quiz**.
Guest buzzer input is suppressed while open and held buttons must be released
after closing/remapping before they can answer a question. Quit requires a
separate confirmation. See `port/pc_overlay.cpp` and `port/pc_menu_model.h`.

The overlay shares the existing D3D12 command list and draws after guest
compositing, before presentation. Its resources are lazy-created, not a second
window or a separate renderer. It does not alter guest shaders, framebuffer
resolution or the Cell/SPURS performance work. `BUZZ_PC_OVERLAY=0` disables it
for diagnostic A/B checks (and restores the SDK's old Escape-to-close behavior).
The runtime now connects volume to the shared audio mixer and borderless/fullscreen
and the 0/30 FPS cap to D3D12 host controls. To seed preferences without the menu,
copy `config/pc-settings.example.ini` to the selected build's `runtime/pc-settings.ini`
or pass an explicit `BUZZ_PC_SETTINGS` path. It is loaded once, never polled.
Without a file, the existing SDK presentation/vsync behaviour remains unchanged.
Save settings writes `runtime/pc-settings.ini` and `runtime/input-settings.ini`
for the selected build. Volume/cap/fullscreen and controller mappings apply live;
network policy remains restart-only. The native launcher reads and writes these
same files, including custom controller mappings and offline policy. Its Game
tab also exposes volume, frame lock and fullscreen. Legacy launcher JSON remains
compatible and provides paths, player labels and fallback preferences where a
build has no INI yet; simply opening/checking a copy never writes a migration.
Unsupported renderer
requests are rejected, not silently replaced with another backend.

Preferences belong to the selected build's runtime directory, not whichever
catalog happened to launch it. Startup checks identify the build asynchronously.
When switching to another prepared build, the launcher loads its preferences
and asks you to review them before Play/Save. It does not overwrite one version's
settings with another version's unsaved values. Gameplay editing/saving in the
launcher is disabled while its game runs; use the in-game menu instead. After
its game exits, the launcher reloads the saved preferences.

Save/launch checks the content revisions read from disk. If another writer has
changed, removed or added an INI since it was loaded, the launcher refuses the
stale action: choose **Reload saved** to discard drafts and read the new files.
Malformed, oversized and non-file settings are errors, never silently reset.
Linked settings can be read, but saves refuse to replace the link. Each INI is
atomically replaced; a partial-pair I/O failure is explicitly reported, not
presented as a successful save. Files are loaded once at startup/menu actions,
not polled during gameplay. The legacy Python UI is not a shared-settings editor.

Volume changes ramp over one mixer block; 100% uses the bit-preserving old path.
The frame limiter only wraps actual D3D12 presentations and leaves the guest
vblank cadence and audio pacing code unchanged. Initial isolated 1.02 startup
tests observed 29.53-30 FPS capped and 60-63 FPS without the cap, after warm-up.
This is not a heavy-scene performance result or verification of uncapped round
timers. Borderless mode deliberately does not change guest rendering resolution.

The Windows game has a default-enabled original Buzz HID provider, labelled
**hardware-unverified** until somebody can test a working receiver. It is
independent of the virtual USB receiver emulation and the launcher's SDL input
preview. See [physical buzzer scope and tests](physical-buzzers.md). The launcher
Button test still covers keyboard/XInput, not physical Buzz receivers.
Linux host HID remains pending. A separate Windows Vulkan experiment now uses a
native Win32 window and has rendered initial screens/video and the 3D main menu. It still waits
after individual GPU submissions, has no PC overlay, and is not exposed by the
ordinary launcher. See [Vulkan bring-up scope and verification](vulkan-bringup.md).
Gameplay, Linux title integration and D3D12 render-feature parity remain pending.

```text
BuzzLauncher --project DIR --check
BuzzLauncher --project DIR --dry-run
BuzzLauncher --project DIR --launch
BuzzLauncher --project DIR --smoke --smoke-tab 0 --capture preview.bmp
```

`--check` / `--dry-run` do not save settings, start a game or create a session.
Dry-run reports only explicitly allowlisted runtime settings, not the inherited
environment. `--smoke` creates a hidden software-rendered window, does not poll
physical pads, and never starts a game or writes settings. Optional captures
are written only to the explicitly supplied BMP path.

CTest checks settings/hash/profile validation, native-game INI parser parity,
unique snapshots, performance defaults/overrides, Unicode/literal arguments,
working directory/environment and redirected logs using a synthetic native child.
The hidden UI test runs on Windows and Linux without taking desktop control.
`tests/test_native_launcher.py`, with `BUZZ_NATIVE_LAUNCHER_ORACLE` pointing at
`launcher_native_test`, compares migration/INI output to the existing Python
model. These checks do not establish physical-controller correctness.

On Windows, `buzzer-host-menu` compiles the real input adapter with synthetic
OS reads. It checks START capture before guest input, held-button release after
closing, RB/D-pad mappings, preserved legacy START mappings, live disable/enable,
keyboard focus loss and XInput disconnect/reconnect. It sends no desktop events
and does not poll a physical pad. The portable menu repeat/release logic and
shared settings tests also run on Linux. These tests complement, not replace,
eventual hardware testing.

`tests/native-launcher-integration.ps1` is a developer-only local test of the
real game: fresh ignored workspace, hash/family/option rejection tests and a
bounded background start. It requires existing local game inputs/builds, never
touches the user's saves, and checks that the launcher creates only the native
game child. `--test-seconds` is for that bounded test only, not ordinary play.

The opt-in `BUZZ_PC_MENU_TEST=1` internal script opens the PC menu, changes
volume and cap, saves, exercises Escape close/reopen and the controller page,
then closes it. It never injects desktop input. Use it only in the isolated
integration fixture, with at least 17 seconds and `-ExpectMenu`. That fixture
also restarts its own game child to verify both preference files are read back.
The fixture also reads a fresh native launch plan after the menu changes tap
retention to 260 ms, and checks that its input/volume/frame cap match the game.
Do not leave this developer variable set for normal play: the internal script
intentionally changes settings. Synthetic tests cover repeat/edge navigation,
R1/D-pad mapping, held-input gates, transactional parsers, Unicode atomic
storage and optional host-overlay dispatch on Windows and Linux. The actual
overlay was tested in a bounded EU 1.02 D3D12 boot, not yet in every game/profile
or with physical controllers. It has not been ported to Vulkan.

For matched developer scene tests, `--test-skip-intro` is accepted only with
`--launch --test-seconds N` (1..300). It selects the existing startup-only video
skip in that child process, not in saved settings. The integration helper
exposes this as `-SkipIntro`. Normal play still starts with the original videos.

EU 1.02 integration tests must provide `-UpdateContent PATH_TO_Patched`. The
fixture copies only that update's twelve original EDAT/SDAT assets, never user
profiles, saves, or plaintext caches. Native validation rejects incomplete 1.02
content before play. The preparation wizard stages the same allowlist from the
user's update PKG or sibling `Patched` folder; 1.00 needs no update assets.
