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

cat <<EOF
<system-reminder>
Tankoban 2 brotherhood pre-digest (auto, $(date +%Y-%m-%d)):
- Uncommitted READY TO COMMIT lines: ${PENDING} (run /commit-sweep to batch)
- CONGRESS: ${CONGRESS_STATUS}${CONGRESS_STALE}
- HELP: ${HELP_STATUS}
- chat.md: ${CHAT_LINES} lines${CHAT_WARN}
- STATUS sections >7d stale:${STALE_AGENTS}

Run /brief for the full live dashboard. CLAUDE.md at repo root has the static state.
</system-reminder>
EOF

exit 0
