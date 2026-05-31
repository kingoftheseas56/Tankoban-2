#!/usr/bin/env python3
"""Headless Office bridge for Agent 7 Codex.

Launch:
  python scripts/office/codex_agent7_bridge.py

This is intentionally NOT a TUI bridge. It never focuses windows, never uses the
clipboard, and never simulates keystrokes. It watches the Office bus, calls
`codex exec` non-interactively on gpt-5.5 to decide whether a short reply is
useful, then posts any approved reply through office_bus.py as session
`codex-agent7` (agent7) only.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from datetime import datetime


for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUS = os.path.join(ROOT, "scripts", "office", "office_bus.py")
BUS_FILE = os.path.join(ROOT, "agents", "bus.jsonl")
LOG = os.path.join(ROOT, ".claude", "agent7_office_bridge.log")
LOCK = os.path.join(ROOT, ".claude", "agent7_office_bridge.lock")

AGENT = "agent7"
SESSION_ID = "codex-agent7"
MODEL = "gpt-5.5"
WAKE_PROMPT_RE = re.compile(r"^Office wake for agent7:", re.IGNORECASE)


RESPONSE_SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "properties": {
        "replies": {
            "type": "array",
            "maxItems": 2,
            "items": {
                "type": "object",
                "additionalProperties": False,
                "properties": {
                    "to": {
                        "type": "string",
                        "description": "Office target, e.g. @agent0, @hemanth, or @all.",
                    },
                    "msg": {
                        "type": "string",
                        "description": "Brief Office reply from Agent 7.",
                    },
                },
                "required": ["to", "msg"],
            },
        }
    },
    "required": ["replies"],
}


def log(message):
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    stamp = time.strftime("%Y-%m-%d %H:%M:%S")
    line = f"[{stamp}] {message}"
    print(line, flush=True)
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def run_bus(*args, check=True):
    proc = subprocess.run(
        [sys.executable, BUS, *args],
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    return proc.stdout.strip()


def ensure_identity():
    run_bus("join", SESSION_ID, "7")
    who = run_bus("whoami", SESSION_ID)
    if who != AGENT:
        raise RuntimeError(f"refusing to run: {SESSION_ID!r} maps to {who!r}, not {AGENT!r}")


def read_bus_records():
    records = []
    if not os.path.exists(BUS_FILE):
        return records
    with open(BUS_FILE, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return records


def max_seq(records=None):
    records = read_bus_records() if records is None else records
    return max((int(r.get("seq", 0)) for r in records), default=0)


def addressed_to_me(rec):
    to = str(rec.get("to", ""))
    if to == "all" or to == AGENT:
        return True
    return AGENT in [part.strip() for part in to.split(",")]


def pending_for_me(after_seq):
    out = []
    for rec in read_bus_records():
        try:
            seq = int(rec.get("seq", 0))
        except (TypeError, ValueError):
            continue
        if seq <= after_seq:
            continue
        if rec.get("from") == AGENT:
            continue
        if addressed_to_me(rec):
            out.append(rec)
    return out


def mark_seen(seq):
    run_bus("mark-seen", AGENT, str(int(seq)))


def acquire_lock():
    os.makedirs(os.path.dirname(LOCK), exist_ok=True)
    try:
        os.mkdir(LOCK)
        return True
    except FileExistsError:
        return False


def release_lock():
    try:
        os.rmdir(LOCK)
    except OSError:
        pass


def context_window(records, limit=12):
    rows = []
    for rec in records[-limit:]:
        rows.append(
            {
                "seq": rec.get("seq"),
                "from": rec.get("from"),
                "to": rec.get("to"),
                "kind": rec.get("kind"),
                "msg": rec.get("msg"),
            }
        )
    return rows


def build_prompt(messages, recent):
    payload = {
        "new_messages_for_agent7": messages,
        "recent_room_context": recent,
        "now": datetime.now().isoformat(timespec="seconds"),
    }
    return (
        "You are Agent 7 in THE OFFICE, running as Codex on gpt-5.5.\n"
        "You are in reply-only mode. Do not start code work, do not inspect files, "
        "do not run commands, and do not claim to have done work. Decide whether a "
        "brief Office reply is useful.\n\n"
        "Hard rules:\n"
        "- Output JSON matching the schema only.\n"
        "- Return {\"replies\": []} when no reply is useful.\n"
        "- Do not echo wake prompts, commit activity, or generic broadcasts.\n"
        "- Do not acknowledge every @all message.\n"
        "- If replying, keep it under 240 characters and target the sender unless "
        "the message clearly asks for a room-wide response.\n"
        "- Speak as Agent 7; the bridge will post as agent7. Never ask to post as "
        "hemanth or any other identity.\n\n"
        "Office payload:\n"
        + json.dumps(payload, ensure_ascii=False, indent=2)
    )


def write_schema(path):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(RESPONSE_SCHEMA, f)


def run_codex(prompt, model):
    with tempfile.TemporaryDirectory(prefix="agent7-office-") as td:
        schema_path = os.path.join(td, "reply_schema.json")
        out_path = os.path.join(td, "reply.json")
        write_schema(schema_path)
        cmd = [
            "codex",
            "exec",
            "-m",
            model,
            "-C",
            ROOT,
            "-s",
            "read-only",
            "-c",
            'approval_policy="never"',
            "--output-schema",
            schema_path,
            "-o",
            out_path,
            "-",
        ]
        proc = subprocess.run(
            cmd,
            input=prompt,
            cwd=ROOT,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=180,
        )
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or "codex exec failed")
        if not os.path.exists(out_path):
            raise RuntimeError("codex exec produced no output-last-message file")
        with open(out_path, "r", encoding="utf-8") as f:
            text = f.read().strip()
    try:
        return json.loads(text)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"codex exec returned invalid JSON: {text[:500]}") from exc


def valid_target(value):
    if not isinstance(value, str):
        return None
    value = value.strip()
    if not value.startswith("@"):
        value = "@" + value
    target = value[1:]
    if target == "all" or target == "hemanth" or re.fullmatch(r"agent\d+", target):
        return value
    return None


def clean_reply(value):
    if not isinstance(value, str):
        return None
    msg = " ".join(value.split())
    if not msg:
        return None
    if len(msg) > 280:
        msg = msg[:277].rstrip() + "..."
    return msg


def post_replies(replies):
    posted = 0
    ensure_identity()
    for reply in replies:
        to = valid_target(reply.get("to"))
        msg = clean_reply(reply.get("msg"))
        if not to or not msg:
            log(f"skipping invalid reply: {reply!r}")
            continue
        if WAKE_PROMPT_RE.search(msg):
            log("skipping reply that looks like a wake prompt")
            continue
        seq = run_bus("send", SESSION_ID, to, msg)
        posted += 1
        log(f"posted seq {seq} as {AGENT} to {to}: {msg}")
    return posted


def process_batch(messages, model, dry_run=False):
    max_pending = max_seq(messages)
    mark_seen(max_pending)

    useful = [
        m for m in messages
        if m.get("kind", "chat") == "chat"
        and not WAKE_PROMPT_RE.search(str(m.get("msg", "")))
    ]
    ignored = len(messages) - len(useful)
    if ignored:
        log(f"ignored {ignored} wake/activity/non-useful trigger(s)")
    if not useful:
        return 0

    records = read_bus_records()
    prompt = build_prompt(useful, context_window(records))
    if dry_run:
        log("dry-run: would invoke codex exec for " + str(len(useful)) + " message(s)")
        return 0
    result = run_codex(prompt, model)
    replies = result.get("replies", [])
    if not isinstance(replies, list):
        log("model returned non-list replies; skipping")
        return 0
    if not replies:
        log("model chose no Office reply")
        return 0
    return post_replies(replies)


def main():
    parser = argparse.ArgumentParser(
        description="Headless, non-interactive Agent 7 bridge for The Office."
    )
    parser.add_argument("--model", default=MODEL)
    parser.add_argument("--interval", type=float, default=3.0)
    parser.add_argument("--once", action="store_true", help="Poll once, process at most one batch, then exit.")
    parser.add_argument(
        "--process-backlog",
        action="store_true",
        help="Process existing unread messages. Default starts from current bus tail for safety.",
    )
    parser.add_argument("--dry-run", action="store_true", help="Mark triggers seen but do not call Codex or post.")
    parser.add_argument("--no-lock", action="store_true", help="Allow running without the single-instance lock.")
    args = parser.parse_args()

    if not args.no_lock and not acquire_lock():
        raise SystemExit("agent7 bridge already running; refusing second instance")
    try:
        ensure_identity()
        if args.process_backlog:
            try:
                last = int(run_bus("cursor", AGENT, check=False) or "0")
            except ValueError:
                last = 0
            log(f"Agent 7 headless bridge live on {args.model}; processing backlog after seq {last}")
        else:
            last = max_seq()
            mark_seen(last)
            log(f"Agent 7 headless bridge live on {args.model}; starting from bus tail seq {last}")

        while True:
            messages = pending_for_me(last)
            if messages:
                last = max_seq(messages)
                log(f"received {len(messages)} trigger(s), newest seq {last}")
                try:
                    process_batch(messages, args.model, dry_run=args.dry_run)
                except Exception as exc:
                    log(f"processing failed after marking seen: {exc}")
            if args.once:
                return 0
            time.sleep(max(0.5, args.interval))
    finally:
        if not args.no_lock:
            release_lock()


if __name__ == "__main__":
    raise SystemExit(main())
