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


def _responder_cursor_path(me):
    return os.path.join(office_bus._dir(), ".bus_responder_cursors", me + ".seq")


def responder_cursor(me):
    try:
        with open(_responder_cursor_path(me), "r", encoding="utf-8") as f:
            return int(f.read().strip())
    except (OSError, ValueError):
        return 0


def set_responder_cursor(me, seq):
    p = _responder_cursor_path(me)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w", encoding="utf-8") as f:
        f.write(str(int(seq)))


def _bus_records():
    bus = office_bus.BUS()
    out = []
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


def find_candidates(records, me, after_seq):
    out = []
    for r in records:
        if int(r.get("seq", 0)) <= after_seq:
            continue
        if is_candidate(r, me):
            out.append({"seq": int(r["seq"]), "frm": r.get("from"),
                        "to": r.get("to"), "text": r.get("msg", "")})
    return out


def log(me, msg):
    print("[responder {0}] {1}".format(me, msg), flush=True)


def run(me, dry_run=True, model="claude", once=False, window=FALLBACK_WINDOW):
    """Watch loop. Dry-run logs would-post decisions and posts nothing."""
    last = responder_cursor(me) or max((int(r.get("seq", 0)) for r in _bus_records()), default=0)
    set_responder_cursor(me, last)
    log(me, "fallback responder live (dry_run={0}, window={1}s) from seq {2}".format(dry_run, window, last))
    pending = []  # list of (trigger, due_epoch)
    while True:
        now = int(time.time())
        records = _bus_records()
        for t in find_candidates(records, me, last):
            last = max(last, t["seq"])
            pending.append((t, now + window))
            log(me, "candidate seq {0} from {1}: '{2}' — waiting {3}s for the real brother".format(
                t["seq"], t["frm"], t["text"][:50], window))
        set_responder_cursor(me, last)
        still = []
        for t, due in pending:
            if now < due:
                still.append((t, due))
                continue
            sup, reason = should_suppress(t, records, me)
            if sup:
                log(me, "seq {0}: SUPPRESS — {1}".format(t["seq"], reason))
            else:
                handle_due_trigger(me, t, records, dry_run, model)
        pending = still
        if once:
            return
        time.sleep(3)


def _claude_bin():
    """Resolve the claude CLI (npm-global claude.CMD on Windows; not on bare PATH)."""
    found = shutil.which("claude")
    if found:
        return found
    for cand in (os.path.join(os.environ.get("APPDATA", ""), "npm", "claude.cmd"),
                 os.path.join(os.path.expanduser("~"), "AppData", "Roaming", "npm", "claude.cmd")):
        if cand and os.path.exists(cand):
            return cand
    return "claude"


def build_prompt(me, trigger, records):
    recent = [{"seq": r.get("seq"), "from": r.get("from"), "to": r.get("to"),
               "msg": str(r.get("msg", ""))[:300]} for r in records[-12:]]
    payload = {"you_are_backup_for": me, "unanswered_message": trigger, "recent_context": recent}
    return (
        "You are the AUTOMATED BACKUP for {0} in THE OFFICE (an AI-agent coordination room). "
        "{0}'s interactive tab went idle and did NOT answer the message below within {1}s. "
        "Decide whether a brief, NON-BINDING backup reply is useful.\n\n"
        "Output ONLY a JSON object — no prose, no markdown fences — exactly this shape:\n"
        '{{"replies": [{{"to": "@agentN or @hemanth", "class": "<ack|handoff|clarifying_question'
        '|nonbinding_assessment|decline_owner_confirmation_required>", "msg": "<under 240 chars>"}}]}}\n'
        'Return {{"replies": []}} if no reply is useful.\n\n'
        "HARD RULES:\n"
        "- You are NOT {0}. NEVER commit {0} to a domain decision, an implementation promise, or a "
        "position he didn't take. For anything substantive use class 'nonbinding_assessment' or "
        "'decline_owner_confirmation_required', phrased as a backup read for the owner to confirm.\n"
        "- Do not echo wake prompts or acknowledge generic broadcasts. Max 2 replies.\n\n"
        "Office payload:\n{2}"
    ).format(me, FALLBACK_WINDOW, json.dumps(payload, ensure_ascii=False, indent=2))


def _extract_replies(out):
    """Pull {replies:[...]} from claude -p output. --output-format json wraps the
    model text in {'type':'result','result':'<text>'}; the text may be bare JSON or
    fenced. Be liberal."""
    texts = [out]
    try:
        wrap = json.loads(out)
        if isinstance(wrap, dict):
            if isinstance(wrap.get("replies"), list):
                return wrap["replies"]
            so = wrap.get("structured_output")
            if isinstance(so, dict) and isinstance(so.get("replies"), list):
                return so["replies"]
            if isinstance(wrap.get("result"), str):
                texts.append(wrap["result"])
    except (json.JSONDecodeError, TypeError):
        pass
    for t in texts:
        if not isinstance(t, str):
            continue
        i, j = t.find("{"), t.rfind("}")
        if i != -1 and j > i:
            try:
                o = json.loads(t[i:j + 1])
                if isinstance(o, dict) and isinstance(o.get("replies"), list):
                    return o["replies"]
            except (json.JSONDecodeError, TypeError):
                pass
    return None


def run_claude(prompt, model="claude", timeout=120):
    """Invoke the contract-tested claude -p command (gate: plan-mode read-only +
    json output-format; model JSON lands in the 'result' wrapper, parsed liberally)."""
    cmd = [_claude_bin(), "-p", "--no-session-persistence", "--permission-mode", "plan",
           "--strict-mcp-config", "--output-format", "json"]
    proc = subprocess.run(cmd, input=prompt, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "claude -p failed")
    replies = _extract_replies(proc.stdout.strip())
    if replies is None:
        raise RuntimeError("could not parse replies from: " + proc.stdout.strip()[:300])
    return replies


def handle_due_trigger(me, trigger, records, dry_run, model):
    set_responder_cursor(me, max(responder_cursor(me), trigger["seq"]))  # consume before draft
    try:
        replies = run_claude(build_prompt(me, trigger, records), model)
    except Exception as exc:
        log(me, "seq {0}: backup-failed — {1}".format(trigger["seq"], exc))
        office_bus.cmd_append("system", "all", "activity", "null",
                              "[responder] backup-failed for {0} seq {1}".format(me, trigger["seq"]))
        return
    if not replies:
        log(me, "seq {0}: model chose no reply".format(trigger["seq"]))
        return
    for rep in replies[:2]:
        try:
            body = format_backup_reply(me, rep.get("class", ""), rep.get("msg", ""))
        except ValueError as e:
            log(me, "seq {0}: bad reply class — {1}".format(trigger["seq"], e))
            continue
        if dry_run:
            log(me, "seq {0}: DRY-RUN would post to {1}: {2}".format(trigger["seq"], rep.get("to"), body))
        else:
            post_reply(me, rep.get("to"), body, trigger, records)


def post_reply(me, to, body, trigger, records):
    log(me, "posting not enabled yet (Task 8)")
