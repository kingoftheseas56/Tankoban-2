# Phase 0 (Diagnostic) overnight findings — 2026-05-21

By Agent 0, overnight wake against Codex's audit at `agents/audits/claude_code_practices_2026-05-21.md`.

Scope: run Codex Phase 0 items 1-4 (profile hooks, capture skill listing state, telemetry de-dup, prompt-caching env experiment).

## Phase 0.1 — Hook latency profiling

### Methodology
Direct `time bash <script>` invocations against the live working tree at 2026-05-21 ~2:04am GMT+5:30. Each script run 3-10x in isolation. No prompt A/B (deferred to Hemanth's morning since it requires live prompt submissions).

### Results

| Hook script | Event | Configured timeout | Measured runtime | Verdict |
|---|---|---|---|---|
| `congress-check.sh` | UserPromptSubmit | 2s | 200-300ms (10 runs) | OK — well under timeout |
| `session-brief.sh` | SessionStart | 5s | ~3.4s (3 runs) | OK — under timeout, runs once per session |
| `pre-rtc-checker.sh` | Stop | **5s** | **13-15s (3 runs)** | **CRITICAL — exceeds timeout 3x, killed mid-loop every Stop** |
| `smoke-evidence-rename.sh` | Stop | 3s | ~650ms (3 runs) | OK |
| `skill-provenance-detect.sh` | Stop | 3s | ~900ms (3 runs) | OK |

### Verdict on Codex V1
Codex's V1 hypothesis confirmed: `congress-check.sh` is NOT the 30-45s prompt-submit lag source. Its isolation runtime is ~250ms, two orders of magnitude below the perceived lag.

Implication: the "VS Code thinking lag" that motivated removing Playwright MCP and commissioning this audit is NOT caused by any UserPromptSubmit hook. The remaining candidates (in descending likelihood):
1. **MCP server cold start** — `pywinauto-mcp` (uvx spawn), `codex` (codex.cmd spawn), plus plugins (`claude-mem`, `superpowers`). Codex V2 partially addressed this; Phase 3 Codex daemonization is conditional on this confirmation.
2. **Claude Code harness initialization** — settings parse, plugin enumeration, hook scan.
3. **VS Code extension overhead** — UI-thread work on extension activation.

Phase 0.1 cannot distinguish between these three from script-isolation profiling alone. Full A/B (hooks on/off, MCP on/off) requires Hemanth driving prompt submissions in the morning.

### New finding: pre-rtc-checker.sh systematic timeout
Independent of the prompt-lag question, **`pre-rtc-checker.sh` is being killed at its 5s timeout every Stop event when there are pending RTCs**. Cost dominated by:
- Two full passes over `$ADDED_RTCS` (nag pass at lines 49-135, scaffold/stale-detect pass at lines 144-232)
- Per-RTC `git diff HEAD -- agents/chat.md` (top of script) + per-RTC `git diff --shortstat HEAD -- $FILE_ARGS` (loops 1 + 2) + per-non-trivial-RTC `bash skill-provenance-detect.sh --candidates-only` sub-invocation
- With ~9 pending RTCs in current chat.md and 2-5 files per RTC, cumulative git invocation cost ≈ 13s

Behavior under timeout: script does most nag work in the first loop (lines 49-135), gets killed somewhere in the second loop. Telemetry partial-write OK because `>> $TELEMETRY_FILE 2>/dev/null` per nag inside loop 1 — those rows persist. Scaffold + stale-file warnings frequently lost.

This directly explains Codex V7 finding: "telemetry overcounts repeated tags for the same Agent 1 RTCs" — the hook fires on EVERY Stop event seeing the same uncommitted RTCs in `git diff HEAD`, regenerating identical telemetry rows turn after turn until the RTC is swept.

### Recommendations (for Hemanth's morning ratification)
1. **Cache `git diff HEAD -- agents/chat.md`** to a temp file once at top of script; reuse for both loops. Estimated savings: ~30%.
2. **Merge the two passes** into one — there's no semantic reason to walk ADDED_RTCS twice. Single pass that emits nag + scaffold + stale per RTC.
3. **De-dup telemetry by `(commit_sha_at_HEAD, tag)`** — if the working-tree RTC line is unchanged from a prior turn's telemetry row, skip the write. Closes Codex V7's overcounting.
4. **Bump timeout to 10s OR fix the root cost first.** Bumping timeout alone leaves the systematic overcounting. Fix #3 first, then re-measure.

## Phase 0.2 — Skill listing truncation state

### Methodology
`/doctor` is a Claude Code CLI command not directly invocable from an agent turn — used the session-start system-reminder as the ground truth for what skills actually render, plus a direct frontmatter audit of `.claude/commands/*.md` for the explicit-vs-fallback split.

### Project skill frontmatter audit (20 total)

| Has explicit `description:` frontmatter (6) | Falls back to first paragraph (14) |
|---|---|
| brief.md | audit-skeleton.md |
| build-verify.md | codex-trigger-d.md |
| commit-sweep.md | fix-todo-new.md |
| repo-health.md | handoff-brief.md |
| rotate-chat.md | hemanth-language.md |
| session-recap.md | hemanth-rewrite.md |
|  | mcp-lock.md |
|  | memory-trim.md |
|  | memory-write.md |
|  | rtc.md |
|  | smoke-package.md |
|  | smoke-report.md |
|  | summon-from-todo-phase.md |
|  | tdd-scaffold.md |

Codex V6's count of "6 frontmatter / 14 fallback" verified exactly.

### Total skill catalog size (from session-start system-reminder)

| Source | Count | Examples |
|---|---|---|
| Project commands | 20 | brief, build-verify, hemanth-language, ... |
| `superpowers:` plugin | 14 | brainstorming, executing-plans, writing-plans, ... |
| `example-skills:` plugin | 17 | brand-guidelines, frontend-design, mcp-builder, ... |
| `claude-mem:` plugin | 7 | do, mem-search, smart-explore, timeline-report, ... |
| Non-plugin top-level | 12 | update-config, verify, simplify, run, review, ... |
| **Total** | **~70** | |

### Truncation state
Inspecting the session-start system-reminder text directly: **all ~70 skills render with descriptions**. None appeared as bare names (the "name-only" / "collapsed-to-name" Codex V6 hypothesized signal). The skill listing currently fits within whatever budget Claude Code is applying on this machine.

### Verdict on Codex V6 gate
- 6/14 frontmatter split — **confirmed**.
- Visible name-only truncation — **NOT confirmed today**.

Codex specifically gated Phase 1.6 visibility controls on `/doctor` confirming truncation. Since I cannot observe truncation in the live system-reminder, **the gate is not currently met**. The frontmatter cleanup (adding explicit `description:` to the 14 fallback files) is still hygiene work worth doing, but `disable-model-invocation: true` / `name-only` is a behavior change that should wait for Hemanth's morning call.

**Decision: hold Phase 1.6 visibility controls for Hemanth's morning ratification.** Offer two options in the morning report: (a) frontmatter-only cleanup (no `disable-model-invocation`), (b) full Codex V6 visibility classification. (a) is the lower-risk move if truncation isn't pressing.



## Phase 0.3 — Telemetry de-dup + clean compliance report

### Deliverables shipped
1. **`.claude/scripts/telemetry-dedup.ps1`** — PowerShell de-dup tool. Reads `.claude/telemetry/skill-discipline.jsonl`, groups by `(tag, date)`, reports overcount factor + per-day distinct miss count + top-10 most-repeated tags. Read-only against source. Supports `-Since YYYY-MM-DD` window. ~70 LOC.
2. **`.claude/telemetry/compliance-report-2026-05-21.txt`** — saved snapshot of full-history + recent-window reports.

### Key numbers
| Metric | Raw | De-duped | Overcount factor |
|---|---|---|---|
| Full history (388 rows) | 388 | 29 unique (tag,day) | **13.38x** |
| Window since 2026-05-19 (66 rows) | 66 | 2 unique (tag,day) | **33x** |

### Verdict on Codex V7
Codex's V7 overcount hypothesis confirmed and quantified. The "47 misses since 2026-05-19 / 30 on 2026-05-21" raw figure in Codex's audit collapses to **2 distinct missed-skill nags** when de-duped by (tag, date). Both are Agent 1 Phase 8a Task 3 + Task 4 — the same line nagged ~33x each over 30+ Stop events while sitting unswept in chat.md.

The systematic compliance failure visible in raw telemetry was almost entirely **hook-overcount artifact**, not actual brotherhood-wide skill-discipline breakdown.

### Top overcounting hotspots (full history)
1. `Agent 3, VIDEO_HUD_TIME_LABELS_FIX` — 57x (2026-04-25)
2. `Agent 4 (Codex), Stream detail-view reopen/download-state...` — 55x (2026-05-18)
3. `Agent 5, THEME_SYSTEM_FIX Phase 2 — topbar theme picker...` — 52x (2026-04-25)
4. `Agent 5, THEME_SYSTEM_FIX P2 follow-up — reposition theme picker...` — 51x (2026-04-25)
5. `Agent 1, Phase 8a Task 3 — TrustedUploaders module...` — 35x (2026-05-20)

Pattern: highest counts cluster on uncommitted RTCs that sit in the working tree across many agent turns (each Stop event re-nags them). The fix Codex recommended (de-dup at write-time by `(commit_sha_at_HEAD, tag)`) would eliminate ~92% of the noise.

### Recommendation for morning ratification
**Update `pre-rtc-checker.sh` to de-dup at write-time** (Phase 2-tier work; design choice). Cheapest implementation: maintain a separate `.claude/telemetry/skill-discipline.seen` index file containing `(commit_sha_at_HEAD, tag_hash)` per row already written; skip new telemetry row if the index already has the tuple for the current HEAD. Index gets pruned naturally when HEAD advances. This is in scope for the Phase 2 `pre-rtc-checker.sh` rewrite that also collapses the two-pass loop. Not safe to ship overnight unsupervised — defer to Hemanth.



## Phase 0.4 — Prompt-caching env experiment

### Deliverables shipped
- **`.claude/settings.local.json`** created (per-machine, not committed) with `ENABLE_PROMPT_CACHING_1H=1` in the `env` block. Mirrors the schema of the shared `settings.json`.

### Why local-only ship
Codex V10 explicitly rejected promoting this env var to shared `settings.json` as a validated fix — official Claude Code settings docs list supported env vars (`MCP_TIMEOUT`, `MCP_TOOL_TIMEOUT`, `MAX_MCP_OUTPUT_TOKENS`) and **do not list `ENABLE_PROMPT_CACHING_1H`**. The official 1-hour cache mechanism per Anthropic API docs is `cache_control: {type: "ephemeral", ttl: "1h"}`, which is a per-request field, not a global env toggle. So the env var may or may not affect Claude Code's behavior.

### Morning A/B procedure (for Hemanth)
1. Note the current `/cost` output (baseline — I cannot fire this from an agent turn; you must run it tomorrow morning before restarting Claude Code).
2. **Close and reopen the Claude Code session** — env vars only take effect on session start.
3. Idle for at least 5 minutes after the first prompt of the new session to allow potential 1h cache creation.
4. Submit a second prompt after the 5-minute idle window.
5. Note the new `/cost` output. Look specifically at: cache_creation_input_tokens, cache_read_input_tokens, and any "1h" or "ephemeral_1h" field if present.
6. **Promote to shared `settings.json` only if** the cache fields prove 1h-tier creation/read tokens changed. Otherwise delete `.claude/settings.local.json` — no harm done.

### Rollback
`rm .claude/settings.local.json` cleanly reverts. The file is per-machine; not in git history.

### Honest caveat
This experiment may produce inconclusive data overnight because:
- Session env vars are SessionStart-bound; the current session won't see them
- Comparing `/cost` requires Hemanth to fire it (agent turn cannot)
- Cache hit rate depends on prompt patterns; isolated overnight activity may not exercise 1h windows

The conservative read: data inconclusive → keep local-only, do not promote.


