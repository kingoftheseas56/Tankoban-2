#!/usr/bin/env python3
"""The Office — bus core (JSON logic). Pure stdlib; no jq dependency.

Subcommands (all paths overridable via OFFICE_DIR / OFFICE_BUS / OFFICE_CURSORS /
OFFICE_SESSIONS env so tests run in a sandbox):

  append   <from> <to> <kind> <arc> <msg>   -> append a message line, prints seq
  join     <session_id> <agent_number>      -> register tab identity
  whoami   <session_id>                      -> prints "agentN" or ""
  unseen   <agentN>                          -> prints JSON lines addressed to agent, seq>cursor
  mar_seen <agentN> <seq>                    -> advance cursor (alias: mark-seen)
  cursor   <agentN>                          -> prints last-seen seq (default 0)

Message schema (one JSON object per line in bus.jsonl):
  {"ts","seq","from","to","kind","arc","msg"}
  to: "agentN" | "agentA,agentB" | "all"
  kind/arc: Congress-aware, reserved (v1 always "chat"/null)
"""
import os
import sys
import json
import time
import errno
from datetime import datetime, timezone

# Windows consoles default to cp1252, which crashes (UnicodeEncodeError) when a
# message contains an emoji (e.g. the salute brothers sign off with). Force UTF-8
# on stdout/stderr with errors="replace" so display NEVER crashes regardless of
# message content. Bug found live 2026-05-30 by Agent 4 on office_join drain.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass


def _repo_root():
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def _dir():
    return os.environ.get("OFFICE_DIR", os.path.join(_repo_root(), "agents"))


def BUS():
    return os.environ.get("OFFICE_BUS", os.path.join(_dir(), "bus.jsonl"))


def CURSORS():
    return os.environ.get("OFFICE_CURSORS", os.path.join(_dir(), ".bus_cursors"))


def SESSIONS():
    return os.environ.get(
        "OFFICE_SESSIONS", os.path.join(_repo_root(), ".claude", ".office_sessions.json")
    )


def _lock(path):
    """mkdir-based advisory lock (portable on Windows Git Bash)."""
    lockdir = path + ".lock"
    for _ in range(200):
        try:
            os.mkdir(lockdir)
            return lockdir
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise
            time.sleep(0.025)
    raise SystemExit("office: lock timeout on " + path)


def _unlock(lockdir):
    try:
        os.rmdir(lockdir)
    except OSError:
        pass


def _next_seq(bus):
    if not os.path.exists(bus):
        return 1
    last = 0
    with open(bus, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                last = json.loads(line).get("seq", last)
            except json.JSONDecodeError:
                continue
    return last + 1


def cmd_append(frm, to, kind, arc, msg):
    bus = BUS()
    os.makedirs(os.path.dirname(bus), exist_ok=True)
    lk = _lock(bus)
    try:
        seq = _next_seq(bus)
        rec = {
            "ts": datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds"),
            "seq": seq,
            "from": frm,
            "to": to,
            "kind": kind,
            "arc": None if arc in ("null", "", None) else arc,
            "msg": msg,
        }
        with open(bus, "a", encoding="utf-8") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
    finally:
        _unlock(lk)
    print(seq)


def cmd_join(sid, num):
    path = SESSIONS()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = {}
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except (json.JSONDecodeError, OSError):
            data = {}
    data[sid] = str(num)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f)
    os.replace(tmp, path)


def cmd_whoami(sid):
    path = SESSIONS()
    if not os.path.exists(path):
        print("")
        return
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (json.JSONDecodeError, OSError):
        print("")
        return
    num = data.get(sid)
    print("agent" + str(num) if num else "")


def cmd_cursor(agent):
    p = os.path.join(CURSORS(), agent + ".seq")
    try:
        with open(p, "r", encoding="utf-8") as f:
            print(int(f.read().strip()))
    except (OSError, ValueError):
        print(0)


def cmd_mark_seen(agent, seq):
    os.makedirs(CURSORS(), exist_ok=True)
    with open(os.path.join(CURSORS(), agent + ".seq"), "w", encoding="utf-8") as f:
        f.write(str(int(seq)))


def _cursor_val(agent):
    p = os.path.join(CURSORS(), agent + ".seq")
    try:
        with open(p, "r", encoding="utf-8") as f:
            return int(f.read().strip())
    except (OSError, ValueError):
        return 0


def _addressed_to(rec, me):
    to = rec.get("to", "")
    if to == "all" or to == me:
        return True
    return me in [t.strip() for t in to.split(",")]


def cmd_unseen(me):
    bus = BUS()
    if not os.path.exists(bus):
        return
    cur = _cursor_val(me)
    with open(bus, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            if rec.get("seq", 0) > cur and rec.get("from") != me and _addressed_to(rec, me):
                print(json.dumps(rec, ensure_ascii=False))


def _agent_for(sid):
    path = SESSIONS()
    if not sid or not os.path.exists(path):
        return ""
    try:
        with open(path, "r", encoding="utf-8") as f:
            num = json.load(f).get(sid)
    except (json.JSONDecodeError, OSError):
        return ""
    return "agent" + str(num) if num else ""


def cmd_send(sid, to_raw, msg):
    frm = _agent_for(sid)
    if not frm:
        sys.exit("office send: tab not registered — run office_join first")
    to = to_raw[1:] if to_raw.startswith("@") else to_raw
    cmd_append(frm, to, "chat", "null", msg)  # prints seq


def cmd_flag(sid, msg):
    """Post a BLOCKER to the room (the honesty / real-talk lane): kind='blocked',
    to='all', so it surfaces distinctly + marks the brother blocked in the roster."""
    frm = _agent_for(sid)
    if not frm:
        sys.exit("office flag: tab not registered — run office_join first")
    cmd_append(frm, "all", "blocked", "null", msg)  # prints seq


def cmd_ack(frm, ask_seq, *note_parts):
    """Acknowledge a direct ask by seq, addressed back to the original asker."""
    note = " ".join(note_parts).strip() or "ack"
    asker = "all"
    bus = BUS()
    if os.path.exists(bus):
        with open(bus, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if str(rec.get("seq")) == str(ask_seq):
                    asker = rec.get("from", "all")
                    break
    cmd_append(frm, asker, "ack", str(ask_seq), note)


import re

# Auto-detect agent number from a wake-prompt / explicit "I am agent N" line.
_AGENT_RE = re.compile(
    r"(?:you'?re|you are|i am|office:\s*i am|wake up,?)\s+agent\s*#?\s*(\d+)", re.IGNORECASE
)
_AGENT_RE2 = re.compile(r"\bagent[\s-]*#?\s*(\d+)\b", re.IGNORECASE)
_COMMIT_AGENT_RE = re.compile(r"^\[Agent\s*#?\s*(\d+)", re.IGNORECASE)


def cmd_mirror_commit(sha, *subject_parts):
    """Mirror a commit into the room as an 'activity' line so off-channel WORK is
    never invisible. FROM is the agent parsed from the '[Agent N' subject tag, or
    'system' if untagged. Called by the git post-commit hook with (sha, subject)."""
    subject = " ".join(subject_parts).strip()
    m = _COMMIT_AGENT_RE.match(subject)
    frm = ("agent" + m.group(1)) if m else "system"
    msg = "{0} ({1})".format(subject[:80], sha)
    cmd_append(frm, "all", "activity", "null", msg)  # prints seq


def _detect_agent_num(prompt):
    if not prompt:
        return ""
    m = _AGENT_RE.search(prompt) or _AGENT_RE2.search(prompt)
    return m.group(1) if m else ""


def cmd_deliver():
    """Hook entrypoint (UserPromptSubmit). Reads hook JSON on stdin (has
    session_id + prompt), auto-binds identity from the prompt if this tab isn't
    registered yet, injects unseen messages as additionalContext JSON, advances
    cursor. Always exit 0 (never block prompt submission)."""
    # DeepSeek endpoint rejects injected context (system-role 400) — stay silent.
    base = os.environ.get("ANTHROPIC_BASE_URL", "")
    if "deepseek" in base.lower():
        return
    raw = sys.stdin.read() if not sys.stdin.isatty() else ""
    sid, prompt = "", ""
    if raw.strip():
        try:
            payload = json.loads(raw)
            sid = payload.get("session_id", "")
            prompt = payload.get("prompt", "")
        except json.JSONDecodeError:
            pass
    if not sid:
        sid = os.environ.get("CLAUDE_CODE_SESSION_ID", os.environ.get("CLAUDE_SESSION_ID", ""))
    if not sid:
        return
    me = _agent_for(sid)
    if not me:
        # Auto-detect + explicit-fallback identity binding from the prompt text.
        num = _detect_agent_num(prompt)
        if num:
            cmd_join(sid, num)
            me = "agent" + str(num)
    if not me:
        return

    # --- Auto-clock-in nudge (v2): on the FIRST delivery of a session, inject a
    # one-time instruction telling this brother to start his always-on watch, so
    # Hemanth never has to type the watch command. Marker keyed by session_id so
    # it fires once per tab, not every prompt. ---
    parts = []
    nudge_dir = os.path.join(_dir(), ".office_nudged")
    marker = os.path.join(nudge_dir, sid.replace("/", "_"))
    if not os.path.exists(marker):
        os.makedirs(nudge_dir, exist_ok=True)
        try:
            open(marker, "w").close()
        except OSError:
            pass
        parts.append(
            "[THE OFFICE] You're {0}, and you're in the live brotherhood room. "
            "To go ALWAYS-ON (auto-wake when any brother messages you, no Hemanth prompt needed), "
            "start your watch now as a background Monitor task and STAY on it:\n"
            "    bash scripts/office/office_watch.sh {0}\n"
            "When it emits a line, read the new message (bash scripts/office/office_bus.py drain {0}), "
            "reply if warranted (bash scripts/office/chat_send.sh \"@agentN\" \"...\"), then keep watching. "
            "Reply-only for now — don't start code work off a bus message unless asked. "
            "OFFICE ETIQUETTE: don't narrate your bus posts back to Hemanth in your VS Code chat "
            "(\"I messaged Agent 4...\") — he's watching the Office room live, so that's redundant "
            "noise. Keep your VS Code replies about substance; let the room carry the coordination.".format(me)
        )
    msgs = []
    bus = BUS()
    if os.path.exists(bus):
        cur = _cursor_val(me)
        with open(bus, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if rec.get("seq", 0) > cur and rec.get("from") != me and _addressed_to(rec, me):
                    msgs.append(rec)
    if msgs:
        maxseq = max(r["seq"] for r in msgs)
        lines = "\n".join("  - {0}: {1}".format(r["from"], r["msg"]) for r in msgs)
        cmd_mark_seen(me, maxseq)
        parts.append(
            "[THE OFFICE] New message(s) for {0} "
            "(reply: bash scripts/office/chat_send.sh \"@agentN\" \"...\"):\n{1}".format(me, lines)
        )
    # Inject the nudge and/or the messages — whichever we have. Nothing => silent.
    if not parts:
        return
    ctx = "\n\n".join(parts)
    out = {"hookSpecificOutput": {"hookEventName": "UserPromptSubmit", "additionalContext": ctx}}
    print(json.dumps(out, ensure_ascii=False))


def cmd_drain(me):
    """Print human-readable pending messages for `me` AND advance the cursor,
    atomically in one process. Used by office_join.sh so a tab joining late sees
    the backlog once without re-showing on the next prompt."""
    bus = BUS()
    rows = []
    if os.path.exists(bus):
        cur = _cursor_val(me)
        with open(bus, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if rec.get("seq", 0) > cur and rec.get("from") != me and _addressed_to(rec, me):
                    rows.append(rec)
    if not rows:
        print("(no messages waiting)")
        return
    for r in rows:
        print("  {0} -> {1}: {2}".format(r["from"], r["to"], r["msg"]))
    print("({0} message(s) above - you are now in the office)".format(len(rows)))
    cmd_mark_seen(me, max(r["seq"] for r in rows))


def cmd_watch_peek(me, after):
    """For office_watch.sh: print '[seq N] from: msg' for each message addressed
    to `me` (direct or all, not from me) with seq > `after`. Does NOT touch the
    cursor (the watch tracks its own high-water mark; the woken brother runs
    `drain` to read + advance). Silent if nothing new."""
    try:
        after = int(after)
    except (TypeError, ValueError):
        after = 0
    bus = BUS()
    if not os.path.exists(bus):
        return
    for line in open(bus, "r", encoding="utf-8"):
        line = line.strip()
        if not line:
            continue
        try:
            rec = json.loads(line)
        except json.JSONDecodeError:
            continue
        if rec.get("seq", 0) > after and rec.get("from") != me and _addressed_to(rec, me):
            print("[seq {0}] {1}: {2}".format(rec["seq"], rec["from"], rec["msg"]))


def cmd_close():
    bus = BUS()
    if not (os.path.exists(bus) and os.path.getsize(bus) > 0):
        print("office: already closed (no live bus)")
        return
    arch_dir = os.path.join(_dir(), "bus_archive")
    os.makedirs(arch_dir, exist_ok=True)
    day = datetime.now().strftime("%Y-%m-%d")
    dest = os.path.join(arch_dir, day + ".jsonl")
    n = 1
    while os.path.exists(dest):
        dest = os.path.join(arch_dir, "{0}-{1}.jsonl".format(day, n))
        n += 1
    with open(bus, "r", encoding="utf-8") as f:
        count = sum(1 for x in f if x.strip())
    os.replace(bus, dest)
    cur = CURSORS()
    if os.path.isdir(cur):
        for fn in os.listdir(cur):
            try:
                os.remove(os.path.join(cur, fn))
            except OSError:
                pass
    print("office: closed — archived {0} msg(s) to {1}; live bus cleared.".format(count, dest))


def main(argv):
    if not argv:
        sys.exit("usage: office_bus.py <append|join|whoami|unseen|mark-seen|cursor|send|flag|ack|deliver|drain|watch-peek|mirror-commit|close> ...")
    cmd, rest = argv[0], argv[1:]
    if cmd == "append":
        cmd_append(*rest)
    elif cmd == "join":
        cmd_join(*rest)
    elif cmd == "whoami":
        cmd_whoami(*rest)
    elif cmd == "unseen":
        cmd_unseen(*rest)
    elif cmd in ("mark-seen", "mark_seen"):
        cmd_mark_seen(*rest)
    elif cmd == "cursor":
        cmd_cursor(*rest)
    elif cmd == "send":
        cmd_send(*rest)
    elif cmd == "flag":
        cmd_flag(*rest)
    elif cmd == "ack":
        cmd_ack(*rest)
    elif cmd == "mirror-commit":
        cmd_mirror_commit(*rest)
    elif cmd == "deliver":
        cmd_deliver()
    elif cmd == "drain":
        cmd_drain(*rest)
    elif cmd == "watch-peek":
        cmd_watch_peek(*rest)
    elif cmd == "close":
        cmd_close()
    else:
        sys.exit("office_bus.py: unknown subcommand " + cmd)


if __name__ == "__main__":
    main(sys.argv[1:])
