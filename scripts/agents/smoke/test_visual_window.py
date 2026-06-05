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
