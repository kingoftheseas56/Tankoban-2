#!/usr/bin/env bash
# Pre-RTC checker hook for Tankoban 2 (Phase 4 of SKILL_DISCIPLINE_FIX_TODO).
#
# Purpose: when an agent finishes a turn that added one or more `READY TO COMMIT`
# lines to agents/chat.md, scan each new line and warn if it's "non-trivial"
# (per contracts-v3 threshold) but missing the `Skills invoked:` field.
#
# Mode: NAG-ONLY. The script always exits 0 and never blocks the agent's turn.
# Promote-to-block decision deferred until 30-day compliance data lands per §5
# question 3 ratification.
#
# Telemetry: each missing-field nag fires one append row to
# .claude/telemetry/skill-discipline.jsonl for the 30-day re-measurement wake.
#
# Hook event: Stop. Hard 5s timeout configured in .claude/settings.json.
#
# Failure modes: any error path exits 0 silently. A broken nag must never
# degrade an agent's session experience.

set +e  # Tolerate sub-command failures; we report and move on.

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT" 2>/dev/null || exit 0

[ -d agents ] || exit 0  # Not a Tankoban repo checkout — silent exit.
[ -f agents/chat.md ] || exit 0

TELEMETRY_DIR=".claude/telemetry"
TELEMETRY_FILE="${TELEMETRY_DIR}/skill-discipline.jsonl"
mkdir -p "$TELEMETRY_DIR" 2>/dev/null

# -------- Step 1: extract added RTC lines since HEAD --------
# Compare working-tree chat.md against HEAD (last commit). Anything added in
# the +diff is a candidate. This catches all uncommitted RTCs every turn —
# nag is self-clearing the moment the agent edits the field in.

ADDED_RTCS="$(git diff HEAD -- agents/chat.md 2>/dev/null \
    | grep -E '^\+READY TO COMMIT [—-]' \
    | sed 's/^+//')"

[ -z "$ADDED_RTCS" ] && exit 0  # No new RTCs this turn — silent exit.

# -------- Step 2: parse + classify each RTC --------
TS="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
NAG_COUNT=0
NAG_LINES=""

# Read each RTC line; bash IFS-newline iteration over heredoc.
while IFS= read -r LINE; do
    [ -z "$LINE" ] && continue

    # Parse tag (between first `[` and matching `]:`).
    TAG="$(echo "$LINE" | sed -nE 's/^READY TO COMMIT [—-] \[([^]]+)\]:.*/\1/p')"
    [ -z "$TAG" ] && continue  # Malformed line; skip silently.

    # Parse files (everything after the last `| files:`).
    FILES_RAW="$(echo "$LINE" | sed -nE 's/.*\| files:\s*(.+)$/\1/p')"
    [ -z "$FILES_RAW" ] && continue  # No files: field; not a valid RTC.

    # Detect Skills invoked field (anywhere between `]:` and `| files:`).
    SKILLS_PRESENT=0
    if echo "$LINE" | grep -qE '\| Skills invoked:\s*\[/'; then
        SKILLS_PRESENT=1
    fi

    # Split files on `,` and trim. Determine non-trivial.
    SRC_TOUCHED=0
    LOC_CHANGED=0
    FILES_COUNT=0

    OLD_IFS="$IFS"
    IFS=','
    for F in $FILES_RAW; do
        F_TRIMMED="$(echo "$F" | xargs)"
        [ -z "$F_TRIMMED" ] && continue
        FILES_COUNT=$((FILES_COUNT + 1))

        # Strip "(NEW)" / "(DELETED)" / similar trailing markers.
        F_PATH="$(echo "$F_TRIMMED" | sed -E 's/\s*\([A-Z]+\)\s*$//')"

        case "$F_PATH" in
            src/*|native_sidecar/src/*)
                SRC_TOUCHED=1
                ;;
        esac
    done
    IFS="$OLD_IFS"

    # LOC threshold: sum insertions + deletions across all listed files.
    # Only invoke git if we haven't already qualified as non-trivial via src/.
    if [ "$SRC_TOUCHED" -eq 0 ]; then
        # Reconstruct file list as space-separated for git
        OLD_IFS="$IFS"
        IFS=','
        FILE_ARGS=""
        for F in $FILES_RAW; do
            F_TRIMMED="$(echo "$F" | xargs)"
            F_PATH="$(echo "$F_TRIMMED" | sed -E 's/\s*\([A-Z]+\)\s*$//')"
            [ -z "$F_PATH" ] && continue
            # Only count files that actually exist in working tree (skip DELETED).
            [ -f "$F_PATH" ] && FILE_ARGS="$FILE_ARGS $F_PATH"
        done
        IFS="$OLD_IFS"

        if [ -n "$FILE_ARGS" ]; then
            STATS="$(git diff --shortstat HEAD -- $FILE_ARGS 2>/dev/null || echo "")"
            INS="$(echo "$STATS" | grep -oE '[0-9]+ insertion' | grep -oE '[0-9]+' | head -1)"
            DEL="$(echo "$STATS" | grep -oE '[0-9]+ deletion'  | grep -oE '[0-9]+' | head -1)"
            INS="${INS:-0}"
            DEL="${DEL:-0}"
            LOC_CHANGED=$((INS + DEL))
        fi
    fi

    # Non-trivial = src/ touched OR ≥30 LOC changed.
    NON_TRIVIAL=0
    if [ "$SRC_TOUCHED" -eq 1 ] || [ "$LOC_CHANGED" -ge 30 ]; then
        NON_TRIVIAL=1
    fi

    # Nag check: non-trivial AND skills field absent.
    if [ "$NON_TRIVIAL" -eq 1 ] && [ "$SKILLS_PRESENT" -eq 0 ]; then
        NAG_COUNT=$((NAG_COUNT + 1))
        NAG_LINES="${NAG_LINES}  - [${TAG}]\n"

        # Append telemetry row (JSONL).
        # Escape tag for JSON (basic: replace " with \").
        TAG_JSON="$(echo "$TAG" | sed 's/"/\\"/g')"
        printf '{"ts":"%s","event":"missing_skills_invoked","tag":"%s","files_count":%d,"src_touched":%s,"loc_changed":%d}\n' \
            "$TS" "$TAG_JSON" "$FILES_COUNT" \
            "$([ "$SRC_TOUCHED" -eq 1 ] && echo true || echo false)" \
            "$LOC_CHANGED" \
            >> "$TELEMETRY_FILE" 2>/dev/null
    fi
done <<< "$ADDED_RTCS"

# -------- Step 3: emit nag warning if any --------
if [ "$NAG_COUNT" -gt 0 ]; then
    cat <<EOF
<system-reminder>
[pre-rtc-checker, contracts-v3 nag-only mode] $NAG_COUNT non-trivial RTC(s) in working-tree chat.md missing 'Skills invoked: [/skill1, /skill2, ...]' field:
$(printf "%b" "$NAG_LINES")
Required for non-trivial RTCs (≥1 file under src/ or native_sidecar/src/, OR ≥30 LOC). See agents/CONTRACTS.md § Skill Provenance in RTCs for the format. Trivial RTCs (doc-only, governance-only, single-line) may omit. Nag-only first 30 days; promote-to-block deferred per SKILL_DISCIPLINE_FIX_TODO §5 question 3.
</system-reminder>
EOF
fi

exit 0
