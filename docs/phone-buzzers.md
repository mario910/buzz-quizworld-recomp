# Phone buzzers over local Wi-Fi

The game hosts an HTTP controller page. Players scan a QR code, choose a player
number and use five large buttons in their phone browser. No account, external
server, browser installation, Python or PWA is involved.

## Start playing

1. Connect the PC and phones to the same trusted Wi-Fi/LAN. A PC on Ethernet
   also works if phones can reach it through the same router.
2. Open **Escape / START → Phone buzzers** and enable **Host phone buzzers**.
   The native launcher's shared preferences can also enable hosting before
   launch. Hosting is off by default; the default port is **8073**.
3. Select the PC's LAN address above the QR code. VPNs and virtual adapters may
   have additional addresses. `127.0.0.1` works only on the PC itself.
4. If Windows Firewall prompts, allow this game's port on your private network.
   On Linux, open the port in your firewall if one is enabled (for example
   `sudo firewall-cmd --add-port=8073/tcp` or `sudo ufw allow 8073/tcp`); SteamOS
   has no firewall enabled by default.
   No firewall rules or router forwarding are added automatically. Guest Wi-Fi
   and client isolation can block phones from reaching the PC.
5. Scan the QR code. Enter a name and tap **Join game**: the phone takes the
   first free player slot (one that no other phone and no gamepad holds). To
   pick a specific player, tap its number first; tap it again to go back to
   automatic. The name identifies the phone in the host list; choose the quiz
   contestant's name separately in the game.
6. Close PC settings to resume gameplay. Use red for the buzzer and the answer
   colors on the TV. The handset order is red, blue, orange, green, yellow;
   each answer also has a number label.
7. Choose **Save settings** if hosting should start on subsequent launches.

Eight player slots are the default; four remain available in PC settings
(Controls → Buzzer slots, restart required). Gamepads default to **AUTO**: a
pad takes the first free player slot when its first buzzer button is pressed
and frees it when unplugged; Controls → Assign controllers to players also
offers fixed players or Unassigned. Phones, keyboard, XInput and physical
buzzers can be mixed. Inputs assigned to the same player are combined; give each
person their own player number. Phones work when the PC window lacks focus.
PC settings still suppress phone gameplay input while the menu is open.

## Sessions and host controls

- A phone keeps its slot while sleeping, switching apps or losing Wi-Fi. Reopen
  the same URL/browser to resume. Recovery across reloads requires local storage;
  private browsing or clearing site data may lose it.
- The QR code and the joined players are kept across game restarts in
  `runtime/phone-pairing.txt` (owner-only; room secret, player names and resume
  credentials, never input state). After a restart the same QR code works and a
  phone that already joined reconnects to its player by reopening the page.
- Brief failures reconnect automatically. After a longer failure, tap
  **Reconnect**. **New code / Remove all phones** ends all phone sessions and
  rotates the QR code; scan the new code.
- Reloading restores the same player and invalidates the previous tab's controls.
  Use one tab at a time for a saved session.
- **Joining: Locked** prevents new players while existing players can resume.
- Select a named player in PC settings to remove that phone and free its slot.
  **New code / Remove all phones** also rotates the QR pairing secret.
- **Leave player slot** on the phone frees it. A disconnected phone's slot stays
  reserved until it leaves, the host removes it or hosting ends.
- **Full screen** depends on browser support. Keep the page visible during play;
  if the phone auto-locks, wake it before answering.

Hosting is separate from the PSN offline setting. HTTP is for trusted local
networks: pairing and session credentials travel without TLS. Do not forward
the port to the Internet.

## Input and transport

All assets are embedded and served at fixed routes. Pairing uses a random
128-bit room secret in the URL fragment. Each player receives separate 128-bit
resume and active-control credentials. APIs accept POST forms with a custom
header, reject cross-origin browser requests and enforce a 2 KiB request limit.
Credentials are not accepted in GET query strings. HTTP workers, queued
connections and read/write timeouts are bounded.

Changes send immediately, with a heartbeat every 200 ms. The browser keeps one
request in flight and retains intervening short taps. Increasing sequence numbers
prevent replay and stale reordering. Uncertain presses after failed requests are
not retried; subsequent requests send neutral input. A 1.2-second lease, checked
every 100 ms, releases stuck buttons and retained pulses even if gameplay is
paused. Returning after expiry requires neutral input. Pointer cancellation,
blur and page hiding also release browser holds. The page displays round-trip
time; Wi-Fi/browser latency can still affect close buzzer races.

Remote input uses the existing router's eight remote sources and guest USB
reports. Removing a phone cannot release another provider's buttons. Menu
capture discards old taps and blocks held answers until release after closing.
Pairing is kept in `phone-pairing.txt`, not in game saves or profiles.

## Build and validation

Normal builds include the HTTP host and QR generator for both renderers. Their
MIT-licensed sources are vendored; no new dependency downloads are needed.
Existing PC settings remain compatible. New optional keys are:

```ini
phone_buzzers=0
phone_port=8073
```

The launcher CMake suite includes `phone-sessions` and `phone-server` without
game files. With the SDK present it also tests live runtime settings and the
real Win32 adapter using synthetic OS reads. Use the usual launcher build, then:

```powershell
ctest --test-dir build-launcher --output-on-failure
# Optional interactive test host: eight slots, port 18730; Enter stops it.
.\build-launcher\phone_server_test.exe --serve
```

The test host prints fresh pairing links and input edges. It does not launch a
game or inject desktop keys. Real iOS/Android hardware, physical Wi-Fi latency
and a complete quiz round still require manual testing.
