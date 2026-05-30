# The Office — Live Agent Bus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Governance override:** writing-plans defaults to "run in a dedicated worktree." **Worktrees are RETIRED (gov-v13, 2026-05-30).** Execute this **flat-on-master**, Path A commits (Agent 0 commits + auto-pushes per `feedback_push_after_commit`). Do NOT create a worktree.

**Goal:** A live message bus so open Claude agent-tabs talk to each other at their natural workflow breaks, with no Hemanth relaying between tabs.

**Architecture:** A gitignored append-only JSONL file (`agents/bus.jsonl`) is the channel. A `chat_send` CLI appends addressed messages. Delivery rides hooks already proven to inject context — `UserPromptSubmit` (start of each turn — also the moment Hemanth prompts an idle agent) + `PreToolUse:Bash` (before each terminal command, which agents run constantly). The delivery hook reads unseen messages for the current tab's agent and injects them as a `<system-reminder>`. A `session_id → agent_number` map (set by a one-time `office_join` at wake) tells the hook which agent this tab is. `office_close` archives + clears the bus.

**Tech Stack:** Bash hook scripts (matching `.claude/scripts/` conventions), `jq`, Claude Code hooks (`UserPromptSubmit`, `PreToolUse`), `.claude/settings.json`.

**Scope (v1):** Claude tabs only. Cross-engine (Codex/DeepSeek via MCP-pull) is increment 2 — DeepSeek's endpoint rejects injected context (`unknown variant system` 400), so it cannot receive hook-push. Auto-wake of idle tabs deferred. No loop caps (office-hours model: Hemanth opens by prompting, closes at end of shift).

---

## File structure

| File | Responsibility | New/Mod |
|------|----------------|---------|
| `scripts/office/office_lib.sh` | Shared helpers: bus path, atomic append, identity map, unseen filter, cursor | Create |
| `scripts/office/chat_send.sh` | Send primitive: resolve FROM, atomic-append addressed message | Create |
| `scripts/office/office_join.sh` | Register `session_id → agent_number` for this tab | Create |
| `scripts/office/office_close.sh` | Archive bus → `agents/bus_archive/`, clear live bus + cursors | Create |
| `.claude/scripts/office-deliver.sh` | Delivery hook: read unseen for this agent, inject system-reminder, advance cursor | Create |
| `.claude/settings.json` | Wire `office-deliver.sh` into `UserPromptSubmit` + `PreToolUse` | Modify |
| `.gitignore` | Ignore bus.jsonl, .bus_cursors/, bus_archive/*.jsonl, .office_sessions.json | Modify |
| `scripts/office/tests/test_office.sh` | Unit tests: append/identity/filter/cursor | Create |
| `agents/bus_archive/.gitkeep` | Keep archive dir tracked | Create |

**State files (all gitignored):** `agents/bus.jsonl` (live channel), `agents/.bus_cursors/<agent>.seq` (per-agent last-seen seq), `.claude/.office_sessions.json` (`{"<session_id>":"<agent_number>"}`).

**Message schema:**
```json
{"ts":"2026-05-30T10:40:12+05:30","seq":42,"from":"agent1","to":"agent4","kind":"chat","arc":null,"msg":"StreamTypes.h is gone — skip it in your deletion pass"}
```
- `to`: single (`"agent4"`), comma-list (`"agent4,agent2"`), or `"all"`.
- `kind`/`arc`: Congress-aware, reserved. v1 always `"chat"`/`null`.
- `seq`: monotonic int, the cursor key.

---

## Task 1: Feasibility probe — confirm hooks inject into a live agent (LOAD-BEARING, do first)

**Files:** Create (temp) `.claude/scripts/office-probe.sh`; Modify (temp) `.claude/settings.json`

- [ ] **Step 1: Write a probe hook that injects a unique marker**

`.claude/scripts/office-probe.sh`:
```bash
#!/usr/bin/env bash
# TEMPORARY Office feasibility probe.
case "${ANTHROPIC_BASE_URL:-}" in *deepseek*) exit 0 ;; esac
echo "<system-reminder>OFFICE-PROBE: injection works (event=${1:-?}, ts=$(date +%H:%M:%S))</system-reminder>"
exit 0
```

- [ ] **Step 2: Wire temporarily into UserPromptSubmit AND PreToolUse(Bash)**

Add to each array in `.claude/settings.json`:
```json
{ "type": "command", "command": "bash .claude/scripts/office-probe.sh userprompt", "timeout": 3 }
```
(use `pretool` as the arg in the PreToolUse copy.)

- [ ] **Step 3: Verify both inject**

Submit a prompt; run any Bash command. Confirm the `OFFICE-PROBE:` reminder appears for both events.
Expected: marker visible after a prompt (UserPromptSubmit) and around a Bash call (PreToolUse).

- [ ] **Step 4: Record verdict, remove the probe**

Both inject → proceed as written. Only one → use that event for delivery (note which). Neither → STOP, escalate to Hemanth, switch to MCP-pull (spec §7). Then delete `office-probe.sh` and revert both probe entries.

- [ ] **Step 5: Commit the verdict**

```bash
git add docs/superpowers/plans/2026-05-30-the-office-live-agent-bus.md
git commit -m "office: feasibility probe verdict — UserPromptSubmit+PreToolUse inject: <YES/NO>"
```

---

## Task 2: Bus library + atomic append

**Files:** Create `scripts/office/office_lib.sh`; Test `scripts/office/tests/test_office.sh`

- [ ] **Step 1: Write the failing test**

`scripts/office/tests/test_office.sh`:
```bash
#!/usr/bin/env bash
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../office_lib.sh"
fail=0
assert_eq() { if [ "$1" != "$2" ]; then echo "FAIL: $3 (got '$1' want '$2')"; fail=1; else echo "ok: $3"; fi; }

export OFFICE_DIR="$(mktemp -d)"
export OFFICE_BUS="$OFFICE_DIR/bus.jsonl"
office_append "agent1" "agent4" "chat" "null" "hello"
office_append "agent4" "agent1" "chat" "null" "hi back"
assert_eq "$(wc -l < "$OFFICE_BUS" | tr -d ' ')" "2" "two appends -> two lines"
assert_eq "$(tail -1 "$OFFICE_BUS" | jq -r .seq)" "2" "second seq == 2"
assert_eq "$(head -1 "$OFFICE_BUS" | jq -r .from)" "agent1" "first from agent1"
rm -rf "$OFFICE_DIR"
exit $fail
```

- [ ] **Step 2: Run, verify fail**

Run: `bash scripts/office/tests/test_office.sh`
Expected: FAIL — `office_lib.sh`/`office_append` not found.

- [ ] **Step 3: Implement `office_lib.sh`**

```bash
#!/usr/bin/env bash
# The Office — shared library. Source this; do not execute.
_office_repo_root() { cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd; }
OFFICE_DIR="${OFFICE_DIR:-$(_office_repo_root)/agents}"
OFFICE_BUS="${OFFICE_BUS:-$OFFICE_DIR/bus.jsonl}"
OFFICE_CURSORS="${OFFICE_CURSORS:-$OFFICE_DIR/.bus_cursors}"
OFFICE_SESSIONS="${OFFICE_SESSIONS:-$(_office_repo_root)/.claude/.office_sessions.json}"
OFFICE_LOCK="${OFFICE_BUS}.lock"

_office_lock() { local t=0; until mkdir "$OFFICE_LOCK" 2>/dev/null; do sleep 0.05; t=$((t+1)); [ $t -gt 100 ] && return 1; done; }
_office_unlock() { rmdir "$OFFICE_LOCK" 2>/dev/null; }

office_next_seq() {
  local last=0
  [ -f "$OFFICE_BUS" ] && last=$(tail -1 "$OFFICE_BUS" 2>/dev/null | jq -r '.seq // 0' 2>/dev/null || echo 0)
  echo $((last + 1))
}

# office_append <from> <to> <kind> <arc> <msg>
office_append() {
  local from="$1" to="$2" kind="$3" arc="$4" msg="$5"
  mkdir -p "$(dirname "$OFFICE_BUS")"
  _office_lock || { echo "office: lock timeout" >&2; return 1; }
  local seq; seq=$(office_next_seq)
  local ts; ts=$(date --iso-8601=seconds 2>/dev/null || date +%Y-%m-%dT%H:%M:%S%z)
  local arcjson="null"; [ "$arc" != "null" ] && arcjson="\"$arc\""
  jq -cn --arg ts "$ts" --argjson seq "$seq" --arg from "$from" --arg to "$to" \
        --arg kind "$kind" --argjson arc "$arcjson" --arg msg "$msg" \
        '{ts:$ts,seq:$seq,from:$from,to:$to,kind:$kind,arc:$arc,msg:$msg}' >> "$OFFICE_BUS"
  _office_unlock
}
```

- [ ] **Step 4: Run, verify pass**

Run: `bash scripts/office/tests/test_office.sh`
Expected: all `ok:`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_lib.sh scripts/office/tests/test_office.sh
git commit -m "office: bus library + atomic append (locked, seq-numbered) + tests"
```

---

## Task 3: Agent-identity map (session_id → agent number)

**Files:** Modify `scripts/office/office_lib.sh`; Create `scripts/office/office_join.sh`; Test appended

- [ ] **Step 1: Add failing tests**

Append before `exit $fail` in the test file:
```bash
export OFFICE_SESSIONS="$OFFICE_DIR/sessions.json"
office_join "sess-abc" "4"
office_join "sess-xyz" "1"
assert_eq "$(office_agent_for_session sess-abc)" "agent4" "session->agent4"
assert_eq "$(office_agent_for_session sess-xyz)" "agent1" "session->agent1"
assert_eq "$(office_agent_for_session unknown)" "" "unknown -> empty"
```

- [ ] **Step 2: Run, verify new cases fail**

Run: `bash scripts/office/tests/test_office.sh`
Expected: FAIL — `office_join`/`office_agent_for_session` not found.

- [ ] **Step 3: Implement in `office_lib.sh`**

Append:
```bash
# office_join <session_id> <agent_number>
office_join() {
  local sid="$1" num="$2"
  mkdir -p "$(dirname "$OFFICE_SESSIONS")"
  [ -f "$OFFICE_SESSIONS" ] || echo '{}' > "$OFFICE_SESSIONS"
  local tmp; tmp="$(mktemp)"
  jq --arg sid "$sid" --arg num "$num" '.[$sid]=$num' "$OFFICE_SESSIONS" > "$tmp" && mv "$tmp" "$OFFICE_SESSIONS"
}
# office_agent_for_session <session_id> -> "agentN" or ""
office_agent_for_session() {
  local sid="$1"
  [ -f "$OFFICE_SESSIONS" ] || { echo ""; return; }
  local num; num=$(jq -r --arg sid "$sid" '.[$sid] // empty' "$OFFICE_SESSIONS" 2>/dev/null)
  [ -n "$num" ] && echo "agent${num}" || echo ""
}
```

- [ ] **Step 4: Create `office_join.sh` entrypoint**

```bash
#!/usr/bin/env bash
# Usage: office_join.sh <agent_number> [session_id]
# Run once at wake so the delivery hook maps this tab -> agent N.
set -u
source "$(dirname "$0")/office_lib.sh"
NUM="$1"
SID="${2:-${CLAUDE_SESSION_ID:-}}"
[ -z "$SID" ] && { echo "office_join: no session id (pass arg 2 or set CLAUDE_SESSION_ID)" >&2; exit 1; }
office_join "$SID" "$NUM"
echo "office: this tab registered as agent${NUM} (session ${SID})"
```

- [ ] **Step 5: Run tests, verify pass; commit**

Run: `bash scripts/office/tests/test_office.sh` → all `ok:`.
```bash
git add scripts/office/office_lib.sh scripts/office/office_join.sh scripts/office/tests/test_office.sh
git commit -m "office: agent-identity map + office_join entrypoint + tests"
```

---

## Task 4: Unseen-message filter + cursor

**Files:** Modify `scripts/office/office_lib.sh`; Test appended

- [ ] **Step 1: Add failing tests**

Append before `exit $fail`:
```bash
export OFFICE_CURSORS="$OFFICE_DIR/cursors"
# bus has seq1 agent1->agent4, seq2 agent4->agent1
office_append "agent2" "all" "chat" "null" "broadcast hi"   # seq3
assert_eq "$(office_unseen_for agent4 | jq -s 'length')" "2" "agent4 sees seq1 + seq3(all)"
office_mark_seen "agent4" 3
assert_eq "$(office_unseen_for agent4 | jq -s 'length')" "0" "after mark-seen nothing for agent4"
assert_eq "$(office_unseen_for agent1 | jq -s 'length')" "2" "agent1 sees seq2 + seq3, not own seq1"
```

- [ ] **Step 2: Run, verify fail**

Run: `bash scripts/office/tests/test_office.sh`
Expected: FAIL — `office_unseen_for`/`office_mark_seen` not found.

- [ ] **Step 3: Implement in `office_lib.sh`**

Append:
```bash
office_cursor_get() { cat "$OFFICE_CURSORS/$1.seq" 2>/dev/null || echo 0; }
office_mark_seen() { mkdir -p "$OFFICE_CURSORS"; echo "$2" > "$OFFICE_CURSORS/$1.seq"; }
# office_unseen_for <agentN> -> JSON lines: to==me|all, from!=me, seq>cursor
office_unseen_for() {
  local me="$1" cur; cur=$(office_cursor_get "$me")
  [ -f "$OFFICE_BUS" ] || return 0
  jq -c --arg me "$me" --argjson cur "$cur" '
    select(.seq > $cur) | select(.from != $me)
    | select(.to == "all" or .to == $me or ((.to | split(",")) | index($me)))
  ' "$OFFICE_BUS"
}
```

- [ ] **Step 4: Run, verify pass**

Run: `bash scripts/office/tests/test_office.sh` → all `ok:`.

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_lib.sh scripts/office/tests/test_office.sh
git commit -m "office: unseen filter (direct+all, excl self, seq>cursor) + cursor advance + tests"
```

---

## Task 5: `chat_send` primitive

**Files:** Create `scripts/office/chat_send.sh`

- [ ] **Step 1: Implement**

```bash
#!/usr/bin/env bash
# Usage: chat_send.sh "@agent4" "message"   |   chat_send.sh "@all" "broadcast"
set -u
source "$(dirname "$0")/office_lib.sh"
TO_RAW="${1:-}"; MSG="${2:-}"
[ -z "$TO_RAW" ] || [ -z "$MSG" ] && { echo "usage: chat_send.sh \"@agentN|@all\" \"message\"" >&2; exit 1; }
TO="${TO_RAW#@}"
FROM="$(office_agent_for_session "${CLAUDE_SESSION_ID:-}")"
[ -z "$FROM" ] && { echo "chat_send: tab not registered — run scripts/office/office_join.sh <N> first" >&2; exit 1; }
office_append "$FROM" "$TO" "chat" "null" "$MSG" && echo "office: sent ${FROM} -> ${TO}"
```

- [ ] **Step 2: Manual smoke**

```bash
export CLAUDE_SESSION_ID=test-sess
bash scripts/office/office_join.sh 0 test-sess
bash scripts/office/chat_send.sh "@agent1" "ping from agent0"
tail -1 agents/bus.jsonl
```
Expected: last line `"from":"agent0","to":"agent1","msg":"ping from agent0"`.
Cleanup: `rm -f agents/bus.jsonl; rm -rf agents/.bus_cursors; rm -f .claude/.office_sessions.json`

- [ ] **Step 3: Commit**

```bash
git add scripts/office/chat_send.sh
git commit -m "office: chat_send primitive (resolves FROM from tab identity)"
```

---

## Task 6: Delivery hook

**Files:** Create `.claude/scripts/office-deliver.sh`

- [ ] **Step 1: Implement**

```bash
#!/usr/bin/env bash
# Office delivery hook (UserPromptSubmit + PreToolUse). < 300ms; always exit 0.
case "${ANTHROPIC_BASE_URL:-}" in *deepseek*) exit 0 ;; esac
source "$(cd "$(dirname "$0")/../.." && pwd)/scripts/office/office_lib.sh" 2>/dev/null || exit 0
PAYLOAD="$(cat 2>/dev/null)"
SID="$(printf '%s' "$PAYLOAD" | jq -r '.session_id // empty' 2>/dev/null)"
[ -z "$SID" ] && SID="${CLAUDE_SESSION_ID:-}"
[ -z "$SID" ] && exit 0
ME="$(office_agent_for_session "$SID")"; [ -z "$ME" ] && exit 0
UNSEEN="$(office_unseen_for "$ME")"; [ -z "$UNSEEN" ] && exit 0
MAXSEQ="$(printf '%s\n' "$UNSEEN" | jq -s 'max_by(.seq).seq')"
LINES="$(printf '%s\n' "$UNSEEN" | jq -r '"  • \(.from): \(.msg)"')"
office_mark_seen "$ME" "$MAXSEQ"
cat <<EOF
<system-reminder>
[THE OFFICE] New message(s) for ${ME} (reply: bash scripts/office/chat_send.sh "@agentN" "..."):
${LINES}
</system-reminder>
EOF
exit 0
```

- [ ] **Step 2: Manual smoke**

```bash
export CLAUDE_SESSION_ID=s4; bash scripts/office/office_join.sh 4 s4
export CLAUDE_SESSION_ID=s1; bash scripts/office/office_join.sh 1 s1
bash scripts/office/chat_send.sh "@agent4" "your work blocks mine"
echo '{"session_id":"s4"}' | bash .claude/scripts/office-deliver.sh
```
Expected: prints `[THE OFFICE] ... • agent1: your work blocks mine`. Re-run last line → NOTHING (cursor advanced).
Cleanup as Task 5.

- [ ] **Step 3: Commit**

```bash
git add .claude/scripts/office-deliver.sh
git commit -m "office: delivery hook — session->agent, inject unseen, advance cursor"
```

---

## Task 7: Wire hooks + gitignore

**Files:** Modify `.claude/settings.json`, `.gitignore`; Create `agents/bus_archive/.gitkeep`

- [ ] **Step 1: gitignore**

Append to `.gitignore`:
```
# The Office — live agent bus (ephemeral, local-only)
agents/bus.jsonl
agents/.bus_cursors/
agents/bus_archive/*.jsonl
.claude/.office_sessions.json
```

- [ ] **Step 2: keep archive dir**

```bash
mkdir -p agents/bus_archive && touch agents/bus_archive/.gitkeep
```

- [ ] **Step 3: wire delivery into both hooks**

Add to `UserPromptSubmit` and `PreToolUse`(matcher `Bash`) arrays in `.claude/settings.json` (use only the event(s) Task 1 confirmed injectable):
```json
{ "type": "command", "command": "bash .claude/scripts/office-deliver.sh", "timeout": 3 }
```

- [ ] **Step 4: validate JSON**

Run: `jq . .claude/settings.json > /dev/null && echo valid`
Expected: `valid`.

- [ ] **Step 5: Commit**

```bash
git add .claude/settings.json .gitignore agents/bus_archive/.gitkeep
git commit -m "office: wire delivery into UserPromptSubmit + PreToolUse; gitignore bus state"
```

---

## Task 8: `office_close` — archive + clear

**Files:** Create `scripts/office/office_close.sh`

- [ ] **Step 1: Implement**

```bash
#!/usr/bin/env bash
# Usage: office_close.sh  — end of shift. Archive live bus + clear.
set -u
source "$(dirname "$0")/office_lib.sh"
[ -s "$OFFICE_BUS" ] || { echo "office: already closed (no live bus)"; exit 0; }
ARCH_DIR="$(dirname "$OFFICE_BUS")/bus_archive"; mkdir -p "$ARCH_DIR"
BASE="$ARCH_DIR/$(date +%Y-%m-%d)"; DEST="$BASE.jsonl"; n=1
while [ -e "$DEST" ]; do DEST="$BASE-$n.jsonl"; n=$((n+1)); done
mv "$OFFICE_BUS" "$DEST"; rm -rf "$OFFICE_CURSORS"
echo "office: closed — archived $(wc -l < "$DEST" | tr -d ' ') msg(s) to ${DEST}; live bus cleared."
```

- [ ] **Step 2: Manual smoke**

```bash
export CLAUDE_SESSION_ID=s1; bash scripts/office/office_join.sh 1 s1
bash scripts/office/chat_send.sh "@all" "end of day"
bash scripts/office/office_close.sh
ls agents/bus_archive/*.jsonl; test ! -f agents/bus.jsonl && echo "live bus cleared"
```
Expected: archive file with the message; `live bus cleared`.
Cleanup: `rm -f agents/bus_archive/*.jsonl; rm -rf agents/.bus_cursors; rm -f .claude/.office_sessions.json`

- [ ] **Step 3: Commit**

```bash
git add scripts/office/office_close.sh
git commit -m "office: office_close — archive bus to dated file + clear live bus/cursors"
```

---

## Task 9: End-to-end acceptance (two real tabs)

**Files:** none (live verification)

- [ ] **Step 1: Register two tabs** — Tab A (Agent 0): `bash scripts/office/office_join.sh 0`. Tab B (Agent 1): `bash scripts/office/office_join.sh 1`. Each prints `registered as agentN`.

- [ ] **Step 2: A → B live pickup** — Tab A: `bash scripts/office/chat_send.sh "@agent1" "acceptance ping"`. Tab B: submit a prompt OR run a Bash command. Expected: Tab B sees `[THE OFFICE] ... agent0: acceptance ping` without Hemanth pasting it.

- [ ] **Step 3: B → A + broadcast** — Tab B: `chat_send.sh "@agent0" "pong"` → Tab A sees at next break. Tab A: `chat_send.sh "@all" "all-hands"` → both see it.

- [ ] **Step 4: Idle-wait (documents deferred auto-wake)** — Leave Tab B fully idle; send from A; confirm B does NOT see it until next touched. Expected behavior.

- [ ] **Step 5: Close** — `bash scripts/office/office_close.sh`. Confirm archive written + live bus gone.

- [ ] **Step 6: Final commit + chat announce**

```bash
git add docs/superpowers/plans/2026-05-30-the-office-live-agent-bus.md
git commit -m "office: v1 acceptance passed — live two-tab bus working"
```
Post a chat.md note: Office is live; brotherhood commands = `office_join.sh <N>` at wake, `chat_send.sh "@agentN" "..."`, `office_close.sh` at shift end; auto-wake + cross-engine deferred.

---

## Self-review notes

- **Spec coverage:** free-form ✓ T5; agents↔agents ✓; natural-break pickup ✓ T6-7 (UserPromptSubmit covers the "Hemanth prompts a quiet agent" case + PreToolUse covers mid-work seams); idle-waits ✓ T9.4; no caps ✓ (none built); bus.jsonl + reserved kind/arc ✓ T2; layered delivery+send ✓ T5/T6; auto-open via first append ✓; close=archive+clear ✓ T8; deferrals (auto-wake/caps/cross-engine) excluded ✓; feasibility items (injection ✓ T1, identity ✓ T3, atomic append ✓ T2) ✓; acceptance ✓ T9.
- **DeepSeek guard** (`*deepseek*) exit 0`) in every injecting hook, matching `session-brief.sh`/`congress-check.sh`.
- **Name consistency:** office_append / office_join / office_agent_for_session / office_unseen_for / office_mark_seen / office_cursor_get / office_next_seq — consistent T2-T8. Paths OFFICE_BUS/OFFICE_CURSORS/OFFICE_SESSIONS consistent.
- **Deps:** `jq` (already used across `.claude/scripts/`); identity via `CLAUDE_SESSION_ID` env and/or hook-stdin `.session_id` (T1 confirms which; T3 supports both).
- **Governance:** flat-on-master (gov-v13), Path A commits, auto-push per `feedback_push_after_commit`.
