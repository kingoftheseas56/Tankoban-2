# Audit - Owned-Worker Responder Feasibility - 2026-05-31

By Agent 7 (Codex). For Agent 0 / The Office.
Reference comparison: local Office responder spec, Agent 7 Codex bridge, Office bus contract, Office app v2 spec, Claude Code CLI / Agent SDK docs.
Scope: review only. This stress-tests the fallback responder design, the proposed `claude -p` mechanism, and the "backup reply as brother" honesty model. No code was changed outside this audit.

## Bottom line

Do not build it exactly as written. Build the narrow responder, yes, but fix the trigger semantics and the Claude invocation contract first.

The idea is sound: a reply-only owned worker that waits for the real tab, then answers if the tab stays silent, is the right first cure for idle-agent stalls. The current spec is too optimistic about two places:

- **Fallback detection:** "did Agent N post anything after seq X?" is too coarse. It prevents some duplicate replies, but it also drops real unanswered messages when the brother posts unrelated chatter, activity, or a reply to someone else.
- **Claude driver:** `claude -p` is viable in principle, but it is not a Codex flag-for-flag clone. Local and official evidence show it needs an explicit wrapper contract: permission mode, tool list, hook isolation, strict MCP config, no session persistence, timeout, schema extraction, and fallback parsing.

Blunt answer: **build after fixing X/Y/Z first**:

1. Replace the silence check with an addressed-message claim/recheck protocol keyed to target message seq.
2. Lock down and smoke-test the exact `claude -p` command before writing the responder loop.
3. Strengthen honesty rules so backup replies cannot commit the brother to domain decisions or implementation promises.

## Sources

Local sources:

- `docs/superpowers/specs/2026-05-31-owned-worker-responder-design.md:20-27` - fallback model and silence check.
- `docs/superpowers/specs/2026-05-31-owned-worker-responder-design.md:32-43` - proposed architecture and `claude -p` driver.
- `docs/superpowers/specs/2026-05-31-owned-worker-responder-design.md:49-63` - honesty and safety claims.
- `docs/superpowers/specs/2026-05-31-owned-worker-responder-design.md:75-81` - open items on fallback window, schema, and cursor sharing.
- `docs/superpowers/specs/2026-05-31-the-office-app-design.md:46-60` - capability contract and bus-is-transport framing.
- `docs/superpowers/specs/2026-05-31-the-office-app-design.md:111-123` - quiesce/fence and cross-engine caution.
- `scripts/office/codex_agent7_bridge.py:44-69` - Codex bridge schema.
- `scripts/office/codex_agent7_bridge.py:212-254` - Codex invocation with read-only sandbox and output schema.
- `scripts/office/codex_agent7_bridge.py:298-326` - mark-seen-before-draft and post path.
- `scripts/office/office_bus.py:96-115` - append lock and monotonic seq.
- `scripts/office/office_bus.py:150-162` - one cursor file per agent id.
- `scripts/office/office_bus.py:181-196` - unseen filtering.
- `scripts/office/office_bus.py:211-216` - send resolves identity from session id.
- `scripts/office/office_bus.py:256-341` - prompt-time delivery advances the agent cursor.
- `scripts/office/office_watch.sh:28-44` - watch uses a private high-water mark and does not advance the cursor.

External docs checked:

- Claude Code CLI reference: `--print/-p`, `--json-schema`, `--output-format`, `--permission-mode`, `--tools`, `--no-session-persistence`, and related flags are documented at https://code.claude.com/docs/en/cli-usage.
- Claude Code permission modes: default and plan are read-only baselines; `dontAsk` means only pre-approved tools run; see https://code.claude.com/docs/en/permission-modes.
- Claude Code Agent SDK overview: SDK exposes built-in tools, MCP, permissions, sessions, and programmatic query loops; see https://code.claude.com/docs/en/agent-sdk/overview.
- Claude Code structured outputs: Agent SDK structured outputs validate against JSON Schema and return `structured_output`, with explicit error subtypes on validation failure; see https://code.claude.com/docs/en/agent-sdk/structured-outputs.

Local CLI smoke:

- `claude --help` on this machine lists `--print`, `--json-schema`, `--output-format`, `--permission-mode`, `--tools`, `--strict-mcp-config`, `--no-session-persistence`, and `--bare`.
- A tiny `claude -p --json-schema ... --output-format json` smoke completed, but returned a normal `result` string, not an obvious `structured_output` field, and a project hook still attempted an MCP memory search despite `--tools ""`.
- A stricter `--bare --strict-mcp-config --output-format stream-json` smoke timed out at 120s. This does not prove the mode is unusable; it proves the implementation must start with a reproducible CLI contract test.

## Observations

### Part A - Fallback model

**Risk: high - silence check is semantically wrong.**  
Observed: the spec says wait after seq X, then check whether Agent N posted anything after X; any post means the real brother handled it (`owned-worker-responder-design.md:26`, `:37-39`). That is not equivalent to "the target message was answered." A brother can post an unrelated RTC, broadcast, blocker line, reply to another agent, or activity mirror after X. Under the current rule, the backup drops the pending direct request even though nobody answered it.

**Risk: medium - race at second 59 is mostly survivable, but only if there is a final just-before-post recheck.**  
Observed: the spec has one silence check after the fallback window (`owned-worker-responder-design.md:37-40`). If the tab posts at 59s and append completes before the worker checks, good. If the worker checks at 60s, starts a 30-90s `claude -p` call, and the tab posts at 61s, both can answer unless the responder rechecks immediately before `send`. The Codex bridge has no fallback window and posts after model return (`codex_agent7_bridge.py:318-326`), so this race is new.

**Risk: medium - separate responder cursor is necessary, but the current bus only has one cursor per agent id.**  
Observed: `office_bus.py` stores cursors as `.bus_cursors/<agent>.seq` (`office_bus.py:150-162`). The tab's prompt-time delivery advances `agentN` (`office_bus.py:326-331`), and the existing Codex bridge also marks seen as `agent7` (`codex_agent7_bridge.py:147-149`, `:298-300`). The spec says the responder should use its own session/cursor (`owned-worker-responder-design.md:81`), but with the current bus API a responder that calls `mark-seen agentN` will fight the tab. It needs either `responder-agentN` as a cursor namespace or a separate responder state file. Identity for posting can still map to `agentN`; cursor namespace must not.

**Risk: medium - @all fallback will be noisy without explicit useful-only gates.**  
Observed: the responder watches direct or `@all` messages (`owned-worker-responder-design.md:36`) and the existing bridge filters "generic broadcasts" by prompt instruction, not deterministic code (`codex_agent7_bridge.py:193-199`). If every brother's fallback responder treats a room-wide message as pending, one `@all` could fan out into multiple paid `claude -p` calls and multiple backup replies. The spec says "don't ack every @all" (`owned-worker-responder-design.md:63`), but that needs deterministic prefilters: explicit mention, question mark, requested ACK token, or sender allowlist.

**Risk: low - append-level double-post from two responders of the same identity is avoidable.**  
Observed: bus append is locked (`office_bus.py:96-115`), and the spec calls for a per-agent single-instance lock (`owned-worker-responder-design.md:61`). That is enough to prevent two responder processes for the same brother if the lock is implemented with stale-lock recovery. It does not prevent tab-vs-responder double-post; that is a semantic race, not a file-lock race.

**Risk: medium - mark-seen-before-drafting prevents loops but can lose messages on transient driver failures.**  
Observed: the spec inherits mark-seen-before-draft from the Codex bridge (`owned-worker-responder-design.md:41`; `codex_agent7_bridge.py:298-300`). This is right for loop safety. It also means an overloaded/timed-out `claude -p` call consumes the trigger and posts nothing. For a "guaranteed responder" feature, that is an honest failure mode only if the room records "backup failed for seq X" as an activity/error event.

**Risk: medium - identity safety is structurally okay but depends on session-map discipline.**  
Observed: `office_bus.py send` resolves `from` through session id (`office_bus.py:211-216`), and the spec requires `responder-agentN -> agentN` plus `whoami` guard (`owned-worker-responder-design.md:35`). That blocks fake-Hemanth style mistakes if every responder owns a unique session id and refuses mismatches. It does not stop the worker from writing text that overclaims as the brother; that is an honesty/prompt problem.

### Part B - `claude -p` mechanism

**Risk: medium - `claude -p` is the right first mechanism, but the spec must stop saying "direct equivalent of `codex exec`."**  
Observed: official CLI docs and local `claude --help` confirm non-interactive `-p`, output formats, JSON schema flag, permission mode, tool controls, and no-session-persistence. That makes `claude -p` viable for a narrow responder. But the Codex bridge uses `codex exec -s read-only --output-schema -o out.json` (`codex_agent7_bridge.py:217-232`). Claude's surface is different: `--permission-mode default|plan`, `--tools`, `--disallowedTools`, `--strict-mcp-config`, `--json-schema`, and output parsing. Treat it as a new driver.

**Risk: high - structured output is not yet proven in the exact CLI shape this script needs.**  
Observed: docs say `--json-schema` exists in print mode, and Agent SDK structured outputs validate schema with success/error subtypes. Local smoke with CLI returned a free-form `result` string in the result wrapper rather than an obvious structured object. This could be quoting, output-format, model behavior, or a CLI/schema contract nuance. Until a test proves `{"replies":[...]}` is extractable and validated, the responder must not assume schema parity with Codex.

**Risk: high - read-only is not a sandbox unless the tool surface and hooks are locked.**  
Observed: permission docs say default/plan are read-only baselines. Local smoke still triggered a project hook attempting an MCP memory search even with `--tools ""`. Also, "read-only" still allows reading repo files and potentially MCP reads. The responder should run with a temporary settings file or `--bare` if workable, `--permission-mode default` or `plan`, explicit `--tools "Read,Glob,Grep"` only if file reads are needed, `--disallowedTools "Edit,Write,Bash"` as belt-and-suspenders, `--strict-mcp-config`, and no inherited MCP unless deliberately allowed.

**Risk: medium - cost/latency is materially higher than the spec implies.**  
Observed: the trivial local smoke took about 34s and reported non-trivial token/cost due loaded context and hooks. That makes a 60s fallback window plus a 30-90s driver call more like a 90-150s response in the common case. It may still cure idle agents, but it is not instant. The prompt must be minimal and use cheap deterministic Python context assembly.

**Risk: medium - Agent SDK is cleaner for production, but probably too heavy for the first cure.**  
Observed: Agent SDK exposes the same tools, MCP, permissions, sessions, and structured output programmatically. For a durable Office process host, SDK is the better long-term abstraction: typed options, stream events, structured output objects, easier cancellation/observability. For a one-file pilot, `claude -p` is acceptable if the exact command is contract-tested. Do not build the future owned-worker substrate around subprocess text parsing if Slice 2A is already becoming a process host.

### Part C - scope and honesty

**Risk: low - reply-only coordination responder is the right narrow first cure.**  
Observed: parent spec correctly says owned workers and capability contracts are the hard layer, and that the bus is transport, not truth (`the-office-app-design.md:48-56`). The responder deliberately avoids deep code work (`owned-worker-responder-design.md:16`, `:67-71`). That is the right scope. Do not jump straight to autonomous worker.

**Risk: high - "posts only as Agent N" plus an honesty prefix is not enough for domain authority.**  
Observed: the spec says backup posts as Agent N and prefixes `[auto - AgentN's tab was idle]` (`owned-worker-responder-design.md:40`, `:49-53`). That is honest to readers, but it still appears in the room as the brother's identity and can be quoted later. For domain decisions, the backup must phrase itself as non-binding: "backup read," "owner to confirm," "I will not commit Agent N to this." Otherwise it can accidentally create commitments the real brother never made.

**Risk: medium - "real-brother-first" needs a typed reply class.**  
Observed: the spec distinguishes simple ack/coordination from substantive calls in prose (`owned-worker-responder-design.md:53`). The implementation should encode reply classes: `ack`, `handoff`, `clarifying_question`, `nonbinding_assessment`, `decline_owner_confirmation_required`. This is how the responder avoids pretending to be a domain master.

**Risk: medium - fallback replies should be visibly machine-authored in the bus schema, not only in message text.**  
Observed: bus schema today has `from`, `to`, `kind`, `arc`, `msg` (`office_bus.py:14-17`). If honesty is only a text prefix, downstream roster/status/Office UI cannot reliably style or filter backup replies. The transport should eventually carry metadata like `kind: "auto_reply"` or `arc/meta: {"backup_for":"agentN","trigger_seq":X}`. If changing bus schema is out of scope for the pilot, use a prefix now but plan schema metadata next.

## Recommendations

### Fixes before implementation

**High - Replace "any Agent N post after X" with target-aware resolution.**  
Why: unrelated posts should not suppress a direct unanswered request. Minimum viable rule:

- Track each candidate trigger by `trigger_seq`, sender, target agent, and message text hash.
- After the fallback window, ignore the trigger only if Agent N posted a chat message after `trigger_seq` that plausibly answers that sender/thread, or if the trigger was explicitly claimed/cleared by the tab.
- Before posting, re-read bus tail and run the same check again.

Best version: add explicit lightweight bus metadata for `reply_to_seq` or `handled_seq`. Without that, use conservative heuristics and err toward nonbinding backup replies.

**High - Separate responder cursor/state from tab cursor.**  
Why: the current cursor namespace is per agent id. Do not let responder `mark-seen agentN`; that can hide messages from the real tab's `deliver`/`drain`. Use `.bus_responder_cursors/agentN.seq` or `mark-seen responder-agentN` for responder state while keeping `office_bus.py send responder-agentN ...` mapped to `from=agentN`.

**High - Prove the exact `claude -p` contract in a unit/integration smoke before responder logic.**  
Why: docs say the pieces exist; local smoke did not yet prove the exact structured-output payload shape. Required acceptance:

- command exits 0 under trusted repo without interactive prompts;
- no file writes;
- no Bash/Edit/Write tools available;
- no inherited MCP/hooks unless deliberately allowed;
- returns parseable validated replies object;
- schema failure is detectable and logged;
- timeout path posts or records a backup-failed event without retry storms.

**Medium - Add final recheck immediately before send.**  
Why: closes the second-61 race where the tab answers while the headless model is drafting.

**Medium - Gate `@all` deterministically before invoking Claude.**  
Why: avoid N responders burning quota and cluttering the room. Only run the model for `@all` if the message names the agent, requests all-agent ACKs, or matches a narrow coordination pattern.

**Medium - Store backup/error metadata, even if the first pass is text-only.**  
Why: `[auto - tab idle]` is readable but not machine-truth. The Office UI will need to distinguish human-tab messages, backup replies, failed backup attempts, and activity lines.

### Recommended first implementation shape

Pilot one agent, probably Agent 4 or Agent 1 as the spec suggests, with a dry-run mode first:

1. Deterministic watcher finds candidate messages and records pending trigger state without posting.
2. Fallback timer fires and runs target-aware silence checks.
3. CLI contract test runs `claude -p` with a tiny prompt and schema.
4. Dry-run logs the reply object and the reason it would/would not post.
5. Only then enable posting with the honesty prefix and final pre-send recheck.

Recommended `claude -p` direction, subject to local smoke:

```powershell
claude -p `
  --no-session-persistence `
  --permission-mode default `
  --tools "Read,Glob,Grep" `
  --disallowedTools "Edit,Write,Bash" `
  --strict-mcp-config `
  --json-schema "<reply schema>" `
  --output-format json `
  "<minimal prompt>"
```

If repo-file reads are not needed for the first responder, use `--tools ""` and make the Python context packet carry everything. If hooks/MCP still leak in, prefer a temporary `--settings` profile or investigate `--bare`; do not hand-wave it.

### Agent SDK call

Use `claude -p` for the first pilot only if the CLI contract test passes. Move to Agent SDK when the Office host owns worker lifecycle beyond reply-only coordination. SDK is better for long-term process hosting because it gives programmatic options, streaming events, permissions, and structured outputs without scraping CLI result text.

## Hypothesized root causes (Agent 0 to validate)

- **Hypothesis -** The spec inherited the Codex bridge's "cursor plus mark-seen" loop without accounting for the fact that the responder is standing in for a separate live tab that already has its own cursor semantics. **Agent 0 to validate.**
- **Hypothesis -** The "any post after seq X" silence rule was optimized for avoiding embarrassment from duplicate replies, not for preserving unanswered direct requests. **Agent 0 to validate.**
- **Hypothesis -** The `claude -p` assumption came from current Claude Code docs, but the plan has not yet separated documentation-level support from the exact local project/hook/settings behavior. **Agent 0 to validate.**

## Risk register

High: Coarse silence check drops real unanswered messages. Why: any unrelated Agent N post after trigger seq suppresses fallback.

High: Structured output is not yet contract-proven. Why: local smoke returned a normal result string, and CLI behavior differs from Codex's `--output-schema -o file`.

High: Read-only/tool isolation is not yet contract-proven. Why: local hook/MCP behavior still appeared despite `--tools ""`; inherited project config can affect print-mode runs.

High: Backup can accidentally commit a brother to a domain position. Why: message is posted under `from=agentN`, and the prefix may be ignored later.

Medium: Tab-vs-responder race can double-post. Why: a tab can answer while `claude -p` is drafting unless there is a final pre-send recheck.

Medium: Cursor collision can starve the real tab. Why: current cursor files are keyed by `agentN`; responder needs independent state.

Medium: `@all` can become quota/noise amplification. Why: every brother's responder may see the same broadcast.

Medium: Timeout after mark-seen can silently lose the safety-net reply. Why: loop-safety consumes triggers before model success.

Low: Same-identity responder double process. Why: single-instance lock plus bus append lock are enough if implemented with stale-lock recovery.

Low: Narrow reply-only scope. Why: this is the right first slice and avoids autonomous code mutation.

## One-line summary for chat.md

Agent 7 audit written - agents/audits/owned_worker_responder_feasibility_2026-05-31.md. Bottom line: build the reply-only responder, but first fix target-aware fallback checks, separate responder cursor state, prove the exact claude -p schema/read-only contract, and make backup replies explicitly non-binding.
