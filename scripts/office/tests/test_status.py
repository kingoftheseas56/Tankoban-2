#!/usr/bin/env python3
"""Tests for office_status.py — pure functions tested directly (deterministic,
no real git/bus), plus the new office_bus subcommands via subprocess.
Mirrors tests/test_office.py: check()/main(), run with `python test_status.py`."""
import os
import sys
import json
import tempfile
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import office_status  # noqa: E402

fails = 0


def check(cond, label):
    global fails
    if cond:
        print("ok:", label)
    else:
        print("FAIL:", label)
        fails += 1


def test_parse_agent_commits():
    now = 1_000_000
    # git log lines: "<ctime>\t<short-sha>\t<subject>"
    text = "\n".join([
        "999700\t4b1f82d\t[Agent 9 (DeepSeek), NetSeam control half] block/throttle",
        "999000\tbb7e4ed\t[Agent 0, NETSEAM closing sweep]: 2 stray sites",
        "998000\tdeadbee\tmerge branch (no agent tag)",
        "997000\t1234abc\t[Agent 0, dashboard] older agent0 commit",
    ])
    out = office_status.parse_agent_commits(text, now)
    check(out["agent9"]["sha"] == "4b1f82d", "parse: agent9 latest commit sha")
    check(out["agent9"]["sec"] == 300, "parse: agent9 commit age = now-ctime")
    check(out["agent0"]["sha"] == "bb7e4ed", "parse: agent0 keeps NEWEST (not older 1234abc)")
    check("subject" in out["agent0"], "parse: subject captured")
    check(all(not k.startswith("agent") or k[5:].isdigit() for k in out), "parse: only agentN keys")


def test_bus_activity():
    now = 1_000_000
    recs = [
        {"seq": 1, "from": "agent4", "to": "all", "kind": "chat", "arc": "NETSEAM",
         "ts": "2026-05-31T00:00:00+00:00", "msg": "throttle ready"},
        {"seq": 2, "from": "agent4", "to": "all", "kind": "activity", "arc": None,
         "ts": "2026-05-31T00:01:00+00:00", "msg": "committed X (abc1234)"},
        {"seq": 3, "from": "agent1", "to": "all", "kind": "blocked", "arc": "COMICS",
         "ts": "2026-05-31T00:02:00+00:00", "msg": "stuck on cover leak"},
    ]
    # epoch_of returns the parsed unix time; feed a now far ahead so sec is positive
    out = office_status.bus_activity(recs, now)
    check(out["agent4"]["last_said"] == "throttle ready",
          "bus: activity line does NOT override last_said (chat only)")
    check(out["agent4"]["arc"] == "NETSEAM", "bus: arc from last chat msg")
    check(out["agent4"]["blocked"] is False, "bus: agent4 not blocked")
    check(out["agent1"]["blocked"] is True, "bus: agent1 blocked (kind=blocked)")
    check(out["agent1"]["last_said"] == "stuck on cover leak", "bus: blocked msg is last_said")


def test_compute_roster():
    now = 1_000_000
    commits = {"agent4": {"subject": "NetSeam control half", "sha": "4b1f82d", "sec": 300}}
    busby = {
        "agent4": {"last_said": "throttle ready", "arc": "NETSEAM", "sec": 120, "blocked": False},
        "agent1": {"last_said": "stuck", "arc": "COMICS", "sec": 99999, "blocked": True},
        # Track C #11 fixture — brothers with NO heartbeat (absent from `heartbeats`)
        # but a bus post: agent9 chatted 60s ago (reachable via presence), agent8 last
        # chatted 300s ago (older than the 90s activity window => still unknown).
        "agent9": {"last_said": "audit done", "arc": None, "sec": 60, "blocked": False},
        "agent8": {"last_said": "old note", "arc": None, "sec": 300, "blocked": False},
    }
    # heartbeats: agent4's watch is live (beat 5s ago), agent1's is dead (beat 900s
    # ago), agent7/agent8/agent9 have no entry at all (never clocked in).
    heartbeats = {"agent4": 5, "agent1": 900}
    roster = office_status.compute_roster(commits, busby, now, heartbeats,
                                          presence_window=1800, liveness_window=30,
                                          activity_window=90)
    by = {r["agent"]: r for r in roster}
    check(by["agent4"]["present"] is True, "roster: agent4 present (recent msg+commit)")
    check(by["agent4"]["current_arc"] == "NETSEAM", "roster: agent4 arc")
    check(by["agent4"]["last_commit"] == "NetSeam control half (4b1f82d)",
          "roster: last_commit = subject (sha)")
    check(by["agent4"]["last_commit_sec"] == 300, "roster: commit age passed through")
    check(by["agent1"]["present"] is False, "roster: agent1 stale (msg sec > window, no commit)")
    check(by["agent1"]["blocked"] is True, "roster: agent1 blocked surfaced")
    check(by["agent4"]["wake_alive"] is True, "roster: agent4 wake live (beat 5s)")
    check(by["agent4"]["wake_state"] == "live", "roster: agent4 wake_state live")
    check(by["agent4"]["wake_age_sec"] == 5, "roster: agent4 wake age passed through")
    check(by["agent1"]["wake_alive"] is False, "roster: agent1 wake DEAD (beat 900s > window)")
    check(by["agent1"]["wake_state"] == "down", "roster: agent1 wake_state down (stale beat)")
    check(by["agent7"]["wake_alive"] is False, "roster: agent7 wake dead (no heartbeat = never clocked in)")
    check(by["agent7"]["wake_state"] == "unknown", "roster: agent7 wake_state UNKNOWN (no beat, not 'down')")
    check(by["agent7"]["wake_age_sec"] is None, "roster: agent7 wake age None (no beat)")
    # Track C #11 — activity fallback for heartbeat-less brothers (Codex/DeepSeek, or
    # a watch that never clocked in). Recent bus post => reachable, never false-dead.
    check(by["agent9"]["wake_alive"] is True,
          "roster #11: agent9 no-beat but chatted 60s -> reachable via presence")
    check(by["agent9"]["wake_state"] == "active",
          "roster #11: agent9 wake_state=active (bus-recent, no heartbeat)")
    check(by["agent9"]["wake_age_sec"] is None,
          "roster #11: agent9 wake age None (no beat) even when active")
    check(by["agent8"]["wake_alive"] is False,
          "roster #11: agent8 no-beat + stale chat (300s > 90s window) -> not reachable")
    check(by["agent8"]["wake_state"] == "unknown",
          "roster #11: agent8 wake_state unknown (activity too old, NOT 'down')")
    check(by["agent4"]["status"] == "active", "roster: agent4 status=active (said 120s)")
    check(by["agent4"]["status_label"] == "active · said 2m ago",
          "roster: agent4 honest label (wake live -> no warning)")
    check(by["agent1"]["status"] == "cold", "roster: agent1 status=cold (said 99999s)")
    check("wake DOWN" in by["agent1"]["status_label"],
          "roster: agent1 label warns wake DOWN (heartbeat 900s > window)")
    check(by["agent7"]["status"] == "cold" and by["agent7"]["status_label"] == "cold · no signal",
          "roster: agent7 no signal + no heartbeat -> cold/no-signal (NO false wake-DOWN)")
    check(by["agent7"]["wakeable"] is False, "roster: agent7 (codex) not wakeable")
    check(by["agent0"]["wakeable"] is True, "roster: agent0 (claude) wakeable")
    check([r["agent"] for r in roster] == sorted([r["agent"] for r in roster]),
          "roster: stable sorted order")


def test_roster_cli():
    # The CLI must run against the REAL repo and emit a JSON list with the
    # canonical keys for every roster agent. (Integration smoke, not values.)
    BUS_PY_DIR = os.path.join(HERE, "..")
    proc = subprocess.run(
        [sys.executable, os.path.join(BUS_PY_DIR, "office_status.py"), "roster"],
        capture_output=True, text=True,
    )
    check(proc.returncode == 0, "cli: roster exits 0")
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError:
        data = None
    check(isinstance(data, list) and len(data) >= 9, "cli: roster prints JSON list of >=9 brothers")
    if data:
        keys = set(data[0].keys())
        need = {"agent", "role", "engine", "wakeable", "present", "wake_alive",
                "wake_state", "wake_age_sec", "status", "status_label",
                "current_arc", "last_said", "last_said_sec", "last_commit", "last_commit_sec",
                "blocked"}
        check(need <= keys, "cli: each entry has all canonical keys")


def _sandbox_env():
    sand = tempfile.mkdtemp()
    env = dict(os.environ)
    env["OFFICE_DIR"] = sand
    env["OFFICE_BUS"] = os.path.join(sand, "bus.jsonl")
    env["OFFICE_CURSORS"] = os.path.join(sand, "cursors")
    env["OFFICE_SESSIONS"] = os.path.join(sand, "sessions.json")
    return env


def test_flag_subcommand():
    env = _sandbox_env()
    BUS_PY = os.path.join(HERE, "..", "office_bus.py")
    subprocess.run([sys.executable, BUS_PY, "join", "sess-b", "4"], env=env)
    subprocess.run([sys.executable, BUS_PY, "flag", "sess-b", "blocked on HTTP preload"], env=env)
    with open(env["OFFICE_BUS"], encoding="utf-8") as f:
        rec = json.loads([x for x in f if x.strip()][-1])
    check(rec["kind"] == "blocked", "flag: posts kind=blocked")
    check(rec["from"] == "agent4" and rec["to"] == "all", "flag: from resolved, to=all")
    check(rec["msg"] == "blocked on HTTP preload", "flag: message body intact")


def test_mirror_commit():
    env = _sandbox_env()
    BUS_PY = os.path.join(HERE, "..", "office_bus.py")
    # mirror-commit takes (sha, subject) so it's testable without a real commit.
    subprocess.run([sys.executable, BUS_PY, "mirror-commit", "abc1234",
                    "[Agent 4, stream] fix preload race"], env=env)
    with open(env["OFFICE_BUS"], encoding="utf-8") as f:
        rec = json.loads([x for x in f if x.strip()][-1])
    check(rec["kind"] == "activity", "mirror: posts kind=activity")
    check(rec["from"] == "agent4", "mirror: from = agent parsed from [Agent N tag")
    check("abc1234" in rec["msg"], "mirror: msg includes short sha")
    # Untagged commit -> from 'system', still mirrored.
    subprocess.run([sys.executable, BUS_PY, "mirror-commit", "def5678",
                    "merge branch master"], env=env)
    with open(env["OFFICE_BUS"], encoding="utf-8") as f:
        rec2 = json.loads([x for x in f if x.strip()][-1])
    check(rec2["from"] == "system", "mirror: untagged commit -> from=system")


def test_derive_status():
    # tiers: active <=300s, recent <=1800s, quiet <=7200s, else cold.
    # freshest signal across (last_said_sec, last_commit_sec) drives the grade.
    tier, label = office_status.derive_status(120, 900, "live")
    check(tier == "active", "derive: freshest signal (said 120s) -> active")
    check(label == "active · said 2m ago", "derive: label names source+age, said wins")
    tier, label = office_status.derive_status(5000, 600, "live")
    check(tier == "recent", "derive: commit 600s is freshest -> recent")
    check(label == "recent · commit 10m ago", "derive: commit label")
    tier, label = office_status.derive_status(4000, None, "live")
    check(tier == "quiet", "derive: said 4000s -> quiet")
    tier, label = office_status.derive_status(99999, 99999, "live")
    check(tier == "cold", "derive: both >2h -> cold")
    tier, label = office_status.derive_status(None, None, "live")
    check(tier == "cold" and label == "cold · no signal", "derive: no signal -> cold/no-signal")
    # wake-reachability folds in ONLY when we actually KNOW it's down (stale heartbeat)
    tier, label = office_status.derive_status(120, None, "down")
    check(label == "active · said 2m ago · wake DOWN",
          "derive: wake-down appends ' · wake DOWN' even when active")
    check(office_status.derive_status(120, None, "live")[1].endswith("ago"),
          "derive: wake-live label carries NO wake warning")
    check("wake" not in office_status.derive_status(120, None, "unknown")[1],
          "derive: wake-UNKNOWN carries NO 'wake DOWN' (no heartbeat data = don't overclaim)")
    check("wake" not in office_status.derive_status(120, None, "active")[1],
          "derive: wake-ACTIVE (#11 presence) carries NO 'wake DOWN' (reachable, not down)")


def main():
    test_parse_agent_commits()
    test_bus_activity()
    test_compute_roster()
    test_roster_cli()
    test_flag_subcommand()
    test_mirror_commit()
    test_derive_status()
    print("\n%d failure(s)" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
