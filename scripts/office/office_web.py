#!/usr/bin/env python3
"""The Office — web GUI (a simple window into the live agent bus).

A zero-dependency local web page (Python stdlib http.server only) that lets
Hemanth WATCH the bus live and SEND messages into it as `hemanth`. Agents still
read the bus via the delivery hook; this is purely a human window + send box.
The look mimics WhatsApp desktop's group chat (Hemanth's chosen reference,
2026-05-31): green-tinted dark palette, incoming/outgoing bubbles, coloured
sender names, a chat-list-style roster rail.

Run:   python scripts/office/office_web.py          (serves http://127.0.0.1:8787)
       python scripts/office/office_web.py 9090      (custom port)

Endpoints:
  GET  /                -> the chat page (HTML)
  GET  /messages?after=<seq>  -> JSON {messages:[...], maxseq:N} (the page polls this)
  GET  /roster          -> JSON {roster:[...]} derived-status engine
  GET  /office.webmanifest -> PWA manifest (standalone app-window)
  POST /send  {to, msg} -> appends a message from "hemanth"; returns {seq}
  POST /close           -> archives + clears the bus (end of shift)

Bus reads/writes go through office_bus.py logic by importing it, so the GUI and
the hook share one source of truth (schema, locking, archive).
"""
import os
import sys
import json
import io
import threading
import time as _time
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import office_bus  # noqa: E402  (shares bus path, schema, lock, append, close)
import office_asks  # noqa: E402  (deterministic ask/ack/escalation projection)
import office_status  # noqa: E402  (derived-status engine; shares bus path)

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8787

PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>The Office</title>
<link rel="manifest" href="/office.webmanifest">
<meta name="theme-color" content="#202C33">
<style>
  /* THE OFFICE — WhatsApp-desktop group-chat look (reference 2026-05-31). */
  :root{
    --deep:#0B141A; --panel:#111B21; --bar:#202C33; --in:#202C33; --out:#005C4B;
    --sel:#2A3942; --field:#2A3942; --txt:#E9EDEF; --txt2:#8696A0;
    --green:#00A884; --red:#F15C6D; --divider:#222D34;
    --sans:'Segoe UI',system-ui,-apple-system,Helvetica,Arial,sans-serif;
  }
  *{box-sizing:border-box;}
  body{margin:0;background:var(--deep);color:var(--txt);font:14px/1.4 var(--sans);
       height:100vh;display:grid;grid-template-columns:380px 1fr;overflow:hidden;
       transition:grid-template-columns 200ms ease;}

  /* ---- left pane: roster as a WhatsApp chat-list ---- */
  #side{background:var(--panel);border-right:1px solid var(--divider);
        display:flex;flex-direction:column;min-height:0;overflow:hidden;}
  .shead{padding:14px 12px 11px;display:flex;align-items:center;gap:9px;}
  .shead h1{font-size:20px;font-weight:600;margin:0;color:var(--txt);white-space:nowrap;}
  .shead .status{font-size:12px;color:var(--txt2);margin-left:auto;white-space:nowrap;}
  #collapseBtn{background:transparent;border:none;color:var(--txt2);cursor:pointer;
               padding:5px;border-radius:7px;display:flex;flex:0 0 auto;}
  #collapseBtn:hover{background:var(--sel);color:var(--txt);}
  #collapseBtn svg{width:20px;height:20px;transition:transform 200ms ease;}
  /* collapsed: roster shrinks to an avatars-only strip */
  body.collapsed{grid-template-columns:72px 1fr;}
  body.collapsed .shead{justify-content:center;padding:14px 0;}
  body.collapsed .shead h1,
  body.collapsed .shead .status,
  body.collapsed .rmeta{display:none;}
  body.collapsed .rcard{justify-content:center;padding:9px 0;}
  body.collapsed #collapseBtn svg{transform:rotate(180deg);}
  #rlist{overflow-y:auto;flex:1;}
  .rcard{display:flex;gap:13px;align-items:center;padding:9px 14px;
         border-bottom:1px solid rgba(134,150,160,.07);}
  .rcard:hover{background:var(--sel);}
  .ava{width:46px;height:46px;flex:0 0 46px;border-radius:50%;background:#6A7175;
       display:flex;align-items:center;justify-content:center;font-weight:600;
       font-size:16px;color:#E9EDEF;position:relative;}
  .pdot{position:absolute;right:0;bottom:1px;width:13px;height:13px;border-radius:50%;
        border:2.5px solid var(--panel);background:#667781;}
  .pdot.active{background:var(--green);}
  .pdot.recent{background:#5FA86B;}
  .pdot.quiet{background:var(--warn);}
  .pdot.cold{background:#667781;}
  .pdot.blocked{background:var(--red);animation:pulse 2s ease-in-out infinite;}
  .rstatus{font-size:11px;color:var(--txt2);margin-top:3px;}
  .rstatus.active,.rstatus.recent{color:#8FCF9C;}
  .rstatus.quiet{color:var(--warn);}
  .rstatus.cold{color:#6B7B86;}
  @keyframes pulse{0%,100%{opacity:1;}50%{opacity:.4;}}
  .rmeta{flex:1;min-width:0;}
  .rtop{display:flex;align-items:baseline;gap:7px;}
  .rname{font-size:16px;color:var(--txt);white-space:nowrap;}
  .rrole{font-size:12px;color:var(--txt2);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
  .rtime{font-size:12px;color:var(--txt2);margin-left:auto;flex:0 0 auto;}
  .rsub{display:flex;align-items:center;gap:6px;margin-top:2px;}
  .rline{flex:1;min-width:0;font-size:13.5px;color:var(--txt2);
         white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
  .rarc{color:var(--green);font-size:12px;flex:0 0 auto;}
  .chip{font-size:11px;padding:1px 8px;border-radius:10px;font-weight:600;flex:0 0 auto;}
  .chip.blocked{background:var(--red);color:#0B141A;}
  .chip.nudge{background:#3B4A54;color:var(--txt2);}
  .chip.backup{background:#10403A;color:var(--green);}
  .wk{display:inline-flex;align-items:center;gap:3px;font-size:10.5px;font-weight:600;flex:0 0 auto;}
  .wk svg{width:13px;height:13px;}
  .wk.live{color:var(--green);}
  .wk.down{color:var(--red);}
  .wk.unknown{color:var(--txt2);opacity:.7;}

  /* ---- right pane: conversation ---- */
  #conv{display:flex;flex-direction:column;min-height:0;background:var(--deep);}
  #chead{background:var(--bar);padding:9px 16px;display:flex;align-items:center;gap:13px;}
  #chead .ava{width:40px;height:40px;flex:0 0 40px;font-size:13px;background:#3B4A54;}
  #chead .ctitle{flex:1;min-width:0;}
  #chead .cname{font-size:16px;color:var(--txt);}
  #chead .cmembers{font-size:12.5px;color:var(--txt2);white-space:nowrap;
                   overflow:hidden;text-overflow:ellipsis;}
  #chead button{background:transparent;border:none;color:var(--txt2);cursor:pointer;
                font-size:12.5px;padding:6px 11px;border-radius:7px;transition:all 150ms;}
  #chead button:hover{background:var(--sel);color:var(--txt);}
  #log{flex:1;overflow-y:auto;padding:16px 7% 20px;background:var(--deep);}
  .empty{color:var(--txt2);text-align:center;margin-top:40vh;font-size:13.5px;}

  .msg{display:flex;margin-top:2px;}
  .msg.first{margin-top:11px;}
  .msg.out{justify-content:flex-end;}
  .gava{width:29px;height:29px;flex:0 0 29px;border-radius:50%;background:#6A7175;
        display:flex;align-items:center;justify-content:center;font-size:11.5px;
        color:#E9EDEF;margin-right:8px;align-self:flex-start;margin-top:2px;}
  .msg:not(.first) .gava{visibility:hidden;}
  .msg.out .gava{display:none;}
  .bub{position:relative;max-width:66%;padding:7px 10px 6px;border-radius:9px;
       background:var(--in);box-shadow:0 1px .8px rgba(0,0,0,.25);font-size:14.2px;}
  .msg.out .bub{background:var(--out);}
  .msg.in.first .bub{border-top-left-radius:0;}
  .msg.out.first .bub{border-top-right-radius:0;}
  .sname{font-size:13px;font-weight:600;margin-bottom:2px;}
  .btext{color:var(--txt);white-space:pre-wrap;word-break:break-word;}
  .bmeta{display:flex;gap:8px;align-items:center;justify-content:flex-end;
         font-size:11px;color:var(--txt2);margin-top:3px;}
  .msg.out .bmeta{color:rgba(233,237,239,.6);}
  .bto{margin-right:auto;opacity:.85;}
  .blk-badge{display:inline-block;font-size:10.5px;font-weight:700;letter-spacing:.5px;
             color:var(--red);margin-bottom:2px;}
  .msg.blk .bub{background:#2C1A1C;border-left:4px solid var(--red);border-top-left-radius:9px;}

  /* activity (auto commit mirror) + system = centered pill, WhatsApp-style */
  .msg.sys{justify-content:center;margin:11px 0;}
  .sys .pill{background:#182229;color:var(--txt2);font-size:12.5px;padding:6px 13px;
             border-radius:9px;box-shadow:0 1px .8px rgba(0,0,0,.2);max-width:82%;
             text-align:center;line-height:1.35;}
  .sys .pill .gi{margin-right:6px;opacity:.7;}
  .sys .pill .gi svg{width:12px;height:12px;vertical-align:-2px;}
  .sys .pill .who{color:var(--txt);opacity:.85;}

  /* composer */
  footer{background:var(--bar);padding:9px 16px;display:flex;gap:10px;align-items:center;}
  select,#msg{background:var(--field);color:var(--txt);border:none;border-radius:8px;
              padding:11px 13px;font-size:14px;outline:none;font-family:var(--sans);}
  #to{flex:0 0 auto;}
  #msg{flex:1;}
  #msg::placeholder{color:var(--txt2);}
  #send{background:var(--green);border:none;color:#0B141A;font-weight:600;
        padding:11px 19px;border-radius:8px;cursor:pointer;font-size:14px;transition:background 150ms;}
  #send:hover{background:#06b894;}
  ::-webkit-scrollbar{width:8px;}
  ::-webkit-scrollbar-thumb{background:#2A3942;border-radius:4px;}
  ::-webkit-scrollbar-thumb:hover{background:#33424b;}
</style></head>
<body>
  <div id="side">
    <div class="shead">
      <button id="collapseBtn" title="Collapse / expand roster">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M11 7l-5 5 5 5M18 7l-5 5 5 5"/></svg>
      </button>
      <h1>THE OFFICE</h1><span class="status" id="status">connecting…</span>
    </div>
    <div id="rlist"></div>
  </div>
  <div id="conv">
    <div id="chead">
      <div class="ava">TO</div>
      <div class="ctitle">
        <div class="cname">THE OFFICE</div>
        <div class="cmembers" id="members">the brotherhood</div>
      </div>
      <button id="notifyBtn" title="Desktop notification when a brother messages @hemanth">alerts</button>
      <button id="closeBtn" title="Archive + clear the bus (end of shift)">close</button>
    </div>
    <div id="log"><div class="empty">No messages yet. Say something below.</div></div>
    <footer>
      <select id="to">
        <option value="all">@all</option>
        <option value="agent0">@agent0</option>
        <option value="agent1">@agent1</option>
        <option value="agent2">@agent2</option>
        <option value="agent3">@agent3</option>
        <option value="agent4">@agent4</option>
        <option value="agent5">@agent5</option>
        <option value="agent7">@agent7</option>
        <option value="agent9">@agent9</option>
      </select>
      <input id="msg" placeholder="Type a message" autocomplete="off">
      <button id="send">Send</button>
    </footer>
  </div>
<script>
let maxseq = 0, lastFrom = null;
let primed = false;  // don't fire notifications for the backlog loaded on first poll
const log = document.getElementById('log');
const statusEl = document.getElementById('status');
const rlist = document.getElementById('rlist');

// --- collapsible roster (remembers your choice across launches) ---
const collapseBtn = document.getElementById('collapseBtn');
let collapsed = localStorage.getItem('office_collapsed') === '1';
function applyCollapsed(){ document.body.classList.toggle('collapsed', collapsed); }
applyCollapsed();
collapseBtn.onclick = () => {
  collapsed = !collapsed;
  localStorage.setItem('office_collapsed', collapsed ? '1' : '0');
  applyCollapsed();
};

// --- Desktop notifications when a brother addresses @hemanth ---
const notifyBtn = document.getElementById('notifyBtn');
const hasNotif = ('Notification' in window);
function updateNotifyBtn(){
  if (!hasNotif) { notifyBtn.textContent = 'no alerts'; notifyBtn.disabled = true; return; }
  if (Notification.permission === 'granted') { notifyBtn.textContent = 'alerts on'; }
  else if (Notification.permission === 'denied') { notifyBtn.textContent = 'alerts off'; }
  else { notifyBtn.textContent = 'alerts'; }
}
notifyBtn.onclick = async () => {
  if (!hasNotif) return;
  if (Notification.permission === 'default') { try { await Notification.requestPermission(); } catch (e) {} }
  updateNotifyBtn();
};
function addressedToHemanth(to){
  const t = String(to || '');
  return t === 'hemanth' || t.split(',').map(s => s.trim()).includes('hemanth');
}
function maybeNotify(m){
  if (!primed || !hasNotif || Notification.permission !== 'granted') return;
  if (m.from === 'hemanth' || !addressedToHemanth(m.to)) return;
  try {
    const n = new Notification('The Office — ' + labelFor(m.from), { body: m.msg, tag: 'office-' + m.seq });
    n.onclick = () => { window.focus(); n.close(); };
  } catch (e) {}
}
updateNotifyBtn();

// WhatsApp-group-style coloured SENDER NAMES (avatars stay neutral grey).
const NAMEPAL = ['#53BDEB','#06CF9C','#E6799F','#A78BFA','#FFAB5C',
                 '#7FD858','#6BC5E8','#F5C84B','#FF8A8A','#9F8AE0'];
function colorFor(name){
  let h = 0; const s = String(name);
  for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0;
  return NAMEPAL[h % NAMEPAL.length];
}
function initialFor(name){
  name = String(name); let d = '';
  for (let i = 0; i < name.length; i++){ const c = name[i]; if (c >= '0' && c <= '9') d += c; }
  return d || name.slice(0, 2).toUpperCase();
}
function labelFor(name){
  name = String(name);
  if (name === 'hemanth') return 'Hemanth';
  if (name === 'system') return 'system';
  if (name.indexOf('agent') === 0) return 'Agent ' + name.slice(5);
  return name;
}
function esc(s){ const d = document.createElement('div'); d.textContent = s == null ? '' : String(s); return d.innerHTML; }
function ago(s){
  if (s == null) return '';
  if (s < 60) return s + 's';
  if (s < 3600) return Math.floor(s/60) + 'm';
  if (s < 86400) return Math.floor(s/3600) + 'h';
  return Math.floor(s/86400) + 'd';
}
const GIT_SVG = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4"><circle cx="4" cy="4" r="1.7"/><circle cx="4" cy="12" r="1.7"/><circle cx="12" cy="6" r="1.7"/><path d="M4 5.7v4.6M5.6 5.2h2.6a2 2 0 0 1 2 2v.6"/></svg>';
const SIGNAL_SVG = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"><circle cx="8" cy="11.6" r="1.3" fill="currentColor" stroke="none"/><path d="M5.4 9.2a3.6 3.6 0 0 1 5.2 0"/><path d="M3.4 7.1a6.4 6.4 0 0 1 9.2 0"/></svg>';

function render(msgs){
  if (maxseq === 0 && msgs.length === 0) return;
  const emptyEl = log.querySelector('.empty'); if (emptyEl) log.innerHTML = '';
  // Only auto-scroll if the reader is already near the bottom.
  const atBottom = log.scrollHeight - log.scrollTop - log.clientHeight < 120;
  for (const m of msgs){
    const me = (m.from === 'hemanth');
    const kind = m.kind || 'chat';
    const time = (m.ts || '').replace('T', ' ').slice(11, 16);
    if (kind === 'activity'){
      const row = document.createElement('div');
      row.className = 'msg sys';
      row.innerHTML = '<div class="pill"><span class="gi">' + GIT_SVG + '</span>' +
        '<span class="who">' + esc(labelFor(m.from)) + '</span> · ' + esc(m.msg) + '</div>';
      log.appendChild(row);
      lastFrom = null;
      continue;
    }
    const first = (m.from !== lastFrom);  // first message of a sender cluster
    const row = document.createElement('div');
    row.className = 'msg ' + (me ? 'out' : 'in') + (first ? ' first' : '') + (kind === 'blocked' ? ' blk' : '');
    const gava = me ? '' : '<div class="gava">' + esc(initialFor(m.from)) + '</div>';
    const sname = (!me && first) ?
      '<div class="sname" style="color:' + colorFor(m.from) + '">' + esc(labelFor(m.from)) + '</div>' : '';
    const blk = (kind === 'blocked') ? '<span class="blk-badge">BLOCKED</span>' : '';
    const toTag = (m.to === 'all') ? '' : '<span class="bto">→ ' + esc(labelFor(m.to)) + '</span>';
    row.innerHTML = gava +
      '<div class="bub">' + sname + blk +
      '<div class="btext">' + esc(m.msg) + '</div>' +
      '<div class="bmeta">' + toTag + '<span>' + esc(time) + '</span></div>' +
      '</div>';
    log.appendChild(row);
    lastFrom = m.from;
    maybeNotify(m);
  }
  if (atBottom) log.scrollTop = log.scrollHeight;
}

async function poll(){
  try {
    const r = await fetch('/messages?after=' + maxseq + '&_=' + Date.now(), {cache: 'no-store'});
    const data = await r.json();
    if (data.messages && data.messages.length) render(data.messages);
    if (typeof data.maxseq === 'number') maxseq = Math.max(maxseq, data.maxseq);
    statusEl.textContent = maxseq + ' message' + (maxseq === 1 ? '' : 's');
    primed = true;
  } catch (e) { statusEl.textContent = 'disconnected'; }
}

async function send(){
  const msg = document.getElementById('msg');
  const to = document.getElementById('to').value;
  const text = msg.value.trim();
  if (!text) return;
  msg.value = '';
  try {
    await fetch('/send', {method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify({to: to, msg: text})});
    poll();
  } catch (e) { statusEl.textContent = 'send failed'; }
}
document.getElementById('send').onclick = send;
document.getElementById('msg').addEventListener('keydown', e => { if (e.key === 'Enter') send(); });
document.getElementById('closeBtn').onclick = async () => {
  if (!confirm('Close the office? This archives + clears all current messages.')) return;
  await fetch('/close', {method:'POST'});
  log.innerHTML = '<div class="empty">Office closed. Messages archived.</div>';
  maxseq = 0; lastFrom = null;
};

function dotClass(r){ return r.blocked ? 'blocked' : (r.status || 'cold'); }
function renderRoster(list){
  rlist.innerHTML = '';
  let present = 0;
  for (const r of list){
    if (r.present) present++;
    const card = document.createElement('div');
    card.className = 'rcard';
    card.title = labelFor(r.agent) + ' — ' + r.role;  // identity on hover when collapsed
    let line, t = '';
    if (r.last_said){ line = esc(r.last_said); t = ago(r.last_said_sec); }
    else if (r.last_commit){ line = 'committed ' + esc(r.last_commit); t = ago(r.last_commit_sec); }
    else { line = '<span style="opacity:.65">idle</span>'; }
    const arc = r.current_arc ? '<span class="rarc">#' + esc(r.current_arc) + '</span>' : '';
    const nudge = r.wakeable ? '' : '<span class="chip nudge" title="not auto-wakeable">nudge</span>';
    const blocked = r.blocked ? '<span class="chip blocked">blocked</span>' : '';
    // backup net: an owned-worker responder is watching this brother — a dropped
    // message still gets a marked, non-binding reply even when his tab is dark.
    const backup = r.responder_alive ? '<span class="chip backup" title="backup responder armed — a dropped message still gets a marked, non-binding reply">backup</span>' : '';
    // wake channel: live (watch beating) vs DOWN (deaf — can't be auto-woken)
    let wake;
    if (r.wake_state === 'live')
      wake = '<span class="wk live" title="watch live — can hear new messages">' + SIGNAL_SVG + '</span>';
    else if (r.wake_state === 'down')
      wake = '<span class="wk down" title="watch DOWN — can NOT be auto-woken; needs a prompt in its tab">' +
             SIGNAL_SVG + 'deaf' + (r.wake_age_sec != null ? ' ' + ago(r.wake_age_sec) : '') + '</span>';
    else
      wake = '<span class="wk unknown" title="watch status unknown — no heartbeat yet (restart watch to report)">' +
             SIGNAL_SVG + '?</span>';
    card.innerHTML =
      '<div class="ava">' + esc(initialFor(r.agent)) + '<span class="pdot ' + dotClass(r) + '"></span></div>' +
      '<div class="rmeta">' +
        '<div class="rtop"><span class="rname">' + esc(labelFor(r.agent)) + '</span>' +
          '<span class="rrole">' + esc(r.role) + '</span><span class="rtime">' + t + '</span></div>' +
        '<div class="rsub"><span class="rline">' + line + '</span>' + arc + nudge + blocked + backup + wake + '</div>' +
        '<div class="rstatus ' + (r.status || 'cold') + '">' + esc(r.status_label || '') + '</div>' +
      '</div>';
    rlist.appendChild(card);
  }
  const mem = document.getElementById('members');
  if (mem) mem.textContent = list.map(r => labelFor(r.agent)).join(', ') + '  ·  ' + present + ' active';
}
async function pollRoster(){
  try {
    const r = await fetch('/roster?_=' + Date.now(), {cache:'no-store'});
    const data = await r.json();
    if (data.roster) renderRoster(data.roster);
  } catch (e) {}
}
pollRoster();
setInterval(pollRoster, 4000);

poll();
setInterval(poll, 1500);
</script>
</body></html>"""


def _read_all_messages():
    bus = office_bus.BUS()
    out = []
    if os.path.exists(bus):
        with open(bus, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    out.append(json.loads(line))
                except json.JSONDecodeError:
                    continue
    return out


def escalate_tick_once(window=None, escalate2=None):
    """Post newly due escalation events. Deterministic; no LLM calls."""
    w = office_asks.WINDOW_SEC if window is None else window
    e2 = office_asks.ESCALATE2_SEC if escalate2 is None else escalate2
    recs = office_asks._bus_records()
    now = int(_time.time())
    posted = 0
    for ask_seq, who, level in office_asks.due_escalations(recs, now, w, e2):
        msg = "{0} hasn't acknowledged ask #{1} in time - needs attention".format(who, ask_seq)
        arc = "{0}:{1}".format(ask_seq, who)
        buf = io.StringIO()
        old = sys.stdout
        sys.stdout = buf
        try:
            office_bus.cmd_append("system", level, "escalate", arc, msg)
        finally:
            sys.stdout = old
        posted += 1
    return posted


def _escalation_loop():
    while True:
        try:
            escalate_tick_once()
        except Exception:
            traceback.print_exc(file=sys.stderr)
        _time.sleep(30)


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        # Never let the browser serve a stale message list — the live poll
        # depends on every /messages fetch hitting the server fresh.
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        self.send_header("Pragma", "no-cache")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/" or self.path.startswith("/index"):
            self._send(200, PAGE, "text/html; charset=utf-8")
            return
        if self.path.startswith("/messages"):
            after = 0
            if "?" in self.path:
                q = self.path.split("?", 1)[1]
                for kv in q.split("&"):
                    if kv.startswith("after="):
                        try:
                            after = int(kv.split("=", 1)[1])
                        except ValueError:
                            after = 0
            msgs = [m for m in _read_all_messages() if m.get("seq", 0) > after]
            allmsgs = _read_all_messages()
            maxseq = max((m.get("seq", 0) for m in allmsgs), default=0)
            self._send(200, json.dumps({"messages": msgs, "maxseq": maxseq}))
            return
        if self.path.startswith("/office.webmanifest"):
            path = os.path.join(HERE, "office.webmanifest")
            try:
                with open(path, "r", encoding="utf-8") as f:
                    self._send(200, f.read(), "application/manifest+json")
            except OSError:
                self._send(404, json.dumps({"error": "no manifest"}))
            return
        if self.path.startswith("/roster"):
            try:
                data = office_status.roster_now()
            except Exception:
                data = []
            self._send(200, json.dumps({"roster": data}))
            return
        self._send(404, json.dumps({"error": "not found"}))

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length).decode("utf-8") if length else "{}"
        try:
            body = json.loads(raw) if raw.strip() else {}
        except json.JSONDecodeError:
            body = {}
        if self.path == "/send":
            to = (body.get("to") or "all").strip()
            msg = (body.get("msg") or "").strip()
            if not msg:
                self._send(400, json.dumps({"error": "empty message"}))
                return
            # Reuse the exact append path (schema/lock/seq) the hook trusts.
            import io
            buf = io.StringIO()
            old = sys.stdout
            sys.stdout = buf
            try:
                office_bus.cmd_append("hemanth", to, "chat", "null", msg)
            finally:
                sys.stdout = old
            seq = buf.getvalue().strip()
            self._send(200, json.dumps({"seq": seq}))
            return
        if self.path == "/close":
            import io
            buf = io.StringIO(); old = sys.stdout; sys.stdout = buf
            try:
                office_bus.cmd_close()
            finally:
                sys.stdout = old
            self._send(200, json.dumps({"result": buf.getvalue().strip()}))
            return
        self._send(404, json.dumps({"error": "not found"}))

    def log_message(self, *args):
        pass  # quiet


def main():
    threading.Thread(target=_escalation_loop, daemon=True).start()
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print("The Office GUI -> http://127.0.0.1:{0}  (Ctrl+C to stop)".format(PORT))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nOffice GUI stopped.")
        srv.shutdown()


if __name__ == "__main__":
    main()
