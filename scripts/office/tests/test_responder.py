#!/usr/bin/env python3
"""Tests for office_responder.py pure logic. Mirrors test_status.py: check()/main(),
run with `python test_responder.py`."""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import office_responder as R  # noqa: E402

fails = 0


def check(cond, label):
    global fails
    print("ok:" if cond else "FAIL:", label)
    if not cond:
        fails += 1


def test_should_suppress():
    me = "agent1"
    trigger = {"seq": 100, "frm": "agent2", "to": "agent1", "text": "need your A/B call"}
    # agent1 replied directly to agent2 after the trigger -> suppress (he answered)
    bus = [{"seq": 105, "from": "agent1", "to": "agent2", "kind": "chat", "msg": "go with ii"}]
    sup, _ = R.should_suppress(trigger, bus, me)
    check(sup is True, "suppress: me answered the sender directly -> suppress")
    # agent1 posted something UNRELATED (to agent4) -> do NOT suppress (the v1 bug)
    bus2 = [{"seq": 106, "from": "agent1", "to": "agent4", "kind": "chat", "msg": "unrelated"}]
    sup2, _ = R.should_suppress(trigger, bus2, me)
    check(sup2 is False, "suppress: unrelated post must NOT suppress (target-aware)")
    # agent1 posted an activity line (commit mirror) -> not a real answer -> do NOT suppress
    bus3 = [{"seq": 107, "from": "agent1", "to": "agent2", "kind": "activity", "msg": "committed x"}]
    sup3, _ = R.should_suppress(trigger, bus3, me)
    check(sup3 is False, "suppress: activity line is not an answer -> do NOT suppress")
    # nothing from agent1 at all -> do NOT suppress (the silent case we must catch)
    sup4, _ = R.should_suppress(trigger, [], me)
    check(sup4 is False, "suppress: total silence -> do NOT suppress (backup fires)")
    # comma-list answer (me -> agent2,agent5) still counts
    bus5 = [{"seq": 108, "from": "agent1", "to": "agent2,agent5", "kind": "chat", "msg": "both"}]
    sup5, _ = R.should_suppress(trigger, bus5, me)
    check(sup5 is True, "suppress: comma-list including sender counts as answer")


def test_is_candidate():
    me = "agent1"
    check(R.is_candidate({"from": "agent2", "to": "agent1", "kind": "chat", "msg": "hi"}, me) is True,
          "candidate: direct to me")
    check(R.is_candidate({"from": "agent1", "to": "agent2", "kind": "chat", "msg": "x"}, me) is False,
          "candidate: my own post is never a candidate")
    check(R.is_candidate({"from": "agent0", "to": "all", "kind": "chat", "msg": "general question?"}, me) is False,
          "candidate: @all without my mention -> NOT (avoid fan-out)")
    check(R.is_candidate({"from": "agent0", "to": "all", "kind": "chat", "msg": "@agent1 your call?"}, me) is True,
          "candidate: @all explicitly mentioning me -> candidate")
    check(R.is_candidate({"from": "agent0", "to": "all", "kind": "chat", "msg": "agent 1 please confirm"}, me) is True,
          "candidate: @all 'agent N' mention -> candidate")
    check(R.is_candidate({"from": "agent2", "to": "agent1", "kind": "activity", "msg": "committed"}, me) is False,
          "candidate: activity line -> never")
    # cascade-guard: another responder's auto-reply addressed to me is NOT a candidate
    check(R.is_candidate({"from": "agent2", "to": "agent1", "kind": "chat", "arc": "auto_reply",
                          "msg": "[auto] backup read"}, me) is False,
          "candidate: an auto_reply addressed to me -> NEVER (cascade-guard)")


def test_format_backup_reply():
    out = R.format_backup_reply("agent1", "ack", "received, will pick up Phase 4")
    check(out.startswith("[auto") and "agent1" in out.lower() and "ack" in out,
          "format: marked with auto + agent + class")
    check("received" in out, "format: body preserved")
    nb = R.format_backup_reply("agent1", "nonbinding_assessment", "looks like option ii")
    check("owner to confirm" in nb.lower() or "non-binding" in nb.lower() or "backup read" in nb.lower(),
          "format: nonbinding_assessment carries a non-binding qualifier")
    try:
        R.format_backup_reply("agent1", "bogus", "x")
        check(False, "format: invalid class should raise")
    except ValueError:
        check(True, "format: invalid class raises ValueError")


def test_responder_cursor():
    sand = tempfile.mkdtemp()
    os.environ["OFFICE_DIR"] = sand   # office_bus._dir() honors this
    me = "agent4"
    check(R.responder_cursor(me) == 0, "cursor: default 0")
    R.set_responder_cursor(me, 42)
    check(R.responder_cursor(me) == 42, "cursor: persists 42")
    real = os.path.join(sand, ".bus_cursors", "agent4.seq")
    check(not os.path.exists(real), "cursor: responder does NOT touch the tab's agent cursor")
    own = os.path.join(sand, ".bus_responder_cursors", "agent4.seq")
    check(os.path.exists(own), "cursor: responder writes its OWN namespaced cursor")


def test_responder_heartbeat():
    sand = tempfile.mkdtemp()
    os.environ["OFFICE_DIR"] = sand
    me = "agent3"
    R.responder_heartbeat(me)
    p = os.path.join(sand, ".office_responder_heartbeats", "agent3.beat")
    check(os.path.exists(p), "heartbeat: responder writes its own beat file")
    with open(p, "r", encoding="utf-8") as f:
        check(f.read().strip().isdigit(), "heartbeat: beat file holds an epoch int (roster reads its freshness)")


def _sandbox_env():
    sand = tempfile.mkdtemp()
    os.environ["OFFICE_DIR"] = sand
    os.environ["OFFICE_BUS"] = os.path.join(sand, "bus.jsonl")
    os.environ["OFFICE_CURSORS"] = os.path.join(sand, "cursors")
    os.environ["OFFICE_SESSIONS"] = os.path.join(sand, "sessions.json")
    return sand


def test_post_reply_and_recheck():
    _sandbox_env()
    import office_bus
    office_bus.cmd_append("agent2", "agent4", "chat", "null", "agent4 need a hand?")  # seq 1
    trigger = {"seq": 1, "frm": "agent2", "to": "agent4", "text": "agent4 need a hand?"}
    # post a synthetic backup reply -> should append FROM agent4 TO agent2
    R.post_reply("agent4", "@agent2", "[auto · agent4's tab idle · ack] on it", trigger, R._bus_records())
    recs = R._bus_records()
    last = recs[-1]
    check(last["from"] == "agent4" and last["to"] == "agent2", "post: backup posts AS agent4 to the sender")
    check(last["msg"].startswith("[auto"), "post: message carries the auto marker")
    check(last.get("arc") == "auto_reply", "post: backup carries arc=auto_reply machine-truth metadata")
    # cascade-guard end-to-end: that posted auto_reply is NOT a candidate for another brother
    check(R.is_candidate(last, "agent2") is False, "post: a posted auto_reply is never another brother's candidate")
    # pre-send recheck: agent4 has now 'answered' agent2 -> a second post_reply ABORTS
    before = len(R._bus_records())
    R.post_reply("agent4", "@agent2", "[auto] second attempt", trigger, R._bus_records())
    check(len(R._bus_records()) == before, "post: pre-send recheck ABORTS when brother already answered")


def main():
    test_should_suppress()
    test_is_candidate()
    test_format_backup_reply()
    test_responder_cursor()
    test_responder_heartbeat()
    test_post_reply_and_recheck()
    print("\n%d failure(s)" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
