#!/usr/bin/env bash
# The Office delivery hook (wired to UserPromptSubmit).
# Delegates to office_bus.py deliver, which: reads the hook stdin JSON
# (session_id + prompt), auto-binds this tab's agent identity from the prompt
# if needed, injects unseen bus messages as additionalContext JSON, and
# advances the per-agent cursor. Always exits 0 — never blocks prompt submission.

# Engine guard (2026-05-30, Agent 0 — Office cross-engine infra). Same rationale as
# session-brief.sh / congress-check.sh: a non-Anthropic endpoint (DeepSeek / Agent 9)
# rejects injected additionalContext as a `system`-role messages entry
# (400 ... unknown variant system). Skip the hook on those wires so the Office never
# 400-breaks a DeepSeek tab mid-turn. DeepSeek/Codex brothers read the bus via
# `office_bus.py drain <agentN>` + the office_watch.sh watch instead of hook-injection.
case "${ANTHROPIC_BASE_URL:-}" in
  *deepseek*) exit 0 ;;
esac

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
python "$REPO_ROOT/scripts/office/office_bus.py" deliver 2>/dev/null
exit 0
