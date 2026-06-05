# scripts/agents/smoke/marks.py
"""Pure-logic parser: extract SMOKE log-mark windows from events.jsonl rows.

`tankoctl log-mark "SMOKE <id> START"` writes a RAW delimiter line into each log
stream, not a JSON event:
    === MARK: SMOKE <assert-id> START @ <iso-ts> ===
    === MARK: SMOKE <assert-id> END <verdict> @ <iso-ts> ===
events.jsonl rows reach us either parsed (dict) or, for these raw lines, wrapped
as {"raw": "<line>"} by drive_journal._events_since. The timestamp is embedded in
the marker line itself (UTC, may carry millis) — not a JSON `ts` field."""
import re

_RE = re.compile(r"SMOKE\s+(\S+)\s+(START|END)(?:\s+(\w+))?\s*@\s*(\S+)")


def _line(ev):
    if isinstance(ev, str):
        return ev
    d = ev.get("data") or {}
    return d.get("message") or ev.get("message") or ev.get("raw") or ""


def parse_marks(events):
    """Return {assert_id: {start, end, verdict}} (ts strings parsed from the
    marker line). ACTION marks (SMOKE <step> ACTION ...) don't match and are
    ignored."""
    marks = {}
    for ev in events:
        m = _RE.search(_line(ev))
        if not m:
            continue
        aid, kind, verdict, ts = m.group(1), m.group(2), m.group(3), m.group(4)
        slot = marks.setdefault(aid, {"start": None, "end": None, "verdict": None})
        if kind == "START":
            slot["start"] = ts
        else:
            slot["end"] = ts
            slot["verdict"] = verdict
    return marks
