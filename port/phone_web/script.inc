R"BUZZ('use strict';
(() => {
  const $ = id => document.getElementById(id);
  const store = {get(k) {try {return localStorage.getItem(k);} catch {return null;}}, set(k,v) {try {localStorage.setItem(k,v);} catch {}}, remove(k) {try {localStorage.removeItem(k);} catch {}}};
  const key = 'buzz-phone-v1';
  let saved; try {saved = JSON.parse(store.get(key) || 'null');} catch {}
  let room = location.hash.slice(1) || (saved && saved.room) || '';
  if (!/^[a-f0-9]{32}$/.test(room)) room = '';
  if (saved && saved.room !== room) saved = null;
  if(room && !saved) {saved = {room}; store.set(key, JSON.stringify(saved));}
  // Pairing secret stays out of HTTP URLs, request logs and copied referrers.
  if (location.hash) history.replaceState(null, '', location.pathname);
  // A camera may reuse an existing tab when a host creates a new pairing code.
  window.addEventListener('hashchange', () => {if(location.hash) location.reload();});
  let session = null, selected = -1, held = 0, pending = 0, seq = 0;
  let busy = false, healthy = false, failedAt = 0, stopped = false, lobbyBusy = false, joining = false;
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
    if (session || lobbyBusy || joining || !room) return;
    lobbyBusy = true;
    try {
      const data = await api('lobby', {room});
      if (session || joining) return;
      connection(data.locked ? 'The host has locked joining' : 'Game found · Choose your player', data.locked ? '' : 'online');
      const seats = $('seats');
      if (selected >= data.players.length || (selected >= 0 && data.players[selected].reserved)) selected = -1;
      data.players.forEach((p, i) => {
        let b = seats.children[i];
        if(!b) {
          b = document.createElement('button'); b.type = 'button'; b.className = 'seat';
          const title = document.createElement('b'); title.textContent = 'P' + (i + 1);
          b.append(title, document.createElement('small'));
          b.addEventListener('click', () => {selected = i; [...seats.children].forEach((s,n) => s.setAttribute('aria-pressed',String(n === i))); $('join').disabled = false;}); seats.append(b);
        }
        b.disabled = p.reserved || data.locked;
        b.setAttribute('aria-pressed', String(selected === i));
        b.querySelector('small').textContent = p.reserved ? p.name + (p.online ? ' · online' : ' · away') : 'Available';
      });
      $('join').disabled = selected < 0 || data.locked;
    } catch(e) {connection(e.status === 403 ? 'Scan the current QR code on the PC' : 'Cannot reach the game. Check the PC and Wi-Fi.', 'offline'); $('join').disabled = true;}
    finally {lobbyBusy = false;}
  }
  async function join(resume) {
    if(joining) return; joining = true; $('join').disabled = true; $('retry').hidden = true; $('message').textContent = '';
    try {
      const data = await api('join', {room, resume:resume || '', player:Math.max(0,selected), name:$('name').value.trim()});
      clearHeld(); seq = 0; stopped = false; session = data;
      remember({room, resume:data.resume, name:data.name});
      $('identity').textContent = 'P' + (data.player + 1) + ' · ' + data.name;
      $('lobby').hidden = true; $('controller').hidden = false;
      document.body.classList.add('playing');
      connection('Connecting your buzzer…'); showButtons(false); send();
    } catch(e) {
      $('message').textContent = e.message;
      if (e.status === 401) {remember({room}); $('controller').hidden = true; $('lobby').hidden = false; document.body.classList.remove('playing');}
      else if (resume) {$('retry').hidden = false;}
    } finally {joining = false; if(!session) lobby();}
  }
  $('join-form').addEventListener('submit', e => {e.preventDefault(); if (selected >= 0) join('');});
  $('retry').addEventListener('click', () => {session = null; if(saved && saved.resume) join(saved.resume); else {$('controller').hidden = true; $('lobby').hidden = false; document.body.classList.remove('playing'); stopped = false; lobby();}});
  function pointerEnd(e) {
    if (!pointers.has(e.pointerId)) return;
    e.preventDefault(); pointers.delete(e.pointerId); held = 0; for(const bit of pointers.values()) held |= bit; renderHeld(); send();
  }
  buttons.forEach(b => {
    b.addEventListener('pointerdown', e => {
      e.preventDefault(); if (!healthy || !session || stopped || (e.pointerType === 'mouse' && e.button !== 0)) return;
      const bit = Number(b.dataset.bit); pointers.set(e.pointerId, bit); b.setPointerCapture(e.pointerId);
      pending |= bit & ~held; held |= bit; renderHeld();
      if(navigator.vibrate) navigator.vibrate(15); send();
    });
    b.addEventListener('pointerup', pointerEnd); b.addEventListener('pointercancel', pointerEnd); b.addEventListener('lostpointercapture', pointerEnd);
    b.addEventListener('contextmenu', e => e.preventDefault());
    // Keyboard and assistive-technology activation produces an isolated tap.
    b.addEventListener('click', e => {if (e.detail === 0 && healthy) {pending |= Number(b.dataset.bit); send();}});
  });
  function suspend() {clearHeld(); showButtons(false); send();}
  document.addEventListener('visibilitychange', () => {suspend(); if (!document.hidden && session && !stopped) {connection('Reconnecting…'); send();}});
  window.addEventListener('blur', suspend);
  window.addEventListener('focus', () => {if(session && !stopped) send();});
  window.addEventListener('pagehide', suspend);
  window.addEventListener('offline', () => {suspend(); connection('Offline · Reconnect to the PC’s Wi-Fi', 'offline');});
  window.addEventListener('online', () => {if(session && !stopped) send(); else lobby();});
  $('leave').addEventListener('click', async () => {
    if(!session) return;
    suspend(); const current = session;
    try {await api('leave', {control:current.control});} catch(e) {if(e.status !== 401) {$('message').textContent = 'Could not leave. Reconnect or ask the host to release your slot.'; return;}}
    session = null; remember({room}); stopped = false; $('controller').hidden = true; $('lobby').hidden = false; document.body.classList.remove('playing'); $('message').textContent = ''; lobby();
  });
  $('screen').addEventListener('click', async () => {
    try {if(document.fullscreenElement) await document.exitFullscreen(); else await document.documentElement.requestFullscreen();}
    catch {$('message').textContent = 'Full screen is unavailable in this browser. You can keep playing here.';}
  });
  setInterval(() => {if (session && !document.hidden) send();},200);
  setInterval(lobby,2500);
  if (!room) {connection('Scan the QR code in PC Settings → Phone buzzers', 'offline'); $('join').disabled = true;}
  else if(saved && saved.resume) {$('name').value = saved.name || ''; join(saved.resume);}
  else lobby();
})();)BUZZ"
