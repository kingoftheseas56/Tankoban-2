# Owned-Worker Responder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A headless, reply-only "fallback responder" for one Claude brother that guarantees his coordination messages get answered when his tab goes idle — the real brother answers first, the worker steps in only if *that specific message* stays unanswered ~60s, posting a marked, non-binding reply as the brother.

**Architecture:** A new `scripts/office/office_responder.py` (pure-logic functions + thin IO + a `claude -p` driver + the watch loop), tested by `scripts/office/tests/test_responder.py`. It mirrors the proven `codex_agent7_bridge.py` but treats the Claude driver as a NEW driver, gated behind a contract test. Pure decision logic (target-aware suppression, `@all` prefilter, reply formatting, responder cursor) is TDD'd in isolation; the loop is built **dry-run first** (logs would-post decisions, posts nothing) and posting is the LAST thing enabled.

**Tech Stack:** Python 3.12 stdlib only (matches the Office), `claude -p` (Claude Code headless print mode), the standalone-script test harness (`tests/test_status.py` style).

**Spec:** `docs/superpowers/specs/2026-05-31-owned-worker-responder-design.md` (v2 — incorporates Agent 7's review).

**Governance:** Flat-on-master, no worktree (gov-v13). Python-only. **NEVER `git reset --hard` on the shared tree** (`feedback_no_reset_hard_on_shared_tree`) — commit only your files, plain-rebase if pushed-behind.

---

## File Structure

**Create:**
- `scripts/office/office_responder_contract_check.py` — the GATING spike: proves the exact `claude -p` command returns a parseable structured reply, clean exit, within timeout. Run once; PASS unlocks the build, FAIL stops it (pivot to Agent SDK).
- `scripts/office/office_responder.py` — the responder: pure functions (`is_candidate`, `should_suppress`, `format_backup_reply`, responder-cursor helpers) + the `claude -p` driver + the dry-run/posting loop.
- `scripts/office/tests/test_responder.py` — standalone test script (mirrors `test_status.py`): `check()`/`main()`, run with `python test_responder.py`.

**Canonical types** (keep exact across tasks):
- **trigger record:** `{"seq": int, "frm": str, "to": str, "text": str}` (`frm` = original sender; note `frm` not `from`, a Python keyword).
- **reply classes:** `"ack" | "handoff" | "clarifying_question" | "nonbinding_assessment" | "decline_owner_confirmation_required"`.
- **`claude -p` reply object:** `{"replies": [{"to": str, "class": str, "msg": str}]}` (≤2 items).

---

## Task 1: `claude -p` contract test — THE GATE

**Files:**
- Create: `scripts/office/office_responder_contract_check.py`

This is a discovery spike, not a TDD unit. It must PASS before any responder logic is built. If it FAILS, **STOP and escalate to Agent 0/Hemanth** — the design pivots to the Claude Agent SDK (spec §7); do NOT hack around a leaky/unparseable CLI.

- [ ] **Step 1: Write the contract-check script** `scripts/office/office_responder_contract_check.py`

```python
#!/usr/bin/env python3
"""GATE: prove `claude -p` can drive a headless, read-only, structured-output
reply for the Office responder. Agent 7's live smoke (spec §7) showed --json-schema
returned a free-form result + a hook ran MCP despite --tools "". This verifies the
EXACT contract before we build the responder. PASS -> build. FAIL -> pivot to SDK.

Run: python scripts/office/office_responder_contract_check.py
"""
import os
import sys
import json
import subprocess
import tempfile

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

SCHEMA = {
    "type": "object", "additionalProperties": False,
    "properties": {"replies": {
        "type": "array", "maxItems": 2,
        "items": {"type": "object", "additionalProperties": False,
                  "properties": {"to": {"type": "string"},
                                 "class": {"type": "string"},
                                 "msg": {"type": "string"}},
                  "required": ["to", "class", "msg"]}}},
    "required": ["replies"],
}

PROMPT = ('You are a test harness. Output ONLY JSON matching the schema. '
          'Return exactly {"replies": []} (an empty replies array). '
          'Do not read files, do not call tools.')


def try_command(args, label, timeout=90):
    print("\n--- trying: %s ---" % label)
    print("  cmd:", " ".join(args[:1] + ["..."]))
    try:
        proc = subprocess.run(args, input=PROMPT, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired:
        print("  RESULT: TIMEOUT after %ss" % timeout)
        return False
    print("  exit:", proc.returncode)
    out = proc.stdout.strip()
    print("  stdout (first 600):", out[:600])
    if proc.stderr.strip():
        print("  stderr (first 300):", proc.stderr.strip()[:300])
    # Acceptance: we can extract a {"replies": [...]} object from the output.
    replies = extract_replies(out)
    if replies is None:
        print("  RESULT: FAIL — could not extract a valid {replies:[...]} object")
        return False
    print("  extracted replies:", replies)
    print("  RESULT: PASS — structured reply parseable")
    return True


def extract_replies(out):
    """Be liberal: try whole-string JSON, then a 'result' wrapper, then first {...} blob."""
    for candidate in _json_candidates(out):
        try:
            obj = json.loads(candidate)
        except (json.JSONDecodeError, TypeError):
            continue
        if isinstance(obj, dict):
            if isinstance(obj.get("replies"), list):
                return obj["replies"]
            # CLI may wrap: {"type":"result","result":"<json string>"} or {"structured_output":{...}}
            inner = obj.get("structured_output") or obj.get("result")
            if isinstance(inner, dict) and isinstance(inner.get("replies"), list):
                return inner["replies"]
            if isinstance(inner, str):
                try:
                    inner_obj = json.loads(inner)
                    if isinstance(inner_obj.get("replies"), list):
                        return inner_obj["replies"]
                except json.JSONDecodeError:
                    pass
    return None


def _json_candidates(out):
    yield out
    i, j = out.find("{"), out.rfind("}")
    if i != -1 and j > i:
        yield out[i:j + 1]


def main():
    with tempfile.TemporaryDirectory() as td:
        schema_path = os.path.join(td, "schema.json")
        with open(schema_path, "w", encoding="utf-8") as f:
            json.dump(SCHEMA, f)
        # Variant A: the spec's recommended shape.
        variants = [
            (["claude", "-p", "--no-session-persistence", "--permission-mode", "default",
              "--tools", "", "--disallowedTools", "Edit,Write,Bash", "--strict-mcp-config",
              "--json-schema", schema_path, "--output-format", "json"], "recommended (json + schema)"),
            (["claude", "-p", "--no-session-persistence", "--permission-mode", "plan",
              "--tools", "", "--strict-mcp-config", "--output-format", "json"], "plan-mode, no schema (parse from text)"),
        ]
        passed = any(try_command(args, label) for args, label in variants)
    print("\n==================")
    print("CONTRACT TEST:", "PASS — claude -p is usable; proceed to Task 2." if passed
          else "FAIL — claude -p contract not clean. STOP. Escalate: pivot to Agent SDK (spec §7).")
    print("==================")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run the gate**

Run: `python scripts/office/office_responder_contract_check.py`
Expected: prints each variant's result and a final `CONTRACT TEST: PASS` or `FAIL`.

- [ ] **Step 3: The GATE decision**
  - **PASS** → record which variant worked (you'll reuse that exact command in Task 7), commit the contract-check, continue to Task 2.
  - **FAIL** → **STOP.** Do not proceed. Post to @agent0: "responder contract test FAILED — claude -p can't return clean structured output / leaks tools. Pivoting to Agent SDK per spec §7." This is an honest gate, not a blocker to hack past.

- [ ] **Step 4: Commit (only on PASS)**

```bash
git add scripts/office/office_responder_contract_check.py
git commit -m "feat(office): claude -p contract-check gate for the responder (Agent 7 finding)"
```

---

## Task 2: Target-aware suppression (pure logic, TDD)

**Files:**
- Create: `scripts/office/office_responder.py`
- Test: `scripts/office/tests/test_responder.py`

- [ ] **Step 1: Write the failing test** (create `scripts/office/tests/test_responder.py`)

```python
#!/usr/bin/env python3
"""Tests for office_responder.py pure logic. Mirrors test_status.py: check()/main(),
run with `python test_responder.py`."""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import office_responder as R  # noqa: E402

fails = 0


def check(cond, label):
    global fails
    print("ok:" if cond else "FAIL:", label)
    if not cond:
        fails += 1


def test_should_suppress():
    me = "agent1"
    trigger = {"seq": 100, "frm": "agent2", "to": "agent1", "text": "need your A/B call"}
    # agent1 replied directly to agent2 after the trigger -> suppress (he answered)
    bus = [{"seq": 105, "from": "agent1", "to": "agent2", "kind": "chat", "msg": "go with ii"}]
    sup, _ = R.should_suppress(trigger, bus, me)
    check(sup is True, "suppress: me answered the sender directly -> suppress")
    # agent1 posted something UNRELATED (to agent4) -> do NOT suppress (the v1 bug)
    bus2 = [{"seq": 106, "from": "agent1", "to": "agent4", "kind": "chat", "msg": "unrelated"}]
    sup2, _ = R.should_suppress(trigger, bus2, me)
    check(sup2 is False, "suppress: unrelated post must NOT suppress (target-aware)")
    # agent1 posted an activity line (commit mirror) -> not a real answer -> do NOT suppress
    bus3 = [{"seq": 107, "from": "agent1", "to": "agent2", "kind": "activity", "msg": "committed x"}]
    sup3, _ = R.should_suppress(trigger, bus3, me)
    check(sup3 is False, "suppress: activity line is not an answer -> do NOT suppress")
    # nothing from agent1 at all -> do NOT suppress (the silent case we must catch)
    sup4, _ = R.should_suppress(trigger, [], me)
    check(sup4 is False, "suppress: total silence -> do NOT suppress (backup fires)")
    # comma-list answer (me -> agent2,agent5) still counts
    bus5 = [{"seq": 108, "from": "agent1", "to": "agent2,agent5", "kind": "chat", "msg": "both"}]
    sup5, _ = R.should_suppress(trigger, bus5, me)
    check(sup5 is True, "suppress: comma-list including sender counts as answer")


def main():
    test_should_suppress()
    print("\n%d failure(s)" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python scripts/office/tests/test_responder.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'office_responder'`.

- [ ] **Step 3: Write the implementation** (create `scripts/office/office_responder.py`)

```python
#!/usr/bin/env python3
"""The Office — owned-worker fallback responder (reply-only) for one Claude brother.

Pure decision logic (target-aware suppression, @all prefilter, reply formatting,
responder cursor) + a claude -p driver + a dry-run/posting watch loop. The real
brother (his tab) answers first; this steps in only if THAT message stays
unanswered ~FALLBACK_WINDOW, posting a marked, non-binding reply as the brother.

Spec: docs/superpowers/specs/2026-05-31-owned-worker-responder-design.md (v2).
"""
import os
import re
import sys
import json
import time
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import office_bus  # noqa: E402  (bus path, _dir, append/cursor logic)

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

FALLBACK_WINDOW = 60     # seconds to let the real tab answer before backing up
REPLY_CLASSES = ("ack", "handoff", "clarifying_question",
                 "nonbinding_assessment", "decline_owner_confirmation_required")


def _to_list(to):
    return [t.strip() for t in str(to).split(",") if t.strip()]


def should_suppress(trigger, bus_records, me):
    """Target-aware: suppress the backup ONLY if `me` plausibly answered THIS thread
    after the trigger — i.e. posted a real chat addressed back to the trigger's
    sender. An unrelated post, an activity line, or silence does NOT suppress
    (that was the v1 bug Agent 7 caught). Returns (suppress: bool, reason: str)."""
    sender = trigger.get("frm")
    for r in bus_records:
        if int(r.get("seq", 0)) <= int(trigger.get("seq", 0)):
            continue
        if r.get("from") != me:
            continue
        if r.get("kind") not in (None, "chat"):   # activity/blocked aren't answers
            continue
        if sender in _to_list(r.get("to")):
            return True, "answered: {0} -> {1} at seq {2}".format(me, sender, r.get("seq"))
    return False, "no plausible answer from {0} to {1}".format(me, sender)
```

- [ ] **Step 4: Run the tests and make sure they pass**

Run: `python scripts/office/tests/test_responder.py`
Expected: all `ok:`, `0 failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_responder.py scripts/office/tests/test_responder.py
git commit -m "feat(office): responder target-aware suppression (pure logic, TDD)"
```

---

## Task 3: `@all` deterministic prefilter (pure logic, TDD)

**Files:**
- Modify: `scripts/office/office_responder.py`
- Test: `scripts/office/tests/test_responder.py`

- [ ] **Step 1: Write the failing test** (add to `test_responder.py`, call from `main()`)

```python
def test_is_candidate():
    me = "agent1"
    # direct to me -> candidate
    check(R.is_candidate({"from": "agent2", "to": "agent1", "kind": "chat", "msg": "hi"}, me) is True,
          "candidate: direct to me")
    # from me -> never
    check(R.is_candidate({"from": "agent1", "to": "agent2", "kind": "chat", "msg": "x"}, me) is False,
          "candidate: my own post is never a candidate")
    # @all WITHOUT my mention -> NOT a candidate (no fan-out)
    check(R.is_candidate({"from": "agent0", "to": "all", "kind": "chat", "msg": "general question?"}, me) is False,
          "candidate: @all without my mention -> NOT (avoid fan-out)")
    # @all WITH explicit @agent1 mention -> candidate
    check(R.is_candidate({"from": "agent0", "to": "all", "kind": "chat", "msg": "@agent1 your call?"}, me) is True,
          "candidate: @all explicitly mentioning me -> candidate")
    # @all WITH 'agent 1' textual mention -> candidate
    check(R.is_candidate({"from": "agent0", "to": "all", "kind": "chat", "msg": "agent 1 please confirm"}, me) is True,
          "candidate: @all 'agent N' mention -> candidate")
    # activity line -> never
    check(R.is_candidate({"from": "agent2", "to": "agent1", "kind": "activity", "msg": "committed"}, me) is False,
          "candidate: activity line -> never")
```

Add `test_is_candidate()` to `main()` before the print line.

- [ ] **Step 2: Run it to verify it fails**

Run: `python scripts/office/tests/test_responder.py`
Expected: FAIL — `AttributeError: module 'office_responder' has no attribute 'is_candidate'`.

- [ ] **Step 3: Write the implementation** (append to `office_responder.py`)

```python
def is_candidate(rec, me):
    """Is this message a fallback candidate for `me`? Direct/comma-list to me: always.
    @all: ONLY if it explicitly names me (@agentN or 'agent N') — a generic broadcast
    does NOT make every brother's responder fire (Agent 7's anti-fan-out gate).
    Never my own posts; never non-chat kinds."""
    if rec.get("from") == me:
        return False
    if rec.get("kind") not in (None, "chat", "blocked"):
        return False
    to = str(rec.get("to", ""))
    if me in _to_list(to):
        return True
    if to == "all":
        msg = str(rec.get("msg", "")).lower()
        num = me[len("agent"):]
        if "@" + me in msg:
            return True
        if re.search(r"\bagent\s*#?\s*" + re.escape(num) + r"\b", msg):
            return True
    return False
```

- [ ] **Step 4: Run the tests and make sure they pass**

Run: `python scripts/office/tests/test_responder.py`
Expected: all `ok:`, `0 failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_responder.py scripts/office/tests/test_responder.py
git commit -m "feat(office): responder @all deterministic prefilter (no fan-out)"
```

---

## Task 4: Typed reply formatting (pure logic, TDD)

**Files:**
- Modify: `scripts/office/office_responder.py`
- Test: `scripts/office/tests/test_responder.py`

- [ ] **Step 1: Write the failing test** (add to `test_responder.py`, call from `main()`)

```python
def test_format_backup_reply():
    out = R.format_backup_reply("agent1", "ack", "received, will pick up Phase 4")
    check(out.startswith("[auto") and "agent1" in out.lower() and "ack" in out,
          "format: marked with auto + agent + class")
    check("received" in out, "format: body preserved")
    # substantive classes must read non-binding
    nb = R.format_backup_reply("agent1", "nonbinding_assessment", "looks like option ii")
    check("owner to confirm" in nb.lower() or "non-binding" in nb.lower() or "backup read" in nb.lower(),
          "format: nonbinding_assessment carries a non-binding qualifier")
    # invalid class rejected
    try:
        R.format_backup_reply("agent1", "bogus", "x")
        check(False, "format: invalid class should raise")
    except ValueError:
        check(True, "format: invalid class raises ValueError")
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python scripts/office/tests/test_responder.py`
Expected: FAIL — `AttributeError: ... 'format_backup_reply'`.

- [ ] **Step 3: Write the implementation** (append to `office_responder.py`)

```python
def _label(me):
    return "Agent " + me[len("agent"):] if me.startswith("agent") else me


def format_backup_reply(me, reply_class, body):
    """Build the marked, honest backup message. Substantive classes get a
    non-binding qualifier so the backup can never commit the real brother."""
    if reply_class not in REPLY_CLASSES:
        raise ValueError("unknown reply class: {0!r}".format(reply_class))
    prefix = "[auto · {0}'s tab idle · {1}]".format(me, reply_class)
    body = " ".join(str(body).split())
    if reply_class in ("nonbinding_assessment", "decline_owner_confirmation_required"):
        if not re.search(r"owner to confirm|non-binding|backup read", body, re.IGNORECASE):
            body = "backup read (non-binding, owner to confirm): " + body
    return "{0} {1}".format(prefix, body)
```

- [ ] **Step 4: Run the tests and make sure they pass**

Run: `python scripts/office/tests/test_responder.py`
Expected: all `ok:`, `0 failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_responder.py scripts/office/tests/test_responder.py
git commit -m "feat(office): responder typed non-binding reply formatting"
```

---

## Task 5: Separate responder cursor (IO, TDD via sandbox)

**Files:**
- Modify: `scripts/office/office_responder.py`
- Test: `scripts/office/tests/test_responder.py`

- [ ] **Step 1: Write the failing test** (add to `test_responder.py`, call from `main()`)

```python
def test_responder_cursor():
    sand = tempfile.mkdtemp()
    os.environ["OFFICE_DIR"] = sand   # office_bus._dir() honors this
    me = "agent4"
    check(R.responder_cursor(me) == 0, "cursor: default 0")
    R.set_responder_cursor(me, 42)
    check(R.responder_cursor(me) == 42, "cursor: persists 42")
    # must be a SEPARATE file from the real agent cursor (never .bus_cursors/agent4.seq)
    real = os.path.join(sand, ".bus_cursors", "agent4.seq")
    check(not os.path.exists(real), "cursor: responder does NOT touch the tab's agent cursor")
    own = os.path.join(sand, ".bus_responder_cursors", "agent4.seq")
    check(os.path.exists(own), "cursor: responder writes its OWN namespaced cursor")
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python scripts/office/tests/test_responder.py`
Expected: FAIL — `AttributeError: ... 'responder_cursor'`.

- [ ] **Step 3: Write the implementation** (append to `office_responder.py`)

```python
def _responder_cursor_path(me):
    return os.path.join(office_bus._dir(), ".bus_responder_cursors", me + ".seq")


def responder_cursor(me):
    try:
        with open(_responder_cursor_path(me), "r", encoding="utf-8") as f:
            return int(f.read().strip())
    except (OSError, ValueError):
        return 0


def set_responder_cursor(me, seq):
    p = _responder_cursor_path(me)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w", encoding="utf-8") as f:
        f.write(str(int(seq)))
```

- [ ] **Step 4: Run the tests and make sure they pass**

Run: `python scripts/office/tests/test_responder.py`
Expected: all `ok:`, `0 failure(s)`.

- [ ] **Step 5: Add `.bus_responder_cursors/` to .gitignore + commit**

In `.gitignore`, after the `agents/.office_heartbeats/` line, add:
```
agents/.bus_responder_cursors/
```
Then:
```bash
git add scripts/office/office_responder.py scripts/office/tests/test_responder.py .gitignore
git commit -m "feat(office): responder gets its own cursor namespace (never starves the tab)"
```

---

## Task 6: The watch loop in DRY-RUN (no claude, no posting)

**Files:**
- Modify: `scripts/office/office_responder.py`

- [ ] **Step 1: Implement the IO helpers + dry-run loop** (append to `office_responder.py`)

```python
def _bus_records():
    bus = office_bus.BUS()
    out = []
    if os.path.exists(bus):
        with open(bus, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    out.append(json.loads(line))
                except json.JSONDecodeError:
                    continue
    return out


def find_candidates(records, me, after_seq):
    out = []
    for r in records:
        if int(r.get("seq", 0)) <= after_seq:
            continue
        if is_candidate(r, me):
            out.append({"seq": int(r["seq"]), "frm": r.get("from"),
                        "to": r.get("to"), "text": r.get("msg", "")})
    return out


def log(me, msg):
    print("[responder {0}] {1}".format(me, msg), flush=True)


def run(me, dry_run=True, model="claude", once=False, window=FALLBACK_WINDOW):
    """Watch loop. Dry-run logs would-post decisions and posts nothing."""
    last = responder_cursor(me) or max((int(r.get("seq", 0)) for r in _bus_records()), default=0)
    set_responder_cursor(me, last)
    log(me, "fallback responder live (dry_run={0}, window={1}s) from seq {2}".format(dry_run, window, last))
    pending = []  # list of (trigger, due_epoch)
    while True:
        now = int(time.time())
        records = _bus_records()
        for t in find_candidates(records, me, last):
            last = max(last, t["seq"])
            pending.append((t, now + window))
            log(me, "candidate seq {0} from {1}: '{2}' — waiting {3}s for the real brother".format(
                t["seq"], t["frm"], t["text"][:50], window))
        set_responder_cursor(me, last)
        still = []
        for t, due in pending:
            if now < due:
                still.append((t, due))
                continue
            sup, reason = should_suppress(t, records, me)
            if sup:
                log(me, "seq {0}: SUPPRESS — {1}".format(t["seq"], reason))
            else:
                handle_due_trigger(me, t, records, dry_run, model)  # Task 7/8
        pending = still
        if once:
            return
        time.sleep(3)
```

(`handle_due_trigger` is defined in Task 7. The loop is structurally complete now; Task 7 fills the draft, Task 8 the posting.)

- [ ] **Step 2: Add a temporary stub for `handle_due_trigger` so the module imports** (append, will be replaced in Task 7)

```python
def handle_due_trigger(me, trigger, records, dry_run, model):
    log(me, "seq {0}: WOULD respond (draft not wired yet — Task 7)".format(trigger["seq"]))
```

- [ ] **Step 3: Smoke the dry-run loop once against the live bus**

Run: `python -c "import sys; sys.argv=['x']; sys.path.insert(0,'scripts/office'); import office_responder as R; R.run('agent4', dry_run=True, once=True, window=0)"`
Expected: logs `fallback responder live ...` and, for any pending message to agent4, a `candidate ...` + `WOULD respond` / `SUPPRESS` line. Posts nothing.

- [ ] **Step 4: Commit**

```bash
git add scripts/office/office_responder.py
git commit -m "feat(office): responder dry-run watch loop (candidates + fallback timer, no posting)"
```

---

## Task 7: Wire `claude -p` draft (still dry-run — logs the reply object)

**Files:**
- Modify: `scripts/office/office_responder.py`

Use the EXACT command variant that PASSED the Task 1 contract test.

- [ ] **Step 1: Implement the driver + replace `handle_due_trigger`** (in `office_responder.py`, replace the Task-6 stub)

```python
RESPONSE_SCHEMA = {
    "type": "object", "additionalProperties": False,
    "properties": {"replies": {
        "type": "array", "maxItems": 2,
        "items": {"type": "object", "additionalProperties": False,
                  "properties": {"to": {"type": "string"}, "class": {"type": "string"},
                                 "msg": {"type": "string"}},
                  "required": ["to", "class", "msg"]}}},
    "required": ["replies"],
}


def build_prompt(me, trigger, records):
    recent = [{"seq": r.get("seq"), "from": r.get("from"), "to": r.get("to"),
               "msg": str(r.get("msg", ""))[:300]} for r in records[-12:]]
    payload = {"you_are_backup_for": me, "unanswered_message": trigger, "recent_context": recent}
    return (
        "You are the AUTOMATED BACKUP for {0} in THE OFFICE. {0}'s interactive tab went "
        "idle and did not answer the message below within {1}s. Decide whether a brief, "
        "NON-BINDING backup reply is useful.\n"
        "HARD RULES:\n"
        "- Output JSON matching the schema only; {{\"replies\": []}} if no reply is useful.\n"
        "- You are NOT {0}. NEVER commit {0} to a domain decision, an implementation promise, "
        "or a position he didn't take. For anything substantive use class "
        "'nonbinding_assessment' or 'decline_owner_confirmation_required' and phrase it as a "
        "backup read for the owner to confirm.\n"
        "- Pick class from: ack, handoff, clarifying_question, nonbinding_assessment, "
        "decline_owner_confirmation_required.\n"
        "- Keep msg under 240 chars. Do not echo wake prompts or acknowledge generic broadcasts.\n\n"
        "Payload:\n{2}"
    ).format(me, FALLBACK_WINDOW, json.dumps(payload, ensure_ascii=False, indent=2))


def run_claude(prompt, model, timeout=120):
    """Invoke the contract-tested claude -p command. Returns the replies list or
    raises. (Use the exact variant that passed office_responder_contract_check.py.)"""
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        schema_path = os.path.join(td, "schema.json")
        with open(schema_path, "w", encoding="utf-8") as f:
            json.dump(RESPONSE_SCHEMA, f)
        cmd = ["claude", "-p", "--no-session-persistence", "--permission-mode", "default",
               "--tools", "", "--disallowedTools", "Edit,Write,Bash", "--strict-mcp-config",
               "--json-schema", schema_path, "--output-format", "json"]
        proc = subprocess.run(cmd, input=prompt, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=timeout)
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr.strip() or "claude -p failed")
        replies = _extract_replies(proc.stdout.strip())
        if replies is None:
            raise RuntimeError("could not parse replies from: " + proc.stdout.strip()[:300])
        return replies


def _extract_replies(out):
    for cand in (out, (out[out.find("{"):out.rfind("}") + 1] if "{" in out else "")):
        try:
            obj = json.loads(cand)
        except (json.JSONDecodeError, TypeError):
            continue
        if isinstance(obj, dict):
            if isinstance(obj.get("replies"), list):
                return obj["replies"]
            inner = obj.get("structured_output") or obj.get("result")
            if isinstance(inner, dict) and isinstance(inner.get("replies"), list):
                return inner["replies"]
            if isinstance(inner, str):
                try:
                    return json.loads(inner).get("replies")
                except json.JSONDecodeError:
                    pass
    return None


def handle_due_trigger(me, trigger, records, dry_run, model):
    set_responder_cursor(me, max(responder_cursor(me), trigger["seq"]))  # consume before draft
    try:
        replies = run_claude(build_prompt(me, trigger, records), model)
    except Exception as exc:
        log(me, "seq {0}: backup-failed — {1}".format(trigger["seq"], exc))
        office_bus.cmd_append("system", "all", "activity", "null",
                              "[responder] backup-failed for {0} seq {1}".format(me, trigger["seq"]))
        return
    if not replies:
        log(me, "seq {0}: model chose no reply".format(trigger["seq"]))
        return
    for rep in replies[:2]:
        try:
            body = format_backup_reply(me, rep.get("class", ""), rep.get("msg", ""))
        except ValueError as e:
            log(me, "seq {0}: bad reply class — {1}".format(trigger["seq"], e))
            continue
        if dry_run:
            log(me, "seq {0}: DRY-RUN would post to {1}: {2}".format(trigger["seq"], rep.get("to"), body))
        else:
            post_reply(me, rep.get("to"), body, trigger, records)  # Task 8
```

- [ ] **Step 2: Add a `post_reply` stub (replaced in Task 8) so the module imports**

```python
def post_reply(me, to, body, trigger, records):
    log(me, "posting not enabled yet (Task 8)")
```

- [ ] **Step 3: Dry-run smoke with a real claude call** (only if Task 1 PASSED)

Run: `python -c "import sys; sys.path.insert(0,'scripts/office'); import office_responder as R; R.run('agent4', dry_run=True, once=True, window=0)"` after appending a test message to agent4 via `python scripts/office/office_bus.py append agent0 agent4 chat null 'responder dry-run probe — please ack?'`.
Expected: logs a `DRY-RUN would post to ...: [auto · agent4's tab idle · ack] ...` line (Claude actually invoked, reply drafted, NOTHING posted). If it logs `backup-failed`, revisit the Task 1 command.

- [ ] **Step 4: Commit**

```bash
git add scripts/office/office_responder.py
git commit -m "feat(office): responder claude -p draft in dry-run (drafts, logs, posts nothing)"
```

---

## Task 8: Enable posting — pre-send recheck, lock, CLI entrypoint

**Files:**
- Modify: `scripts/office/office_responder.py`

- [ ] **Step 1: Implement `post_reply` with the pre-send recheck** (replace the Task-7 stub)

```python
def post_reply(me, to, body, trigger, records):
    # Pre-send recheck (Agent 7's second-61 race): re-read the bus, re-run the
    # target-aware check; if the real brother answered while we were drafting, abort.
    fresh = _bus_records()
    sup, reason = should_suppress(trigger, fresh, me)
    if sup:
        log(me, "seq {0}: ABORT post — brother answered during draft ({1})".format(trigger["seq"], reason))
        return
    session = "responder-" + me
    office_bus.cmd_join(session, me[len("agent"):])     # session -> agentN identity
    who = office_bus._agent_for(session)
    if who != me:
        log(me, "REFUSING to post: session {0} maps to {1}, not {2}".format(session, who, me))
        return
    target = to[1:] if str(to).startswith("@") else to
    office_bus.cmd_append(me, target, "chat", "null", body)
    log(me, "seq {0}: POSTED as {1} to {2}".format(trigger["seq"], me, target))
```

- [ ] **Step 2: Add single-instance lock + CLI `main`** (append)

```python
def _lock_path(me):
    return os.path.join(office_bus._dir(), ".responder_" + me + ".lock")


def acquire_lock(me):
    p = _lock_path(me)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    try:
        os.mkdir(p)
        return True
    except FileExistsError:
        return False


def release_lock(me):
    try:
        os.rmdir(_lock_path(me))
    except OSError:
        pass


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="Owned-worker fallback responder for one Claude brother.")
    ap.add_argument("agent", help="e.g. agent4")
    ap.add_argument("--live", action="store_true", help="actually post (default is dry-run)")
    ap.add_argument("--window", type=int, default=FALLBACK_WINDOW)
    ap.add_argument("--no-lock", action="store_true")
    a = ap.parse_args(argv)
    if not a.no_lock and not acquire_lock(a.agent):
        raise SystemExit("responder already running for " + a.agent)
    try:
        run(a.agent, dry_run=not a.live, window=a.window)
    finally:
        if not a.no_lock:
            release_lock(a.agent)


if __name__ == "__main__":
    main(sys.argv[1:])
```

- [ ] **Step 3: Run the full test suite (pure logic still green)**

Run: `python scripts/office/tests/test_responder.py`
Expected: all `ok:`, `0 failure(s)`.

- [ ] **Step 4: Commit**

```bash
git add scripts/office/office_responder.py
git commit -m "feat(office): responder posting enabled — pre-send recheck + lock + CLI"
```

---

## Task 9: Closeout — pilot smoke on one brother, docs, memory

**Files:**
- Verify: `scripts/office/tests/test_responder.py`
- Update: spec status; memory

- [ ] **Step 1: Full suite green + import check**

```bash
python scripts/office/tests/test_responder.py && python -c "import sys;sys.path.insert(0,'scripts/office');import office_responder;print('imports OK')"
```
Expected: `0 failure(s)` and `imports OK`.

- [ ] **Step 2: Live pilot smoke (Agent 4 — a brother that stranded a collaborator this session).** With Agent 4's tab idle/closed, in a console:

```bash
python scripts/office/office_responder.py agent4 --live --window 20
```
Then from another shell: `python scripts/office/office_bus.py append agent0 agent4 chat null "responder live test — quick ack?"`. Confirm: after ~20s of Agent 4 silence, the responder posts `[auto · agent4's tab idle · ack] ...` AS agent4. Then post AS agent4 within the window on a second message and confirm the pre-send recheck ABORTS the backup. Ctrl+C to stop.

- [ ] **Step 3: Mark the spec piloted.** In `docs/superpowers/specs/2026-05-31-owned-worker-responder-design.md`, change the Status line to append `· PILOTED on agent4 2026-05-31`.

- [ ] **Step 4: Commit + record memory**

```bash
git add docs/superpowers/specs/2026-05-31-owned-worker-responder-design.md
git commit -m "docs(office): owned-worker responder piloted on agent4"
```
Then use memory-write to note in `project_the_office_live_agent_bus`: the responder shipped (claude -p contract gated, target-aware fallback, non-binding typed replies), piloted on agent4 — the first reliable wake/respond cure for a Claude brother.

---

## Self-Review

**Spec coverage (§ → task):**
- §2 target-aware fallback + pre-send recheck → Task 2 (`should_suppress`) + Task 8 (recheck in `post_reply`). ✓
- §3 separate cursor / @all gate / loop → Task 5 (cursor) + Task 3 (`is_candidate`) + Task 6 (loop). ✓
- §4 non-binding typed replies + marker → Task 4 (`format_backup_reply` + classes). ✓
- §5 lock / reply-only / backup-failed event → Task 8 (lock) + Task 7 (`backup-failed` activity post). ✓
- §7 `claude -p` contract test GATE → Task 1 (with SDK-pivot escalation). ✓
- §8 dry-run-first build order → Tasks 6 (dry loop) → 7 (dry draft) → 8 (posting last). ✓

**Placeholder scan:** the Task-6/7 stubs are explicitly temporary and replaced in the next task (named, not vague). No TBD/TODO. Every code step is complete. ✓

**Type/name consistency:** trigger record `{seq,frm,to,text}` identical across `should_suppress`/`find_candidates`/`handle_due_trigger`. Reply classes tuple identical in `REPLY_CLASSES`/schema/prompt/`format_backup_reply`. `should_suppress`/`is_candidate`/`format_backup_reply`/`responder_cursor` signatures consistent across def, tests, and callers. The Task-1 contract command == the Task-7 `run_claude` command. ✓

**Gate honesty:** Task 1 is a real gate — FAIL stops the build and escalates to the Agent SDK pivot (§7), not a hack-around. Posting is the LAST thing enabled (Task 8), after two dry-run stages. ✓
