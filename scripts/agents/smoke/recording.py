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
