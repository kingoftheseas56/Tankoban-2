#!/usr/bin/env python3
"""
drive_journal.py - Agent-driven app control with an automatic ACTION -> EFFECT journal.

This is the "logs meticulously track everything the app does" piece. It wraps each
tankoctl action so every step is bracketed with `log-mark` markers (across all four
app log streams) and the resulting app events are captured into a human-readable
journal: "did X -> app did Y".

Distinct from `tankoctl record` (which captures COMMANDS for replay) - this captures
the app's RESPONSE to each command, building a self-writing manual of app behaviour.

USAGE - single action from the shell:
  python scripts/agents/drive_journal.py --label "open Theatre" -- open-page stream
  python scripts/agents/drive_journal.py --label "search One Piece" --probe get-torrents -- search "One Piece"

USAGE - a sequence, imported (e.g. Agent 4 driving the One Piece flow):
  from drive_journal import Driver
  d = Driver(session="onepiece-ep1164")
  d.do("open Theatre",      ["open-page", "stream"])
  d.do("search One Piece",  ["search", "One Piece"],            probe=["get-torrents"])
  d.do("download ep 1164",  ["dispatch-episode", "<id>", "1164"], probe=["get-downloads"])
  d.close()

REQUIRES: Tankoban running with --dev-control (build_and_run.bat). For the log-mark
markers, also TANKOBAN_DEV_WRITE=1; for literal ui-* clicks, TANKOBAN_DEV_UI_SIM=1.
The journal still works without the write gate - markers degrade to best-effort and
the events.jsonl delta carries the effect.

OUTPUTS (append):
  out/agent_drive_journal.md     - human-readable action->effect log
  out/agent_drive_journal.jsonl  - one structured object per action
"""
import json
import os
import subprocess
import sys
import time
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
TANKOCTL = os.path.join(REPO, "out", "tankoctl.exe")
EVENTS = os.path.join(REPO, "out", "events.jsonl")
JOURNAL_MD = os.path.join(REPO, "out", "agent_drive_journal.md")
JOURNAL_JSONL = os.path.join(REPO, "out", "agent_drive_journal.jsonl")

sys.path.insert(0, HERE)
try:
    from screen_record import ScreenRecorder
except Exception:
    ScreenRecorder = None


def _now():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def tankoctl(args, timeout=15):
    """Run tankoctl <args>; return (ok, parsed_reply_or_text). DLLs resolve from out/."""
    if not os.path.exists(TANKOCTL):
        return False, {"error": "tankoctl.exe not found - run build_and_run.bat first"}
    try:
        proc = subprocess.run(
            [TANKOCTL] + [str(a) for a in args],
            capture_output=True, text=True, timeout=timeout, cwd=os.path.join(REPO, "out"),
        )
    except subprocess.TimeoutExpired:
        return False, {"error": "tankoctl timed out (is the app frozen?)"}
    out = (proc.stdout or "").strip()
    try:
        reply = json.loads(out.splitlines()[-1]) if out else {}
    except (ValueError, IndexError):
        reply = {"raw": out, "stderr": (proc.stderr or "").strip()}
    ok = proc.returncode == 0 and not (isinstance(reply, dict) and reply.get("type") == "error")
    return ok, reply


def _events_offset():
    try:
        return os.path.getsize(EVENTS)
    except OSError:
        return 0


def _events_since(offset):
    """Return the event objects appended to events.jsonl after `offset` bytes."""
    try:
        with open(EVENTS, "r", encoding="utf-8", errors="replace") as f:
            f.seek(offset)
            rows = []
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    rows.append(json.loads(line))
                except ValueError:
                    rows.append({"raw": line})
            return rows
    except OSError:
        return []


class Driver:
    def __init__(self, session="drive", record=False, record_fps=15):
        self.session = session
        self.n = 0
        self.rec = None
        with open(JOURNAL_MD, "a", encoding="utf-8") as f:
            f.write(f"\n\n## Drive session `{session}` - started {_now()}\n")
        if record and ScreenRecorder:
            mp4 = os.path.join(REPO, "out", f"agent_drive_{session}.mp4")
            self.rec = ScreenRecorder(mp4, fps=record_fps)
            ok = self.rec.start()
            with open(JOURNAL_MD, "a", encoding="utf-8") as f:
                f.write(f"- recording: {self.rec.active_mode + ' -> ' + mp4 if ok else 'FAILED to start'}\n")
            if ok:
                print(f"  recording ({self.rec.active_mode}) -> {mp4}")
            else:
                self.rec = None
        elif record and not ScreenRecorder:
            print("  (screen_record unavailable; journaling without video)")

    def do(self, label, action_args, probe=None, settle=1.2):
        """Journal one action: mark -> snapshot -> act -> settle -> capture effect."""
        self.n += 1
        tag = f"{self.session}#{self.n} {label}"
        tankoctl(["log-mark", f"BEGIN {tag}"])          # best-effort (needs DEV_WRITE)
        state_before = tankoctl(probe)[1] if probe else None
        off = _events_offset()
        ok, reply = tankoctl(action_args)
        time.sleep(settle)
        tankoctl(["log-mark", f"END {tag}"])
        events = _events_since(off)
        state_after = tankoctl(probe)[1] if probe else None

        entry = {
            "ts": _now(), "session": self.session, "step": self.n, "label": label,
            "action": action_args, "ok": ok, "reply": reply,
            "events": events, "state_before": state_before, "state_after": state_after,
        }
        with open(JOURNAL_JSONL, "a", encoding="utf-8") as f:
            f.write(json.dumps(entry, ensure_ascii=False) + "\n")
        self._write_md(entry)
        kinds = ", ".join(sorted({e.get("kind", "?") for e in events})) or "(none)"
        print(f"  [{self.n}] {label}: {'OK' if ok else 'ERR'} -> {len(events)} events ({kinds})")
        return entry

    def _write_md(self, e):
        with open(JOURNAL_MD, "a", encoding="utf-8") as f:
            f.write(f"\n### {e['ts']} - step {e['step']}: {e['label']}\n")
            f.write(f"- **did:** `tankoctl {' '.join(map(str, e['action']))}` -> "
                    f"{'OK' if e['ok'] else 'ERR'}\n")
            if e["events"]:
                f.write(f"- **app did:** {len(e['events'])} event(s):\n")
                for ev in e["events"][:12]:
                    msg = ev.get("msg") or ev.get("type") or json.dumps(ev)[:120]
                    f.write(f"    - `{ev.get('kind', '?')}` {msg}\n")
            else:
                f.write("- **app did:** (no new events captured)\n")
            if e["state_before"] is not None:
                f.write(f"- **state probe:** before -> after recorded in jsonl\n")

    def close(self):
        vid = self.rec.stop() if self.rec else None
        with open(JOURNAL_MD, "a", encoding="utf-8") as f:
            if vid:
                f.write(f"\n_video: {vid['path']} ({vid['bytes']} bytes, {vid['mode']})_\n")
            f.write(f"\n_session `{self.session}` ended {_now()} - {self.n} actions._\n")
        if vid:
            print(f"video: {vid['path']} ({vid['bytes']} bytes, {vid['mode']})")
        print(f"journal: {JOURNAL_MD}")


def _main(argv):
    label, probe, action, record = "action", None, None, False
    if "--" not in argv:
        print("usage: drive_journal.py --label \"...\" [--probe <get-cmd>] -- <tankoctl args>")
        return 2
    head, action = argv[:argv.index("--")], argv[argv.index("--") + 1:]
    i = 0
    while i < len(head):
        if head[i] == "--label":
            label = head[i + 1]; i += 2
        elif head[i] == "--probe":
            probe = [head[i + 1]]; i += 2
        elif head[i] == "--record":
            record = True; i += 1
        else:
            i += 1
    if not action:
        print("error: no tankoctl action after --")
        return 2
    d = Driver(session="cli", record=record)
    d.do(label, action, probe=probe)
    d.close()
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
