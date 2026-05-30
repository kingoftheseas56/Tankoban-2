#!/usr/bin/env python3
"""The Office — web GUI (a simple window into the live agent bus).

A zero-dependency local web page (Python stdlib http.server only) that lets
Hemanth WATCH the bus live and SEND messages into it as `hemanth`. Agents still
read the bus via the delivery hook; this is purely a human window + send box.

Run:   python scripts/office/office_web.py          (serves http://127.0.0.1:8787)
       python scripts/office/office_web.py 9090      (custom port)

Endpoints:
  GET  /                -> the chat page (HTML)
  GET  /messages?after=<seq>  -> JSON {messages:[...], maxseq:N} (the page polls this)
  POST /send  {to, msg} -> appends a message from "hemanth"; returns {seq}
  POST /close           -> archives + clears the bus (end of shift)

Bus reads/writes go through office_bus.py logic by importing it, so the GUI and
the hook share one source of truth (schema, locking, archive).
"""
import os
import sys
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import office_bus  # noqa: E402  (shares bus path, schema, lock, append, close)

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8787

PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>The Office</title>
<style>
  :root{
    --bg:#0b141a; --panel:#17212b; --line:#0a1014; --txt:#e9edf0; --dim:#8696a0;
    --bubble-in:#1f2c38; --bubble-me:#2b5278; --send:#3390ec;
  }
  *{box-sizing:border-box;}
  body{margin:0;background:var(--bg);color:var(--txt);
       font:14px/1.45 "Segoe UI",system-ui,sans-serif;height:100vh;
       display:flex;flex-direction:column;}
  header{padding:10px 16px;border-bottom:1px solid var(--line);
         display:flex;align-items:center;gap:12px;background:var(--panel);
         box-shadow:0 1px 5px rgba(0,0,0,.35);z-index:2;}
  header h1{font-size:15px;font-weight:600;margin:0;letter-spacing:.7px;}
  header .status{color:var(--dim);font-size:12px;}
  header .spacer{flex:1;}
  header button{background:transparent;color:var(--dim);border:1px solid var(--line);
                padding:5px 11px;border-radius:6px;cursor:pointer;font-size:12px;}
  header button:hover{background:#22323f;color:var(--txt);}
  #log{flex:1;overflow-y:auto;padding:12px 14px 18px;
       display:flex;flex-direction:column;
       background:linear-gradient(180deg,#0b141a,#0d171e);}
  .row{display:flex;align-items:flex-end;max-width:100%;}
  .row:not(.grouped){margin-top:11px;}
  .row.grouped{margin-top:2px;}
  .row.me{justify-content:flex-end;}
  .avatar{width:33px;height:33px;border-radius:50%;flex:0 0 33px;
          display:flex;align-items:center;justify-content:center;
          font-size:12.5px;font-weight:700;color:#0b141a;margin-right:8px;}
  .row.grouped .avatar{visibility:hidden;}
  .row.me .avatar{display:none;}
  .bubble{max-width:74%;padding:6px 11px 5px;border-radius:11px;
          background:var(--bubble-in);box-shadow:0 1px 1px rgba(0,0,0,.28);}
  .row:not(.grouped):not(.me) .bubble{border-top-left-radius:4px;}
  .row.me:not(.grouped) .bubble{border-top-right-radius:4px;}
  .row.me .bubble{background:var(--bubble-me);}
  .bubble .name{font-size:12.5px;font-weight:700;margin-bottom:2px;}
  .bubble .body{white-space:pre-wrap;word-break:break-word;font-size:14px;}
  .bubble .foot{font-size:10.5px;color:var(--dim);margin-top:3px;
                display:flex;gap:7px;justify-content:flex-end;align-items:center;}
  .row.me .bubble .foot{color:#a8c7e8;}
  .bubble .to-tag{opacity:.8;}
  .empty{color:var(--dim);text-align:center;margin:auto;}
  footer{border-top:1px solid var(--line);background:var(--panel);padding:10px 14px;
         display:flex;gap:8px;align-items:center;}
  select,input{background:#0e1822;color:var(--txt);border:1px solid var(--line);
               border-radius:8px;padding:9px;font-size:14px;outline:none;}
  select:focus,input:focus{border-color:var(--send);}
  #to{width:108px;}
  #msg{flex:1;}
  #send{background:var(--send);border:none;color:#fff;font-weight:600;
        padding:9px 18px;border-radius:8px;cursor:pointer;}
  #send:hover{background:#2a82da;}
  #log::-webkit-scrollbar{width:9px;}
  #log::-webkit-scrollbar-thumb{background:#22323f;border-radius:5px;}
  #log::-webkit-scrollbar-thumb:hover{background:#2c4150;}
</style></head>
<body>
  <header>
    <h1>THE OFFICE</h1>
    <span class="status" id="status">connecting...</span>
    <span class="spacer"></span>
    <button id="closeBtn" title="Archive + clear the bus (end of shift)">Close office</button>
  </header>
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
    <input id="msg" placeholder="Message as hemanth...  (Enter to send)" autocomplete="off">
    <button id="send">Send</button>
  </footer>
<script>
let maxseq = 0, lastFrom = null;
const log = document.getElementById('log');
const statusEl = document.getElementById('status');
// Telegram-style per-member accent colours (readable on the dark bubbles).
const PALETTE = ['#e17076','#7bc862','#65aadd','#a695e7','#ee7aae',
                 '#6ec9cb','#faa774','#f2c94c','#9ed888','#e0a2c0'];
function colorFor(name){
  let h = 0; const s = String(name);
  for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0;
  return PALETTE[h % PALETTE.length];
}
function initialFor(name){
  name = String(name); let d = '';
  for (let i = 0; i < name.length; i++){ const c = name[i]; if (c >= '0' && c <= '9') d += c; }
  return d || name.slice(0, 2).toUpperCase();
}
function labelFor(name){
  name = String(name);
  if (name === 'hemanth') return 'Hemanth';
  if (name.indexOf('agent') === 0) return 'Agent ' + name.slice(5);
  return name;
}
function esc(s){ const d = document.createElement('div'); d.textContent = s == null ? '' : String(s); return d.innerHTML; }

function render(msgs){
  if (maxseq === 0 && msgs.length === 0) return;
  const emptyEl = log.querySelector('.empty'); if (emptyEl) log.innerHTML = '';
  // Only auto-scroll if the reader is already near the bottom — so scrolling
  // up to read older messages isn't yanked back down by an incoming poll.
  const atBottom = log.scrollHeight - log.scrollTop - log.clientHeight < 90;
  for (const m of msgs){
    const me = (m.from === 'hemanth');
    const grouped = (m.from === lastFrom);
    const col = colorFor(m.from);
    const time = (m.ts || '').replace('T', ' ').slice(11, 16);
    const row = document.createElement('div');
    row.className = 'row' + (me ? ' me' : '') + (grouped ? ' grouped' : '');
    const avatar = me ? '' :
      '<div class="avatar" style="background:' + col + '">' + esc(initialFor(m.from)) + '</div>';
    const nameHtml = grouped ? '' :
      '<div class="name" style="color:' + (me ? '#8ec3f0' : col) + '">' + esc(labelFor(m.from)) + '</div>';
    const toTag = (m.to === 'all') ? 'to all' : ('to ' + esc(labelFor(m.to)));
    row.innerHTML = avatar +
      '<div class="bubble">' + nameHtml +
      '<div class="body">' + esc(m.msg) + '</div>' +
      '<div class="foot"><span class="to-tag">' + toTag + '</span><span>' + esc(time) + '</span></div>' +
      '</div>';
    log.appendChild(row);
    lastFrom = m.from;
  }
  if (atBottom) log.scrollTop = log.scrollHeight;
}

async function poll(){
  try {
    const r = await fetch('/messages?after=' + maxseq + '&_=' + Date.now(), {cache: 'no-store'});
    const data = await r.json();
    if (data.messages && data.messages.length) render(data.messages);
    if (typeof data.maxseq === 'number') maxseq = Math.max(maxseq, data.maxseq);
    statusEl.textContent = 'office open · ' + maxseq + ' msg' + (maxseq === 1 ? '' : 's');
  } catch (e) { statusEl.textContent = 'disconnected (server stopped?)'; }
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
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print("The Office GUI -> http://127.0.0.1:{0}  (Ctrl+C to stop)".format(PORT))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nOffice GUI stopped.")
        srv.shutdown()


if __name__ == "__main__":
    main()
