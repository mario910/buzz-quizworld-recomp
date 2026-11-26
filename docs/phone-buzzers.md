# Phone buzzers over local Wi-Fi

The game hosts an HTTP controller page. Players open its local address (QR code or a bookmark), get ready, join on
the game's player screen and use five large buttons in their phone browser. No account, external
server, browser installation, Python or PWA is involved.

## Start playing

1. Connect the PC and phones to the same trusted Wi-Fi/LAN. A PC on Ethernet
   also works if phones can reach it through the same router.
2. Open **Escape / START → Phone buzzers** and enable **Host phone buzzers**.
   `phone_buzzers=1` in `pc-settings.ini` enables hosting at startup. Hosting is off by default; the default port is **8073**. The switch
   is remembered at once: hosting that was on starts again with the next launch.
3. Select the PC's LAN address above the QR code. VPNs and virtual adapters may
   have additional addresses. `127.0.0.1` works only on the PC itself.
4. If Windows Firewall prompts, allow this game's port on your private network.
   On Linux, open the port in your firewall if one is enabled (for example
   `sudo firewall-cmd --add-port=8073/tcp` or `sudo ufw allow 8073/tcp`); SteamOS
   has no firewall enabled by default.
   No firewall rules or router forwarding are added automatically. Guest Wi-Fi
   and client isolation can block phones from reaching the PC.
5. Scan the QR code or open the address (for example `http://192.168.1.20:8073/`).
   No pairing key is needed, so a bookmark on the phone works every time the
   game runs with hosting on. Enter a name and tap **Get ready**: the phone
   waits without a player, so any number of phones can be ready. When the game
   shows its player screen, the phone's first press takes the first free player
   slot and joins it in the game; there is no player number to pick. With ten
   phones ready and eight slots, the first eight to press play and the others
   are told that all players are in. The name identifies the phone in the host
   list; choose the quiz contestant's name separately in the game.
6. Close PC settings to resume gameplay. Use red for the buzzer and the answer
   colors on the TV. The handset order is red, blue, orange, green, yellow;
   each answer also has a number label.
7. Bookmark the page on each phone for the next game night.

Eight player slots are the default; four remain available in PC settings
(Controls → Buzzer slots, restart required). Keyboard key sets, gamepads
(default **AUTO**) and phones are equal devices; none has a player until it
joins on the game's player screen. The game blinks the handset lights there
(all on ↔ joined players), which is how the PC knows joining is open and who
joined. A device's press on that screen takes the first free player and joins
it, in press order. Outside that screen an unjoined keyboard set or pad drives
P1 for the menus (for example the language choice) unless a joined device holds
P1; unjoined phones stay idle. When the screen closes, places that did not join
in the game are released; unplugging a pad also frees its place. Settings saved
from older builds with per-pad players load with those players dropped; all
pads share one button layout (Controls → Keyboard and pad bindings → Gamepads).
Phones, keyboard, XInput and physical
buzzers can be mixed. Inputs assigned to the same player are combined; give each
person their own player number. Phones work when the PC window lacks focus.
PC settings still suppress phone gameplay input while the menu is open.

## Sessions and host controls

- A phone keeps its slot while sleeping, switching apps or losing Wi-Fi. Reopen
  the same URL/browser to resume. Recovery across reloads requires local storage;
  private browsing or clearing site data may lose it.
- The joined players are kept across game restarts in
  `runtime/phone-pairing.txt` (owner-only; player names and resume credentials,
  never input state). After a restart a phone that already joined reconnects to
  its player by reopening the page.
- Brief failures reconnect automatically. After a longer failure, tap
  **Reconnect**. **Remove all phones** ends all phone sessions; phones can join
  again from the same address.
- Reloading restores the same player and invalidates the previous tab's controls.
  Use one tab at a time for a saved session.
- Anyone who can reach the address on the network can join. **Joining: Locked**
  prevents new players while existing players can resume.
- Select a named player in PC settings to remove that phone and free its slot.
- **Leave player slot** on the phone frees it. A disconnected phone's slot stays
  reserved until it leaves, the host removes it, hosting ends or a player
  screen closes without that player joining. When a player screen opens,
  places of phones that are not connected (for example kept from an earlier
  run) are released too, so new players fill places in order. A released phone
  goes back to waiting and can join on the next player screen.
- A player the game has joined but no device holds any more (a phone that
  tapped **Leave player slot** or lost its session) is taken over by the next
  phone, pad or keyboard set that presses a button, also in the middle of a
  game. A phone that left can therefore get its place back by getting ready
  and pressing again.
- **Full screen** depends on browser support. Keep the page visible during play;
  if the phone auto-locks, wake it before answering.

Hosting is separate from the PSN offline setting. HTTP is for trusted local
networks: joining is open to the local network and session credentials travel
without TLS. Do not forward
the port to the Internet.

## Input and transport

All assets are embedded and served at fixed routes. Joining needs no pairing
key (keys in older QR codes are ignored). Each player receives separate 128-bit
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

The host test suite (`tests/CMakeLists.txt`) includes `phone-sessions` and
`phone-server` without game files. With the SDK present it also tests live
runtime settings and the real Win32 adapter using synthetic OS reads:

```powershell
cmake -S tests -B build-tests -G Ninja; cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
# Optional interactive test host: eight slots, port 18730; Enter stops it.
.\build-tests\phone_server_test.exe --serve
```

The test host prints fresh pairing links and input edges. It does not launch a
game or inject desktop keys. Real iOS/Android hardware, physical Wi-Fi latency
and a complete quiz round still require manual testing.
