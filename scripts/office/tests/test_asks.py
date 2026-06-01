#!/usr/bin/env python3
"""Tests for office_asks.py pure logic. Mirrors test_status.py: check()/main()."""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import office_asks as A  # noqa: E402

fails = 0


def check(cond, label):
    global fails
    print("ok:" if cond else "FAIL:", label)
    if not cond:
        fails += 1


def rec(seq, frm, to, kind, msg, arc=None, ts="2026-06-01T12:00:00+05:30"):
    return {"seq": seq, "from": frm, "to": to, "kind": kind, "arc": arc, "msg": msg, "ts": ts}


def test_compute_asks():
    now = 1_000_000
    base = [rec(10, "agent2", "agent1", "chat", "can you do X?")]
    asks = A.compute_asks(base, now, window=300, escalate2=300, now_age={10: 120})
    by = {(a["ask_seq"], a["to_agent"]): a for a in asks}
    check((10, "agent1") in by, "ask: direct chat creates an ask owed by the addressee")
    check(by[(10, "agent1")]["state"] == "open", "ask: young unanswered ask is open")
    check(by[(10, "agent1")]["from"] == "agent2", "ask: asker recorded")

    acked = base + [rec(12, "agent1", "agent2", "ack", "on it", arc="10")]
    a2 = {(x["ask_seq"], x["to_agent"]): x for x in A.compute_asks(acked, now, 300, 300, now_age={10: 9999})}
    check(a2[(10, "agent1")]["state"] == "acked", "ask: ack closes the escalation clock")

    answered = base + [rec(13, "agent1", "agent2", "chat", "done, here")]
    a3 = {(x["ask_seq"], x["to_agent"]): x for x in A.compute_asks(answered, now, 300, 300, now_age={10: 9999})}
    check(a3[(10, "agent1")]["state"] == "answered", "ask: direct reply answers the ask")

    auto = base + [rec(14, "agent1", "agent2", "chat", "[auto] old backup", arc="auto_reply")]
    a_auto = {(x["ask_seq"], x["to_agent"]): x for x in A.compute_asks(auto, now, 300, 300, now_age={10: 9999})}
    check(a_auto[(10, "agent1")]["state"] == "owed", "ask: legacy auto_reply is not an answer")

    bc = [rec(20, "hemanth", "all", "chat", "good morning")]
    check(A.compute_asks(bc, now, 300, 300, now_age={20: 50}) == [],
          "ask: @all broadcast creates no ask")

    multi = [rec(30, "agent0", "agent1,agent2", "chat", "both please")]
    am = {(x["ask_seq"], x["to_agent"]) for x in A.compute_asks(multi, now, 300, 300, now_age={30: 50})}
    check((30, "agent1") in am and (30, "agent2") in am, "ask: comma-list creates one ask per addressee")

    a4 = {(x["ask_seq"], x["to_agent"]): x for x in A.compute_asks(base, now, 300, 300, now_age={10: 400})}
    check(a4[(10, "agent1")]["state"] == "owed", "ask: past window with no ack/answer -> owed")

    esc = base + [rec(15, "system", "agent0", "escalate", "agent1 missed #10", arc="10:agent1")]
    a5 = {(x["ask_seq"], x["to_agent"]): x for x in A.compute_asks(esc, now, 300, 300, now_age={10: 400})}
    check(a5[(10, "agent1")]["state"] == "escalated_a0", "ask: per-addressee escalation -> escalated_a0")

    legacy = base + [rec(16, "system", "agent0", "escalate", "agent1 missed #10", arc="10")]
    a6 = {(x["ask_seq"], x["to_agent"]): x for x in A.compute_asks(legacy, now, 300, 300, now_age={10: 400})}
    check(a6[(10, "agent1")]["state"] == "escalated_a0", "ask: legacy escalation arc remains readable")


def test_due_escalations():
    now = 1_000_000
    base = [rec(10, "agent2", "agent1", "chat", "X?")]
    check(A.due_escalations(base, now, 300, 300, now_age={10: 50}) == [],
          "due: young ask -> nothing due")

    d = A.due_escalations(base, now, 300, 300, now_age={10: 400})
    check(d == [(10, "agent1", "agent0")], "due: overdue ask -> escalate to agent0")

    e0 = base + [rec(11, "system", "agent0", "escalate", "...", arc="10:agent1")]
    check(A.due_escalations(e0, now, 300, 300, now_age={10: 400}) == [],
          "due: agent0 escalation already posted -> idempotent")
    d2 = A.due_escalations(e0, now, 300, 300, now_age={10: 700})
    check(d2 == [(10, "agent1", "hemanth")], "due: past second window -> escalate to hemanth")

    eh = e0 + [rec(12, "system", "hemanth", "escalate", "...", arc="10:agent1")]
    check(A.due_escalations(eh, now, 300, 300, now_age={10: 700}) == [],
          "due: hemanth escalation already posted -> idempotent")

    ack = base + [rec(13, "agent1", "agent2", "ack", "ok", arc="10")]
    check(A.due_escalations(ack, now, 300, 300, now_age={10: 9999}) == [],
          "due: acked ask never escalates")

    multi = [rec(20, "agent2", "agent1,agent3", "chat", "both?"),
             rec(21, "system", "agent0", "escalate", "agent1 missed", arc="20:agent1")]
    dm = A.due_escalations(multi, now, 300, 300, now_age={20: 400})
    check(dm == [(20, "agent3", "agent0")], "due: comma-list idempotency is per addressee")


def _sandbox():
    sand = tempfile.mkdtemp()
    os.environ["OFFICE_DIR"] = sand
    os.environ["OFFICE_BUS"] = os.path.join(sand, "bus.jsonl")
    os.environ["OFFICE_CURSORS"] = os.path.join(sand, "cursors")
    os.environ["OFFICE_SESSIONS"] = os.path.join(sand, "sessions.json")
    return sand


def test_ack_event():
    _sandbox()
    import office_bus

    office_bus.cmd_append("agent2", "agent1", "chat", "null", "X?")
    office_bus.cmd_ack("agent1", "1", "on it")
    recs = A._bus_records()
    last = recs[-1]
    check(last["kind"] == "ack" and last["from"] == "agent1" and str(last["arc"]) == "1",
          "ack: cmd_ack appends kind=ack from the brother, arc=ask_seq")
    check(last["to"] == "agent2", "ack: ack is addressed back to the asker")
    by = {(a["ask_seq"], a["to_agent"]): a for a in A.compute_asks(recs, 1_000_000, now_age={1: 9999})}
    check(by[(1, "agent1")]["state"] == "acked", "ack: projection closes escalation clock")


def test_escalation_tick_posts():
    _sandbox()
    import office_bus
    import office_web

    office_bus.cmd_append("agent2", "agent1", "chat", "null", "X?")
    posted = office_web.escalate_tick_once(window=0, escalate2=999999)
    check(posted == 1, "tick: an overdue unacked ask posts one escalate event")
    recs = A._bus_records()
    esc = [r for r in recs if r.get("kind") == "escalate"]
    check(esc and esc[0]["to"] == "agent0" and str(esc[0]["arc"]) == "1:agent1",
          "tick: escalation is addressed to agent0, arc=ask_seq:addressee")

    again = office_web.escalate_tick_once(window=0, escalate2=999999)
    check(again == 0, "tick: re-running does not double-post the same level")


def main():
    test_compute_asks()
    test_due_escalations()
    test_ack_event()
    test_escalation_tick_posts()
    print("\n%d failure(s)" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
