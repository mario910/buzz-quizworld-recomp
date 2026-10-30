R"BUZZ(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
  <meta name="theme-color" content="#111026">
  <title>Buzz! Phone Buzzer</title>
  <link rel="icon" href="/favicon.svg" type="image/svg+xml">
  <link rel="stylesheet" href="/style.css">
  <script src="/controller.js" defer></script>
</head>
<body>
<main>
  <header><span class="wordmark">BUZZ<span>!</span></span><span class="tag">PHONE BUZZER</span></header>
  <div id="connection" role="status" aria-live="polite">Connecting to the game…</div>
  <section id="lobby" aria-labelledby="join-title">
    <h1 id="join-title">Your phone. Your buzzer.</h1>
    <p>Choose your player number to match the game on the TV. Keep this page open while you play.</p>
    <form id="join-form">
      <label for="name">Your name</label><input id="name" maxlength="24" autocomplete="nickname" placeholder="Player name" required>
      <fieldset><legend>Player number</legend><div id="seats"></div></fieldset>
      <button id="join" class="primary" disabled>Join game</button>
    </form>
    <p class="hint">Already joined? Reopening this page restores your player. A sleeping phone keeps its place.</p>
  </section>
  <section id="controller" hidden aria-label="Your buzzer">
    <div class="identity"><h1 id="identity"></h1><button id="screen" type="button">Full screen</button></div>
    <p class="hint">Look at the TV for questions. Press a color to answer.</p>
    <div id="buttons" aria-label="Buzzer buttons">
      <button class="buzzer red" data-bit="1" aria-label="Red buzzer"><span>BUZZ!</span><small>RED</small></button>
      <button class="buzzer blue" data-bit="16" aria-label="Blue answer"><b>1</b><span>BLUE</span></button>
      <button class="buzzer orange" data-bit="8" aria-label="Orange answer"><b>2</b><span>ORANGE</span></button>
      <button class="buzzer green" data-bit="4" aria-label="Green answer"><b>3</b><span>GREEN</span></button>
      <button class="buzzer yellow" data-bit="2" aria-label="Yellow answer"><b>4</b><span>YELLOW</span></button>
    </div>
    <footer><span id="latency">Waiting for connection</span><button id="leave" type="button">Leave player slot</button></footer>
  </section>
  <p id="message" role="alert"></p>
  <button id="retry" class="primary" type="button" hidden>Reconnect</button>
  <p class="network-help">Same Wi-Fi as the PC · No account or app needed<br>If the game closes, scan its new QR code next time.</p>
</main>
</body></html>)BUZZ"
