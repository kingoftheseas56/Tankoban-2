#!/usr/bin/env bash
# SessionStart hook for Tankoban 2.
# Prints a 5-7 line system-reminder digest of brotherhood state to stdout.
# Stdout from SessionStart hooks is injected into the session as a system-reminder.
# Target runtime: < 500ms total. Always exit 0 — never block session start.

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT" 2>/dev/null || exit 0

# Pending commit count (cheap)
PENDING="$(bash "$REPO_ROOT/.claude/scripts/scan-pending-commits.sh" 2>/dev/null || echo "?")"

# CONGRESS state (read first 30 lines)
CONGRESS_STATUS="?"
CONGRESS_STALE=""
if [ -f agents/CONGRESS.md ]; then
    CONGRESS_STATUS="$(head -30 agents/CONGRESS.md | grep -oE 'STATUS: [A-Z ]+' | head -1 | sed 's/STATUS: //' || echo '?')"
    if [ "$CONGRESS_STATUS" = "OPEN" ]; then
        if grep -qE 'ratified|APPROVES|Final Word|Execute' agents/CONGRESS.md 2>/dev/null; then
            CONGRESS_STALE=" [STALE — needs archive]"
        fi
    fi
fi

# HELP state
HELP_STATUS="?"
if [ -f agents/HELP.md ]; then
    HELP_STATUS="$(head -10 agents/HELP.md | grep -oE 'STATUS: [A-Z ]+' | head -1 | sed 's/STATUS: //' || echo '?')"
fi

# Chat.md size
CHAT_LINES=0
CHAT_WARN=""
if [ -f agents/chat.md ]; then
    CHAT_LINES="$(wc -l < agents/chat.md 2>/dev/null || echo 0)"
    if [ "$CHAT_LINES" -gt 3000 ] 2>/dev/null; then
        CHAT_WARN=" [ROTATION DUE — run /rotate-chat]"
    fi
fi

# Stale STATUS sections (Last session > 7 days)
STALE_AGENTS=""
if [ -f agents/STATUS.md ]; then
    TODAY_EPOCH="$(date +%s)"
    SEVEN_DAYS=$((7 * 86400))
    while IFS= read -r line; do
        DATE_PART="$(echo "$line" | grep -oE '[0-9]{4}-[0-9]{2}-[0-9]{2}' | head -1)"
        if [ -n "$DATE_PART" ]; then
            ENTRY_EPOCH="$(date -d "$DATE_PART" +%s 2>/dev/null || echo 0)"
            if [ "$ENTRY_EPOCH" -gt 0 ] && [ "$((TODAY_EPOCH - ENTRY_EPOCH))" -gt "$SEVEN_DAYS" ]; then
                STALE_AGENTS="${STALE_AGENTS} ${DATE_PART}"
            fi
        fi
    done < <(grep '^Last session:' agents/STATUS.md 2>/dev/null)
fi
[ -z "$STALE_AGENTS" ] && STALE_AGENTS=" none"

# Memory-degraded sentinel (Phase 5 of SKILL_DISCIPLINE_FIX_TODO).
# Probes claude-mem health; if degraded, demotes /mem-search rule + shows banner.
MEMORY_STATUS=""
MEMORY_DEGRADED=0
if [ -x "$REPO_ROOT/.claude/scripts/memory-health.sh" ]; then
    # Capture stdout regardless of exit code; non-zero exit IS the degraded signal.
    MEMORY_STATUS="$(bash "$REPO_ROOT/.claude/scripts/memory-health.sh" 2>/dev/null)"
    [ -z "$MEMORY_STATUS" ] && MEMORY_STATUS="DEGRADED: probe-failed (script not runnable)"
    if echo "$MEMORY_STATUS" | grep -q '^DEGRADED'; then
        MEMORY_DEGRADED=1
    fi
fi

# Conditional /mem-search line: full rule if healthy, demoted note if degraded.
if [ "$MEMORY_DEGRADED" -eq 1 ]; then
    MEM_SEARCH_LINE="- /claude-mem:mem-search: AUTO-DEMOTED (memory pipeline degraded — fall back to chat_archive dig until repaired; see SKILL_DISCIPLINE_FIX_TODO §6 Phase 5)"
else
    MEM_SEARCH_LINE="- On \"did we solve this before?\": /claude-mem:mem-search BEFORE chat_archive dig"
fi

# Optional banner at top of digest if memory degraded.
MEMORY_BANNER=""
if [ "$MEMORY_DEGRADED" -eq 1 ]; then
    MEMORY_BANNER="
=== MEMORY DEGRADED ===
${MEMORY_STATUS}
Skill-discipline rules referencing /mem-search are auto-relaxed for this session.
"
fi

cat <<EOF
<system-reminder>
Tankoban 2 brotherhood pre-digest (auto, $(date +%Y-%m-%d)):
- Uncommitted READY TO COMMIT lines: ${PENDING} (run /commit-sweep to batch)
- CONGRESS: ${CONGRESS_STATUS}${CONGRESS_STALE}
- HELP: ${HELP_STATUS}
- chat.md: ${CHAT_LINES} lines${CHAT_WARN}
- STATUS sections >7d stale:${STALE_AGENTS}
${MEMORY_BANNER}

=== REQUIRED SKILL TRIGGERS (invoke at each trigger; CLAUDE.md "Required Skills & Protocols" for full list of 21) ===
- Session start: /brief (full dashboard)
- Before ANY RTC: /superpowers:verification-before-completion + /simplify + /build-verify (if src/ or native_sidecar/src/ touched) + /superpowers:requesting-code-review
- Before RTC on stream / torrent / sidecar / user-input: /security-review
- On bug / test failure / unexpected behavior: /superpowers:systematic-debugging FIRST (before proposing fixes)
- On feature scoping OR Congress position draft: /superpowers:brainstorming
- On plan authoring / execution (~/.claude/plans/*.md): /superpowers:writing-plans + /superpowers:executing-plans
- On correction from Hemanth or Agent 7 audit: /superpowers:receiving-code-review
${MEM_SEARCH_LINE}
- On structural code queries: /claude-mem:smart-explore

=== RTC PROVENANCE (contracts-v3, since 2026-04-25) ===
Non-trivial RTCs (≥1 src/ file or ≥30 LOC) MUST include a "Skills invoked: [/skill1, /skill2, ...]" field
between the message body and "| files:" — see CONTRACTS.md § Skill Provenance in RTCs.
Trivial RTCs (doc-only / governance-only / single-line) may omit. Phase 4 pre-RTC hook will nag (not block)
on missing fields for the first 30 days post-ship. Honest under-listing > dishonest padding.

Run /brief for the full live dashboard. CLAUDE.md at repo root has the static state + full skill-trigger map.
</system-reminder>
EOF

exit 0
