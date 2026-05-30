#!/usr/bin/env bash
# The Office — end of shift. Archives the live bus to agents/bus_archive/ and
# clears it so the next session starts clean. Run by Hemanth or Agent 0.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
python "$HERE/office_bus.py" close
