#!/usr/bin/env bash
# The Office — raise a BLOCKER / be honest about being stuck (real-talk lane).
# Usage: office_flag.sh "what you're blocked on"
# FROM is resolved from this tab's registered identity.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SID="${CLAUDE_CODE_SESSION_ID:-${CLAUDE_SESSION_ID:-}}"
[ -z "$SID" ] && { echo "office_flag: no session id (CLAUDE_CODE_SESSION_ID unset)" >&2; exit 1; }
MSG="${1:-}"
[ -z "$MSG" ] && { echo "usage: office_flag.sh \"what you're blocked on\"" >&2; exit 1; }
python "$HERE/office_bus.py" flag "$SID" "$MSG"
