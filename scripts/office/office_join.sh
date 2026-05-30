#!/usr/bin/env bash
# The Office — explicitly bind this tab to an agent number.
# Usage: office_join.sh <agent_number>
# Normally unnecessary (the delivery hook auto-binds from your wake prompt);
# use this if auto-detect missed or you want to override.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SID="${CLAUDE_CODE_SESSION_ID:-${CLAUDE_SESSION_ID:-}}"
[ -z "$SID" ] && { echo "office_join: no session id (CLAUDE_CODE_SESSION_ID unset)" >&2; exit 1; }
NUM="${1:-}"
[ -z "$NUM" ] && { echo "usage: office_join.sh <agent_number>" >&2; exit 1; }
python "$HERE/office_bus.py" join "$SID" "$NUM"
echo "office: this tab registered as agent${NUM} (session ${SID})"
