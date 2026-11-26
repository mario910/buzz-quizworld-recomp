# Frontend offline and extensible buzzer input (28 October 2026)

These changes are separate from Claude's geometry/performance work. They touch
only hand-written title code, tests, the launcher and documentation. No SDK,
lifted PPU/SPU files, game dump, save/profile or performance defaults are changed.
The existing CMake cache is preserved, including the selected Stage F geometry TU.

## Offline boot, not PSN emulation

Default `BUZZ_OFFLINE=1` overrides the title's Lua C binding
`Network.HasUserNpAccount` (BCES00647 1.02 PC 0x0012B760) to return false.
The original `InitNetworkConnection.doInitNetworkConnection` then takes its
normal no-account path and sets its own initialization-complete flags. We do
not return a fake online state, accept a EULA, forge authentication or suppress
all error dialogs. Online services remain offline; the game naturally disables
their menu entries. This option deliberately also prevents using online/MyBuzz
services until disabled.

Default `BUZZ_HIDE_PSN_STORE=1` independently replaces the Lua predicate
`Menu.Callbacks.ShouldAllowAccessToDLC` with a function returning false **after**
`NaviSliderMenu/Menu_Callbacks` is included. `Menu.Root.BuildMenu` uses this
predicate to decide whether to add the Store item. Existing installed DLC and
its files are not removed. Other menu entries and dialogs are not filtered.

The short original port-owned Lua snippet is compiled and run using the title's
own Lua APIs on the same Lua thread. The original PPU return context is kept;
the Lua stack is restored by logical index, including on a compile/run error.
It is not a disk script replacement. The executable-specific string/OPD pair guards the account binding. The disc
1.00 generator also checks the Lua API instruction signatures against the
selected EU or US executable before generating its host adapter.

Restore independently, before launch:

```powershell
$env:BUZZ_OFFLINE='0'          # Original account/login behavior, e.g. custom server tests
$env:BUZZ_HIDE_PSN_STORE='0'   # Original Store visibility policy
.\scripts\run-windows.ps1 -Seconds 0 -MenuBootstrap
```

Unset variables use the new offline/hidden defaults. Values must be exactly
0 or 1. There is no real PSN service, matchmaking, authentication or new HTTP
listener in this change.

## Configure input

Controls are edited and tested in the game's PC menu (Escape / START →
Controls); see [PC settings](pc-settings.md).
Manual INI configuration below remains supported independently.

The D3D12 game also has an Escape/START PC menu for live controller settings,
audio/video preferences and persistent saves. See [PC settings](pc-settings.md)
for the current validation scope and per-build settings behavior.
Original physical Buzz HID now has an experimental Windows provider, separate
from guest USB emulation; see [hardware-unverified scope](physical-buzzers.md).

The file is deliberately opt-in, so existing automated scripts keep their
keyboard behavior. From the project directory:

```powershell
Copy-Item -LiteralPath .\config\buzz-input.example.ini -Destination .\buzz-input.ini
# Edit buzz-input.ini, then:
.\scripts\run-windows.ps1 -Seconds 0 -MenuBootstrap -InputConfig .\buzz-input.ini
```

`buzz-input.ini` is ignored by Git. The launcher resolves a relative path before
changing the game's working directory and restores its caller's env afterward.
Alternatively set `BUZZ_INPUT_CONFIG` to an absolute file path. Missing/invalid
configuration exits explicitly with a diagnostic, rather than silently assigning
buttons to the wrong person. Unknown and duplicate settings are rejected.

All five-button mappings use **RED, YELLOW, GREEN, ORANGE, BLUE** order, not
the order of answers on the screen:

```ini
players=4
pulse_ms=250
keyboard.1=Space,4,3,2,1
keyboard.2=Q,T,R,E,W
keyboard.3=A,G,F,D,S
keyboard.4=Z,B,V,C,X
gamepads=1
background_gamepads=0
gamepad=LB+RB,A,B,X,Y
```

Game controller support is always on; there is no switch to turn it off.
`gamepads=0` in older files is accepted and ignored, and saved files always
write `gamepads=1`. `gamepad=` is the one button layout every controller shares;
controllers have no fixed player and join on the game's player screen. Older
files with per-slot `gamepad.N=player,buttons` lines still load: the lowest
slot's buttons become the shared layout and the players (and `NONE`) are dropped.

Keyboard tokens: A–Z, 0–9, Space, Enter, Tab, Esc, Backspace, arrows,
LShift/RShift, LCtrl/RCtrl, F1–F24, numeric VK codes such as 0x60, or NONE.
Single digits in the file mean the printed keyboard key; legacy `BUZZ_KEYS`
env variables still mean numeric VK values. Duplicate non-NONE buttons within
one handset are rejected; sharing a key between players is allowed deliberately.
Ordinary keyboards can have hardware rollover/ghosting limits for simultaneous
players; the software cannot remove them.

Existing `BUZZ_KEYS`, `BUZZ_KEYS_2` … `_4`, `BUZZ_PULSE_MS` and `BUZZ_SCRIPT`
remain supported. Explicit env mappings override file values. Without a config
the launcher keeps its legacy 250 ms pulse default; with a config it leaves
pulse_ms to the file unless BUZZ_PULSE_MS was explicitly supplied. Script masks still address the first
four players; their historical format has not been silently widened.

## Controllers

The Windows provider uses XInput 1.4 (always enabled), loaded
from the system directory, not the working directory. Up to four controllers
are read at once (XInput itself supports only four); eight logical players do
not mean eight physical pads. Every controller uses the same `gamepad=` layout
and takes a player when it joins on the game's player screen. [Microsoft XInput documentation](https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput).

PlayStation DualShock 4 (054C:05C4/09CC) and DualSense / DualSense Edge
(054C:0CE6/0DF2) are not XInput devices, so XInput alone never sees them. The
Windows game reads them through the same RawInput gamepad registration as the
Buzz! receivers, over USB or Bluetooth, without sending any report to the pad.
Each Sony pad takes the lowest of the four gamepad slots that no XInput pad
uses (like SDL's player index) and behaves like an XInput pad there: Cross/Circle/
Square/Triangle = A/B/X/Y, L1/R1 = LB/RB, Options = START, Share/Create = BACK,
L3/R3 = LS/RS. If an XInput pad later appears on that slot, the Sony pad moves to
the next free one. The log shows `[buzz-hid] DualShock 4 controller 1 connected
(USB)` and `[input] Sony controller 1 -> gamepad slot 0`.
Tools such as DS4Windows that add a virtual Xbox pad without hiding the real
one would give two pads for one controller; hide the real pad there, or start
the game with `BUZZ_SONY_PADS=0`. The PS4 USB wireless adapter (054C:0BA0) is
not supported yet. Buzz! receiver detection (054C:0002/1000) is unchanged.

Mappings support START, BACK, A, B, X, Y, LB, RB, LB+RB (either bumper), LS, RS and D-pad directions.
The default matches RPCS3 and PCSX2: red is either bumper, L1/LB or R1/RB (`LB+RB`), and the four colours follow
the handset from top to bottom: Triangle/Y blue, Square/X orange, Circle/B
green, Cross/A yellow. START is reserved for the PC menu. Saved mappings that
still equal an earlier default (`START,Y,A,B,X`, `RB,LEFT,DOWN,RIGHT,UP` or `LB,A,B,X,Y`)
were never customised and are switched to this default when loaded; any other
saved mapping is kept as it is.
Controller state is polled at most every 8 ms; disconnected slots are retried
once a second. Disconnect releases that provider's held state and pulses.
By default controllers affect the game only while its window has focus.
`background_gamepads=1` is an explicit opt-in for controller input in the
background; it never activates the window or injects keyboard events.

Keyboard input likewise belongs to the game's window; focus loss releases held
keys but preserves a short real press until its bounded pulse expires. No
global keyboard hook or SendInput is installed. These are buzzer inputs, not a
second mapping into the game's DualShock port.

## Eight players / two receivers

`players=8` exposes **two separate 054C:1000 USB receivers**, each with a normal
20-bit/four-handset report. It does not invent a nonstandard 40-bit USB packet.
Receiver 1 carries P1–P4, receiver 2 P5–P8, with separate descriptors, serial,
location, private driver data and pipe ownership. Default is still one receiver.
The remaining four keyboard mappings start unbound; configure keyboard.5–8.
All game-mode player-count restrictions remain the game's own.

The platform-independent router supports eight players and independent provider
states. Short down/up presses survive USB polling; a release from one provider
does not clear another provider's held button. The USB layer only consumes a
snapshot and packs the appropriate receiver's twenty bits.

## Browser provider and additional transports

The router does not know about Windows keys, XInput, USB, sockets or guest RAM.
A provider submits five-bit button state for an assigned player and
must release its own provider state on disconnect/timeout. Reserved provider
slots start at `remote0`; the host adapter must serialize router access.
The [phone-buzzer provider](phone-buzzers.md) supplies paired local browser
sessions with player ownership, heartbeat expiry and bounded messages. It is
optional; ordinary input does not require a network listener. Other transports
can use the same router without changing the guest USB protocol or game logic.

## Tests and current validation

```powershell
.\tests\scripts\test-buzzer.ps1
.\tests\scripts\test-frontend.ps1
```

- Existing exhaustive 20-bit USB report and script tests: PASS.
- Pure router/config tests: all 8×5 buttons across two reports, provider
  isolation, tap lifetime, focus/release, input validation and pad masks: PASS.
- Synthetic real USB adapter/guest-driver harness: separate two-device
  descriptors/location/private data/pipes, P1 and P8 interrupt reports,
  second-device serial, closing one pipe without breaking the other: PASS.
- Frontend options/version guard and Lua boolean ABI: PASS. Synthetic Lua API
  tests cover unchanged PPU return context/active context and Lua stack on
  success, compilation failure and runtime failure. Existing no-FMV boot tests
  also pass.
- Incremental build passes without changing CMake performance options.
- Four-player background integration `logs/20261028-141145.stderr.log`:
  receiver attach succeeds; real no-account path is invoked; store snippet
  runs successfully in real Lua (status 0). Root menu construction logs show
  PlayOffline/MyBuzz/PlayOnline/Extras **without Store**; the script then enters
  the game-mode menu. Boot-smoke 8/8 PASS. Native render-target snapshots confirm
  a functioning game-mode menu, not a desktop screenshot.

Physical controller presses and a full eight-person round still require a human
test. The eight-player background integration
`logs/20261028-141440.stderr.log` confirms probe=0/attach=0 for **both** receivers,
the real offline path, successful store policy and 8/8 boot-smoke checks.
`analysis/frontend-input-background-02/frame_001.bmp` shows the root menu with
Store absent; online/MyBuzz entries remain visible but correctly disabled.
Do not equate receiver attachment or synthetic packets with a completed
eight-person multiplayer round. 
These are functionality checks, not a new FPS benchmark.

Unattended integration uses the existing `-Background` window mode and internal
BUZZ_SCRIPT input, with optional renderer captures. It never activates the
window or sends desktop keys, respecting the user's concurrent RDP work.
