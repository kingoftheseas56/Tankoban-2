# Office Reachability Hardening Backlog — 2026-06-02

**Source:** adversarial workflow `office-reachability-hardening-sweep` (wf_5fbe4a30-634) — 33 agents, 5 review lenses, per-finding adversarial verification. **24 confirmed findings** (false positives already removed). Full raw output: workflow task `wre2kz965`.

**Context:** hole-checks the two fixes shipped this session — `ade6272` (ack-or-fallback delivery + activity-liveness) and `db96175` (summoned brothers wake on Opus) — plus a full sweep of the wake/summon/ask system. "HOLE" = gap inside one of those two commits; "PRE-EXISTING" = older infra gap now load-bearing.

Status legend: `[ ]` todo · `[x]` done (commit) · `[~]` deferred to follow-up.

## P0 — reachability-breaking (silent black-holes / invisible death)

- [ ] **#2 loose ACK false-resolve via `activity` (HOLE/ade6272).** `resolve_pending` (office_dispatch.py:293-306) builds `max_posted` with no kind filter, so a brother's own commit-mirror `activity` line (office_bus.py:304) — which watch-peek SKIPS (459-460), so it never woke him — cancels his own fallback. Summon black-holed.
  *Fix:* count only `kind in (chat,ack,blocked)` when building `max_posted`; ideally also require `to==summoner` or arc==summon-seq. **Most dangerous regression — fix FIRST (~2 lines + test).**
- [ ] **#1 held fallback dropped (HOLE/ade6272).** Fallback loop (office_dispatch.py:432-439) discards `_spawn_for`'s `False` return (lock busy / cap / OSError → 336-372) and drops the entry from pending anyway. Re-black-holes the summon ade6272 targets.
  *Fix:* honor the return — keep held entries pending with bumped deadline (`now+INTERVAL`) + a `tries` counter; surface to Hemanth after N attempts (don't re-spam the cap note).
- [ ] **#3 dispatcher invisible-down (PRE-EXISTING).** Dispatcher writes no self-heartbeat; roster has no dispatcher card; open_office.bat:27 is fire-and-forget. Killed twice this session. When it dies, every closed-tab summon silently stops; brothers still show "live".
  *Fix:* write `agents/.office_heartbeats/dispatcher.beat` each loop + a red "DISPATCHER DOWN" card in office_status roster. Auto-restart = fast-follow.

## P1 — quality / duplicate-work / wrong-engine / noise

- [ ] **#4 held idle-spawn no pending memory (HOLE/ade6272).** `_dispatch` spawn path returns None regardless of `_spawn_for` result (404-406); a cap/lock-held immediate spawn leaves only a chat line. *Fix:* return a `deferred-spawn` pending entry that retries (not ACK-gated).
- [ ] **#5 wrong-engine impostor (PRE-EXISTING, known-deferred but LIVE).** Closed agent7 (Codex) / agent9 (DeepSeek) summon spawns `claude -p --model opus` — an Opus answering as Codex. `classify_summon` has no engine guard. *Fix:* `NON_CLAUDE={agent7,agent9}` → `skip_wrong_engine` + hard-refuse in spawn_brother.sh + test.
- [ ] **#6 heads-down live brother duplicate-spawn (HOLE/ade6272).** A genuinely-live brother heads-down >90s (e.g. 915s build) gets a duplicate Opus spawn on the shared tree (identity collision risk). *Fix:* re-check `_is_live` before firing fallback; EXTEND deadline if heartbeat still fresh; only spawn when also gone stale or extension-cap hit.
- [ ] **#7 no orphan-watch reaper (PRE-EXISTING).** Dead session's `while true` keeps beating for hours; roster truth + duplicate-spawn-of-zombie stay broken. *Fix:* watch self-terminates on owner-PID death (`kill -0`); dispatcher sweeps `.beat` older than `2*LIVE_WINDOW`; close hook `rm`s the beat.
- [ ] **#8 no watch singleton (PRE-EXISTING).** Re-clock-in stacks duplicate watchers, doubling wake spam + masking a dead one. *Fix:* `mkdir agents/.office_watch_locks/<me>.lock` at startup, reclaim if stale, refresh mtime.
- [ ] **#9 no dispatcher singleton (PRE-EXISTING).** Two dispatchers double-spawn every summon + split the cap. *Fix:* atomic `mkdir agents/.office_dispatch.lock` at main() top (doubles as #3 heartbeat).
- [ ] **#10 liveness window mismatch (PRE-EXISTING).** dispatcher LIVE_WINDOW=15 vs roster HEARTBEAT_WINDOW_SEC=25 → roster "live" while dispatcher spawns a clone. *Fix:* one shared constant.
- [ ] **#11 roster lacks activity fallback (HOLE-adjacent/ade6272).** Dispatcher gained `_recently_active`; roster (office_status.py) didn't, so it contradicts the routing. *Fix:* share one liveness predicate; render `live (active)` when bus-recent but no beat.
- [ ] **#12 no summon-fate trail (PRE-EXISTING).** State split across 3 hidden dotfiles + detached stdout. *Fix:* append `agents/.office_delivery.log` lifecycle lines (routed-live / no-reply / spawned / result).
- [ ] **#13 `?` anywhere → FYIs escalate (PRE-EXISTING).** office_asks.py:68 `if "?" in t`; a URL `?keyword=` escalated #368 ~49h. *Fix:* sentence-final `?` regex / strip URL tokens + test.
- [ ] **#14 descriptive phrases trip heuristic; "No rush" never read (PRE-EXISTING).** `needs your`/`please` trip it; #825/#465. *Fix:* no-rush/defer suppressor before the heuristic + tests.
- [ ] **#15 owed asks never expire (PRE-EXISTING).** Replay from genesis every tick, stay escalated (#368 ~49h, #465 ~30h). *Fix:* `OFFICE_ASK_OWED_MAX_SEC` → terminal `expired` state; one-time retraction for #368/#465.

## P2 — polish / quota / durability / edge

- [ ] **#16 spawn cap is rate not concurrency (HOLE/db96175).** Up to 5 simultaneous long Opus sessions; never decremented on done. *Fix:* `OFFICE_SPAWN_CONCURRENCY` (2-3) counting start-without-done.
- [ ] **#17 restart mid-fallback double-spawn (HOLE/ade6272).** Spawn precedes `_save_pending`; kill between → reload re-spawns. *Fix:* persist intent before spawn, or idempotency key = target+seq has a ledger start row.
- [ ] **#18 model string unvalidated (PRE-EXISTING).** Typo'd OFFICE_BROTHER_MODEL burns a cap slot. *Fix:* allowlist {opus,sonnet,haiku} + full-id escape.
- [ ] **#19 corrupt pending line wipes all (HOLE/ade6272).** `_load_pending` whole-loop try/except returns [] on first bad line. *Fix:* per-line try/except continue (mirror `_iter_bus`).
- [ ] **#20 torn/empty `.beat` split-brain (PRE-EXISTING).** dispatcher reads DOWN (content) vs roster LIVE (mtime); agent9.beat is NUL bytes now. *Fix:* atomic beat write; both readers agree on content-epoch.
- [ ] **#21 force-kill orphans spawn + wedges lock; restart double-processes (PRE-EXISTING).** *Fix:* persist cursor BEFORE `_dispatch`; trap SIGTERM in spawn_brother.sh; pair with #9; lower LOCK_STALE.
- [ ] **#22 no busy/lease exemption in escalation (PRE-EXISTING).** Lane-holder escalated like an idle brother. *Fix:* widen window (or suppress) while addressee holds a DevControl lease.

## Recommended sequence (workflow synthesis)
1. #2 (kind-filter ACK) — single most dangerous, ~2 lines.
2. #1 (honor held fallback).
3. #3 (dispatcher self-heartbeat + roster DOWN card).
4. #9 + #6 (dispatcher singleton + re-check live before fallback).
5. #5 (wrong-engine guard).
6. #13 + #14 + #15 (asks hygiene batch — one file).
7. reaper + watch singleton (#7/#8/#10) + P2 tail.

Steps 1-3 = bulletproofing core (no summon silently lost + dispatcher death visible). One change → one test → one verify (brotherhood one-fix-per-rebuild). `classify_summon`/`resolve_pending`/`_is_request` are already side-effect-free + test-shaped.
