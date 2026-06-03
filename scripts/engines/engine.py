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
    always wins — the file only sets what is currently unset."""
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
    # sharing one machine to catch runaway call loops — NOT a security boundary
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
    transient 503/429 (Gemini Flash returns 503 under load — caught live
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
    """`see` lane — text prompt + one or more inline images. Gemini is the only
    engine with eyes (Codex/DeepSeek are text). Images ride as base64 inline_data
    parts; total must stay under the 4 MB inline cap (Files API above that)."""
    if os.environ.get("ENGINE_DRY_RUN") == "1":
        return "DRY:gemini-visual:" + (prompt or "")[:20]
    if not image_paths:
        raise ValueError("see: at least one --image is required")
    parts = [{"text": prompt}]
    total_b64 = 0
    for p in image_paths:
        ext = os.path.splitext(p)[1].lower()
        mime = _GEMINI_MIME.get(ext)
        if not mime:
            raise ValueError(
                f"see: unsupported image type '{ext}' for {p} "
                f"(allowed: {sorted(_GEMINI_MIME)})")
        with open(p, "rb") as f:
            raw = f.read()
        if not raw:
            raise ValueError(f"see: image {p} is empty (0 bytes)")  # P2 (Codex)
        b64 = base64.b64encode(raw).decode()
        # The 4 MB inline cap applies to the base64 payload actually sent, not the
        # raw bytes (base64 inflates ~33%) — caught by Codex review 2026-06-03 (P1).
        total_b64 += len(b64)
        if total_b64 > _GEMINI_INLINE_LIMIT:
            raise ValueError(
                f"see: inline images exceed the 4 MB base64 cap ({total_b64} chars) "
                f"— use fewer/smaller images (Files API not wired)")
        parts.append({"inline_data": {"mime_type": mime, "data": b64}})
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
    # callers (EDEADLK) — it broke Agent 3's live DeepSeek review. Reserving the
    # row under the lock keeps the no-overshoot guarantee; tokens=0 marks it a
    # reservation (cap counts rows, not tokens — token size is informational).
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
