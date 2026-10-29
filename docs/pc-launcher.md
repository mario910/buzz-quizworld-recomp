# PC launcher

## Starting it

In the `quizworld-recomp` folder, double-click **Launch-Buzz.cmd**, or run:

```powershell
.\Launch-Buzz.ps1
```

The launcher uses the local Python from `toolenv` and the standard Tkinter.
It installs no extra framework, downloads no game files and does not build the
port. You need an already built Windows EXE plus your own game dump and
firmware, exactly as for the command-line start. It is not yet a standalone
installer or a packaged launcher EXE.

## Three tabs

**Start and game files**

- Select the game folder, `dev_flash` and the decrypted `EBOOT.elf`.
- Checks the dump structure, TITLE_ID=BCES00647 in PARAM.SFO, the font folder,
  the SHA-256 of the update 1.02 EBOOT and the presence of the native EXE.
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
  `run-windows.ps1` via `-PathsConfig` and `-InputConfig`.
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
.\test-pc-launcher.ps1
.\test-launcher.ps1
.\test-buzzer.ps1
.\Launch-Buzz.ps1 -Smoke
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
optional `-PathsConfig` of `run-windows.ps1`/`buzz-paths.ps1`, and the only
input-adapter change of this work is `gamepad.N=NONE`.
