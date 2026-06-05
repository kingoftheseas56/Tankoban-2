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
