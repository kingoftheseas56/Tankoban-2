# scripts/agents/smoke/runner.py
"""Drive the live app through a catalogue, evaluate SELF assertions, recover from
death/freeze. Emits SELF results (VISUAL handled post-session by visual_workorder).

Per assertion: log-mark START -> (run the step action once per step) -> poll the
probe until expect passes or timeout -> log-mark END pass|fail."""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))  # scripts/agents -> drive_journal
from drive_journal import tankoctl                  # noqa: E402
from oracle import evaluate                          # noqa: E402
from probes import extract, MISSING                  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
HANG = os.path.join(REPO, "out", "HANG_DETECTED.json")
RUN_DRIVE = os.path.join(REPO, "scripts", "agents", "run_drive_mode.bat")


def _mark(label):
    tankoctl(["log-mark", f"SMOKE {label}"])


def _alive():
    ok, _ = tankoctl(["ping"], timeout=8)
    return ok


def _hang_present(since_size):
    try:
        return os.path.getsize(HANG) != since_size and os.path.exists(HANG)
    except OSError:
        return False


def relaunch():
    """Kill stray instances (Rule 1 — known relaunch loop) and rebuild+launch."""
    subprocess.run(["taskkill", "/F", "/IM", "Tankoban.exe"],
                   capture_output=True)
    time.sleep(2)
    subprocess.Popen(["cmd", "/c", RUN_DRIVE],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(60):           # wait up to ~60s for the bridge
        time.sleep(1)
        if _alive():
            return True
    return False


def _eval_assert(a, hang_size):
    """Poll a SELF assertion until pass or timeout. Returns a result dict."""
    deadline = time.time() + a.timeoutSec
    last_reason = "no probe result"
    while time.time() < deadline:
        if _hang_present(hang_size):
            return {"verdict": "blocked", "reason": "HANG_DETECTED during probe",
                    "stack_available": False,
                    "note": "blocked-by-crash — needs Agent 3 idle-spin fix / OBS-4"}
        ok, reply = tankoctl(a.probe, timeout=max(5, a.timeoutSec))
        if not ok and not isinstance(reply, dict):
            last_reason = "probe call failed"
            time.sleep(1); continue
        value = extract(reply, a.path) if a.path else reply
        if value is MISSING:
            last_reason = f"path {a.path} missing"
            time.sleep(1); continue
        passed, reason = evaluate(a.expect, value)
        if passed:
            return {"verdict": "pass", "reason": reason}
        last_reason = reason
        time.sleep(1)
    return {"verdict": "fail", "reason": last_reason}


def run_journey(journey, results):
    for step in journey.steps:
        # 1) perform the step action once (the trigger), bracketed by a mark.
        if step.action:
            _mark(f"{step.id} ACTION START")
            ok, reply = tankoctl(step.action, timeout=30)
            _mark(f"{step.id} ACTION END {'ok' if ok else 'err'}")
            time.sleep(step.settleSec)
        # 2) evaluate each SELF assertion (VISUAL ones are marked for later).
        passed_ids = {r["id"] for r in results if r["verdict"] == "pass"}
        for a in step.asserts:
            if a.needs and not all(n in passed_ids for n in a.needs):
                continue  # prereq failed -> skip (still traceable as absent)
            hang_size = os.path.getsize(HANG) if os.path.exists(HANG) else -1
            _mark(f"{a.id} START")
            if a.lane == "SELF":
                res = _eval_assert(a, hang_size)
            else:  # VISUAL — record a window; visual_workorder fills it later
                time.sleep(2)  # let a couple seconds of footage accrue
                res = {"verdict": "pending_visual", "reason": "see visual_workorder"}
            _mark(f"{a.id} END {res['verdict']}")
            res.update({"id": a.id, "journey": journey.id, "lane": a.lane,
                        "action": " ".join(map(str, step.action or [a.id]))})
            if res["verdict"] == "pass":
                passed_ids.add(a.id)
            results.append(res)
            print(f"  [{a.id}] {res['verdict']}: {res.get('reason','')[:80]}")
            if res["verdict"] == "fail" and a.on_fail == "abort_journey":
                return
            if res["verdict"] == "fail" and a.on_fail == "relaunch":
                relaunch()
        # 3) recovery check between steps. A dead bridge = blocked-by-crash, NOT a
        # functional fail of the feature under test (Agent 0 review flag 3).
        if not _alive():
            print("  ! bridge dead — relaunching (blocked-by-crash)")
            results.append({"id": f"{step.id}.death", "journey": journey.id,
                            "lane": "SELF", "verdict": "blocked",
                            "action": step.id, "reason": "bridge dead after step",
                            "stack_available": False,
                            "note": "blocked-by-crash — needs Agent 3 idle-spin fix / OBS-4"})
            relaunch()


def run_catalogue(cat, only=None):
    results = []
    for j in cat.journeys:
        if only and j.id not in only:
            continue
        print(f"== Journey {j.id}: {j.name} ==")
        run_journey(j, results)
    return results
