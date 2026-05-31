#!/usr/bin/env bash
# Git post-commit hook -> mirror the new commit into the Office as an activity
# line. Best-effort + always exit 0 (a messaging failure must NEVER break a commit).
HERE="$(cd "$(dirname "$0")" && pwd)"
SHA="$(git rev-parse --short HEAD 2>/dev/null)"
SUBJ="$(git log -1 --pretty=%s 2>/dev/null)"
[ -n "$SHA" ] && python "$HERE/office_bus.py" mirror-commit "$SHA" "$SUBJ" >/dev/null 2>&1
exit 0
