#pragma once
// =====================================================================
// dashboard_html.h : the dashboard web page served at "/" (gateway only)
//
// Generated from dashboard.html. To change the page, edit the HTML between
// the R"rawliteral( and )rawliteral" markers. Keep the page free of the
// text )rawliteral" or the string ends early.
// =====================================================================

const char DASHBOARD_HTML[] = R"rawliteral(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>IoT Dashboard | EE4216 Gateway</title>
<style>
/* Palette and card style follow the Lab 2 reference dashboard. No external fonts or libraries,
   so the page works with no Internet (the gateway serves it over its own Wi-Fi). */
:root{--ink:#152f35;--muted:#677d82;--line:#dce5e5;--teal:#087f77;--paper:#f3f6f5;--deep:#153f43;
--ok:#2fbf7a;--warn:#e8a63b;--bad:#e5604d;--idle:#8fa5a8}
*{box-sizing:border-box}
body{margin:0;background:var(--paper);color:var(--ink);font:14px/1.5 system-ui,-apple-system,Segoe UI,sans-serif}
button,input{font:inherit}
button{cursor:pointer;border:0}
button:disabled{opacity:.5;cursor:wait}
button:focus-visible,input:focus-visible{outline:3px solid #e8a63b;outline-offset:3px}

.topbar{min-height:70px;background:#fff;border-bottom:1px solid var(--line);display:flex;align-items:center;
justify-content:space-between;flex-wrap:wrap;gap:10px 18px;padding:10px max(24px,calc((100vw - 1280px)/2))}
.brand{display:flex;align-items:center;gap:12px;font-size:17px;font-weight:750;letter-spacing:-.4px}
.mark{width:35px;height:35px;display:grid;place-items:center;border-radius:10px;background:var(--ink);color:#9ae2cf;font-size:22px}
.chips{display:flex;gap:8px;flex-wrap:wrap}
.chip{display:flex;align-items:center;gap:7px;font-size:12px;font-weight:600;border:1px solid var(--line);
border-radius:30px;padding:6px 12px;color:var(--muted);background:#fff}
.dot{width:9px;height:9px;border-radius:50%;background:var(--idle);flex-shrink:0}
.s-ok .dot{background:var(--ok)}.s-warn .dot{background:var(--warn)}.s-bad .dot{background:var(--bad)}

main{max-width:1336px;margin:auto;padding:30px 24px 22px}
.eyebrow{font-size:11px;text-transform:uppercase;letter-spacing:2px;font-weight:750;color:var(--teal);margin:0 0 7px}
h1{font-size:32px;line-height:1.2;letter-spacing:-1.2px;margin:0 0 8px}
h2{margin:0;font-size:21px;letter-spacing:-.5px}
.intro{color:var(--muted);margin:0 0 22px}
.banner{display:none;background:#fdecea;color:#8a2b1f;border:1px solid #f3c1ba;border-radius:10px;padding:10px 14px;margin:0 0 18px}
.banner.show{display:block}

.cloud{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin:0 0 24px}
.stat{background:#fff;border:1px solid var(--line);border-radius:12px;padding:12px 14px}
.stat span{display:block;font-size:11px;color:var(--muted)}
.stat b{font-size:16px;font-variant-numeric:tabular-nums}

.layout{display:grid;grid-template-columns:1fr 350px;gap:24px;align-items:start}
.nodes{display:grid;gap:20px;transition:opacity .2s}
.nodes.lost{opacity:.6}
.node{background:var(--deep);color:#fff;border-radius:20px;padding:24px}
.node .eyebrow{color:#9bcac4}
.node-head{display:flex;justify-content:space-between;align-items:flex-start;gap:12px}
.desc{color:#a9c8c5;font-size:12px;margin:2px 0 0}
.badge{display:flex;align-items:center;gap:7px;font-size:12px;font-weight:650;padding:5px 11px;border-radius:20px;
border:1px solid #4e7779;white-space:nowrap;color:#e3f1ef}
.age{color:#c4dedb;font-size:12px;margin:14px 0 16px;min-height:18px}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}
.tile{background:#1c4c51;border-radius:14px;padding:14px 16px;color:#7fd7c7}
.tile.dim{opacity:.55}
.lbl{display:block;font-size:11px;letter-spacing:.8px;text-transform:uppercase;color:#a9c8c5}
.val{display:flex;align-items:baseline;gap:6px;color:#fff}
.val output{font-size:40px;font-weight:650;letter-spacing:-1.5px;font-variant-numeric:tabular-nums;line-height:1.2}
.unit{font-size:16px;color:#a6c8c5}
.spark{display:block;width:100%;height:28px;margin-top:6px}
.counters{border-top:1px solid #41676b;margin-top:18px;padding-top:12px;color:#a9c8c5;font-size:11px}

.card{background:#fff;border:1px solid var(--line);border-radius:20px;padding:24px}
.hint{color:var(--muted);font-size:12px;margin:2px 0 0}
.preview{height:56px;border-radius:12px;border:1px solid var(--line);background:#000;margin:16px 0 6px}
.payload{font:11px ui-monospace,Consolas,monospace;color:var(--muted);margin:0 0 16px}
.colour-line{display:flex;align-items:center;gap:10px;margin:0 0 16px}
.colour-line input{width:43px;height:43px;border:1px solid #d4dede;border-radius:12px;background:#fff;padding:3px;cursor:pointer;flex-shrink:0}
input[type=color]::-webkit-color-swatch-wrapper{padding:0}
input[type=color]::-webkit-color-swatch{border:0;border-radius:8px}
input[type=color]::-moz-color-swatch{border:0;border-radius:8px}
.colour-line label{display:block;font-size:11px;color:var(--muted)}
.hex{font:11px ui-monospace,Consolas,monospace}
.range-label{display:flex;justify-content:space-between;font-size:11px}
input[type=range]{width:100%;accent-color:var(--teal);margin:10px 0 16px;cursor:pointer}
.presets{display:flex;gap:8px;flex-wrap:wrap;margin:0 0 20px}
.sw{width:30px;height:30px;border-radius:50%;border:2px solid #fff;box-shadow:0 0 0 1px #cddada;padding:0}
.btns{display:grid;grid-template-columns:2fr 1fr;gap:10px}
.btns button{padding:12px;border-radius:10px;font-weight:700}
.apply{background:var(--teal);color:#fff}
.off{background:#edf5f3;color:#16645e}
.status{font-size:12px;color:var(--muted);margin:14px 0 0;min-height:36px;overflow-wrap:anywhere}
.status.ok{color:#17784a}.status.bad{color:#b3392a}
.sub{font-size:11px;color:var(--muted);margin:6px 0 0;min-height:16px}

.footer{margin-top:26px;border-top:1px solid var(--line);padding-top:14px;color:var(--muted);font-size:11px}
@media(max-width:900px){.layout{grid-template-columns:1fr}}
@media(max-width:560px){main{padding:22px 16px}h1{font-size:27px}.val output{font-size:34px}}

/* RGB automation status */
.automation-info {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
  margin: 12px 0 16px;
  font-size: 12px;
  color: var(--muted);
}

.mode-badge {
  padding: 5px 11px;
  border-radius: 20px;
  background: #edf0f0;
  color: #526569;
  font-size: 11px;
  font-weight: 750;
}

.mode-badge.auto {
  background: #d9f5e7;
  color: #167647;
}

.mode-badge.manual {
  background: #e3eafb;
  color: #345cb2;
}

.mode-badge.offline {
  background: #fdecea;
  color: #a63c2e;
}

.automation-info .trigger-warning {
  color: #a63c2e;
  font-weight: 650;
}

</style></head><body>

<header class="topbar">
  <div class="brand"><span class="mark" aria-hidden="true">&#9638;</span>IoT Dashboard</div>
  <div class="chips">
    <span class="chip" id="chipGw"><i class="dot"></i><span>Gateway: connecting</span></span>
    <span class="chip" id="chipNet"><i class="dot"></i><span>Internet: unknown</span></span>
    <span class="chip" id="chipMqtt"><i class="dot"></i><span>Cloud: unknown</span></span>
  </div>
</header>

<main>
  <p class="eyebrow">Sense &amp; control</p>
  <h1>Your sensor network, live.</h1>
  <p class="intro">Readings refresh automatically every 2 seconds. The colour of each badge shows how fresh a node's data is.</p>
  <div class="banner" id="banner" role="alert"></div>

  <div class="cloud" aria-label="Gateway and cloud status">
    <div class="stat"><span>Last cloud publish</span><b id="stPub">-</b></div>
    <div class="stat"><span>Publishes</span><b id="stCnt">-</b></div>
    <div class="stat"><span>MQTT connections</span><b id="stConn">-</b></div>
    <div class="stat"><span>Gateway uptime</span><b id="stUp">-</b></div>
  </div>

  <div class="layout">
    <section id="nodes" class="nodes" aria-label="Sensor nodes"><p class="intro">Loading...</p></section>

    <section class="card" aria-labelledby="ledTitle">
      <p class="eyebrow">Actuator</p>
      <h2 id="ledTitle">RGB LED stick</h2>
      <div class="automation-info">
        <span id="modeBadge" class="mode-badge">
          UNKNOWN
        </span>
        <span id="triggerText">
          Waiting for Node 1...
        </span>
      </div>
      <p class="hint">Node 1 &middot; all 8 pixels are set together</p>
      <div class="preview" id="preview" aria-hidden="true"></div>
      <p class="payload" id="payload"></p>
      <div class="colour-line">
        <input type="color" id="colour" value="#ff0000" aria-label="LED colour">
        <div><label for="colour">Colour</label><span class="hex" id="hex">#FF0000</span></div>
      </div>
      <div class="range-label"><label for="level">Brightness</label><span><output id="levelv">100</output> %</span></div>
      <input type="range" id="level" min="0" max="100" value="100">
      <div class="presets" id="presets" aria-label="Colour presets"></div>
      <div class="btns">
        <button class="apply" id="apply">Apply colour</button>
        <button class="off" id="off">Turn off</button>
      </div>
      <p class="status" id="ledStatus" role="status">Ready.</p>
      <p class="sub" id="ledHint"></p>
      <p class="sub" id="gwCmd"></p>
    </section>
  </div>

  <footer class="footer">Served by the ESP32-S3 gateway &middot; works with no Internet &middot; shows the last values received</footer>
</main>

<script>
// ===================== Settings (the only lines you should need to edit) =====================
// Prefix for /api/status. Leave '' when this page is served by the gateway. If you open the file
// from your laptop while testing, set it to 'http://192.168.4.1' (the gateway allows this via CORS).
const API_BASE = '';
const POLL_MS = 2000;            // how often the page asks the gateway for new data
const POLL_TIMEOUT_MS = 3000;    // give up on one status request after this
// Where LED commands go. Today: straight to node1 (needs the CORS line added in node1.ino).
// Once the gateway forwards commands itself, change this to API_BASE + '/api/cmd'.
const CMD_URL = 'http://192.168.4.11/api/cmd';
const CMD_TIMEOUT_MS = 4000;     // a little longer than the gateway's 3 s ack timeout
const LED_NODE = 'node1';        // the node that owns the LED stick
const HISTORY_LEN = 40;          // points kept for each mini chart

// Friendly names. Anything not listed here still displays, using the raw key from the gateway.
const NODE_INFO = {
  node1: {name: 'Node 1', desc: 'Ultrasonic distance + RGB LED stick'},
  node2: {name: 'Node 2', desc: 'DHT22 temperature / humidity + light sensor'}
};
const LABELS = {distance_cm: 'Distance', temp_c: 'Temperature', humidity_pct: 'Humidity', light_pct: 'Light level'};
const STATE_UI = {online: ['s-ok', 'Online'], stale: ['s-warn', 'Stale'], offline: ['s-bad', 'Offline'], never_seen: ['', 'Never seen']};
const PRESETS = [['Red', '#ff0000'], ['Green', '#00ff00'], ['Blue', '#0000ff'],
                 ['White', '#ffffff'], ['Amber', '#ffa000'], ['Purple', '#8000ff']];

// ===================== Small helpers =====================
const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));

// Seconds -> "12 s", "3 min", "1 h 5 min".
function dur(t) {
  t = Math.max(0, Math.floor(t));
  const h = Math.floor(t / 3600), m = Math.floor(t % 3600 / 60);
  return h ? h + ' h ' + m + ' min' : m ? m + ' min' : t + ' s';
}
// AbortController-based timeout, so a dead gateway cannot leave a request hanging forever.
function timeoutSignal(ms) { const c = new AbortController(); setTimeout(() => c.abort(), ms); return c.signal; }
function setChip(id, cls, text, tip) {
  const el = $(id);
  el.className = 'chip ' + cls;
  el.querySelector('span').textContent = text;
  el.title = tip || '';
}

// ===================== Reading history (for the mini charts) =====================
const history = {};   // "node1.distance_cm" -> [numbers]
const lastSeq = {};   // node id -> last seq already recorded, so repeated polls don't add duplicate points

function recordHistory(s) {
  for (const [id, n] of Object.entries(s.nodes)) {
    if (n.seq == null || n.seq === lastSeq[id]) continue;
    lastSeq[id] = n.seq;
    for (const [k, v] of Object.entries(n.readings)) {
      if (typeof v !== 'number') continue;
      const a = history[id + '.' + k] = history[id + '.' + k] || [];
      a.push(v);
      if (a.length > HISTORY_LEN) a.shift();
    }
  }
}
function spark(key) {
  const a = history[key];
  if (!a || a.length < 2) return '<svg class="spark" aria-hidden="true"></svg>';
  const min = Math.min(...a), span = (Math.max(...a) - min) || 1, w = 120, h = 28;
  const pts = a.map((v, i) => (i * w / (a.length - 1)).toFixed(1) + ',' + (h - 2 - (v - min) / span * (h - 4)).toFixed(1)).join(' ');
  return '<svg class="spark" viewBox="0 0 ' + w + ' ' + h + '" preserveAspectRatio="none" aria-hidden="true">' +
         '<polyline points="' + pts + '" fill="none" stroke="currentColor" stroke-width="1.5"/></svg>';
}

// ===================== Rendering /api/status =====================
function ageLine(n) {
  switch (n.state) {
    case 'online':  return 'Updated ' + dur(n.age_s) + ' ago';
    case 'stale':   return 'No new data for ' + dur(n.age_s) + '. Values may be out of date.';
    case 'offline': return 'Node offline (last heard ' + dur(n.age_s) + ' ago). Showing last known values.';
    default:        return 'Waiting for the first reading.';
  }
}

function renderNodes(s) {
  $('nodes').innerHTML = Object.entries(s.nodes).map(([id, n]) => {
    const info = NODE_INFO[id] || {name: id, desc: ''};
    const ui = STATE_UI[n.state] || ['', n.state];
    const dim = n.state !== 'online' ? ' dim' : '';
    // One tile per key in "units": the gateway decides what exists, so a new sensor needs no change here.
    const tiles = Object.keys(n.units).map(k => {
      const v = n.readings[k];
      const text = typeof v === 'number' ? v.toFixed(1) : '-';
      return '<div class="tile' + dim + '"><span class="lbl">' + esc(LABELS[k] || k) + '</span>' +
             '<div class="val"><output>' + text + '</output><span class="unit">' + esc(n.units[k]) + '</span></div>' +
             spark(id + '.' + k) + '</div>';
    }).join('');
    return '<article class="node"><div class="node-head"><div><p class="eyebrow">' + esc(id) + '</p>' +
           '<h2>' + esc(info.name) + '</h2><p class="desc">' + esc(info.desc) + '</p></div>' +
           '<span class="badge ' + ui[0] + '"><i class="dot"></i>' + esc(ui[1]) + '</span></div>' +
           '<p class="age">' + esc(ageLine(n)) + '</p><div class="tiles">' + tiles + '</div>' +
           '<p class="counters">Posts OK ' + n.posts_ok + ' &middot; rejected ' + n.posts_rejected +
           ' &middot; lost ' + n.seq_gaps + ' &middot; restarts ' + n.reboots + '</p></article>';
  }).join('');
}

function renderCloud(s) {
  const c = s.cloud;
  const mq = {connected: ['s-ok', 'Cloud connected'], disconnected: ['s-warn', 'Broker unreachable'],
              waiting_for_wifi: ['s-bad', 'No Internet']}[c.mqtt] || ['', String(c.mqtt)];
  setChip('chipNet', c.wifi === 'online' ? 's-ok' : 's-bad', c.wifi === 'online' ? 'Internet online' : 'Internet offline');
  setChip('chipMqtt', mq[0], mq[1], c.mqtt_detail);
  $('stPub').textContent = c.last_publish_age_s == null ? 'never' : dur(c.last_publish_age_s) + ' ago';
  $('stCnt').textContent = c.publishes_ok + ' ok / ' + c.publishes_failed + ' failed';
  $('stConn').textContent = c.mqtt_connects;
  $('stUp').textContent = dur(s.uptime_s);
}

function renderAutomation(n) {
  const badge = $('modeBadge');
  const triggerText = $('triggerText');

  const labels = {
    proximity: 'Proximity alert - Red',
    light: 'High light level - Orange',
    humidity: 'High humidity - Yellow',
    sensor_unavailable: 'Sensor data unavailable - Purple',
    normal: 'All conditions normal - Green',
    manual: 'Manual RGB control',
    unknown: 'Waiting for automation status'
  };

  if (!n || n.state !== 'online') {
    badge.textContent = 'OFFLINE';
    badge.className = 'mode-badge offline';
    triggerText.textContent =
      'Node 1 data unavailable or stale';
    triggerText.className = 'trigger-warning';
    return;
  }

  const mode = n.mode || 'unknown';
  const trigger = n.trigger || 'unknown';

  badge.textContent = mode.toUpperCase();
  badge.className = 'mode-badge ' + mode;

  triggerText.textContent =
    labels[trigger] || 'Unknown automation state';

  triggerText.className =
    trigger === 'sensor_unavailable'
      ? 'trigger-warning'
      : '';
}

function update(s) {
  recordHistory(s);
  renderCloud(s);
  renderNodes(s);
  const n = s.nodes[LED_NODE];
  renderAutomation(n);
  $('ledHint').textContent = n && n.state !== 'online'
    ? (NODE_INFO[LED_NODE] || {name: LED_NODE}).name + ' is ' + n.state.replace('_', ' ') + ': commands may time out.' : '';
  const c = s.last_cmd;   // stays null until the gateway fills it in
  $('gwCmd').textContent = c ? 'Gateway last command: ' + c.cmd_id + ' to ' + c.target + ': ' + c.status : '';
}

// ===================== Polling =====================
let lastOkAt = null;
function setLink(ok) {
  setChip('chipGw', ok ? 's-ok' : 's-bad', ok ? 'Gateway live' : 'Gateway unreachable');
  $('nodes').classList.toggle('lost', !ok);
  const b = $('banner');
  b.classList.toggle('show', !ok);
  if (!ok) b.textContent = lastOkAt
    ? 'Cannot reach the gateway. Showing the last values received at ' + new Date(lastOkAt).toLocaleTimeString() + '.'
    : 'Cannot reach the gateway. Check that this device is connected to the gateway Wi-Fi.';
}
// setTimeout chain (not setInterval) so a slow request can never overlap the next one.
async function poll() {
  try {
    const r = await fetch(API_BASE + '/api/status', {cache: 'no-store', signal: timeoutSignal(POLL_TIMEOUT_MS)});
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const s = await r.json();
    lastOkAt = Date.now();
    update(s);
    setLink(true);
  } catch (e) {
    setLink(false);   // keep the last rendered values on screen
  } finally {
    setTimeout(poll, POLL_MS);
  }
}

// ===================== RGB LED control =====================
// The node's command has no brightness field, so brightness is applied here by scaling r, g, b.
function rgbToSend() {
  const hex = $('colour').value, k = Number($('level').value) / 100;
  return [1, 3, 5].map(i => Math.round(parseInt(hex.slice(i, i + 2), 16) * k));
}
function refreshPreview() {
  const v = rgbToSend();
  $('preview').style.background = 'rgb(' + v.join(',') + ')';
  $('hex').textContent = $('colour').value.toUpperCase();
  $('levelv').textContent = $('level').value;
  $('payload').textContent = 'Will send [' + v.join(', ') + ']';
}
function setStatus(text, cls) { const el = $('ledStatus'); el.textContent = text; el.className = 'status ' + (cls || ''); }

let cmdCount = 0;
async function sendCmd(body) {
  body.cmd_id = 'web' + (++cmdCount);   // lets the reply be matched to this click
  $('apply').disabled = $('off').disabled = true;
  setStatus('Sending ' + body.cmd_id + '...');
  const t0 = performance.now();
  try {
    const r = await fetch(CMD_URL, {
      method: 'POST',
      headers: {'Content-Type': 'text/plain'},   // text/plain avoids a browser "preflight" the node does not answer
      body: JSON.stringify(body),
      signal: timeoutSignal(CMD_TIMEOUT_MS)
    });
    const j = await r.json();
    const ms = Math.round(performance.now() - t0);
    if (r.ok && j.status === 'executed') setStatus('Executed by the node in ' + ms + ' ms (' + j.cmd_id + ').', 'ok');
    else setStatus((j.status || 'error') + ': ' + (j.error || 'HTTP ' + r.status) + ' (' + body.cmd_id + ')', 'bad');
  } catch (e) {
    setStatus(e.name === 'AbortError'
      ? 'No acknowledgment within ' + CMD_TIMEOUT_MS / 1000 + ' s. The node may be offline.'
      : 'Could not reach the node. Check that it is online and that the CORS line is in node1.ino.', 'bad');
  } finally {
    $('apply').disabled = $('off').disabled = false;
  }
}

PRESETS.forEach(([name, hex]) => {
  const b = document.createElement('button');
  b.className = 'sw'; b.style.background = hex; b.title = name; b.setAttribute('aria-label', name);
  b.addEventListener('click', () => { $('colour').value = hex; refreshPreview(); });
  $('presets').appendChild(b);
});
$('colour').addEventListener('input', refreshPreview);
$('level').addEventListener('input', refreshPreview);
$('apply').addEventListener('click', () => sendCmd({actuator: 'led', action: 'set', value: rgbToSend()}));
$('off').addEventListener('click', () => sendCmd({actuator: 'led', action: 'off'}));

refreshPreview();
poll();
</script></body></html>
)rawliteral";
