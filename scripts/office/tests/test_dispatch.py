#!/usr/bin/env python3
"""Tests for office_dispatch.py pure logic. Mirrors test_asks.py: check()/main()."""
import json
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
    # #5 wrong-engine guard: agent7 (Codex) / agent9 (DeepSeek) run their OWN engines —
    # must NEVER be spawned as a claude -p impostor, regardless of live state.
    a, _ = D.classify_summon("agent0", "agent7", None, is_live=False)
    check(a == "refuse_nonclaude", "classify: agent7 (Codex) refused — not a claude -p brother")
    a, _ = D.classify_summon("agent0", "agent9", None, is_live=True)
    check(a == "refuse_nonclaude", "classify: agent9 (DeepSeek) refused even if 'live'")


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


def test_resolve_pending_ignores_activity():
    # #2 HOLE: a brother's own commit-mirror posts kind='activity' from=agentN with a
    # seq > the summon — but watch-peek SKIPS activity, so it never woke him. That post
    # must NOT count as an ACK, or his own commit black-holes his own summon.
    base_ts = "2026-06-02T20:00:00+05:30"
    t0 = D._epoch(base_ts)
    pending = [{"target": "agent5", "seq": 20, "frm": "agent0", "task": "x", "deadline": t0 + 90}]
    activity = [rec(21, "agent5", "all", kind="activity", msg="feat(theme): ... (abc123)", ts=base_ts)]
    still, fb = D.resolve_pending(pending, activity, now=t0 + 120)
    check(len(fb) == 1 and still == [], "resolve: an 'activity' (commit-mirror) post does NOT ack — fallback still fires")
    # a real chat reply DOES ack (regression guard)
    chat = [rec(21, "agent5", "agent0", kind="chat", msg="on it", ts=base_ts)]
    still2, fb2 = D.resolve_pending(pending, chat, now=t0 + 120)
    check(fb2 == [] and still2 == [], "resolve: a real 'chat' reply still acks (regression)")


def test_reconcile_fallback():
    # #1 HOLE: a fallback spawn that is HELD (lock busy / cap / OSError -> _spawn_for False)
    # must STAY pending and retry, not be silently dropped.
    base_ts = "2026-06-02T20:00:00+05:30"
    t0 = D._epoch(base_ts)
    p = {"target": "agent5", "seq": 20, "frm": "agent0", "task": "x", "deadline": t0}
    # spawned ok -> resolved, nothing kept
    kept, gave = D.reconcile_fallback([p], [True], now=t0, retry_delay=3, max_tries=3)
    check(kept == [] and gave == [], "reconcile: a spawned fallback is resolved")
    # held -> kept with bumped deadline + tries=1
    kept, gave = D.reconcile_fallback([p], [False], now=t0, retry_delay=3, max_tries=3)
    check(len(kept) == 1 and gave == [], "reconcile: a held fallback stays pending (not dropped)")
    check(kept[0]["tries"] == 1 and kept[0]["deadline"] == t0 + 3, "reconcile: held entry bumps deadline + tries")
    # held past max_tries -> given up (surfaced), not silently dropped
    p3 = {**p, "tries": 2}
    kept, gave = D.reconcile_fallback([p3], [False], now=t0, retry_delay=3, max_tries=3)
    check(kept == [] and len(gave) == 1, "reconcile: held past max_tries is surfaced, not silently dropped")


def test_load_pending_skips_corrupt_line():
    # #19 HOLE: one corrupt line must NOT wipe ALL fallback memory.
    import tempfile
    d = tempfile.mkdtemp()
    old = os.environ.get("OFFICE_DIR")
    os.environ["OFFICE_DIR"] = d
    try:
        with open(os.path.join(d, ".office_pending.jsonl"), "w", encoding="utf-8") as f:
            f.write(json.dumps({"target": "agent5", "seq": 20, "frm": "agent0", "task": "x", "deadline": 1}) + "\n")
            f.write("{corrupt json>>>\n")
            f.write(json.dumps({"target": "agent2", "seq": 21, "frm": "agent0", "task": "y", "deadline": 2}) + "\n")
        loaded = D._load_pending()
        check(len(loaded) == 2, "load_pending: a corrupt line is skipped, good entries survive")
    finally:
        if old is None:
            os.environ.pop("OFFICE_DIR", None)
        else:
            os.environ["OFFICE_DIR"] = old


def test_dispatch_singleton():
    # #9: two dispatchers must not run at once (would double-spawn every summon).
    import tempfile
    d = tempfile.mkdtemp()
    old = os.environ.get("OFFICE_DIR")
    os.environ["OFFICE_DIR"] = d
    try:
        check(D._acquire_dispatch_singleton(), "singleton: first acquire succeeds")
        check(not D._acquire_dispatch_singleton(), "singleton: a fresh lock blocks a second dispatcher")
        os.utime(D.DISPATCH_LOCK(), (1, 1))  # force-stale (epoch 1970)
        check(D._acquire_dispatch_singleton(), "singleton: a STALE lock is reclaimed (crash recovery)")
        D._release_dispatch_singleton()
        check(D._acquire_dispatch_singleton(), "singleton: re-acquire after release")
        D._release_dispatch_singleton()
    finally:
        if old is None:
            os.environ.pop("OFFICE_DIR", None)
        else:
            os.environ["OFFICE_DIR"] = old


def test_validate_model():
    # Track C #18 — typo'd model must not ride into `claude -p --model <garbage>`.
    check(D._validate_model("opus") == "opus", "model: opus tier passes")
    check(D._validate_model("sonnet") == "sonnet", "model: sonnet tier passes")
    check(D._validate_model("haiku") == "haiku", "model: haiku tier passes")
    check(D._validate_model("claude-opus-4-8") == "claude-opus-4-8", "model: full claude-* id passes")
    check(D._validate_model("opsu") == "opus", "model: typo -> opus fallback")
    check(D._validate_model("") == "opus", "model: empty -> opus fallback")
    check(D._validate_model("gpt-4o") == "opus", "model: non-claude -> opus fallback")


def main():
    test_recently_active()
    test_validate_model()
    test_classify_summon()
    test_resolve_pending()
    test_resolve_pending_ignores_activity()
    test_reconcile_fallback()
    test_load_pending_skips_corrupt_line()
    test_dispatch_singleton()
    print("\n{0}".format("ALL PASS" if fails == 0 else "{0} FAILED".format(fails)))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
