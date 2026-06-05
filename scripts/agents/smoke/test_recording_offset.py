# scripts/agents/smoke/test_recording_offset.py
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recording import wallclock_to_offset

def test_offset_basic():
    assert wallclock_to_offset("2026-06-05T10:00:00Z", "2026-06-05T10:02:05Z") == "00:02:05"

def test_offset_negative_is_zero():
    assert wallclock_to_offset("2026-06-05T10:00:10Z", "2026-06-05T10:00:00Z") == "00:00:00"
