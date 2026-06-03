OpenAI Codex v0.131.0
--------
workdir: C:\Users\Suprabha\Desktop\Tankoban 2
model: gpt-5.5
provider: openai
approval: never
sandbox: read-only
reasoning effort: high
reasoning summaries: none
session id: 019e8d34-b32d-77d2-8921-c612bc01cb5c
--------
user
Cross-model review for Tankoban 2 (author = Agent 0/Opus, you are a DIFFERENT model). Read-only; do NOT edit. Review my changes to scripts/engines/engine.py (the multi-engine broker) against this Definition of Done. Focus hardest on the concurrency fix.

DEFINITION OF DONE (verify each):
1. LOCK FIX: previously main() ran dispatch() (a multi-minute engine call) INSIDE `with _ledger_lock()`, which serialized all engine calls and deadlocked concurrent callers (EDEADLK). New code must: hold the lock ONLY for check_cap + a reservation log_call, release it, then run dispatch() OUTSIDE the lock with log=False. Verify: (a) no engine call happens under the lock now; (b) the per-wake hard-cap no-overshoot guarantee is preserved (the reservation row is written under the lock so a concurrent check_cap counts it); (c) dispatch() called directly (as the tests do) still logs by default (log=True).
2. SEE LANE: call_gemini_visual(image_paths, prompt, cfg) base64-packs images into inline_data parts with correct mime from extension, guards a 4MB total inline limit, rejects empty/unsupported, and dispatch("see") gates on gemini.enabled like "read".
3. RETRY: _gemini_request retries on HTTP 503/429 (transient) with backoff, raises other errors; call_gemini + call_gemini_visual both route through it.
4. No regression to cap accounting, the gate, or the dry-run paths.

Find any real bug: a race the reservation doesn't close, a path where a call is double-logged or never logged, the reservation row leaking on a failed dispatch (acceptable? note it), mime/size guard holes, retry that loops forever or swallows a real error.

END with exactly one line: APPROVE or REQUEST-CHANGES + one-sentence reason.

==== DIFF: scripts/engines/engine.py + engines.config.json ====
diff --git a/scripts/engines/engine.py b/scripts/engines/engine.py
index 6042a09..54b7eeb 100644
--- a/scripts/engines/engine.py
+++ b/scripts/engines/engine.py
@@ -4,7 +4,7 @@
 Spec: docs/superpowers/specs/2026-06-01-multi-engine-brother-design.md
 Keys come from environment only (DEEPSEEK_API_KEY, GEMINI_API_KEY). Never logged.
 """
-import os, sys, json, time, subprocess, tempfile, urllib.request, contextlib
+import os, sys, json, time, subprocess, tempfile, urllib.request, urllib.error, contextlib, base64
 
 HERE = os.path.dirname(os.path.abspath(__file__))
 CONFIG_PATH = os.path.join(HERE, "engines.config.json")
@@ -182,6 +182,7 @@ def call_deepseek(packet, cfg):
     proc = subprocess.run(
         [cli, "-p", "--model", cfg["deepseek"]["model"]],
         input=packet, cwd=scratch, env=env, capture_output=True, text=True,
+        encoding="utf-8", errors="replace",
         timeout=cfg["timeouts"]["deepseek"])
     if proc.returncode != 0:
         tail = proc.stderr.strip()[-200:] if proc.stderr else "(no stderr)"
@@ -200,6 +201,7 @@ def call_codex(packet, cfg):
     proc = subprocess.run(
         [cli, "exec"],
         input=packet, capture_output=True, text=True,
+        encoding="utf-8", errors="replace",
         timeout=cfg["timeouts"]["codex"])
     if proc.returncode != 0:
         tail = proc.stderr.strip()[-200:] if proc.stderr else "(no stderr)"
@@ -208,20 +210,72 @@ def call_codex(packet, cfg):
     return parse_codex(proc.stdout)
 
 
+# Gemini multimodal mime map (the types generateContent accepts inline).
+_GEMINI_MIME = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
+                ".webp": "image/webp", ".heic": "image/heic", ".heif": "image/heif"}
+_GEMINI_INLINE_LIMIT = 4 * 1024 * 1024  # 4 MB total inline cap; above this -> Files API
+
+
+def _gemini_request(parts, cfg):
+    """POST one contents[].parts[] payload to generateContent, with retry on
+    transient 503/429 (Gemini Flash returns 503 under load — caught live
+    2026-06-03). Auth via x-goog-api-key header."""
+    key = require_key("GEMINI_API_KEY")
+    url = cfg["gemini"]["url"].format(model=cfg["gemini"]["model"])
+    body = json.dumps({"contents": [{"parts": parts}]}).encode()
+    last = None
+    for attempt in range(4):
+        req = urllib.request.Request(
+            url, data=body,
+            headers={"Content-Type": "application/json", "x-goog-api-key": key})
+        try:
+            with urllib.request.urlopen(req, timeout=cfg["timeouts"]["gemini"]) as resp:
+                return parse_gemini(json.load(resp))
+        except urllib.error.HTTPError as e:
+            last = e
+            if e.code in (429, 503) and attempt < 3:
+                time.sleep(2 * (attempt + 1))  # 2s, 4s, 6s backoff
+                continue
+            raise
+    raise last
+
+
 def call_gemini(packet, cfg):
     if os.environ.get("ENGINE_DRY_RUN") == "1":
         return "DRY:gemini:" + packet[:20]
-    key = require_key("GEMINI_API_KEY")
-    url = cfg["gemini"]["url"].format(model=cfg["gemini"]["model"])
-    body = json.dumps({"contents": [{"parts": [{"text": packet}]}]}).encode()
-    req = urllib.request.Request(url, data=body,
-                                 headers={"Content-Type": "application/json",
-                                          "x-goog-api-key": key})
-    with urllib.request.urlopen(req, timeout=cfg["timeouts"]["gemini"]) as resp:
-        return parse_gemini(json.load(resp))
+    return _gemini_request([{"text": packet}], cfg)
 
 
-def dispatch(cmd, packet, task, purpose, cfg):
+def call_gemini_visual(image_paths, prompt, cfg):
+    """`see` lane — text prompt + one or more inline images. Gemini is the only
+    engine with eyes (Codex/DeepSeek are text). Images ride as base64 inline_data
+    parts; total must stay under the 4 MB inline cap (Files API above that)."""
+    if os.environ.get("ENGINE_DRY_RUN") == "1":
+        return "DRY:gemini-visual:" + (prompt or "")[:20]
+    if not image_paths:
+        raise ValueError("see: at least one --image is required")
+    parts = [{"text": prompt}]
+    total = 0
+    for p in image_paths:
+        ext = os.path.splitext(p)[1].lower()
+        mime = _GEMINI_MIME.get(ext)
+        if not mime:
+            raise ValueError(
+                f"see: unsupported image type '{ext}' for {p} "
+                f"(allowed: {sorted(_GEMINI_MIME)})")
+        with open(p, "rb") as f:
+            raw = f.read()
+        total += len(raw)
+        if total > _GEMINI_INLINE_LIMIT:
+            raise ValueError(
+                f"see: inline images exceed 4 MB total ({total} bytes) — "
+                f"use fewer/smaller images (Files API not wired)")
+        parts.append({"inline_data": {"mime_type": mime,
+                                      "data": base64.b64encode(raw).decode()}})
+    return _gemini_request(parts, cfg)
+
+
+def dispatch(cmd, packet, task, purpose, cfg, images=None, log=True):
     guard_packet(packet, cfg)
     if cmd == "grunt":
         out = call_deepseek(packet, cfg); name = "deepseek"
@@ -232,13 +286,31 @@ def dispatch(cmd, packet, task, purpose, cfg):
             raise RuntimeError("Gemini disabled (reliability gate not passed). "
                                "Read directly on Claude, or run gemini_gate.py.")
         out = call_gemini(packet, cfg); name = "gemini"
+    elif cmd == "see":
+        if not cfg["gemini"].get("enabled"):
+            raise RuntimeError("Gemini disabled (reliability gate not passed). "
+                               "Run gemini_gate.py to enable.")
+        out = call_gemini_visual(images, packet, cfg); name = "gemini"
     else:
         raise ValueError(f"unknown command: {cmd}")
-    log_call(name, len(out), purpose, task)
+    # log defaults True so a direct dispatch() call records its own row (tests +
+    # the gate rely on this). main() passes log=False because it has already
+    # reserved the cap slot under the lock before calling out-of-lock.
+    if log:
+        log_call(name, len(out), purpose, task)
     return out
 
 
 def main(argv=None):
+    # Windows console is cp1252 and crashes on engine output containing arrows /
+    # emoji / non-Latin1 (caught live 2026-06-03 on a Gemini `see` reply). Mirror
+    # office_bus.py's reconfigure so any engine's UTF-8 output prints cleanly.
+    if sys.platform == "win32":
+        try:
+            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
+            sys.stderr.reconfigure(encoding="utf-8", errors="replace")
+        except Exception:
+            pass
     _load_dotenv()
     import argparse
     ap = argparse.ArgumentParser(description="Multi-engine brother helper")
@@ -248,6 +320,12 @@ def main(argv=None):
         p.add_argument("--task", required=True)
         p.add_argument("--purpose", default="")
         p.add_argument("packet")
+    ps = sub.add_parser("see")  # Gemini visual: text prompt + inline image(s)
+    ps.add_argument("--task", required=True)
+    ps.add_argument("--purpose", default="")
+    ps.add_argument("--image", action="append", required=True,
+                    help="image path (repeatable for multiple images)")
+    ps.add_argument("packet", help="the text prompt about the image(s)")
     sub.add_parser("wake-start")
     sub.add_parser("status")
     args = ap.parse_args(argv)
@@ -264,16 +342,25 @@ def main(argv=None):
             print(f"  {r['engine']:9} task={r['task']:6} {r['purpose']}")
         return 0
 
-    # Lock across cap-check + log_call so concurrent agents can't both pass
-    # the hard cap before either has written their log row.
+    # CONCURRENCY FIX 2026-06-03: hold the lock ONLY for the atomic cap-check +
+    # slot reservation, then run the (possibly multi-minute) engine call OUTSIDE
+    # the lock. The old code ran dispatch() INSIDE the lock, so one long call
+    # serialized every other engine call globally and deadlocked concurrent
+    # callers (EDEADLK) — it broke Agent 3's live DeepSeek review. Reserving the
+    # row under the lock keeps the no-overshoot guarantee; tokens=0 marks it a
+    # reservation (cap counts rows, not tokens — token size is informational).
+    engine_name = {"grunt": "deepseek", "review": "codex",
+                   "read": "gemini", "see": "gemini"}[args.cmd]
     with _ledger_lock():
         ok, msg = check_cap(args.task, cfg)
         if msg:
             print(msg, file=sys.stderr)
         if not ok:
             return 2
-        out = dispatch(args.cmd, args.packet, args.task, args.purpose, cfg)
-        print(out)
+        log_call(engine_name, 0, args.purpose, args.task)  # reserve the slot
+    out = dispatch(args.cmd, args.packet, args.task, args.purpose, cfg,
+                   images=getattr(args, "image", None), log=False)
+    print(out)
     return 0
 
 

codex
I’ll review this as read-only and verify the actual file around the diff, especially the lock/logging path and any helper behavior that the excerpt depends on.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'rg -n "def (_ledger_lock|check_cap|log_call|dispatch|call_gemini|call_gemini_visual|_gemini_request|main)|ENGINE_DRY_RUN|wake-start|status|ledger" scripts/engines/engine.py scripts/engines -S' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content -Path .\\scripts\\engines\\engines.config.json" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content -Path .\\scripts\\engines\\engine.py" in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 4012ms:
scripts/engines/engine.py:11:LEDGER_PATH = os.path.join(HERE, ".ledger.jsonl")
scripts/engines/engine.py:35:def _ledger_lock():
scripts/engines/engine.py:72:def _read_ledger():
scripts/engines/engine.py:87:def ledger_rows_since_wake():
scripts/engines/engine.py:88:    rows = _read_ledger()
scripts/engines/engine.py:92:        if r.get("event") == "wake-start" and r.get("agent") == agent:
scripts/engines/engine.py:98:def log_call(engine_name, tokens, purpose, task):
scripts/engines/engine.py:109:        f.write(json.dumps({"event": "wake-start", "ts": int(time.time()),
scripts/engines/engine.py:113:def check_cap(task, cfg):
scripts/engines/engine.py:115:    calls = [r for r in ledger_rows_since_wake() if r.get("event") == "call"]
scripts/engines/engine.py:167:    if os.environ.get("ENGINE_DRY_RUN") == "1":
scripts/engines/engine.py:195:    if os.environ.get("ENGINE_DRY_RUN") == "1":
scripts/engines/engine.py:219:def _gemini_request(parts, cfg):
scripts/engines/engine.py:243:def call_gemini(packet, cfg):
scripts/engines/engine.py:244:    if os.environ.get("ENGINE_DRY_RUN") == "1":
scripts/engines/engine.py:249:def call_gemini_visual(image_paths, prompt, cfg):
scripts/engines/engine.py:253:    if os.environ.get("ENGINE_DRY_RUN") == "1":
scripts/engines/engine.py:278:def dispatch(cmd, packet, task, purpose, cfg, images=None, log=True):
scripts/engines/engine.py:304:def main(argv=None):
scripts/engines/engine.py:329:    sub.add_parser("wake-start")
scripts/engines/engine.py:330:    sub.add_parser("status")
scripts/engines/engine.py:334:    if args.cmd == "wake-start":
scripts/engines/engine.py:336:    if args.cmd == "status":
scripts/engines/engine.py:337:        calls = [r for r in ledger_rows_since_wake() if r.get("event") == "call"]
scripts/engines/engine.py:354:    with _ledger_lock():
scripts/engines\engine.py:11:LEDGER_PATH = os.path.join(HERE, ".ledger.jsonl")
scripts/engines\engine.py:35:def _ledger_lock():
scripts/engines\engine.py:72:def _read_ledger():
scripts/engines\engine.py:87:def ledger_rows_since_wake():
scripts/engines\engine.py:88:    rows = _read_ledger()
scripts/engines\engine.py:92:        if r.get("event") == "wake-start" and r.get("agent") == agent:
scripts/engines\engine.py:98:def log_call(engine_name, tokens, purpose, task):
scripts/engines\engine.py:109:        f.write(json.dumps({"event": "wake-start", "ts": int(time.time()),
scripts/engines\engine.py:113:def check_cap(task, cfg):
scripts/engines\engine.py:115:    calls = [r for r in ledger_rows_since_wake() if r.get("event") == "call"]
scripts/engines\engine.py:167:    if os.environ.get("ENGINE_DRY_RUN") == "1":
scripts/engines\engine.py:195:    if os.environ.get("ENGINE_DRY_RUN") == "1":
scripts/engines\engine.py:219:def _gemini_request(parts, cfg):
scripts/engines\engine.py:243:def call_gemini(packet, cfg):
scripts/engines\engine.py:244:    if os.environ.get("ENGINE_DRY_RUN") == "1":
scripts/engines\engine.py:249:def call_gemini_visual(image_paths, prompt, cfg):
scripts/engines\engine.py:253:    if os.environ.get("ENGINE_DRY_RUN") == "1":
scripts/engines\engine.py:278:def dispatch(cmd, packet, task, purpose, cfg, images=None, log=True):
scripts/engines\engine.py:304:def main(argv=None):
scripts/engines\engine.py:329:    sub.add_parser("wake-start")
scripts/engines\engine.py:330:    sub.add_parser("status")
scripts/engines\engine.py:334:    if args.cmd == "wake-start":
scripts/engines\engine.py:336:    if args.cmd == "status":
scripts/engines\engine.py:337:        calls = [r for r in ledger_rows_since_wake() if r.get("event") == "call"]
scripts/engines\engine.py:354:    with _ledger_lock():
scripts/engines\README.md:8:At wake start (once):       python scripts/engines/engine.py wake-start
scripts/engines\README.md:12:Status / spend:             python scripts/engines/engine.py status
scripts/engines\README.md:43:No key leakage in ledger. Both calls logged, 2/25 wake cap.
scripts/engines\gemini_gate.py:14:    ("Output ONLY the HTTP status code in this line as a number: "
scripts/engines\gemini_gate.py:21:def main():
scripts/engines\gemini_gate.py:32:        with engine._ledger_lock():
scripts/engines\test_engine.py:25:        open(self.tmp.name, "w").close()  # empty ledger
scripts/engines\test_engine.py:36:        rows = engine.ledger_rows_since_wake()
scripts/engines\test_engine.py:65:        rows = engine._read_ledger()
scripts/engines\test_engine.py:72:        # Brother B's wake-start + cap check should be independent.
scripts/engines\test_engine.py:75:        rows = engine.ledger_rows_since_wake()
scripts/engines\test_engine.py:167:        os.environ["ENGINE_DRY_RUN"] = "1"
scripts/engines\test_engine.py:171:        os.environ.pop("ENGINE_DRY_RUN", None)
scripts/engines\test_engine.py:176:        calls = [r for r in engine.ledger_rows_since_wake() if r.get("event") == "call"]
scripts/engines\test_engine.py:192:        os.environ.pop("ENGINE_DRY_RUN", None)
scripts/engines\test_engine.py:205:        os.environ.pop("ENGINE_DRY_RUN", None)
scripts/engines\test_engine.py:215:        os.environ.pop("ENGINE_DRY_RUN", None)
scripts/engines\test_engine.py:230:    def test_ledger_lock_context_manager(self):
scripts/engines\test_engine.py:231:        with engine._ledger_lock():
scripts/engines\test_engine.py:257:            [r for r in engine.ledger_rows_since_wake()
scripts/engines\test_engine.py:263:            [r for r in engine.ledger_rows_since_wake()

 succeeded in 4340ms:
{
  "deepseek": {
    "base_url": "https://api.deepseek.com/anthropic",
    "model": "deepseek-v4-pro"
  },
  "codex": {
    "model": "gpt-5.5"
  },
  "gemini": {
    "url": "https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent",
    "model": "gemini-2.5-flash",
    "enabled": true
  },
  "caps": {
    "per_wake_hard": 25,
    "per_task_soft": 8,
    "max_packet_chars": 8000
  },
  "timeouts": {
    "deepseek": 300,
    "codex": 300,
    "gemini": 120
  }
}

 succeeded in 9994ms:
#!/usr/bin/env python3
"""Multi-engine brother helper: bare-tool access to DeepSeek/Codex/Gemini.

Spec: docs/superpowers/specs/2026-06-01-multi-engine-brother-design.md
Keys come from environment only (DEEPSEEK_API_KEY, GEMINI_API_KEY). Never logged.
"""
import os, sys, json, time, subprocess, tempfile, urllib.request, urllib.error, contextlib, base64

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(HERE, "engines.config.json")
LEDGER_PATH = os.path.join(HERE, ".ledger.jsonl")
LOCK_PATH = LEDGER_PATH + ".lock"
ENV_PATH = os.path.join(HERE, ".env")


def _load_dotenv(path=None):
    """Fill engine keys from a gitignored scripts/engines/.env (KEY=value lines)
    so every brother's tab works with no per-session export. The REAL environment
    always wins â€” the file only sets what is currently unset."""
    path = path or ENV_PATH
    if not os.path.exists(path):
        return
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, val = line.partition("=")
            key, val = key.strip(), val.strip().strip('"').strip("'")
            if key and key not in os.environ:
                os.environ[key] = val


@contextlib.contextmanager
def _ledger_lock():
    """Cross-process file lock so cap-check + log are atomic."""
    fd = os.open(LOCK_PATH, os.O_CREAT | os.O_RDWR, 0o644)
    try:
        if sys.platform == "win32":
            import msvcrt
            msvcrt.locking(fd, msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(fd, fcntl.LOCK_EX)
        yield
    finally:
        if sys.platform == "win32":
            import msvcrt
            try:
                msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
            except Exception:
                pass
        else:
            import fcntl
            fcntl.flock(fd, fcntl.LOCK_UN)
        os.close(fd)


def _agent_id():
    # ENGINE_AGENT/AGENT_ID is a cooperative guardrail for trusted brothers
    # sharing one machine to catch runaway call loops â€” NOT a security boundary
    # against adversarial agents (it is caller-settable via env).  Two brothers
    # both defaulting to "solo" will collide; Task 9 wires per-brother ids.
    return os.environ.get("ENGINE_AGENT") or os.environ.get("AGENT_ID") or "solo"


def load_config():
    with open(CONFIG_PATH) as f:
        return json.load(f)


def _read_ledger():
    if not os.path.exists(LEDGER_PATH):
        return []
    rows = []
    with open(LEDGER_PATH) as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    rows.append(json.loads(line))
                except json.JSONDecodeError:
                    pass  # skip torn/incomplete lines
    return rows


def ledger_rows_since_wake():
    rows = _read_ledger()
    agent = _agent_id()
    last = 0
    for i, r in enumerate(rows):
        if r.get("event") == "wake-start" and r.get("agent") == agent:
            last = i + 1
    # Only return rows for THIS agent (skip rows tagged for other brothers).
    return [r for r in rows[last:] if r.get("agent") == agent]


def log_call(engine_name, tokens, purpose, task):
    row = {"event": "call", "ts": int(time.time()),
           "engine": engine_name, "tokens": tokens,
           "purpose": purpose, "task": task,
           "agent": _agent_id()}
    with open(LEDGER_PATH, "a") as f:
        f.write(json.dumps(row) + "\n")


def mark_wake():
    with open(LEDGER_PATH, "a") as f:
        f.write(json.dumps({"event": "wake-start", "ts": int(time.time()),
                            "agent": _agent_id()}) + "\n")


def check_cap(task, cfg):
    """Return (allowed: bool, message: str|None)."""
    calls = [r for r in ledger_rows_since_wake() if r.get("event") == "call"]
    wake_count = len(calls)
    task_count = len([r for r in calls if r.get("task") == task])
    hard = cfg["caps"]["per_wake_hard"]
    soft = cfg["caps"]["per_task_soft"]
    if wake_count >= hard:
        return (False, f"STOP: per-wake hard cap {hard} reached ({wake_count} calls). "
                       f"Reconsider before spending more.")
    if task_count >= soft:
        return (True, f"WARN: per-task soft cap {soft} reached for task '{task}' "
                      f"({task_count}). Continuing, but check scope.")
    return (True, None)


def require_key(env_name):
    key = os.environ.get(env_name, "").strip()
    if not key:
        raise RuntimeError(f"{env_name} not set in environment. "
                           f"Refusing to call (key hygiene).")
    return key


def guard_packet(packet, cfg):
    limit = cfg["caps"]["max_packet_chars"]
    if len(packet) > limit:
        raise ValueError(f"packet {len(packet)} chars exceeds limit {limit}. "
                         f"Hand a SMALLER slice (small-context rule).")


def parse_gemini(data):
    return data["candidates"][0]["content"]["parts"][0]["text"].strip()


def parse_codex(stdout):
    """codex exec prints a banner, then a lone 'codex' line, then the answer,
    then 'tokens used'. Extract the answer between them."""
    lines = stdout.splitlines()
    start = None
    for i, ln in enumerate(lines):
        if ln.strip() == "codex":
            start = i + 1  # last 'codex' marker wins
    if start is None:
        return stdout.strip()
    answer = []
    for ln in lines[start:]:
        if ln.strip() == "tokens used":
            break
        answer.append(ln)
    return "\n".join(answer).strip()


def call_deepseek(packet, cfg):
    if os.environ.get("ENGINE_DRY_RUN") == "1":
        return "DRY:deepseek:" + packet[:20]
    scratch = tempfile.mkdtemp(prefix="ds_engine_")
    env = dict(os.environ)
    env.update({
        "CLAUDE_CONFIG_DIR": os.path.join(scratch, ".cfg"),
        "ANTHROPIC_BASE_URL": cfg["deepseek"]["base_url"],
        "ANTHROPIC_AUTH_TOKEN": require_key("DEEPSEEK_API_KEY"),
        "ANTHROPIC_API_KEY": "",
        "ANTHROPIC_MODEL": cfg["deepseek"]["model"],
        "ANTHROPIC_DEFAULT_OPUS_MODEL": cfg["deepseek"]["model"],
    })
    cli = "claude.cmd" if sys.platform == "win32" else "claude"
    # Feed the packet via STDIN, not argv: a multiline arg passed to the Windows
    # .cmd shim gets truncated at the first newline (caught by Agent 1 2026-06-01).
    proc = subprocess.run(
        [cli, "-p", "--model", cfg["deepseek"]["model"]],
        input=packet, cwd=scratch, env=env, capture_output=True, text=True,
        encoding="utf-8", errors="replace",
        timeout=cfg["timeouts"]["deepseek"])
    if proc.returncode != 0:
        tail = proc.stderr.strip()[-200:] if proc.stderr else "(no stderr)"
        raise RuntimeError(
            f"DeepSeek (claude) exited {proc.returncode}: {tail}")
    return proc.stdout.strip()


def call_codex(packet, cfg):
    if os.environ.get("ENGINE_DRY_RUN") == "1":
        return "DRY:codex:" + packet[:20]
    cli = "codex.cmd" if sys.platform == "win32" else "codex"
    # Feed the packet via STDIN, not argv: codex exec reads the prompt from stdin,
    # and a multiline argv gets truncated at the first newline through the Windows
    # .cmd shim (caught by Agent 1 2026-06-01).
    proc = subprocess.run(
        [cli, "exec"],
        input=packet, capture_output=True, text=True,
        encoding="utf-8", errors="replace",
        timeout=cfg["timeouts"]["codex"])
    if proc.returncode != 0:
        tail = proc.stderr.strip()[-200:] if proc.stderr else "(no stderr)"
        raise RuntimeError(
            f"Codex exited {proc.returncode}: {tail}")
    return parse_codex(proc.stdout)


# Gemini multimodal mime map (the types generateContent accepts inline).
_GEMINI_MIME = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
                ".webp": "image/webp", ".heic": "image/heic", ".heif": "image/heif"}
_GEMINI_INLINE_LIMIT = 4 * 1024 * 1024  # 4 MB total inline cap; above this -> Files API


def _gemini_request(parts, cfg):
    """POST one contents[].parts[] payload to generateContent, with retry on
    transient 503/429 (Gemini Flash returns 503 under load â€” caught live
    2026-06-03). Auth via x-goog-api-key header."""
    key = require_key("GEMINI_API_KEY")
    url = cfg["gemini"]["url"].format(model=cfg["gemini"]["model"])
    body = json.dumps({"contents": [{"parts": parts}]}).encode()
    last = None
    for attempt in range(4):
        req = urllib.request.Request(
            url, data=body,
            headers={"Content-Type": "application/json", "x-goog-api-key": key})
        try:
            with urllib.request.urlopen(req, timeout=cfg["timeouts"]["gemini"]) as resp:
                return parse_gemini(json.load(resp))
        except urllib.error.HTTPError as e:
            last = e
            if e.code in (429, 503) and attempt < 3:
                time.sleep(2 * (attempt + 1))  # 2s, 4s, 6s backoff
                continue
            raise
    raise last


def call_gemini(packet, cfg):
    if os.environ.get("ENGINE_DRY_RUN") == "1":
        return "DRY:gemini:" + packet[:20]
    return _gemini_request([{"text": packet}], cfg)


def call_gemini_visual(image_paths, prompt, cfg):
    """`see` lane â€” text prompt + one or more inline images. Gemini is the only
    engine with eyes (Codex/DeepSeek are text). Images ride as base64 inline_data
    parts; total must stay under the 4 MB inline cap (Files API above that)."""
    if os.environ.get("ENGINE_DRY_RUN") == "1":
        return "DRY:gemini-visual:" + (prompt or "")[:20]
    if not image_paths:
        raise ValueError("see: at least one --image is required")
    parts = [{"text": prompt}]
    total = 0
    for p in image_paths:
        ext = os.path.splitext(p)[1].lower()
        mime = _GEMINI_MIME.get(ext)
        if not mime:
            raise ValueError(
                f"see: unsupported image type '{ext}' for {p} "
                f"(allowed: {sorted(_GEMINI_MIME)})")
        with open(p, "rb") as f:
            raw = f.read()
        total += len(raw)
        if total > _GEMINI_INLINE_LIMIT:
            raise ValueError(
                f"see: inline images exceed 4 MB total ({total} bytes) â€” "
                f"use fewer/smaller images (Files API not wired)")
        parts.append({"inline_data": {"mime_type": mime,
                                      "data": base64.b64encode(raw).decode()}})
    return _gemini_request(parts, cfg)


def dispatch(cmd, packet, task, purpose, cfg, images=None, log=True):
    guard_packet(packet, cfg)
    if cmd == "grunt":
        out = call_deepseek(packet, cfg); name = "deepseek"
    elif cmd == "review":
        out = call_codex(packet, cfg); name = "codex"
    elif cmd == "read":
        if not cfg["gemini"].get("enabled"):
            raise RuntimeError("Gemini disabled (reliability gate not passed). "
                               "Read directly on Claude, or run gemini_gate.py.")
        out = call_gemini(packet, cfg); name = "gemini"
    elif cmd == "see":
        if not cfg["gemini"].get("enabled"):
            raise RuntimeError("Gemini disabled (reliability gate not passed). "
                               "Run gemini_gate.py to enable.")
        out = call_gemini_visual(images, packet, cfg); name = "gemini"
    else:
        raise ValueError(f"unknown command: {cmd}")
    # log defaults True so a direct dispatch() call records its own row (tests +
    # the gate rely on this). main() passes log=False because it has already
    # reserved the cap slot under the lock before calling out-of-lock.
    if log:
        log_call(name, len(out), purpose, task)
    return out


def main(argv=None):
    # Windows console is cp1252 and crashes on engine output containing arrows /
    # emoji / non-Latin1 (caught live 2026-06-03 on a Gemini `see` reply). Mirror
    # office_bus.py's reconfigure so any engine's UTF-8 output prints cleanly.
    if sys.platform == "win32":
        try:
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
            sys.stderr.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    _load_dotenv()
    import argparse
    ap = argparse.ArgumentParser(description="Multi-engine brother helper")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("grunt", "review", "read"):
        p = sub.add_parser(name)
        p.add_argument("--task", required=True)
        p.add_argument("--purpose", default="")
        p.add_argument("packet")
    ps = sub.add_parser("see")  # Gemini visual: text prompt + inline image(s)
    ps.add_argument("--task", required=True)
    ps.add_argument("--purpose", default="")
    ps.add_argument("--image", action="append", required=True,
                    help="image path (repeatable for multiple images)")
    ps.add_argument("packet", help="the text prompt about the image(s)")
    sub.add_parser("wake-start")
    sub.add_parser("status")
    args = ap.parse_args(argv)
    cfg = load_config()

    if args.cmd == "wake-start":
        mark_wake(); print("wake marked"); return 0
    if args.cmd == "status":
        calls = [r for r in ledger_rows_since_wake() if r.get("event") == "call"]
        tot = sum(r.get("tokens", 0) for r in calls)
        print(f"calls this wake: {len(calls)}/{cfg['caps']['per_wake_hard']} "
              f"| out-chars: {tot}")
        for r in calls:
            print(f"  {r['engine']:9} task={r['task']:6} {r['purpose']}")
        return 0

    # CONCURRENCY FIX 2026-06-03: hold the lock ONLY for the atomic cap-check +
    # slot reservation, then run the (possibly multi-minute) engine call OUTSIDE
    # the lock. The old code ran dispatch() INSIDE the lock, so one long call
    # serialized every other engine call globally and deadlocked concurrent
    # callers (EDEADLK) â€” it broke Agent 3's live DeepSeek review. Reserving the
    # row under the lock keeps the no-overshoot guarantee; tokens=0 marks it a
    # reservation (cap counts rows, not tokens â€” token size is informational).
    engine_name = {"grunt": "deepseek", "review": "codex",
                   "read": "gemini", "see": "gemini"}[args.cmd]
    with _ledger_lock():
        ok, msg = check_cap(args.task, cfg)
        if msg:
            print(msg, file=sys.stderr)
        if not ok:
            return 2
        log_call(engine_name, 0, args.purpose, args.task)  # reserve the slot
    out = dispatch(args.cmd, args.packet, args.task, args.purpose, cfg,
                   images=getattr(args, "image", None), log=False)
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

codex
The file matches the supplied diff. I’m checking the surrounding tests and line-numbered regions now so any finding is tied to actual behavior rather than only the pasted patch.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command '$i=1; Get-Content .'"\\scripts\\engines\\test_engine.py | ForEach-Object { '{0,4}: {1}' -f "'$i, $_; $i++ }' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command '$i=1; Get-Content .'"\\scripts\\engines\\engine.py | ForEach-Object { '{0,4}: {1}' -f "'$i, $_; $i++ }' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command '$i=1; Get-Content .'"\\scripts\\engines\\gemini_gate.py | ForEach-Object { '{0,4}: {1}' -f "'$i, $_; $i++ }' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content .\\scripts\\engines\\README.md" in C:\Users\Suprabha\Desktop\Tankoban 2
 declined in 0ms:
`"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command '$i=1; Get-Content .'"\\scripts\\engines\\gemini_gate.py | ForEach-Object { '{0,4}: {1}' -f "'$i, $_; $i++ }'` rejected: blocked by policy
 declined in 0ms:
`"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command '$i=1; Get-Content .'"\\scripts\\engines\\test_engine.py | ForEach-Object { '{0,4}: {1}' -f "'$i, $_; $i++ }'` rejected: blocked by policy
2026-06-03T11:19:14.502087Z ERROR codex_core::tools::router: error=`"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command '$i=1; Get-Content .'"\\scripts\\engines\\gemini_gate.py | ForEach-Object { '{0,4}: {1}' -f "'$i, $_; $i++ }'` rejected: blocked by policy
2026-06-03T11:19:14.502078Z ERROR codex_core::tools::router: error=`"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command '$i=1; Get-Content .'"\\scripts\\engines\\test_engine.py | ForEach-Object { '{0,4}: {1}' -f "'$i, $_; $i++ }'` rejected: blocked by policy
2026-06-03T11:19:14.505025Z ERROR codex_core::tools::router: error=`"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command '$i=1; Get-Content .'"\\scripts\\engines\\engine.py | ForEach-Object { '{0,4}: {1}' -f "'$i, $_; $i++ }'` rejected: blocked by policy
 declined in 0ms:
`"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command '$i=1; Get-Content .'"\\scripts\\engines\\engine.py | ForEach-Object { '{0,4}: {1}' -f "'$i, $_; $i++ }'` rejected: blocked by policy
 succeeded in 3741ms:
# scripts/engines â€” the multi-engine brother helper

One Claude brain (the brother), three engines on tap. Spec:
`docs/superpowers/specs/2026-06-01-multi-engine-brother-design.md`.

## Use it
Set your identity first:    $env:ENGINE_AGENT="agentN"   (per-brother cap isolation; defaults to "solo" if unset)
At wake start (once):       python scripts/engines/engine.py wake-start
Grunt (DeepSeek):           python scripts/engines/engine.py grunt  --task T1 --purpose "write fn" "<tiny packet>"
Review (Codex):             python scripts/engines/engine.py review --task T1 --purpose "sign off" "<diff + ask>"
Read  (Gemini, if enabled): python scripts/engines/engine.py read   --task T1 --purpose "extract"  "<blob + ask>"
Status / spend:             python scripts/engines/engine.py status

## The routing rule (the brother decides, per task)
- grunt  -> mechanical edit / bulk parse / boilerplate            -> DeepSeek
- review -> a load-bearing decision/diff needs second eyes (rare) -> Codex
- read   -> chew a big log/file, hand back the slice              -> Gemini (if enabled)
- think  -> design / judgment / identity / important code         -> keep on Claude

## Contract
- Keys come from env OR a gitignored `scripts/engines/.env` (auto-loaded; real env wins). Never commit them.
- Packets must be tiny (small-context rule); oversized packets are refused.
- Hard cap ~25 calls/wake, soft ~8/task. Hitting the hard cap STOPS and asks.

## Writing good packets (the engine has NO project context â€” by design)
The bare grunt/read engines run context-starved (clean scratch dir, no CLAUDE.md)
to stay cheap and fast. So the packet must be **self-contained**:
- Spell out the exact output you want ("output ONLY the function in a code block").
- One small job per call. Need a bit of context? Paste the ONE relevant snippet â€”
  never the whole file/repo (that's the slow timeout path).
- Always verify what comes back; the verify/review gate catches divergence before
  it touches code.

Multiline packets are fine (fed via stdin as of 2026-06-01). NOTE: an earlier
"use single-line plain prose, avoid trigger words" guideline was a misdiagnosis of
the multiline-truncation bug Agent 1 found â€” the engine was only ever receiving the
first line. That's fixed; clarity still helps, but newlines are no longer the enemy.

## Live smoke 2026-06-01

DeepSeek grunt: produced correct `clampVolume` (ternary with correct bounds).
Codex review: `APPROVE: Correctly clamps values below 0 to 0, above 100 to 100, and preserves in-range values.`
No key leakage in ledger. Both calls logged, 2/25 wake cap.
Platform fix: `claude.cmd` / `codex.cmd` on Windows (Python subprocess requires .cmd extension).

codex
One line-number command tripped the shell policy because of nested formatting quotes; I’ll use simpler reads and targeted `Select-String` instead.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content .\\scripts\\engines\\test_engine.py" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Select-String -Path .\\scripts\\engines\\engine.py -Pattern \"def _gemini_request|def call_gemini_visual|def dispatch|def main|with _ledger_lock|log_call\\(engine_name|out = dispatch|if log:|def check_cap|def ledger_rows_since_wake|def log_call\" -Context 3,8" in C:\Users\Suprabha\Desktop\Tankoban 2
2026-06-03T11:19:27.675731Z ERROR codex_core::tools::router: error=`"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Select-String -Path .\\scripts\\engines\\engine.py -Pattern \"def _gemini_request|def call_gemini_visual|def dispatch|def main|with _ledger_lock|log_call\\(engine_name|out = dispatch|if log:|def check_cap|def ledger_rows_since_wake|def log_call\" -Context 3,8"` rejected: blocked by policy
 declined in 0ms:
`"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Select-String -Path .\\scripts\\engines\\engine.py -Pattern \"def _gemini_request|def call_gemini_visual|def dispatch|def main|with _ledger_lock|log_call\\(engine_name|out = dispatch|if log:|def check_cap|def ledger_rows_since_wake|def log_call\" -Context 3,8"` rejected: blocked by policy
2026-06-03T11:19:27.831371Z ERROR codex_core::tools::router: error=`"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Select-String -Path .\\scripts\\engines\\*.py -Pattern \"dispatch\\(|call_gemini_visual|_gemini_request|ENGINE_DRY_RUN|see|read\" -Context 2,4"` rejected: blocked by policy
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Select-String -Path .\\scripts\\engines\\*.py -Pattern \"dispatch\\(|call_gemini_visual|_gemini_request|ENGINE_DRY_RUN|see|read\" -Context 2,4" in C:\Users\Suprabha\Desktop\Tankoban 2
 declined in 0ms:
`"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Select-String -Path .\\scripts\\engines\\*.py -Pattern \"dispatch\\(|call_gemini_visual|_gemini_request|ENGINE_DRY_RUN|see|read\" -Context 2,4"` rejected: blocked by policy
 succeeded in 8591ms:
import os, json, tempfile, unittest, importlib.util
from unittest.mock import patch, MagicMock

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("engine", os.path.join(HERE, "engine.py"))
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)

CFG = {"caps": {"per_wake_hard": 3, "per_task_soft": 2, "max_packet_chars": 100}}

# Full config for live-caller tests (needs deepseek/codex/gemini/timeouts keys).
CFG_FULL = {
    "caps": {"per_wake_hard": 3, "per_task_soft": 2, "max_packet_chars": 100},
    "deepseek": {"base_url": "https://api.deepseek.com/anthropic", "model": "test"},
    "codex": {"model": "test"},
    "gemini": {"url": "https://example.com/{model}:generateContent", "model": "test", "enabled": True},
    "timeouts": {"deepseek": 30, "codex": 30, "gemini": 30},
}

class CapTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".jsonl")
        self.tmp.close()
        engine.LEDGER_PATH = self.tmp.name
        open(self.tmp.name, "w").close()  # empty ledger
        os.environ["ENGINE_AGENT"] = "test-agent"

    def tearDown(self):
        os.unlink(self.tmp.name)
        os.environ.pop("ENGINE_AGENT", None)

    def test_wake_marker_resets_count(self):
        for _ in range(2):
            engine.log_call("deepseek", 10, "x", "T1")
        engine.mark_wake()
        rows = engine.ledger_rows_since_wake()
        calls = [r for r in rows if r.get("event") == "call"]
        self.assertEqual(len(calls), 0)

    def test_hard_cap_blocks(self):
        for _ in range(3):
            engine.log_call("deepseek", 10, "x", "T1")
        ok, msg = engine.check_cap("T1", CFG)
        self.assertFalse(ok)
        self.assertIn("hard cap", msg)

    def test_soft_cap_warns_but_allows(self):
        for _ in range(2):
            engine.log_call("deepseek", 10, "x", "T1")
        ok, msg = engine.check_cap("T1", CFG)
        self.assertTrue(ok)
        self.assertIn("soft cap", msg)

    def test_under_cap_clean(self):
        engine.log_call("deepseek", 10, "x", "T1")
        ok, msg = engine.check_cap("T1", CFG)
        self.assertTrue(ok)
        self.assertIsNone(msg)

    def test_torn_lines_are_skipped(self):
        with open(self.tmp.name, "w") as f:
            f.write('{"event":"call","engine":"d","tokens":1,"task":"T1","agent":"test-agent"}\n')
            f.write('this is torn garbage\n')
            f.write('{"event":"call","engine":"c","tokens":2,"task":"T1","agent":"test-agent"}\n')
        rows = engine._read_ledger()
        self.assertEqual(len(rows), 2)

    def test_per_agent_isolation(self):
        # Brother A logs 3 calls (at hard cap).
        for _ in range(3):
            engine.log_call("deepseek", 10, "x", "T1")
        # Brother B's wake-start + cap check should be independent.
        os.environ["ENGINE_AGENT"] = "brother-b"
        engine.mark_wake()
        rows = engine.ledger_rows_since_wake()
        calls = [r for r in rows if r.get("event") == "call"]
        self.assertEqual(len(calls), 0)
        ok, _ = engine.check_cap("T1", CFG)
        self.assertTrue(ok)
        os.environ["ENGINE_AGENT"] = "test-agent"  # restore

class GuardTest(unittest.TestCase):
    def test_packet_too_big_raises(self):
        with self.assertRaises(ValueError):
            engine.guard_packet("x" * 101, CFG)

    def test_packet_ok(self):
        engine.guard_packet("small", CFG)  # no raise

    def test_missing_key_raises(self):
        os.environ.pop("FAKE_KEY_XYZ", None)
        with self.assertRaises(RuntimeError):
            engine.require_key("FAKE_KEY_XYZ")

    def test_present_key_returns(self):
        os.environ["FAKE_KEY_XYZ"] = "sk-test"
        try:
            self.assertEqual(engine.require_key("FAKE_KEY_XYZ"), "sk-test")
        finally:
            os.environ.pop("FAKE_KEY_XYZ", None)

GEMINI_FIXTURE = {
    "candidates": [
        {"content": {"parts": [{"text": "3.2"}], "role": "model"},
         "finishReason": "STOP", "index": 0}
    ],
    "usageMetadata": {"totalTokenCount": 159},
    "modelVersion": "gemini-2.5-flash",
}

CODEX_FIXTURE = """Reading additional input from stdin...
OpenAI Codex v0.131.0
--------
workdir: C:\\Users\\Suprabha\\Desktop\\Tankoban 2
model: gpt-5.5
--------
user
Review this C++ function...
codex
APPROVE clamps values below 0 to 0, above 100 to 100, and returns in-range values unchanged.
tokens used
15,018
"""

class ParserTest(unittest.TestCase):
    def test_parse_gemini(self):
        self.assertEqual(engine.parse_gemini(GEMINI_FIXTURE), "3.2")

    def test_parse_codex_extracts_answer(self):
        out = engine.parse_codex(CODEX_FIXTURE)
        self.assertEqual(
            out,
            "APPROVE clamps values below 0 to 0, above 100 to 100, "
            "and returns in-range values unchanged.")

    def test_parse_codex_multiline_answer(self):
        sample = "banner\ncodex\nline one\nline two\ntokens used\n42\n"
        self.assertEqual(engine.parse_codex(sample), "line one\nline two")

class DotenvTest(unittest.TestCase):
    def test_loads_unset_keys_only(self):
        import tempfile
        fd, path = tempfile.mkstemp(suffix=".env")
        with os.fdopen(fd, "w") as f:
            f.write("# comment\n\nFROM_FILE_KEY=file-val\nALREADY_SET_KEY=file-should-not-win\n")
        os.environ.pop("FROM_FILE_KEY", None)
        os.environ["ALREADY_SET_KEY"] = "env-wins"
        try:
            engine._load_dotenv(path)
            self.assertEqual(os.environ.get("FROM_FILE_KEY"), "file-val")
            self.assertEqual(os.environ.get("ALREADY_SET_KEY"), "env-wins")
        finally:
            os.unlink(path)
            os.environ.pop("FROM_FILE_KEY", None)
            os.environ.pop("ALREADY_SET_KEY", None)

    def test_missing_file_is_noop(self):
        engine._load_dotenv("/no/such/.env")  # must not raise


class DispatchTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".jsonl")
        self.tmp.close()
        engine.LEDGER_PATH = self.tmp.name
        open(self.tmp.name, "w").close()
        os.environ["ENGINE_DRY_RUN"] = "1"

    def tearDown(self):
        os.unlink(self.tmp.name)
        os.environ.pop("ENGINE_DRY_RUN", None)

    def test_grunt_dry_runs_and_logs(self):
        out = engine.dispatch("grunt", "tiny packet", "T1", "p", CFG)
        self.assertTrue(out.startswith("DRY:deepseek"))
        calls = [r for r in engine.ledger_rows_since_wake() if r.get("event") == "call"]
        self.assertEqual(calls[-1]["engine"], "deepseek")

    def test_review_dry_runs(self):
        out = engine.dispatch("review", "diff", "T1", "p", CFG)
        self.assertTrue(out.startswith("DRY:codex"))

    def test_read_blocked_when_gemini_disabled(self):
        cfg = dict(CFG)
        cfg["gemini"] = {"enabled": False}
        with self.assertRaises(RuntimeError):
            engine.dispatch("read", "blob", "T1", "p", cfg)

    @patch("engine.subprocess.run")
    def test_grunt_nonzero_returncode_raises(self, mock_run):
        mock_run.return_value = MagicMock(returncode=1, stdout="", stderr="claude: error")
        os.environ.pop("ENGINE_DRY_RUN", None)
        os.environ["DEEPSEEK_API_KEY"] = "sk-test"
        try:
            with self.assertRaises(RuntimeError) as ctx:
                engine.call_deepseek("packet", CFG_FULL)
            self.assertIn("exited 1", str(ctx.exception))
            self.assertIn("claude: error", str(ctx.exception))
        finally:
            os.environ.pop("DEEPSEEK_API_KEY", None)

    @patch("engine.subprocess.run")
    def test_codex_nonzero_returncode_raises(self, mock_run):
        mock_run.return_value = MagicMock(returncode=2, stdout="", stderr="codex: fatal")
        os.environ.pop("ENGINE_DRY_RUN", None)
        with self.assertRaises(RuntimeError) as ctx:
            engine.call_codex("packet", CFG_FULL)
        self.assertIn("exited 2", str(ctx.exception))

    @patch("engine.subprocess.run")
    def test_packet_fed_via_stdin_not_argv(self, mock_run):
        """Multiline packets ride stdin (input=), never argv â€” a multiline arg to
        the Windows .cmd shim truncates at the first newline. Regression: Agent 1."""
        mock_run.return_value = MagicMock(returncode=0, stdout="ok\ncodex\nok", stderr="")
        os.environ.pop("ENGINE_DRY_RUN", None)
        ml = "line one\nline two\nline three"
        os.environ["DEEPSEEK_API_KEY"] = "sk-test"
        try:
            engine.call_deepseek(ml, CFG_FULL)
            args, kwargs = mock_run.call_args
            self.assertEqual(kwargs.get("input"), ml, "deepseek: packet via stdin")
            self.assertNotIn(ml, args[0], "deepseek: packet NOT in argv")
        finally:
            os.environ.pop("DEEPSEEK_API_KEY", None)
        engine.call_codex(ml, CFG_FULL)
        args, kwargs = mock_run.call_args
        self.assertEqual(kwargs.get("input"), ml, "codex: packet via stdin")
        self.assertNotIn(ml, args[0], "codex: packet NOT in argv")

    def test_ledger_lock_context_manager(self):
        with engine._ledger_lock():
            self.assertTrue(os.path.exists(engine.LOCK_PATH))
        # Lock file remains on disk (the fd is closed, not the file removed).
        self.assertTrue(os.path.exists(engine.LOCK_PATH))
        os.unlink(engine.LOCK_PATH)


class GateTest(unittest.TestCase):
    """Prove the Gemini gate does NOT reset the caller's cap counter."""

    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".jsonl")
        self.tmp.close()
        engine.LEDGER_PATH = self.tmp.name
        open(self.tmp.name, "w").close()
        os.environ["ENGINE_AGENT"] = "solo"

    def tearDown(self):
        os.unlink(self.tmp.name)
        os.environ.pop("ENGINE_AGENT", None)

    def test_gate_probe_does_not_write_wake_marker(self):
        # Simulate existing calls from the solo user.
        engine.log_call("deepseek", 10, "existing work", "T1")
        engine.log_call("codex", 5, "existing review", "T1")
        calls_before = len(
            [r for r in engine.ledger_rows_since_wake()
             if r.get("event") == "call"])
        self.assertEqual(calls_before, 2)
        # The gate probe logs a gemini call but does NOT call mark_wake.
        engine.log_call("gemini", 3, "gate-probe", "GEMINI_GATE")
        calls_after = len(
            [r for r in engine.ledger_rows_since_wake()
             if r.get("event") == "call"])
        # Still 3 calls (2 original + 1 gate probe); wake markers unchanged.
        self.assertEqual(calls_after, 3)

if __name__ == "__main__":
    unittest.main()

codex
The existing tests cover direct `dispatch()` logging and basic dry-run behavior, but they do not exercise the new reservation-vs-dispatch path in `main()` or the see/retry additions. I’m doing a focused static pass for those gaps.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n \"dispatch\\(|call_gemini_visual|_gemini_request|ENGINE_DRY_RUN|see|read|main\\(\" .\\scripts\\engines\\test_engine.py .\\scripts\\engines\\gemini_gate.py .\\scripts\\engines\\README.md" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n \"def _gemini_request|def call_gemini_visual|def dispatch|def main|with _ledger_lock|log_call\\(engine_name|out = dispatch|if log:|def check_cap|def ledger_rows_since_wake|def log_call|cmd == \\\"see\\\"|ENGINE_DRY_RUN\" .\\scripts\\engines\\engine.py" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n \"_ledger_lock|msvcrt.locking|log_call|check_cap|ledger_rows_since_wake|LOCK_PATH|LEDGER_PATH\" .\\scripts\\engines\\engine.py" in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 2790ms:
11:LEDGER_PATH = os.path.join(HERE, ".ledger.jsonl")
12:LOCK_PATH = LEDGER_PATH + ".lock"
35:def _ledger_lock():
37:    fd = os.open(LOCK_PATH, os.O_CREAT | os.O_RDWR, 0o644)
41:            msvcrt.locking(fd, msvcrt.LK_LOCK, 1)
50:                msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
73:    if not os.path.exists(LEDGER_PATH):
76:    with open(LEDGER_PATH) as f:
87:def ledger_rows_since_wake():
98:def log_call(engine_name, tokens, purpose, task):
103:    with open(LEDGER_PATH, "a") as f:
108:    with open(LEDGER_PATH, "a") as f:
113:def check_cap(task, cfg):
115:    calls = [r for r in ledger_rows_since_wake() if r.get("event") == "call"]
300:        log_call(name, len(out), purpose, task)
337:        calls = [r for r in ledger_rows_since_wake() if r.get("event") == "call"]
354:    with _ledger_lock():
355:        ok, msg = check_cap(args.task, cfg)
360:        log_call(engine_name, 0, args.purpose, args.task)  # reserve the slot

2026-06-03T11:19:48.992718Z ERROR codex_core::tools::router: error=Exit code: 1
Wall time: 2.9 seconds
Output:
rg: regex parse error:
    (?:def _gemini_request|def call_gemini_visual|def dispatch|def main|with _ledger_lock|log_call\(engine_name|out = dispatch|if log:|def check_cap|def ledger_rows_since_wake|def log_call|cmd == " see\|ENGINE_DRY_RUN .\scripts\engines\engine.py)
                                                                                                                                                                                                                                   ^^
error: unrecognized escape sequence

 exited 1 in 2853ms:
rg: regex parse error:
    (?:def _gemini_request|def call_gemini_visual|def dispatch|def main|with _ledger_lock|log_call\(engine_name|out = dispatch|if log:|def check_cap|def ledger_rows_since_wake|def log_call|cmd == " see\|ENGINE_DRY_RUN .\scripts\engines\engine.py)
                                                                                                                                                                                                                                   ^^
error: unrecognized escape sequence

 succeeded in 3121ms:
.\scripts\engines\README.md:11:Read  (Gemini, if enabled): python scripts/engines/engine.py read   --task T1 --purpose "extract"  "<blob + ask>"
.\scripts\engines\README.md:17:- read   -> chew a big log/file, hand back the slice              -> Gemini (if enabled)
.\scripts\engines\README.md:26:The bare grunt/read engines run context-starved (clean scratch dir, no CLAUDE.md)
.\scripts\engines\test_engine.py:11:# Full config for live-caller tests (needs deepseek/codex/gemini/timeouts keys).
.\scripts\engines\test_engine.py:14:    "deepseek": {"base_url": "https://api.deepseek.com/anthropic", "model": "test"},
.\scripts\engines\test_engine.py:17:    "timeouts": {"deepseek": 30, "codex": 30, "gemini": 30},
.\scripts\engines\test_engine.py:34:            engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:42:            engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:49:            engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:55:        engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:65:        rows = engine._read_ledger()
.\scripts\engines\test_engine.py:71:            engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:167:        os.environ["ENGINE_DRY_RUN"] = "1"
.\scripts\engines\test_engine.py:171:        os.environ.pop("ENGINE_DRY_RUN", None)
.\scripts\engines\test_engine.py:174:        out = engine.dispatch("grunt", "tiny packet", "T1", "p", CFG)
.\scripts\engines\test_engine.py:175:        self.assertTrue(out.startswith("DRY:deepseek"))
.\scripts\engines\test_engine.py:177:        self.assertEqual(calls[-1]["engine"], "deepseek")
.\scripts\engines\test_engine.py:180:        out = engine.dispatch("review", "diff", "T1", "p", CFG)
.\scripts\engines\test_engine.py:183:    def test_read_blocked_when_gemini_disabled(self):
.\scripts\engines\test_engine.py:187:            engine.dispatch("read", "blob", "T1", "p", cfg)
.\scripts\engines\test_engine.py:192:        os.environ.pop("ENGINE_DRY_RUN", None)
.\scripts\engines\test_engine.py:196:                engine.call_deepseek("packet", CFG_FULL)
.\scripts\engines\test_engine.py:205:        os.environ.pop("ENGINE_DRY_RUN", None)
.\scripts\engines\test_engine.py:215:        os.environ.pop("ENGINE_DRY_RUN", None)
.\scripts\engines\test_engine.py:219:            engine.call_deepseek(ml, CFG_FULL)
.\scripts\engines\test_engine.py:221:            self.assertEqual(kwargs.get("input"), ml, "deepseek: packet via stdin")
.\scripts\engines\test_engine.py:222:            self.assertNotIn(ml, args[0], "deepseek: packet NOT in argv")
.\scripts\engines\test_engine.py:254:        engine.log_call("deepseek", 10, "existing work", "T1")
.\scripts\engines\test_engine.py:269:    unittest.main()
.\scripts\engines\gemini_gate.py:2:"""Reliability gate for Gemini-as-reader. Runs N reader-extracts against known
.\scripts\engines\gemini_gate.py:21:def main():
.\scripts\engines\gemini_gate.py:30:        # Mirror dispatch()'s critical-section pattern: check cap + call +
.\scripts\engines\gemini_gate.py:66:    sys.exit(main())

codex
The critical `main()` path now reserves under `_ledger_lock()` and calls `dispatch(..., log=False)` after leaving the block. I’m checking whether any companion path still assumes the old “log after success” semantics or calls engines while holding the same lock.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content .\\scripts\\engines\\gemini_gate.py" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n \"_ledger_lock\\(|dispatch\\(|log_call\\(|check_cap\\(\" .\\scripts\\engines .\\scripts -g \"*.py\"" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n \"def _gemini_request|def call_gemini_visual|def dispatch|def main|with _ledger_lock|log_call|dispatch\\(|cmd ==|ENGINE_DRY_RUN|inline_data|_GEMINI_INLINE_LIMIT|urllib.error.HTTPError\" .\\scripts\\engines\\engine.py" in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 3500ms:
.\scripts\engines\test_engine.py:34:            engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:42:            engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:43:        ok, msg = engine.check_cap("T1", CFG)
.\scripts\engines\test_engine.py:49:            engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:50:        ok, msg = engine.check_cap("T1", CFG)
.\scripts\engines\test_engine.py:55:        engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:56:        ok, msg = engine.check_cap("T1", CFG)
.\scripts\engines\test_engine.py:71:            engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:78:        ok, _ = engine.check_cap("T1", CFG)
.\scripts\engines\test_engine.py:174:        out = engine.dispatch("grunt", "tiny packet", "T1", "p", CFG)
.\scripts\engines\test_engine.py:180:        out = engine.dispatch("review", "diff", "T1", "p", CFG)
.\scripts\engines\test_engine.py:187:            engine.dispatch("read", "blob", "T1", "p", cfg)
.\scripts\engines\test_engine.py:231:        with engine._ledger_lock():
.\scripts\engines\test_engine.py:254:        engine.log_call("deepseek", 10, "existing work", "T1")
.\scripts\engines\test_engine.py:255:        engine.log_call("codex", 5, "existing review", "T1")
.\scripts\engines\test_engine.py:261:        engine.log_call("gemini", 3, "gate-probe", "GEMINI_GATE")
.\scripts\engines\test_engine.py:34:            engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:42:            engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:43:        ok, msg = engine.check_cap("T1", CFG)
.\scripts\engines\test_engine.py:49:            engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:50:        ok, msg = engine.check_cap("T1", CFG)
.\scripts\engines\test_engine.py:55:        engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:56:        ok, msg = engine.check_cap("T1", CFG)
.\scripts\engines\test_engine.py:71:            engine.log_call("deepseek", 10, "x", "T1")
.\scripts\engines\test_engine.py:78:        ok, _ = engine.check_cap("T1", CFG)
.\scripts\engines\test_engine.py:174:        out = engine.dispatch("grunt", "tiny packet", "T1", "p", CFG)
.\scripts\engines\test_engine.py:180:        out = engine.dispatch("review", "diff", "T1", "p", CFG)
.\scripts\engines\test_engine.py:187:            engine.dispatch("read", "blob", "T1", "p", cfg)
.\scripts\engines\test_engine.py:231:        with engine._ledger_lock():
.\scripts\engines\test_engine.py:254:        engine.log_call("deepseek", 10, "existing work", "T1")
.\scripts\engines\test_engine.py:255:        engine.log_call("codex", 5, "existing review", "T1")
.\scripts\engines\test_engine.py:261:        engine.log_call("gemini", 3, "gate-probe", "GEMINI_GATE")
.\scripts\office\office_dispatch.py:230:def _ledger_lock():
.\scripts\office\office_dispatch.py:328:    so it is unit-testable; _dispatch() acts on the verdict."""
.\scripts\office\office_dispatch.py:438:    lk = _ledger_lock()
.\scripts\office\office_dispatch.py:472:def _dispatch(rec):
.\scripts\office\office_dispatch.py:590:                p = _dispatch(rec)
.\scripts\engines\engine.py:35:def _ledger_lock():
.\scripts\engines\engine.py:98:def log_call(engine_name, tokens, purpose, task):
.\scripts\engines\engine.py:113:def check_cap(task, cfg):
.\scripts\engines\engine.py:278:def dispatch(cmd, packet, task, purpose, cfg, images=None, log=True):
.\scripts\engines\engine.py:296:    # log defaults True so a direct dispatch() call records its own row (tests +
.\scripts\engines\engine.py:300:        log_call(name, len(out), purpose, task)
.\scripts\engines\engine.py:347:    # the lock. The old code ran dispatch() INSIDE the lock, so one long call
.\scripts\engines\engine.py:354:    with _ledger_lock():
.\scripts\engines\engine.py:355:        ok, msg = check_cap(args.task, cfg)
.\scripts\engines\engine.py:360:        log_call(engine_name, 0, args.purpose, args.task)  # reserve the slot
.\scripts\engines\engine.py:361:    out = dispatch(args.cmd, args.packet, args.task, args.purpose, cfg,
.\scripts\engines\gemini_gate.py:30:        # Mirror dispatch()'s critical-section pattern: check cap + call +
.\scripts\engines\gemini_gate.py:32:        with engine._ledger_lock():
.\scripts\engines\gemini_gate.py:33:            ok_flag, msg = engine.check_cap("GEMINI_GATE", cfg)
.\scripts\engines\gemini_gate.py:43:            engine.log_call("gemini", len(got), "gate-probe", "GEMINI_GATE")
.\scripts\engines\engine.py:35:def _ledger_lock():
.\scripts\engines\engine.py:98:def log_call(engine_name, tokens, purpose, task):
.\scripts\engines\engine.py:113:def check_cap(task, cfg):
.\scripts\engines\engine.py:278:def dispatch(cmd, packet, task, purpose, cfg, images=None, log=True):
.\scripts\engines\engine.py:296:    # log defaults True so a direct dispatch() call records its own row (tests +
.\scripts\engines\engine.py:300:        log_call(name, len(out), purpose, task)
.\scripts\engines\engine.py:347:    # the lock. The old code ran dispatch() INSIDE the lock, so one long call
.\scripts\engines\engine.py:354:    with _ledger_lock():
.\scripts\engines\engine.py:355:        ok, msg = check_cap(args.task, cfg)
.\scripts\engines\engine.py:360:        log_call(engine_name, 0, args.purpose, args.task)  # reserve the slot
.\scripts\engines\engine.py:361:    out = dispatch(args.cmd, args.packet, args.task, args.purpose, cfg,
.\scripts\engines\gemini_gate.py:30:        # Mirror dispatch()'s critical-section pattern: check cap + call +
.\scripts\engines\gemini_gate.py:32:        with engine._ledger_lock():
.\scripts\engines\gemini_gate.py:33:            ok_flag, msg = engine.check_cap("GEMINI_GATE", cfg)
.\scripts\engines\gemini_gate.py:43:            engine.log_call("gemini", len(got), "gate-probe", "GEMINI_GATE")

 succeeded in 3859ms:
98:def log_call(engine_name, tokens, purpose, task):
167:    if os.environ.get("ENGINE_DRY_RUN") == "1":
195:    if os.environ.get("ENGINE_DRY_RUN") == "1":
216:_GEMINI_INLINE_LIMIT = 4 * 1024 * 1024  # 4 MB total inline cap; above this -> Files API
219:def _gemini_request(parts, cfg):
234:        except urllib.error.HTTPError as e:
244:    if os.environ.get("ENGINE_DRY_RUN") == "1":
249:def call_gemini_visual(image_paths, prompt, cfg):
251:    engine with eyes (Codex/DeepSeek are text). Images ride as base64 inline_data
253:    if os.environ.get("ENGINE_DRY_RUN") == "1":
269:        if total > _GEMINI_INLINE_LIMIT:
273:        parts.append({"inline_data": {"mime_type": mime,
278:def dispatch(cmd, packet, task, purpose, cfg, images=None, log=True):
280:    if cmd == "grunt":
282:    elif cmd == "review":
284:    elif cmd == "read":
289:    elif cmd == "see":
296:    # log defaults True so a direct dispatch() call records its own row (tests +
300:        log_call(name, len(out), purpose, task)
304:def main(argv=None):
334:    if args.cmd == "wake-start":
336:    if args.cmd == "status":
347:    # the lock. The old code ran dispatch() INSIDE the lock, so one long call
354:    with _ledger_lock():
360:        log_call(engine_name, 0, args.purpose, args.task)  # reserve the slot
361:    out = dispatch(args.cmd, args.packet, args.task, args.purpose, cfg,

 succeeded in 4268ms:
#!/usr/bin/env python3
"""Reliability gate for Gemini-as-reader. Runs N reader-extracts against known
answers; flips engines.config.json gemini.enabled true iff all pass."""
import os, sys, json, tempfile, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("engine", os.path.join(HERE, "engine.py"))
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)

CASES = [
    ("From this log, output ONLY the stall seconds as a number: "
     "[stream] underrun pts=842.5s, refill took 3.2s. Just the number.", "3.2"),
    ("Output ONLY the HTTP status code in this line as a number: "
     "GET /stream 200 OK 14ms. Just the number.", "200"),
    ("Output ONLY the episode number: 'One Piece S01E1089 unavailable'. "
     "Just the number.", "1089"),
]


def main():
    engine._load_dotenv()
    cfg = engine.load_config()
    cfg["gemini"]["enabled"] = True  # force-enable for the probe only
    engine.require_key("GEMINI_API_KEY")
    # Do NOT call mark_wake() â€” gate probes must NOT reset the caller's cap
    # counter. log_call per probe is sufficient for budget visibility.
    passed = 0
    for prompt, want in CASES:
        # Mirror dispatch()'s critical-section pattern: check cap + call +
        # log under one lock so concurrent agents can't both sneak past.
        with engine._ledger_lock():
            ok_flag, msg = engine.check_cap("GEMINI_GATE", cfg)
            if msg:
                print(msg, file=sys.stderr)
            if not ok_flag:
                print("GEMINI GATE STOPPED: cap exhausted â€” gate cannot proceed.", file=sys.stderr)
                return 2
            try:
                got = engine.call_gemini(prompt, cfg).strip()
            except Exception as e:
                print(f"FAIL (error): {e}"); continue
            engine.log_call("gemini", len(got), "gate-probe", "GEMINI_GATE")
        ok = got == want  # exact match â€” '1200' must not pass '200'
        print(f"{'PASS' if ok else 'FAIL'}: want '{want}' got '{got[:40]}'")
        passed += ok
    flip = passed == len(CASES)
    path = engine.CONFIG_PATH
    disk = json.load(open(path))
    disk["gemini"]["enabled"] = flip
    # Atomic write: temp file + os.replace to avoid truncation on crash
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), suffix=".tmp")
    try:
        with os.fdopen(fd, "w") as f:
            json.dump(disk, f, indent=2)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)
    print(f"\nGemini reliability: {passed}/{len(CASES)} -> "
          f"gemini.enabled = {flip}")
    return 0 if flip else 1


if __name__ == "__main__":
    sys.exit(main())

codex
Findings:
- P1 [engine.py](</C:/Users/Suprabha/Desktop/Tankoban 2/scripts/engines/engine.py:269>): the 4 MB `see` guard counts raw bytes before base64. A raw image under 4 MB can become over 4 MB in `inline_data.data`, so the stated inline payload cap is not actually enforced.
- P2 [engine.py](</C:/Users/Suprabha/Desktop/Tankoban 2/scripts/engines/engine.py:254>): zero-byte files with supported extensions are accepted and sent as empty base64 data. If “rejects empty” means empty image files, this misses the DoD.

Concurrency check: [engine.py](</C:/Users/Suprabha/Desktop/Tankoban 2/scripts/engines/engine.py:354>) now holds `_ledger_lock()` only for `check_cap()` plus reservation `log_call()`, then calls `dispatch(..., log=False)` outside the lock. Direct `dispatch()` still logs by default at [engine.py](</C:/Users/Suprabha/Desktop/Tankoban 2/scripts/engines/engine.py:300>). The reservation can leak on later dispatch/preflight failure, but that is consistent with the reservation model if failed attempts are meant to spend a cap slot.

REQUEST-CHANGES + the `see` lane does not actually enforce the stated 4 MB inline cap because it checks raw image bytes instead of the base64 inline payload.
tokens used
43,494
Findings:
- P1 [engine.py](</C:/Users/Suprabha/Desktop/Tankoban 2/scripts/engines/engine.py:269>): the 4 MB `see` guard counts raw bytes before base64. A raw image under 4 MB can become over 4 MB in `inline_data.data`, so the stated inline payload cap is not actually enforced.
- P2 [engine.py](</C:/Users/Suprabha/Desktop/Tankoban 2/scripts/engines/engine.py:254>): zero-byte files with supported extensions are accepted and sent as empty base64 data. If “rejects empty” means empty image files, this misses the DoD.

Concurrency check: [engine.py](</C:/Users/Suprabha/Desktop/Tankoban 2/scripts/engines/engine.py:354>) now holds `_ledger_lock()` only for `check_cap()` plus reservation `log_call()`, then calls `dispatch(..., log=False)` outside the lock. Direct `dispatch()` still logs by default at [engine.py](</C:/Users/Suprabha/Desktop/Tankoban 2/scripts/engines/engine.py:300>). The reservation can leak on later dispatch/preflight failure, but that is consistent with the reservation model if failed attempts are meant to spend a cap slot.

REQUEST-CHANGES + the `see` lane does not actually enforce the stated 4 MB inline cap because it checks raw image bytes instead of the base64 inline payload.
