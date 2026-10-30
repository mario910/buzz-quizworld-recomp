# Physical Buzz receivers (Windows, hardware-unverified)

The Windows game now has an experimental, default-enabled host HID provider
for original Sony receivers: wired `054c:0002` and wireless `054c:1000`.
It is **hardware-unverified**: a working receiver is not available locally.
Synthetic tests are not a claim of a tested physical match or successful radio
pairing. Linux host HID integration, receiver LEDs and other vendor/product IDs
are not implemented by this provider yet.

## Player experience

Plug in a receiver normally; no special/exclusive driver replacement is used.
The game registers joystick/gamepad RawInput collections for its own window and
accepts input only from the two supported IDs with valid button capabilities.
Wireless handsets must already be paired with their receiver.

Receiver 1 maps to players 1-4. Receiver 2 maps to players 5-8 and requires the
existing eight-buzzer setting and a game restart. It does not silently change
the virtual USB topology or the game's chosen contestant count. Initial slots
are assigned in device-interface order, not USB enumeration order; unplugging
one receiver never renumbers the other. Changing USB ports between launches
can change interface order. Persistent per-device reassignment is not provided.

Physical, keyboard and XInput inputs are independent providers merged into the
same logical players. Assign XInput pads to other player slots if mixing devices
without intending them to share a handset. Removing a receiver releases only
its own held buttons and retained taps.

The PC menu's Controllers page shows the usable receiver count and the
hardware-unverified label. Physical buzzers have no START: open the PC menu
with Escape, then use blue/orange for up/down, yellow/green to adjust and red
to confirm. Return/Back rows are reachable using the handset. No delayed button
chord is added to gameplay. Menu capture and held-input release gates apply to
physical receivers as they do to keyboard and XInput.

For diagnostic A/B only, set `BUZZ_PHYSICAL_BUZZERS=0` before launch. It disables
registration, not the keyboard or XInput providers. Physical support is independent
of `BUZZ_PC_OVERLAY=0` or an overlay initialization failure. Ordinary play needs
no environment variables, Python, PowerShell, DLL downloads or HID polling tool.

## Implementation and safety

`port/buzzer_hid_win32.cpp` owns RawInput registration and device lifetimes.
It caches the OS's preparsed capabilities at attachment and uses HidP button
usages, not hard-coded byte offsets from the game's virtual USB reports.
`port/buzzer_hid.h` contains the portable usage/layout/report contract. All 20
button usages must be accounted for; ambiguous, incomplete or excessively large
capabilities are rejected. Buttons are ordered red/yellow/green/orange/blue
within each five-button handset.

RawInput registration does not include keyboard/mouse collections, use
NOLEGACY, capture other windows, replace drivers or write feature/output reports.
Background reports track releases; the router suppresses unfocused gameplay
and requires held buttons to be released before refocusing can answer again.
Unknown controllers' report bodies are not read. Device paths/serials are not
logged or saved. Enumeration happens on attachment/hotplug, not every frame;
there is no disk polling or per-report logging.

Packet sizes, counts, report IDs and usage-list sizes are bounded and checked
before decoding. Unnumbered reports with/without the zero ID prefix are
normalized to the HidP buffer size. Numbered reports require the exact declared
length; shorter variable-size variants are not guessed. Separate report IDs
retain independent button state. Unrelated status report IDs are ignored, not
interpreted as button releases. Malformed input releases/suspends that receiver
until every button-report group has reported neutral input.

WM_INPUT is still passed to DefWindowProc for OS cleanup. Shutdown unregisters
this window's joystick/gamepad subscriptions and releases held inputs. HID
failure leaves keyboard/XInput and the rest of the game available.

## Verification

- `buzzer-hid` (Windows and Linux): all 20 usages, ID allowlist, report prefixes,
  length checks, descriptor coverage, duplicate/unknown usages, multiple report
  IDs, state isolation and transactional failures.
- `buzzer-hid-win32` (Windows): the real RawInput adapter compiled with synthetic
  Windows/HidP reads; two receivers, sorted slots, unknown-device filtering,
  batched reports, invalid-capability rejection, malformed-packet suspension,
  neutral recovery, hotplug and teardown. No actual device registration occurs.
- `buzzer-host-menu` (Windows): the real input router with synthetic OS reads;
  physical/keyboard/XInput isolation, receiver-2 topology masking, player 8,
  menu capture, background suppression, release gates and disconnection.

The local game integration test checks registration during a bounded D3D12 boot
and menu operation with no working receiver. It cannot prove real descriptor,
dongle, radio, handset timing, LED or OS-driver compatibility. Keep the
hardware-unverified label until working hardware has been tested.

## Primary references

Device IDs and button numbering were checked against the Linux project's
[device ID definitions](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/heads/master/drivers/hid/hid-ids.h)
and [Buzz mapping documentation](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-sony.c).
These are protocol facts; no Linux driver code or report descriptor is copied
into the port.

Windows integration follows Microsoft's [RawInput lifecycle](https://learn.microsoft.com/en-us/windows/win32/inputdev/using-raw-input),
[HID report size/ID contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidpi/ns-hidpi-_hidp_caps)
and [button-usage API](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidpi/nf-hidpi-hidp_getusages).
