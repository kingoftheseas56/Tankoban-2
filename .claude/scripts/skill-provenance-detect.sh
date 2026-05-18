#!/usr/bin/env bash
# Skill-provenance detector for Tankoban 2 (Phase A.5).
#
# Modes:
#   bash skill-provenance-detect.sh                 → emit JSONL append for current session
#   bash skill-provenance-detect.sh --candidates-only → emit comma-separated skill list
#   bash skill-provenance-detect.sh --schema-check  → emit telemetry schema for docs

set +e

MODE="${1:-jsonl}"
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT" 2>/dev/null || exit 0

TELEMETRY_DIR=".claude/telemetry"
TELEMETRY_FILE="${TELEMETRY_DIR}/skill-invocations.jsonl"
SESSION_FILE="${TELEMETRY_DIR}/.current-session-id"
mkdir -p "$TELEMETRY_DIR" 2>/dev/null

SESSION_ID="${CLAUDE_SESSION_ID:-$(cat "$SESSION_FILE" 2>/dev/null)}"
if [ -z "$SESSION_ID" ]; then
    SESSION_ID="session-$(date -u +%Y%m%dT%H%M%SZ)"
    echo "$SESSION_ID" > "$SESSION_FILE" 2>/dev/null
fi

case "$MODE" in
    --schema-check)
        cat <<EOF
{ "ts": "<ISO8601 UTC>", "sessionId": "<string>", "agent": "<string>", "skill": "<string>", "source": "claude-code|codex-text", "cwd": "<string>" }
EOF
        exit 0
        ;;
    --candidates-only)
        if [ -f agents/chat.md ]; then
            CANDIDATES="$(tail -300 agents/chat.md \
                | grep -oE '/(brief|simplify|build-verify|commit-sweep|repo-health|rotate-chat|session-recap|security-review|superpowers:[a-z-]+|claude-mem:[a-z-]+)' \
                | sort -u \
                | head -10 \
                | tr '\n' ',' \
                | sed 's/,$//' \
                | sed 's/,/, /g')"
            if [ -z "$CANDIDATES" ]; then
                CANDIDATES="/build-verify, /superpowers:verification-before-completion"
            fi
            echo "$CANDIDATES"
        else
            echo "/build-verify, /superpowers:verification-before-completion"
        fi
        exit 0
        ;;
    jsonl|*)
        if [ ! -f agents/chat.md ]; then
            exit 0
        fi

        TS="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        CWD="$(pwd | tr '\\' '/')"

        SKILLS="$(tail -100 agents/chat.md \
            | grep -oE '/(brief|simplify|build-verify|commit-sweep|repo-health|rotate-chat|session-recap|security-review|superpowers:[a-z-]+|claude-mem:[a-z-]+)' \
            | sort -u)"

        while IFS= read -r SKILL; do
            [ -z "$SKILL" ] && continue
            if [ -f "$TELEMETRY_FILE" ]; then
                if tail -50 "$TELEMETRY_FILE" 2>/dev/null | grep -qF "\"sessionId\":\"${SESSION_ID}\"" 2>/dev/null; then
                    if tail -50 "$TELEMETRY_FILE" 2>/dev/null | grep -qF "\"skill\":\"${SKILL}\"" 2>/dev/null; then
                        continue
                    fi
                fi
            fi
            printf '{"ts":"%s","sessionId":"%s","agent":"unknown","skill":"%s","source":"claude-code","cwd":"%s"}\n' \
                "$TS" "$SESSION_ID" "$SKILL" "$CWD" \
                >> "$TELEMETRY_FILE" 2>/dev/null
        done <<< "$SKILLS"

        exit 0
        ;;
esac

exit 0
