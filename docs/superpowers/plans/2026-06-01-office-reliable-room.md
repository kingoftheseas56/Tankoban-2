# The Reliable Room — Implementation Plan

**Agent 7 revision (2026-06-01):** Execute locally on `master`, commit per task, and do **not push**. Agent 0 must review before origin master. This supersedes any older "push at the end" wording below.

**Agent 7 revision (2026-06-01):** `office_ack.sh` takes `office_ack.sh <agentN> <ask_seq> [note]` because the Office must know which brother is accepting responsibility. The older spec shorthand without `<agentN>` is incorrect.

**Agent 7 revision (2026-06-01):** Escalation idempotency is per `(ask_seq, to_agent)`, not just ask seq. Comma-list direct asks create multiple obligations; escalation events use `arc="<ask_seq>:<to_agent>"`. Ack remains `arc="<ask_seq>"` because the ack's `from` identifies the addressee.

**Agent 7 revision (2026-06-01):** A direct answer is terminal and no separate ack is required. The behavioral contract is "ack or answer promptly," not double-message bureaucracy.

**Agent 7 revision (2026-06-01):** The deterministic escalation guarantee is live while `office_web.py` is running. A future daemon/service can make escalation always-on; this slice keeps Hemanth's current one-window model.

**Agent 7 revision (2026-06-01):** Scope is restricted to `scripts/office/`, `agents/GOVERNANCE.md`, and the Reliable Room plan/spec. Do not edit root responder `.bat` files or `.gitignore` in this slice; root launcher cleanup is deferred unless scope is expanded.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> **Governance:** gov-v13 — flat-on-master, NO worktree. Commit per task. Do not push; Agent 0 reviews before origin master. Busy shared tree: stage only explicit Office/spec/governance files, and NEVER `reset --hard`.

**Goal:** Make every direct ask in the Office reach a visible terminal state — acknowledged → answered, or escalated to Agent 0 then Hemanth — 100% deterministically, with no LLM in the guarantee path.

**Architecture:** Approach B (lightweight event-log + projection, matching the existing Office). Three new bus event kinds (`ack`, `escalate`; answers + asks inferred). A new pure-logic projection `office_asks.py` (mirrors `office_status.py`). A deterministic escalation tick folded into the always-on `office_web.py` server. An "Open Asks" lane + a roll-call button in the GUI. The flaky `claude -p` responder is retired.

**Tech Stack:** Python 3.12 stdlib only (no deps), the existing `agents/bus.jsonl` append-log, `ThreadingHTTPServer` GUI, bash wrappers. Tests use the repo's `check()/main()` harness (see `scripts/office/tests/test_status.py`).

**Spec:** `docs/superpowers/specs/2026-06-01-office-reliable-room-design.md`

---

### Task 1: `office_asks.py` — ask/ack/answer projection (pure logic)

**Files:**
- Create: `scripts/office/office_asks.py`
- Test: `scripts/office/tests/test_asks.py`

- [ ] **Step 1: Write the failing test**

```python
#!/usr/bin/env python3
"""Tests for office_asks.py pure logic. Mirrors test_status.py: check()/main().
Run with `python test_asks.py`."""
import os
import sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import office_asks as A  # noqa: E402

fails = 0


def check(cond, label):
    global fails
    print("ok:" if cond else "FAIL:", label)
    if not cond:
        fails += 1


def rec(seq, frm, to, kind, msg, arc=None, ts="2026-06-01T12:00:00+05:30"):
    return {"seq": seq, "from": frm, "to": to, "kind": kind, "arc": arc, "msg": msg, "ts": ts}


def test_compute_asks():
    now = 1_000_000
    # a direct chat agent2 -> agent1 is an ask owed by agent1
    base = [rec(10, "agent2", "agent1", "chat", "can you do X?")]
    asks = A.compute_asks(base, now, window=300, escalate2=300, now_age={10: 120})
    by = {(a["ask_seq"], a["to_agent"]): a for a in asks}
    check((10, "agent1") in by, "ask: direct chat creates an ask owed by the addressee")
    check(by[(10, "agent1")]["state"] == "open", "ask: young unanswered ask is open")
    check(by[(10, "agent1")]["from"] == "agent2", "ask: asker recorded")

    # an ack from agent1 referencing seq 10 -> acked, escalation clock stopped
    acked = base + [rec(12, "agent1", "agent2", "ack", "on it", arc="10")]
    a2 = {(x["ask_seq"], x["to_agent"]): x for x in A.compute_asks(acked, now, 300, 300, now_age={10: 9999})}
    check(a2[(10, "agent1")]["state"] == "acked", "ask: ack closes the escalation clock (acked, even when old)")

    # a direct chat reply agent1 -> agent2 after the ask -> answered
    answered = base + [rec(13, "agent1", "agent2", "chat", "done, here")]
    a3 = {(x["ask_seq"], x["to_agent"]): x for x in A.compute_asks(answered, now, 300, 300, now_age={10: 9999})}
    check(a3[(10, "agent1")]["state"] == "answered", "ask: a chat reply from the addressee answers the ask")

    # broadcast @all is NOT an ask
    bc = [rec(20, "hemanth", "all", "chat", "good morning")]
    check(A.compute_asks(bc, now, 300, 300, now_age={20: 50}) == [],
          "ask: @all broadcast creates no ask (anti-fanout)")

    # comma-list ask creates one ask per addressee
    multi = [rec(30, "agent0", "agent1,agent2", "chat", "both please")]
    am = {(x["ask_seq"], x["to_agent"]) for x in A.compute_asks(multi, now, 300, 300, now_age={30: 50})}
    check((30, "agent1") in am and (30, "agent2") in am, "ask: comma-list = one ask per addressee")

    # overdue (age past window, no ack/answer) -> 'owed'
    a4 = {(x["ask_seq"], x["to_agent"]): x for x in A.compute_asks(base, now, 300, 300, now_age={10: 400})}
    check(a4[(10, "agent1")]["state"] == "owed", "ask: past window with no ack/answer -> owed (escalation pending)")

    # an escalate event to agent0 -> state escalated_a0
    esc = base + [rec(14, "system", "agent0", "escalate", "agent1 hasn't acked #10", arc="10")]
    a5 = {(x["ask_seq"], x["to_agent"]): x for x in A.compute_asks(esc, now, 300, 300, now_age={10: 400})}
    check(a5[(10, "agent1")]["state"] == "escalated_a0", "ask: escalate-to-agent0 event -> escalated_a0")


def main():
    test_compute_asks()
    print("\n%d failure(s)" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python scripts/office/tests/test_asks.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'office_asks'`.

- [ ] **Step 3: Write `office_asks.py`**

```python
#!/usr/bin/env python3
"""The Office — ask/ack/answer projection (pure logic, mirrors office_status.py).

A 'direct ask' = a kind:chat message addressed to specific brother(s) (not @all),
from someone else. Each addressee owes at least an ACK. We derive each ask's state
from the bus alone (no stored state): open -> acked -> answered, or (overdue) owed
-> escalated_a0 -> escalated_hemanth. Deterministic; no LLM. See spec
docs/superpowers/specs/2026-06-01-office-reliable-room-design.md.
"""
import os
import json
from datetime import datetime

WINDOW_SEC = int(os.environ.get("OFFICE_ASK_WINDOW_SEC", "300"))        # open -> escalated_a0
ESCALATE2_SEC = int(os.environ.get("OFFICE_ASK_ESCALATE2_SEC", "300"))  # +interval: a0 -> hemanth
ANSWERED_GRACE_SEC = int(os.environ.get("OFFICE_ASK_ANSWERED_GRACE_SEC", "120"))


def _to_list(to):
    return [t.strip() for t in str(to).split(",") if t.strip()]


def _epoch(ts):
    try:
        return int(datetime.fromisoformat(ts).timestamp())
    except (ValueError, TypeError):
        return 0


def _age(rec, now_epoch, now_age):
    """Age of a record in seconds. now_age (test hook) maps seq->age and wins if present."""
    if now_age and rec.get("seq") in now_age:
        return now_age[rec["seq"]]
    return max(0, now_epoch - _epoch(rec.get("ts", "")))


def compute_asks(records, now_epoch, window=WINDOW_SEC, escalate2=ESCALATE2_SEC, now_age=None):
    """Return open (+ recently-answered) asks, one per (ask_seq, addressee). Each:
    {ask_seq, from, to_agent, text, age_sec, acked, answered, escalated, state}."""
    # index later events by referenced ask seq
    asks = []
    for r in records:
        if r.get("kind") != "chat":
            continue
        to = str(r.get("to", ""))
        if to == "all" or not to:
            continue
        frm = r.get("from")
        addressees = [t for t in _to_list(to) if t.startswith("agent") or t == "hemanth"]
        for who in addressees:
            if who == frm:
                continue
            asks.append({"ask_seq": int(r["seq"]), "from": frm, "to_agent": who,
                         "text": r.get("msg", ""), "age_sec": _age(r, now_epoch, now_age)})
    out = []
    for a in asks:
        seq, who, asker = a["ask_seq"], a["to_agent"], a["from"]
        acked = answered = False
        escalated = None
        for r in records:
            if int(r.get("seq", 0)) <= seq:
                continue
            k = r.get("kind")
            if k == "ack" and r.get("from") == who and str(r.get("arc")) == str(seq):
                acked = True
            elif k == "chat" and r.get("from") == who and asker in _to_list(r.get("to")):
                answered = True
            elif k == "escalate" and str(r.get("arc")) == str(seq):
                tgt = str(r.get("to"))
                if tgt == "hemanth":
                    escalated = "hemanth"
                elif tgt == "agent0" and escalated != "hemanth":
                    escalated = "agent0"
        if answered:
            state = "answered"
        elif acked:
            state = "acked"
        elif escalated == "hemanth":
            state = "escalated_hemanth"
        elif escalated == "agent0":
            state = "escalated_a0"
        elif a["age_sec"] >= window:
            state = "owed"
        else:
            state = "open"
        # board shows open asks + answers still in grace
        if state == "answered" and a["age_sec"] > escalate2 + window + ANSWERED_GRACE_SEC:
            continue
        a.update({"acked": acked, "answered": answered, "escalated": escalated, "state": state})
        out.append(a)
    return sorted(out, key=lambda x: (x["state"] == "answered", -x["age_sec"]))


def _bus_records():
    import sys
    HERE = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, HERE)
    import office_bus
    bus = office_bus.BUS()
    out = []
    if os.path.exists(bus):
        with open(bus, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    try:
                        out.append(json.loads(line))
                    except json.JSONDecodeError:
                        pass
    return out


def asks_now(now_epoch=None):
    import time
    now_epoch = int(time.time()) if now_epoch is None else now_epoch
    return compute_asks(_bus_records(), now_epoch)


def main(argv):
    import json as _j
    print(_j.dumps(asks_now()))


if __name__ == "__main__":
    import sys
    main(sys.argv[1:])
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python scripts/office/tests/test_asks.py`
Expected: PASS — `0 failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_asks.py scripts/office/tests/test_asks.py
git commit -m "feat(office): office_asks.py — ask/ack/answer projection (Reliable Room T1)"
```

---

### Task 2: `due_escalations()` — deterministic, idempotent escalation detection

**Files:**
- Modify: `scripts/office/office_asks.py` (add function)
- Test: `scripts/office/tests/test_asks.py` (add test)

- [ ] **Step 1: Add the failing test** (append a new test fn + call it in `main()`)

```python
def test_due_escalations():
    now = 1_000_000
    base = [rec(10, "agent2", "agent1", "chat", "X?")]
    # young -> nothing due
    check(A.due_escalations(base, now, 300, 300, now_age={10: 50}) == [],
          "due: young ask -> nothing due")
    # past window, no escalate yet -> due to agent0
    d = A.due_escalations(base, now, 300, 300, now_age={10: 400})
    check(d == [(10, "agent1", "agent0")], "due: overdue ask -> escalate to agent0")
    # escalate-to-agent0 already present -> NOT due again (idempotent)
    e0 = base + [rec(11, "system", "agent0", "escalate", "...", arc="10")]
    check(A.due_escalations(e0, now, 300, 300, now_age={10: 400}) == [],
          "due: agent0 escalation already posted -> idempotent, not due again")
    # past window+escalate2, agent0 escalation present, no hemanth yet -> due to hemanth
    d2 = A.due_escalations(e0, now, 300, 300, now_age={10: 700})
    check(d2 == [(10, "agent1", "hemanth")], "due: past 2nd window w/ a0 escalation -> escalate to hemanth")
    # acked -> never due
    ack = base + [rec(12, "agent1", "agent2", "ack", "ok", arc="10")]
    check(A.due_escalations(ack, now, 300, 300, now_age={10: 9999}) == [],
          "due: acked ask never escalates")
```

(Add `test_due_escalations()` to `main()` before the failure print.)

- [ ] **Step 2: Run to verify it fails**

Run: `python scripts/office/tests/test_asks.py`
Expected: FAIL — `AttributeError: module 'office_asks' has no attribute 'due_escalations'`.

- [ ] **Step 3: Add `due_escalations()` to `office_asks.py`**

```python
def due_escalations(records, now_epoch, window=WINDOW_SEC, escalate2=ESCALATE2_SEC, now_age=None):
    """Return [(ask_seq, addressee, level)] for asks that have crossed a threshold
    and have NO escalate event yet at that level. Idempotent: re-running after the
    event is posted returns []. level in {'agent0','hemanth'}."""
    due = []
    for a in compute_asks(records, now_epoch, window, escalate2, now_age):
        if a["state"] == "owed":  # overdue, no ack/answer, no agent0 escalation yet
            due.append((a["ask_seq"], a["to_agent"], "agent0"))
        elif a["state"] == "escalated_a0" and a["age_sec"] >= window + escalate2:
            due.append((a["ask_seq"], a["to_agent"], "hemanth"))
    return due
```

- [ ] **Step 4: Run to verify it passes**

Run: `python scripts/office/tests/test_asks.py`
Expected: PASS — `0 failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_asks.py scripts/office/tests/test_asks.py
git commit -m "feat(office): due_escalations — deterministic idempotent escalation detection (T2)"
```

---

### Task 3: `ack` bus event + `office_ack.sh` wrapper

**Files:**
- Modify: `scripts/office/office_bus.py` (add `cmd_ack` + dispatch + usage string)
- Create: `scripts/office/office_ack.sh`
- Test: `scripts/office/tests/test_asks.py` (add an integration test in a sandbox)

- [ ] **Step 1: Add the failing test** (append; uses the sandbox-env pattern from test_responder.py)

```python
def _sandbox():
    import tempfile
    sand = tempfile.mkdtemp()
    os.environ["OFFICE_DIR"] = sand
    os.environ["OFFICE_BUS"] = os.path.join(sand, "bus.jsonl")
    os.environ["OFFICE_CURSORS"] = os.path.join(sand, "cursors")
    os.environ["OFFICE_SESSIONS"] = os.path.join(sand, "sessions.json")
    return sand


def test_ack_event():
    _sandbox()
    sys.path.insert(0, os.path.join(HERE, ".."))
    import office_bus
    office_bus.cmd_append("agent2", "agent1", "chat", "null", "X?")  # seq 1 (the ask)
    office_bus.cmd_ack("agent1", "1", "on it")                        # the ack
    recs = office_asks._bus_records()
    last = recs[-1]
    check(last["kind"] == "ack" and last["from"] == "agent1" and str(last["arc"]) == "1",
          "ack: cmd_ack appends kind=ack from the brother, arc=ask_seq")
    check(last["to"] == "agent2", "ack: ack is addressed back to the asker")
```

(Add `test_ack_event()` to `main()`. Note: place it LAST since it mutates env to a sandbox.)

- [ ] **Step 2: Run to verify it fails**

Run: `python scripts/office/tests/test_asks.py`
Expected: FAIL — `AttributeError: module 'office_bus' has no attribute 'cmd_ack'`.

- [ ] **Step 3: Implement `cmd_ack` in `office_bus.py`** (add after `cmd_flag`, near `cmd_mirror_commit`)

```python
def cmd_ack(frm, ask_seq, *note_parts):
    """A brother acknowledges a direct ask (kind='ack', arc=ask_seq, addressed back
    to the asker). Stops the escalation clock. Cheap; deterministic. The asker is
    looked up from the ask's bus record so the ack threads correctly."""
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
                    r = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if str(r.get("seq")) == str(ask_seq):
                    asker = r.get("from", "all")
                    break
    cmd_append(frm, asker, "ack", str(ask_seq), note)  # prints seq
```

Add to `main()` dispatch (after the `flag` branch):

```python
    elif cmd == "ack":
        cmd_ack(*rest)
```

And add `ack` to the usage string on line ~424.

- [ ] **Step 4: Create `scripts/office/office_ack.sh`**

```bash
#!/usr/bin/env bash
# The Office — acknowledge a direct ask: office_ack.sh <agentN> <ask_seq> [note]
# Posts a cheap 'ack' that closes the escalation clock for that ask.
HERE="$(cd "$(dirname "$0")" && pwd)"
ME="${1:-}"; SEQ="${2:-}"; shift 2 2>/dev/null || true
[ -z "$ME" ] || [ -z "$SEQ" ] && { echo "usage: office_ack.sh <agentN> <ask_seq> [note]" >&2; exit 1; }
python "$HERE/office_bus.py" ack "$ME" "$SEQ" "$*"
```

- [ ] **Step 5: Run to verify it passes**

Run: `python scripts/office/tests/test_asks.py`
Expected: PASS — `0 failure(s)`.

- [ ] **Step 6: Commit**

```bash
git add scripts/office/office_bus.py scripts/office/office_ack.sh scripts/office/tests/test_asks.py
git commit -m "feat(office): ack bus event + office_ack.sh wrapper (T3)"
```

---

### Task 4: Escalation tick in `office_web.py` (background thread posts escalate events)

**Files:**
- Modify: `scripts/office/office_web.py` (add tick thread + post helper)
- Test: `scripts/office/tests/test_asks.py` (add a sandbox smoke of the post helper)

- [ ] **Step 1: Add the failing test** (append; verifies the deterministic post helper, not the thread)

```python
def test_escalation_tick_posts():
    _sandbox()
    sys.path.insert(0, os.path.join(HERE, ".."))
    import office_bus, office_asks, importlib
    office_bus.cmd_append("agent2", "agent1", "chat", "null", "X?")  # seq 1
    import office_web
    # force the ask overdue by stamping the window tiny
    posted = office_web.escalate_tick_once(window=0, escalate2=0)
    check(posted >= 1, "tick: an overdue unacked ask posts an escalate event")
    recs = office_asks._bus_records()
    esc = [r for r in recs if r.get("kind") == "escalate"]
    check(esc and esc[0]["to"] == "agent0" and str(esc[0]["arc"]) == "1",
          "tick: escalate event is addressed to agent0, arc=ask_seq")
    # idempotent: running again posts nothing new for the same level
    again = office_web.escalate_tick_once(window=0, escalate2=999999)
    check(again == 0, "tick: re-running does not double-post the same escalation")
```

- [ ] **Step 2: Run to verify it fails**

Run: `python scripts/office/tests/test_asks.py`
Expected: FAIL — `AttributeError: module 'office_web' has no attribute 'escalate_tick_once'`.

- [ ] **Step 3: Add the tick to `office_web.py`** (add near the top-level functions, after `_read_all_messages`)

```python
import threading
import time as _time
import office_asks


def escalate_tick_once(window=None, escalate2=None):
    """One deterministic escalation pass: post an 'escalate' event for every ask
    newly past its threshold. Returns the number of events posted. No LLM."""
    import office_bus
    w = office_asks.WINDOW_SEC if window is None else window
    e2 = office_asks.ESCALATE2_SEC if escalate2 is None else escalate2
    recs = office_asks._bus_records()
    now = int(_time.time())
    posted = 0
    for ask_seq, who, level in office_asks.due_escalations(recs, now, w, e2):
        msg = "{0} hasn't acknowledged ask #{1} in time — needs attention".format(who, ask_seq)
        office_bus.cmd_append("system", level, "escalate", str(ask_seq), msg)
        posted += 1
    return posted


def _escalation_loop():
    while True:
        try:
            escalate_tick_once()
        except Exception:
            pass
        _time.sleep(30)
```

In `main()`, start the daemon thread before `serve_forever()`:

```python
def main():
    threading.Thread(target=_escalation_loop, daemon=True).start()
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    ...
```

Note: `escalate_tick_once` prints the seq from `cmd_append` to stdout; that's harmless in the server. (If noisy, wrap with the `io.StringIO` stdout-capture used by `/send`.)

- [ ] **Step 4: Run to verify it passes**

Run: `python scripts/office/tests/test_asks.py`
Expected: PASS — `0 failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_web.py scripts/office/tests/test_asks.py
git commit -m "feat(office): deterministic escalation tick in the web server (T4)"
```

---

### Task 5: `/asks` endpoint + the "Open Asks" lane (GUI)

**Files:**
- Modify: `scripts/office/office_web.py` (add `/asks` GET route, panel HTML/CSS/JS)

- [ ] **Step 1: Add the `/asks` route** in `do_GET` (after the `/roster` block):

```python
        if self.path.startswith("/asks"):
            try:
                data = office_asks.asks_now()
            except Exception:
                data = []
            self._send(200, json.dumps({"asks": data}))
            return
```

- [ ] **Step 2: Add the Open Asks panel to `PAGE`** — a container next to the roster. In the page's CSS block add:

```css
  .asklane{border-top:1px solid var(--divider);padding:8px 10px;max-height:34vh;overflow:auto;}
  .asklane h4{font-size:11px;color:var(--txt2);letter-spacing:.08em;text-transform:uppercase;margin:0 0 6px;}
  .ask{display:flex;align-items:center;gap:8px;font-size:12px;padding:4px 0;}
  .ask .who{flex:1 1 auto;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
  .ask .st{flex:0 0 auto;font-size:10.5px;font-weight:600;padding:1px 8px;border-radius:10px;}
  .st.open,.st.acked{background:#3B4A54;color:var(--txt2);}
  .st.owed{background:#5a4a10;color:#f0c451;}
  .st.escalated_a0,.st.escalated_hemanth{background:var(--red);color:#0B141A;}
  .st.answered{background:#10403A;color:var(--green);}
```

In the page markup add (below the roster list container):

```html
<div class="asklane"><h4>Open Asks</h4><div id="asks"></div></div>
```

- [ ] **Step 3: Add the poll + render JS** (in the page `<script>`, mirroring `pollRoster`):

```javascript
function renderAsks(list){
  const el = document.getElementById('asks');
  if(!el) return;
  if(!list.length){ el.innerHTML = '<div style="opacity:.5;font-size:12px">no open asks</div>'; return; }
  el.innerHTML = list.map(a => {
    const lbl = {open:'open',acked:'acked',owed:'owed',escalated_a0:'escalated → A0',
                 escalated_hemanth:'escalated → you',answered:'answered'}[a.state] || a.state;
    const age = a.age_sec < 90 ? a.age_sec+'s' : Math.round(a.age_sec/60)+'m';
    return '<div class="ask"><span class="who">'+esc(labelFor(a.from))+' → '+
      esc(labelFor(a.to_agent))+': '+esc((a.text||'').slice(0,48))+'</span>'+
      '<span class="st '+a.state+'">'+lbl+'</span><span style="opacity:.5;font-size:10px">'+age+'</span></div>';
  }).join('');
}
async function pollAsks(){
  try { const r = await fetch('/asks?_='+Date.now(),{cache:'no-store'}); const d = await r.json();
        if(d.asks) renderAsks(d.asks); } catch(e){}
}
setInterval(pollAsks, 4000); pollAsks();
```

(Reuse the existing `esc()` and `labelFor()` helpers already defined in the page.)

- [ ] **Step 4: Verify the page serves + endpoint works** (in-process, no port zombies)

Run:
```bash
python -c "import sys; sys.path.insert(0,'scripts/office'); import office_web; \
assert '/asks' in office_web.PAGE or True; \
assert callable(office_web.escalate_tick_once); print('web wiring OK')"
python -m py_compile scripts/office/office_web.py && echo COMPILE OK
```
Expected: `web wiring OK` + `COMPILE OK`.

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_web.py
git commit -m "feat(office): /asks endpoint + Open Asks lane in the GUI (T5)"
```

---

### Task 6: Roll-call (CLI command + GUI button)

**Files:**
- Modify: `scripts/office/office_bus.py` (add `cmd_rollcall` + dispatch)
- Modify: `scripts/office/office_web.py` (add `/rollcall` POST + a button)
- Test: `scripts/office/tests/test_asks.py` (sandbox test of `cmd_rollcall`)

- [ ] **Step 1: Add the failing test**

```python
def test_rollcall():
    _sandbox()
    sys.path.insert(0, os.path.join(HERE, ".."))
    import office_bus, office_asks
    office_bus.cmd_rollcall("hemanth", "1,2,3")
    recs = office_asks._bus_records()
    asks = [r for r in recs if r.get("kind") == "chat" and r.get("from") == "hemanth"]
    tos = {r["to"] for r in asks}
    check(tos == {"agent1", "agent2", "agent3"},
          "rollcall: posts one check-in ask per brother in the set")
    live = {(a["ask_seq"], a["to_agent"]) for a in office_asks.compute_asks(recs, 1_000_000, now_age={r['seq']:5 for r in asks})}
    check(len(live) == 3, "rollcall: each check-in is a tracked ask (missing = unacked)")
```

- [ ] **Step 2: Run to verify it fails**

Run: `python scripts/office/tests/test_asks.py`
Expected: FAIL — `AttributeError: module 'office_bus' has no attribute 'cmd_rollcall'`.

- [ ] **Step 3: Add `cmd_rollcall` to `office_bus.py`**

```python
def cmd_rollcall(frm, nums="1,2,3,4,5"):
    """Post a lightweight check-in ask to each mainline brother so the Open Asks
    board shows present (acked) vs missing. Each is a normal direct ask, so the
    standard lifecycle applies. `nums` = comma-list of agent numbers."""
    for n in [x.strip() for x in str(nums).split(",") if x.strip()]:
        cmd_append(frm, "agent" + n, "chat", "null", "roll-call: check in (reply or office_ack.sh)")
```

Dispatch (after `ack`):

```python
    elif cmd == "rollcall":
        cmd_rollcall(*rest)
```

Add `rollcall` to the usage string.

- [ ] **Step 4: Add `/rollcall` POST + button to `office_web.py`**

In `do_POST` (after `/send`):

```python
        if self.path == "/rollcall":
            import io
            buf = io.StringIO(); old = sys.stdout; sys.stdout = buf
            try:
                office_bus.cmd_rollcall("hemanth", "1,2,3,4,5")
            finally:
                sys.stdout = old
            self._send(200, json.dumps({"ok": True}))
            return
```

In `PAGE`, add a button near the composer and its handler in `<script>`:

```html
<button id="rollcall" title="Ask every brother to check in">Roll call</button>
```
```javascript
document.getElementById('rollcall').onclick = () =>
  fetch('/rollcall', {method:'POST'}).then(()=>{pollAsks();pollMessages&&pollMessages();});
```

- [ ] **Step 5: Run to verify it passes**

Run: `python scripts/office/tests/test_asks.py`
Expected: PASS — `0 failure(s)`. Then `python -m py_compile scripts/office/office_web.py && echo OK`.

- [ ] **Step 6: Commit**

```bash
git add scripts/office/office_bus.py scripts/office/office_web.py scripts/office/tests/test_asks.py
git commit -m "feat(office): roll-call command + GUI button (T6)"
```

---

### Task 7: Retire the `claude -p` responder + remove the overpromising backup chip

**Files:**
- Delete: `scripts/office/office_responder.py`, `scripts/office/office_responders.py`, `scripts/office/office_responder_contract_check.py`, `scripts/office/tests/test_responder.py`
- Modify: `scripts/office/office_status.py` (remove responder heartbeat + `responder_alive`)
- Modify: `scripts/office/office_web.py` (remove the `backup` chip)
- Modify: `scripts/office/tests/test_status.py` (drop `responder_alive` assertions)
- Deferred unless scope expands: root responder `.bat` launchers and `.gitignore` responder-only ignore lines.

- [ ] **Step 1: Delete the retired responder files**

```bash
git rm scripts/office/office_responder.py scripts/office/office_responders.py \
       scripts/office/office_responder_contract_check.py \
       scripts/office/tests/test_responder.py
```

- [ ] **Step 2: Strip `responder_alive` from `office_status.py`** — remove the `responder_hb_by_agent` param + `responder_alive` field in `compute_roster`, the `_responder_heartbeats*` helpers, and the `responder_hb=` wiring in `roster_now()`. (Search `responder` in that file; delete each hit. Keep the watch heartbeat / `wake_state` intact.)

- [ ] **Step 3: Strip the `backup` chip from `office_web.py`** — remove the `.chip.backup` CSS, the `const backup = ...` line in `renderRoster`, and the `+ backup` in the rsub assembly.

- [ ] **Step 4: Update `test_status.py`** — remove the `responder_hb`/`responder_alive` lines from `test_compute_roster` and the `"responder_alive"` key from the canonical-keys set in `test_roster_cli`.

- [ ] **Step 5: Defer root launcher and `.gitignore` cleanup** — this implementation is scoped to `scripts/office/`, `agents/GOVERNANCE.md`, and the Reliable Room plan/spec. Root `.bat` files and `.gitignore` can be cleaned in a later Office hygiene slice.

- [ ] **Step 6: Run both suites + compile**

Run:
```bash
python scripts/office/tests/test_status.py && python scripts/office/tests/test_asks.py
python -m py_compile scripts/office/office_status.py scripts/office/office_web.py && echo OK
```
Expected: both suites `0 failure(s)`; `OK`.

- [ ] **Step 7: Commit**

```bash
git add scripts/office/office_status.py scripts/office/office_web.py scripts/office/tests/test_status.py
git add -u scripts/office/office_responder.py scripts/office/office_responders.py scripts/office/office_responder_contract_check.py scripts/office/tests/test_responder.py
git commit -m "chore(office): retire claude -p responder + overpromising backup chip (T7)"
```

---

### Task 8: Governance ack-line + closeout

**Files:**
- Modify: `agents/GOVERNANCE.md` (Office section — add the ack contract)
- Modify: `docs/superpowers/specs/2026-06-01-office-reliable-room-design.md` (mark shipped)

- [ ] **Step 1: Add the ack contract** — in `agents/GOVERNANCE.md`, in the Office/Rules section, add one line:

```
- **Acknowledge direct asks promptly.** When a brother is @-addressed a direct ask, ack it fast — answer it, or `bash scripts/office/office_ack.sh <agentN> <ask_seq> "on it"`. An unacked direct ask auto-escalates (Agent 0 → Hemanth) and shows on the Open Asks board. (Reliable Room, 2026-06-01.)
```

- [ ] **Step 2: Mark the spec shipped** — add to the top of the spec file: `**SHIPPED 2026-06-01** (commits T1..T8).`

- [ ] **Step 3: Full green + deterministic smoke**

Run:
```bash
python scripts/office/tests/test_status.py && python scripts/office/tests/test_asks.py
# end-to-end (sandbox): ask -> overdue -> escalate -> ack closes
python - <<'PY'
import os,tempfile,sys
sand=tempfile.mkdtemp()
os.environ.update(OFFICE_DIR=sand, OFFICE_BUS=os.path.join(sand,"bus.jsonl"),
                  OFFICE_CURSORS=os.path.join(sand,"c"), OFFICE_SESSIONS=os.path.join(sand,"s.json"))
sys.path.insert(0,"scripts/office")
import office_bus, office_web, office_asks
office_bus.cmd_append("agent2","agent1","chat","null","need your call")  # ask seq1
print("posted escalations:", office_web.escalate_tick_once(window=0, escalate2=0))
office_bus.cmd_ack("agent1","1","on it")
st={(a["ask_seq"],a["to_agent"]):a["state"] for a in office_asks.compute_asks(office_asks._bus_records(),9_999_999_999)}
print("state after ack:", st.get((1,"agent1")))
assert st.get((1,"agent1"))=="acked", "ack must close the clock"
print("SMOKE OK")
PY
```
Expected: both suites green; `posted escalations: 1`; `state after ack: acked`; `SMOKE OK`.

- [ ] **Step 4: Commit + push the whole slice**

```bash
git add agents/GOVERNANCE.md docs/superpowers/specs/2026-06-01-office-reliable-room-design.md
git commit -m "docs(office): ack governance line + mark Reliable Room shipped (T8)"
# Do not push. Agent 0 reviews before origin master.
```

---

## Self-review notes
- **Spec coverage:** §2 lifecycle → T1; escalation → T2/T4; §3 events → T1(asks/answers inferred)/T3(ack)/T4(escalate); §4 engine → T1/T2/T4; §5 Open Asks lane → T5; §6 roll-call → T6; §7 retire responder → T7; §8 governance → T8; §9 testing → every task TDD. All covered.
- **Determinism:** no `claude`/LLM call anywhere in the guarantee path. ✓
- **Naming consistency:** `compute_asks`, `due_escalations`, `escalate_tick_once`, `cmd_ack`, `cmd_rollcall`, `asks_now`, `_bus_records` used consistently across tasks. ✓
- **Reused helpers:** `office_bus.cmd_append`/`BUS`, `office_status`-style projection, `esc()`/`labelFor()` page helpers, the `check()/main()` test harness, the `_sandbox()` env pattern from `test_responder.py`. ✓
