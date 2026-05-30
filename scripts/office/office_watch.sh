#!/usr/bin/env bash
# The Office — bus watch (always-on mode, v2).
# A brother runs this via the Monitor tool (or /loop) to "clock in": it sleeps,
# checks the bus every few seconds, and EMITS A LINE the instant a new message
# addressed to this agent (or @all) arrives. That emitted line is what wakes the
# brother (harness re-invokes the session on watch output — same mechanism that
# wakes an agent when a build finishes).
#
# Usage (inside an agent tab, via Monitor):  scripts/office/office_watch.sh <agentN>
#   e.g.  scripts/office/office_watch.sh agent1
# It does NOT advance the cursor — the woken brother runs `office_bus.py drain`
# to read + clear, so a normal prompt-time delivery and a watch wake agree.
#
# Tuning: OFFICE_WATCH_INTERVAL (seconds between checks, default 3).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ME="${1:-}"
[ -z "$ME" ] && { echo "usage: office_watch.sh <agentN>" >&2; exit 1; }
INTERVAL="${OFFICE_WATCH_INTERVAL:-3}"

# Track the highest seq we've already announced so we don't repeat. Seed from the
# agent's current cursor so a freshly-clocked-in brother doesn't re-announce the
# whole backlog (he saw that via drain at join).
LAST="$(python "$HERE/office_bus.py" cursor "$ME" 2>/dev/null || echo 0)"

echo "[office-watch] $ME on watch (interval ${INTERVAL}s, from seq ${LAST}) — waiting for messages..."
while true; do
  # Ask the bus for the max seq of unseen-for-me messages above LAST.
  NEW="$(python "$HERE/office_bus.py" watch-peek "$ME" "$LAST" 2>/dev/null)"
  if [ -n "$NEW" ]; then
    # Emit one human line per new message — each line is a wake signal to the brother.
    echo "$NEW"
    # Advance our local high-water mark to the last seq we just announced.
    LAST="$(printf '%s\n' "$NEW" | sed -n 's/^\[seq \([0-9]*\)\].*/\1/p' | tail -1)"
  fi
  sleep "$INTERVAL"
done
