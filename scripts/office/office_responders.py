#!/usr/bin/env python3
"""The Office — responder SUPERVISOR: arm the fallback backup net for several
Claude brothers at once, in ONE window.

Spawns one office_responder.py per brother (live by default), posts an
'armed'/'stood down' activity line into the room so the net is VISIBLE, tracks
child PIDs in a file (so a prior hard-closed run can always be cleaned up), and
tears every child down cleanly on Ctrl+C / stand-down.

Cost note: a responder PROCESS is free while watching — it only spends a
claude -p call when a brother is genuinely unreachable for ~60s and a message
named him. The cascade-guard (arc=auto_reply) keeps that self-limiting.

Usage:
  python office_responders.py                 # mainline 1-5, LIVE
  python office_responders.py 1 2 4           # just those, LIVE
  python office_responders.py --dry-run       # watch + log only, post nothing (safe smoke)
  python office_responders.py --stop          # stand down whatever a prior run armed
"""
import os
import sys
import time
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import office_bus  # noqa: E402

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

DEFAULT_NUMS = ["1", "2", "3", "4", "5"]   # mainline Claude brothers


def _pid_file():
    return os.path.join(office_bus._dir(), ".office_responder_pids")


def _read_pids():
    try:
        with open(_pid_file(), "r", encoding="utf-8") as f:
            return [int(x) for x in f.read().split() if x.strip().isdigit()]
    except (OSError, ValueError):
        return []


def _write_pids(pids):
    p = _pid_file()
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w", encoding="utf-8") as f:
        f.write(" ".join(str(x) for x in pids))


def _clear_pid_file():
    try:
        os.remove(_pid_file())
    except OSError:
        pass


def _kill(pid):
    """Best-effort terminate a single PID, cross-platform."""
    try:
        if os.name == "nt":
            subprocess.run(["taskkill", "/F", "/PID", str(pid)],
                           capture_output=True, check=False)
        else:
            import signal
            os.kill(pid, signal.SIGTERM)
    except Exception:
        pass


def stop():
    """Kill whatever a prior arm left running (idempotent — safe to run twice)."""
    pids = _read_pids()
    for pid in pids:
        _kill(pid)
    _clear_pid_file()
    if pids:
        office_bus.cmd_append("system", "all", "activity", "null",
                              "[responder] backup net stood down")
    print("[supervisor] stood down %d responder(s)." % len(pids))


def arm(nums, dry_run):
    agents = ["agent" + n for n in nums]
    stop()  # clean any orphans from a prior hard-close before arming fresh
    children = []   # (agent, Popen, logfile)
    for ag in agents:
        cmd = [sys.executable, os.path.join(HERE, "office_responder.py"), ag, "--no-lock"]
        if not dry_run:
            cmd.append("--live")
        log = open(os.path.join(HERE, "responder_%s.log" % ag), "a", encoding="utf-8")
        proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)
        children.append((ag, proc, log))
        print("[supervisor] armed %s (pid %d)%s" % (ag, proc.pid, " [dry-run]" if dry_run else ""))
    _write_pids([p.pid for _, p, _ in children])
    mode = "dry-run, no posting" if dry_run else "live"
    office_bus.cmd_append("system", "all", "activity", "null",
                          "[responder] backup net armed (%s): %s" % (mode, ", ".join(agents)))
    print("[supervisor] net armed: %s — Ctrl+C (or run stop_office_responders.bat) to stand down."
          % ", ".join(agents))
    try:
        while True:
            time.sleep(2)
            for ag, proc, _ in children:
                if proc.poll() is not None:
                    print("[supervisor] WARNING: responder %s exited (code %s)" % (ag, proc.returncode))
            children = [(ag, p, lg) for (ag, p, lg) in children if p.poll() is None]
            if not children:
                print("[supervisor] all responders exited — standing down.")
                break
    except KeyboardInterrupt:
        print("\n[supervisor] standing down...")
    finally:
        for ag, proc, lg in children:
            try:
                proc.terminate()
            except Exception:
                pass
            try:
                lg.close()
            except Exception:
                pass
        # Only post 'stood down' if an external --stop didn't already (it removes
        # the pid file) — avoids a double activity line in the room.
        if os.path.exists(_pid_file()):
            _clear_pid_file()
            office_bus.cmd_append("system", "all", "activity", "null",
                                  "[responder] backup net stood down")
            print("[supervisor] all responders stopped.")
        else:
            print("[supervisor] already stood down externally.")


def main(argv):
    if "--stop" in argv:
        stop()
        return
    dry_run = "--dry-run" in argv
    nums = [a for a in argv if a.isdigit()] or DEFAULT_NUMS
    arm(nums, dry_run)


if __name__ == "__main__":
    main(sys.argv[1:])
