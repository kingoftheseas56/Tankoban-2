#!/usr/bin/env python3
"""
screen_record.py - programmatic screen recorder for agent-driven sessions.

Records the desktop to an MP4 via ffmpeg, started/stopped by an agent AROUND a
drive session (no GUI, no hotkeys). Prefers ddagrab (GPU desktop-duplication -
captures actually-playing video correctly, e.g. Theatre playback) and falls back
to gdigrab (simple GDI grab) if ddagrab can't init.

Pairs with drive_journal.py: a session yields an MP4 + a time-aligned
action->effect log of the same run. Feed the MP4 to Gemini for narration.

CLI:
  python screen_record.py test [seconds]        # record N s (default 4), verify
  python screen_record.py record <out.mp4> <seconds>

Import:
  from screen_record import ScreenRecorder
  r = ScreenRecorder("out/session.mp4"); r.start()
  ...drive...
  r.stop()
"""
import os
import shutil
import subprocess
import sys
import time

FFMPEG = r"C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin\ffmpeg.exe"
if not os.path.exists(FFMPEG):
    FFMPEG = shutil.which("ffmpeg") or FFMPEG


class ScreenRecorder:
    """Start/stop a desktop MP4 recording. ddagrab preferred, gdigrab fallback."""

    def __init__(self, out_mp4, fps=15, mode="auto"):
        self.out = os.path.abspath(out_mp4)
        self.fps = fps
        self.mode = mode          # "auto" | "ddagrab" | "gdigrab"
        self.active_mode = None
        self.proc = None

    def _cmd(self, mode):
        base = [FFMPEG, "-hide_banner", "-loglevel", "error", "-y"]
        if mode == "ddagrab":
            src = ["-f", "lavfi", "-i", f"ddagrab=framerate={self.fps}",
                   "-vf", "hwdownload,format=bgra"]
        else:  # gdigrab
            src = ["-f", "gdigrab", "-framerate", str(self.fps), "-i", "desktop"]
        enc = ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p", self.out]
        return base + src + enc

    def start(self):
        """Launch ffmpeg; if the chosen mode dies on init, fall through to next."""
        d = os.path.dirname(self.out)
        if d:
            os.makedirs(d, exist_ok=True)
        modes = ["ddagrab", "gdigrab"] if self.mode == "auto" else [self.mode]
        for m in modes:
            self.proc = subprocess.Popen(
                self._cmd(m), stdin=subprocess.PIPE,
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            )
            time.sleep(1.3)            # let it init / fail fast
            if self.proc.poll() is None:
                self.active_mode = m
                return True
        self.proc = None
        return False

    def stop(self, timeout=12):
        """Send 'q' to ffmpeg so the MP4 finalizes cleanly (moov atom written)."""
        if not self.proc:
            return None
        if self.proc.poll() is None:
            try:
                self.proc.communicate(input=b"q", timeout=timeout)
            except subprocess.TimeoutExpired:
                self.proc.terminate()
                try:
                    self.proc.wait(3)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
        self.proc = None
        size = os.path.getsize(self.out) if os.path.exists(self.out) else 0
        return {"path": self.out, "mode": self.active_mode, "bytes": size}


def _main(argv):
    if not argv:
        print("usage: screen_record.py test [seconds] | record <out.mp4> <seconds>")
        return 2
    if argv[0] == "test":
        secs = int(argv[1]) if len(argv) > 1 else 4
        out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "..", "..", "out", "_rec_selftest.mp4")
        r = ScreenRecorder(out)
        print(f"recording {secs}s ...")
        if not r.start():
            print("FAILED to start recorder")
            return 1
        time.sleep(secs)
        info = r.stop()
        print(f"mode={info['mode']} bytes={info['bytes']} -> {info['path']}")
        return 0 if info["bytes"] > 0 else 1
    if argv[0] == "record" and len(argv) >= 3:
        r = ScreenRecorder(argv[1])
        if not r.start():
            print("FAILED")
            return 1
        time.sleep(int(argv[2]))
        print(r.stop())
        return 0
    print("bad args")
    return 2


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
