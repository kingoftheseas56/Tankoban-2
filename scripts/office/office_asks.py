#!/usr/bin/env python3
"""The Office - deterministic ask/ack/answer projection.

The bus is still an append-only transport. This module derives ask lifecycle
state from bus records without model calls or side effects, mirroring
office_status.py's pure-projection shape.
"""
import json
import os
from datetime import datetime

WINDOW_SEC = int(os.environ.get("OFFICE_ASK_WINDOW_SEC", "300"))
ESCALATE2_SEC = int(os.environ.get("OFFICE_ASK_ESCALATE2_SEC", "300"))
ANSWERED_GRACE_SEC = int(os.environ.get("OFFICE_ASK_ANSWERED_GRACE_SEC", "120"))
# #15 owed-ask expiry: once an ask has been owed (past `window`) for this long
# without an ack, the escalation is silently dropped rather than re-escalated.
EXPIRE_SEC = int(os.environ.get("OFFICE_ASK_EXPIRE_SEC", "600"))  # 10 minutes


def _to_list(to):
    return [t.strip() for t in str(to or "").split(",") if t.strip()]


def _seq(rec):
    try:
        return int(rec.get("seq", 0))
    except (TypeError, ValueError):
        return 0


def _epoch(ts):
    try:
        return int(datetime.fromisoformat(ts).timestamp())
    except (TypeError, ValueError):
        return 0


def _age(rec, now_epoch, now_age):
    seq = _seq(rec)
    if now_age and seq in now_age:
        return now_age[seq]
    return max(0, now_epoch - _epoch(rec.get("ts", "")))


def _is_addressee(name):
    return str(name).startswith("agent") or name == "hemanth"


def _is_explicit_ask(text):
    """An authoritative opt-in marker — a deliberate, must-track request."""
    t = str(text or "").strip().lower()
    return "[ask]" in t or "[needs-reply]" in t


def _is_request(text):
    """Return True if text needs a reply (a real ask), False for statements/FYIs.

    Explicit [ask]/[fyi] tags are authoritative; the heuristic is the backstop.
    NOTE: a bare imperative with no '?' (e.g. "Review this", "Re-run the suite")
    is ambiguous against statements ("review closed") and is NOT auto-detected —
    tag those [ask]. This keeps false-positives near zero at the cost of needing
    one tag for the rare unmarked imperative ask.
    """
    t = str(text or "").strip().lower()
    if not t:
        return False
    if _is_explicit_ask(t):
        return True  # explicit ask wins even if [fyi] also present (never drop a deliberate ask)
    if "[fyi]" in t:
        return False
    if "?" in t:  # a question mark ANYWHERE (multi-part asks don't end in '?')
        return True
    for phrase in ("can you", "could you", "can u", "could u", "please", "pls",
                   "wdyt", "let me know", "go/no-go", "go or no go",
                   "take a look", "need you", "needs your"):
        if phrase in t:
            return True
    return False


def _has_no_rush(text):
    """#14 - True if the ask explicitly defers urgency ('no rush' & equivalents)."""
    t = str(text or "").lower()
    for phrase in ("no rush", "no hurry", "no urgency", "not urgent",
                   "take your time", "no pressure", "whenever you get a chance",
                   "when you get a chance", "whenever you have a moment",
                   "no worries if"):
        if phrase in t:
            return True
    return False


def _escalatable(text):
    """Gate for FIRING an escalation - stricter than _is_request (which only
    decides ask *tracking*). An escalate event is noisy, so it fires only for a
    genuine, non-deferred ask:

      #13  the text ends with '?' (sentence-final), OR carries an explicit
           [ask]/[needs-reply] tag. Plain chat/posts never escalate.
      #14  a 'no rush'/'no hurry'/equivalent ask is suppressed entirely, even
           when a '?' or [ask] tag is present.

    The ask is still tracked on the dashboard via compute_asks; this only
    suppresses the automatic escalate-to-agent0/hemanth ladder.
    """
    t = str(text or "").strip().lower()
    if not t:
        return False
    if _has_no_rush(t):       # #14 - no-rush wins over '?'/[ask]
        return False
    if _is_explicit_ask(t):   # deliberate opt-in escalates without needing '?'
        return True
    return t.rstrip().endswith("?")  # #13 - sentence-final question only


def _escalation_matches(arc, ask_seq, to_agent):
    arc = str(arc or "")
    return arc == "{0}:{1}".format(ask_seq, to_agent) or arc == str(ask_seq)


def compute_asks(records, now_epoch, window=WINDOW_SEC, escalate2=ESCALATE2_SEC, now_age=None, since_seq=0):
    """Return derived ask states, one per (ask_seq, addressee).

    Direct chat creates the obligation. Ack or answer closes the escalation
    clock. Escalation events are idempotency/state markers, not closure.
    """
    asks = []
    for rec in records:
        if rec.get("kind") != "chat" or rec.get("arc") == "auto_reply":
            continue
        if _seq(rec) <= since_seq:
            continue
        if rec.get("to") == "all":
            continue
        if not _is_request(rec.get("msg")):
            continue  # only real asks create obligations; statements/FYIs don't
        frm = rec.get("from")
        explicit = _is_explicit_ask(rec.get("msg"))
        for who in _to_list(rec.get("to")):
            if who == frm or not _is_addressee(who):
                continue
            if who == "hemanth" and not explicit:
                continue  # hemanth watches live; only an explicit [ask] to him is tracked
            asks.append({
                "ask_seq": _seq(rec),
                "from": frm,
                "to_agent": who,
                "text": rec.get("msg", ""),
                "age_sec": _age(rec, now_epoch, now_age),
            })

    out = []
    for ask in asks:
        ask_seq = ask["ask_seq"]
        asker = ask["from"]
        who = ask["to_agent"]
        acked = False
        answered = False
        escalated = None

        for rec in records:
            if _seq(rec) <= ask_seq:
                continue
            kind = rec.get("kind")
            if kind == "ack" and rec.get("from") == who and str(rec.get("arc")) == str(ask_seq):
                acked = True
            elif (
                kind == "chat"
                and rec.get("arc") != "auto_reply"
                and rec.get("from") == who
                and asker in _to_list(rec.get("to"))
            ):
                answered = True
            elif kind == "escalate" and _escalation_matches(rec.get("arc"), ask_seq, who):
                target = rec.get("to")
                if target == "hemanth":
                    escalated = "hemanth"
                elif target == "agent0" and escalated != "hemanth":
                    escalated = "agent0"

        if answered:
            state = "answered"
        elif acked:
            state = "acked"
        elif escalated == "hemanth":
            state = "escalated_hemanth"
        elif escalated == "agent0":
            state = "escalated_a0"
        elif ask["age_sec"] >= window:
            state = "owed"
        else:
            state = "open"

        row = dict(ask)
        row.update({"acked": acked, "answered": answered, "escalated": escalated, "state": state})
        out.append(row)
    return sorted(out, key=lambda x: (x["state"] == "answered", -x["age_sec"], x["ask_seq"], x["to_agent"]))


def due_escalations(records, now_epoch, window=WINDOW_SEC, escalate2=ESCALATE2_SEC,
                    now_age=None, since_seq=0, expire=EXPIRE_SEC):
    """Return [(ask_seq, to_agent, level)] for newly due escalation events.

    Three escalation-hygiene gates (Track C #13/#14/#15):
      #13  only genuine asks escalate - text ends with '?' or an explicit [ask]
           tag (see _escalatable); plain chat/posts generate no escalate events.
      #14  a 'no rush'/'no hurry'/equivalent ask is suppressed entirely.
      #15  an ask owed past `expire` seconds without an ack is silently dropped
           (no further escalation), rather than re-escalated.
    """
    due = []
    for ask in compute_asks(records, now_epoch, window, escalate2, now_age, since_seq):
        if ask["age_sec"] >= window + expire:
            continue  # #15 - owed too long with no ack; expire silently
        if not _escalatable(ask["text"]):
            continue  # #13 + #14 - not a genuine, non-deferred ask
        if ask["state"] == "owed":
            due.append((ask["ask_seq"], ask["to_agent"], "agent0"))
        elif ask["state"] == "escalated_a0" and ask["age_sec"] >= window + escalate2:
            due.append((ask["ask_seq"], ask["to_agent"], "hemanth"))
    return due


def _bus_records():
    import sys
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    import office_bus

    out = []
    bus = office_bus.BUS()
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


def asks_now(now_epoch=None, since_seq=0):
    import time
    now_epoch = int(time.time()) if now_epoch is None else now_epoch
    rows = compute_asks(_bus_records(), now_epoch, since_seq=since_seq)
    return [r for r in rows if r["state"] != "answered" or r["age_sec"] <= ANSWERED_GRACE_SEC]


def main(argv):
    print(json.dumps(asks_now(), ensure_ascii=False))


if __name__ == "__main__":
    import sys
    main(sys.argv[1:])
