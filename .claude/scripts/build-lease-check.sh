#!/usr/bin/env bash
# PreToolUse build-lease invariant for Tankoban 2.
#
# Hook event: PreToolUse, matcher: Bash, timeout: 5s in .claude/settings.json.
#
# Contract:
# - Shared build outputs under out/ are protected by the DevControl lease
#   registry lane named "build".
# - Per-lane builds bypass this hook when they target out_<lane>/ directly, or
#   when build_check.bat / build_and_run.bat runs with TANKOBAN_BUILD_LANE set.
# - The shared-out patterns gated here are:
#     * build_check.bat and build_and_run.bat without TANKOBAN_BUILD_LANE
#     * cmake --build out, ./out, .\out, or out\... (but not out_<lane>)
#     * native_sidecar/build.ps1
#     * build_qrhi.bat
#     * ninja -C out or ninja -f out/build.ninja (but not out_<lane>)
#
# Agent identity:
# - Preferred: set TANKOBAN_AGENT_ID=agent-N in the Claude session env.
# - Fallback: git config tankoban.agentId.
# - Last resort: infer "agent-N" from the checkout/worktree path if present.
# If the bridge reports an active lease and identity cannot be proven, the hook
# blocks the shared-out build and asks the agent to set TANKOBAN_AGENT_ID.
#
# Failure mode:
# - out/tankoctl.exe missing, unreachable, invalid JSON, or >2s timeout is
#   fail-open with a stderr warning. The hook must not strand the brotherhood
#   when the dev-control bridge is down.

set +e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT" 2>/dev/null || exit 0

INPUT_JSON="$(cat)"

json_escape() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g; s/	/\\t/g'
}

deny() {
    REASON="$1"
    REASON_JSON="$(json_escape "$REASON")"
    printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"%s"}}\n' "$REASON_JSON"
    exit 2
}

warn_allow() {
    printf 'build-lease-check: %s\n' "$1" >&2
    exit 0
}

json_field_from() {
    JSON="$1"
    KEY="$2"

    if command -v jq >/dev/null 2>&1; then
        printf '%s' "$JSON" | jq -r --arg key "$KEY" '.[$key] // empty' 2>/dev/null
        return
    fi

    printf '%s' "$JSON" \
        | tr '\n' ' ' \
        | sed -nE 's/.*"'"$KEY"'"[[:space:]]*:[[:space:]]*"([^"]*)".*/\1/p'
}

json_number_field_from() {
    JSON="$1"
    KEY="$2"

    if command -v jq >/dev/null 2>&1; then
        printf '%s' "$JSON" | jq -r --arg key "$KEY" '.[$key] // empty' 2>/dev/null
        return
    fi

    printf '%s' "$JSON" \
        | tr '\n' ' ' \
        | sed -nE 's/.*"'"$KEY"'"[[:space:]]*:[[:space:]]*([0-9.eE+-]+).*/\1/p'
}

extract_command() {
    if command -v jq >/dev/null 2>&1; then
        printf '%s' "$INPUT_JSON" | jq -r '.tool_input.command // empty' 2>/dev/null
        return
    fi

    printf '%s' "$INPUT_JSON" \
        | tr '\n' ' ' \
        | sed -nE 's/.*"command"[[:space:]]*:[[:space:]]*"([^"]*)".*/\1/p' \
        | sed 's/\\"/"/g; s/\\\\/\\/g'
}

has_lane_override() {
    [ -n "$TANKOBAN_BUILD_LANE" ] && return 0
    printf '%s\n' "$CMD_NORM" | grep -Eq 'tankoban_build_lane[[:space:]]*=[[:space:]]*["'\'']?[a-z0-9_-]+' && return 0
    return 1
}

is_shared_build_command() {
    # Root helper scripts. These build shared out/ unless TANKOBAN_BUILD_LANE is
    # already set in the hook environment or inline in the command.
    if printf '%s\n' "$CMD_NORM" | grep -Eq '(^|[[:space:];&|('\''"/])build_check\.bat([[:space:];&|)'\''"]|$)'; then
        has_lane_override && return 1
        return 0
    fi
    if printf '%s\n' "$CMD_NORM" | grep -Eq '(^|[[:space:];&|('\''"/])build_and_run\.bat([[:space:];&|)'\''"]|$)'; then
        has_lane_override && return 1
        return 0
    fi

    # Shared-output build commands that do not honor TANKOBAN_BUILD_LANE.
    printf '%s\n' "$CMD_NORM" | grep -Eq '(^|[[:space:];&|('\''"/])build_qrhi\.bat([[:space:];&|)'\''"]|$)' && return 0
    printf '%s\n' "$CMD_NORM" | grep -Eq '(^|[[:space:];&|('\''"/])native_sidecar/build\.ps1([[:space:];&|)'\''"]|$)' && return 0

    # cmake --build out. Boundary after "out" intentionally excludes out_<lane>.
    printf '%s\n' "$CMD_NORM" | grep -Eq '(^|[[:space:];&|('\''"])cmake(\.exe)?[[:space:]][^;&|]*--build[[:space:]]+["'\'']?([^;&|]*/)?out["'\'']?([[:space:]/; &|)'\''"]|$)' && return 0

    # Direct ninja against shared out/.
    printf '%s\n' "$CMD_NORM" | grep -Eq '(^|[[:space:];&|('\''"])ninja(\.exe)?[[:space:]][^;&|]*-c[[:space:]]+["'\'']?([^;&|]*/)?out["'\'']?([[:space:]/; &|)'\''"]|$)' && return 0
    printf '%s\n' "$CMD_NORM" | grep -Eq '(^|[[:space:];&|('\''"])ninja(\.exe)?[[:space:]][^;&|]*-f[[:space:]]+["'\'']?([^;&|]*/)?out/build\.ninja["'\'']?([[:space:];&|)'\''"]|$)' && return 0

    return 1
}

canonical_agent_id() {
    printf '%s' "$1" \
        | tr '[:upper:]' '[:lower:]' \
        | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//; s/^agent[ _-]*([0-9]+)$/agent-\1/'
}

detect_agent_id() {
    if [ -n "$TANKOBAN_AGENT_ID" ]; then
        canonical_agent_id "$TANKOBAN_AGENT_ID"
        return
    fi

    CONFIG_ID="$(git config --get tankoban.agentId 2>/dev/null)"
    if [ -n "$CONFIG_ID" ]; then
        canonical_agent_id "$CONFIG_ID"
        return
    fi

    ROOT="$(git rev-parse --show-toplevel 2>/dev/null)"
    [ -z "$ROOT" ] && ROOT="$PWD"
    printf '%s' "$ROOT" \
        | tr '[:upper:]' '[:lower:]' \
        | sed -nE 's/.*agent[ _-]*([0-9]+).*/agent-\1/p'
}

run_lease_get() {
    TANKOCTL="out/tankoctl.exe"
    [ -x "$TANKOCTL" ] || return 127

    if command -v timeout >/dev/null 2>&1 && timeout --version 2>/dev/null | grep -qi 'coreutils'; then
        LEASE_REPLY="$(timeout 2 "$TANKOCTL" lease-get build 2>&1)"
        return $?
    fi

    TMP_OUT="${TMPDIR:-/tmp}/build-lease-check.$$"
    "$TANKOCTL" lease-get build >"$TMP_OUT" 2>&1 &
    PID=$!
    COUNT=0
    while kill -0 "$PID" 2>/dev/null; do
        COUNT=$((COUNT + 1))
        if [ "$COUNT" -ge 20 ]; then
            kill "$PID" 2>/dev/null
            wait "$PID" 2>/dev/null
            LEASE_REPLY="$(cat "$TMP_OUT" 2>/dev/null)"
            rm -f "$TMP_OUT" 2>/dev/null
            return 124
        fi
        sleep 0.1
    done
    wait "$PID"
    RC=$?
    LEASE_REPLY="$(cat "$TMP_OUT" 2>/dev/null)"
    rm -f "$TMP_OUT" 2>/dev/null
    return $RC
}

format_expiry() {
    RAW="$1"
    MS="$(printf '%s' "$RAW" | sed -E 's/\..*$//; s/[^0-9].*$//')"
    if [ -n "$MS" ] && [ "$MS" -gt 0 ] 2>/dev/null; then
        SEC=$((MS / 1000))
        if date -u -d "@$SEC" '+%Y-%m-%dT%H:%M:%SZ' >/dev/null 2>&1; then
            date -u -d "@$SEC" '+%Y-%m-%dT%H:%M:%SZ'
            return
        fi
    fi
    printf 'expiry_ms=%s' "$RAW"
}

COMMAND="$(extract_command)"
[ -z "$COMMAND" ] && exit 0

CMD_NORM="$(printf '%s' "$COMMAND" | tr '\\' '/' | tr '[:upper:]' '[:lower:]')"

# False-positive defense: bypass the hook when the command is an unchained
# invocation of a known read-only verb. is_shared_build_command matches
# build_check.bat / build_and_run.bat as substrings, which falsely caught
# `git status -- build_check.bat`, `cat build_check.bat`, `grep -n ... build_*.bat`,
# `head -60 build_check.bat`, etc. — read-only inspections that can never
# write to out/. Restrict bypass to single-segment commands (no ; && || |
# cmd /c, no command substitution) to keep chained build invocations gated.
if ! printf '%s' "$CMD_NORM" | grep -Eq '(\$\(|`|;|&&|\|\||\||cmd[[:space:]]+/c|cmd\.exe[[:space:]]+/c|bash[[:space:]]+-c|powershell[[:space:]]+(-c|-command))'; then
    FIRST_TOKEN="$(printf '%s' "$CMD_NORM" | sed -E 's/^[[:space:]]*([^[:space:]]+).*/\1/')"
    case "$FIRST_TOKEN" in
        git|grep|rg|cat|head|tail|ls|find|wc|awk|sed|diff|jq|fzf|file|stat|less|more|tree|dirname|basename|readlink|realpath|md5sum|sha1sum|sha256sum|cksum|sort|uniq|tr|cut|paste|column|hexdump|xxd|od|echo|printf|true|false|test|env|pwd)
            exit 0
            ;;
    esac
fi

is_shared_build_command || exit 0

run_lease_get
LEASE_RC=$?
if [ "$LEASE_RC" -ne 0 ]; then
    warn_allow "tankoctl unreachable; allowing build (fail-open)"
fi

STATUS="$(json_field_from "$LEASE_REPLY" status)"
HOLDER="$(json_field_from "$LEASE_REPLY" holder)"
EXPIRY_MS="$(json_number_field_from "$LEASE_REPLY" expiry_ms)"
PRIOR_HOLDER="$(json_field_from "$LEASE_REPLY" prior_holder)"

if [ -z "$STATUS" ] && [ -z "$HOLDER" ]; then
    warn_allow "tankoctl returned unrecognized lease reply; allowing build (fail-open)"
fi

case "$STATUS" in
    FREE)
        deny 'Shared out/ build blocked: claim the build lease first with /mcp-lock claim build "<reason>" or `out\tankoctl.exe lease-acquire build --holder agent-N --purpose "<reason>" --ttl-sec 1800`.'
        ;;
    EXPIRED)
        [ -z "$PRIOR_HOLDER" ] && PRIOR_HOLDER="prior holder"
        deny "Shared out/ build blocked: build lease is expired from ${PRIOR_HOLDER}. Re-acquire it first; lease-acquire will return STALE_RECLAIMED automatically."
        ;;
esac

if [ -n "$HOLDER" ]; then
    AGENT_ID="$(detect_agent_id)"
    HOLDER_CANON="$(canonical_agent_id "$HOLDER")"
    AGENT_CANON="$(canonical_agent_id "$AGENT_ID")"
    EXPIRY_TEXT="$(format_expiry "$EXPIRY_MS")"

    if [ -z "$AGENT_CANON" ]; then
        deny "Shared out/ build blocked: ${HOLDER} holds the build lease until ${EXPIRY_TEXT}, and this session has no TANKOBAN_AGENT_ID to prove ownership."
    fi

    if [ "$AGENT_CANON" = "$HOLDER_CANON" ]; then
        exit 0
    fi

    deny "Shared out/ build blocked: ${HOLDER} holds the build lease until ${EXPIRY_TEXT}. Current agent identity is ${AGENT_CANON}."
fi

warn_allow "tankoctl returned unrecognized lease reply; allowing build (fail-open)"
