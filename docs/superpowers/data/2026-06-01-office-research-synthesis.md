# The Office — Phase 2 Research Synthesis (2026-06-01)

Distilled from three independent Phase-2 research passes (deliberately different prompts):
- **Agent 7 / Codex** (technical, buildable, ours-specific): `agents/audits/office_research_agent7_2026-06-01.md`
- **ChatGPT deep research** (market/practice landscape): "Solo Operator AI Coding Agent Brotherhoods"
- **Gemini deep research** (complementary landscape): tools, features, supervision, anti-patterns

This is the design source-of-truth for Phase 3 (where-do-we-take-the-Office brainstorm). Raw GPT/Gemini reports not saved verbatim (Gemini had heavy encoding artifacts; the convergent signal is captured here).

---

## The headline: we independently reinvented a real, named category

The Office is not a one-off. The field has the exact same shape under other names:
- **"Agent Room" (godlymane, MCP-based)** = "IRC for agents": isolated agent sessions join a shared message log, and a **Stop hook force-continues an agent when a new message arrives** — *that is our `office_watch.sh` + Stop-hook wake mechanism, independently built.*
- **EPAM "Octobots"** = a human `@`-addresses agents over Telegram, agents post status back, with a TUI supervisor — *that is our Office room + roster.*
- **Agent Kanban / Vibe Kanban / Conductor / Claude Squad** = mission-control dashboards over parallel agents.

So the Office's existence is justified beyond "live chat.md + live congress." It's a recognized pattern (chat-room + mission-control for an agent fleet). The question isn't *whether* — it's *how far*.

## The reliability cure is UNANIMOUS across all three (answers goal #1)

None of the field solves "every agent responds" by keeping a chat tab warm. The convergent cure:

1. **Owned workers, not federated tabs.** A tab you hope responds can never guarantee; the responder must be a unit of execution the system controls. (All three.)
2. **A durable per-request lifecycle (the biggest steal).** Every direct ask gets a state machine: `posted → delivered → responsibility-ACK → replied | failed → retry | escalated`, persisted outside the chat log (Agent 7: SQLite "Room State Store", Temporal-style). This is the cure for our exact wound — *"drain marked it seen, still no reply."*
3. **ACK ≠ seen (RabbitMQ/NATS semantics).** "Acknowledged" must mean *"I took responsibility,"* a separate stronger event than "delivered/drained." Never treat a cursor advance as an answer. (Agent 7 + the broker patterns.)
4. **Required-ack protocol, proven in production (EPAM):** *every task must end with an ack or the pipeline stalls* — and *"act, don't ask"* to stop agents endlessly asking instead of doing. This is the human-protocol half of the same cure.
5. **Circuit-breaker + cheap-deterministic-ACK-before-expensive-model.** When the LLM responder times out (our live 120s `claude -p` failures), first post a deterministic *"✓ acknowledged, escalating"* — so a timeout becomes "acknowledged + escalated," never silence. Then optionally run the model. (Agent 7's actor-supervision + cost-governor patterns.)

**Implication for "is absolute availability even required?" (Hemanth's Phase-3 question):** the field says **no — you don't chase 100% LLM-response certainty.** You make a cheap *acknowledgement* guaranteed and *visible*, and escalate the rest. "Good-enough response + guaranteed ack + visible who's-missing" is the proven sweet spot. Absolute certainty is *achievable* (owned workers) but the cheaper rung is usually enough — exactly the cost/benefit call Phase 3 makes.

## Two tensions the research forces us to confront

1. **Git worktrees — the universal collision fix we RETIRED (gov-v13).** Every source calls worktree-per-agent the answer to parallel-file collisions ("the Git Worktree Revolution"; CAID, Conductor, Claude Squad, Cursor all isolate per worktree/branch). We went flat-on-master and have **live clobber evidence** (the `reset --hard` that wiped a brother's work). **Agent 7's reconciliation:** keep *human* brothers flat-on-master for the manual flow, but **Office-OWNED workers must isolate** (separate branch / patch artifact + Agent0/Hemanth apply, never mutate the shared tree without an Office lease + dirty-tree preflight). Worth re-litigating *only* for owned workers, not humans.
2. **Congress: consensus → voting.** Research (arXiv 2502.19130) is blunt: **consensus/debate degrades into infinite loops and diluted compromises; independent parallel VOTING is best practice.** Our Congress is consensus-style. **Agent 7's path:** keep Congress human/Agent0-led but make it *stateful* — motion → required positions → received/missing → owner veto → Hemanth final word; the Office runs the roll-call + deadlines, doesn't replace authority.

## Things the field VALIDATES that we already do

- **Different-model reviewer = the named "HBOON pattern"** (a *different foundational model* reviews to avoid shared blind spots) → exactly Agent 7's cross-model role. Strongly validated.
- **Cheap models for review, premium for planning** → our engine-switching (DeepSeek/Codex grunt, Opus design). Validated; add cost governors as first-class.
- **3–5 agents is the sweet spot; over-agenting hurts** (pruning 7→5 *accelerated* delivery; coordination cost is superlinear) → our "don't run 6 tabs when 2 are working."
- **One task per session / context purity** → our wake-cycle + quota discipline.
- **Knowledge/memory files (CLAUDE.md / AGENTS.md)** → we have these; the win is a *reproducible context-packet builder* every worker uses (deterministic retrieval first, embeddings later).

## Capability landscape — what the Office could GROW into (goal #2)

Stealable feature → problem it solves:
- **Operations / task-board surface** (separate from chat): direct-asks, active tasks, build/desktop lane, dirty-tree, worker attempts, review gates, "needs Hemanth" queue. Don't bolt onto the chat log — project from the state store.
- **Capability registry** (per agent: engine, owned-by-office?, can-wake/cancel/write/build/desktop, allowed-paths, default-model, cost-class, reliability-class). *Show* the differences between a Claude tab, Codex, DeepSeek, an SDK worker — don't smooth them over.
- **Observability / one trace per request** (prompt-packet hash, engine, run id, stdout tail, cost, duration, terminal status) — a backup failure one click from its logs.
- **Cost governors** (max backup calls/hr, max seconds/attempt, daily budget, circuit-open after repeated timeouts) — "backup net DEGRADED: claude driver timed out 2/2" instead of "armed."
- **Approval gates** (needs-human / needs-agent0 / needs-domain-owner / needs-Hemanth) that suspend a workflow and show exactly what's being approved.
- **Roll-call / require-ack command** to fix `@all` (today it intentionally summons no backups) — an explicit "everybody check in" with one lightweight ack per agent + a visible missing-agent list.
- **Planning/spec state** (`intake → clarify → plan → approved → executing → verifying → review → done`) with required artifacts (scope, anti-scope, verify command, owner, gate).
- **Owned-worker substrate**: replace `claude -p` result-scraping with the **Claude Agent SDK / OpenAI Agents SDK** (structured output, sessions, guardrails, tracing) — pilot a non-Claude owned responder (Agent 7/Codex shape already proves it).

## Tool shortlist worth learning from
- **Factory.ai** (Droid roles: Coordinator/Code/Review/Test/Knowledge; rigid role boundaries; Knowledge droid = shared memory).
- **Agent Room / Agent Kanban / Conductor** (the "IRC-for-agents" + Kanban-board surfaces nearest our shape).
- **OpenHands / CAID** (academic: dependency-graph decomposition → isolated worktrees → self-verify → central merge; +26.7% accuracy).
- **Bernstein** (deterministic non-LLM scheduling + a "Janitor" that tests/lints every PR pre-merge).
- **AgentOps** (observability + time-travel debugging — the dashboard model).
- **Temporal / RabbitMQ / NATS** (the durability/ack primitives to copy semantically, not adopt wholesale).

## Anti-patterns (validated against our own pain)
- **No feedback loop / no required-ack → pipeline stalls.** (Our exact unresponsiveness wound; EPAM's required-ack is the fix.)
- **Silent failures** — agents hide errors under try/except, rewrite signatures without updating callers. Force executable self-verification, never read-only self-review.
- **Over-agenting** — more agents ≠ more capability; coordination cost dominates.
- **Parallel-first on entangled code** — only parallelize genuinely decoupled modules.
- **Vague/"optional" instructions** — use deterministic scripts, not soft suggestions.
- **Consensus debate** — use voting.

## Key sources
Temporal (durable execution), RabbitMQ/NATS JetStream (ack/dedup), Azure Event Sourcing, Akka supervision, OpenAI Agents SDK, Claude Code subagents/SDK, LangGraph persistence, CrewAI Flows, AutoGen termination, Cursor background agents, Devin, Factory Droids; ChatGPT/Gemini landscape (Conductor, Claude Squad, Agent Room/godlymane, AgentR/daraijaola, Agent Kanban, OpenHands/CAID arXiv 2603.21489, EPAM Octobots, voting-vs-consensus arXiv 2502.19130, git-worktree practitioner reports).
