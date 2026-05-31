# THE OFFICE — Owned-Worker Responder (the idle-agent cure) — design spec (v2)

**Date:** 2026-05-31 (v1 brainstormed; **v2 = this revision**)
**Author:** Agent 0 (brainstormed live with Hemanth)
**Status:** v2 — incorporates Agent 7 (Codex) feasibility review (`a23225d`, `agents/audits/owned_worker_responder_feasibility_2026-05-31.md`). Awaiting written-spec review → implementation plan (gated by the `claude -p` contract test, §7).
**Part of:** the owned-worker model in `2026-05-31-the-office-app-design.md` (spec v2). This is the **narrow first cure** — a reliable coordination responder, not a full autonomous worker.

> **What changed in v2 (Agent 7's review):** the core idea holds — a reply-only fallback responder is the right first cure — but four things were too optimistic and are now fixed: (1) the silence check was semantically wrong (coarse "any post" drops real unanswered messages → now **target-aware**); (2) the responder would fight the tab over the shared read-cursor → now **separate cursor state**; (3) `claude -p` was assumed to be a `codex exec` twin — his live on-machine smoke proved it isn't → now **contract-test-gated before any build**; (4) a backup posting *as* the brother could commit him to decisions he never made → now **non-binding typed replies**. Plus a pre-send race recheck, deterministic `@all` gating, and recorded backup-failure events.

---

## 1. The problem this cures

Every "agent X non-responsive" Hemanth hit this session (Agent 4 ×2, Agent 1 ×2) has one root: **a Claude tab that goes idle cannot be guaranteed to wake or respond.** The watch dies (Monitor timeout, context compaction, closed tab), or the brother is heads-down and never circles back to a coordination message. The coordinator can nudge but cannot *force* a tab awake. Worst when an owner delegates then goes idle, stranding a collaborator (the Agent 1 → Agent 2 handoff this session).

The proven cure exists for one engine: **Agent 7's headless bridge** (`codex_agent7_bridge.py`, `f69e3b8`) — an owned worker, a process we control that auto-wakes via `codex exec`. This brings that reliability to the **Claude brothers**.

**Scope (narrow — Hemanth chose "reliable responder first," YAGNI):** a guaranteed *coordination responder*, NOT an autonomous worker. It answers coordination/handoff/ack messages reliably; it does NOT do deep code work, commit, or mutate the repo. The interactive tab stays the place for hands-on work.

---

## 2. The model: Fallback safety-net, TARGET-AWARE (v2)

The headless worker is a **backup**, not a replacement. The real brother (in his tab) answers first; the worker steps in only if that *specific* message goes unanswered.

**Why Fallback is the most reliable option** (Hemanth's criterion): it's the only one that *guarantees a reply* AND keeps replies high-quality (the real brother, full context, answers first). It catches both failure modes seen this session — the dead watch *and* "saw it, didn't reply" (Agent 4).

**v2 fix — the trigger is per-message, not per-agent.** Each candidate trigger is tracked as a record: `{trigger_seq, from_sender, target, text_hash}`. After the `FALLBACK_WINDOW` (~60s), the worker suppresses the trigger ONLY IF one of these is true:
- Agent N posted a **chat** message (not activity/blocker-to-someone-else) after `trigger_seq` that **plausibly answers that sender/thread** (heuristic: addressed back to `from_sender`, or references the thread), OR
- the trigger was **explicitly cleared** by the tab (best version: lightweight bus metadata `reply_to_seq` / `handled_seq` — see §4/§9; until that exists, conservative heuristics + err toward a non-binding backup reply).

A bare "Agent N posted *something*" no longer counts — an unrelated RTC, broadcast, or reply-to-another-agent must NOT suppress a real unanswered direct request (Agent 7, HIGH risk).

**Pre-send recheck (v2 fix — the second-61 race):** immediately before `send`, re-read the bus tail and run the same target-aware check again. If the tab answered while `claude -p` was drafting, drop the reply. This closes the race the Codex bridge never had (it has no fallback window).

---

## 3. Architecture

A single generalized script — `scripts/office/office_responder.py` — parameterized by agent number, one instance per brother. Generalizes `codex_agent7_bridge.py`; treats the Claude driver as a **new** driver (not a flag-rename of `codex exec`).

**Per-brother loop:**
1. **Identity:** join a dedicated session `responder-agentN` (guard `whoami == agentN`, refuse on mismatch). Posting maps `from=agentN`.
2. **Separate cursor (v2 fix):** the responder keeps its OWN read state — `.bus_responder_cursors/agentN.seq` (or `mark-seen responder-agentN`), **never** `mark-seen agentN`. The tab's `deliver`/`drain` own the real `agentN` cursor; the responder must not advance it or it hides messages from the live tab (Agent 7, MED-HIGH risk).
3. **Watch + deterministic prefilter (v2 fix):** poll for messages addressed to Agent N. **Direct** messages are candidates. **`@all`** messages are candidates ONLY if they deterministically pass a gate *before* any model call — explicit "@agentN" mention, a trailing `?`, an ACK-request token, or a narrow coordination pattern — so one broadcast doesn't fan out into N paid `claude -p` calls + N backups (Agent 7, MED risk).
4. **Fallback window:** record the candidate trigger; wait `FALLBACK_WINDOW` (~60s).
5. **Target-aware silence check (§2):** suppress if the brother plausibly answered *that thread*; else proceed.
6. **Draft:** invoke `claude -p` (the contract-tested command, §7) with a minimal Python-assembled context packet (the unanswered message + thread + reply-class instructions). Minimal prompt — cost/latency is real (§7).
7. **Pre-send recheck (§2):** re-run the target-aware check on fresh bus tail; abort if answered.
8. **Post:** structured reply → `office_bus.py send responder-agentN @target "<non-binding, marked reply>"` (§4).
9. **Loop-safety + failure honesty (v2):** mark the trigger consumed in the responder's OWN cursor before drafting (no infinite retry). If `claude -p` times out / fails, **record a `backup-failed for seq X` activity event** — a "guaranteed responder" must not silently drop the safety-net reply (Agent 7, MED risk).

**Headless:** no window, no SendKeys, no desktop.

---

## 4. Honesty — non-binding, typed (v2)

A backup reply appears under `from=agentN`, so a bare honesty prefix is **not enough** — it can be quoted later as the brother's position (Agent 7, HIGH risk). v2 rules:

- **Marker:** every backup reply is prefixed `[auto — AgentN's tab idle]` (text-level honesty now; **bus-schema metadata** `kind:"auto_reply"` + `{backup_for, trigger_seq}` is the proper fix, planned as a fast-follow so the roster/UI can style/filter backups — §9).
- **Typed reply classes** — the backup must classify its reply and stay within authority:
  - `ack` — simple acknowledgement / "received."
  - `handoff` — routes/forwards, no decision.
  - `clarifying_question` — asks rather than answers.
  - `nonbinding_assessment` — "backup read: … — owner to confirm." NEVER stated as the brother's decision.
  - `decline_owner_confirmation_required` — for domain/implementation calls the backup must NOT make: "Agent N is away; I won't commit him to this — flagging for his return."
- **Hard rule:** the backup **never** commits Agent N to a domain decision, an implementation promise, or a position the real brother didn't take. On anything substantive it gives a non-binding read + defers to the owner.

---

## 5. Safety (mirrored from the Codex bridge + v2 additions)

- **Posts only as Agent N** (identity-guarded; fake-hemanth-class bug structurally impossible).
- **Loop-guarded:** ignores its own posts, filters wake/activity/marker lines, marks-seen-before-drafting (in the *responder* cursor).
- **Single-instance lock** per agent, with **stale-lock recovery** (Agent 7, LOW risk — the lock + bus append-lock suffice against same-identity double-process; tab-vs-responder is a *semantic* race handled by §2's pre-send recheck, not a file lock).
- **Reply-only:** no deep work, no commit, no repo mutation. `claude -p` runs read-only with a locked tool surface (§7).
- **Backup-failure is recorded, not silent** (§3.9).

---

## 6. What this is NOT (deferred)

Not a tab replacement; not an autonomous worker (Slice 2A); not the heartbeat/status feature (already shipped — that's *visibility*, this is the *response guarantee*).

---

## 7. The `claude -p` contract test — GATING (v2, Agent 7's key finding)

**We do NOT assume `claude -p` ≈ `codex exec`.** Agent 7's live on-machine smoke found: `--json-schema --output-format json` returned a free-form `result` string (not a clean `structured_output`); a project hook attempted an MCP memory search **despite `--tools ""`**; a `--bare --strict-mcp-config` run timed out at 120s. The flags exist (`--print/-p`, `--json-schema`, `--permission-mode`, `--tools`, `--disallowedTools`, `--strict-mcp-config`, `--no-session-persistence`, `--bare`) — but the **exact working contract is unproven.**

**Therefore the implementation plan's FIRST task is a contract-test spike** (no responder logic until it passes). Acceptance criteria:
- exits 0 in this repo with no interactive prompts;
- **no file writes**; no Bash/Edit/Write tools available;
- **no inherited MCP/hooks** fire unless deliberately allowed;
- returns a **parseable, validated** `{"replies":[...]}` object;
- schema-validation failure is detectable + logged;
- timeout path records a backup-failed event without retry storms.

**Recommended starting command** (subject to the smoke; Agent 7's shape):
```
claude -p --no-session-persistence --permission-mode default \
  --tools "" --disallowedTools "Edit,Write,Bash" --strict-mcp-config \
  --json-schema "<reply schema>" --output-format json "<minimal prompt>"
```
If repo reads aren't needed, prefer `--tools ""` and put everything in the Python context packet. If hooks/MCP still leak, use a temporary `--settings` profile or investigate `--bare` — **do not hand-wave it.**

**Fallback mechanism:** if the CLI contract can't be made clean/reliable, **switch to the Claude Agent SDK** (typed options, programmatic structured output with success/error subtypes, no CLI text-scraping). Agent 7's note: the SDK is the better long-term substrate once the Office host owns worker lifecycle beyond reply-only — but `claude -p` is acceptable for the first pilot *if and only if* the contract test passes.

---

## 8. Build order — dry-run first (v2, Agent 7's recommended shape)

Pilot ONE brother (Agent 4 or Agent 1 — the two that stranded a collaborator this session):
1. **Contract test** (§7) — gate. Must pass (or pivot to SDK) before anything else.
2. **Deterministic watcher + pending-trigger state** — finds candidates, records trigger records, posts NOTHING.
3. **Fallback timer + target-aware silence checks** — logs would/wouldn't-post decisions (still dry-run).
4. **`claude -p` draft in dry-run** — logs the reply object + the reason, no posting.
5. **Enable posting** — with the honesty marker, typed reply class, and the pre-send recheck.

Each stage is verifiable before the next. Posting is the *last* thing turned on.

---

## 9. Open items (carry into planning)

- **`FALLBACK_WINDOW`:** ~60s default; but cost/latency is real — a 60s window + a ~34-90s `claude -p` call = **90-150s** total response in the common case (Agent 7, MED). Not instant; tune + keep prompts minimal.
- **Target-aware "plausibly answered" heuristic:** exact rule (addressed-back-to-sender / thread-reference) — specify in the plan; the clean version needs the bus `reply_to_seq` metadata below.
- **Bus-schema metadata (fast-follow):** add `reply_to_seq`/`handled_seq` (for target-aware clearing) + `kind:"auto_reply"`/`backup_for` (for honesty + UI styling). If out of scope for the pilot, prefix-only now + schema metadata as the immediate next step.
- **Which brother first:** Agent 4 or Agent 1.
- **Agent SDK vs `claude -p`:** decided by the §7 contract test.

---

## 10. Decisions + why (v1 + v2)

**v1:** narrow reply-only responder; Fallback safety-net model (most reliable per Hemanth's criterion); `claude -p` as the Claude equivalent of `codex exec`; honesty marker.

**v2 (from Agent 7's review):**
- **Target-aware fallback** — coarse "any post" drops real unanswered messages; track per-message + plausibly-answered + pre-send recheck.
- **Separate responder cursor** — never `mark-seen agentN`; use a responder-namespaced cursor so the tab isn't starved.
- **`claude -p` is contract-test-gated** — his live smoke proved it's not a `codex exec` twin; prove the exact command (or pivot to Agent SDK) before building.
- **Non-binding typed replies** — posting as Agent N can commit him; reply classes + "owner to confirm" + never-commit rule.
- **Pre-send recheck, deterministic `@all` gate, recorded backup-failures, dry-run-first build order.**

---

## 11. References

- Agent 7 review: `agents/audits/owned_worker_responder_feasibility_2026-05-31.md` (`a23225d`)
- Template: `scripts/office/codex_agent7_bridge.py` (`f69e3b8`)
- Parent: `docs/superpowers/specs/2026-05-31-the-office-app-design.md` (owned-worker model)
- Claude Code docs (Agent 7-checked): cli-usage, permission-modes, agent-sdk/overview, agent-sdk/structured-outputs (code.claude.com/docs)
- Existing: `scripts/office/office_bus.py`, `office_watch.sh`, `office_status.py`
- Memories: `project_the_office_live_agent_bus`, `feedback_no_overclaim_in_rtc`, `feedback_no_reset_hard_on_shared_tree`
