#!/usr/bin/env bash
# The Office — SUMMON a brother to do work (not just chat).
# Usage: office_summon.sh "@agent4" "stuck on X, need your streaming eyes — do Y"
#
# A summon is a kind="summon" bus message. The dispatcher (office_dispatch.py)
# routes it: if the target's tab is live-watching, his tab gets woken; if his tab
# is closed/idle, he is spun up as a BACKGROUND headless session that loads his
# identity, does the task, posts the result to the Office, and exits (tight leash:
# posts, never commits to master; cannot summon others).
#
# FROM is resolved from this tab's registered identity (same as chat_send.sh).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SID="${CLAUDE_CODE_SESSION_ID:-${CLAUDE_SESSION_ID:-}}"
[ -z "$SID" ] && { echo "office_summon: no session id (CLAUDE_CODE_SESSION_ID unset)" >&2; exit 1; }
TO="${1:-}"; TASK="${2:-}"
[ -z "$TO" ] || [ -z "$TASK" ] && { echo "usage: office_summon.sh \"@agentN\" \"task to do\"" >&2; exit 1; }
python "$HERE/office_bus.py" summon-send "$SID" "$TO" "$TASK"
