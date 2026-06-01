# Audit - The Office Defect Map - 2026-06-01

By Agent 7 (Codex). For Agent 0 / Hemanth / The Office.
Reference comparison: none. Per brief, this is a cold isolation audit of local artifacts only.
Scope: Reliability core first, then transport/status/web/process/cost/git defects visible from the current Office implementation and live local artifacts. No internet research. No code changes beyond this audit artifact.

## Bottom line

The Office cannot reach "absolute certainty every agent responds" while the responder of record is a federated, human-opened Claude tab plus a shell watch. That architecture can improve visibility and sometimes wake tabs, but it cannot guarantee a reply because the Office does not own the tab lifecycle, the model turn, the context window, or the user's open window. The current code reflects this: `office_watch.sh` is a shell loop that emits lines; it is not a durable worker, cannot force a model response, and disappears when the Monitor task/tab/session disappears (`scripts/office/office_watch.sh:35-45`).

Owned workers are required for the guarantee. But the current owned-worker attempt does not yet meet the guarantee either. The `claude -p` responder can post useful marked replies, and the `arc=auto_reply` cascade guard now holds, but live logs show the driver timed out after 120 seconds for Agent 2 and Agent 1 backup attempts (`scripts/office/responder_agent2.log:6`, `scripts/office/responder_agent1.log:11`), with bus-visible backup failures at seq 462 and 466 (`agents/bus.jsonl:462`, `agents/bus.jsonl:466`). So the present system is "best-effort response plus visible failure," not certainty.

Top 3 things to fix first:

1. Replace federated-tab reliability expectations with an owned-worker reliability contract: per-agent worker process, per-message state, durable ack/fail outcome, bounded latency, and explicit capabilities.
2. Replace `claude -p` as the critical-path responder engine unless it can be made fast, cheap, and contract-stable; otherwise use an SDK/API-style worker with structured output, cancellation/timeout control, tiny context packets, and no full project load per fire.
3. Promote the bus from transport-only JSONL scans into a Room State Store that tracks message lifecycle, responder attempts, heartbeat history, build/lease state, dedup keys, and failure classifications.

## Observations

### Reliability core

**High - Federated tabs cannot guarantee response.**  
Observed: prompt-time delivery injects messages only when a Claude Code prompt is submitted for a registered session; it auto-binds identity from prompt text and then injects unseen bus messages as additional context (`scripts/office/office_bus.py:256-331`). The watch path is separate: it writes heartbeat files, polls `watch-peek`, and emits a line when messages arrive (`scripts/office/office_watch.sh:25-45`). Neither path owns the actual Claude tab or can force the model to answer. A tab can close, compact, time out, miss Monitor output, or read a drained message and choose not to reply. This is an architectural boundary, not a bug in the shell loop.

Why high: it directly blocks the non-negotiable goal. You can know a tab might be reachable; you cannot compel response from that tab.

**High - "Seen" is not "answered."**  
Observed: `cmd_deliver` advances the real agent cursor to the max delivered seq before any reply exists (`scripts/office/office_bus.py:328-331`). `cmd_drain` also prints pending messages and advances the cursor (`scripts/office/office_bus.py:344-369`). The bus has no `reply_to_seq`, `handled_seq`, SLA status, or required-response record in the append schema (`scripts/office/office_bus.py:96-112`). Therefore a message can become invisible to future drain/deliver calls without a corresponding answer.

Why high: this is exactly the silent-drop wound. The transport can mark delivery; it cannot prove acknowledgement or response.

**High - Current owned-worker backup is not reliable enough yet.**  
Observed: the responder uses `claude -p --no-session-persistence --permission-mode plan --strict-mcp-config --output-format json` with a 120-second subprocess timeout (`scripts/office/office_responder.py:258-264`). On failure, it logs and posts `[responder] backup-failed...` as activity (`scripts/office/office_responder.py:273-280`). Live evidence shows Agent 2 seq 461 and Agent 1 seq 465 both timed out after 120 seconds (`scripts/office/responder_agent2.log:5-6`, `scripts/office/responder_agent1.log:10-11`), producing bus failures at seq 462 and 466 (`agents/bus.jsonl:462`, `agents/bus.jsonl:466`).

Why high: the safety net is allowed to fail after already waiting the fallback window plus up to 120 seconds. That is visible and honest, but not a response guarantee.

**High - The responder consumes before successful draft.**  
Observed: due triggers are consumed by setting the responder cursor before `run_claude` is called (`scripts/office/office_responder.py:273-276`). If the driver times out, the code records a backup failure and returns (`scripts/office/office_responder.py:277-281`). This prevents retry storms, but it also means a transient `claude -p` failure ends that attempt.

Why high: the current design chooses loop safety over eventual delivery. That is reasonable for a prototype, but incompatible with "absolute certainty" unless a durable retry/escalation state exists.

**Medium - Successful backup replies prove the model shape, not the guarantee.**  
Observed: Agent 1's responder posted two marked replies after candidates seq 441 and 445 (`scripts/office/responder_agent1.log:4-9`), and the bus shows those replies with `arc:"auto_reply"` at seq 452 and 453 (`agents/bus.jsonl:452`, `agents/bus.jsonl:453`). This proves the responder loop can work. It does not prove bounded reliability because the same live net later timed out.

Why medium: useful but uneven. The system can save some missed handoffs; it cannot promise all of them.

**Medium - Generic @all messages are intentionally not guaranteed to receive all-agent responses.**  
Observed: `is_candidate` treats direct/comma-list messages as candidates, but for `to == "all"` it only candidates the message if the text explicitly mentions `@agentN` or `agent N` (`scripts/office/office_responder.py:56-79`). Live bus had Hemanth's `@all` "good morning" and "everybody make your presence known" messages at seq 442 and 444 (`agents/bus.jsonl:442`, `agents/bus.jsonl:444`), but the responder gate is designed not to fan those out into every backup.

Why medium: the anti-fanout rule is correct for cost/noise, but it means "everybody respond to @all" is not a supported guarantee today.

**Medium - The wake heartbeat visibility is diagnosis, not cure.**  
Observed: status computes `wake_state` from heartbeat file mtimes with a 25-second freshness window (`scripts/office/office_status.py:36-38`, `scripts/office/office_status.py:172-177`) and separately computes `responder_alive` from responder heartbeat files (`scripts/office/office_status.py:176-177`, `scripts/office/office_status.py:260-277`). The web UI renders `live`, `deaf`, `?`, and `backup` chips from those fields (`scripts/office/office_web.py:372-384`). This makes reachability visible, but it does not wake or answer by itself.

Why medium: visibility prevents false confidence; it does not close the reliability gap.

**Low - The auto-reply cascade guard holds in current code.**  
Observed: `is_candidate` immediately rejects records whose `arc` is `auto_reply` (`scripts/office/office_responder.py:61-66`). `post_reply` writes backup replies as normal chat with `arc="auto_reply"` (`scripts/office/office_responder.py:311-315`). The tests cover both candidate rejection and posted auto-reply rejection (`scripts/office/tests/test_responder.py:60-63`, `scripts/office/tests/test_responder.py:125-127`). Live bus seq 452 and 453 are `arc:"auto_reply"` and there is no subsequent backup ping-pong in the tail (`agents/bus.jsonl:452`, `agents/bus.jsonl:453`).

Why low: this particular cascade defect appears patched and tested.

### Transport and state

**High - The bus is transport, not truth, but current behavior still leans on it as truth.**  
Observed: the app design explicitly says the JSONL bus is transport, not truth, and calls for a Room State Store (`docs/superpowers/specs/2026-05-31-the-office-app-design.md:48-50`). Current code still derives most room behavior by scanning the bus, cursor files, git log, and heartbeat file mtimes (`scripts/office/office_status.py:280-287`). The append schema has no durable per-message lifecycle or responder attempt object (`scripts/office/office_bus.py:96-112`).

Why high: without state beyond message lines, the Office cannot answer "who was asked, who saw it, who answered, who failed, what retries remain" as machine truth.

**Medium - Full-file scans happen on the hot path.**  
Observed: `/messages` calls `_read_all_messages()` twice per request: once for messages after `after`, and again for max seq (`scripts/office/office_web.py:447-460`). `_read_all_messages()` reads the whole bus file each time (`scripts/office/office_web.py:414-427`). The browser polls `/messages` every 1.5 seconds and `/roster` every 4 seconds (`scripts/office/office_web.py:324-331`, `scripts/office/office_web.py:398-409`).

Why medium: fine for a tiny room; brittle once worker heartbeats, responder failures, foreman events, and high-volume status events share the same store.

**Medium - Cursor model is delivery-only, not acknowledgement-aware.**  
Observed: the real tab cursor is per agent (`scripts/office/office_bus.py:159-166`), while the responder has its own separate cursor namespace (`scripts/office/office_responder.py:112-125`). The separate responder cursor is the right fix for not starving the tab, and it is tested (`scripts/office/tests/test_responder.py:81-91`). But neither cursor records whether the message got a human/tab answer, backup answer, no-answer, timeout, or escalation.

Why medium: the cursor collision defect is fixed; the lifecycle-truth defect remains.

**Medium - Ordering and dedup are seq-only.**  
Observed: bus append locks and computes the next seq by scanning the existing file (`scripts/office/office_bus.py:80-112`). Message identity is essentially seq plus text. There is no immutable message id, reply id, `reply_to_seq`, idempotency key, worker attempt id, or dedup record in the schema.

Why medium: seq is enough for a chat log. It is weak for retries, exactly-once backup posting, and post-failure recovery.

**Low - Append locking is adequate for the current local room.**  
Observed: `cmd_append` uses an advisory mkdir lock around seq allocation and append (`scripts/office/office_bus.py:96-115`). For one local machine and a small number of agents, this is simple and probably sufficient.

Why low: not the current failure source. It becomes a limit only when state semantics expand.

### Web surface and status honesty

**Medium - Roster "backup" wording can still overpromise.**  
Observed: `compute_roster` says `responder_alive` means a dropped message will still get a marked, non-binding reply (`scripts/office/office_status.py:160-162`). The web chip repeats that tooltip (`scripts/office/office_web.py:372-374`). Live evidence contradicts the absolute wording: responder alive still produced backup failures at seq 462 and 466 (`agents/bus.jsonl:462`, `agents/bus.jsonl:466`).

Why medium: the UI has improved reachability honesty, but this tooltip now overstates the safety net.

**Medium - Status is still inferred from sparse signals.**  
Observed: presence is true if a bus message or tagged commit is within 30 minutes (`scripts/office/office_status.py:36`, `scripts/office/office_status.py:167-178`). Current spec already calls for dirty-tree, branch, build/test freshness, MCP lease, and live process state beyond bus+commits (`docs/superpowers/specs/2026-05-31-the-office-app-design.md:41`, `docs/superpowers/specs/2026-05-31-the-office-app-design.md:130`). Current `roster_now()` still merges git log, bus records, watch heartbeat, and responder heartbeat only (`scripts/office/office_status.py:280-287`).

Why medium: a brother can be actively working, stuck, building, or conflicted without the roster knowing.

**Medium - The web page is a single inline app with polling and ad hoc UI state.**  
Observed: `office_web.py` embeds the full HTML/CSS/JS in the Python `PAGE` string (`scripts/office/office_web.py:37`) and serves it through `ThreadingHTTPServer` (`scripts/office/office_web.py:520`). The spec already calls for extracting static assets, typed endpoints, and a mission-control shell before autonomy (`docs/superpowers/specs/2026-05-31-the-office-app-design.md:58`, `docs/superpowers/specs/2026-05-31-the-office-app-design.md:130`).

Why medium: okay for Slice 1, but will slow down correctness work once Operations/Infrastructure surfaces need stateful interactions.

**Low - The close button archives and clears without per-agent handoff state.**  
Observed: `/close` calls `office_bus.cmd_close()` from the web handler (`scripts/office/office_web.py:504-505`), and `cmd_close` archives the live bus and removes cursor files (`scripts/office/office_bus.py:396-414`). That is probably fine for a daily chat room, but it is not a state-store close with unresolved request handling.

Why low now: manageable while Office is chat-first. Risk rises if unanswered obligations live only in bus lines.

### Owned-worker process layer

**High - The current supervisor only starts/kills Python children; it does not supervise quality of response.**  
Observed: `office_responders.py` starts one `office_responder.py` subprocess per agent, writes PIDs, posts "armed", and loops to notice child exit (`scripts/office/office_responders.py:89-106`). The child process itself handles a timeout by logging and posting a failure event. There is no retry policy, degraded-engine fallback, queue depth cap, circuit breaker, cost budget, or escalation lane.

Why high: process liveness is not response reliability. Under concurrent failures, you get visible failure lines, not guaranteed answers.

**High - The critical-path engine loads too much context and lacks a stable structured contract.**  
Observed: the contract check comments record that prior smoke found `--json-schema` returned free-form result and hooks/MCP still appeared despite tool controls; the current accepted command parses JSON out of the result wrapper instead of relying on schema-enforced output (`scripts/office/office_responder_contract_check.py:1-7`, `scripts/office/office_responder_contract_check.py:90-101`). `run_claude` uses the same result-string parse path (`scripts/office/office_responder.py:258-270`).

Why high: string-extracted JSON from a full Claude Code CLI run is not the foundation for an "absolute certainty" responder.

**Medium - The Codex bridge is cleaner than the Claude responder but not enough for all agents.**  
Observed: Agent 7's bridge runs `codex exec` with read-only sandbox, explicit output schema, output file, and approval policy never (`scripts/office/codex_agent7_bridge.py:212-240`). It still marks pending messages seen before drafting (`scripts/office/codex_agent7_bridge.py:298-318`) and is for Agent 7 only. It proves the owned-worker shape is feasible when the engine has a clean noninteractive contract; it does not solve Claude agents.

Why medium: it is the right pattern family, but not a universal worker substrate.

**Medium - Cost scales with direct pings and failures, not idle time.**  
Observed: `office_cost.py` correctly distinguishes idle watch cost as zero model tokens and wake turns as the cost unit (`scripts/office/office_cost.py:12-19`). The responder path is worse than a shell watch because each due trigger can spawn `claude -p`; the prompt includes recent context and the CLI may load project settings/hooks (`scripts/office/office_responder.py:205-224`, `scripts/office/office_responder_contract_check.py:1-7`). Live timeout cases spent 120 seconds without useful reply (`scripts/office/responder_agent2.log:6`, `scripts/office/responder_agent1.log:11`).

Why medium: cost is controllable only if the worker context is tiny, cache-aware, and bounded by budgets.

### Coordination, off-channel work, and git/build collisions

**High - Flat-on-master creates real collision risk for concurrent Office use.**  
Observed: live bus records show a destructive git reset on the shared tree clobbered another agent's uncommitted tracked edits, requiring context-based recovery and immediate commit (`agents/bus.jsonl:394`, `agents/bus.jsonl:396`, `agents/bus.jsonl:397`). Later, two agents collided on `out/` builds when lease registry was unavailable (`agents/bus.jsonl:461`, `agents/bus.jsonl:465`). The Office surfaced these, but did not prevent them.

Why high: the Office increases coordination bandwidth and concurrent work. Without a state/lease/dirty-tree gate, it can also increase shared-tree damage.

**Medium - Off-channel replies are only partly mirrored.**  
Observed: `mirror-commit` can post commits into the room by parsing `[Agent N` subjects (`scripts/office/office_bus.py:233-244`), and bus activity lines show commits. There is no equivalent mirror for ordinary VS Code prose, local decisions, uncommitted file edits, failed commands, or current build state except what agents voluntarily post.

Why medium: commit mirroring helps; it does not eliminate off-channel truth.

**Medium - Build/desktop lane claims are social, not enforced by Office state.**  
Observed: recent collision messages show agents claiming/releasing build lane by chat text when the lease registry was down (`agents/bus.jsonl:456`, `agents/bus.jsonl:461`, `agents/bus.jsonl:465`). The current Office status projector does not ingest lane leases or build process state (`scripts/office/office_status.py:280-287`).

Why medium: the Office can display claims only after humans post them. It cannot yet arbitrate or prevent concurrent `out/` builds.

## Recommendations

These are advisory and deliberately scoped to reliability before product design.

**High - Treat federated tabs as opportunistic participants, not reliable workers.**  
For the "every agent responds" goal, each agent needs an owned responder or worker whose lifecycle the Office controls. The federated tab can still be the high-context owner, but it should be a preferred responder, not the only responder.

**High - Change the guarantee from "message delivered" to a per-message SLA state machine.**  
Minimum lifecycle: `posted -> delivered_to_tab? -> tab_answered | backup_pending -> backup_answered | backup_failed -> escalated`. Store this outside the chat JSONL. Every direct request should have one current state and a visible terminal condition.

**High - Replace or harden the `claude -p` driver before relying on it.**  
The current driver is acceptable for a pilot, but the live 120-second timeouts prove it is not a guarantee. A minimal reliable engine needs structured output as a first-class API result, bounded timeout, cancellation, no inherited project hooks/MCP unless explicitly supplied, small context packets, and retry/fallback classification. If `claude -p` cannot provide that locally, use an SDK/API worker for the responder path.

**High - Split response into cheap deterministic ack plus optional LLM content.**  
For reliability, first post a deterministic marked acknowledgement when the real tab misses the SLA, then optionally run the LLM for a richer non-binding assessment. That turns "the model timed out" from "no response" into "response acknowledged, substance failed/escalated."

**High - Add durable attempt records and budgets.**  
Each backup attempt should record trigger seq, responder agent, due time, engine, prompt size, start/end time, outcome, cost if known, retry count, and escalation target. Cap per-agent concurrent attempts and per-hour spend.

**Medium - Keep the anti-fanout gate, but add an explicit "require all ack" message kind.**  
Generic `@all` should not summon every paid responder. But if Hemanth wants "everybody make presence known," that should be a distinct message kind or command with known cost, one lightweight ack per agent, and visible missing-agent list.

**Medium - Make `arc=auto_reply` a real metadata field, not an overloaded arc string.**  
The cascade guard holds today, but the schema should distinguish `kind:"auto_reply"` or `meta.backup_for/trigger_seq/reply_class`. That makes UI filtering, dedup, and audits less fragile.

**Medium - Move status to a Room State Store.**  
Keep `agents/bus.jsonl` for human-readable room chat, but put roster projection, request lifecycle, responder state, heartbeats, build/lease state, and failure counters in SQLite or an append-only event log with materialized snapshots.

**Medium - Enforce or at least machine-check shared-tree safety before concurrent work.**  
At minimum, Office should display dirty tree owners, active build processes, build lane claims, and last known lease state. Stronger: prevent a responder/foreman from recommending destructive git operations while the tree has uncommitted tracked changes by another active agent.

**Low - Keep the current JSONL append bus for Slice 1 chat.**  
It is not the main defect for small-room chat. Do not overbuild transport before the response lifecycle and worker engine are fixed.

## Hypothesized root causes (Agent 0 to validate)

- **Hypothesis -** The first Office iteration optimized for fast human-visible coordination, so delivery and presence primitives landed before durable request/response lifecycle state. **Agent 0 to validate.**
- **Hypothesis -** The responder design inherited the successful Codex bridge shape but underestimated the difference between `codex exec` with schema/output-file support and `claude -p` running inside a full Claude Code project environment. **Agent 0 to validate.**
- **Hypothesis -** The anti-fanout gate was intentionally tightened to prevent cost/ping-pong, but that means "all agents must answer this broadcast" needs a separate explicit command rather than normal `@all` chat. **Agent 0 to validate.**
- **Hypothesis -** Shared-tree/build collisions are worsening because the Office successfully increases parallel coordination before the state layer can enforce dirty-tree, build-lane, and lease invariants. **Agent 0 to validate.**

## Flaw ratings

| Flaw | Rating | Why |
|---|---:|---|
| Federated tabs as reliable responders | High | Office cannot own tab lifecycle or force a model turn. |
| Seen/drained without answered state | High | Direct cause of silent drops. |
| `claude -p` responder timeout/failure | High | Live safety-net failure at seq 461/465. |
| Consume-before-success responder cursor | High | Prevents retry storms but sacrifices guaranteed delivery. |
| Bus as truth/state store | High | No durable lifecycle for requests, attempts, or escalation. |
| Flat-on-master concurrent work | High | Live clobber/build collision evidence. |
| Generic `@all` cannot force every backup | Medium | Correct anti-cost gate but fails presence-roll-call semantics. |
| Status/backup chips overpromise | Medium | "Backup armed" can still become backup-failed. |
| Full-file scans and polling | Medium | Fine now, poor for high-frequency worker/event state. |
| Off-channel mirror limited to commits | Medium | Ordinary decisions and dirty work remain invisible. |
| `arc=auto_reply` cascade guard | Low | Patched, tested, and live tail shows no ping-pong. |
| JSONL append lock | Low | Adequate for current local chat volume. |

## One-line summary for chat.md

Agent 7 audit written - agents/audits/office_defect_audit_2026-06-01.md. Bottom line: federated tabs can never guarantee every agent responds; owned workers are required, but current `claude -p` responders still fail under timeout/cost, so fix worker engine, per-message state, and shared-tree/build safety first.
