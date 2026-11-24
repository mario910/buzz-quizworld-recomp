R"BUZZ('use strict';
(() => {
  const $ = id => document.getElementById(id);
  const store = {get(k) {try {return localStorage.getItem(k);} catch {return null;}}, set(k,v) {try {localStorage.setItem(k,v);} catch {}}, remove(k) {try {localStorage.removeItem(k);} catch {}}};
  const key = 'buzz-phone-v1';
  let saved; try {saved = JSON.parse(store.get(key) || 'null');} catch {}
  // No pairing key: the plain local address (QR code or a bookmark) is enough.
  // Older QR codes carried a key in the fragment; drop it so bookmarks stay clean.
  if (location.hash) history.replaceState(null, '', location.pathname);
  let session = null, held = 0, pending = 0, seq = 0;
  let busy = false, healthy = false, failedAt = 0, stopped = false, lobbyBusy = false, joining = false;
  // Ready but not yet a player: any number of phones can wait here. The first
  // button press takes the next free player place (first come, first served).
  let waiting = false;
  const pointers = new Map(), buttons = [...document.querySelectorAll('.buzzer')];
  function connection(text, state) {$('connection').textContent = text; $('connection').className = state || '';}
  function remember(value) {saved = value; if(value) store.set(key, JSON.stringify(value)); else store.remove(key);}
  async function api(path, data, timeout = 1600) {
    const abort = new AbortController(), timer = setTimeout(() => abort.abort(), timeout);
    try {
      const response = await fetch('/api/' + path, {method:'POST', headers:{'X-Buzz-Controller':'1'}, body:new URLSearchParams(data), cache:'no-store', signal:abort.signal});
      const result = await response.json();
      if (!response.ok) {const e = new Error(result.error || 'Request failed'); e.status = response.status; throw e;}
      return result;
    } finally {clearTimeout(timer);}
  }
  function showButtons(active) {healthy = active; $('buttons').classList.toggle('inactive', !active); buttons.forEach(b => b.disabled = !active);}
  function renderHeld() {buttons.forEach(b => b.classList.toggle('pressed', !!(held & Number(b.dataset.bit))));}
  function clearHeld() {held = 0; pending = 0; pointers.clear(); renderHeld();}
  function endSession(text) {
    clearHeld(); session = null; stopped = true; showButtons(false);
    connection(text, 'offline'); $('message').textContent = 'Your place can be restored if the host has not removed it.';
    $('retry').hidden = false;
  }
  async function send() {
    if (!session || busy || stopped) return;
    // One request in flight. Edges occurring while it is in flight are retained,
    // even when a finger lifts before the previous acknowledgement arrives.
    busy = true;
    const current = session, pressed = pending, state = held, sequence = ++seq;
    pending = 0;
    const start = performance.now();
    try {
      await api('input', {control:current.control,seq:sequence,held:state,pressed}, 1000);
      if (session !== current) return;
      failedAt = 0;
      connection('Connected · Player ' + (current.player + 1), 'online');
      $('latency').textContent = Math.round(performance.now() - start) + ' ms round trip';
      if (!document.hidden) showButtons(true);
    } catch(e) {
      if (session !== current) return;
      clearHeld(); showButtons(false);
      // Released by the host, or the place did not join on the game's player
      // screen: wait again, ready to join on the next player screen.
      if (e.status === 401 && current.name) {
        $('name').value = current.name; getReady();
        $('message').textContent = 'Your player place was released. Press a button on the game\'s next player screen to join.';
        return;
      }
      if (e.status === 401 || e.status === 409) {endSession('Session ended or opened in another tab'); return;}
      // Never retry an unacknowledged press: its delivery is uncertain. The next
      // packet sends a neutral state; old sequence numbers cannot replay answers.
      connection('Reconnecting… Keep this page open.', 'offline');
      if (!failedAt) failedAt = Date.now();
      if (Date.now() - failedAt > 10000) endSession('Cannot reach the game');
    } finally {
      busy = false;
      if (session && !stopped && (held !== state || pending)) queueMicrotask(send);
    }
  }
  async function lobby() {
    if (session || waiting || lobbyBusy || joining) return;
    lobbyBusy = true;
    try {
      const data = await api('lobby', {});
      if (session || waiting || joining) return;
      // No player picking: the first press after getting ready takes a free place.
      const full = data.players.every(p => p.reserved);
      connection(data.locked ? 'The host has locked joining' : full ? 'All players are in · You can still get ready and wait' : 'Game found · Enter your name and get ready', data.locked ? '' : 'online');
      $('join').disabled = data.locked;
    } catch(e) {connection('Cannot reach the game. Check that it is running with phone buzzers on, and the Wi-Fi.', 'offline'); $('join').disabled = true;}
    finally {lobbyBusy = false;}
  }
  // claim: called by the first button press while waiting. That press (and a
  // still-held button) is delivered once the player place is granted.
  async function join(resume, claim) {
    if(joining) return; joining = true; $('join').disabled = true; $('retry').hidden = true; $('message').textContent = '';
    try {
      const data = await api('join', {resume:resume || '', player:'auto', name:$('name').value.trim()});
      if (!claim) clearHeld();
      seq = 0; stopped = false; waiting = false; session = data;
      remember({resume:data.resume, name:data.name});
      $('identity').textContent = 'P' + (data.player + 1) + ' · ' + data.name;
      $('lobby').hidden = true; $('controller').hidden = false;
      document.body.classList.add('playing');
      connection('Connecting your buzzer…'); if (!claim) showButtons(false); send();
    } catch(e) {
      if (claim) {
        clearHeld();
        $('message').textContent = e.status === 409 ? 'All players are already in. Wait until someone leaves, then press again.' : e.message;
        return;
      }
      $('message').textContent = e.message;
      if (e.status === 401) {remember({name:$('name').value.trim()}); $('controller').hidden = true; $('lobby').hidden = false; document.body.classList.remove('playing');}
      else if (resume) {$('retry').hidden = false;}
    } finally {joining = false; if(!session && !waiting) lobby();}
  }
  function getReady() {
    const name = $('name').value.trim();
    waiting = true; session = null; stopped = false; clearHeld(); remember({name});
    $('identity').textContent = name;
    $('lobby').hidden = true; $('controller').hidden = false; document.body.classList.add('playing'); $('message').textContent = '';
    connection('Ready · Press any button on the game\'s player screen to join', 'online');
    $('latency').textContent = 'Not a player yet'; showButtons(true);
  }
  $('join-form').addEventListener('submit', e => {e.preventDefault(); getReady();});
  $('retry').addEventListener('click', () => {session = null; if(saved && saved.resume) join(saved.resume); else {$('controller').hidden = true; $('lobby').hidden = false; document.body.classList.remove('playing'); stopped = false; lobby();}});
  function pointerEnd(e) {
    if (!pointers.has(e.pointerId)) return;
    e.preventDefault(); pointers.delete(e.pointerId); held = 0; for(const bit of pointers.values()) held |= bit; renderHeld(); send();
  }
  buttons.forEach(b => {
    b.addEventListener('pointerdown', e => {
      e.preventDefault(); if (!healthy || (!session && !waiting) || stopped || (e.pointerType === 'mouse' && e.button !== 0)) return;
      const bit = Number(b.dataset.bit); pointers.set(e.pointerId, bit); b.setPointerCapture(e.pointerId);
      pending |= bit & ~held; held |= bit; renderHeld();
      if(navigator.vibrate) navigator.vibrate(15);
      if (waiting && !session) join('', true); else send();
    });
    b.addEventListener('pointerup', pointerEnd); b.addEventListener('pointercancel', pointerEnd); b.addEventListener('lostpointercapture', pointerEnd);
    b.addEventListener('contextmenu', e => e.preventDefault());
    // Keyboard and assistive-technology activation produces an isolated tap.
    b.addEventListener('click', e => {if (e.detail === 0 && healthy) {pending |= Number(b.dataset.bit); if (waiting && !session) join('', true); else send();}});
  });
  function suspend() {clearHeld(); showButtons(false); send();}
  document.addEventListener('visibilitychange', () => {suspend(); if (!document.hidden && session && !stopped) {connection('Reconnecting…'); send();} else if (!document.hidden && waiting) showButtons(true);});
  window.addEventListener('blur', suspend);
  window.addEventListener('focus', () => {if(session && !stopped) send(); else if (waiting) showButtons(true);});
  window.addEventListener('pagehide', suspend);
  window.addEventListener('offline', () => {suspend(); connection('Offline · Reconnect to the PC’s Wi-Fi', 'offline');});
  window.addEventListener('online', () => {if(session && !stopped) send(); else lobby();});
  $('leave').addEventListener('click', async () => {
    if(!session) {
      if (waiting) {waiting = false; clearHeld(); $('controller').hidden = true; $('lobby').hidden = false; document.body.classList.remove('playing'); $('message').textContent = ''; lobby();}
      return;
    }
    suspend(); const current = session;
    try {await api('leave', {control:current.control});} catch(e) {if(e.status !== 401) {$('message').textContent = 'Could not leave. Reconnect or ask the host to release your slot.'; return;}}
    session = null; remember({name:current.name}); stopped = false; $('controller').hidden = true; $('lobby').hidden = false; document.body.classList.remove('playing'); $('message').textContent = ''; lobby();
  });
  $('screen').addEventListener('click', async () => {
    try {if(document.fullscreenElement) await document.exitFullscreen(); else await document.documentElement.requestFullscreen();}
    catch {$('message').textContent = 'Full screen is unavailable in this browser. You can keep playing here.';}
  });
  setInterval(() => {if (session && !document.hidden) send();},200);
  setInterval(lobby,2500);
  if (saved && saved.name) $('name').value = saved.name;
  if (saved && saved.resume) join(saved.resume);
  else lobby();
})();)BUZZ"
