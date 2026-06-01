#!/usr/bin/env bash
# The Office - acknowledge a direct ask: office_ack.sh <agentN> <ask_seq> [note]
HERE="$(cd "$(dirname "$0")" && pwd)"
ME="${1:-}"
SEQ="${2:-}"
shift 2 2>/dev/null || true
if [ -z "$ME" ] || [ -z "$SEQ" ]; then
  echo "usage: office_ack.sh <agentN> <ask_seq> [note]" >&2
  exit 1
fi
python "$HERE/office_bus.py" ack "$ME" "$SEQ" "$*"
