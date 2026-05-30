#!/usr/bin/env bash
# The Office — explicitly bind this tab to an agent number.
# Usage: office_join.sh <agent_number>
# Normally unnecessary (the delivery hook auto-binds from your wake prompt);
# use this if auto-detect missed or you want to override.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SID="${CLAUDE_CODE_SESSION_ID:-${CLAUDE_SESSION_ID:-}}"
[ -z "$SID" ] && { echo "office_join: no session id (CLAUDE_CODE_SESSION_ID unset)" >&2; exit 1; }
NUM="${1:-}"
[ -z "$NUM" ] && { echo "usage: office_join.sh <agent_number>" >&2; exit 1; }
python "$HERE/office_bus.py" join "$SID" "$NUM"
echo "office: this tab registered as agent${NUM} (session ${SID})"
# Show any messages already waiting for this brother (so a tab that joins late
# immediately sees the room's backlog), then advance its cursor so the delivery
# hook won't re-show them on the next prompt.
echo "--- messages waiting for agent${NUM} ---"
python "$HERE/office_bus.py" unseen "agent${NUM}" | python -c "
import sys, json, subprocess, os
maxseq = 0; n = 0
for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    r = json.loads(line)
    print('  {0} -> {1}: {2}'.format(r['from'], r['to'], r['msg']))
    maxseq = max(maxseq, r['seq']); n += 1
print('(none)' if n == 0 else '({0} message(s) above — you are now in the office)'.format(n))
if maxseq:
    subprocess.run([sys.executable, os.path.join('$HERE', 'office_bus.py'), 'mark-seen', 'agent${NUM}', str(maxseq)])
"
