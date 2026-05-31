#!/usr/bin/env python3
"""The Office — derived-status clarity engine (Python, $0).

Computes each brother's REAL status from GROUND TRUTH — git commits + bus
activity + a static roster — NEVER from self-reporting (the thing that's
unreliable). Pure functions do the logic (deterministic, unit-tested); thin IO
wrappers read real git/bus; `roster` CLI prints JSON for the web GUI.

  python office_status.py roster      -> prints JSON list of per-brother status
"""
import os
import re
import sys
import json
import subprocess
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import office_bus  # noqa: E402  (shares BUS() path + repo root)

# Static roster: identity facts that ground truth can't derive (role, engine,
# whether this engine auto-wakes reliably). Order here = display order.
ROSTER = [
    ("agent0", "Coordinator",        "claude",   True),
    ("agent1", "Comics / Tankoyomi", "claude",   True),
    ("agent2", "Books / TankoLibrary", "claude", True),
    ("agent3", "Video Player",       "claude",   True),
    ("agent4", "Stream / Tankorent", "claude",   True),
    ("agent5", "Library UX / Theme", "claude",   True),
    ("agent7", "Codex (prototypes / audits)", "codex", False),
    ("agent8", "Prompt Architect",   "claude",   True),
    ("agent9", "DeepSeek (exec / audit)", "deepseek", False),
]

PRESENCE_WINDOW_SEC = 1800  # 30 min: active if a bus msg OR commit within this
HEARTBEAT_WINDOW_SEC = 25    # wake channel is "live" if the watch beat within this
                             # (office_watch.sh beats every ~3s; missing/stale = deaf)

# Graded status tiers (seconds): the freshest signal's age picks the tier.
STATUS_TIERS = (("active", 300), ("recent", 1800), ("quiet", 7200))  # else "cold"


def _ago(sec):
    """Seconds -> compact human age ('2m', '3h', '4d'). Mirrors the GUI's ago()."""
    if sec is None:
        return ""
    if sec < 60:
        return "{0}s".format(int(sec))
    if sec < 3600:
        return "{0}m".format(int(sec // 60))
    if sec < 86400:
        return "{0}h".format(int(sec // 3600))
    return "{0}d".format(int(sec // 86400))


def freshest_signal(last_said_sec, last_commit_sec):
    """Return (age_sec, source) for the most-recent signal, or (None, None)."""
    cands = []
    if last_said_sec is not None:
        cands.append((last_said_sec, "said"))
    if last_commit_sec is not None:
        cands.append((last_commit_sec, "commit"))
    if not cands:
        return (None, None)
    return min(cands, key=lambda c: c[0])


def derive_status(last_said_sec, last_commit_sec, wake_state):
    """Grade a brother into a tier + an HONEST label that names the signal's
    source + age and folds in wake-reachability. Never claims certainty the
    signals don't support (spec §2.1) — incl. NEVER claiming 'wake DOWN' when we
    have no heartbeat data at all (that's 'unknown', not 'down')."""
    age, source = freshest_signal(last_said_sec, last_commit_sec)
    if age is None:
        tier, label = "cold", "cold · no signal"
    else:
        tier = "cold"
        for name, bound in STATUS_TIERS:
            if age <= bound:
                tier = name
                break
        label = "{0} · {1} {2} ago".format(tier, source, _ago(age))
    if wake_state == "down":  # only warn when a heartbeat EXISTED and went stale
        label += " · wake DOWN"
    return tier, label

_AGENT_TAG = re.compile(r"^\[Agent\s*#?\s*(\d+)", re.IGNORECASE)


def parse_agent_commits(git_log_text, now_epoch):
    """git log text ('<ctime>\\t<short-sha>\\t<subject>' per line, NEWEST first)
    -> {('agent'+N): {subject, sha, sec}} keeping the newest commit per agent."""
    out = {}
    for line in git_log_text.splitlines():
        line = line.rstrip("\n")
        if not line:
            continue
        parts = line.split("\t", 2)
        if len(parts) < 3:
            continue
        ctime_s, sha, subject = parts
        m = _AGENT_TAG.match(subject)
        if not m:
            continue
        key = "agent" + m.group(1)
        if key in out:  # newest-first: first hit wins
            continue
        try:
            ctime = int(ctime_s)
        except ValueError:
            continue
        out[key] = {"subject": subject, "sha": sha, "sec": max(0, now_epoch - ctime)}
    return out


def _epoch_of(ts):
    """ISO-8601 ('2026-05-31T00:00:00+00:00') -> unix seconds, or None."""
    if not ts:
        return None
    try:
        return int(datetime.fromisoformat(ts).timestamp())
    except (ValueError, TypeError):
        return None


def bus_activity(records, now_epoch):
    """Bus records -> {('agent'+N): {last_said, arc, sec, blocked}}.
    Only kind in (chat, blocked) feed last_said/arc/blocked; 'activity' lines
    (auto-mirrored commits) are for the message log, not the roster."""
    out = {}
    for rec in records:
        frm = rec.get("from", "")
        if not frm.startswith("agent"):
            continue
        if rec.get("kind") not in ("chat", "blocked"):
            continue
        ep = _epoch_of(rec.get("ts"))
        sec = max(0, now_epoch - ep) if ep is not None else None
        out[frm] = {
            "last_said": (rec.get("msg") or "")[:80],
            "arc": rec.get("arc"),
            "sec": sec,
            "blocked": rec.get("kind") == "blocked",
        }
    return out


def compute_roster(commits_by_agent, bus_by_agent, now_epoch,
                   heartbeats_by_agent=None, roster=ROSTER,
                   presence_window=PRESENCE_WINDOW_SEC,
                   heartbeat_window=HEARTBEAT_WINDOW_SEC,
                   responder_hb_by_agent=None):
    """Merge static roster + git + bus + watch heartbeats into the canonical list.

    wake_alive distinguishes "this brother's watch is alive and can hear new
    messages" from "present" (did something recently). A brother can be present
    (committed 5m ago) yet wake-dead (watch died) = deaf to new pings.

    responder_alive is separate again: it means the OWNED-WORKER backup net is
    watching this brother (a fresh responder heartbeat) — so a dropped message
    will still get a marked, non-binding reply even when his tab is dark."""
    heartbeats_by_agent = heartbeats_by_agent or {}
    responder_hb_by_agent = responder_hb_by_agent or {}
    result = []
    for agent, role, engine, wakeable in roster:
        c = commits_by_agent.get(agent)
        b = bus_by_agent.get(agent)
        bus_sec = b.get("sec") if b else None
        com_sec = c.get("sec") if c else None
        present = any(s is not None and s <= presence_window for s in (bus_sec, com_sec))
        wake_age = heartbeats_by_agent.get(agent)
        wake_alive = wake_age is not None and wake_age <= heartbeat_window
        # 3-state honesty: no heartbeat data = "unknown" (we can't tell), NOT "down".
        wake_state = "unknown" if wake_age is None else ("live" if wake_alive else "down")
        resp_age = responder_hb_by_agent.get(agent)
        responder_alive = resp_age is not None and resp_age <= heartbeat_window
        status, status_label = derive_status(bus_sec, com_sec, wake_state)
        last_commit = None
        if c:
            last_commit = "{0} ({1})".format(c["subject"][:60], c["sha"])
        result.append({
            "agent": agent,
            "role": role,
            "engine": engine,
            "wakeable": wakeable,
            "present": present,
            "wake_alive": wake_alive,
            "wake_state": wake_state,
            "wake_age_sec": wake_age,
            "responder_alive": responder_alive,
            "status": status,
            "status_label": status_label,
            "current_arc": b.get("arc") if b else None,
            "last_said": b.get("last_said") if b else None,
            "last_said_sec": bus_sec,
            "last_commit": last_commit,
            "last_commit_sec": com_sec,
            "blocked": bool(b.get("blocked")) if b else False,
        })
    return sorted(result, key=lambda r: r["agent"])


def _git_log_text(repo=None, limit=200):
    repo = repo or office_bus._repo_root()
    try:
        r = subprocess.run(
            ["git", "-C", repo, "log", "-n", str(limit),
             "--pretty=format:%ct%x09%h%x09%s"],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
        )
        return r.stdout if r.returncode == 0 else ""
    except (OSError, ValueError):
        return ""


def _bus_records():
    bus = office_bus.BUS()
    out = []
    if os.path.exists(bus):
        with open(bus, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    out.append(json.loads(line))
                except json.JSONDecodeError:
                    continue
    return out


def _heartbeats_dir():
    return os.path.join(office_bus._dir(), ".office_heartbeats")


def _heartbeats(now_epoch):
    """{('agent'+N): age_sec} from the watch heartbeat files' mtimes. Missing
    file => agent absent from the dict => wake_alive False (never clocked in)."""
    out = {}
    hb = _heartbeats_dir()
    if not os.path.isdir(hb):
        return out
    for fn in os.listdir(hb):
        if not fn.endswith(".beat"):
            continue
        agent = fn[:-5]
        try:
            mtime = os.path.getmtime(os.path.join(hb, fn))
        except OSError:
            continue
        out[agent] = max(0, now_epoch - int(mtime))
    return out


def _responder_heartbeats_dir():
    return os.path.join(office_bus._dir(), ".office_responder_heartbeats")


def _responder_heartbeats(now_epoch):
    """{('agent'+N): age_sec} from the responder-beat files' mtimes — i.e. which
    brothers the owned-worker backup net is currently watching. Missing file =>
    no responder armed for that brother."""
    out = {}
    hb = _responder_heartbeats_dir()
    if not os.path.isdir(hb):
        return out
    for fn in os.listdir(hb):
        if not fn.endswith(".beat"):
            continue
        agent = fn[:-5]
        try:
            mtime = os.path.getmtime(os.path.join(hb, fn))
        except OSError:
            continue
        out[agent] = max(0, now_epoch - int(mtime))
    return out


def roster_now(now_epoch=None):
    import time
    now_epoch = int(time.time()) if now_epoch is None else now_epoch
    commits = parse_agent_commits(_git_log_text(), now_epoch)
    busby = bus_activity(_bus_records(), now_epoch)
    heartbeats = _heartbeats(now_epoch)
    responder_hb = _responder_heartbeats(now_epoch)
    return compute_roster(commits, busby, now_epoch, heartbeats,
                          responder_hb_by_agent=responder_hb)


def main(argv):
    if not argv or argv[0] != "roster":
        sys.exit("usage: office_status.py roster")
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    print(json.dumps(roster_now(), ensure_ascii=False))


if __name__ == "__main__":
    main(sys.argv[1:])
