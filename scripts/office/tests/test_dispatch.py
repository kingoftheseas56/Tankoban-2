#!/usr/bin/env python3
"""Tests for office_dispatch.py pure logic. Mirrors test_asks.py: check()/main()."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import office_dispatch as D  # noqa: E402

fails = 0


def check(cond, label):
    global fails
    print("ok:" if cond else "FAIL:", label)
    if not cond:
        fails += 1


def rec(seq, frm, to, kind="chat", msg="", arc=None, ts="2026-06-02T20:00:00+05:30"):
    return {"seq": seq, "from": frm, "to": to, "kind": kind, "arc": arc, "msg": msg, "ts": ts}


def test_recently_active():
    now = D._epoch("2026-06-02T20:01:00+05:30")  # 60s after the post below
    bus = [rec(10, "agent3", "all", msg="P0 shipped", ts="2026-06-02T20:00:00+05:30")]
    check(D._recently_active("agent3", now, 90, bus), "active: a post 60s ago is within a 90s window")
    check(not D._recently_active("agent3", now, 30, bus), "active: a post 60s ago is outside a 30s window")
    check(not D._recently_active("agent2", now, 90, bus), "active: a brother who never posted is not active")


def test_classify_summon():
    # background brothers can't summon (no chains)
    a, _ = D.classify_summon("agent2", "agent3", "bg", is_live=False)
    check(a == "refuse_chain", "classify: arc='bg' summon is refused (no chains)")
    # malformed target
    a, _ = D.classify_summon("agent0", "all", None, is_live=False)
    check(a == "skip_badtarget", "classify: target 'all' is rejected (must be one brother)")
    a, _ = D.classify_summon("agent0", "agent2,agent3", None, is_live=False)
    check(a == "skip_badtarget", "classify: multi-target is rejected")
    # self
    a, _ = D.classify_summon("agent0", "agent0", None, is_live=False)
    check(a == "skip_self", "classify: summoning yourself is a no-op")
    # the two real routes
    a, _ = D.classify_summon("agent0", "agent3", None, is_live=True)
    check(a == "route_live", "classify: a live target routes to his watch")
    a, _ = D.classify_summon("agent0", "agent2", None, is_live=False)
    check(a == "spawn", "classify: a non-live target spawns a background brother")


def test_resolve_pending():
    base_ts = "2026-06-02T20:00:00+05:30"
    t0 = D._epoch(base_ts)
    # a summon #20 routed-live to agent4 at t0, deadline t0+90
    pending = [{"target": "agent4", "seq": 20, "frm": "agent0", "task": "x", "deadline": t0 + 90}]

    # (a) target answered (posted seq 21 > 20): resolved, nothing pending, no fallback
    bus_ack = [rec(21, "agent4", "agent0", msg="on it", ts=base_ts)]
    still, fb = D.resolve_pending(pending, bus_ack, now=t0 + 30)
    check(still == [] and fb == [], "pending: a reply (seq>summon) resolves the summon")

    # (b) no answer, before deadline: stays pending, no fallback yet
    still, fb = D.resolve_pending(pending, [], now=t0 + 30)
    check(len(still) == 1 and fb == [], "pending: silence before deadline keeps it open")

    # (c) no answer, past deadline: falls back to a spawn, dropped from pending
    still, fb = D.resolve_pending(pending, [], now=t0 + 120)
    check(still == [] and len(fb) == 1, "pending: silence past deadline triggers a fallback spawn")
    check(fb[0]["target"] == "agent4", "pending: fallback carries the original target")

    # (d) an EARLIER post (seq 19 < 20) does NOT count as an answer
    bus_old = [rec(19, "agent4", "agent0", msg="earlier", ts=base_ts)]
    still, fb = D.resolve_pending(pending, bus_old, now=t0 + 120)
    check(len(fb) == 1, "pending: a pre-summon post is not an ack")


def main():
    test_recently_active()
    test_classify_summon()
    test_resolve_pending()
    print("\n{0}".format("ALL PASS" if fails == 0 else "{0} FAILED".format(fails)))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
