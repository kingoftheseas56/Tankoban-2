#!/usr/bin/env bash
# The Office delivery hook (wired to UserPromptSubmit).
# Delegates to office_bus.py deliver, which: reads the hook stdin JSON
# (session_id + prompt), auto-binds this tab's agent identity from the prompt
# if needed, injects unseen bus messages as additionalContext JSON, and
# advances the per-agent cursor. Always exits 0 — never blocks prompt submission.
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
python "$REPO_ROOT/scripts/office/office_bus.py" deliver 2>/dev/null
exit 0
