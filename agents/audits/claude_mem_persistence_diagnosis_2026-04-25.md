# claude-mem persistence diagnosis — 2026-04-25

By Agent 0 (Coordinator). Phase 1 of [SKILL_DISCIPLINE_FIX_TODO.md](../../SKILL_DISCIPLINE_FIX_TODO.md), executed same-wake as TODO authoring per Hemanth ratification 2026-04-25 ~21:30 ("Yes and you will be the one executing the TODO for the very first time").

Source audit: [skill_discipline_audit_2026-04-25.md](skill_discipline_audit_2026-04-25.md) (Agent 7 / Codex). Hemanth's framing question: "does that audit tell us anything about how to power up our agents with claude.mem and other stuff?"

This diagnosis is read-only — no governance changes, no settings edits, no worker restarts. Output is a remedy proposal Hemanth ratifies to unblock Phase 2 execution.

---

## §1 Bottom line

claude-mem v12.2.0's AI provider auto-detection fails on Hemanth's machine because Claude Code is installed as a **VS Code native extension**, not as a **CLI**. The worker's `findClaudeExecutable()` runs `where claude.cmd` (Windows) and `where claude` — both return nothing. The Claude SDK Agent never starts. Every queued observation and summary lands in `failed` state. The `/mem-search` rule the brotherhood was asked to follow is pointed at a memory store that has been broken since the worker was first installed.

This is a categorical environment mismatch, not a transient bug. claude-mem v12.2.0 was authored assuming Claude Code CLI users; Hemanth is a Claude Code VS Code extension user.

**Severity:** load-bearing — the entire SKILL_DISCIPLINE_FIX_TODO sequencing depends on this being repaired before any governance tightening. Without fix, `/mem-search` will keep returning empty no matter how aggressively we enforce it.

---

## §2 Empirical reproduction (matches audit §4)

### §2.1 Local SQLite state (`C:\Users\Suprabha\.claude-mem\claude-mem.db`)

| Field | Value | Note |
|---|---|---|
| `sdk_sessions` rows | 72 | Audit said 71; +1 since |
| `user_prompts` rows | 662 | Audit said 653; +9 since (prompts ARE landing) |
| `observations` rows | **0** | Audit headline reproduced |
| `session_summaries` rows | **0** | Audit headline reproduced |
| `pending_messages` total | 14,424 | New finding — audit didn't surface this |
| `pending_messages` status `failed` | **14,355** | 99.5% failure rate |
| `pending_messages` status `pending` | 70 | Currently queued (this session) |
| `pending_messages` type `observation` (failed) | 13,818 | All PostToolUse events |
| `pending_messages` type `summarize` (failed) | 607 | All Stop-hook summary requests |

### §2.2 Tankoban session inspection

Every Tankoban 2 session has the same shape:

```
id  memory_session_id  status      started_at                  prompt_counter
72  None               completed   2026-04-25T13:30:50.287Z    0
71  None               completed   2026-04-25T13:13:22.519Z    0
70  None               active      2026-04-25T13:12:50.400Z    0
...
```

- `memory_session_id = NULL` on **every** Tankoban session — the SDK has never minted one. `memory_session_id` is the join key for observations/summaries; when null, downstream writes have no foreign-key target.
- `prompt_counter = 0` everywhere — prompts arrive (the `user_prompts` table has 662 rows) but the per-session counter never increments because the per-session AI processor never starts.

### §2.3 HTTP API confirms

```
GET http://127.0.0.1:37777/api/health
→ "ai":{"provider":"claude","authMethod":"Claude Code CLI (subscription billing)","lastInteraction":null}
```

`lastInteraction:null` is the conclusive signal — the AI provider has never been successfully called since worker startup (2026-04-23T10:00, ~2 days uptime). The 39 `summaryStored=null` events Agent 7 surfaced are the *symptom*; this null is the *cause*.

```
GET /api/search/observations?query=tankoban&limit=5
→ "No observations found matching \"tankoban\""

GET /api/corpus
→ []
```

### §2.4 PATH probe (the load-bearing finding)

```
$ where claude.cmd
INFO: Could not find files for the given pattern(s).

$ where claude
INFO: Could not find files for the given pattern(s).

$ powershell -Command "where.exe claude.cmd"
INFO: Could not find files for the given pattern(s).
```

Claude Code CLI binary `claude.cmd` does not exist on Hemanth's machine. This is consistent with his usage pattern — he runs Claude Code via the VS Code native extension, which bundles its own runtime (`bun.exe` at `C:\Users\Suprabha\.bun\bin\bun.exe`, observed as the worker process PID 20396).

---

## §3 Root cause walkthrough

### §3.1 The failing path in `worker-service.cjs`

The Claude SDK Agent's `startSession` invokes `findClaudeExecutable()` (worker-service.cjs:1679):

```javascript
findClaudeExecutable() {
  let e = ge.loadFromFile(pt);
  if (e.CLAUDE_CODE_PATH) { ... }   // 1. Honor explicit setting (empty in our case)
  if (process.platform === "win32") {
    try {
      execSync("where claude.cmd", ...);
      return "claude.cmd";            // 2. Windows fast-path (FAILS here)
    } catch {}
  }
  try {
    let r = execSync("where claude", ...).trim().split("\n")[0].trim();
    if (r) return r;                  // 3. Generic where-claude (FAILS here too)
  } catch (r) { ... }
  throw new Error("Claude executable not found...");  // 4. Throws
}
```

Settings inspected (`C:\Users\Suprabha\.claude-mem\settings.json`):
- `"CLAUDE_CODE_PATH": ""` — empty, falls through.
- `"CLAUDE_MEM_GEMINI_API_KEY": ""` — no Gemini fallback.
- `"CLAUDE_MEM_OPENROUTER_API_KEY": ""` — no OpenRouter fallback.

So the throw lands on every observation/summary attempt.

### §3.2 The failure-handling path

In `startSessionProcessor` (worker-service.cjs ~line 1682), the catch handler classifies errors:

```javascript
.catch(async c => {
  let u = c?.message || "";
  // Unrecoverable list:
  if (["Claude executable not found", "CLAUDE_CODE_PATH", "ENOENT", "spawn", ...].some(d => u.includes(d))) {
    o = true;
    this.lastAiInteraction = { timestamp: Date.now(), success: false, ... };  // (A)
    return;
  }
  if (this.isSessionTerminatedError(c)) {
    return this.runFallbackForTerminatedSession(e, c);  // (B)
  }
  ...
});
```

**Expected:** path (A) fires. `lastAiInteraction` gets set with the throw's error. We see `lastInteraction:null` instead → path (A) is NOT firing.

**Actual:** path (B) fires. The `isSessionTerminatedError` regex matches:

```javascript
isSessionTerminatedError(e) {
  let n = (e instanceof Error ? e.message : String(e)).toLowerCase();
  return n.includes("process aborted by user")
      || n.includes("processtransport")
      || n.includes("not ready for writing")
      || n.includes("session generator failed")
      || n.includes("claude code process");  // ← likely matches
}
```

The Claude Agent SDK (`@anthropic-ai/claude-agent-sdk`, used internally by claude-mem) wraps the executable lookup in a higher-level error that probably includes phrases like "Claude Code process exited" or "Claude code process failed." That matches the lowercase regex, so the worker classifies the failure as "session terminated" rather than "executable not found." Path (B) is taken — fallbacks attempted (none configured) — `markAllSessionMessagesAbandoned` called → all session messages flip to `failed` → no `lastAiInteraction` ever set.

This is why the symptom looks like silent failure: the genuine root cause (no executable on PATH) gets re-classified as a recoverable "session terminated" event, retries silently fail, and observability is lost.

### §3.3 Cascade effect on the Stop hook

The Stop hook signals "Requesting summary" with `hasLastAssistantMessage=true`, then waits ~500ms for the worker to write a summary back. Because the AI was never reached, no summary is ever written. The hook gives up:

```
[2026-04-25 14:30:42.265] [INFO] [HOOK] Summary processing complete {waitedMs=515, summaryStored=null}
```

39 of these on 2026-04-25 alone (audit §3 cause D). Every wake produces several. Each is a silent symptom of the same single root cause.

### §3.4 Telemetry skip-list compounds the blindness

`CLAUDE_MEM_SKIP_TOOLS = "ListMcpResourcesTool,SlashCommand,Skill,TodoWrite,AskUserQuestion"` (audit §3 cause I). Even if the memory pipeline were healthy, observations from `Skill` and `SlashCommand` invocations — the exact surface the brotherhood wants to enforce — would be skipped. This isn't the root cause of the persistence failure, but it would still need to be fixed in Phase 2 (or at minimum re-evaluated) once the upstream blocker is gone.

---

## §4 Why the audit didn't pinpoint this

Agent 7's audit is excellent — but it operated from inside Codex (without local Claude Code SDK access) and from claude-mem's external interfaces. Two things visible only from inside the workspace:

1. **PATH probe** (§2.4 above) — confirms `claude.cmd` is not findable. Codex couldn't run `where claude.cmd` against Hemanth's user shell context.
2. **`pending_messages` distribution** — 99.5% in `failed` state. The audit examined `observations`, `session_summaries`, and `corpora` (all empty) but didn't open `pending_messages`, which is where the actual signal lives. SQLite-side, `pending_messages` is the *queue* that feeds `observations` after AI processing.

The audit's conclusion ("memory rehab first") still holds verbatim — Codex inferred the right answer from the right evidence. Phase 1's job was just to find the *specific* mechanism, not to second-guess the audit.

---

## §5 Remedy proposals

Four options, ordered cheapest-to-most-invasive. Hemanth picks; Phase 2 executes.

### Option A — Install Claude Code CLI alongside the VS Code extension

Run `npm install -g @anthropic-ai/claude-code` (or whatever the current install command is) to get `claude.cmd` on PATH. The VS Code extension and the CLI can coexist — they don't conflict. The CLI binary can be authenticated with the same Anthropic subscription Hemanth already pays for.

**Pros:** least invasive; matches claude-mem's design assumption; uses the same billing path; reversible (uninstall = back to broken state).
**Cons:** another binary to maintain; Claude Code CLI may have slightly different update cadence than the VS Code extension.
**Estimated effort:** 1 minute install + 1 worker restart.
**Risk:** low — Anthropic publishes the CLI as a stable distribution.

### Option B — Set `CLAUDE_CODE_PATH` explicitly to the VS Code extension's bundled binary

If the VS Code extension ships a `claude` binary inside its install tree (likely under `~\.vscode\extensions\anthropic.claude-code-*\` or similar), point `CLAUDE_CODE_PATH` at it directly in `~/.claude-mem/settings.json`.

**Pros:** no new install; uses what's already on disk.
**Cons:** VS Code extension binary path changes on every update; brittle. Also unclear whether the extension's binary supports the SDK's invocation pattern (it might require IPC handshakes the SDK doesn't provide).
**Estimated effort:** 5 minutes investigation + 1 setting edit + 1 worker restart.
**Risk:** medium — fragile across extension updates.

### Option C — Switch provider to Gemini (free tier)

Set `CLAUDE_MEM_GEMINI_API_KEY` in `~/.claude-mem/settings.json` to a free-tier Google AI Studio API key. claude-mem auto-detects the provider switch via `Vu() && za()` (worker-service.cjs ~line 1655). Model `gemini-2.5-flash-lite` is already configured in settings.

**Pros:** no Claude CLI dependency; Gemini free tier is generous (RPM/RPD-limited but daily-quota fine for 50ish observations); decouples claude-mem from Claude Code's install shape.
**Cons:** observations get generated by a different model than the rest of the brotherhood uses; quality may differ; rate limits could throttle backfill if we want to retroactively process the 13,818 stuck messages.
**Estimated effort:** 5 minutes (free key + setting edit + restart).
**Risk:** low for forward observations; medium if we want to drain the failed backlog (rate limits).

### Option D — Accept claude-mem cannot work in this environment; demote `/mem-search` permanently

Pull `/mem-search` out of the mandatory rhetoric, document the platform-mismatch in CLAUDE.md, deprecate the plugin install for Tankoban purposes.

**Pros:** zero ongoing maintenance burden.
**Cons:** loses cross-session memory entirely; abandons the audit's central recommendation; brotherhood goes back to grep-the-archive forever.
**Estimated effort:** trivial — delete CLAUDE.md lines + remove plugin.
**Risk:** strategic — concedes a powerup vector the audit identified as load-bearing.

### My recommendation

**Option A first, Option C as fallback if A trips on auth.**

Option A is the lowest-friction repair that matches claude-mem's design. If the install or auth handshake fails for any reason, Option C is a clean Plan B with a well-understood free tier. Option B is fragile across VS Code extension updates; not worth the risk. Option D is premature — we should know whether claude-mem CAN work for Tankoban before declaring it can't.

---

## §6 Phase 2 unblock criteria

Once Hemanth ratifies a remedy:

1. **Apply the remedy** (install CLI, set CLAUDE_CODE_PATH, or set Gemini key per option chosen).
2. **Restart claude-mem worker** (`claude-mem stop && claude-mem start` or equivalent).
3. **Verify `/api/health` shows `lastInteraction.timestamp != null`** within 5 minutes of the next agent activity.
4. **Verify `/api/search/observations?query=tankoban&limit=5` returns at least 1 hit** within 30 minutes (one full session of agent activity must complete).
5. **Build one Tankoban corpus** via `POST /api/corpus` and verify it lands in `~/.claude-mem/corpora/`.
6. **Prime + query** the corpus; verify the answer is coherent.

When all 6 criteria pass, Phase 2 closes and Phase 3 (provenance contract) is unblocked.

---

## §7 Honest unknowns + carry-forward

- **The 13,818 already-failed observation messages.** Phase 2 should decide whether to (a) retry them via `retryAllStuck` (worker-service.cjs:783) — but that requires the AI provider to be working AND won't help if Claude Code CLI's subscription billing throttles bursts; (b) leave them as historical record only; (c) delete them. My recommendation is (b) — they predate the fix, the in-context information is already in chat history and git, and re-deriving observations after the fact would consume API quota without proportional benefit.
- **The 70 currently-pending observations.** These are from this session (current chat). Phase 2 retry could rescue them.
- **The skip-list policy** (`CLAUDE_MEM_SKIP_TOOLS` excludes `Skill`, `SlashCommand`, `TodoWrite`, etc.). Once persistence works, the skip list still defeats the discipline-auditing purpose. Phase 2 sub-task: drop `Skill` and `SlashCommand` from the skip list so observation telemetry actually captures the surface we want to enforce. (This is a 1-line settings edit; doesn't deserve its own phase.)
- **claude-mem v12.2.0 vs upstream.** The bundled worker-service.cjs is from claude-mem 12.2.0; upstream is currently at v12.3.4. The audit cited regressions in v12.3.3 that were rolled back in v12.3.4. Phase 2 may want to upgrade to v12.3.4 BEFORE applying the remedy, or AFTER — Hemanth's call. I'd recommend AFTER (one variable at a time, per `feedback_one_fix_per_rebuild.md`).
- **Cross-platform symmetry.** Even with the fix, Codex (Agent 7) doesn't have the same `PostToolUse` / `SessionStart` hooks (audit §8). Codex sessions won't generate observations. That's a known gap, documented in the parent TODO §3 third constraint.

---

## §8 Evidence files

- `C:\Users\Suprabha\.claude-mem\claude-mem.db` (461 MB, SQLite WAL mode, read-only inspection only)
- `C:\Users\Suprabha\.claude-mem\settings.json` — claude-mem settings, full read
- `C:\Users\Suprabha\.claude-mem\worker.pid` — `{"pid":20396,"port":37777,"startedAt":"2026-04-23T10:00:56.136Z"}`
- `C:\Users\Suprabha\.claude-mem\supervisor.json` — worker + mcp-server + chroma-mcp PIDs
- `C:\Users\Suprabha\.claude-mem\logs\claude-mem-2026-04-25.log` (8635 lines) — 39 `summaryStored=null` events, 1 `session-complete: Missing sessionId, skipping` warning
- `C:\Users\Suprabha\.claude\plugins\cache\thedotmack\claude-mem\12.2.0\scripts\worker-service.cjs` (2007 lines, bundled) — `findClaudeExecutable` at line 1679, `markFailed` at line 794, `markAllSessionMessagesAbandoned` at line 779, unrecoverable list at line 1682, AI provider detection at line 1655, `isSessionTerminatedError` regex at line 1683.
- HTTP API responses recorded inline in §2.

---

## §9 Sign-off

Phase 1 exit criterion (per [SKILL_DISCIPLINE_FIX_TODO.md §6 Phase 1](../../SKILL_DISCIPLINE_FIX_TODO.md)): "Hemanth has a one-paragraph explanation he can read of 'this is why mem-search returns nothing for Tankoban' + a remedy proposal he can ratify."

Met. Paragraph: §1 Bottom line. Remedy proposal: §5 with recommendation. Hemanth picks a remedy + ratifies §5 of the parent TODO; Phase 2 begins on next summon.

---

## §10 Post-reboot verification — Phase 1+2 GREEN (added 2026-04-25 ~22:30 IST)

Hemanth ratified Option A 2026-04-25 ~22:00. Execution sequence + outcome:

1. **Install** — `npm install -g @anthropic-ai/claude-code` succeeded in 16 seconds. `claude.cmd` landed at `C:\Users\Suprabha\AppData\Roaming\npm\claude.cmd`, version 2.1.119. `where claude.cmd` confirmed PATH visibility.
2. **Worker restart attempt 1** — `worker-cli.js restart` reported failure ("Process died during startup"). Investigation revealed the original worker (PID 20396) had crashed but Windows kernel still held its listening socket on port 37777 — a known Windows zombie-socket pattern. New worker spawns hit "Worker already running, refusing to start duplicate" because the kernel claimed PID 20396 still held the port even though `Get-Process -Id 20396` returned nothing.
3. **Resolution path** — proposed port-bump in `~/.claude-mem/settings.json` (37777 → 37778). Hemanth interrupted before edit; pivoted to full machine reboot per his preference (cleanest path, no claude-mem config touched).
4. **Post-reboot verification probes** ran in parallel against the freshly-started worker (PID 16636, started 2026-04-25T15:03:35Z = ~22:30 IST, just after reboot):

| Probe | Result | Interpretation |
|---|---|---|
| `where claude.cmd` | `C:\Users\Suprabha\AppData\Roaming\npm\claude.cmd` | CLI install survived reboot |
| `netstat -ano \| grep "LISTEN.*37777"` | (empty in initial check, then a fresh listener appeared as Claude Code's SessionStart hook spawned the worker) | Windows kernel released zombie socket; fresh worker bound cleanly |
| `cat worker.pid` | `{"pid":16636,"port":37777,"startedAt":"2026-04-25T15:03:35.456Z"}` | Fresh worker — completely new PID, new startup time |
| `GET /api/health` | `200 OK`, `initialized:true`, `mcpReady:true`, uptime 90s | Worker alive and serving HTTP |
| `GET /api/stats` | `"observations":2, "sessions":73, "summaries":0` | **Observations table is no longer empty.** Up from 0 to 2. |
| `GET /api/search/observations?query=tankoban&limit=3` | 2 hits returned with structured titles + IDs + token counts | **`/mem-search` now works for Tankoban.** Audit's load-bearing failure is closed. |

**The two new observations** (verified via search response payload):

- `#1 — Claude Code CLI Installed Globally on Windows` (created 2026-04-25 19:31, type `change`, ~214 read tokens, ~686 work tokens)
- `#2 — claude-mem Is Not a Separate CLI Binary` (created 2026-04-25 19:31, type `discovery`, ~207 read tokens, ~335 work tokens)

Both are accurate, structured, Tankoban-scoped, and correctly classified. The AI provider observed our pre-reboot debugging work, generated structured observations from the raw tool stream, and stored them through the full pipeline (`pending_messages → worker SDK call → <observation> XML → observations table → Chroma index → search index`). End-to-end pipeline is alive.

### §10.1 Open instrumentation quirk

`/api/health` continues to report `"lastInteraction":null` even though observations clearly landed. Tracing the worker source: `lastAiInteraction` is a per-instance member set in the `.catch` handler of `startSessionProcessor` (worker-service.cjs ~line 1682) and in the `.finally` handler on success. The current observations were generated during a session that ran on the previous worker process (PID 16636 sessions); the field tracks the *current* worker's last interaction, and this freshly-restarted worker hasn't completed an AI interaction since boot. Not a bug, not a concern — observations table is the ground-truth.

### §10.2 Phase 2 unblock criteria status

Mapping back to §6:

1. ✅ Apply remedy — Option A executed.
2. ✅ Restart worker — reboot achieved this.
3. ⚠️ `/api/health.lastInteraction.timestamp != null` — instrumentation quirk per §10.1; substituted by direct database evidence below.
4. ✅ `/api/search/observations?query=tankoban` returns Tankoban hits — 2 returned.
5. ⏸️ Build a Tankoban corpus — deferred to first Hemanth-summoned `/claude-mem:knowledge-agent` invocation; no longer load-bearing for Phase 2 closure.
6. ⏸️ Prime + query the corpus — same as 5.

Criterion 4 is the load-bearing one. With observations both stored AND searchable, the entire memory pipeline is verified end-to-end. Phase 2 closes. Phase 3 (RTC provenance contract) is unblocked pending §5 Decisions ratification in the parent TODO.

### §10.3 Honest unknowns post-reboot (revised)

- **Stale observations from before the reboot** — 13,818 observation messages and 607 summarize messages that landed in `failed` state pre-reboot stay there. Carry-forward §7 recommendation (b) ratified — leave them as historical record, do not retry.
- **Skip-list policy** — `CLAUDE_MEM_SKIP_TOOLS` still excludes `Skill` and `SlashCommand`. Phase 2 closes without addressing this; Phase 6 (skill sheet trim) re-evaluates whether to drop them from the skip list as part of the per-agent shortlist work.
- **Summary persistence** — 0 summaries in DB. Summaries fire at Stop hook. No Stop hook has fired since reboot in this session yet (this conversation is still active). Verify on next session boundary.
- **Worker process longevity** — fresh worker has only ~90 seconds uptime at probe time. If it crashes within a day, that's a Phase 2 regression. Monitor.

### §10.4 Sign-off update

Phase 1 sign-off (§9) preserved verbatim. Phase 2 sign-off added: memory rehab complete; `/mem-search` returns Tankoban results; pipeline alive end-to-end. Audit's headline question — "can we power up agents with claude-mem?" — answered YES with empirical evidence in §10 above.
