#!/usr/bin/env bash
# The Office — send a message to a brother (or @all).
# Usage: chat_send.sh "@agent4" "message"   |   chat_send.sh "@all" "broadcast"
# FROM is resolved from this tab's registered identity (auto-bound at first prompt,
# or run office_join.sh <N> to set it explicitly).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SID="${CLAUDE_CODE_SESSION_ID:-${CLAUDE_SESSION_ID:-}}"
[ -z "$SID" ] && { echo "chat_send: no session id (CLAUDE_CODE_SESSION_ID unset)" >&2; exit 1; }
TO="${1:-}"; MSG="${2:-}"
[ -z "$TO" ] || [ -z "$MSG" ] && { echo "usage: chat_send.sh \"@agentN|@all\" \"message\"" >&2; exit 1; }
python "$HERE/office_bus.py" send "$SID" "$TO" "$MSG"
