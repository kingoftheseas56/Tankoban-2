#!/usr/bin/env python3
"""screen_record.py - programmatic screen recorder for agent-driven sessions (v2).

Records the desktop to a CRASH-RESILIENT MP4 via ffmpeg: **fragmented mp4** so a
partial/killed recording is still playable - critical for long autonomous smokes
that may crash mid-run (the player idle-spin crash WILL hit). ddagrab (captures
real video playback, not a black overlay) with gdigrab fallback.

=== WHY THE TRIAL RECORDER FAILED (read this) ===
ddagrab (DXGI Desktop Duplication) needs the ACTIVE DESKTOP SESSION. A *detached,
window-hidden* ffmpeg (Start-Process -WindowStyle Hidden) loses that session and
ddagrab silently dies -> zero-byte / no file. That was the bug. The rules:
  - LONG HARNESS: use ScreenRecorder IN-PROCESS. The harness is the long-running
    session-attached parent; start the recorder at session begin, stop at end.
    (This path is validated and produces a complete, playable file.)
  - CROSS-PROCESS start/stop: use `daemon-start`/`daemon-stop`, which launches
    ffmpeg in its OWN console (session-attached, survives the launcher) and stops
    it via taskkill (fragmented mp4 stays playable). NEVER launch it window-hidden.

=== SPEEDUP (any recording, any factor) ===
Speed a recording up Nx for cheap Gemini description: a 2-hour smoke -> a 15-min
clip the visual lane can describe at a fraction of the cost/time.
  screen_record.py speedup <in.mp4> <out.mp4> 8        # 8x, audio pitch-preserved
  screen_record.py speedup <in.mp4> <out.mp4> 8 --mute # 8x, no audio

CLI:
  screen_record.py test [seconds]
  screen_record.py daemon-start <out.mp4>
  screen_record.py daemon-stop
  screen_record.py speedup <in.mp4> <out.mp4> <factor> [--mute]

Import (harness):
  from screen_record import ScreenRecorder
  r = ScreenRecorder("out/session.mp4"); r.start(); ...drive+journal...; r.stop()
"""
import os
import shutil
import subprocess
import sys
import time

FFMPEG = r"C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin\ffmpeg.exe"
if not os.path.exists(FFMPEG):
    FFMPEG = shutil.which("ffmpeg") or FFMPEG

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
PIDFILE = os.path.join(REPO, "out", "_screenrec.pid")

# crash-resilient encode: fragmented mp4 = playable even if ffmpeg is killed.
_ENC = ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p",
        "-movflags", "+frag_keyframe+empty_moov+default_base_moof"]


def _src(mode, fps):
    if mode == "ddagrab":
        return ["-f", "lavfi", "-i", f"ddagrab=framerate={fps}", "-vf", "hwdownload,format=bgra"]
    return ["-f", "gdigrab", "-framerate", str(fps), "-i", "desktop"]


class ScreenRecorder:
    """In-process recorder. Session-attached via the parent process. ddagrab->gdigrab."""

    def __init__(self, out_mp4, fps=15, mode="auto"):
        self.out = os.path.abspath(out_mp4)
        self.fps = fps
        self.mode = mode
        self.active_mode = None
        self.proc = None

    def _cmd(self, mode):
        return [FFMPEG, "-hide_banner", "-loglevel", "error", "-y"] + _src(mode, self.fps) + _ENC + [self.out]

    def start(self):
        d = os.path.dirname(self.out)
        if d:
            os.makedirs(d, exist_ok=True)
        for m in (["ddagrab", "gdigrab"] if self.mode == "auto" else [self.mode]):
            self.proc = subprocess.Popen(self._cmd(m), stdin=subprocess.PIPE,
                                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            time.sleep(1.3)
            if self.proc.poll() is None:
                self.active_mode = m
                return True
        self.proc = None
        return False

    def stop(self, timeout=15):
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


def _atempo_chain(factor):
    parts, f = [], float(factor)
    while f > 2.0:
        parts.append("atempo=2.0"); f /= 2.0
    while f < 0.5:
        parts.append("atempo=0.5"); f /= 0.5
    parts.append(f"atempo={f:.4f}")
    return ",".join(parts)


def speedup(src, dst, factor, mute=False):
    """Speed a recording up Nx (audio pitch-preserved unless --mute). For cheap Gemini description."""
    factor = float(factor)
    cmd = [FFMPEG, "-hide_banner", "-loglevel", "error", "-y", "-i", src,
           "-filter:v", f"setpts=PTS/{factor}"]
    cmd += ["-an"] if mute else ["-filter:a", _atempo_chain(factor)]
    cmd += ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p", dst]
    subprocess.run(cmd, check=False)
    return os.path.getsize(dst) if os.path.exists(dst) else 0


def daemon_start(out_mp4, fps=15):
    """Cross-process recorder: ffmpeg in its OWN console (session-attached, survives us)."""
    out = os.path.abspath(out_mp4)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    cmd = [FFMPEG, "-hide_banner", "-loglevel", "error", "-y"] + _src("ddagrab", fps) + _ENC + [out]
    flags = getattr(subprocess, "CREATE_NEW_CONSOLE", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    p = subprocess.Popen(cmd, creationflags=flags)
    time.sleep(1.5)
    if p.poll() is not None:  # ddagrab died -> fall back to gdigrab
        cmd = [FFMPEG, "-hide_banner", "-loglevel", "error", "-y"] + _src("gdigrab", fps) + _ENC + [out]
        p = subprocess.Popen(cmd, creationflags=flags)
        time.sleep(1.5)
    with open(PIDFILE, "w") as f:
        f.write(str(p.pid))
    return p.pid if p.poll() is None else 0


def daemon_stop():
    if not os.path.exists(PIDFILE):
        return False
    pid = open(PIDFILE).read().strip()
    subprocess.run(["taskkill", "/PID", pid, "/T", "/F"], capture_output=True)
    os.remove(PIDFILE)
    return True


def _main(argv):
    if not argv:
        print(__doc__.splitlines()[0]); return 2
    cmd = argv[0]
    if cmd == "test":
        secs = int(argv[1]) if len(argv) > 1 else 6
        out = os.path.join(REPO, "out", "_rec_selftest.mp4")
        r = ScreenRecorder(out)
        print(f"recording {secs}s ...")
        if not r.start():
            print("FAILED to start"); return 1
        time.sleep(secs)
        info = r.stop()
        print(f"mode={info['mode']} bytes={info['bytes']} -> {info['path']}")
        return 0 if info["bytes"] > 0 else 1
    if cmd == "daemon-start" and len(argv) >= 2:
        pid = daemon_start(argv[1])
        print(f"daemon pid={pid}" if pid else "daemon FAILED to start"); return 0 if pid else 1
    if cmd == "daemon-stop":
        print("stopped" if daemon_stop() else "no daemon running"); return 0
    if cmd == "speedup" and len(argv) >= 4:
        mute = "--mute" in argv
        n = speedup(argv[1], argv[2], argv[3], mute=mute)
        print(f"sped up {argv[3]}x -> {argv[2]} ({n} bytes)"); return 0 if n else 1
    print("bad args"); return 2


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
