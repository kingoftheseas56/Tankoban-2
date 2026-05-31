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
