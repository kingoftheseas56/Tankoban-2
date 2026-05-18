#!/usr/bin/env bash
# Memory-degraded sentinel for Tankoban 2 (Phase 5 of SKILL_DISCIPLINE_FIX_TODO).
#
# Purpose: detect when claude-mem's local store is structurally empty
# (no observations, no corpora) so the SessionStart card can auto-demote
# the /mem-search rule. This prevents the audit's load-bearing failure
# mode where governance demands /mem-search against an empty box.
#
# Usage:
#   bash .claude/scripts/memory-health.sh
#   exit 0 = healthy (observations > 0 AND corpora endpoint responsive)
#   exit 1 = degraded (worker down, observations=0, or corpora missing)
#
# Stdout: one human-readable line summarizing health state.
# Designed cheap (<200ms typical, <1s worst case via curl --max-time 1).
# Always tolerates worker absence — never errors loud.
#
# Used by:
#   - .claude/scripts/session-brief.sh (sources this to condition card output)
#   - manual diagnostics (`bash .claude/scripts/memory-health.sh`)

set +e

# Read configured port from claude-mem settings; default to 37777 if missing.
SETTINGS_FILE="$HOME/.claude-mem/settings.json"
PORT=37777
if [ -f "$SETTINGS_FILE" ]; then
    EXTRACTED="$(grep -oE '"CLAUDE_MEM_WORKER_PORT"\s*:\s*"[0-9]+"' "$SETTINGS_FILE" 2>/dev/null \
        | grep -oE '[0-9]+' | tail -1)"
    [ -n "$EXTRACTED" ] && PORT="$EXTRACTED"
fi

# Probe /api/stats: returns observation/session/summary counts + worker status.
STATS="$(curl -s --max-time 1 "http://127.0.0.1:${PORT}/api/stats" 2>/dev/null || echo "")"

if [ -z "$STATS" ]; then
    echo "DEGRADED: claude-mem worker not responding on port ${PORT} (observations:? corpora:?)"
    exit 1
fi

# Parse observations count + sessions count from JSON (regex-light, no jq).
OBS_COUNT="$(echo "$STATS" | grep -oE '"observations":[0-9]+' | grep -oE '[0-9]+' | head -1)"
OBS_COUNT="${OBS_COUNT:-0}"

SESS_COUNT="$(echo "$STATS" | grep -oE '"sessions":[0-9]+' | grep -oE '[0-9]+' | head -1)"
SESS_COUNT="${SESS_COUNT:-0}"

# Probe /api/corpus: empty array means no built corpora.
CORPUS_RAW="$(curl -s --max-time 1 "http://127.0.0.1:${PORT}/api/corpus" 2>/dev/null || echo "[]")"
CORPUS_EMPTY=0
# Conservative: treat both literal "[]" and {"content":[{"type":"text","text":"[]"}]} as empty.
if echo "$CORPUS_RAW" | grep -qE '^\[\]$|"text":"\[\]"'; then
    CORPUS_EMPTY=1
fi

# -------- Decision logic (claude-mem health + MEMORY.md size) --------

CLAUDE_MEM_DEGRADED=0
CLAUDE_MEM_REASON=""
if [ "$OBS_COUNT" -eq 0 ] || [ "$CORPUS_EMPTY" -eq 1 ]; then
    CLAUDE_MEM_DEGRADED=1
    if [ "$OBS_COUNT" -eq 0 ]; then
        CLAUDE_MEM_REASON="${CLAUDE_MEM_REASON}observations:0 "
    fi
    if [ "$CORPUS_EMPTY" -eq 1 ]; then
        CLAUDE_MEM_REASON="${CLAUDE_MEM_REASON}corpora:empty "
    fi
fi

# MEMORY.md size check (Phase A.3 extension).
MEM_FILE="$HOME/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/MEMORY.md"
MEM_BYTES=0
if [ -f "$MEM_FILE" ]; then
    MEM_BYTES="$(stat -c%s "$MEM_FILE" 2>/dev/null || stat -f%z "$MEM_FILE" 2>/dev/null || echo 0)"
fi
MEM_DEGRADED=0
MEM_REASON=""
if [ "$MEM_BYTES" -gt 32768 ] 2>/dev/null; then
    MEM_DEGRADED=1
    MEM_REASON="MEMORY.md=${MEM_BYTES}b (>32KB RED — run /memory-trim NOW)"
elif [ "$MEM_BYTES" -gt 24986 ] 2>/dev/null; then
    MEM_DEGRADED=1
    MEM_REASON="MEMORY.md=${MEM_BYTES}b (>24.4KB cap — run /memory-trim)"
fi

# Emit single status line covering both axes.
if [ "$CLAUDE_MEM_DEGRADED" -eq 1 ] && [ "$MEM_DEGRADED" -eq 1 ]; then
    echo "DEGRADED: ${CLAUDE_MEM_REASON}+ ${MEM_REASON} (sessions:${SESS_COUNT})"
    exit 1
elif [ "$CLAUDE_MEM_DEGRADED" -eq 1 ]; then
    echo "DEGRADED: ${CLAUDE_MEM_REASON}(sessions:${SESS_COUNT} — pipeline stores prompts but not memory)"
    exit 1
elif [ "$MEM_DEGRADED" -eq 1 ]; then
    echo "DEGRADED: ${MEM_REASON} (claude-mem:healthy observations:${OBS_COUNT})"
    exit 1
fi

echo "HEALTHY: observations:${OBS_COUNT} sessions:${SESS_COUNT} MEMORY.md:${MEM_BYTES}b (corpora present)"
exit 0
