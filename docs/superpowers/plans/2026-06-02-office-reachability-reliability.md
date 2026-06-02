# Office Reachability Reliability — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (inline, agent0's own infra) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Office summons reliably reach a brother even when the heartbeat-based liveness guess is wrong — so a summon is never silently black-holed and a busy-but-live brother is never needlessly duplicate-spawned.

**Architecture:** The dispatcher infers liveness from a heartbeat file that `office_watch.sh` rewrites every 3s. That signal is decoupled from the actual Claude session: an **orphaned** watch (closed tab, surviving bash loop) keeps the heartbeat fresh for a dead session → dispatcher routes the summon to a ghost and forgets it (cursor advances), so it vanishes. Conversely a **live brother with no watch** (Agent 3 mid-smoke) has no heartbeat → he gets a duplicate spawn. Fix = defense-in-depth, two layers: (1) **activity-based liveness** — a brother who posted to the bus recently counts as live even without a heartbeat; (2) **ack-or-fallback** — when a summon is routed to a "live" watch, record it as *pending* with a deadline; if the target posts nothing by the deadline, automatically fall back to a background spawn. A wrong liveness guess self-heals instead of dropping the summon.

**Tech Stack:** Python 3 (stdlib only), the existing `scripts/office/` pure-projection + persistent-loop pattern; tests in the `scripts/office/tests/` `check(cond, label)` style (no pytest).

**Out of scope (documented follow-ups):**
- **Orphan-watch reaping** — killing watch bash loops whose Claude session died. With ack-or-fallback the orphan black-hole is *auto-healed* (fallback fires after the deadline), so reaping is hygiene/latency, not correctness. Separate follow-up.
- **Ask-escalation calming** (`office_asks.py`) — the no-rush note to a busy lane-holder that escalated to agent0 (#825). Different subsystem; separate plan.

---

## File Structure

- `scripts/office/office_dispatch.py` — MODIFY. Add `_epoch()`, `_recently_active()`, pure `classify_summon()`, pure `resolve_pending()`; refactor `_dispatch()` to return a pending entry on the live path; factor the spawn body into `_spawn_for()`; wire pending re-check + persistence into `main()`.
- `scripts/office/tests/test_dispatch.py` — CREATE. Pure-logic tests for `classify_summon`, `_recently_active`, `resolve_pending` in the existing `check(cond, label)` style.
- `agents/.office_pending.jsonl` — RUNTIME state (git-ignored like other `.office_*`). Persists pending live-routed summons so a dispatcher restart can't drop them.

---

## Task 1: Activity-based liveness + pure summon classification

**Files:**
- Modify: `scripts/office/office_dispatch.py`
- Test: `scripts/office/tests/test_dispatch.py` (create)

- [ ] **Step 1: Write the failing test**

Create `scripts/office/tests/test_dispatch.py`:

```python
#!/usr/bin/env python3
"""Tests for office_dispatch.py pure logic. Mirrors test_asks.py: check()/main()."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import office_dispatch as D  # noqa: E402

fails = 0


def check(cond, label):
    global fails
    print("ok:" if cond else "FAIL:", label)
    if not cond:
        fails += 1


def rec(seq, frm, to, kind="chat", msg="", arc=None, ts="2026-06-02T20:00:00+05:30"):
    return {"seq": seq, "from": frm, "to": to, "kind": kind, "arc": arc, "msg": msg, "ts": ts}


def test_recently_active():
    now = D._epoch("2026-06-02T20:01:00+05:30")  # 60s after the post below
    bus = [rec(10, "agent3", "all", msg="P0 shipped", ts="2026-06-02T20:00:00+05:30")]
    check(D._recently_active("agent3", now, 90, bus), "active: a post 60s ago is within a 90s window")
    check(not D._recently_active("agent3", now, 30, bus), "active: a post 60s ago is outside a 30s window")
    check(not D._recently_active("agent2", now, 90, bus), "active: a brother who never posted is not active")


def test_classify_summon():
    # background brothers can't summon (no chains)
    a, _ = D.classify_summon("agent2", "agent3", "bg", is_live=False)
    check(a == "refuse_chain", "classify: arc='bg' summon is refused (no chains)")
    # malformed target
    a, _ = D.classify_summon("agent0", "all", None, is_live=False)
    check(a == "skip_badtarget", "classify: target 'all' is rejected (must be one brother)")
    a, _ = D.classify_summon("agent0", "agent2,agent3", None, is_live=False)
    check(a == "skip_badtarget", "classify: multi-target is rejected")
    # self
    a, _ = D.classify_summon("agent0", "agent0", None, is_live=False)
    check(a == "skip_self", "classify: summoning yourself is a no-op")
    # the two real routes
    a, _ = D.classify_summon("agent0", "agent3", None, is_live=True)
    check(a == "route_live", "classify: a live target routes to his watch")
    a, _ = D.classify_summon("agent0", "agent2", None, is_live=False)
    check(a == "spawn", "classify: a non-live target spawns a background brother")


def main():
    test_recently_active()
    test_classify_summon()
    print("\n{0}".format("ALL PASS" if fails == 0 else "{0} FAILED".format(fails)))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python scripts/office/tests/test_dispatch.py`
Expected: FAIL — `AttributeError: module 'office_dispatch' has no attribute '_epoch'` (functions not defined yet).

- [ ] **Step 3: Write minimal implementation**

In `scripts/office/office_dispatch.py`, after the `_now_iso()` helper (~line 89), add:

```python
def _epoch(ts):
    from datetime import datetime
    try:
        return int(datetime.fromisoformat(ts).timestamp())
    except (TypeError, ValueError):
        return 0


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
```

Add the activity window env knob near the other tuning constants (~line 84):

```python
ACTIVE_WINDOW = int(os.environ.get("OFFICE_ACTIVE_WINDOW", "90"))    # recent bus post => live (s)
ACK_TIMEOUT = int(os.environ.get("OFFICE_ACK_TIMEOUT", "90"))        # wait for a live-routed brother to respond (s)
```

Add the pure classifier just above `_dispatch()` (~line 248):

```python
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
    if is_live:
        return ("route_live", "live tab / recently active — routed to his watch")
    return ("spawn", "idle/closed — spawn a background brother")
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python scripts/office/tests/test_dispatch.py`
Expected: PASS — `ALL PASS`.

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_dispatch.py scripts/office/tests/test_dispatch.py
git commit -m "feat(office): activity-based liveness + pure summon classifier (reachability reliability T1)"
```

---

## Task 2: Ack-or-fallback pending tracker

**Files:**
- Modify: `scripts/office/office_dispatch.py` (`_dispatch`, `main`, new `_spawn_for`, `resolve_pending`, pending persistence)
- Test: `scripts/office/tests/test_dispatch.py`

- [ ] **Step 1: Write the failing test** — append to `test_dispatch.py`:

```python
def test_resolve_pending():
    base_ts = "2026-06-02T20:00:00+05:30"
    t0 = D._epoch(base_ts)
    # a summon #20 routed-live to agent4 at t0, deadline t0+90
    pending = [{"target": "agent4", "seq": 20, "frm": "agent0", "task": "x", "deadline": t0 + 90}]

    # (a) target answered (posted seq 21 > 20): resolved, nothing pending, no fallback
    bus_ack = [rec(21, "agent4", "agent0", msg="on it", ts=base_ts)]
    still, fb = D.resolve_pending(pending, bus_ack, now=t0 + 30)
    check(still == [] and fb == [], "pending: a reply (seq>summon) resolves the summon")

    # (b) no answer, before deadline: stays pending, no fallback yet
    still, fb = D.resolve_pending(pending, [], now=t0 + 30)
    check(len(still) == 1 and fb == [], "pending: silence before deadline keeps it open")

    # (c) no answer, past deadline: falls back to a spawn, dropped from pending
    still, fb = D.resolve_pending(pending, [], now=t0 + 120)
    check(still == [] and len(fb) == 1, "pending: silence past deadline triggers a fallback spawn")
    check(fb[0]["target"] == "agent4", "pending: fallback carries the original target")

    # (d) an EARLIER post (seq 19 < 20) does NOT count as an answer
    bus_old = [rec(19, "agent4", "agent0", msg="earlier", ts=base_ts)]
    still, fb = D.resolve_pending(pending, bus_old, now=t0 + 120)
    check(len(fb) == 1, "pending: a pre-summon post is not an ack")
```

And register it in `main()`:

```python
    test_resolve_pending()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python scripts/office/tests/test_dispatch.py`
Expected: FAIL — `AttributeError: module 'office_dispatch' has no attribute 'resolve_pending'`.

- [ ] **Step 3: Write minimal implementation**

(3a) Add the pure resolver above `_dispatch()`:

```python
def resolve_pending(pending, bus_records, now):
    """Re-evaluate live-routed summons. A pending is RESOLVED (dropped) when its target
    posted anything with seq > the summon seq (he answered / is clearly alive). Otherwise,
    once `now` passes the deadline it goes to fallback (a background spawn). Returns
    (still_open, to_fallback) — both lists. Pure: no side effects."""
    # highest seq each agent has posted
    max_posted = {}
    for r in bus_records:
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
```

(3b) Factor the spawn body out of `_dispatch` into a reusable `_spawn_for(target, frm, seq, task)` that does lock + cap + ledger + `Popen(spawn_brother.sh ...)` (move lines ~268-300 verbatim into it, returning True on spawn / False if held by lock or cap). `_dispatch` then calls `_spawn_for` for the `spawn` action.

(3c) Change `_dispatch(rec)` to consult `classify_summon` and, on `route_live`, RETURN a pending entry instead of just printing:

```python
def _dispatch(rec):
    target = (rec.get("to") or "").strip()
    frm = rec.get("from", "?")
    seq = rec.get("seq")
    task = rec.get("msg", "")

    live = _is_live(target) or _recently_active(target, time.time(), ACTIVE_WINDOW, list(_iter_bus()))
    action, reason = classify_summon(frm, target, rec.get("arc"), live)

    if action == "refuse_chain":
        _post("system", frm, "(summon #{0} refused: background brothers can't summon others — no chains)".format(seq))
        return None
    if action == "skip_badtarget":
        _post("system", frm, "(summon #{0} skipped: must target exactly one brother, got '{1}')".format(seq, target))
        return None
    if action in ("skip_self",):
        return None
    if action == "route_live":
        print("[office-dispatch] summon #{0} -> {1}: {2}; will fallback-spawn if no reply in {3}s".format(
            seq, target, reason, ACK_TIMEOUT))
        sys.stdout.flush()
        return {"target": target, "seq": seq, "frm": frm, "task": task, "deadline": time.time() + ACK_TIMEOUT}
    # action == "spawn"
    _spawn_for(target, frm, seq, task)
    return None
```

(3d) Add pending persistence helpers (mirror the cursor pattern):

```python
def PENDING_FILE():
    return os.path.join(_dir(), ".office_pending.jsonl")


def _load_pending():
    try:
        out = []
        for line in open(PENDING_FILE(), encoding="utf-8"):
            line = line.strip()
            if line:
                out.append(json.loads(line))
        return out
    except (OSError, ValueError):
        return []


def _save_pending(pending):
    os.makedirs(_dir(), exist_ok=True)
    tmp = PENDING_FILE() + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        for p in pending:
            f.write(json.dumps(p, ensure_ascii=False) + "\n")
    os.replace(tmp, PENDING_FILE())
```

(3e) Wire into `main()`:

```python
def main():
    cur = _read_cursor()
    if cur is None:
        cur = _max_seq()
        _write_cursor(cur)
    pending = _load_pending()
    print("[office-dispatch] watching for summons (from seq {0}, interval {1}s, cap {2}/{3}s)".format(
        cur, INTERVAL, SPAWN_CAP, CAP_WINDOW))
    sys.stdout.flush()
    while True:
        try:
            for rec in _new_summons(cur):
                p = _dispatch(rec)
                if p:
                    pending.append(p)
                    _save_pending(pending)
                cur = max(cur, rec.get("seq", cur))
                _write_cursor(cur)
            if pending:
                still, fallback = resolve_pending(pending, list(_iter_bus()), time.time())
                for p in fallback:
                    print("[office-dispatch] summon #{0} -> {1}: no reply in {2}s — falling back to a background spawn".format(
                        p["seq"], p["target"], ACK_TIMEOUT))
                    sys.stdout.flush()
                    _spawn_for(p["target"], p["frm"], p["seq"], p["task"])
                if fallback or len(still) != len(pending):
                    pending = still
                    _save_pending(pending)
        except Exception as ex:
            print("[office-dispatch] error: {0}".format(ex))
            sys.stdout.flush()
        time.sleep(INTERVAL)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python scripts/office/tests/test_dispatch.py`
Expected: PASS — `ALL PASS`.

- [ ] **Step 5: Regression — the existing office tests still pass**

Run: `python scripts/office/tests/test_asks.py && python scripts/office/tests/test_office.py && python scripts/office/tests/test_status.py`
Expected: each prints `ALL PASS`.

- [ ] **Step 6: Commit**

```bash
git add scripts/office/office_dispatch.py scripts/office/tests/test_dispatch.py
git commit -m "feat(office): ack-or-fallback delivery — a black-holed summon self-heals to a bg spawn (reachability reliability T2)"
```

---

## Task 3: Live validation

**Files:** none (operational verification). The dispatcher must be restarted to pick up the new code.

- [ ] **Step 1: Restart the dispatcher** so the running loop has the new logic.

```bash
# kill the running dispatcher (cmd + python child), then relaunch detached
powershell -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'office_dispatch.py' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
```
Relaunch via the normal path (`open_office.bat` starts it alongside `office_web.py`), or detached:
`python scripts/office/office_dispatch.py` in a background process.

- [ ] **Step 2: Fake-live black-hole test** — summon a CLOSED brother whose heartbeat is stale and confirm the **fallback** path delivers.

Pick a brother with a stale heartbeat + no live tab (e.g. agent5). Confirm `_is_live` is false and there's no recent post, then:
```bash
bash scripts/office/office_summon.sh "@agent5" "Reachability validation: read ONLY your latest recap, post ONE line — your domain + last ship. Then stop."
```
Expected: within `ACK_TIMEOUT` the dispatcher logs `falling back to a background spawn` (or, if no watch ever existed, it spawns immediately as `spawn`); agent5 posts a `RESULT:` line. **A summon to a non-live brother lands.**

- [ ] **Step 3: Genuinely-live test** — confirm a live brother is NOT needlessly duplicate-spawned.

With a brother who posted within `ACTIVE_WINDOW` (or has a fresh real heartbeat), summon him and confirm the dispatcher logs `route_live` and NO spawn ledger `start` row is written for him (he answers via his own watch). Check:
```bash
tail -3 agents/.office_spawns.jsonl
```
Expected: no new `start` row for the live target within the window; he replies on the bus.

- [ ] **Step 4: Confirm pending file drains** — after both tests, `agents/.office_pending.jsonl` should be empty (all resolved or fell back).

```bash
cat agents/.office_pending.jsonl 2>/dev/null; echo "(end)"
```
Expected: empty.

---

## Self-Review

1. **Spec coverage:** activity-liveness (T1) ✓; ack-or-fallback so a wrong guess self-heals (T2) ✓; live validation of both the black-hole and the duplicate-spawn directions (T3) ✓. Orphan-reaping + ask-escalation calming explicitly deferred with rationale ✓.
2. **Placeholder scan:** none — every step has concrete code/commands.
3. **Type consistency:** pending entries use keys `{target, seq, frm, task, deadline}` consistently across `_dispatch`, `resolve_pending`, `_save_pending`, and `main`. `classify_summon` action strings (`refuse_chain`/`skip_badtarget`/`skip_self`/`route_live`/`spawn`) match between Task 1 impl, its test, and `_dispatch`'s consumption.
</content>
</invoke>
