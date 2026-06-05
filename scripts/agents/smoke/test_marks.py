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
