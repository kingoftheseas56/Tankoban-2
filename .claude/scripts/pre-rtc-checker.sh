#!/usr/bin/env bash
# Pre-RTC checker hook for Tankoban 2 (Phase 4 of SKILL_DISCIPLINE_FIX_TODO).
#
# Purpose: when an agent finishes a turn that added one or more `READY TO COMMIT`
# lines to agents/chat.md, scan each new line and warn if it is "non-trivial"
# (per contracts-v3 threshold) but missing the `Skills invoked:` field.
#
# Mode: NAG-ONLY. The script always exits 0 and never blocks the agent's turn.
#
# Design notes, 2026-05-21 rewrite:
# - Single pass over added RTC lines. Nag classification, scaffold generation,
#   stale-file detection, and telemetry all happen together per RTC.
# - Cache `git diff HEAD -- agents/chat.md` once at startup, then derive added
#   RTCs from that cached diff.
# - Cache working-tree diff metadata once (`--name-only` and `--numstat`) and
#   reuse it for LOC classification and stale-file detection instead of running
#   per-RTC git diff loops.
# - Telemetry is de-duplicated at write time by `(short HEAD sha, tag hash)`.
#   The monotonic index is `.claude/telemetry/skill-discipline.seen`, with rows
#   shaped as `<short-sha>:<tag-md5>`. Existing JSONL history is never rewritten.
#
# Hook event: Stop. Hard 5s timeout configured in .claude/settings.json.
#
# Failure modes: any error path exits 0 silently. A broken nag must never
# degrade an agent's session experience.

set +e  # Tolerate sub-command failures; we report and move on.

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT" 2>/dev/null || exit 0

[ -d agents ] || exit 0  # Not a Tankoban repo checkout; silent exit.
[ -f agents/chat.md ] || exit 0

TELEMETRY_DIR=".claude/telemetry"
TELEMETRY_FILE="${TELEMETRY_DIR}/skill-discipline.jsonl"
SEEN_FILE="${TELEMETRY_DIR}/skill-discipline.seen"
mkdir -p "$TELEMETRY_DIR" 2>/dev/null
touch "$SEEN_FILE" 2>/dev/null

trim_field() {
    printf '%s' "$1" | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//'
}

strip_file_marker() {
    printf '%s' "$1" | sed -E 's/[[:space:]]*\([A-Z]+\)[[:space:]]*$//'
}

json_escape() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

tag_hash() {
    if command -v md5sum >/dev/null 2>&1; then
        printf '%s' "$1" | md5sum | awk '{print $1}'
    elif command -v md5 >/dev/null 2>&1; then
        printf '%s' "$1" | md5 -q 2>/dev/null
    else
        # Last-resort POSIX fallback. Git Bash and macOS should hit md5sum/md5.
        printf '%s' "$1" | cksum | awk '{print $1}'
    fi
}

path_changed_against_head() {
    [ -z "$1" ] && return 1
    printf '%s\n' "$CHANGED_PATHS" | grep -Fxq -- "$1"
}

loc_changed_for_path() {
    [ -z "$1" ] && {
        printf '0'
        return
    }

    awk -v path="$1" '
        BEGIN { total = 0 }
        $3 == path {
            ins = ($1 ~ /^[0-9]+$/) ? $1 : 0
            del = ($2 ~ /^[0-9]+$/) ? $2 : 0
            total += ins + del
        }
        END { print total + 0 }
    ' <<EOF
$DIFF_NUMSTAT
EOF
}

detect_candidate_skills() {
    if [ "$DETECTED_SKILLS_READY" -eq 0 ]; then
        DETECTED_SKILLS="$(bash "$REPO_ROOT/.claude/scripts/skill-provenance-detect.sh" --candidates-only 2>/dev/null || echo "")"
        [ -z "$DETECTED_SKILLS" ] && DETECTED_SKILLS="/build-verify, /superpowers:verification-before-completion"
        DETECTED_SKILLS_READY=1
    fi
    printf '%s' "$DETECTED_SKILLS"
}

append_telemetry_once() {
    TAG="$1"
    FILES_COUNT="$2"
    SRC_TOUCHED="$3"
    LOC_CHANGED="$4"

    TAG_MD5="$(tag_hash "$TAG")"
    [ -z "$TAG_MD5" ] && return 0
    SEEN_KEY="${HEAD_SHORT}:${TAG_MD5}"

    if grep -Fxq -- "$SEEN_KEY" "$SEEN_FILE" 2>/dev/null; then
        return 0
    fi

    TAG_JSON="$(json_escape "$TAG")"
    SRC_JSON=false
    [ "$SRC_TOUCHED" -eq 1 ] && SRC_JSON=true

    if printf '{"ts":"%s","event":"missing_skills_invoked","tag":"%s","files_count":%d,"src_touched":%s,"loc_changed":%d}\n' \
        "$TS" "$TAG_JSON" "$FILES_COUNT" "$SRC_JSON" "$LOC_CHANGED" >> "$TELEMETRY_FILE" 2>/dev/null; then
        printf '%s\n' "$SEEN_KEY" >> "$SEEN_FILE" 2>/dev/null
    fi
}

# -------- Step 1: cache git state and extract added RTC lines since HEAD --------
# Compare working-tree chat.md against HEAD (last commit). Anything added in
# the +diff is a candidate. This catches all uncommitted RTCs every turn; nag is
# self-clearing the moment the agent edits the field in.

CHAT_DIFF="$(git diff HEAD -- agents/chat.md 2>/dev/null || echo "")"
ADDED_RTCS="$(printf '%s\n' "$CHAT_DIFF" \
    | grep -E '^\+READY TO COMMIT [^[]*\[' \
    | sed 's/^+//')"

[ -z "$ADDED_RTCS" ] && exit 0  # No new RTCs this turn; silent exit.

CHANGED_PATHS="$(git diff --name-only HEAD -- 2>/dev/null || echo "")"
DIFF_NUMSTAT="$(git diff --numstat HEAD -- 2>/dev/null || echo "")"
HEAD_SHORT="$(git rev-parse --short HEAD 2>/dev/null || echo no-head)"

# -------- Step 2: parse, classify, scaffold, detect stale files, emit telemetry --------
TS="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
NAG_COUNT=0
NAG_LINES=""
SCAFFOLD_LINES=""
STALE_LINES=""
DETECTED_SKILLS=""
DETECTED_SKILLS_READY=0

while IFS= read -r LINE; do
    [ -z "$LINE" ] && continue

    # Parse tag (between first `[` and matching `]:`).
    TAG="$(printf '%s\n' "$LINE" | sed -nE 's/^READY TO COMMIT [^[]*\[([^]]+)\]:.*/\1/p')"
    [ -z "$TAG" ] && continue  # Malformed line; skip silently.

    # Parse files (everything after the last `| files:`).
    FILES_RAW="$(printf '%s\n' "$LINE" | sed -nE 's/.*\| files:[[:space:]]*(.+)$/\1/p')"
    [ -z "$FILES_RAW" ] && continue  # No files: field; not a valid RTC.

    SKILLS_PRESENT=0
    printf '%s\n' "$LINE" | grep -qE '\| Skills invoked:\s*\[/' && SKILLS_PRESENT=1

    SRC_TOUCHED=0
    LOC_CHANGED=0
    FILES_COUNT=0
    FILE_PATHS=""

    OLD_IFS="$IFS"
    IFS=','
    for F in $FILES_RAW; do
        F_TRIMMED="$(trim_field "$F")"
        [ -z "$F_TRIMMED" ] && continue
        FILES_COUNT=$((FILES_COUNT + 1))

        F_PATH="$(strip_file_marker "$F_TRIMMED")"
        [ -z "$F_PATH" ] && continue
        FILE_PATHS="${FILE_PATHS}${F_PATH}
"

        case "$F_PATH" in
            src/*|native_sidecar/src/*)
                SRC_TOUCHED=1
                ;;
        esac
    done
    IFS="$OLD_IFS"

    # Preserve the old telemetry behavior: once src/ qualifies the RTC as
    # non-trivial, loc_changed stays 0 instead of doing extra LOC accounting.
    if [ "$SRC_TOUCHED" -eq 0 ]; then
        while IFS= read -r F_PATH; do
            [ -z "$F_PATH" ] && continue
            [ -f "$F_PATH" ] || continue
            LOC_CHANGED=$((LOC_CHANGED + $(loc_changed_for_path "$F_PATH")))
        done <<EOF
$FILE_PATHS
EOF
    fi

    NON_TRIVIAL=0
    if [ "$SRC_TOUCHED" -eq 1 ] || [ "$LOC_CHANGED" -ge 30 ]; then
        NON_TRIVIAL=1
    fi

    # Stale-file detection (warn on missing files or files clean against HEAD).
    while IFS= read -r F_PATH; do
        [ -z "$F_PATH" ] && continue
        if [ ! -e "$F_PATH" ]; then
            STALE_LINES="${STALE_LINES}  - [${TAG}]: missing file ${F_PATH}\n"
        elif [ -f "$F_PATH" ]; then
            if ! path_changed_against_head "$F_PATH"; then
                STALE_LINES="${STALE_LINES}  - [${TAG}]: file ${F_PATH} clean vs HEAD (verify - may be stale if file was committed mid-session)\n"
            fi
        fi
    done <<EOF
$FILE_PATHS
EOF

    if [ "$NON_TRIVIAL" -eq 1 ] && [ "$SKILLS_PRESENT" -eq 0 ]; then
        NAG_COUNT=$((NAG_COUNT + 1))
        NAG_LINES="${NAG_LINES}  - [${TAG}]\n"
        append_telemetry_once "$TAG" "$FILES_COUNT" "$SRC_TOUCHED" "$LOC_CHANGED"

        DETECTED="$(detect_candidate_skills)"
        MESSAGE_BODY="$(printf '%s\n' "$LINE" | sed -nE 's/^READY TO COMMIT [^[]*\[[^]]+\]:[[:space:]]+([^|]+)[[:space:]]+\| files:.*/\1/p')"
        SCAFFOLD_LINES="${SCAFFOLD_LINES}  Scaffolded for [${TAG}]:\n    READY TO COMMIT - [${TAG}]: ${MESSAGE_BODY} | Skills invoked: [${DETECTED}] | files: ${FILES_RAW}\n\n"
    fi
done <<EOF
$ADDED_RTCS
EOF

# -------- Step 3: emit nag warning + scaffold + stale-file info if any --------
if [ "$NAG_COUNT" -gt 0 ] || [ -n "$SCAFFOLD_LINES" ] || [ -n "$STALE_LINES" ]; then
    cat <<EOF
<system-reminder>
[pre-rtc-checker, contracts-v3 nag-only mode] Working-tree chat.md needs attention.
EOF

    if [ "$NAG_COUNT" -gt 0 ]; then
        cat <<EOF

$NAG_COUNT non-trivial RTC(s) missing 'Skills invoked: [/skill1, /skill2, ...]' field:
$(printf "%b" "$NAG_LINES")
EOF
    fi

    if [ -n "$SCAFFOLD_LINES" ]; then
        cat <<EOF

Suggested scaffolded replacements (detected from session telemetry; verify before pasting):
$(printf "%b" "$SCAFFOLD_LINES")
EOF
    fi

    if [ -n "$STALE_LINES" ]; then
        cat <<EOF

Stale file references in RTCs (file missing or clean against HEAD):
$(printf "%b" "$STALE_LINES")
EOF
    fi

    cat <<EOF
Required for non-trivial RTCs (>=1 file under src/ or native_sidecar/src/, OR >=30 LOC). See agents/CONTRACTS.md section Skill Provenance in RTCs for the format. Trivial RTCs (doc-only, governance-only, single-line) may omit. Nag-only first 30 days; promote-to-block deferred per SKILL_DISCIPLINE_FIX_TODO section 5 question 3.
</system-reminder>
EOF
fi

exit 0
