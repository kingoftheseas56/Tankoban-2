#!/usr/bin/env bash
# The Office — spawn a brother as a BACKGROUND headless session to handle a summon.
#
# Usage: spawn_brother.sh <agentN> <from> <seq> <task> [lockdir]
#
# Loads the brother's identity (kernel CLAUDE.md auto-loads + his latest recap),
# does the task in the shared repo, posts the result to the Office, and exits.
# Invoked DETACHED by office_dispatch.py (which creates [lockdir] first); this
# script removes the lock on exit so the next summon to the same brother can run.
#
# TIGHT LEASH (enforced by prompt + the TANKOBAN_BG_SESSION flag):
#   - posts results, never commits/pushes to master
#   - cannot summon/wake another brother (no chains)
# Model: a summoned brother wakes as his REAL reasoning self (Opus) — brothers are
#        brothers, not cheap model slots. OFFICE_BROTHER_MODEL overrides (e.g. to
#        'sonnet') for the rare, deliberately-cheap background helper. NOTE: Agent 7
#        (Codex) and Agent 9 (DeepSeek) run their OWN engines via scripts/engines/ and
#        should not be spun up through `claude -p` — per-brother engine routing is the
#        next layer; today's Claude brothers (0-5, 8) wake on Opus.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
ME="${1:-}"; FROM="${2:-}"; SEQ="${3:-}"; TASK="${4:-}"; LOCKDIR="${5:-}"
[ -z "$ME" ] || [ -z "$TASK" ] && { echo "usage: spawn_brother.sh <agentN> <from> <seq> <task> [lockdir]" >&2; exit 1; }

cleanup(){ [ -n "$LOCKDIR" ] && rmdir "$LOCKDIR" 2>/dev/null; }
trap cleanup EXIT

MODEL="${OFFICE_BROTHER_MODEL:-opus}"
N="${ME#agent}"
RECAP_DIR="$HOME/.claude/recaps/agent-$N"
RECAP=""
[ -d "$RECAP_DIR" ] && RECAP="$(ls -t "$RECAP_DIR"/*.md 2>/dev/null | head -1)"
RECAP_LINE="(No recap on disk — work from the kernel + repo.)"
[ -n "$RECAP" ] && RECAP_LINE="Your latest recap (read it for your bearings): $RECAP"

OFFICE_DIR_R="${OFFICE_DIR:-$REPO/agents}"
LEDGER="$OFFICE_DIR_R/.office_spawns.jsonl"
LOG_DIR="$OFFICE_DIR_R/.office_spawn_logs"
mkdir -p "$LOG_DIR" 2>/dev/null
LOG="$LOG_DIR/${ME}-${SEQ}.log"
# The dispatcher writes the ledger "start" row synchronously (under a lock) BEFORE
# launching us, so the spawn cap is reliable; we only record completion below.

PROMPT="$(cat <<EOF
You are ${ME}, a brother in the Tankoban 2 brotherhood, running right now as a
BACKGROUND session because your own tab isn't open. Brother ${FROM} summoned you
in the Office to handle this:

  ${TASK}

Get your bearings FAST, then do it:
  1. Your kernel CLAUDE.md is already loaded. ${RECAP_LINE}
  2. Do the task in the shared repo using your tools. Stay focused on the summon.

TIGHT LEASH — you are a background helper, so these are HARD rules:
  - Do NOT commit, merge, or push to master. Leave any code edits uncommitted for
    a live brother to review, or simply report your findings.
  - Do NOT summon, wake, or dispatch any other brother (no chains).
  - When finished, POST your result to the Office in ONE concise line, then STOP:
      bash scripts/office/office_bus.py append ${ME} ${FROM} chat null "RESULT: <your answer / what you did>"
  - Be concise and quota-aware — this is a short background run, not a full wake.
EOF
)"

cd "$REPO" || exit 1

# Structural leash (Codex review 2026-06-02): even under --dangerously-skip-permissions,
# a background brother must not commit/push/merge. Two layers: (1) a git PATH-shim
# that blocks state-changing subcommands (defense-in-depth — casual bypasses only),
# and (2) the real structural block, a pre-commit hook gated on TANKOBAN_BG_SESSION
# (installed by open_office.bat) that refuses ANY commit from a bg session.
export PATH="$HERE/bg_git_guard:$PATH"

printf '%s' "$PROMPT" | TANKOBAN_BG_SESSION=1 ENGINE_AGENT="$ME" \
  claude -p --dangerously-skip-permissions --model "$MODEL" > "$LOG" 2>&1
RC=$?

printf '{"ts":"%s","agent":"%s","from":"%s","seq":"%s","status":"done","rc":%s}\n' \
  "$(date +%Y-%m-%dT%H:%M:%S%z)" "$ME" "$FROM" "$SEQ" "$RC" >> "$LEDGER"

# Safety net: if the brother posted NOTHING to the bus, surface that so the summon
# is never silently dropped. We check the bus (not the log) for a chat post from
# this brother with seq above the summon seq — the reliable signal he replied.
POSTED="$(BUS_DEFAULT="$REPO/agents/bus.jsonl" python - "$ME" "$SEQ" <<'PY'
import json, os, sys
me, seq = sys.argv[1], int(sys.argv[2] or 0)
bus = os.environ.get("OFFICE_BUS") or os.environ.get("BUS_DEFAULT")
ok = 0
try:
    for line in open(bus, encoding="utf-8"):
        line = line.strip()
        if not line:
            continue
        try:
            r = json.loads(line)
        except (json.JSONDecodeError, ValueError):
            continue
        if r.get("from") == me and r.get("seq", 0) > seq:
            ok = 1
            break
except OSError:
    pass
print(ok)
PY
)"
if [ "$POSTED" != "1" ]; then
  python "$REPO/scripts/office/office_bus.py" append "$ME" "$FROM" chat null \
    "(background ${ME} finished summon #${SEQ} but posted no result — see agents/.office_spawn_logs/${ME}-${SEQ}.log; rc=${RC})" 2>/dev/null || true
fi
