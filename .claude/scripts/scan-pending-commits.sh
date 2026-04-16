#!/usr/bin/env bash
# Scan agents/chat.md for READY TO COMMIT lines added since the last [Agent 0, chat.md sweep] commit.
# Output: integer count on stdout. Designed to be cheap (< 100ms) and fail-silent for hook use.
#
# Used by:
#  - .claude/scripts/session-brief.sh  (for SessionStart hook)
#  - /commit-sweep slash command  (for the actual sweep — it re-uses this logic but also stages/commits)

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

# Find last sweep commit
SWEEP_SHA="$(git log --grep='chat.md sweep' -n 1 --format='%H' 2>/dev/null || true)"

if [ -z "$SWEEP_SHA" ]; then
    # No prior sweep — scan all of chat.md
    COUNT="$(grep -cE '^READY TO COMMIT [^a-zA-Z0-9 ]' agents/chat.md 2>/dev/null || true)"
    echo "${COUNT:-0}"
    exit 0
fi

# Get blob hash of chat.md at that commit
SWEEP_BLOB="$(git rev-parse "${SWEEP_SHA}:agents/chat.md" 2>/dev/null || true)"

if [ -z "$SWEEP_BLOB" ]; then
    # Sweep commit didn't touch chat.md — fallback to all
    COUNT="$(grep -cE '^READY TO COMMIT [^a-zA-Z0-9 ]' agents/chat.md 2>/dev/null || true)"
    echo "${COUNT:-0}"
    exit 0
fi

# Diff blob -> current chat.md, count + (added) READY TO COMMIT lines
COUNT="$(git diff "$SWEEP_BLOB" -- agents/chat.md 2>/dev/null | grep -cE '^\+READY TO COMMIT [^a-zA-Z0-9 ]' || true)"
echo "${COUNT:-0}"
