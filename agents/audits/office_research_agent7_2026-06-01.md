# Audit - The Office Research Pass - 2026-06-01

By Agent 7 (Codex). For Agent 0 / Hemanth / The Office.
Research mode: web research authorized. This builds on `agents/audits/office_defect_audit_2026-06-01.md`.
Scope: technical patterns worth stealing for our local Python Office. No code changes. No market-landscape ranking.

## Executive read

The wider field does not solve "every agent responds" by keeping a chat tab warm. It solves it by making the responder an owned unit of execution, putting every request into durable state, and requiring a terminal outcome for every unit: answered, failed, retried, escalated, cancelled, or waiting for human approval.

For our Office, the buildable shape is not "bigger chat." It is:

1. A Room State Store in SQLite: message lifecycle, worker attempts, ack/fail state, leases, dirty-tree state, cost, and traces.
2. Owned responder/workers as activities: tiny context packet in, structured result out, timeout/retry/circuit-breaker around every model call.
3. Isolated task execution for real code work: branch/PR-style or at least durable local commit checkpoints, with human review gates before merge/apply.

The strongest patterns to steal are Temporal-style durable execution, RabbitMQ/NATS-style acknowledgements and deduplication, LangGraph/OpenAI Agents SDK trace/session primitives, and coding-agent product isolation via remote branch/PR workers.

## Part A - Reliability architectures

### 1. Durable workflow state machines

**Sources / pattern:** Temporal describes crash-proof execution that resumes workflows from where they left off after crashes or outages, and the Python ecosystem around Temporal emphasizes retrying failed model/API interactions until completion while replaying saved decisions ([Temporal docs](https://docs.temporal.io/), [Pydantic AI Temporal integration](https://pydantic.dev/docs/ai/integrations/durable_execution/temporal/)). Temporal APIs also make activity heartbeats, start-to-close timeouts, and retry policy first-class ([Temporal API docs](https://api-docs.temporal.io/)).

**What it solves:** This directly maps to our `claude -p` failure. Today a responder consumes the trigger before drafting and then can timeout with only a `backup-failed` activity line. A durable workflow would persist "attempt started", timeout, retry count, last error, and next action, then resume or escalate rather than silently ending the responsibility.

**Buildable for our Office:** Yes, without adopting full Temporal first. Implement the minimal version in SQLite:

- `office_requests(id, trigger_seq, from_agent, to_agent, kind, state, due_at, terminal_at)`
- `office_attempts(id, request_id, worker, engine, started_at, ended_at, status, error, cost_estimate, output_seq)`
- state machine: `posted -> tab_window -> backup_due -> backup_running -> backup_answered | backup_failed -> retry_wait | escalated`

Use Python subprocesses as activities for now. If the Office grows into real autonomy, revisit Temporal Python as the durable runtime. For Phase 2, the important steal is the state machine plus timeout/retry semantics, not the whole platform.

### 2. Broker acknowledgements, publisher confirms, and redelivery

**Sources / pattern:** RabbitMQ's reliability docs draw the line we are currently missing: an acknowledgement means the receiver has taken responsibility for the message, not merely seen bytes. Without acknowledgements, loss is possible; with manual acknowledgements, unacked deliveries can be requeued on failure ([RabbitMQ reliability guide](https://www.rabbitmq.com/docs/reliability), [RabbitMQ confirms guide](https://www.rabbitmq.com/docs/3.13/confirms)). NATS JetStream adds server-side publish acknowledgement, deduplication keys, and double acknowledgements for stronger "processed" certainty ([NATS JetStream](https://docs.nats.io/nats-concepts/jetstream), [JetStream model deep dive](https://docs.nats.io/using-nats/developer/develop_jetstream/model_deep_dive)).

**What it solves:** Our bus cursor currently says "delivered or drained", not "the brother accepted responsibility." That is why seen can still mean dropped. Broker semantics say the cursor should not be the lifecycle authority; ack is a separate event with stronger meaning.

**Buildable for our Office:** Yes, locally. Keep `agents/bus.jsonl` for readable chat, but add explicit events/state:

- `message_posted`
- `delivery_seen(agent, seq)`
- `responsibility_ack(agent, seq)` for "I will handle this"
- `reply_posted(reply_to_seq, output_seq)`
- `backup_attempt_started/failed/succeeded`
- idempotency key: `trigger_seq + responder_agent + reply_class`

For direct requests, do not consider the request closed until `reply_posted`, `responsibility_ack`, or explicit `decline/blocked/escalated`. This is the core cure for "drain marked it seen, still no reply."

### 3. Event sourcing plus projections

**Sources / pattern:** Microsoft's Event Sourcing pattern records operations as a sequence of events and uses projections/materialized views to reconstruct state and support audit/history ([Azure Architecture Center](https://learn.microsoft.com/en-us/azure/architecture/patterns/event-sourcing)).

**What it solves:** The Office has both an audit problem and a status-projection problem. Chat lines are not enough to reconstruct "what happened to request 461?" unless every lifecycle transition is an event. Event sourcing gives us replayable room state and postmortem ability.

**Buildable for our Office:** Yes. Use SQLite append-only `office_events` as canonical truth and derive:

- `current_roster`
- `open_requests`
- `worker_health`
- `build_lane_state`
- `cost_by_agent`
- `unanswered_directs`

Do not put every heartbeat tick in the event log. Store high-frequency heartbeat snapshots separately, and log transitions: worker became live/down, responder failed, circuit opened, etc.

### 4. Actor supervision

**Sources / pattern:** Akka's supervision model separates failure handling from normal message logic. Supervisors can resume, restart, stop, or escalate failed actors; DeathWatch/lifecycle monitoring detects actor termination ([Akka supervision](https://doc.akka.io/libraries/akka-core/current/general/supervision.html), [Akka fault tolerance](https://doc.akka.io/libraries/akka-core/current/fault-tolerance.html)).

**What it solves:** Our `office_responders.py` supervises process existence, but not quality of service. A child can be alive while every `claude -p` call times out. Actor supervision says each worker needs a policy: retry with backoff, restart, open circuit, degrade to deterministic ack, or escalate.

**Buildable for our Office:** Yes. Add a supervisor table/policy:

- `worker_status`: live, degraded, circuit_open, stopped
- failure window counters per agent/engine
- restart/backoff policy
- max concurrent attempts per worker
- fallback chain: `tab -> deterministic ack -> cheap model -> strong model -> Hemanth/Agent0 escalation`

The key is to stop treating "PID exists" as "responder works."

### 5. LangGraph persistence and human interrupts

**Sources / pattern:** LangGraph persistence saves graph state as checkpoints and enables human-in-the-loop workflows, memory, time-travel debugging, and fault-tolerant execution ([LangGraph persistence docs](https://langchain-5e9cc07a.mintlify.app/oss/python/langgraph/persistence), [LangGraph checkpoints reference](https://reference.langchain.com/python/langgraph/checkpoints)).

**What it solves:** This is the right pattern for future Foreman loops and approval gates. The graph state is not the chat transcript. It is the current task, last node, pending approval, tool outputs, and next transition.

**Buildable for our Office:** Maybe, but do not start there. LangGraph is useful if the Office starts orchestrating multi-step tasks: plan -> dispatch -> wait -> review -> build -> gate -> handoff. For the immediate responder guarantee, a simpler SQLite state machine is easier and less dependency-heavy. Steal checkpoints/interrupt semantics; adopt LangGraph only if Foreman workflows become graph-shaped.

### 6. CrewAI Flows and human feedback

**Sources / pattern:** CrewAI Flows provide structured event-driven workflows with state, branching, loops, and human feedback decorators that pause execution for review/approval ([CrewAI Flows](https://docs.crewai.com/en/concepts/flows), [CrewAI human feedback](https://docs.crewai.com/en/learn/human-feedback-in-flows), [CrewAI flow state](https://docs.crewai.com/en/guides/flows/mastering-flow-state)).

**What it solves:** This validates our future "Operations" surface: task state, branches, human checkpoints, and reusable flows are normal agent-orchestration requirements. It is less directly about guaranteed response and more about organizing work once workers are reliable.

**Buildable for our Office:** Partly. The state/pause/resume model maps well. CrewAI itself may be overkill for our local scripts because the brotherhood already has agent identities, governance, and custom tooling. Steal the "Flow" vocabulary for Office: every task has a visible flow state and can pause on Hemanth/Agent0 approval.

### 7. OpenAI Agents SDK worker primitives

**Sources / pattern:** OpenAI's Agents SDK gives an `Agent` plus `Runner` loop that manages turns, tools, guardrails, handoffs, and sessions ([Agents overview](https://openai.github.io/openai-agents-python/agents/)). It supports sessions with SQLite-backed memory ([Sessions](https://openai.github.io/openai-agents-python/sessions/)), handoffs with input filters ([Handoffs](https://openai.github.io/openai-agents-python/handoffs/)), guardrails that can block before model/tool execution ([Guardrails](https://openai.github.io/openai-agents-js/guides/guardrails)), and tracing of model calls, tools, handoffs, guardrails, and custom events ([Tracing](https://openai.github.io/openai-agents-python/tracing/)).

**What it solves:** This is closest to a clean replacement for our `claude -p` responder. It gives structured worker runs, trace records, handoffs, guardrails, and explicit session state without scraping a CLI result string. It also supports "manager plus specialized agents" if we later run Codex/OpenAI-backed workers inside Office.

**Buildable for our Office:** Yes for Agent 7/Codex-like responders and maybe non-Claude brothers. Recommendation:

- Build `office_worker_openai.py` around Agents SDK for one non-Claude owned responder.
- Store session per brother using SQLiteSession or our own state store.
- Emit trace ids into `office_attempts`.
- Use guardrails to enforce "reply-only, non-binding, no file writes" before tool execution.
- Use handoffs for "this is real code work, hand to human tab / Agent0 / future worker."

It does not replace Claude domain agents by itself unless Hemanth accepts OpenAI-backed backup voices for Claude brothers.

### 8. Claude Agent SDK / Claude Code subagents

**Sources / pattern:** Claude Code subagents are specialized agents with separate context windows, own prompts, tools, and model choices ([Claude Code subagents](https://code.claude.com/docs/en/sub-agents), [Claude SDK subagents](https://code.claude.com/docs/en/agent-sdk/subagents)). Claude SDK sessions track prompt, tool calls, tool results, responses, and support resume/fork semantics ([Claude Agent SDK sessions](https://platform.claude.com/docs/en/agent-sdk/sessions)).

**What it solves:** For Claude-native workers, SDK sessions are the correct direction. They expose session concepts and subagent context isolation more cleanly than `claude -p`. Subagents solve "fresh context for specialized work"; sessions solve "resume/fork history." They do not automatically solve our lifecycle/ack state; we still need the Office state machine outside the SDK.

**Buildable for our Office:** Yes if the local Claude SDK/CLI contract is stable enough. Recommendation:

- Stop treating `claude -p` as the production responder substrate.
- Prototype `office_worker_claude_sdk.py` that submits minimal prompts, captures session id, resumes/forks deliberately, and writes structured attempt rows.
- Use Claude subagent definitions for specialized backup classes: `ack-responder`, `reviewer`, `build-diagnoser`.
- Keep the Office supervisor authoritative; Claude sessions are workers, not the source of truth.

### 9. AutoGen / AG2 team termination and external stop

**Sources / pattern:** AutoGen AgentChat teams use explicit termination conditions: max messages, timeout, token usage, source match, handoff, external termination, stop messages, etc. ([AutoGen termination docs](https://microsoft.github.io/autogen/0.4.8/user-guide/agentchat-user-guide/tutorial/termination.html)). AutoGen Studio models skills, agents, models, and workflows declaratively ([AutoGen Studio docs](https://autogenhub.github.io/autogen/docs/autogen-studio/usage/)).

**What it solves:** It gives two useful patterns: all loops need stop conditions, and agents/workflows should be declarative objects that the UI can inspect. This maps to cost governors and runaway-conversation prevention.

**Buildable for our Office:** Yes as design, not dependency. Every Office workflow should carry:

- max attempts
- max model seconds
- max tokens/cost
- max messages
- required terminal condition
- external stop flag

This is also how we make Hemanth's stop button honest: it sets external termination and then the worker transitions to cancelled/needs-inspection.

### 10. OpenAI Swarm routines/handoffs

**Sources / pattern:** OpenAI Swarm was an experimental educational framework around agents and handoffs, now superseded by the production OpenAI Agents SDK ([OpenAI Swarm GitHub](https://github.com/openai/swarm)).

**What it solves:** It is useful mostly as a minimal conceptual model: agent = instructions + tools; handoff = transfer to another specialist. It does not solve durability, process supervision, retry, or state storage.

**Buildable for our Office:** Steal the conceptual simplicity, not the package. Our "brother capability contract" should be this simple at the top level:

- identity
- role
- tools
- writable scope
- can_answer
- can_work
- can_build
- can_use_desktop
- escalation target

Then the durable state layer handles reliability below it.

## Part B - Capability landscape: what Office can become

### 1. Task board / Operations surface

**Sources / pattern:** Coding agents increasingly expose tasks, branches, status, and review surfaces instead of only chat. Cursor background agents show a sidebar for all background agents; they clone repos, work in remote environments, and push branches for handoff ([Cursor background agents](https://docs.cursor.com/background-agent)). Claude Code on the web runs remote tasks in isolated VMs and creates pull requests for review ([Claude Code on the web](https://support.claude.com/en/articles/12618689-claude-code-on-the-web), [web quickstart](https://code.claude.com/docs/en/web-quickstart)).

**What it solves:** This maps to our current flat-on-master collisions. Work is not just conversation; it has task id, worker, branch, diff, tests, PR/review status, and owner.

**Buildable recommendation:** Add an Office Operations view before autonomous code work:

- Direct requests / open asks
- Active tasks
- Build lane / desktop lane
- Dirty tree / files touched
- Worker attempts
- Review gates
- "needs Hemanth" queue

Do not bolt this into the chat log. It should be a separate projection from SQLite.

### 2. Isolated worker branches / PR handoff

**Sources / pattern:** Claude Code web, Cursor background agents, and Factory Droids converge on isolation and review. Claude web uses isolated VMs and PRs. Cursor agents work remotely on separate branches. Factory Droids handle planning, implementation, testing, and PR creation, while keeping review workflows visible ([Factory Droids](https://factory.ai/product/droids), [Factory CLI overview](https://docs.factory.ai/cli/getting-started/overview)).

**What it solves:** Our shared-tree audit found real clobber/build collision risk. The field answer is not "trust agents harder"; it is isolate work, then review/apply.

**Buildable recommendation:** For local Office, reconcile with gov-v13 carefully:

- Keep human brothers flat-on-master for today's manual flow.
- For Office-owned workers, use separate branch or patch artifact as the isolation unit.
- If Hemanth rejects worktrees, store worker output as patch files plus metadata and require Agent0/Hemanth apply.
- Never let an owned worker mutate the shared tree without an Office-held lease and dirty-tree preflight.

### 3. Planning/spec mode before doing

**Sources / pattern:** Factory recommends Specification Mode for complex features: explore and plan before coding; prompts should define success and verification ([Factory context/power-user docs](https://docs.factory.ai/user-guides/managing-context/context-retrieval), [How to talk to a Droid](https://docs.factory.ai/cli/getting-started/how-to-talk-to-a-droid)). CrewAI and LangGraph also encode multi-step workflows as explicit state, not pure chat.

**What it solves:** It prevents long-session drift and unclear scope. The brotherhood already does this with brainstorm/plans/TODOs; the Office can make it visible and enforceable.

**Buildable recommendation:** Add task states:

`intake -> clarify -> plan -> approved -> executing -> verifying -> review -> done`

Attach required artifacts: scope, files, anti-scope, verification command, owner, and gate. Let chat mention tasks, but task truth lives in the state store.

### 4. Human approval gates

**Sources / pattern:** LangGraph checkpoints plus interrupts and CrewAI human feedback both pause workflows for human review before risky downstream actions ([LangGraph persistence](https://langchain-5e9cc07a.mintlify.app/oss/python/langgraph/persistence), [CrewAI human feedback](https://docs.crewai.com/en/learn/human-feedback-in-flows)). OpenAI Agents SDK guardrails can block before model or tool execution when configured synchronously ([OpenAI guardrails](https://openai.github.io/openai-agents-js/guides/guardrails)).

**What it solves:** It maps to our "green build is necessary but not sufficient" problem and to Hemanth's role boundary. Agents can prepare work; humans approve irreversible transitions.

**Buildable recommendation:** Office gates:

- `needs_human_approval`: before applying a worker patch
- `needs_agent0_gate`: before shared-file / build-system changes
- `needs_domain_owner_gate`: before cross-domain changes
- `needs_hemanth_gate`: taste, risk, and final veto

The gate should suspend the workflow and display what exactly is being approved.

### 5. Role and capability registry

**Sources / pattern:** OpenAI Agents SDK, Swarm, Claude subagents, Factory custom droids, and Devin workspaces all make roles/tools/context explicit. Factory custom droids carry their own prompt, model preference, and tooling policy ([Factory custom droids](https://docs.factory.ai/cli/configuration/custom-droids)); Devin native workspaces scope setup and knowledge per subdirectory ([Devin workspaces](https://docs.devin.ai/onboard-devin/environment/workspaces)).

**What it solves:** Our agents are not equivalent. Agent 7, Claude tabs, DeepSeek, future SDK workers, and human-opened tabs differ in context, tools, cost, cancellation, write authority, and reliability.

**Buildable recommendation:** Add `office_capabilities.yaml` or SQLite table:

- `agent_id`
- `engine`
- `owned_by_office` true/false
- `can_be_woken`
- `can_be_cancelled`
- `can_write_repo`
- `can_build`
- `can_use_desktop`
- `allowed_paths`
- `default_model`
- `cost_class`
- `reliability_class`

The UI should show capability differences, not smooth them over.

### 6. Shared memory / knowledge

**Sources / pattern:** Devin Knowledge is an onboarding-style knowledge base that can be scoped/pinned to repositories and retrieved when relevant ([Devin Knowledge](https://docs.devin.ai/onboard-devin/knowledge-onboarding), [Devin Playbooks](https://docs.devin.ai/product-guides/creating-playbooks)). CrewAI has memory and knowledge systems for recall across tasks ([CrewAI memory](https://docs.crewai.com/en/concepts/memory)).

**What it solves:** The brotherhood already has memory files, AGENTS.md, governance, TODOs, and audits, but retrieval is social/manual. Office can make "what should this worker know?" explicit and cheap.

**Buildable recommendation:** Start with deterministic retrieval, not embeddings:

- per-agent required docs
- per-task references
- recent relevant audit/TODO
- last N bus lines for that task
- domain memory files

Later add semantic search. The first win is a reproducible context packet builder that every responder/worker uses.

### 7. Observability and traces

**Sources / pattern:** OpenAI Agents SDK tracing records LLM generations, tool calls, handoffs, guardrails, and custom events by default ([OpenAI tracing](https://openai.github.io/openai-agents-python/tracing/)). Factory and Devin surfaces expose terminals/logs/review artifacts; Factory Droid Control emphasizes evidence from automated QA flows ([Factory Droid Control](https://docs.factory.ai/cli/features/droid-control), [Devin intro/interface](https://docs.devin.ai/)).

**What it solves:** Our current responder failure evidence is scattered across bus lines and per-agent log files. Office needs one trace per request/attempt.

**Buildable recommendation:** Add `trace_id` to every request and attempt. Persist:

- prompt packet hash
- model/engine
- command line or SDK run id
- stdout/stderr tail
- output seq
- cost estimate
- duration
- terminal status

Expose it in the Office UI so a backup failure is one click away from logs.

### 8. Cost governors

**Sources / pattern:** AutoGen has token-usage and timeout termination conditions ([AutoGen termination](https://microsoft.github.io/autogen/0.4.8/user-guide/agentchat-user-guide/tutorial/termination.html)). OpenAI guardrails can run before model calls to prevent unnecessary expensive execution ([OpenAI guardrails](https://openai.github.io/openai-agents-js/guides/guardrails)). Factory publishes cost/quality benchmark thinking around review models ([Factory review benchmark](https://docs.factory.ai/benchmarks/review-benchmark)).

**What it solves:** Our live `claude -p` backup failures are expensive in wall time and likely token load. Cost should be a first-class state dimension.

**Buildable recommendation:** Per engine/agent:

- max backup calls per hour
- max concurrent backup calls
- max seconds per attempt
- cheap deterministic ack before expensive model
- daily/shift cost budget
- circuit-open after repeated timeouts

The Office should display "backup net degraded: claude driver timed out 2/2 recent attempts" rather than just "backup armed."

### 9. Consensus / voting / review mechanisms

**Sources / pattern:** AutoGen group chat/teams and OpenAI handoffs model specialist collaboration. Coding-agent products mostly keep merge authority human even when agents carry PR work forward; a recent empirical study frames this as agents taking initiative while humans retain endorsement/merge authority ([PR lifecycle study](https://arxiv.org/abs/2605.08017)).

**What it solves:** This maps to Congress. The useful part is not free-form debate; it is structured positions, objections, owner veto, and final gate.

**Buildable recommendation:** Keep Congress human/Agent0-led, but make it stateful:

- motion id
- requested agents
- required positions
- received positions
- missing positions / backup positions
- domain owner position
- Agent0 synthesis
- Hemanth final word

Office can run the roll call and deadlines; it should not replace Hemanth/Agent0 authority.

## Pattern fit matrix for our Office

| Pattern | Solves owned-worker reliability? | Solves per-message lifecycle? | Buildable locally now? | Recommendation |
|---|---:|---:|---:|---|
| SQLite state machine inspired by Temporal | High | High | High | Do first. |
| RabbitMQ/NATS ack/dedup semantics | Medium | High | High | Copy semantics, not broker. |
| Event sourcing + projections | Medium | High | High | Use SQLite event log + projections. |
| OpenAI Agents SDK worker | High for OpenAI-backed workers | Medium | Medium | Pilot for Agent7/non-Claude responder. |
| Claude Agent SDK worker | High if stable | Medium | Medium | Replace `claude -p` when contract is proven. |
| LangGraph | Medium | High | Medium | Use for Foreman workflows later. |
| CrewAI Flows | Low/Medium | Medium | Medium | Steal flow/gate concepts; dependency optional. |
| AutoGen teams | Low/Medium | Medium | Medium | Steal termination/cost controls. |
| Coding-agent remote branch/PR model | High for code work | Medium | Medium | Use patch/branch isolation for owned workers. |
| Current shell watch | Low | Low | Already built | Keep as opportunistic presence only. |

## Top 3 steal these

1. **Temporal-style durable request workflow.** Every direct ask gets a durable lifecycle, attempts, retries, timeout, and terminal state. This is the cure for "seen but no reply."
2. **Broker-style ack/dedup semantics.** Separate "delivered", "responsibility accepted", "replied", and "processed"; use idempotency keys and never treat cursor advance as answer.
3. **Coding-agent branch/PR isolation.** For any Office-owned code work, isolate the workspace/output and gate application through human/Agent0 review. Chat coordination can stay flat; autonomous mutation cannot.

## What not to steal yet

- Do not deploy Temporal before the Office has a simple SQLite state machine. The mental model matters more than the infrastructure right now.
- Do not turn the Office into a generic CrewAI/AutoGen playground. The brotherhood already has roles and governance; generic frameworks will fight the local protocol unless wrapped carefully.
- Do not rely on `claude -p` CLI result scraping for a guaranteed responder. It can be a pilot tool, not the reliability foundation.
- Do not make generic `@all` imply paid all-agent response. Add an explicit roll-call/require-ack command with visible cost and missing-agent list.

## One-line summary for chat.md

Agent 7 research audit written - agents/audits/office_research_agent7_2026-06-01.md. Bottom line: steal durable workflow state, broker ack/dedup semantics, and isolated branch/PR worker handoffs; do not try to make federated tabs or `claude -p` scraping carry the reliability guarantee.
