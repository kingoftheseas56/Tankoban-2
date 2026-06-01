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


def _escalation_matches(arc, ask_seq, to_agent):
    arc = str(arc or "")
    return arc == "{0}:{1}".format(ask_seq, to_agent) or arc == str(ask_seq)


def compute_asks(records, now_epoch, window=WINDOW_SEC, escalate2=ESCALATE2_SEC, now_age=None):
    """Return derived ask states, one per (ask_seq, addressee).

    Direct chat creates the obligation. Ack or answer closes the escalation
    clock. Escalation events are idempotency/state markers, not closure.
    """
    asks = []
    for rec in records:
        if rec.get("kind") != "chat" or rec.get("arc") == "auto_reply":
            continue
        if rec.get("to") == "all":
            continue
        frm = rec.get("from")
        for who in _to_list(rec.get("to")):
            if who == frm or not _is_addressee(who):
                continue
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


def due_escalations(records, now_epoch, window=WINDOW_SEC, escalate2=ESCALATE2_SEC, now_age=None):
    """Return [(ask_seq, to_agent, level)] for newly due escalation events."""
    due = []
    for ask in compute_asks(records, now_epoch, window, escalate2, now_age):
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


def asks_now(now_epoch=None):
    import time
    now_epoch = int(time.time()) if now_epoch is None else now_epoch
    rows = compute_asks(_bus_records(), now_epoch)
    return [r for r in rows if r["state"] != "answered" or r["age_sec"] <= ANSWERED_GRACE_SEC]


def main(argv):
    print(json.dumps(asks_now(), ensure_ascii=False))


if __name__ == "__main__":
    import sys
    main(sys.argv[1:])
