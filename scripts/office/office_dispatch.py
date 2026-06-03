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
  OFFICE_DIR, OFFICE_BUS, OFFICE_DISPATCH_INTERVAL, OFFICE_LIVENESS_SEC,
  OFFICE_SPAWN_CAP, OFFICE_SPAWN_CAP_WINDOW, OFFICE_LOCK_STALE, OFFICE_BROTHER_MODEL.
"""
import os
import sys
import json
import time
import errno
import atexit
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


def DELIVERY_LOG():
    return os.path.join(_dir(), ".office_delivery.log")


def CURSOR_FILE():
    return os.path.join(_dir(), ".office_dispatch.seq")


def DISPATCH_LOCK():
    return os.path.join(_dir(), ".office_dispatch.lock")


def DISPATCH_BEAT():
    return os.path.join(HB_DIR(), "dispatcher.beat")


INTERVAL = int(os.environ.get("OFFICE_DISPATCH_INTERVAL", "3"))
LIVENESS_WINDOW_SEC = int(os.environ.get("OFFICE_LIVENESS_SEC", "30"))  # Track C #10: ONE Office-wide liveness window (office_status.py roster reads the SAME env) — watch-heartbeat freshness (s)
SPAWN_CAP = int(os.environ.get("OFFICE_SPAWN_CAP", "5"))               # max spawns / window
CAP_WINDOW = int(os.environ.get("OFFICE_SPAWN_CAP_WINDOW", "3600"))    # window (s)
LOCK_STALE = int(os.environ.get("OFFICE_LOCK_STALE", "1800"))         # stale per-target lock (s)
ACTIVE_WINDOW = int(os.environ.get("OFFICE_ACTIVE_WINDOW", "90"))     # recent bus post => live (s)
ACK_TIMEOUT = int(os.environ.get("OFFICE_ACK_TIMEOUT", "90"))         # wait for a live-routed brother to reply (s)

# Brothers that run their OWN engines (Codex / DeepSeek) — MUST NOT be spawned via
# `claude -p`.  Summoning them is a courier job for Hemanth, not the dispatcher.
NON_CLAUDE_BROTHERS = frozenset({"agent7", "agent9"})
ACK_KINDS = ("chat", "ack", "blocked")                               # post kinds that count as a real reply (NOT 'activity')
FALLBACK_RETRY = int(os.environ.get("OFFICE_FALLBACK_RETRY", "30"))   # held-fallback retry backoff (s)
FALLBACK_MAX_TRIES = int(os.environ.get("OFFICE_FALLBACK_MAX_TRIES", "10"))  # surface to Hemanth after N held attempts
MODEL = os.environ.get("OFFICE_BROTHER_MODEL", "opus")  # summoned brothers wake as their real self, not a cheap shadow
DISPATCH_STALE = int(os.environ.get("OFFICE_DISPATCH_STALE", "30"))  # dispatcher beat/lock stale threshold (s)


def _now_iso():
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")


def _epoch(ts):
    try:
        return int(datetime.fromisoformat(ts).timestamp())
    except (TypeError, ValueError):
        return 0


def _fate(seq, to, fate, detail=""):
    """Append one durable JSONL fate record for a summon — its trail through the
    dispatcher (queued -> routed/refused/skipped -> spawn_attempt -> spawn_ok/held/error,
    plus fallback_fire/retry/gave_up). Answers 'what happened to this summon?' after the
    fact: did the target receive it, did the spawn fail, why was it held. Append-only
    (.office_delivery.log); best-effort — a logging failure must NEVER break dispatch."""
    rec = {"ts": _now_iso(), "seq": seq, "to": to, "fate": fate, "detail": detail}
    try:
        os.makedirs(_dir(), exist_ok=True)
        with open(DELIVERY_LOG(), "a", encoding="utf-8") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
    except OSError:
        pass


def _recently_active(agent, now, window, bus_records):
    """A brother who POSTED to the bus within `window`s counts as live even with no
    fresh heartbeat — covers a busy brother who never started (or lost) his watch, so
    he is never needlessly duplicate-spawned."""
    latest = 0
    for r in bus_records:
        if r.get("from") == agent:
            t = _epoch(r.get("ts", ""))
            if t > latest:
                latest = t
    return bool(latest) and (now - latest) < window


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
        return (time.time() - beat) < LIVENESS_WINDOW_SEC
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


# ── pure routing logic (unit-testable, no side effects) ──────────────────────
def classify_summon(frm, target, arc, is_live):
    """Pure routing decision for a summon. Returns (action, reason). Side-effect-free
    so it is unit-testable; _dispatch() acts on the verdict."""
    target = (target or "").strip()
    if arc == "bg":
        return ("refuse_chain", "background brothers can't summon others")
    if not target.startswith("agent") or "," in target or target in ("all", ""):
        return ("skip_badtarget", "must target exactly one brother, got '{0}'".format(target))
    if target == frm:
        return ("skip_self", "don't summon yourself")
    if target in NON_CLAUDE_BROTHERS:
        return ("refuse_nonclaude", "{0} runs his own engine — courier Hemanth to open his tab".format(target))
    if is_live:
        return ("route_live", "live tab / recently active — routed to his watch")
    return ("spawn", "idle/closed — spawn a background brother")


def resolve_pending(pending, bus_records, now):
    """Re-evaluate live-routed summons. A pending is RESOLVED (dropped) when its target
    posted anything with seq > the summon seq (he answered / is clearly alive). Otherwise,
    once `now` passes the deadline it goes to fallback (a background spawn). Returns
    (still_open, to_fallback) — both lists. Pure: no side effects.

    Only a REAL reply counts as an ack: kinds in ACK_KINDS (chat/ack/blocked). An
    'activity' line (commit-mirror) is NOT an ack — watch-peek skips activity, so it
    never woke him; counting it would let a brother's own commit cancel his own
    un-delivered summon (the black-hole this whole mechanism exists to prevent)."""
    max_posted = {}
    for r in bus_records:
        if r.get("kind") not in ACK_KINDS:
            continue
        frm = r.get("from")
        s = r.get("seq")
        if isinstance(s, int) and (frm not in max_posted or s > max_posted[frm]):
            max_posted[frm] = s
    still, fallback = [], []
    for p in pending:
        if max_posted.get(p["target"], -1) > p["seq"]:
            continue  # answered / alive -> resolved
        if now >= p["deadline"]:
            fallback.append(p)
        else:
            still.append(p)
    return still, fallback


def reconcile_fallback(fallback, spawn_results, now, retry_delay, max_tries):
    """After attempting each fallback spawn, decide what stays pending. `spawn_results`
    is aligned with `fallback`: True = spawned (resolved), False = HELD (per-target lock
    busy / spawn cap reached / OSError). A HELD entry is KEPT — deadline bumped to
    now+retry_delay, tries+1 — so it RETRIES instead of being silently dropped (the bug
    that re-opened the black-hole). After max_tries held attempts it's surfaced to the
    summoner (gave_up) rather than retried forever. Returns (kept_pending, gave_up)."""
    kept, gave_up = [], []
    for p, ok in zip(fallback, spawn_results):
        if ok:
            continue  # spawned -> resolved
        tries = p.get("tries", 0) + 1
        if tries >= max_tries:
            gave_up.append(p)
        else:
            kept.append({**p, "deadline": now + retry_delay, "tries": tries})
    return kept, gave_up


# ── pending-summons persistence (survives a dispatcher restart) ──────────────
def PENDING_FILE():
    return os.path.join(_dir(), ".office_pending.jsonl")


def _load_pending():
    out = []
    try:
        for line in open(PENDING_FILE(), encoding="utf-8"):
            line = line.strip()
            if not line:
                continue
            try:
                out.append(json.loads(line))
            except (json.JSONDecodeError, ValueError):
                continue  # skip ONE corrupt line; don't wipe ALL fallback memory
    except OSError:
        pass
    return out


def _save_pending(pending):
    os.makedirs(_dir(), exist_ok=True)
    tmp = PENDING_FILE() + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        for p in pending:
            f.write(json.dumps(p, ensure_ascii=False) + "\n")
    os.replace(tmp, PENDING_FILE())


# ── spawn (shared by the idle path AND the ack-timeout fallback) ──────────────
def _spawn_for(target, frm, seq, task, quiet=False):
    """Spawn `target` as a background brother under the per-target lock + spawn cap.
    Returns True if spawned, False if held (lock busy or cap reached). `quiet` suppresses
    the held/cap/error bus notice — used by the ack-timeout fallback RETRIES, which would
    otherwise re-post 'already handling' every FALLBACK_RETRY while a brother stays busy
    (the give-up after FALLBACK_MAX_TRIES is the single surfacing instead)."""
    _fate(seq, target, "spawn_attempt", "from={0} quiet={1}".format(frm, quiet))
    lockdir = os.path.join(LOCK_DIR(), target + ".lock")
    if not _acquire_target_lock(lockdir):
        _fate(seq, target, "spawn_held", "per-target lock busy ({0} already handling)".format(target))
        if not quiet:
            _post("system", frm, "(summon #{0}: {1} is already handling a summon — try again when he's free)".format(seq, target))
        return False

    # cap: count + write the "start" row SYNCHRONOUSLY under the ledger lock, so a
    # burst of summons can't all pass the cap before any ledger row is recorded.
    lk = _ledger_lock()
    try:
        if _spawn_count_recent() >= SPAWN_CAP:
            _release_target_lock(lockdir)
            _fate(seq, target, "spawn_held", "spawn cap {0}/{1}s reached".format(SPAWN_CAP, CAP_WINDOW))
            if not quiet:
                _post("system", frm, "(summon #{0} held: spawn cap {1}/{2}s reached — ask Hemanth)".format(seq, SPAWN_CAP, CAP_WINDOW))
            return False
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
        _fate(seq, target, "spawn_ok", "background brother spawned (model={0})".format(MODEL))
        return True
    except OSError as ex:
        _release_target_lock(lockdir)
        _fate(seq, target, "spawn_error", str(ex))
        if not quiet:
            _post("system", frm, "(summon #{0}: failed to spawn {1}: {2})".format(seq, target, ex))
        return False


def _dispatch(rec):
    """Route a single summon. Returns a PENDING entry dict when the summon was routed to
    a (supposedly) live brother — the main loop ack-or-fallback-checks it later — else None."""
    target = (rec.get("to") or "").strip()
    frm = rec.get("from", "?")
    seq = rec.get("seq")
    task = rec.get("msg", "")
    _fate(seq, target, "queued", "from={0} task={1!r}".format(frm, (task or "")[:60]))

    # liveness = fresh heartbeat OR a recent bus post (so a busy-but-live brother with
    # no watch isn't duplicate-spawned, and the routing self-heals via fallback either way)
    live = _is_live(target) or _recently_active(target, time.time(), ACTIVE_WINDOW, list(_iter_bus()))
    action, reason = classify_summon(frm, target, rec.get("arc"), live)

    if action == "refuse_chain":
        _fate(seq, target, "refused", reason)
        _post("system", frm, "(summon #{0} refused: background brothers can't summon others — no chains)".format(seq))
        return None
    if action == "skip_badtarget":
        _fate(seq, target, "skipped", reason)
        _post("system", frm, "(summon #{0} skipped: must target exactly one brother, got '{1}')".format(seq, target))
        return None
    if action == "skip_self":
        _fate(seq, target, "skipped", reason)
        return None
    if action == "refuse_nonclaude":
        _fate(seq, target, "refused", reason)
        _post("system", frm, "(summon #{0}: {1} runs his own engine (Codex/DeepSeek) — bg spawn unavailable; open his tab or courier via Hemanth)".format(seq, target))
        return None
    if action == "route_live":
        # DON'T forget it: record a pending entry so a wrong "live" guess (zombie
        # heartbeat / heads-down tab) self-heals to a background spawn after the deadline.
        _fate(seq, target, "routed_live", "{0}; fallback-spawn if no reply in {1}s".format(reason, ACK_TIMEOUT))
        print("[office-dispatch] summon #{0} -> {1}: {2}; fallback-spawn if no reply in {3}s".format(
            seq, target, reason, ACK_TIMEOUT))
        sys.stdout.flush()
        return {"target": target, "seq": seq, "frm": frm, "task": task,
                "deadline": time.time() + ACK_TIMEOUT}
    # action == "spawn"
    _spawn_for(target, frm, seq, task)
    return None


# ── dispatcher self-supervision (singleton + heartbeat) ──────────────────────
def _dispatcher_beat():
    """Stamp the dispatcher's own heartbeat so the roster + the night-watch foreman can
    SEE it's alive and restart it if not — a dead dispatcher must never be invisible
    (every closed-tab summon depends on it)."""
    try:
        os.makedirs(HB_DIR(), exist_ok=True)
        tmp = DISPATCH_BEAT() + ".tmp"
        with open(tmp, "w") as f:
            f.write(str(int(time.time())))
        os.replace(tmp, DISPATCH_BEAT())
    except OSError:
        pass


def _acquire_dispatch_singleton():
    """Atomic mkdir singleton so TWO dispatchers can't run at once (which would
    double-spawn every summon + split the cap). Reclaims a stale lock left by a crash or
    host restart. Returns True if we own it, False if another LIVE dispatcher holds it."""
    lock = DISPATCH_LOCK()
    os.makedirs(_dir(), exist_ok=True)
    try:
        os.mkdir(lock)
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise
        try:
            age = time.time() - os.path.getmtime(lock)
        except OSError:
            age = 0
        if age <= DISPATCH_STALE:
            return False  # another dispatcher refreshed it recently — it's alive
        shutil.rmtree(lock, ignore_errors=True)  # stale: reclaim
        try:
            os.mkdir(lock)
        except OSError:
            return False
    try:
        with open(os.path.join(lock, "pid"), "w") as f:
            f.write("{0} {1}".format(os.getpid(), _now_iso()))
    except OSError:
        pass
    return True


def _refresh_dispatch_lock():
    try:
        os.utime(DISPATCH_LOCK(), None)
    except OSError:
        pass


def _release_dispatch_singleton():
    shutil.rmtree(DISPATCH_LOCK(), ignore_errors=True)


def main():
    if not _acquire_dispatch_singleton():
        print("[office-dispatch] another dispatcher already holds the singleton — exiting")
        sys.stdout.flush()
        return
    atexit.register(_release_dispatch_singleton)
    cur = _read_cursor()
    if cur is None:
        cur = _max_seq()  # seed at startup so we never re-process the backlog
        _write_cursor(cur)
    pending = _load_pending()
    print("[office-dispatch] watching for summons (from seq {0}, interval {1}s, cap {2}/{3}s, ack {4}s)".format(
        cur, INTERVAL, SPAWN_CAP, CAP_WINDOW, ACK_TIMEOUT))
    sys.stdout.flush()
    while True:
        _dispatcher_beat()        # prove we're alive — roster + night-watch foreman watch this
        _refresh_dispatch_lock()  # keep the singleton fresh so a peer doesn't reclaim us
        try:
            for rec in _new_summons(cur):
                p = _dispatch(rec)
                if p:
                    pending.append(p)
                    _save_pending(pending)
                # advance + persist the cursor PER summon, so a crash can't replay it
                cur = max(cur, rec.get("seq", cur))
                _write_cursor(cur)
            # ack-or-fallback: a live-routed summon the target never answered (zombie
            # heartbeat / closed tab) falls back to a background spawn past its deadline.
            if pending:
                still, fallback = resolve_pending(pending, list(_iter_bus()), time.time())
                # false-escalation guard (bus seq 827): a brother can go LIVE (fresh
                # heartbeat) in the window AFTER his route-time liveness check failed but
                # BEFORE this deadline — a heads-down brother working without posting an ack.
                # Re-check _is_live right before the fallback fires; if he's live NOW, abort
                # the escalation silently and keep waiting, rather than duplicate-spawn a
                # background copy of a brother who's already alive at the keyboard.
                ready = []
                for p in fallback:
                    if _is_live(p["target"]):
                        still.append(p)          # live now -> keep pending, don't escalate
                    else:
                        ready.append(p)
                fallback = ready
                spawn_results = []
                for p in fallback:
                    print("[office-dispatch] summon #{0} -> {1}: no reply in {2}s — falling back to a background spawn".format(
                        p["seq"], p["target"], ACK_TIMEOUT))
                    sys.stdout.flush()
                    _fate(p["seq"], p["target"], "fallback_fire", "no ack in {0}s (try {1}) — escalating to background spawn".format(
                        ACK_TIMEOUT, p.get("tries", 0)))
                    # quiet=True: a fallback that stays HELD (target busy) must not re-post
                    # 'already handling' every retry — the give-up after FALLBACK_MAX_TRIES
                    # is the single surfacing (kills the bus-seq-883+ retry spam).
                    spawn_results.append(_spawn_for(p["target"], p["frm"], p["seq"], p["task"], quiet=True))
                # a HELD fallback (lock busy / cap / error) stays pending + retries — never
                # silently dropped; surfaced to the summoner only after FALLBACK_MAX_TRIES.
                kept, gave_up = reconcile_fallback(fallback, spawn_results, time.time(), FALLBACK_RETRY, FALLBACK_MAX_TRIES)
                for p in kept:
                    _fate(p["seq"], p["target"], "retry", "held — retry in {0}s (try {1}/{2})".format(
                        FALLBACK_RETRY, p.get("tries"), FALLBACK_MAX_TRIES))
                for p in gave_up:
                    _fate(p["seq"], p["target"], "gave_up", "undelivered after {0} attempts — surfaced to {1}".format(
                        FALLBACK_MAX_TRIES, p["frm"]))
                    _post("system", p["frm"], "(summon #{0} to {1} could NOT be delivered after {2} attempts — he may be unreachable; please check him directly)".format(
                        p["seq"], p["target"], FALLBACK_MAX_TRIES))
                new_pending = still + kept
                if new_pending != pending:
                    pending = new_pending
                    _save_pending(pending)
        except Exception as ex:  # never let the loop die
            print("[office-dispatch] error: {0}".format(ex))
            sys.stdout.flush()
        time.sleep(INTERVAL)


if __name__ == "__main__":
    main()
