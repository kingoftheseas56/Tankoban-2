# Overnight Morning Report — 2026-05-21

By Agent 0. Wake start: 2026-05-21 ~1:55am GMT+5:30. Report written: ~2:25am.

Hemanth went to sleep after greenlighting the Phase 0 + Phase 1 dispatch queue. This report summarizes what shipped, what surfaced, what's gated on his morning call, and the Phase 2 queue waiting for daytime ratification.

---

## TL;DR

- **All 4 Phase 0 diagnostic items completed.** Two big findings: (1) UserPromptSubmit hooks are NOT the lag source — `congress-check.sh` runs in 200-300ms (Codex V1 confirmed); (2) `pre-rtc-checker.sh` exceeds its 5s timeout 3x systematically, AND telemetry overcounting is **13.38x full-history / 33x recent** (Codex V7 confirmed and quantified).
- **3 of 4 Phase 1 salt items completed.** Phase 1.5 (tankoctl-primary wording) shipped to CLAUDE.md + STATUS.md. Phase 1.7 (`.claude/rules/` migration plan) authored as design doc. Phase 1.6 (skill visibility frontmatter) **HELD** for your call — Codex's gate (`/doctor` confirms truncation) not currently met.
- **No other-agent files touched.** A1/A2 WIP (ComicsSeriesView family, BookCatalogueAggregator, TrustedUploaders) stayed untouched per past-me's lockout.
- **Phase 2 architectural items deferred to your morning** per the preview-gated contract. Queue drafted below.

## Files shipped this wake (Agent 0 only)

| File | Type | Purpose |
|---|---|---|
| `CLAUDE.md` | edit | Tool-priority block reframed: tankoctl primary, pywinauto fallback. Playwright bullet removed (server gone since 2026-05-20). "Three MCP servers" → "Two MCP servers". |
| `agents/STATUS.md` | edit | "Universal tool priority" line added beneath the existing Universal Tier-1 line in the per-agent skill-trigger shortlists section. Header touch entry added. |
| `agents/audits/overnight_phase0_findings_2026-05-21.md` | new | Full Phase 0 findings — hook latency table, frontmatter audit, de-dup compliance report, prompt-caching experiment design. |
| `.claude/scripts/telemetry-dedup.ps1` | new | PowerShell de-dup tool for `skill-discipline.jsonl`. Reports by `(tag, date)`. Closes Codex V7 quantification. ~70 LOC. |
| `.claude/telemetry/compliance-report-2026-05-21.txt` | new | Saved snapshot of full-history + recent-window dedup output. |
| `.claude/settings.local.json` | new | Per-machine override with `ENABLE_PROMPT_CACHING_1H=1` for Codex V10 experiment. Not in shared settings per Codex V10's explicit rejection. |
| `docs/superpowers/plans/2026-05-21-claude-rules-migration.md` | new | Phase 1.7 deliverable — path-scoped rules migration matrix + smoke procedure + Hemanth ratification sequence. No rule files created yet. |
| `agents/audits/overnight_morning_report_2026-05-21.md` | new | This document. |

## Phase 0 findings summary

**Full details:** `agents/audits/overnight_phase0_findings_2026-05-21.md`.

### Phase 0.1 — Hook latency
| Hook | Event | Timeout | Measured | Verdict |
|---|---|---|---|---|
| congress-check.sh | UserPromptSubmit | 2s | 200-300ms | OK |
| session-brief.sh | SessionStart | 5s | ~3.4s | OK |
| **pre-rtc-checker.sh** | Stop | **5s** | **13-15s** | **CRITICAL — killed mid-loop every Stop** |
| smoke-evidence-rename.sh | Stop | 3s | ~650ms | OK |
| skill-provenance-detect.sh | Stop | 3s | ~900ms | OK |

**Codex V1 confirmed:** UserPromptSubmit hooks are NOT the 30-45s lag source. Remaining candidates: MCP server cold-start, Claude Code harness init, VS Code extension overhead. Cannot distinguish from script-isolation alone — needs your morning prompt-submit A/B with hooks on/off.

### Phase 0.2 — Skill listing
- 6 of 20 project commands have explicit frontmatter descriptions (Codex V6 count verified exactly).
- Total skill catalog ~70 (project + plugins + non-plugin top-level).
- **No name-only truncation visible** in this session's session-start system-reminder — all skills render with descriptions.
- **Codex V6's gate for Phase 1.6 visibility controls NOT met today.**

### Phase 0.3 — Telemetry compliance (de-duped)
| Metric | Raw | De-duped | Overcount factor |
|---|---|---|---|
| Full history (388 rows) | 388 | 29 unique (tag,day) | **13.38x** |
| Since 2026-05-19 (66 rows) | 66 | 2 unique (tag,day) | **33x** |

**Codex V7 confirmed and quantified:** the "47 misses since 2026-05-19 / 30 on 2026-05-21" headline collapses to **2 distinct nags** when de-duped (both Agent 1 Phase 8a Task 3 + Task 4, re-nagged ~33x each over many Stop events). The systematic compliance failure visible in raw telemetry is almost entirely **hook-overcount artifact**, not actual brotherhood-wide skill-discipline breakdown.

### Phase 0.4 — Prompt-caching experiment
- `.claude/settings.local.json` created with `ENABLE_PROMPT_CACHING_1H=1`. Per-machine, not shared.
- Codex V10 explicitly rejected promoting this env var to shared `settings.json` as a validated fix (not listed in official Claude Code settings docs).
- **A/B procedure waits for you in the morning:** close + reopen Claude Code, capture `/cost` before/after a 5-minute idle window, look at cache_creation/read input_tokens fields.

## Open questions for your morning call

### Q1. Phase 1.6 — skill visibility controls
**Codex's gate (`/doctor` confirms truncation) is not currently met.** I have two options drafted:

| Option | Scope | Risk | When to pick |
|---|---|---|---|
| **A. Frontmatter-only cleanup** | Add explicit `description:` to the 14 fallback files. No `disable-model-invocation: true`. | LOW — pure hygiene, no behavior change. | Default if `/doctor` shows no truncation pressure. |
| **B. Full Codex V6 classification** | Add `disable-model-invocation: true` to 11 user-only commands + `name-only` to 3 helpers, per Codex V6. | MEDIUM — those skills become non-discoverable in Skill tool context, may surprise an agent expecting auto-suggest. | If `/doctor` shows truncation OR you want to ship the visibility hygiene anyway. |

Recommend **A** unless you fire `/doctor` and see truncation evidence.

### Q2. Phase 2 architectural — design calls
Each of these needs your nod on API shape before any code edits. None should run unsupervised:

1. **DevControl lease registry** (Codex V3 + Phase 2.1). Adds `lease_acquire/release/heartbeat/get/list` commands to `DevControlServer.{h,cpp}`. Collapses Rules 19 + 22 from chat.md truth to machine truth. Estimated 1-2 days.
2. **Update Rule 19/22 implementation text** to reference lease truth + `/mcp-lock` replacement command (Codex Phase 2.2). 0.5 day after lease lands.
3. **Per-lane build dirs** (Codex V4 + Phase 2.3). `build_check.bat` needs `TANKOBAN_BUILD_LANE` env var + configure-on-missing path using same preset as `build_and_run.bat`. Estimated 1-1.5 days. Codex notes this is NOT a one-line patch.
4. **CLAUDE.md shrink 231 → 80-100 lines** (Codex V5 + Phase 2.4). Prerequisite enabler is the `.claude/rules/` migration plan (already authored this wake — `docs/superpowers/plans/2026-05-21-claude-rules-migration.md`). Estimated 1 day for migration + 0.5 day smoke.
5. **pre-rtc-checker.sh rewrite** — collapse 2 passes to 1, cache `git diff HEAD -- agents/chat.md` once, de-dup telemetry at write-time by `(commit_sha_at_HEAD, tag_hash)`. Closes Phase 0.1 + Phase 0.3 follow-ups. Estimated 0.5 day.

### Q3. UserPromptSubmit Congress hook (Codex Phase 1.4)
Codex recommended removing/demoting this if Phase 0 showed latency. **Phase 0 showed 200-300ms — well under timeout, not the lag source.** No removal warranted. Recommend keeping as-is unless you have a different angle.

### Q4. ENABLE_PROMPT_CACHING_1H promote decision
After your morning `/cost` A/B (per Phase 0.4 procedure):
- If cache_creation/read input_tokens prove 1h-tier changed → promote to shared `settings.json`.
- If inconclusive → leave local-only or delete `.claude/settings.local.json`.

### Q5. /commit-sweep on 9 pending RTCs
Past-me deferred this to your morning. Most are A1 Comics polish + A2 BOOKS_STREMIO_PIVOT + A4 torrent-persistence — not mine, so I want your eyeball on the batch before sweep. Recommend firing `/commit-sweep` after you've cleared anything in this report you need clarified.

## Phase 2 dispatch queue draft

Order assumes you've ratified all five items in Q2. Reorder freely.

| Item | Owner | Effort | Depends on |
|---|---|---|---|
| 1. pre-rtc-checker.sh rewrite | Agent 0 (script work) | 0.5 day | Nothing — independent |
| 2. .claude/rules/ migration (MVP rule file) | Agent 0 | 0.5 day | Migration matrix sign-off |
| 3. CLAUDE.md shrink (post-rules) | Agent 0 | 1 day | #2 GREEN |
| 4. DevControl lease registry | Agent 0 + (Codex Trigger D for src/devtools/) | 1-2 days | API shape ratification |
| 5. Rule 19/22 text update | Agent 0 | 0.5 day | #4 lands |
| 6. Per-lane build dirs | Agent 0 + (Codex Trigger D for build_check.bat) | 1-1.5 days | Independent of #4-#5 |
| 7. /doctor inspection + Phase 1.6 final decision | You + Agent 0 | 0.5 day | After #1-#6 settle |

## Honest unknowns

1. **Did my CLAUDE.md edits keep the file readable?** I touched lines 28-37 (tool-priority block) without restructuring. Easy to verify with a fresh session start; if anything looks off, `git diff CLAUDE.md` shows exactly what I changed.
2. **Is `.claude/rules/` actually available in your Claude Code version?** The migration plan assumes yes per Anthropic docs. Need a `/doctor` confirmation or version check before running the MVP smoke. Plan doc flags this as a precondition.
3. **The ENABLE_PROMPT_CACHING_1H experiment may produce inconclusive data overnight** — env vars only apply on session start, and you're asleep. Real A/B is morning.

## Commit state

I plan to land Phase 0 + Phase 1.5 + Phase 1.7 in two surgical commits (one for diagnostics + telemetry tool, one for the wording + plan deliverables) BEFORE you wake. Other agents' WIP stays untouched — `git add` uses explicit paths only, never `.` or `-A`. Your morning state will be clean for `/commit-sweep` against the pending 9 RTC lines.

If you want me to NOT pre-commit and let you sweep everything in one batch in the morning, tell me when you wake. I'll have left commit messages on the changes themselves either way.

## What I promised, what I delivered

| Promise (preview text) | Delivered? |
|---|---|
| Read Codex's audit | YES |
| Draft precise overnight dispatch queue | YES (in the preview itself, then executed) |
| Preview before firing | YES |
| Run Phase 0 (diagnostic) + Phase 1 (salt) only after your nod | YES — 4/4 Phase 0, 2/3 Phase 1 (1.6 held by Codex's gate not me) |
| STOP at Phase 2 | YES — drafted dispatch queue only, no code edits |
| Wake with morning report + Phase 2+ proposals | YES — this document |
| Honest about unknowns + bailouts | YES — Q1, Q2, Q3, Q4, Phase 0.1 needs your A/B, Phase 0.4 inconclusive overnight |

Sleep well, brother. See you in the morning.

— Agent 0
