# Legacy Python PC launcher (developer tool)

This page describes the earlier Python/Tk implementation retained for reference.
The normal player entry point is now the [native C++ launcher](pc-launcher.md).
Use `scripts/Launch-Buzz.ps1 -PythonLauncher` explicitly for the old UI.

## Starting it

In the `buzz-quizworld-recomp` repository folder, double-click **Launch-Buzz.cmd**, or run:

```powershell
.\scripts\Launch-Buzz.ps1
```

The launcher uses the local Python from `toolenv` and the standard Tkinter.
The launcher, preparation wizard and application-provided messages are in
English. Native file dialogs follow the Windows display language. The game
window is titled **Buzz! Quiz World** (with the existing FPS statistics).
Only legacy default labels such as "Gracz 1" are translated to "Player 1";
custom player names and input mappings are preserved.
It downloads no game files. The separate **Import ISO / prepare** wizard can
generate/build from your ISO/dump and firmware, using your local RPCS3 as an
import helper. A completed prepared build is selected automatically. Python,
the C++ build tools and Windows SDK are still required; this is not a standalone
installer or a packaged launcher EXE.

## Three tabs

**Start and game files**

- Select the game folder, `dev_flash` and the decrypted `EBOOT.elf`.
- Checks the dump structure, title ID, font folder and exact EBOOT SHA-256.
  Profiles cover EU 1.00 (BCES00644/645/647), US 1.00 (BCUS98209) and BCES00647 1.02. Other EU language
  assets remain experimental. Changing executable paths selects an existing
  matching prepared build, not a fixed default version. The build's ELF/EXE
  identity and version-specific runtime options are recorded in a manifest.
- Offline mode or the original network-services path, plus a separate option
  to hide the store. The second mode only restores the game's existing
  behaviour; it does not start or configure any server, PSN, DNS or RPCS3.
- Starting in the background is the default. Untick it if the game window
  should get focus right away; with a background start, click the game window
  before using the keyboard.

**Players and mapping**

- 4 or 8 available slots/handsets. This does NOT force that number of
  contestants; single-player mode and contestants are still chosen in the game.
- Player names are launcher labels only; they do not change game profiles.
- The five keys can be typed or recorded one after another with
  "Record 5 keys". Recording only happens in a dialog the user opened. Esc
  cancels; to assign Esc, type it. Maps in the file keep the order red,
  yellow, green, orange, blue.
- Up to four XInput slots, custom five-button maps and assignment to players
  1–8. "Unassigned" disables that slot in the native adapter.
- Keys shared between players or incomplete mappings raise a warning before
  the start. A key repeated within one buzzer is an error. Keyboard and pad
  can drive the same player at once.

**Button test**

- Live view of each player's five buttons without starting the game.
- Assigning a pad: choose a player, click "Press START on the pad" and press
  START. Moving a pad from another player asks for confirmation.
- This shows raw input; it does not emulate the extended USB pulse.
- Keyboard events belong to this window only: no global hook, no SendInput, no
  automatic window activation and no keys sent to the desktop. Pads are polled
  only while the launcher is in the foreground; on focus loss the input masks
  are cleared and the test pauses.
- XInput is limited to four physical controllers. Eight logical players can
  use a mix of inputs; there is no eight-pad backend or Android app yet.
  Keyboard rollover limits are a hardware matter.

The tabs scroll, and the Save/Start buttons stay in the bottom bar on smaller
screens. The launcher currently targets Windows; it is not yet a verified
SteamOS/Steam Deck launcher.

## Where settings go

On first start the paths are read from an existing `buzz-paths.json`, if any.
Manual maps from `buzz-input.ini` and `BUZZ_KEYS` are not imported
automatically; enter them in the form to use them from the launcher. The
command-line files stay unchanged.

- `runtime/launcher/settings.json` — UI settings, written on Save or on a
  confirmed game start. Writes are atomic.
- `runtime/launcher/sessions/<time-id>/paths.json` and `input.ini` — a
  separate, immutable set per start. The launcher passes their full paths to
  `scripts/run-windows.ps1` via `-PathsConfig` and `-InputConfig`.
- `runtime/launcher/last-start.log` — output of the PowerShell start.
- `runtime/launcher/launcher-error.log` — an unhandled error of the `.pyw`
  entry point, if the interface fails to start.
- Full game logs still go to `logs/`.

All of `runtime` is ignored by Git and may contain private paths; review it
before attaching it to a public report. The launcher does not change the dump,
the EBOOT, saves, profiles or the main `buzz-paths.json`/`buzz-input.ini`. A
damaged settings file is never overwritten silently: writing a new set needs
confirmation and keeps a copy of the old JSON.

The GUI mapping takes precedence over inherited `BUZZ_KEYS`/`BUZZ_PULSE_MS`.
An automatic `BUZZ_SCRIPT` is removed from the environment of a normal GUI
start, so an old test script cannot trigger unexpected menu actions. Other
performance/SPURS settings are inherited; the launcher does not change
optimizations or worker counts. File validation and the start run off the UI
thread. Editing the form during validation cancels the start of the old set
instead of overwriting the new edits. Closing the launcher does not close a
running game.

## Tests

```powershell
.\tests\scripts\test-pc-launcher.ps1
.\tests\scripts\test-launcher.ps1
.\tests\scripts\test-buzzer.ps1
.\scripts\Launch-Buzz.ps1 -Smoke
```

The model and headless-UI tests cover validation, saving/loading and
protection of the previous file on a failed write, all encodable virtual keys,
TITLE_ID/SFO bounds, distinct per-start snapshots, literal paths with spaces,
Unicode and shell characters, the preview, no pad polling while the window is
inactive, and pad assignment by synthetic input. The real C++ parser confirms
every map in the generated INI for 4 and 8 slots, including an unassigned pad.
The start bridge is also checked under Windows PowerShell 5.1 with UTF-8 and
Unicode paths (with process creation replaced by a fixture), and a native
adapter test reads a real config file from a Unicode path without opening a
window or starting the game.

Not yet verified by hand: the full visual check, physical pads, and starting
a round through the GUI button.

The launcher code lives in `launcher/`; the command-line integration is the
optional `-PathsConfig` of `scripts/run-windows.ps1`/`scripts/buzz-paths.ps1`, and the only
input-adapter change of this work is `gamepad.N=NONE`.
