#!/usr/bin/env bash
# UserPromptSubmit hook for Tankoban 2.
# If CONGRESS.md is OPEN but contains a Hemanth ratification line, inject a system-reminder
# asking Agent 0 to archive. Debounced once per day per congress via .claude/.congress-warned-<N> markers.
# Target runtime: < 100ms. Always exit 0 — never block prompt submission.

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT" 2>/dev/null || exit 0

CONGRESS="agents/CONGRESS.md"
[ -f "$CONGRESS" ] || exit 0

STATUS="$(head -30 "$CONGRESS" | grep -oE 'STATUS: [A-Z ]+' | head -1 | sed 's/STATUS: //' || echo '')"
[ "$STATUS" = "OPEN" ] || exit 0

# Find the congress number (## CONGRESS N — STATUS: OPEN)
CONGRESS_NUM="$(head -30 "$CONGRESS" | grep -oE '## CONGRESS [0-9]+' | head -1 | grep -oE '[0-9]+' || echo 'unknown')"

# Check for ratification markers anywhere in the file
if ! grep -qE 'ratified|APPROVES|Final Word|Execute' "$CONGRESS" 2>/dev/null; then
    exit 0  # OPEN, but no ratification yet — normal in-flight state
fi

# Stale state: OPEN + ratification present. Check if we already warned today.
MARKER=".claude/.congress-warned-${CONGRESS_NUM}-$(date +%Y-%m-%d)"
[ -f "$MARKER" ] && exit 0

mkdir -p .claude 2>/dev/null
touch "$MARKER"

cat <<EOF
<system-reminder>
Congress ${CONGRESS_NUM} appears RATIFIED (Hemanth posted ratification line) but CONGRESS.md still says STATUS: OPEN. Agent 0: archive to agents/congress_archive/YYYY-MM-DD_<topic>.md and reset CONGRESS.md to empty template — same-session per the auto-close rule in agents/GOVERNANCE.md "CONGRESS Protocol".
</system-reminder>
EOF

exit 0
