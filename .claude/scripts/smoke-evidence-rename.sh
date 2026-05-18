#!/usr/bin/env bash
# Stop-hook helper for Tankoban 2 — smoke-evidence naming + bundle reconciliation.
#
# Purpose: when an agent adds files to agents/audits/smoke_evidence/ during a
# turn, validate naming convention. Emits warnings via a single system-reminder
# block when violations are found.
#
# Mode: WARN-ONLY. Always exits 0. Never blocks agent turn.
#
# Convention:
#   - PNG screenshot: <UPPERCASE_FINDING>_<HHMMSS>.png  OR  <slug>_NN.png
#   - The /smoke-package command produces compliant names by default

set +e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT" 2>/dev/null || exit 0

[ -d agents/audits/smoke_evidence ] || exit 0

ADDED_FILES="$(git status --porcelain agents/audits/smoke_evidence 2>/dev/null \
    | grep -E '^(\?\?|A |M )' \
    | awk '{print $2}')"

[ -z "$ADDED_FILES" ] && exit 0

WARN_LINES=""

while IFS= read -r FILE; do
    [ -z "$FILE" ] && continue
    BASENAME="$(basename "$FILE")"
    EXT="${BASENAME##*.}"

    case "$EXT" in
        png|jpg|jpeg|gif|webp)
            if ! echo "$BASENAME" | grep -qE '^([A-Z][A-Z0-9_]+_[0-9]{6}|[a-z0-9_-]+_[0-9]{2,3})\.(png|jpg|jpeg|gif|webp)$'; then
                WARN_LINES="${WARN_LINES}  - ${FILE} — does not match <UPPERCASE>_<HHMMSS>.png OR <slug>_NN.png\n"
            fi
            ;;
        log|md|jsonl)
            :
            ;;
    esac
done <<< "$ADDED_FILES"

if [ -n "$WARN_LINES" ]; then
    cat <<EOF
<system-reminder>
[smoke-evidence-rename] Smoke evidence files added this turn that do not match Tankoban's naming convention:
$(printf "%b" "$WARN_LINES")
Convention: <UPPERCASE_FINDING>_<HHMMSS>.png (one-off) or <slug>_NN.png (sequence). Use /smoke-package <finding-name> to scaffold compliant names automatically. Non-blocking warn; commit will still succeed.
</system-reminder>
EOF
fi

exit 0
