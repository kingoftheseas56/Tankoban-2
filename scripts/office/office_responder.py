#!/usr/bin/env python3
"""The Office — owned-worker fallback responder (reply-only) for one Claude brother.

Pure decision logic (target-aware suppression, @all prefilter, reply formatting,
responder cursor) + a claude -p driver + a dry-run/posting watch loop. The real
brother (his tab) answers first; this steps in only if THAT message stays
unanswered ~FALLBACK_WINDOW, posting a marked, non-binding reply as the brother.

Spec: docs/superpowers/specs/2026-05-31-owned-worker-responder-design.md (v2).
"""
import os
import re
import sys
import json
import time
import shutil
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import office_bus  # noqa: E402  (bus path, _dir, append/cursor logic)

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

FALLBACK_WINDOW = 60     # seconds to let the real tab answer before backing up
REPLY_CLASSES = ("ack", "handoff", "clarifying_question",
                 "nonbinding_assessment", "decline_owner_confirmation_required")


def _to_list(to):
    return [t.strip() for t in str(to).split(",") if t.strip()]


def should_suppress(trigger, bus_records, me):
    """Target-aware: suppress the backup ONLY if `me` plausibly answered THIS thread
    after the trigger — i.e. posted a real chat addressed back to the trigger's
    sender. An unrelated post, an activity line, or silence does NOT suppress
    (that was the v1 bug Agent 7 caught). Returns (suppress: bool, reason: str)."""
    sender = trigger.get("frm")
    for r in bus_records:
        if int(r.get("seq", 0)) <= int(trigger.get("seq", 0)):
            continue
        if r.get("from") != me:
            continue
        if r.get("kind") not in (None, "chat"):   # activity/blocked aren't answers
            continue
        if sender in _to_list(r.get("to")):
            return True, "answered: {0} -> {1} at seq {2}".format(me, sender, r.get("seq"))
    return False, "no plausible answer from {0} to {1}".format(me, sender)


def is_candidate(rec, me):
    """Is this message a fallback candidate for `me`? Direct/comma-list to me: always.
    @all: ONLY if it explicitly names me (@agentN or 'agent N') — a generic broadcast
    does NOT make every brother's responder fire (Agent 7's anti-fan-out gate).
    Never my own posts; never non-chat kinds."""
    if rec.get("from") == me:
        return False
    if rec.get("kind") not in (None, "chat", "blocked"):
        return False
    to = str(rec.get("to", ""))
    if me in _to_list(to):
        return True
    if to == "all":
        msg = str(rec.get("msg", "")).lower()
        num = me[len("agent"):]
        if "@" + me in msg:
            return True
        if re.search(r"\bagent\s*#?\s*" + re.escape(num) + r"\b", msg):
            return True
    return False


def format_backup_reply(me, reply_class, body):
    """Build the marked, honest backup message. Substantive classes get a
    non-binding qualifier so the backup can never commit the real brother."""
    if reply_class not in REPLY_CLASSES:
        raise ValueError("unknown reply class: {0!r}".format(reply_class))
    prefix = "[auto · {0}'s tab idle · {1}]".format(me, reply_class)
    body = " ".join(str(body).split())
    if reply_class in ("nonbinding_assessment", "decline_owner_confirmation_required"):
        if not re.search(r"owner to confirm|non-binding|backup read", body, re.IGNORECASE):
            body = "backup read (non-binding, owner to confirm): " + body
    return "{0} {1}".format(prefix, body)
