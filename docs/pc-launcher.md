# Native PC launcher

For a game prepared with BuzzPrepare, open `dist/BuzzQuizWorldRecomp.exe`
and use the in-game PC menu. The launcher described here is for developer builds.


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
as working controllers. The standalone Linux game uses its own SDL input adapter
and stores its bindings through the in-game menu.

## Settings and process isolation

The in-game settings menu has five tabs: **General, Display, Textures, Controls,
and Phones**. Each has at most five primary options, with **Save settings,
Resume game, and Quit** kept in a separate fixed footer. Controller assignments,
advanced input options, bindings, and paired phones use short subpages with a
Back option. Settings are applied and saved exactly as before.

Open with **Escape / controller START**. With a physical buzzer or any player's
configured keyboard buzzer, **blue/orange** move up/down, **yellow/green**
decrease/increase, and **red** selects. The menu initially highlights the tab
strip: yellow/green switch tabs, red (or orange) enters the options. Blue above
the first option returns to the tabs. Save and Resume are reachable using just
these five buttons; no gameplay chord is reserved to open the menu. Arrow keys,
D-pad, A/R1, B/Backspace, and mouse navigation remain available. Physical USB
buzzer navigation is covered with synthetic input tests, not verified hardware.

The in-game **Escape / controller START** menu includes **Performance overlay**
(off by default). It toggles a non-interactive top-right HUD immediately, showing
game FPS, game draw calls per frame, internal render resolution and output
(swapchain) resolution in physical pixels. FPS is averaged over half-second
intervals; paused-menu redraws do not count as game frames. Draw calls exclude
clears, final scaling/compositing and the host UI. Choose **Save settings** to
remember the toggle (`performance_overlay=1` in version-2 `pc-settings.ini`).
When disabled, the HUD does not start ImGui or sample clocks during gameplay.

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

The purple/navy overlay has volume, a 30 / 60 / Uncapped frame limit, borderless fullscreen,
restart-only offline mode, controller assignments, the R1/D-pad preset, tap
retention and a restart-only 4/8-buzzer count. It **does not pause the quiz**.
Guest buzzer input is suppressed while open and held buttons must be released
after closing/remapping before they can answer a question. Quit requires a
separate confirmation. See `port/pc_overlay.cpp` and `port/pc_menu_model.h`.

The overlay shares the renderer's command list and draws after the guest
frame has been scaled into the window, at window resolution, before
presentation. Its resources are lazy-created, not a second window or a
separate renderer. Its layout scales with the window height (1x at ~900 px,
up to 3x), so it stays readable at 1440p and 4K. `BUZZ_PC_OVERLAY=0` disables it
for diagnostic A/B checks (and restores the SDK's old Escape-to-close behavior).
The runtime now connects volume to the shared audio mixer and borderless/fullscreen
and the 0/30/60 FPS cap to D3D12 host controls. To seed preferences without the menu,
copy `config/pc-settings.example.ini` to the selected build's `runtime/pc-settings.ini`
or pass an explicit `BUZZ_PC_SETTINGS` path. It is loaded once, never polled.
The desktop game defaults to 30 FPS and applies a saved cap at startup.
Save settings writes `runtime/pc-settings.ini` and `runtime/input-settings.ini`
for the selected build. Volume/cap/display mode/window size and controller
mappings apply live; network policy and internal resolution remain restart-only.

### Display size and internal resolution

These are two separate settings, in code (`window_width`/`window_height` vs
`render_scale`, `ps3_host_set_window_size` vs `ps3_host_set_render_scale`) and in
both UIs:

- **Display mode / window size** is the window client area and swapchain.
  Windowed sizes are Default (1280x720), 1280x720, 1920x1080, 2560x1440,
  3840x2160 or a custom 640x360-7680x7680, in physical pixels (the window is
  per-monitor DPI aware; `RSX_PIXEL_WINDOW=0` restores the old DPI-unaware
  window). A size larger than the monitor's work area is shrunk to fit,
  keeping its proportions. Borderless fullscreen covers the monitor at the
  desktop resolution. Both apply live. The frame is scaled into the window
  with its 16:9 aspect ratio kept, black bars filling the rest; resizing,
  DPI changes, minimizing and restoring recreate or keep the swapchain as
  needed, and a minimized window simply presents nothing.
- **Internal resolution** is the size the game renders at: 100% (1280x720),
  150% (1920x1080), 200% (2560x1440) or 300% (3840x2160). Every guest colour
  and depth surface is created that much larger; viewports, scissors, clear
  and copy rectangles are scaled where they reach the GPU, and fragment
  programs reading the window position get it back in guest pixels. The game
  itself still sees its own 1280x720 surfaces: pixels read back to guest
  memory are sampled down (nearest), pixels the CPU writes are sampled up.
  It is read once at startup, so changes are marked `[restart]` in the menu.
  D3D12 keeps 100% when the diagnostic `RSX_PACK_AA_COLOR=1` is set.
- **Upscaling filter** decides how the finished frame is scaled into the
  window: bilinear, or AMD FidelityFX Super Resolution 1 (EASU edge-adaptive
  upscale followed by RCAS sharpening, `ps3recomp/libs/video/rsx_fsr1.h`).
  FSR 1 is used only when the output rectangle is larger than the internal
  resolution, e.g. 100-150% into a 1440p or 4K window; downscaling a higher
  internal resolution stays bilinear. Sharpness 0-100 maps to RCAS 2-0 stops.
  Both apply live. If the shaders cannot be built (Vulkan needs the HLSL
  translator) presentation stays bilinear. Temporal upscalers (DLSS, FSR 2/3,
  XeSS) are not offered: the game provides no motion vectors or jitter.

### Texture packs (upscaled textures)

`runtime/textures/` beside `pc-settings.ini` holds texture packs
(`ps3recomp/libs/video/rsx_texture_pack.h`, both renderers):

- **Dump textures** (`texture_dump=1`) writes every new 2D colour texture the
  game uploads to `textures/dump/` as a PNG named after its contents:
  `<64-bit hash>_<width>x<height>_<format>.png`. The name is the same in every
  run and on both renderers. Dumping costs a PNG encode per new texture, so it
  is for making packs, not for playing; an address whose contents keep
  changing (video, rendered surfaces) stops being dumped after four images,
  and a run stops after 16384 files.
- **Use texture packs** (`texture_replace=1`, the default) loads any PNG under
  `textures/replace/` (subfolders allowed) in place of the texture with the
  same name, at the PNG's own size; a full mip chain is built from it. Upscale
  the dump offline (Real-ESRGAN or similar, or by hand), keep the file names,
  and copy the results to `replace/`. An empty or missing folder costs nothing.
- Only images are eligible: B8, G8B8, the ARGB/packed 16-bit colour formats
  and DXT1/23/45, not cube maps, volumes, normal maps, depth or float
  textures. The game's channel remap still applies, so a B8 font atlas stays
  one channel (grey PNG) and G8B8 two (grey + alpha PNG); DXT is replaced by
  uncompressed RGBA. A PNG whose aspect ratio differs from the original is
  used stretched, with a log line.
- **Upscale textures automatically** (`texture_auto_upscale=1`) does the
  above while you play: it turns on dumping, and a background thread feeds
  each new dump through Real-ESRGAN (4x, `realesrgan-x4plus`) into
  `textures/replace/auto/`. The renderer rescans `replace/` as files arrive,
  so an upscale applies the next time the game loads that texture (usually
  the next screen or round); upscales are kept, so later runs start with
  them. Install the upscaler (Real-ESRGAN's `realesrgan-ncnn-vulkan`) one
  of three ways: tick **Download the Real-ESRGAN texture upscaler** in
  `Prepare-Buzz` (an extra, resumable preparation stage for that build), run
  `scripts\Install-Upscaler.ps1` for a developer checkout (`-Runtime` picks
  another game folder, `-Archive` installs offline from a local copy), or
  unpack the release yourself into `runtime/textures/upscaler/`. The first
  two download one pinned release (v0.2.5.0, 45 MB from GitHub) only when
  asked, check its size and SHA-256 before extracting, keep it in
  `runtime/setup/downloads/` for later preparations, and install only the
  executable, its OpenMP DLLs and the models, replacing an older install
  whole. The launcher shows whether it is found. It runs at below-normal priority on the GPU, in
  batches of up to 32 textures (smallest first, at most 4 megapixels of
  input), so the model loads once per batch rather than once per texture
  (one 128x128 texture 2.1 s, ten 3.5 s). It skips textures whose upscale would exceed 2048
  pixels (so 4x covers textures up to 512: ~445 MB of video memory for the
  menu's set, against ~2 GB at 4096; `BUZZ_TEXTURE_UPSCALER_MAX_SIDE`
  raises the limit up to 8192) and any texture with a hand-made replacement, which always wins
  over `replace/auto/`. Delete `replace/auto/` to redo the upscales.
  `BUZZ_TEXTURE_UPSCALER` (executable path), `BUZZ_TEXTURE_UPSCALER_MODEL`
  and `BUZZ_TEXTURE_UPSCALER_SCALE` (2-4, matching the model) override the
  defaults. After five failed batches in a row (no Vulkan GPU, missing model) it
  stops for the run and logs why. A running upscale ends with the game, on
  every exit: a Windows job object, and on Linux the child's parent-death
  signal plus an exit handler (other POSIX systems have the exit handler
  only, so a crash there can leave one batch to finish).
- All are read at startup and are restart-only. `RSX_TEXPACK_DIR`,
  `RSX_TEXPACK_DUMP=0/1` and `RSX_TEXPACK_REPLACE=0/1` override them for
  diagnosis. They are set in the launcher's Game tab and the settings file,
  not the in-game menu.

A version-1 `pc-settings.ini` loads unchanged with the display and texture
pack defaults, and default settings are still written as the exact version-1
file. If only the version-2 keys of a file are invalid, the game starts with
the default display and texture pack settings and logs why; the launcher still reports
the file for repair. The native launcher reads and writes these
same files, including custom controller mappings and offline policy. Its Game
tab also exposes volume, frame lock, display mode, window size, internal
resolution, upscaling filter and optional local phone-buzzer hosting. The in-game **Phone buzzers** page displays its QR code and player
sessions; see [phone controllers](phone-buzzers.md). Legacy launcher JSON remains
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
The 30/60 FPS limiter paces the guest PPU thread at flip reset on both renderers;
it leaves the vblank cadence and audio rate unchanged. Switching the cap is live,
but selecting 60 does not guarantee that the workload reaches 60 FPS. Earlier
isolated 1.02 startup tests observed 29.53-30 FPS with the 30 cap and 60-63 FPS
uncapped after warm-up. Those historical startup results are not a heavy-scene
benchmark or validation of round timing above 30 FPS. Borderless mode
deliberately does not change guest rendering resolution.

The Windows game has a default-enabled original Buzz HID provider, labelled
**hardware-unverified** until somebody can test a working receiver. It is
independent of the virtual USB receiver emulation and the launcher's SDL input
preview. See [physical buzzer scope and tests](physical-buzzers.md). The launcher
Button test still covers keyboard/XInput, not physical Buzz receivers.
Linux host HID remains unverified. Vulkan is the normal Windows renderer and
supports the native PC menu, host pause and batched/asynchronous draw path.
See [renderer architecture](renderer.md) for submission and resource lifetime.
Linux title integration, complete gameplay coverage and renderer-feature parity
remain unqualified.

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
overlay has bounded D3D12 and Windows Vulkan integration coverage, but not
every game/profile or physical controller. Public CI builds the launcher and
synthetic tests without game inputs; see [CI scope](ci.md), including the
macOS launcher-only bootstrap.

For matched developer scene tests, `--test-skip-intro` is accepted only with
`--launch --test-seconds N` (1..300). It selects the existing startup-only video
skip in that child process, not in saved settings. The integration helper
exposes this as `-SkipIntro`. Normal play still starts with the original videos.

EU 1.02 integration tests must provide `-UpdateContent PATH_TO_Patched`. The
fixture copies only that update's twelve original EDAT/SDAT assets, never user
profiles, saves, or plaintext caches. Native validation rejects incomplete 1.02
content before play. The preparation wizard stages the same allowlist from the
user's update PKG or sibling `Patched` folder; 1.00 needs no update assets.
