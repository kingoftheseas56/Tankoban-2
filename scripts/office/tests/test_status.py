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
    }
    roster = office_status.compute_roster(commits, busby, now, presence_window=1800)
    by = {r["agent"]: r for r in roster}
    check(by["agent4"]["present"] is True, "roster: agent4 present (recent msg+commit)")
    check(by["agent4"]["current_arc"] == "NETSEAM", "roster: agent4 arc")
    check(by["agent4"]["last_commit"] == "NetSeam control half (4b1f82d)",
          "roster: last_commit = subject (sha)")
    check(by["agent4"]["last_commit_sec"] == 300, "roster: commit age passed through")
    check(by["agent1"]["present"] is False, "roster: agent1 stale (msg sec > window, no commit)")
    check(by["agent1"]["blocked"] is True, "roster: agent1 blocked surfaced")
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
        need = {"agent", "role", "engine", "wakeable", "present", "current_arc",
                "last_said", "last_said_sec", "last_commit", "last_commit_sec", "blocked"}
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


def main():
    test_parse_agent_commits()
    test_bus_activity()
    test_compute_roster()
    test_roster_cli()
    test_flag_subcommand()
    print("\n%d failure(s)" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
