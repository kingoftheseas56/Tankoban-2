#!/usr/bin/env python3
"""The Office — summon dispatcher (reachability foundation, mini-congress arc 1).

A persistent loop that watches bus.jsonl for kind="summon" messages and routes each:
  - target tab LIVE (heartbeat fresh)  -> do nothing; the target's own watch wakes it.
  - target idle/closed                 -> spawn the brother as a BACKGROUND headless
                                          session (spawn_brother.sh), under a per-target
                                          lock + a reliable spawn cap.

Guardrails (tight leash):
  - no chains:   refuses summons stamped arc="bg" (issued from a background session;
                 force-stamped in office_bus.py cmd_append, so raw append can't forge it).
  - no double:   per-target mkdir lock; one background spawn per brother at a time
                 (with stale-lock reclaim so a crashed run can't wedge a brother forever).
  - spawn cap:   the dispatcher writes the ledger "start" row SYNCHRONOUSLY under a
                 ledger lock before spawning, so a burst can't outrun the cap.
  (background brothers post results, never merge — prompt + a git PATH-shim in spawn_brother.sh.)

Run by open_office.bat alongside office_web.py. Path/tuning env mirrors office_bus.py:
  OFFICE_DIR, OFFICE_BUS, OFFICE_DISPATCH_INTERVAL, OFFICE_LIVE_WINDOW,
  OFFICE_SPAWN_CAP, OFFICE_SPAWN_CAP_WINDOW, OFFICE_LOCK_STALE, OFFICE_BROTHER_MODEL.
"""
import os
import sys
import json
import time
import errno
import shutil
import subprocess
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass


def _bash():
    """Resolve bash even when launched from cmd (Git Bash not always on PATH)."""
    b = shutil.which("bash")
    if b:
        return b
    for cand in (r"C:\Program Files\Git\bin\bash.exe",
                 r"C:\Program Files\Git\usr\bin\bash.exe",
                 r"C:\Program Files (x86)\Git\bin\bash.exe"):
        if os.path.exists(cand):
            return cand
    return "bash"


def _dir():
    return os.environ.get("OFFICE_DIR", os.path.join(REPO, "agents"))


def BUS():
    return os.environ.get("OFFICE_BUS", os.path.join(_dir(), "bus.jsonl"))


def HB_DIR():
    return os.path.join(_dir(), ".office_heartbeats")


def LOCK_DIR():
    return os.path.join(_dir(), ".office_spawn_locks")


def LEDGER():
    return os.path.join(_dir(), ".office_spawns.jsonl")


def CURSOR_FILE():
    return os.path.join(_dir(), ".office_dispatch.seq")


INTERVAL = int(os.environ.get("OFFICE_DISPATCH_INTERVAL", "3"))
LIVE_WINDOW = int(os.environ.get("OFFICE_LIVE_WINDOW", "15"))          # heartbeat freshness (s)
SPAWN_CAP = int(os.environ.get("OFFICE_SPAWN_CAP", "5"))               # max spawns / window
CAP_WINDOW = int(os.environ.get("OFFICE_SPAWN_CAP_WINDOW", "3600"))    # window (s)
LOCK_STALE = int(os.environ.get("OFFICE_LOCK_STALE", "1800"))         # stale per-target lock (s)
MODEL = os.environ.get("OFFICE_BROTHER_MODEL", "sonnet")


def _now_iso():
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")


# ── cursor ───────────────────────────────────────────────────────────────────
def _read_cursor():
    try:
        with open(CURSOR_FILE()) as f:
            return int(f.read().strip())
    except (OSError, ValueError):
        return None


def _write_cursor(seq):
    os.makedirs(_dir(), exist_ok=True)
    tmp = CURSOR_FILE() + ".tmp"
    with open(tmp, "w") as f:
        f.write(str(int(seq)))
    os.replace(tmp, CURSOR_FILE())


# ── bus ──────────────────────────────────────────────────────────────────────
def _iter_bus():
    bus = BUS()
    if not os.path.exists(bus):
        return
    with open(bus, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except (json.JSONDecodeError, ValueError):
                continue
            if isinstance(rec, dict):   # a bare [] / "str" / number is not a message
                yield rec


def _max_seq():
    last = 0
    for rec in _iter_bus():
        s = rec.get("seq", last)
        if isinstance(s, int):
            last = s
    return last


def _new_summons(after):
    out = []
    for rec in _iter_bus():
        if rec.get("kind") == "summon" and isinstance(rec.get("seq"), int) and rec["seq"] > after:
            out.append(rec)
    return out


def _is_live(agent):
    p = os.path.join(HB_DIR(), agent + ".beat")
    try:
        with open(p) as f:
            beat = int(f.read().strip())
        return (time.time() - beat) < LIVE_WINDOW
    except (OSError, ValueError):
        return False


# ── ledger / cap (synchronous, lock-guarded) ─────────────────────────────────
def _ledger_lock():
    p = LEDGER() + ".lock"
    os.makedirs(_dir(), exist_ok=True)
    for _ in range(200):
        try:
            os.mkdir(p)
            return p
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise
            time.sleep(0.025)
    return None  # proceed unlocked rather than hang the loop forever


def _ledger_unlock(p):
    if p:
        try:
            os.rmdir(p)
        except OSError:
            pass


def _spawn_count_recent():
    led = LEDGER()
    if not os.path.exists(led):
        return 0
    n = 0
    cut = time.time() - CAP_WINDOW
    for line in open(led, encoding="utf-8"):
        line = line.strip()
        if not line:
            continue
        try:
            rec = json.loads(line)
        except (json.JSONDecodeError, ValueError):
            continue
        if not isinstance(rec, dict) or rec.get("status") != "start":
            continue
        try:
            t = datetime.fromisoformat(rec.get("ts", "")).timestamp()
        except (ValueError, TypeError):
            continue
        if t >= cut:
            n += 1
    return n


def _write_start_row(agent, frm, seq, task):
    rec = {"ts": _now_iso(), "agent": agent, "from": frm, "seq": str(seq),
           "model": MODEL, "task": (task or "")[:100], "status": "start"}
    with open(LEDGER(), "a", encoding="utf-8") as f:
        f.write(json.dumps(rec, ensure_ascii=False) + "\n")


# ── per-target spawn lock (with stale reclaim) ───────────────────────────────
def _acquire_target_lock(lockdir):
    os.makedirs(LOCK_DIR(), exist_ok=True)
    try:
        os.mkdir(lockdir)
        return True
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise
    # exists — reclaim if stale (crashed/hung/host-restart left it behind)
    try:
        age = time.time() - os.path.getmtime(lockdir)
    except OSError:
        age = 0
    if age > LOCK_STALE:
        try:
            os.rmdir(lockdir)
            os.mkdir(lockdir)
            return True
        except OSError:
            return False
    return False


def _release_target_lock(lockdir):
    try:
        os.rmdir(lockdir)
    except OSError:
        pass


def _post(frm, to, msg):
    try:
        subprocess.run(
            [sys.executable, os.path.join(HERE, "office_bus.py"), "append", frm, to, "chat", "null", msg],
            cwd=REPO, check=False,
        )
    except OSError:
        pass


def _dispatch(rec):
    target = (rec.get("to") or "").strip()
    frm = rec.get("from", "?")
    seq = rec.get("seq")
    task = rec.get("msg", "")

    if rec.get("arc") == "bg":
        _post("system", frm, "(summon #{0} refused: background brothers can't summon others — no chains)".format(seq))
        return
    if not target.startswith("agent") or "," in target or target in ("all", ""):
        _post("system", frm, "(summon #{0} skipped: must target exactly one brother, got '{1}')".format(seq, target))
        return
    if target == frm:
        return  # don't summon yourself
    if _is_live(target):
        print("[office-dispatch] summon #{0} -> {1}: live tab, routed to his watch".format(seq, target))
        sys.stdout.flush()
        return

    # no-double: claim the per-target lock first (reclaim if stale)
    lockdir = os.path.join(LOCK_DIR(), target + ".lock")
    if not _acquire_target_lock(lockdir):
        _post("system", frm, "(summon #{0}: {1} is already handling a summon — try again when he's free)".format(seq, target))
        return

    # cap: count + write the "start" row SYNCHRONOUSLY under the ledger lock, so a
    # burst of summons can't all pass the cap before any ledger row is recorded.
    lk = _ledger_lock()
    try:
        if _spawn_count_recent() >= SPAWN_CAP:
            _release_target_lock(lockdir)
            _post("system", frm, "(summon #{0} held: spawn cap {1}/{2}s reached — ask Hemanth)".format(seq, SPAWN_CAP, CAP_WINDOW))
            return
        _write_start_row(target, frm, seq, task)
    finally:
        _ledger_unlock(lk)

    # spawn detached (spawn_brother.sh holds the lock for the run, removes on exit)
    try:
        subprocess.Popen(
            [_bash(), os.path.join(HERE, "spawn_brother.sh"), target, frm, str(seq), task, lockdir],
            cwd=REPO,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            close_fds=True,
        )
        print("[office-dispatch] summon #{0} -> {1}: spawned background brother".format(seq, target))
        sys.stdout.flush()
    except OSError as ex:
        _release_target_lock(lockdir)
        _post("system", frm, "(summon #{0}: failed to spawn {1}: {2})".format(seq, target, ex))


def main():
    cur = _read_cursor()
    if cur is None:
        cur = _max_seq()  # seed at startup so we never re-process the backlog
        _write_cursor(cur)
    print("[office-dispatch] watching for summons (from seq {0}, interval {1}s, cap {2}/{3}s)".format(
        cur, INTERVAL, SPAWN_CAP, CAP_WINDOW))
    sys.stdout.flush()
    while True:
        try:
            for rec in _new_summons(cur):
                _dispatch(rec)
                # advance + persist the cursor PER summon, so a crash can't replay it
                cur = max(cur, rec.get("seq", cur))
                _write_cursor(cur)
        except Exception as ex:  # never let the loop die
            print("[office-dispatch] error: {0}".format(ex))
            sys.stdout.flush()
        time.sleep(INTERVAL)


if __name__ == "__main__":
    main()
