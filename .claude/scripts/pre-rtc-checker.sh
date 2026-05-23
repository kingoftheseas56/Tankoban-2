#!/usr/bin/env bash
# Pre-RTC checker hook for Tankoban 2 (Phase 4 of SKILL_DISCIPLINE_FIX_TODO).
#
# Purpose: when an agent finishes a turn that added one or more `READY TO COMMIT`
# lines to agents/chat.md, scan each new line and warn if it is "non-trivial"
# (per contracts-v3 threshold) but missing the `Skills invoked:` field.
#
# Mode: NAG-ONLY. The script always exits 0 and never blocks the agent's turn.
# The pre-RTC build gate below is a soft-block banner + telemetry during its
# initial rollout; it deliberately preserves the existing nag-only hook shape.
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
BUILD_GATE_TELEMETRY_FILE="${TELEMETRY_DIR}/pre-rtc-build-gate.jsonl"
BUILD_GATE_THRESHOLD_SEC=$((10 * 60))
BUILD_GATE_THRESHOLD_MIN=10
mkdir -p "$TELEMETRY_DIR" 2>/dev/null
touch "$SEEN_FILE" 2>/dev/null

trim_field() {
    printf '%s' "$1" | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//'
}

strip_file_marker() {
    printf '%s' "$1" | sed -E 's/[[:space:]]*\([A-Z]+\)[[:space:]]*$//'
}

normalize_repo_path() {
    printf '%s' "$1" | sed -E 's#\\#/#g; s#^\./##; s/^"//; s/"$//; s/^\x27//; s/\x27$//'
}

json_escape() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

json_array_from_lines() {
    ARR="["
    FIRST=1
    while IFS= read -r ITEM; do
        [ -z "$ITEM" ] && continue
        ITEM_JSON="$(json_escape "$ITEM")"
        if [ "$FIRST" -eq 1 ]; then
            ARR="${ARR}\"${ITEM_JSON}\""
            FIRST=0
        else
            ARR="${ARR},\"${ITEM_JSON}\""
        fi
    done <<EOF
$1
EOF
    ARR="${ARR}]"
    printf '%s' "$ARR"
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

agent_from_tag() {
    printf '%s' "$1" | sed -E 's/,.*$//' | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//'
}

display_log_path() {
    printf '%s' "$1" | sed 's#/#\\#g'
}

build_gate_log_path() {
    LANE="${TANKOBAN_BUILD_LANE:-}"
    if [ -n "$LANE" ]; then
        if printf '%s' "$LANE" | grep -Eq '^[A-Za-z0-9_-]+$'; then
            printf 'out_%s/_build_check.log' "$LANE"
            return
        fi
    fi
    printf 'out/_build_check.log'
}

file_requires_build_gate() {
    case "$1" in
        src/*.cpp|src/*.cc|src/*.h|src/*.hpp|native_sidecar/src/*.cpp|native_sidecar/src/*.cc|native_sidecar/src/*.h|native_sidecar/src/*.hpp)
            return 0
            ;;
    esac
    return 1
}

last_nonempty_line() {
    [ -f "$1" ] || return 1
    awk 'NF { line = $0 } END { print line }' "$1" 2>/dev/null
}

build_log_status() {
    if [ ! -f "$1" ]; then
        printf 'missing'
        return
    fi

    LAST_LINE="$(last_nonempty_line "$1")"
    if [ "$LAST_LINE" = "BUILD OK" ]; then
        printf 'BUILD OK'
    elif printf '%s' "$LAST_LINE" | grep -Eq '^BUILD FAILED exit=[0-9]+$'; then
        printf '%s' "$LAST_LINE"
    else
        printf 'unknown'
    fi
}

file_mtime_epoch() {
    [ -f "$1" ] || return 1
    stat -c %Y "$1" 2>/dev/null || stat -f %m "$1" 2>/dev/null
}

format_epoch_utc() {
    [ -z "$1" ] && {
        printf 'missing'
        return
    }
    date -u -d "@$1" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null \
        || date -u -r "$1" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null \
        || printf '%s' "$1"
}

append_build_gate_telemetry() {
    AGENT="$1"
    FILES="$2"
    LOG_PATH="$3"
    LOG_MTIME="$4"
    LOG_STATUS="$5"
    DECISION="$6"

    AGENT_JSON="$(json_escape "$AGENT")"
    FILES_JSON="$(json_array_from_lines "$FILES")"
    LOG_PATH_JSON="$(json_escape "$(display_log_path "$LOG_PATH")")"
    LOG_STATUS_JSON="$(json_escape "$LOG_STATUS")"

    if [ "$LOG_MTIME" = "missing" ]; then
        LOG_MTIME_JSON="null"
    else
        LOG_MTIME_JSON="\"$(json_escape "$LOG_MTIME")\""
    fi

    printf '{"timestamp":"%s","agent":"%s","files":%s,"log_path":"%s","log_mtime":%s,"log_status":"%s","decision":"%s"}\n' \
        "$TS" "$AGENT_JSON" "$FILES_JSON" "$LOG_PATH_JSON" "$LOG_MTIME_JSON" "$LOG_STATUS_JSON" "$DECISION" \
        >> "$BUILD_GATE_TELEMETRY_FILE" 2>/dev/null
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
BUILD_GATE_BLOCK_COUNT=0
BUILD_GATE_BLOCK_LINES=""
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
    BUILD_GATE_FILES=""

    OLD_IFS="$IFS"
    IFS=','
    for F in $FILES_RAW; do
        F_TRIMMED="$(trim_field "$F")"
        [ -z "$F_TRIMMED" ] && continue
        FILES_COUNT=$((FILES_COUNT + 1))

        F_PATH="$(normalize_repo_path "$(strip_file_marker "$F_TRIMMED")")"
        [ -z "$F_PATH" ] && continue
        FILE_PATHS="${FILE_PATHS}${F_PATH}
"

        case "$F_PATH" in
            src/*|native_sidecar/src/*)
                SRC_TOUCHED=1
                ;;
        esac

        if file_requires_build_gate "$F_PATH"; then
            BUILD_GATE_FILES="${BUILD_GATE_FILES}${F_PATH}
"
        fi
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

    if [ -n "$BUILD_GATE_FILES" ]; then
        AGENT="$(agent_from_tag "$TAG")"
        [ -z "$AGENT" ] && AGENT="$TAG"
        LOG_PATH="$(build_gate_log_path)"
        LOG_STATUS="$(build_log_status "$LOG_PATH")"
        LOG_MTIME_EPOCH=""
        LOG_MTIME="missing"
        LOG_AGE_SEC=""
        LOG_AGE_MIN="n/a"

        if [ -f "$LOG_PATH" ]; then
            LOG_MTIME_EPOCH="$(file_mtime_epoch "$LOG_PATH")"
            LOG_MTIME="$(format_epoch_utc "$LOG_MTIME_EPOCH")"
            NOW_EPOCH="$(date +%s 2>/dev/null || echo 0)"
            if [ -n "$LOG_MTIME_EPOCH" ] && [ "$NOW_EPOCH" -gt 0 ]; then
                LOG_AGE_SEC=$((NOW_EPOCH - LOG_MTIME_EPOCH))
                [ "$LOG_AGE_SEC" -lt 0 ] && LOG_AGE_SEC=0
                LOG_AGE_MIN=$(((LOG_AGE_SEC + 59) / 60))
            fi
        fi

        BUILD_GATE_DECISION="BLOCK"
        if [ "$LOG_STATUS" = "BUILD OK" ] && [ -n "$LOG_AGE_SEC" ] && [ "$LOG_AGE_SEC" -le "$BUILD_GATE_THRESHOLD_SEC" ]; then
            BUILD_GATE_DECISION="PASS"
        fi

        append_build_gate_telemetry "$AGENT" "$BUILD_GATE_FILES" "$LOG_PATH" "$LOG_MTIME" "$LOG_STATUS" "$BUILD_GATE_DECISION"

        if [ "$BUILD_GATE_DECISION" = "BLOCK" ]; then
            BUILD_GATE_BLOCK_COUNT=$((BUILD_GATE_BLOCK_COUNT + 1))
            BUILD_GATE_BLOCK_LINES="${BUILD_GATE_BLOCK_LINES}PRE-RTC BUILD GATE FAILED for ${AGENT}:\n\nYour RTC names src/ or native_sidecar/src/ files but no recent successful build_check.bat log exists:\n- log path: $(display_log_path "$LOG_PATH")\n- log mtime: ${LOG_MTIME}  (age: ${LOG_AGE_MIN} min, threshold: ${BUILD_GATE_THRESHOLD_MIN} min)\n- log status: ${LOG_STATUS}\n\nRun build_check.bat to compile your changes, then re-post the RTC.\n\nWhy this gate exists: shipping uncompiled .cpp/.h work to chat.md leaves Hemanth absorbing the build cost when he next clicks build_and_run.bat. The contract (CLAUDE.md HEMANTH'S ROLE block + feedback_agent_launches_app.md memory): agents compile their own changes before posting RTCs.\n\n"
        fi
    fi
done <<EOF
$ADDED_RTCS
EOF

# -------- Step 3: emit nag warning + scaffold + stale-file info if any --------
if [ "$NAG_COUNT" -gt 0 ] || [ -n "$SCAFFOLD_LINES" ] || [ -n "$STALE_LINES" ] || [ "$BUILD_GATE_BLOCK_COUNT" -gt 0 ]; then
    cat <<EOF
<system-reminder>
[pre-rtc-checker, contracts-v3 nag-only mode] Working-tree chat.md needs attention.
EOF

    if [ "$BUILD_GATE_BLOCK_COUNT" -gt 0 ]; then
        cat <<EOF

$(printf "%b" "$BUILD_GATE_BLOCK_LINES")
Soft-block rollout: this Stop hook still exits 0 during the initial nag-first window, but the RTC is treated as blocked by process. Re-run build_check.bat and re-post the RTC with a fresh BUILD OK log.
EOF
    fi

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
