# scripts/agents/smoke/findings.py
"""Pure-logic correlator: join SELF/VISUAL results + marks + log lines into
traceable findings. A finding with no mark window is dropped (not done)."""
from datetime import datetime


def _iso(ts):
    # ts may carry fractional seconds + trailing Z (marker line ts) or neither.
    t = ts.replace("Z", "").split(".")[0]
    return datetime.strptime(t, "%Y-%m-%dT%H:%M:%S")


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
