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


def main(argv):
    if not argv:
        sys.exit("usage: office_bus.py <append|join|whoami|unseen|mark-seen|cursor> ...")
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
    else:
        sys.exit("office_bus.py: unknown subcommand " + cmd)


if __name__ == "__main__":
    main(sys.argv[1:])
