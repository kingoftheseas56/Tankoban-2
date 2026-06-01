#!/usr/bin/env python3
"""Multi-engine brother helper: bare-tool access to DeepSeek/Codex/Gemini.

Spec: docs/superpowers/specs/2026-06-01-multi-engine-brother-design.md
Keys come from environment only (DEEPSEEK_API_KEY, GEMINI_API_KEY). Never logged.
"""
import os, sys, json, time, subprocess, tempfile, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(HERE, "engines.config.json")
LEDGER_PATH = os.path.join(HERE, ".ledger.jsonl")


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
                rows.append(json.loads(line))
    return rows


def ledger_rows_since_wake():
    rows = _read_ledger()
    last = 0
    for i, r in enumerate(rows):
        if r.get("event") == "wake-start":
            last = i + 1
    return rows[last:]


def log_call(engine_name, tokens, purpose, task):
    row = {"event": "call", "ts": int(time.time()),
           "engine": engine_name, "tokens": tokens,
           "purpose": purpose, "task": task}
    with open(LEDGER_PATH, "a") as f:
        f.write(json.dumps(row) + "\n")


def mark_wake():
    with open(LEDGER_PATH, "a") as f:
        f.write(json.dumps({"event": "wake-start", "ts": int(time.time())}) + "\n")


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
    proc = subprocess.run(
        [cli, "-p", packet, "--model", cfg["deepseek"]["model"]],
        cwd=scratch, env=env, capture_output=True, text=True,
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
    proc = subprocess.run(
        [cli, "exec", packet],
        stdin=subprocess.DEVNULL, capture_output=True, text=True,
        timeout=cfg["timeouts"]["codex"])
    if proc.returncode != 0:
        tail = proc.stderr.strip()[-200:] if proc.stderr else "(no stderr)"
        raise RuntimeError(
            f"Codex exited {proc.returncode}: {tail}")
    return parse_codex(proc.stdout)


def call_gemini(packet, cfg):
    if os.environ.get("ENGINE_DRY_RUN") == "1":
        return "DRY:gemini:" + packet[:20]
    key = require_key("GEMINI_API_KEY")
    url = cfg["gemini"]["url"].format(model=cfg["gemini"]["model"])
    body = json.dumps({"contents": [{"parts": [{"text": packet}]}]}).encode()
    req = urllib.request.Request(url, data=body,
                                 headers={"Content-Type": "application/json",
                                          "x-goog-api-key": key})
    with urllib.request.urlopen(req, timeout=cfg["timeouts"]["gemini"]) as resp:
        return parse_gemini(json.load(resp))


def dispatch(cmd, packet, task, purpose, cfg):
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
    else:
        raise ValueError(f"unknown command: {cmd}")
    log_call(name, len(out), purpose, task)
    return out


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description="Multi-engine brother helper")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("grunt", "review", "read"):
        p = sub.add_parser(name)
        p.add_argument("--task", required=True)
        p.add_argument("--purpose", default="")
        p.add_argument("packet")
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

    ok, msg = check_cap(args.task, cfg)
    if msg:
        print(msg, file=sys.stderr)
    if not ok:
        return 2
    out = dispatch(args.cmd, args.packet, args.task, args.purpose, cfg)
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
