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
