# PC settings menu, display and textures

The game is started by BuzzSetup (**Play Buzz!**), `Launch-Buzz.cmd` /
`Launch-Buzz.sh` in a prepared copy, or `BuzzSetup-x86_64.AppImage --play` on
Linux. Everything a player configures lives in the game's own PC menu; there is
no separate launcher. Developers start a build from `scripts/Build-Buzz.ps1`
with `scripts/Launch-Buzz.ps1` or `scripts/run-windows.ps1 -Seconds 0 -MenuBootstrap`.

Game controllers (XInput, DualShock 4, DualSense) are always on. The default
buzzer layout matches RPCS3 and PCSX2: **L1/LB or R1/RB = red, Triangle/Y = blue,
Square/X = orange, Circle/B = green, Cross/A = yellow** (the handset's colours
from top to bottom). START is not a buzzer action. Saved mappings that still
equal an earlier default are moved to this layout automatically; customised
mappings are kept. Controls > the gamepad layout's **L1/R1 preset**, then Save,
resets a customised mapping. Eight logical handsets do not imply eight physical
gamepads or force eight contestants into the selected mode.

## Settings menu

The in-game settings menu has five tabs. **General, Display, Graphics, Controls,
and Phones** share a fixed footer with **Save settings, Resume game, and Quit**.
On supported builds, **Graphics** includes live switches for studio floor
reflections, bloom and extra audience lighting alongside the texture options. Controls has
button hints on the answers (on/off); advanced input options, bindings (keyboard
sets and the one gamepad layout) and paired phones use short subpages with a
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

Per-build `runtime/pc-settings.ini` and `runtime/input-settings.ini` are the
authoritative audio/video/network and controller settings. Malformed settings
are preserved for repair; defaults are not silently written over them. The
existing `runtime/dev_hdd0`/`dev_hdd1` save locations are kept per build.
Settings files may contain private local paths; review them before sharing.

## In-game overlay

The game draws the PC settings overlay itself (D3D12 and Vulkan). Open/close it with
**Escape** or **START**/Options on a controller. L1/LB or R1/RB is the red buzzer;
Triangle/Square/Circle/Cross (Y/X/B/A) are blue/orange/green/yellow. Custom legacy mappings
that use START as a buzzer keep that binding; use Escape for the menu until
you select the new preset. The keyboard uses arrows, Enter/Space and Backspace;
controllers use D-pad, A/R1 to select and B to go back. Mouse buttons also work.

The purple/navy overlay has volume, a 30 / 60 / Uncapped frame limit, borderless fullscreen,
restart-only offline mode, the gamepad layout and its L1/R1 preset, tap
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
in the menu:

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
  of three ways: tick **Download the optional Real-ESRGAN texture upscaler**
  in BuzzSetup (an extra, resumable preparation stage for that build), run
  `scripts\Install-Upscaler.ps1` for a developer checkout (`-Runtime` picks
  another game folder, `-Archive` installs offline from a local copy), or
  unpack the release yourself into `runtime/textures/upscaler/`. The first
  two download one pinned release (v0.2.5.0, 45 MB from GitHub) only when
  asked, check its size and SHA-256 before extracting, keep it in
  `runtime/setup/downloads/` for later preparations, and install only the
  executable, its OpenMP DLLs and the models, replacing an older install
  whole. It runs at below-normal priority on the GPU, in
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
  diagnosis. They are set in the in-game Graphics tab or the settings file.


A version-1 `pc-settings.ini` loads unchanged with the display and texture
pack defaults, and default settings are still written as the exact version-1
file. If only the version-2 keys of a file are invalid, the game starts with
the default display and texture pack settings and logs why. The in-game
**Phone buzzers** page displays its QR code and player sessions; see
[phone controllers](phone-buzzers.md). Unsupported renderer requests are
rejected, not silently replaced with another backend.

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
independent of the virtual USB receiver emulation. See [physical buzzer scope and tests](physical-buzzers.md).
Linux host HID remains unverified. Vulkan is the normal Windows renderer and
supports the native PC menu, host pause and batched/asynchronous draw path.
See [renderer architecture](renderer.md) for submission and resource lifetime.
Linux title integration, complete gameplay coverage and renderer-feature parity
remain unqualified.

## Tests

`tests/CMakeLists.txt` builds the synthetic host tests (settings parser,
controller mapping, buzzer HID, phone server/sessions, host controls/overlay,
game paths, PC runtime) without game files; CI runs them on Windows, Linux and
macOS (`.github/workflows/port-tests.yml`):

```sh
cmake -S tests -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

On Windows, `buzzer-host-menu` compiles the real input adapter with synthetic
OS reads. It checks START capture before guest input, held-button release after
closing, RB/D-pad mappings, preserved legacy START mappings, live disable/enable,
keyboard focus loss and XInput disconnect/reconnect. It sends no desktop events
and does not poll a physical pad. The portable menu repeat/release logic and
shared settings tests also run on Linux. These tests complement, not replace,
eventual hardware testing.

The opt-in `BUZZ_PC_MENU_TEST=1` internal script opens the PC menu, changes
volume and cap, saves, exercises Escape close/reopen and the controller page,
then closes it. It never injects desktop input. Do not leave this developer
variable set for normal play: the internal script intentionally changes
settings. The overlay has bounded D3D12 and Windows Vulkan integration coverage,
but not every game/profile or physical controller.
