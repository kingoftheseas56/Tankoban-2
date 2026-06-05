# Stream/Theatre Autonomous Smoke Harness — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an autonomous on-device smoke harness that drives the real Tankoban app across Stream/Theatre journeys, decides pass/fail via a two-lane oracle (SELF=tankoctl+logs, VISUAL=Gemini-described ffmpeg), and emits fully-traceable findings.

**Architecture:** A thin layer on `scripts/agents/drive_journal.py` (`Driver`/`tankoctl()` already do log-mark bracketing + events-delta capture + recording). New pure-logic modules (oracle, probe extractor, catalogue loader, mark parser, correlator) are TDD'd; the runner/recorder/visual modules are integration code proven by the first real run. Spec: `docs/superpowers/specs/2026-06-05-stream-theatre-autonomous-smoke-harness-design.md`.

**Tech Stack:** Python 3 (stdlib + pytest), `tankoctl.exe`, ffmpeg (`screen_record.ScreenRecorder`), Gemini via `scripts/engines/engine.py` (`call_gemini_visual`), OBS-10 `introspect-*`.

---

## File Structure

All new files under `scripts/agents/smoke/`:

- `oracle.py` — pure: evaluate an `expect` spec against a value → (pass, reason).
- `probes.py` — pure: extract a value from a tankoctl reply by dotted/indexed path.
- `catalogue.py` — pure: load + validate the journeys/assertions JSON.
- `marks.py` — pure: parse `log-mark` BEGIN/END windows (label → start/end ts) from events.jsonl.
- `findings.py` — pure: correlate SELF results + VISUAL descriptions + log lines on the mark label; drop untraceable.
- `recording.py` — integration: session-long ScreenRecorder owner + wallclock→video-offset math (offset math is pure, tested).
- `runner.py` — integration: load catalogue → start recording → drive each step (log-mark, act, poll, oracle) → recovery → emit SELF results.
- `describe_visual.py` — integration: pick windows (pure select) → sample+downscale frames → batch under 4MB → call engine `see` → attach to labels.
- `run_smoke.py` — CLI entry wiring runner → describe_visual → findings.
- `catalogue_stream.json` — the Stream/Theatre check catalogue (J1–J8).
- `README.md` — runbook.
- `test_oracle.py`, `test_probes.py`, `test_catalogue.py`, `test_marks.py`, `test_findings.py`, `test_recording_offset.py`, `test_visual_window.py` — pytest.

Test invocation throughout: `python -m pytest scripts/agents/smoke/<test>.py -v` (pytest already used by `scripts/engines/test_engine.py`).

---

## Task 0: Validate the hardened recorder (gate — do this FIRST)

Agent 0 hardened + proved the recorder (`c6f59d2`): in-process/session-attached,
fragmented MP4 (a crash-killed long run is still playable), ddagrab→gdigrab
fallback, runbook §3c. The whole visual lane sits on this — confirm it in YOUR
context before building, and **never launch the recorder detached-hidden** (that
silently kills ddagrab → no file).

**Files:** none (validation only).

- [ ] **Step 1: Real-duration recording self-test**

Run: `python scripts\agents\screen_record.py test 18`
Expected: `mode=ddagrab bytes=<nonzero>` (or `gdigrab` fallback) + a playable MP4.
A zero-byte / missing file is a STOP — coordinate with Agent 0, do not proceed.

- [ ] **Step 2: Confirm clip extraction works on the output**

Run: `"C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin\ffmpeg.exe" -hide_banner -loglevel error -y -ss 00:00:02 -t 4 -i out\_rec_selftest.mp4 -c copy out\_clip_test.mp4`
Then confirm `out\_clip_test.mp4` is nonzero + playable (proves the Task 8 clip path).

- [ ] **Step 3: Gate**

No commit (validation only). Both pass → foundation is real, proceed to Task 1.
Either fails → STOP and coordinate with Agent 0.

---

## Task 1: oracle.py — expect evaluator (pure logic, TDD)

**Files:**
- Create: `scripts/agents/smoke/oracle.py`
- Test: `scripts/agents/smoke/test_oracle.py`

- [ ] **Step 1: Write the failing test**

```python
# scripts/agents/smoke/test_oracle.py
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oracle import evaluate

def test_equals():
    assert evaluate({"equals": "playing"}, "playing")[0] is True
    assert evaluate({"equals": "playing"}, "paused")[0] is False

def test_approx_tolerance():
    assert evaluate({"approx": 600, "tolerance": 5}, 603)[0] is True
    assert evaluate({"approx": 600, "tolerance": 5}, 590)[0] is False

def test_gt_gte_lt():
    assert evaluate({"gt": 0}, 5)[0] is True
    assert evaluate({"gt": 0}, 0)[0] is False
    assert evaluate({"gte": 0}, 0)[0] is True
    assert evaluate({"lt": 100}, 50)[0] is True

def test_contains_regex():
    assert evaluate({"contains": "One Piece"}, "[SubsPlease] One Piece 1133")[0] is True
    assert evaluate({"regex": r"\d+~\d+"}, "1123~1133")[0] is True
    assert evaluate({"regex": r"\d+~\d+"}, "1133")[0] is False

def test_exists_nonempty_len():
    assert evaluate({"exists": True}, 0)[0] is True
    assert evaluate({"exists": True}, None)[0] is False
    assert evaluate({"nonempty": True}, [1])[0] is True
    assert evaluate({"nonempty": True}, [])[0] is False
    assert evaluate({"len_gte": 1}, ["a"])[0] is True
    assert evaluate({"len_gte": 2}, ["a"])[0] is False

def test_reason_is_human():
    ok, reason = evaluate({"approx": 600, "tolerance": 5}, 590)
    assert ok is False and "590" in reason and "600" in reason
```

- [ ] **Step 2: Run to verify it fails**

Run: `python -m pytest scripts/agents/smoke/test_oracle.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'oracle'`.

- [ ] **Step 3: Implement oracle.py**

```python
# scripts/agents/smoke/oracle.py
"""Pure-logic evaluator: does a probed VALUE satisfy an EXPECT spec?
Returns (passed: bool, reason: str). No I/O, no Qt, no tankoctl."""
import re


def _num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def evaluate(expect, value):
    """expect is a dict with exactly one or more operator keys (AND-combined).
    Supported: equals, approx(+tolerance), gt, gte, lt, lte, contains, regex,
    exists, nonempty, len_gte."""
    if not isinstance(expect, dict) or not expect:
        return False, f"bad expect spec: {expect!r}"

    for op, target in expect.items():
        if op == "tolerance":
            continue  # consumed by approx
        if op == "equals":
            if value != target:
                return False, f"equals {target!r}: got {value!r}"
        elif op == "approx":
            tol = _num(expect.get("tolerance", 0)) or 0.0
            v, t = _num(value), _num(target)
            if v is None or t is None or abs(v - t) > tol:
                return False, f"approx {target}±{tol}: got {value!r}"
        elif op in ("gt", "gte", "lt", "lte"):
            v, t = _num(value), _num(target)
            if v is None or t is None:
                return False, f"{op} {target}: non-numeric {value!r}"
            ok = (op == "gt" and v > t) or (op == "gte" and v >= t) \
                or (op == "lt" and v < t) or (op == "lte" and v <= t)
            if not ok:
                return False, f"{op} {target}: got {value}"
        elif op == "contains":
            if target not in (value or ""):
                return False, f"contains {target!r}: got {str(value)[:80]!r}"
        elif op == "regex":
            if not re.search(target, str(value or "")):
                return False, f"regex {target!r}: no match in {str(value)[:80]!r}"
        elif op == "exists":
            present = value is not None
            if present != bool(target):
                return False, f"exists {target}: value is {value!r}"
        elif op == "nonempty":
            ne = bool(value)
            if ne != bool(target):
                return False, f"nonempty {target}: got {value!r}"
        elif op == "len_gte":
            n = len(value) if hasattr(value, "__len__") else -1
            if n < target:
                return False, f"len_gte {target}: len is {n}"
        else:
            return False, f"unknown operator {op!r}"
    return True, "ok"
```

- [ ] **Step 4: Run to verify it passes**

Run: `python -m pytest scripts/agents/smoke/test_oracle.py -v`
Expected: PASS (7 tests).

- [ ] **Step 5: Commit**

```bash
git add scripts/agents/smoke/oracle.py scripts/agents/smoke/test_oracle.py
git commit -m "feat(smoke): oracle expect-evaluator (pure logic, TDD)"
```

---

## Task 2: probes.py — reply value extractor (pure logic, TDD)

**Files:**
- Create: `scripts/agents/smoke/probes.py`
- Test: `scripts/agents/smoke/test_probes.py`

- [ ] **Step 1: Write the failing test**

```python
# scripts/agents/smoke/test_probes.py
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probes import extract, MISSING

REPLY = {
    "snapshot": {"detail": {"currentImdb": "tt0388629", "episodeRows": [1, 2, 3]}},
    "entries": [{"progress": 0.42, "imdb": "tt0388629"}],
    "results": [{"imdb": "tt0388629"}, {"imdb": "tt11737520"}],
}

def test_dotted_path():
    assert extract(REPLY, "snapshot.detail.currentImdb") == "tt0388629"

def test_indexed_path():
    assert extract(REPLY, "entries[0].progress") == 0.42

def test_list_len_via_path():
    assert extract(REPLY, "snapshot.detail.episodeRows") == [1, 2, 3]

def test_missing_returns_sentinel():
    assert extract(REPLY, "snapshot.nope.x") is MISSING
    assert extract(REPLY, "entries[9].progress") is MISSING

def test_wildcard_collects_field():
    # "results[*].imdb" -> list of all imdb values
    assert extract(REPLY, "results[*].imdb") == ["tt0388629", "tt11737520"]
```

- [ ] **Step 2: Run to verify it fails**

Run: `python -m pytest scripts/agents/smoke/test_probes.py -v`
Expected: FAIL — no module `probes`.

- [ ] **Step 3: Implement probes.py**

```python
# scripts/agents/smoke/probes.py
"""Pure-logic accessor: pull a value out of a parsed tankoctl reply by a
dotted/indexed path. Supports `a.b`, `a[0].b`, and `a[*].b` (collect)."""
import re

MISSING = object()
_TOK = re.compile(r"([^.\[\]]+)|\[(\d+|\*)\]")


def _tokens(path):
    out = []
    for name, idx in _TOK.findall(path):
        if name:
            out.append(("key", name))
        elif idx == "*":
            out.append(("all", None))
        else:
            out.append(("idx", int(idx)))
    return out


def extract(obj, path):
    """Return the value at `path`, or MISSING if any hop is absent."""
    cur = obj
    for kind, val in _tokens(path):
        if kind == "key":
            if isinstance(cur, dict) and val in cur:
                cur = cur[val]
            else:
                return MISSING
        elif kind == "idx":
            if isinstance(cur, list) and -len(cur) <= val < len(cur):
                cur = cur[val]
            else:
                return MISSING
        elif kind == "all":
            if not isinstance(cur, list):
                return MISSING
            # remaining tokens after [*] apply to each element
            rest = path.split("[*]", 1)[1].lstrip(".")
            return [extract(e, rest) for e in cur] if rest else cur
    return cur
```

- [ ] **Step 4: Run to verify it passes**

Run: `python -m pytest scripts/agents/smoke/test_probes.py -v`
Expected: PASS (5 tests).

- [ ] **Step 5: Commit**

```bash
git add scripts/agents/smoke/probes.py scripts/agents/smoke/test_probes.py
git commit -m "feat(smoke): probes reply-value extractor (pure logic, TDD)"
```

---

## Task 3: catalogue.py — load + validate (pure logic, TDD)

**Files:**
- Create: `scripts/agents/smoke/catalogue.py`
- Test: `scripts/agents/smoke/test_catalogue.py`

- [ ] **Step 1: Write the failing test**

```python
# scripts/agents/smoke/test_catalogue.py
import os, sys, json, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from catalogue import load, ValidationError
import pytest

GOOD = {
  "journeys": [
    {"id": "J1", "name": "One Piece", "steps": [
      {"id": "op.search", "action": ["search", "One Piece"],
       "asserts": [
         {"id": "op.search.returns", "lane": "SELF",
          "probe": ["search", "One Piece"], "path": "results[*].imdb",
          "expect": {"contains": "tt0388629"}, "timeoutSec": 15},
         {"id": "op.detail.renders", "lane": "VISUAL",
          "gemini_prompt": "Is the hero art and episode list painted?"}
       ]}
    ]}
  ]
}

def _write(obj):
    fd, p = tempfile.mkstemp(suffix=".json"); os.close(fd)
    with open(p, "w", encoding="utf-8") as f: json.dump(obj, f)
    return p

def test_loads_good():
    cat = load(_write(GOOD))
    assert cat.journeys[0].id == "J1"
    assert cat.journeys[0].steps[0].asserts[0].lane == "SELF"

def test_self_requires_probe_and_expect():
    bad = json.loads(json.dumps(GOOD))
    del bad["journeys"][0]["steps"][0]["asserts"][0]["expect"]
    with pytest.raises(ValidationError):
        load(_write(bad))

def test_visual_requires_prompt():
    bad = json.loads(json.dumps(GOOD))
    del bad["journeys"][0]["steps"][0]["asserts"][1]["gemini_prompt"]
    with pytest.raises(ValidationError):
        load(_write(bad))

def test_duplicate_assert_id_rejected():
    bad = json.loads(json.dumps(GOOD))
    bad["journeys"][0]["steps"][0]["asserts"][1]["id"] = "op.search.returns"
    with pytest.raises(ValidationError):
        load(_write(bad))
```

- [ ] **Step 2: Run to verify it fails**

Run: `python -m pytest scripts/agents/smoke/test_catalogue.py -v`
Expected: FAIL — no module `catalogue`.

- [ ] **Step 3: Implement catalogue.py**

```python
# scripts/agents/smoke/catalogue.py
"""Pure-logic loader+validator for the smoke catalogue JSON."""
import json
from dataclasses import dataclass, field
from typing import List, Optional


class ValidationError(Exception):
    pass


@dataclass
class Assertion:
    id: str
    lane: str
    probe: Optional[list] = None
    path: Optional[str] = None
    expect: Optional[dict] = None
    gemini_prompt: Optional[str] = None
    timeoutSec: int = 10
    on_fail: str = "continue"
    needs: List[str] = field(default_factory=list)


@dataclass
class Step:
    id: str
    action: Optional[list]
    asserts: List[Assertion]
    settleSec: float = 1.5


@dataclass
class Journey:
    id: str
    name: str
    steps: List[Step]


@dataclass
class Catalogue:
    journeys: List[Journey]


def load(path):
    with open(path, "r", encoding="utf-8") as f:
        raw = json.load(f)
    seen_ids = set()
    journeys = []
    if not raw.get("journeys"):
        raise ValidationError("catalogue has no journeys")
    for j in raw["journeys"]:
        steps = []
        for s in j.get("steps", []):
            asserts = []
            for a in s.get("asserts", []):
                aid = a.get("id")
                if not aid:
                    raise ValidationError(f"assertion missing id in step {s.get('id')}")
                if aid in seen_ids:
                    raise ValidationError(f"duplicate assertion id {aid}")
                seen_ids.add(aid)
                lane = a.get("lane")
                if lane == "SELF":
                    if not a.get("probe") or "expect" not in a:
                        raise ValidationError(f"SELF assertion {aid} needs probe+expect")
                elif lane == "VISUAL":
                    if not a.get("gemini_prompt"):
                        raise ValidationError(f"VISUAL assertion {aid} needs gemini_prompt")
                else:
                    raise ValidationError(f"assertion {aid} bad lane {lane!r}")
                asserts.append(Assertion(
                    id=aid, lane=lane, probe=a.get("probe"), path=a.get("path"),
                    expect=a.get("expect"), gemini_prompt=a.get("gemini_prompt"),
                    timeoutSec=a.get("timeoutSec", 10), on_fail=a.get("on_fail", "continue"),
                    needs=a.get("needs", [])))
            steps.append(Step(id=s["id"], action=s.get("action"),
                              asserts=asserts, settleSec=s.get("settleSec", 1.5)))
        journeys.append(Journey(id=j["id"], name=j.get("name", j["id"]), steps=steps))
    return Catalogue(journeys=journeys)
```

- [ ] **Step 4: Run to verify it passes**

Run: `python -m pytest scripts/agents/smoke/test_catalogue.py -v`
Expected: PASS (4 tests).

- [ ] **Step 5: Commit**

```bash
git add scripts/agents/smoke/catalogue.py scripts/agents/smoke/test_catalogue.py
git commit -m "feat(smoke): catalogue loader+validator (pure logic, TDD)"
```

---

## Task 4: marks.py — log-mark window parser (pure logic, TDD)

The runner emits `log-mark "SMOKE <assert-id> START"` / `... END pass|fail`. Those land in `events.jsonl` as `qt_message` rows. `marks.py` turns them into per-id windows used to slice the recording + logs.

**Files:**
- Create: `scripts/agents/smoke/marks.py`
- Test: `scripts/agents/smoke/test_marks.py`

- [ ] **Step 1: Write the failing test**

```python
# scripts/agents/smoke/test_marks.py
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from marks import parse_marks

EVENTS = [
    {"ts": "2026-06-05T10:00:00Z", "data": {"message": "[log-mark] SMOKE op.play.starts START"}},
    {"ts": "2026-06-05T10:00:09Z", "data": {"message": "[log-mark] SMOKE op.play.starts END pass"}},
    {"ts": "2026-06-05T10:00:10Z", "data": {"message": "[log-mark] SMOKE op.play.seek START"}},
    {"ts": "2026-06-05T10:00:14Z", "data": {"message": "[log-mark] SMOKE op.play.seek END fail"}},
    {"ts": "2026-06-05T10:00:20Z", "data": {"message": "unrelated noise"}},
]

def test_pairs_start_end():
    m = parse_marks(EVENTS)
    assert m["op.play.starts"]["start"] == "2026-06-05T10:00:00Z"
    assert m["op.play.starts"]["end"] == "2026-06-05T10:00:09Z"
    assert m["op.play.starts"]["verdict"] == "pass"
    assert m["op.play.seek"]["verdict"] == "fail"

def test_unterminated_start_has_no_end():
    ev = EVENTS[:1]
    m = parse_marks(ev)
    assert m["op.play.starts"]["end"] is None
```

- [ ] **Step 2: Run to verify it fails**

Run: `python -m pytest scripts/agents/smoke/test_marks.py -v`
Expected: FAIL — no module `marks`.

- [ ] **Step 3: Implement marks.py**

```python
# scripts/agents/smoke/marks.py
"""Pure-logic parser: extract SMOKE log-mark windows from events.jsonl rows.
Label grammar: '[log-mark] SMOKE <assert-id> START' and
'[log-mark] SMOKE <assert-id> END <verdict>'."""
import re

_RE = re.compile(r"SMOKE\s+(\S+)\s+(START|END)(?:\s+(\w+))?")


def _msg(ev):
    d = ev.get("data") or {}
    return d.get("message") or ev.get("message") or ""


def parse_marks(events):
    """Return {assert_id: {start, end, verdict}} from a list of event dicts."""
    marks = {}
    for ev in events:
        m = _RE.search(_msg(ev))
        if not m:
            continue
        aid, kind, verdict = m.group(1), m.group(2), m.group(3)
        slot = marks.setdefault(aid, {"start": None, "end": None, "verdict": None})
        if kind == "START":
            slot["start"] = ev.get("ts")
        else:
            slot["end"] = ev.get("ts")
            slot["verdict"] = verdict
    return marks
```

- [ ] **Step 4: Run to verify it passes**

Run: `python -m pytest scripts/agents/smoke/test_marks.py -v`
Expected: PASS (2 tests).

- [ ] **Step 5: Commit**

```bash
git add scripts/agents/smoke/marks.py scripts/agents/smoke/test_marks.py
git commit -m "feat(smoke): log-mark window parser (pure logic, TDD)"
```

---

## Task 5: findings.py — correlator (pure logic, TDD)

**Files:**
- Create: `scripts/agents/smoke/findings.py`
- Test: `scripts/agents/smoke/test_findings.py`

- [ ] **Step 1: Write the failing test**

```python
# scripts/agents/smoke/test_findings.py
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from findings import build_findings

SELF = [
    {"id": "op.play.starts", "journey": "J1", "lane": "SELF", "verdict": "pass",
     "action": "play-file ...", "reason": "ok"},
    {"id": "op.play.seek", "journey": "J1", "lane": "SELF", "verdict": "fail",
     "action": "player-seek 600", "reason": "approx 600: got 0"},
]
MARKS = {
    "op.play.starts": {"start": "2026-06-05T10:00:00Z", "end": "2026-06-05T10:00:09Z", "verdict": "pass"},
    "op.play.seek":   {"start": "2026-06-05T10:00:10Z", "end": "2026-06-05T10:00:14Z", "verdict": "fail"},
}
VISUAL = {"op.play.seek": "Frame is frozen; timestamp overlay does not move."}
LOGS = {"op.play.seek": "sidecar_debug_live.log: seek requested 600 but PTS frozen at 0"}

def test_traceable_finding_has_five_parts():
    f = build_findings(SELF, MARKS, VISUAL, LOGS, session_start="2026-06-05T10:00:00Z")
    seek = [x for x in f if x["id"] == "op.play.seek"][0]
    assert seek["verdict"] == "fail"
    assert seek["action"] == "player-seek 600"
    assert seek["video_offset"] == "00:00:10"   # 10s after session start
    assert "frozen" in seek["gemini"]
    assert "PTS frozen" in seek["log"]

def test_untraceable_dropped():
    # an id with no mark window cannot be joined -> dropped
    self_only = SELF + [{"id": "ghost", "journey": "J1", "lane": "SELF",
                         "verdict": "fail", "action": "x", "reason": "y"}]
    f = build_findings(self_only, MARKS, {}, {}, session_start="2026-06-05T10:00:00Z")
    assert all(x["id"] != "ghost" for x in f)
```

- [ ] **Step 2: Run to verify it fails**

Run: `python -m pytest scripts/agents/smoke/test_findings.py -v`
Expected: FAIL — no module `findings`.

- [ ] **Step 3: Implement findings.py**

```python
# scripts/agents/smoke/findings.py
"""Pure-logic correlator: join SELF/VISUAL results + marks + log lines into
traceable findings. A finding with no mark window is dropped (not done)."""
from datetime import datetime


def _iso(ts):
    return datetime.strptime(ts.replace("Z", ""), "%Y-%m-%dT%H:%M:%S")


def _offset(start, ts):
    if not start or not ts:
        return None
    secs = int((_iso(ts) - _iso(start)).total_seconds())
    if secs < 0:
        return None
    return f"{secs // 3600:02d}:{(secs % 3600) // 60:02d}:{secs % 60:02d}"


def build_findings(self_results, marks, visual, logs, session_start):
    """Return a list of traceable finding dicts. self_results: list of
    {id, journey, lane, verdict, action, reason}. visual/logs: {id: text}."""
    out = []
    # SELF + VISUAL ids both keyed by assert id; iterate the union present in marks.
    by_id = {r["id"]: r for r in self_results}
    all_ids = set(by_id) | set(visual)
    for aid in sorted(all_ids):
        win = marks.get(aid)
        if not win or not win.get("start"):
            continue  # untraceable -> dropped
        base = by_id.get(aid, {"id": aid, "journey": "?", "lane": "VISUAL",
                               "verdict": win.get("verdict") or "?",
                               "action": aid, "reason": ""})
        out.append({
            "id": aid,
            "journey": base.get("journey", "?"),
            "lane": base.get("lane"),
            "verdict": base.get("verdict"),
            "action": base.get("action"),
            "reason": base.get("reason", ""),
            "ts": win["start"],
            "video_offset": _offset(session_start, win["start"]),
            "gemini": visual.get(aid, ""),
            "log": logs.get(aid, ""),
        })
    return out
```

- [ ] **Step 4: Run to verify it passes**

Run: `python -m pytest scripts/agents/smoke/test_findings.py -v`
Expected: PASS (2 tests).

- [ ] **Step 5: Commit**

```bash
git add scripts/agents/smoke/findings.py scripts/agents/smoke/test_findings.py
git commit -m "feat(smoke): findings correlator, drop-untraceable (pure logic, TDD)"
```

---

## Task 6: recording.py — session recorder + offset (integration + pure offset test)

**Files:**
- Create: `scripts/agents/smoke/recording.py`
- Test: `scripts/agents/smoke/test_recording_offset.py`

- [ ] **Step 1: Write the failing test (pure offset helper)**

```python
# scripts/agents/smoke/test_recording_offset.py
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recording import wallclock_to_offset

def test_offset_basic():
    assert wallclock_to_offset("2026-06-05T10:00:00Z", "2026-06-05T10:02:05Z") == "00:02:05"

def test_offset_negative_is_zero():
    assert wallclock_to_offset("2026-06-05T10:00:10Z", "2026-06-05T10:00:00Z") == "00:00:00"
```

- [ ] **Step 2: Run to verify it fails**

Run: `python -m pytest scripts/agents/smoke/test_recording_offset.py -v`
Expected: FAIL — no module `recording`.

- [ ] **Step 3: Implement recording.py**

```python
# scripts/agents/smoke/recording.py
"""Owns ONE continuous screen recording for the whole smoke session. Because it
captures the screen (not the app process), it survives app crashes/relaunches —
the crash's frozen/black frame lands on the same timeline as the log-marks."""
import os
import sys
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))  # repo root not needed; screen_record is sibling-up
sys.path.insert(0, os.path.join(HERE, ".."))         # scripts/agents
from screen_record import ScreenRecorder  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))


def _iso(ts):
    return datetime.strptime(ts.replace("Z", ""), "%Y-%m-%dT%H:%M:%S")


def wallclock_to_offset(session_start, ts):
    secs = int((_iso(ts) - _iso(session_start)).total_seconds())
    if secs < 0:
        secs = 0
    return f"{secs // 3600:02d}:{(secs % 3600) // 60:02d}:{secs % 60:02d}"


class SessionRecording:
    def __init__(self, session, fps=10):
        self.path = os.path.join(REPO, "out", f"smoke_{session}.mp4")
        self.rec = ScreenRecorder(self.path, fps=fps)
        self.start_ts = None

    def start(self):
        ok = self.rec.start()
        self.start_ts = datetime.now().strftime("%Y-%m-%dT%H:%M:%SZ")
        return ok

    def stop(self):
        return self.rec.stop()
```

- [ ] **Step 4: Run to verify it passes**

Run: `python -m pytest scripts/agents/smoke/test_recording_offset.py -v`
Expected: PASS (2 tests).

- [ ] **Step 5: Commit**

```bash
git add scripts/agents/smoke/recording.py scripts/agents/smoke/test_recording_offset.py
git commit -m "feat(smoke): session recorder wrapper + offset math"
```

---

## Task 7: runner.py — drive loop + oracle + recovery (integration)

Integration code (drives the live app); verified by the proof run in Task 11, not a unit test. Uses `tankoctl` from `drive_journal`.

**Files:**
- Create: `scripts/agents/smoke/runner.py`

- [ ] **Step 1: Implement runner.py**

```python
# scripts/agents/smoke/runner.py
"""Drive the live app through a catalogue, evaluate SELF assertions, recover from
death/freeze. Emits SELF results (VISUAL handled post-session by describe_visual).

Per assertion: log-mark START -> (run the step action once per step) -> poll the
probe until expect passes or timeout -> log-mark END pass|fail."""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))  # scripts/agents -> drive_journal
from drive_journal import tankoctl                  # noqa: E402
from oracle import evaluate                          # noqa: E402
from probes import extract, MISSING                  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
HANG = os.path.join(REPO, "out", "HANG_DETECTED.json")
RUN_DRIVE = os.path.join(REPO, "scripts", "agents", "run_drive_mode.bat")


def _mark(label):
    tankoctl(["log-mark", f"SMOKE {label}"])


def _alive():
    ok, _ = tankoctl(["ping"], timeout=8)
    return ok


def _hang_present(since_size):
    try:
        return os.path.getsize(HANG) != since_size and os.path.exists(HANG)
    except OSError:
        return False


def relaunch():
    """Kill stray instances (Rule 1 — known relaunch loop) and rebuild+launch."""
    subprocess.run(["taskkill", "/F", "/IM", "Tankoban.exe"],
                   capture_output=True)
    time.sleep(2)
    subprocess.Popen(["cmd", "/c", RUN_DRIVE],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(60):           # wait up to ~60s for the bridge
        time.sleep(1)
        if _alive():
            return True
    return False


def _eval_assert(a, hang_size):
    """Poll a SELF assertion until pass or timeout. Returns a result dict."""
    deadline = time.time() + a.timeoutSec
    last_reason = "no probe result"
    while time.time() < deadline:
        if _hang_present(hang_size):
            return {"verdict": "blocked", "reason": "HANG_DETECTED during probe",
                    "stack_available": False, "note": "blocked-by-crash — needs Agent 3 idle-spin fix / OBS-4"}
        ok, reply = tankoctl(a.probe, timeout=max(5, a.timeoutSec))
        if not ok and not isinstance(reply, dict):
            last_reason = "probe call failed"
            time.sleep(1); continue
        value = extract(reply, a.path) if a.path else reply
        if value is MISSING:
            last_reason = f"path {a.path} missing"
            time.sleep(1); continue
        passed, reason = evaluate(a.expect, value)
        if passed:
            return {"verdict": "pass", "reason": reason}
        last_reason = reason
        time.sleep(1)
    return {"verdict": "fail", "reason": last_reason}


def run_journey(journey, results):
    for step in journey.steps:
        # 1) perform the step action once (the trigger), bracketed by a mark.
        if step.action:
            _mark(f"{step.id} ACTION START")
            ok, reply = tankoctl(step.action, timeout=30)
            _mark(f"{step.id} ACTION END {'ok' if ok else 'err'}")
            time.sleep(step.settleSec)
        # 2) evaluate each SELF assertion (VISUAL ones are marked for later).
        passed_ids = {r["id"] for r in results if r["verdict"] == "pass"}
        for a in step.asserts:
            if a.needs and not all(n in passed_ids for n in a.needs):
                continue  # prereq failed -> skip (still traceable as absent)
            hang_size = os.path.getsize(HANG) if os.path.exists(HANG) else -1
            _mark(f"{a.id} START")
            if a.lane == "SELF":
                res = _eval_assert(a, hang_size)
            else:  # VISUAL — record a window; describe_visual fills it later
                time.sleep(2)  # let a couple seconds of footage accrue
                res = {"verdict": "pending_visual", "reason": "see describe_visual"}
            _mark(f"{a.id} END {res['verdict']}")
            res.update({"id": a.id, "journey": journey.id, "lane": a.lane,
                        "action": " ".join(map(str, step.action or [a.id]))})
            if res["verdict"] == "pass":
                passed_ids.add(a.id)
            results.append(res)
            print(f"  [{a.id}] {res['verdict']}: {res.get('reason','')[:80]}")
            if res["verdict"] == "fail" and a.on_fail == "abort_journey":
                return
            if res["verdict"] == "fail" and a.on_fail == "relaunch":
                relaunch()
        # 3) recovery check between steps. A dead bridge = blocked-by-crash, NOT a
        # functional fail of the feature under test (Agent 0 review flag 3).
        if not _alive():
            print("  ! bridge dead — relaunching (blocked-by-crash)")
            results.append({"id": f"{step.id}.death", "journey": journey.id,
                            "lane": "SELF", "verdict": "blocked",
                            "action": step.id, "reason": "bridge dead after step",
                            "stack_available": False,
                            "note": "blocked-by-crash — needs Agent 3 idle-spin fix / OBS-4"})
            relaunch()


def run_catalogue(cat, only=None):
    results = []
    for j in cat.journeys:
        if only and j.id not in only:
            continue
        print(f"== Journey {j.id}: {j.name} ==")
        run_journey(j, results)
    return results
```

- [ ] **Step 2: Smoke-compile the module**

Run: `python -c "import sys; sys.path.insert(0,'scripts/agents/smoke'); import runner; print('ok')"`
Expected: `ok` (imports resolve; no live app needed for import).

- [ ] **Step 3: Commit**

```bash
git add scripts/agents/smoke/runner.py
git commit -m "feat(smoke): runner — drive loop, oracle eval, recovery (integration)"
```

---

## Task 8: visual_workorder.py — clip flagged windows + work-order (no vision API)

**Files:**
- Create: `scripts/agents/smoke/visual_workorder.py`
- Test: `scripts/agents/smoke/test_visual_window.py`

> Revised per Agent 0 review flag 2 + Hemanth's courier directive: the harness
> calls NO vision API. It clips each flagged window from the full MP4; Hemanth
> couriers the CLIPS to the Gemini LLM; `fold_visual.py` (Task 9) merges the
> pasted-back `{id: description}` JSON. Cost lever = clip only flagged windows
> (every VISUAL assert + every SELF fail + every watchdog hit), never the 2h file.

- [ ] **Step 1: Write the failing test (pure window-selection)**

```python
# scripts/agents/smoke/test_visual_window.py
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from visual_workorder import select_flagged_windows

def test_self_fail_flagged():
    sr = [{"id": "a", "verdict": "pass"}, {"id": "b", "verdict": "fail"},
          {"id": "d", "verdict": "blocked"}]
    fl = select_flagged_windows(sr, ["v1"], watchdog_ids=[])
    assert "b" in fl      # SELF fail -> LOOK HARD
    assert "d" in fl      # blocked-by-crash -> LOOK HARD (eyes confirm the freeze)
    assert "a" not in fl  # SELF pass -> not flagged (still clipped if VISUAL, but not "look hard")
    assert "v1" not in fl # a VISUAL assert is described anyway; flagged = the look-hard set

def test_watchdog_flagged():
    assert "c" in select_flagged_windows([], [], watchdog_ids=["c"])
```

- [ ] **Step 2: Run to verify it fails**

Run: `python -m pytest scripts/agents/smoke/test_visual_window.py -v`
Expected: FAIL — no module `visual_workorder`.

- [ ] **Step 3: Implement visual_workorder.py**

```python
# scripts/agents/smoke/visual_workorder.py
"""Visual lane = a directed work-order over CLIPS of the session MP4, couriered by
Hemanth to the Gemini LLM (native video understanding — NOT the Gemini API). The
harness calls no vision API. It (1) clips each to-be-seen window from the full MP4
(ffmpeg -ss -t), (2) emits a work-order listing each clip + its question, flagging
SELF-fail/watchdog windows as LOOK HARD; Hemanth uploads the clips to Gemini and
pastes back {id: description} JSON, folded by fold_visual.py (Task 9)."""
import json
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
FFMPEG = r"C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin\ffmpeg.exe"


def select_flagged_windows(self_results, visual_ids, watchdog_ids):
    """The 'LOOK HARD' set: SELF fails + SELF blocked-by-crash + watchdog hits.
    VISUAL assertions are clipped+described regardless; this is the extra-attention
    set where something already looked wrong on the data side."""
    flagged = set(watchdog_ids)
    for r in self_results:
        if r.get("verdict") in ("fail", "blocked"):
            flagged.add(r["id"])
    return flagged


def _clip(mp4, off, dur, out_path):
    """Clip [off, off+dur] from mp4 -> out_path. Returns out_path or None."""
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    subprocess.run([FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
                    "-ss", off, "-t", str(dur), "-i", mp4,
                    "-c", "copy", out_path], capture_output=True)
    if not os.path.exists(out_path) or os.path.getsize(out_path) == 0:
        # stream-copy can fail on a non-keyframe boundary; re-encode fallback.
        subprocess.run([FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
                        "-ss", off, "-t", str(dur), "-i", mp4,
                        "-c:v", "libx264", "-preset", "veryfast",
                        "-pix_fmt", "yuv420p", out_path], capture_output=True)
    return out_path if os.path.exists(out_path) and os.path.getsize(out_path) > 0 else None


def build_workorder(session, mp4, visual_asserts, marks, self_results,
                    watchdog_ids, session_start, offset_fn, dur=8):
    """Clip flagged+visual windows and write the courier work-order.
    Returns (workorder_path, clips_dir). visual_asserts: {id: gemini_prompt}."""
    clips_dir = os.path.join(REPO, "out", f"smoke_{session}_clips")
    wo_path = os.path.join(REPO, "out", f"smoke_{session}_visual_workorder.md")
    flagged = select_flagged_windows(self_results, list(visual_asserts), watchdog_ids)
    # Clip every VISUAL assertion window + every flagged (look-hard) window.
    targets = set(visual_asserts) | flagged
    rows = []
    recorder_ok = os.path.exists(mp4) and os.path.getsize(mp4) > 0
    for aid in sorted(targets):
        win = marks.get(aid)
        if not win or not win.get("start"):
            continue
        off = offset_fn(session_start, win["start"])
        clip = _clip(mp4, off, dur, os.path.join(clips_dir, f"{aid}.mp4")) if recorder_ok else None
        rows.append((aid, off, clip, aid in flagged,
                     visual_asserts.get(aid, "A data check failed/blocked here — "
                                             "describe exactly what is on screen.")))
    lines = [f"# Visual work-order — {session}", "",
             "Courier each CLIP below to the Gemini LLM. For each, answer the "
             "question in one sentence. Reply as ONE JSON object: "
             "{\"<id>\": \"<answer>\", ...} and save it to "
             f"`out/smoke_{session}_visual_answers.json`.",
             "Checkpoints marked ** LOOK HARD ** already looked wrong on the data "
             "side — describe them most carefully.", ""]
    if not recorder_ok:
        lines.append("> ⚠ recorder produced no usable MP4 — clips unavailable. "
                     "Visual lane degraded (flagged, not faked).\n")
    for aid, off, clip, hard, q in rows:
        tag = " ** LOOK HARD **" if hard else ""
        cliptxt = os.path.relpath(clip, REPO) if clip else "(no clip — recorder failed)"
        lines.append(f"- [{aid}] @ {off}{tag}\n      clip: {cliptxt}\n      Q: {q}")
    with open(wo_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return wo_path, clips_dir


def load_visual_answers(path):
    """Parse Gemini's returned {id: description} JSON (or {} if absent/bad)."""
    if not path or not os.path.exists(path):
        return {}
    with open(path, "r", encoding="utf-8") as f:
        try:
            return json.load(f)
        except ValueError:
            return {}
```

- [ ] **Step 4: Run to verify it passes**

Run: `python -m pytest scripts/agents/smoke/test_visual_window.py -v`
Expected: PASS (2 tests).

- [ ] **Step 5: Commit**

```bash
git add scripts/agents/smoke/visual_workorder.py scripts/agents/smoke/test_visual_window.py
git commit -m "feat(smoke): visual lane — clip flagged windows + courier work-order (no vision API)"
```

---

## Task 9: run_smoke.py — CLI wiring + README

**Files:**
- Create: `scripts/agents/smoke/run_smoke.py`
- Create: `scripts/agents/smoke/README.md`

- [ ] **Step 1: Implement run_smoke.py**

```python
# scripts/agents/smoke/run_smoke.py
"""Entry point: catalogue -> drive (SELF) -> describe (VISUAL) -> traceable findings.

Usage:
  python scripts/agents/smoke/run_smoke.py --session op-proof \
      --catalogue scripts/agents/smoke/catalogue_stream.json --only J1,J2
  (add --no-visual to skip the Gemini pass; SELF lane still runs fully)
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
from drive_journal import tankoctl, _events_since, _events_offset  # noqa: E402
import catalogue as cat_mod                                         # noqa: E402
import runner as runner_mod                                         # noqa: E402
import marks as marks_mod                                           # noqa: E402
import findings as findings_mod                                     # noqa: E402
import visual_workorder as wo_mod                                   # noqa: E402
from recording import SessionRecording, wallclock_to_offset         # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--session", default="smoke")
    ap.add_argument("--catalogue", default=os.path.join(HERE, "catalogue_stream.json"))
    ap.add_argument("--only", default="")
    ap.add_argument("--no-visual", action="store_true")
    args = ap.parse_args()

    if not tankoctl(["ping"])[0]:
        print("ERROR: app not reachable — launch scripts/agents/run_drive_mode.bat first")
        return 1

    cat = cat_mod.load(args.catalogue)
    only = [s for s in args.only.split(",") if s] or None

    rec = SessionRecording(args.session)
    started = rec.start()
    print(f"recording: {'on -> ' + rec.path if started else 'FAILED (continuing SELF-only)'}")

    ev_off = _events_offset()
    self_results = runner_mod.run_catalogue(cat, only=only)
    events = _events_since(ev_off)
    rec.stop()

    marks = marks_mod.parse_marks(events)

    # Persist phase-A state so fold_visual.py can merge Gemini answers later.
    state_path = os.path.join(REPO, "out", f"smoke_{args.session}_state.json")
    with open(state_path, "w", encoding="utf-8") as f:
        json.dump({"self_results": self_results, "marks": marks,
                   "session_start": rec.start_ts}, f, ensure_ascii=False)

    # Build the visual work-order: clip flagged windows (NO vision API here).
    if not args.no_visual:
        visual_asserts = {a.id: a.gemini_prompt
                          for j in cat.journeys for s in j.steps for a in s.asserts
                          if a.lane == "VISUAL" and (not only or j.id in only)}
        watchdog_ids = [r["id"] for r in self_results if r.get("verdict") == "blocked"]
        wo, clips = wo_mod.build_workorder(
            args.session, rec.path, visual_asserts, marks, self_results,
            watchdog_ids, rec.start_ts, wallclock_to_offset)
        print(f"visual work-order -> {wo}\nclips dir       -> {clips}")

    logs = collect_logs(marks)
    found = findings_mod.build_findings(self_results, marks, {}, logs, rec.start_ts)
    write_report(args.session, found)  # SELF-only; visual folded by fold_visual.py
    print(f"\nSELF findings -> out/smoke_{args.session}_findings.md")
    if not args.no_visual:
        print("\nNEXT — visual lane (manual courier to the Gemini LLM):"
              f"\n  1. (optional, cheap) speed clips: screen_record.py speedup <clip> <out> 4"
              f"\n  2. upload out/smoke_{args.session}_clips/*.mp4 + the work-order to Gemini"
              f"\n  3. save its JSON reply -> out/smoke_{args.session}_visual_answers.json"
              f"\n  4. run: python scripts/agents/smoke/fold_visual.py --session {args.session}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

These two module-level helpers are used by both `run_smoke.py` and `fold_visual.py` — add them above `main()` in `run_smoke.py`:

```python
def collect_logs(marks):
    """For each assert id, grep the four log streams for one matching line."""
    logs = {}
    for aid in marks:
        ok, reply = tankoctl(["log-grep", aid, "3"])
        if ok and isinstance(reply, dict):
            hits = []
            for comp in (reply.get("perComponent") or {}).values():
                for m in (comp.get("matches") or [])[:1]:
                    hits.append(m.get("text", "")[:160])
            logs[aid] = " | ".join(hits)
    return logs


def write_report(session, found):
    """Write the traceable findings report (md + jsonl). blocked != fail."""
    md = os.path.join(REPO, "out", f"smoke_{session}_findings.md")
    js = os.path.join(REPO, "out", f"smoke_{session}_findings.jsonl")
    with open(js, "w", encoding="utf-8") as f:
        for x in found:
            f.write(json.dumps(x, ensure_ascii=False) + "\n")
    with open(md, "w", encoding="utf-8") as f:
        npass = sum(1 for x in found if x["verdict"] == "pass")
        nfail = sum(1 for x in found if x["verdict"] == "fail")
        nblk = sum(1 for x in found if x["verdict"] == "blocked")
        f.write(f"# Smoke findings — {session}\n\n{npass} pass / {nfail} fail / "
                f"{nblk} blocked-by-crash / {len(found)} traceable.\n\n")
        for x in found:
            f.write(f"## [{x['verdict'].upper()}] {x['id']} ({x['journey']}/{x['lane']})\n")
            f.write(f"- action: {x['action']}\n- ts: {x['ts']}  video: {x['video_offset']}\n")
            f.write(f"- reason: {x['reason']}\n")
            if x.get("gemini"):
                f.write(f"- gemini: {x['gemini']}\n")
            if x.get("log"):
                f.write(f"- log: {x['log']}\n")
            if x.get("note"):
                f.write(f"- note: {x['note']}\n")
            f.write("\n")
    print(f"findings -> {md}\n         {js}")
```

- [ ] **Step 1b: Add `fold_visual.py` (phase B — merge Gemini's clip descriptions)**

```python
# scripts/agents/smoke/fold_visual.py
"""Phase B: read persisted phase-A state + Gemini's pasted-back answers and
rewrite the findings with the visual descriptions folded in."""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import findings as findings_mod                       # noqa: E402
from visual_workorder import load_visual_answers      # noqa: E402
from run_smoke import collect_logs, write_report, REPO  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--session", required=True)
    ap.add_argument("--answers", default=None,
                    help="defaults to out/smoke_<session>_visual_answers.json")
    args = ap.parse_args()
    state_path = os.path.join(REPO, "out", f"smoke_{args.session}_state.json")
    with open(state_path, "r", encoding="utf-8") as f:
        st = json.load(f)
    answers_path = args.answers or os.path.join(
        REPO, "out", f"smoke_{args.session}_visual_answers.json")
    visual = load_visual_answers(answers_path)
    if not visual:
        print(f"no answers at {answers_path} — nothing to fold")
        return 1
    logs = collect_logs(st["marks"])  # app may be down; grep is best-effort
    found = findings_mod.build_findings(
        st["self_results"], st["marks"], visual, logs, st["session_start"])
    write_report(args.session, found)
    print(f"folded {len(visual)} visual answers into out/smoke_{args.session}_findings.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Write README.md**

```markdown
# Stream/Theatre Smoke Harness

Autonomous on-device smoke for Agent 4's domain. Two-lane oracle:
SELF (tankoctl + logs, decided by the runner) and VISUAL (ffmpeg + Gemini).

## Run (two phases — the visual lane is a manual courier to the Gemini LLM)
Phase A (autonomous): drive + record + SELF judging + clip the flagged windows.
1. Launch in drive mode **in-process / visible (never detached-hidden** — that
   silently kills ddagrab, per Agent 0's recorder fix `c6f59d2`):
   `scripts\agents\run_drive_mode.bat`.
2. `python scripts\agents\smoke\run_smoke.py --session <name> --only J1,J2`
   → writes `out\smoke_<name>_findings.md` (SELF) + `..._clips\*.mp4` + `..._visual_workorder.md`.

Phase B (visual, manual): courier the clips to the Gemini LLM.
3. (optional, cheap) speed a long clip: `python scripts\agents\screen_record.py speedup <clip> <out> 4`.
4. Upload the clips + work-order to the Gemini LLM; save its JSON reply to
   `out\smoke_<name>_visual_answers.json`.
5. `python scripts\agents\smoke\fold_visual.py --session <name>` → final findings,
   every one traceable (action → ts → video offset → gemini → log line).

No vision API / no API key. `--no-visual` skips clipping (SELF still runs fully).

## Scope: plumbing, not watching
Normal smokes are journey-driven and short (~20–30 min total). Playback is
**spot-checks** at a few positions (first frame painted, audio out, subs rendered,
a mid-seek re-paints, the end is reached) — seconds each, never a playthrough.
Whether the video *looks/feels good* is Hemanth's taste call, out of scope.

## Soak mode + speed levers (opt-in, not the default)
A separate occasional **soak run** catches time-dependent bugs (the idle-spin
crash that fires "minutes after open," memory growth, stutter-over-time): loop a
journey / hold playback for N minutes under the same recorder + watchdog. Only
soak uses Agent 0's speed levers (runbook §3c) — `tankoctl player-set-speed N` to
shrink a long hold, `screen_record.py speedup` to shrink the soak footage before
review. The default smoke plays through nothing, so there's nothing to speed up.

## Extend / reuse (Agents 1/2/3)
Author `catalogue_<domain>.json` (same shape as `catalogue_stream.json`) and run with
`--catalogue`. Runner/recording/visual/findings are domain-agnostic.

## Honest blind spot
During a hard freeze `introspect-*` is dead too (GUI-thread-bound). The harness
detects the freeze (watchdog + ping timeout + frozen frame) and reports it, but
the internal call-stack needs OBS-4 (out-of-process dump, not built) — flagged
`stack_available:false`, never faked.
```

- [ ] **Step 3: Smoke-compile both entry points**

Run: `python -c "import sys; sys.path.insert(0,'scripts/agents/smoke'); import run_smoke, fold_visual; print('ok')"`
Expected: `ok`.

- [ ] **Step 4: Commit**

```bash
git add scripts/agents/smoke/run_smoke.py scripts/agents/smoke/fold_visual.py scripts/agents/smoke/README.md
git commit -m "feat(smoke): run_smoke phase-A + fold_visual phase-B + README runbook"
```

---

## Task 10: catalogue_stream.json — the check catalogue (data)

Author the catalogue from the spec's §5 tables. Start with J1 + J2 (proof), then J3–J8. Verb names dash-form; reconfirm against live `tankoctl ping` while authoring.

**Files:**
- Create: `scripts/agents/smoke/catalogue_stream.json`

- [ ] **Step 1: Write J1 + J2 (the proof journeys)**

```json
{
  "journeys": [
    {
      "id": "J1", "name": "One Piece (anime full chain)",
      "steps": [
        {"id": "op.search", "action": ["search", "One Piece"], "asserts": [
          {"id": "op.search.returns", "lane": "SELF", "probe": ["search", "One Piece"],
           "path": "results[*].imdb", "expect": {"contains": "tt0388629"}, "timeoutSec": 15}
        ]},
        {"id": "op.dispatch", "action": ["dispatch-episode", "tt0388629", "1", "1133"], "asserts": [
          {"id": "op.dispatch.accepted", "lane": "SELF",
           "probe": ["dispatch-episode", "tt0388629", "1", "1133"],
           "path": "status", "expect": {"equals": "dispatched"}, "timeoutSec": 60,
           "on_fail": "continue"},
          {"id": "op.dispatch.amatsu_source", "lane": "SELF", "probe": ["get-downloads"],
           "path": "entries[*].canonicalPath", "expect": {"regex": "(?i)\\[(SubsPlease|Erai-raws|Judas|MA0MA0|ASW|Yameii|EMBER|Anime Time)\\]|MultiSub"},
           "timeoutSec": 30, "needs": ["op.dispatch.accepted"]}
        ]},
        {"id": "op.dl", "action": ["get-downloads"], "asserts": [
          {"id": "op.dl.starts", "lane": "SELF", "probe": ["get-downloads"],
           "path": "entries", "expect": {"nonempty": true}, "timeoutSec": 30}
        ]},
        {"id": "op.play", "action": ["play-file", "REPLACE_WITH_KNOWN_ONEPIECE_MKV"], "asserts": [
          {"id": "op.play.starts", "lane": "SELF", "probe": ["get-player"],
           "path": "snapshot.playing", "expect": {"equals": true}, "timeoutSec": 25},
          {"id": "op.play.sidecar_ok", "lane": "SELF", "probe": ["sidecar-get-process-state"],
           "path": "snapshot.running", "expect": {"equals": true}, "timeoutSec": 10,
           "needs": ["op.play.starts"]},
          {"id": "op.play.subs_present", "lane": "SELF", "probe": ["player-get-subtitle-tracks"],
           "path": "tracks", "expect": {"nonempty": true}, "timeoutSec": 10,
           "needs": ["op.play.starts"]},
          {"id": "op.play.video_paints", "lane": "VISUAL",
           "gemini_prompt": "Is real video playing (moving frames), or is the screen black/frozen? One sentence.",
           "needs": ["op.play.starts"]},
          {"id": "op.play.subs_on_screen", "lane": "VISUAL",
           "gemini_prompt": "Is there English subtitle text visible over the video? One sentence.",
           "needs": ["op.play.starts"]},
          {"id": "op.play.audio_present", "lane": "VISUAL",
           "gemini_prompt": "Does the recording's audio track carry sound during this window? One sentence.",
           "needs": ["op.play.starts"]}
        ]},
        {"id": "op.seek", "action": ["player-seek", "600"], "asserts": [
          {"id": "op.play.seek_lands", "lane": "SELF", "probe": ["get-player"],
           "path": "snapshot.positionSec", "expect": {"approx": 600, "tolerance": 8},
           "timeoutSec": 12, "needs": ["op.play.starts"]},
          {"id": "op.play.seek_repaints", "lane": "VISUAL",
           "gemini_prompt": "Spot-check: right after the seek, does the frame RE-PAINT to a new image, or is it frozen on the old frame? One sentence.",
           "needs": ["op.play.starts"]}
        ]}
      ]
    },
    {
      "id": "J2", "name": "Western series (Torrentio full chain)",
      "steps": [
        {"id": "w.search", "action": ["search", "REPLACE_WITH_WESTERN_SHOW"], "asserts": [
          {"id": "w.search.returns", "lane": "SELF", "probe": ["search", "REPLACE_WITH_WESTERN_SHOW"],
           "path": "results", "expect": {"nonempty": true}, "timeoutSec": 15}
        ]},
        {"id": "w.dispatch", "action": ["dispatch-episode", "REPLACE_WITH_IMDB", "1", "1"], "asserts": [
          {"id": "w.dispatch.accepted", "lane": "SELF",
           "probe": ["dispatch-episode", "REPLACE_WITH_IMDB", "1", "1"],
           "path": "status", "expect": {"equals": "dispatched"}, "timeoutSec": 60}
        ]},
        {"id": "w.dl", "action": ["get-downloads"], "asserts": [
          {"id": "w.dl.starts", "lane": "SELF", "probe": ["get-downloads"],
           "path": "entries", "expect": {"nonempty": true}, "timeoutSec": 30}
        ]},
        {"id": "w.play", "action": ["play-file", "REPLACE_WITH_KNOWN_WESTERN_MKV"], "asserts": [
          {"id": "w.play.starts", "lane": "SELF", "probe": ["get-player"],
           "path": "snapshot.playing", "expect": {"equals": true}, "timeoutSec": 25},
          {"id": "w.play.video_paints", "lane": "VISUAL",
           "gemini_prompt": "Is real video playing, or black/frozen? One sentence.",
           "needs": ["w.play.starts"]}
        ]}
      ]
    }
  ]
}
```

- [ ] **Step 2: Validate the catalogue loads**

Run: `python -c "import sys; sys.path.insert(0,'scripts/agents/smoke'); import catalogue; print(len(catalogue.load('scripts/agents/smoke/catalogue_stream.json').journeys),'journeys')"`
Expected: `2 journeys` (after J3–J8 added in Step 3: `8 journeys`).

- [ ] **Step 3: Append J3–J8**

Add journeys J3 (addons), J4 (episode-state tiles via `introspect-object EpisodeTile`), J5 (`dispatch-season`), J6 (player controls), J7 (sidecar health), J8 (Tankorent page) following the same row shape, transcribing the spec §5 tables. Each SELF row: `{id, lane:"SELF", probe:[...], path, expect, timeoutSec}`; each VISUAL row: `{id, lane:"VISUAL", gemini_prompt}`. Re-run the Step 2 validation; expect `8 journeys`.

- [ ] **Step 4: Commit**

```bash
git add scripts/agents/smoke/catalogue_stream.json
git commit -m "feat(smoke): Stream/Theatre check catalogue J1-J8 (data)"
```

---

## Task 11: First proof run — J1 + J2, one traceable finding

Integration milestone (no unit test — the real on-device run). Prereq: Task 0 passed.

- [ ] **Step 0: Claim the desktop lane + confirm recorder gate**

A long run holds the desktop (Rules 19/22): `out\tankoctl.exe lease-acquire desktop agent4 1800` (release at end). Confirm Task 0 passed (recording + clip both worked).

- [ ] **Step 1: Resolve the catalogue placeholders against live state**

Launch the app **in-process / visible** (never detached): `scripts\agents\run_drive_mode.bat`; wait for `out\tankoctl.exe ping`.
- Replace `REPLACE_WITH_KNOWN_ONEPIECE_MKV` with a real path from `get-downloads` (an existing One Piece file under `Media/TV`).
- Pick a Western show with sources: `search "<show>"` → set `REPLACE_WITH_WESTERN_SHOW`, `REPLACE_WITH_IMDB`, and a known Western `.mkv` for `REPLACE_WITH_KNOWN_WESTERN_MKV`.
- Reconfirm every verb/path against `tankoctl ping` + a manual `get-player` / `sidecar-get-process-state` reply shape (fix `path`s if the JSON keys differ).
- (optional) add a `["player-set-speed","4"]` step before playback to shrink wall-clock; keep VISUAL smoothness windows at 1×.
Commit the resolved catalogue: `git add scripts/agents/smoke/catalogue_stream.json && git commit -m "chore(smoke): resolve catalogue placeholders to live state"`.

- [ ] **Step 2: Run phase A (drive + record + SELF + clips)**

Run: `python scripts\agents\smoke\run_smoke.py --session op-proof --only J1,J2`
Expected: per-assert pass/fail/blocked printed; `out\smoke_op-proof_findings.md` (SELF) + `out\smoke_op-proof.mp4` + `out\smoke_op-proof_clips\*.mp4` + `..._visual_workorder.md` written.

- [ ] **Step 3: Phase B — courier clips to Gemini, fold answers**

Hand the clips + `smoke_op-proof_visual_workorder.md` to Hemanth to courier to the Gemini LLM (optionally speed long clips: `screen_record.py speedup`). Save the returned JSON to `out\smoke_op-proof_visual_answers.json`, then: `python scripts\agents\smoke\fold_visual.py --session op-proof`.

- [ ] **Step 4: Verify the chain on ONE finding**

Open `out\smoke_op-proof_findings.md`. Confirm at least one finding carries all five parts: action, ts, video_offset, gemini description, log line. Open the clip `out\smoke_op-proof_clips\<id>.mp4` and confirm it matches the gemini description. (If the idle-spin crash fires, a `blocked` finding flagged `needs OBS-4` is itself a valid first traceable finding — Agent 0 review flag 3.)

- [ ] **Step 5: Stop the app + release the lane (Rules 17/19)**

Run: `taskkill /F /IM Tankoban.exe` (check `tasklist` for a stray relaunch); `out\tankoctl.exe lease-release desktop agent4`.

- [ ] **Step 6: Show Hemanth + post RTC**

Surface the one fully-traceable finding to Hemanth in hemanth-language. Post a contracts-v3 RTC for the harness. Then (only after Hemanth's go) scale to long runs (all J1–J8), using `player-set-speed` + footage speedup to keep them cheap.

---

## Self-Review

**Spec coverage:** two-lane oracle (oracle.py + describe_visual.py ✓); log-mark correlation (runner `_mark` + marks.py + findings.py ✓); runner-owned continuous recording (recording.py ✓); catalogue as data (catalogue.py + catalogue_stream.json ✓); cost lever (describe_visual.select_deep_windows ✓); traceable findings (findings.py + run_smoke md/jsonl ✓); recovery (runner.relaunch ✓); blind spot flagged not faked (runner death/hang → `needs OBS-4` ✓); first proof = J1+J2 (Task 11 ✓); reusability (catalogue_<domain>.json + README ✓).

**Placeholder scan:** the only `REPLACE_WITH_*` tokens are in the catalogue and are explicitly resolved against live state in Task 11 Step 1 (not code placeholders).

**Type consistency:** `evaluate(expect, value)`, `extract(reply, path)`/`MISSING`, `parse_marks(events)→{id:{start,end,verdict}}`, `build_findings(self_results, marks, visual, logs, session_start)`, `select_deep_windows(self_results, visual_ids, watchdog_ids)`, `wallclock_to_offset(start, ts)`, `SessionRecording.start()/.stop()/.path/.start_ts` — all consistent across runner.py and run_smoke.py call sites.
