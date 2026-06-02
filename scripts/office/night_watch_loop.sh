#!/usr/bin/env bash
# Night Watch — detached foreman loop (the root of trust).
# Fires one headless Opus foreman tick every NIGHT_TICK_INTERVAL seconds, forever.
# It is a DETACHED OS process: it survives VS Code tabs closing AND agent0 going idle —
# the two failure modes Hemanth named. It dies only on reboot or an explicit kill.
#
# LAUNCH (held for Hemanth's go-live):
#   nohup bash scripts/office/night_watch_loop.sh >/dev/null 2>&1 &
# or via PowerShell Start-Process so it's fully detached from the launching session.
#
# STOP:
#   touch agents/night_ops/STOP     (graceful — loop exits after the current tick)
#   or kill the loop PID in agents/night_ops/.loop.pid
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
cd "$REPO" || exit 1

PROMPT_FILE="$HERE/night_foreman_prompt.md"
INTERVAL="${NIGHT_TICK_INTERVAL:-900}"          # 15 min default
NIGHT_DIR="$REPO/agents/night_ops"
LOG="$NIGHT_DIR/foreman_ticks.log"
mkdir -p "$NIGHT_DIR/staged" "$NIGHT_DIR/inflight" 2>/dev/null
echo $$ > "$NIGHT_DIR/.loop.pid"

echo "[night-watch-loop] START $(date) interval=${INTERVAL}s pid=$$" >> "$LOG"
while true; do
  # graceful stop switch
  if [ -f "$NIGHT_DIR/STOP" ]; then
    echo "[night-watch-loop] STOP file present — exiting $(date)" >> "$LOG"
    rm -f "$NIGHT_DIR/.loop.pid"
    exit 0
  fi
  echo "" >> "$LOG"; echo "═══ FOREMAN TICK $(date) ═══" >> "$LOG"
  # heartbeat for the loop itself (separate from the foreman tick's own beat)
  date +%s > "$NIGHT_DIR/.loop.beat" 2>/dev/null
  # fire one headless foreman cycle (NOT leashed -> it COULD commit, but the prompt is
  # STAGE-ONLY in v1; the loop just re-fires it). The FOREMAN runs on Sonnet — it is
  # supervisory autopilot (collect/dispatch/stage), not a brother doing creative work, so
  # Sonnet keeps the all-night loop sustainable (the 22:53 proof tick died on an Opus
  # session limit) while the BROTHERS it summons stay Opus. Override: NIGHT_FOREMAN_MODEL.
  cat "$PROMPT_FILE" | NIGHT_WATCH=1 claude -p --dangerously-skip-permissions --model "${NIGHT_FOREMAN_MODEL:-sonnet}" >> "$LOG" 2>&1
  echo "[night-watch-loop] tick complete $(date); sleeping ${INTERVAL}s" >> "$LOG"
  sleep "$INTERVAL"
done
