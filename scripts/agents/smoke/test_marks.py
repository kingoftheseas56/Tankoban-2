# scripts/agents/smoke/test_marks.py
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from marks import parse_marks

# Real format: log-mark writes a raw delimiter line into events.jsonl, wrapped by
# drive_journal._events_since as {"raw": "<line>"}. ts is embedded in the line.
EVENTS = [
    {"raw": "=== MARK: SMOKE op.play.starts START @ 2026-06-05T10:00:00.100Z ==="},
    {"raw": "=== MARK: SMOKE op.play.starts END pass @ 2026-06-05T10:00:09.200Z ==="},
    {"raw": "=== MARK: SMOKE op.play.seek START @ 2026-06-05T10:00:10.000Z ==="},
    {"raw": "=== MARK: SMOKE op.play.seek END fail @ 2026-06-05T10:00:14.000Z ==="},
    {"raw": "=== MARK: SMOKE op.search ACTION START @ 2026-06-05T10:00:15.000Z ==="},
    {"raw": "unrelated noise line"},
]

def test_pairs_start_end():
    m = parse_marks(EVENTS)
    assert m["op.play.starts"]["start"] == "2026-06-05T10:00:00.100Z"
    assert m["op.play.starts"]["end"] == "2026-06-05T10:00:09.200Z"
    assert m["op.play.starts"]["verdict"] == "pass"
    assert m["op.play.seek"]["verdict"] == "fail"

def test_unterminated_start_has_no_end():
    m = parse_marks(EVENTS[:1])
    assert m["op.play.starts"]["end"] is None

def test_action_marks_ignored():
    m = parse_marks(EVENTS)
    assert "op.search" not in m  # "SMOKE op.search ACTION START" is not a START/END

def test_also_reads_qt_message_form():
    m = parse_marks([{"data": {"message": "SMOKE x.y START @ 2026-06-05T10:00:00Z"}}])
    assert m["x.y"]["start"] == "2026-06-05T10:00:00Z"
