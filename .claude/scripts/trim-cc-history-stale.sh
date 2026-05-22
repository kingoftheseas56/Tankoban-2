#!/usr/bin/env bash
# Stop hook: auto-trim stale Claude Code Exporter transcripts.
#
# Enumerates .cc-history/*.md (excluding *.trimmed.md files), and for each
# source file invokes trim-cc-history.ps1 ONLY IF:
#   - the .trimmed.md counterpart doesn't exist, OR
#   - the source's mtime is >= THROTTLE_SEC seconds newer than the
#     .trimmed.md's mtime.
#
# Throttle (default 60s) prevents thrashing -- if user/agent are rapidly
# turn-cycling, we re-trim at most once per minute per file. The trim work
# itself takes ~2 sec per file, so worst case adds ~2 sec to one turn per
# minute per active session. PowerShell launch cost is ONLY paid when an
# actual trim is needed; the bash enumeration + mtime-check fast-path
# completes in ~50-100ms when no work is required (the common case).
#
# Silent on stdout/stderr (would otherwise leak into Claude's context).
# Trim attempts logged to .claude/telemetry/cc-history-trim.log for
# post-hoc diagnosis. Exits 0 unconditionally -- this hook MUST NOT
# block the Stop event flow even if a trim fails.
#
# Authored 2026-05-22 by Agent 0 (Coordinator) as the "background mode"
# for the .cc-history trim architecture introduced earlier in the wake.
# See .claude/scripts/trim-cc-history.ps1 for the actual trim logic.

set +e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT" 2>/dev/null || exit 0

CC_HISTORY_DIR="$REPO_ROOT/.cc-history"
TRIM_SCRIPT="$REPO_ROOT/.claude/scripts/trim-cc-history.ps1"
LOG_DIR="$REPO_ROOT/.claude/telemetry"
LOG_FILE="$LOG_DIR/cc-history-trim.log"
THROTTLE_SEC=60

# Bail silently if either the .cc-history dir or the trim script is missing.
[ -d "$CC_HISTORY_DIR" ] || exit 0
[ -f "$TRIM_SCRIPT" ]   || exit 0

mkdir -p "$LOG_DIR" 2>/dev/null

now_ts() { date +%s; }

# Resolve a Windows-style path for PowerShell -File arg. Git Bash auto-
# converts /c/... back to C:\... in most invocations, but being explicit
# avoids surprises.
to_winpath() {
    local p="$1"
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -w "$p"
    else
        printf '%s\n' "$p"
    fi
}

TRIM_SCRIPT_WIN="$(to_winpath "$TRIM_SCRIPT")"

shopt -s nullglob
for src in "$CC_HISTORY_DIR"/*.md; do
    # Skip already-trimmed files.
    case "$src" in
        *.trimmed.md) continue ;;
    esac

    # Fast-path skip: files that have no User/Assistant turn headers are
    # not real transcripts (Claude _compact.md summary files, unknown-date_*
    # exporter artifacts, etc.). The PowerShell trim script errors on these
    # with "No turn headers found" and rc=1; skipping here avoids wasted
    # PowerShell launches + log spam on every Stop event.
    if ! grep -q -E '^## (User|Assistant) <sup>' "$src" 2>/dev/null; then
        continue
    fi

    base="${src%.md}"
    trimmed="${base}.trimmed.md"
    src_win="$(to_winpath "$src")"

    # First-time trim: .trimmed.md doesn't exist yet.
    if [ ! -f "$trimmed" ]; then
        powershell -NoProfile -ExecutionPolicy Bypass -File "$TRIM_SCRIPT_WIN" -InputFile "$src_win" -Quiet >/dev/null 2>&1
        rc=$?
        if [ "$rc" -eq 0 ]; then
            echo "$(now_ts) trim_missing rc=0 src=$src" >> "$LOG_FILE"
        else
            echo "$(now_ts) trim_missing rc=$rc src=$src" >> "$LOG_FILE"
        fi
        continue
    fi

    # Both exist -- compare mtimes.
    src_mtime=$(stat -c %Y "$src" 2>/dev/null)
    trimmed_mtime=$(stat -c %Y "$trimmed" 2>/dev/null)

    # If stat failed on either, skip (unknown state, safer not to act).
    if [ -z "$src_mtime" ] || [ -z "$trimmed_mtime" ]; then
        continue
    fi

    # Skip if source is not newer.
    if [ "$src_mtime" -le "$trimmed_mtime" ]; then
        continue
    fi

    # Throttle: skip if source is less than THROTTLE_SEC newer than trimmed.
    age_diff=$((src_mtime - trimmed_mtime))
    if [ "$age_diff" -lt "$THROTTLE_SEC" ]; then
        continue
    fi

    # Re-trim.
    powershell -NoProfile -ExecutionPolicy Bypass -File "$TRIM_SCRIPT_WIN" -InputFile "$src_win" -Quiet >/dev/null 2>&1
    rc=$?
    if [ "$rc" -eq 0 ]; then
        echo "$(now_ts) trim_stale rc=0 age_diff=${age_diff}s src=$src" >> "$LOG_FILE"
    else
        echo "$(now_ts) trim_stale rc=$rc age_diff=${age_diff}s src=$src" >> "$LOG_FILE"
    fi
done

exit 0
