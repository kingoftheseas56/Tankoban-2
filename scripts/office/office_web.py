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
  :root { --bg:#0e0e0e; --panel:#161616; --line:#262626; --txt:#e6e6e6;
          --dim:#8a8a8a; --me:#1f1f1f; --accent:#3a3a3a; }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--txt);
         font:14px/1.5 "Segoe UI",system-ui,sans-serif; height:100vh;
         display:flex; flex-direction:column; }
  header { padding:10px 16px; border-bottom:1px solid var(--line);
           display:flex; align-items:center; gap:12px; background:var(--panel); }
  header h1 { font-size:15px; font-weight:600; margin:0; letter-spacing:.5px; }
  header .status { color:var(--dim); font-size:12px; }
  header .spacer { flex:1; }
  header button { background:var(--accent); color:var(--txt); border:1px solid var(--line);
                  padding:5px 10px; border-radius:4px; cursor:pointer; font-size:12px; }
  header button:hover { background:#4a4a4a; }
  #log { flex:1; overflow-y:auto; padding:14px 16px; }
  .msg { padding:6px 0; border-bottom:1px solid #1b1b1b; }
  .msg .meta { color:var(--dim); font-size:11px; margin-bottom:2px; }
  .msg .from { color:var(--txt); font-weight:600; }
  .msg.hemanth .from { color:#cfcfcf; }
  .msg.broadcast .to { color:#bdbdbd; }
  .msg .body { white-space:pre-wrap; word-break:break-word; }
  .empty { color:var(--dim); text-align:center; margin-top:40px; }
  footer { border-top:1px solid var(--line); background:var(--panel); padding:10px 16px;
           display:flex; gap:8px; align-items:center; }
  select, input { background:#101010; color:var(--txt); border:1px solid var(--line);
                  border-radius:4px; padding:8px; font-size:14px; }
  #to { width:110px; }
  #msg { flex:1; }
  #send { background:#2a2a2a; border:1px solid var(--line); color:var(--txt);
          padding:8px 16px; border-radius:4px; cursor:pointer; }
  #send:hover { background:#3a3a3a; }
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
    <input id="msg" placeholder="Message as hemanth... (Enter to send)" autocomplete="off">
    <button id="send">Send</button>
  </footer>
<script>
let maxseq = 0;
const log = document.getElementById('log');
const statusEl = document.getElementById('status');

function render(msgs) {
  if (maxseq === 0 && msgs.length === 0) return;
  if (log.querySelector('.empty')) log.innerHTML = '';
  for (const m of msgs) {
    const div = document.createElement('div');
    div.className = 'msg' + (m.from === 'hemanth' ? ' hemanth' : '') + (m.to === 'all' ? ' broadcast' : '');
    const time = (m.ts || '').replace('T', ' ').slice(0, 19).slice(11);
    div.innerHTML = '<div class="meta"><span class="from">' + esc(m.from) +
      '</span> &rarr; <span class="to">' + esc(m.to) + '</span> &middot; ' + esc(time) +
      '</div><div class="body">' + esc(m.msg) + '</div>';
    log.appendChild(div);
  }
  log.scrollTop = log.scrollHeight;
}
function esc(s) { const d = document.createElement('div'); d.textContent = s == null ? '' : String(s); return d.innerHTML; }

async function poll() {
  try {
    const r = await fetch('/messages?after=' + maxseq + '&_=' + Date.now(), {cache: 'no-store'});
    const data = await r.json();
    if (data.messages && data.messages.length) { render(data.messages); }
    if (typeof data.maxseq === 'number') maxseq = Math.max(maxseq, data.maxseq);
    statusEl.textContent = 'office open · ' + maxseq + ' msg' + (maxseq === 1 ? '' : 's');
  } catch (e) { statusEl.textContent = 'disconnected (server stopped?)'; }
}

async function send() {
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
  maxseq = 0;
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
