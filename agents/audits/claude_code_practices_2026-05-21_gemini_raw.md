# Gemini Deep Research Report — Claude Code Practices Audit for Tankoban 2
# Source: Google Gemini Deep Research
# Date received: 2026-05-21
# Status: RAW — preserved verbatim for Codex Trigger C cross-reference

---

Comprehensive Best-Practices and Efficiency Audit for Multi-Agent LLM Workflows: Tankoban 2 Project

The following document represents an exhaustive, expert-level analysis of the multi-agent Large Language Model (LLM) workflow currently deployed for the development of the "Tankoban 2" application. The analysis synthesizes current architectural capabilities, known platform limitations, advanced orchestration paradigms, and empirical performance metrics to address specific friction points within the Qt6 C++ desktop application environment. The Tankoban 2 project utilizes a highly ambitious multi-agent "brotherhood" pattern, coordinating eight parallel Claude Code sessions across a shared Windows 11 environment to drive a C++ application utilizing native sidecars, inter-process communication over named pipes, and a bespoke developer-control bridge.

While this architecture achieves significant theoretical parallelization, an analysis of the provided project constraints indicates that it currently suffers from severe systemic friction points. These include Model Context Protocol (MCP) initialization timeouts, crippling context-window token pressure from monolithic communication files, build-tree clobbering within a shared Git working tree, and compounding token costs from inefficient polling loops. Furthermore, operating eight concurrent agent sessions, multiple VS Code extension hosts, and heavyweight compilation toolchains on hardware constrained to 16 GB of RAM with integrated graphics creates a fragile operational envelope where process overhead directly degrades AI reasoning latency.

Comprehensive analysis indicates that the current manual coordination primitives—such as chat-based lane locks, append-only status files, and standard input/output (stdio) subprocess spawning—are rapidly becoming obsolescent in the face of native tooling advancements. By migrating from stdio-spawned MCPs to daemonized Streamable HTTP servers, shifting from a flat Git tree to native Git worktrees, replacing monolithic memory indices with progressive disclosure architectures, and converting nag-only post-execution hooks into blocking pre-execution gates, the development workflow can achieve strict concurrency safety while simultaneously driving down token expenditure and latency. The subsequent sections detail the specific mechanics, theoretical underpinnings, and implementation strategies required to execute these architectural shifts.

## Per-Research-Question Findings

### A. MCP Server Lifecycle and Performance

**Direct Answer:**
The recurring 60000ms MCP_TIMEOUT and the 30-45 second "thinking" lag observed upon initialization on the Windows 11 host are not symptoms of transient network latency. Instead, they are the direct result of a systemic command-line interface parsing bug specific to the Windows implementation of the agent software, combined with the catastrophic architectural overhead of standard input/output (stdio) subprocess spawning via package managers. The canonical pattern for mitigating this on Windows is to abandon npx and uvx stdio transport entirely in favor of daemonized Streamable HTTP transports.

**Detailed Analysis:** The architecture of the Model Context Protocol (MCP) allows agents to interface with external tools through standardized JSON-RPC messages. The default transport mechanism for local execution is stdio, wherein the agent spawns a child process (e.g., a Python script or a Node.js package) and communicates over standard input and output streams. However, attempting to utilize this mechanism on Windows via standard command wrappers like cmd /c uvx triggers a deeply embedded parser corruption bug (referenced as GitHub issue anthropics/claude-code#36808). The cross-platform command-line parser incorrectly interprets the Windows /c execution flag, translating it either into a root drive path (C:/) or a system scheduling flag (/schedule). This corruption fundamentally breaks the server configuration. Even if a developer attempts to bypass the parser by manually editing the .mcp.json or .claude.json configuration file to enforce the correct syntax, the runtime engine aggressively re-evaluates the arguments upon launching the session, re-introducing the argument corruption and causing the server to permanently display a failed connection status.

Beyond the parsing bug, the usage of package execution wrappers (npx or uvx) introduces severe latency penalties that manifest as the 30-45 second "thinking" indicator delay. Every time a new session tab is initialized, the package manager attempts to resolve metadata, check for updates, and allocate a fresh runtime environment. In a memory-constrained 16 GB RAM environment running eight concurrent agents, the operating system's process scheduler and memory compression algorithms are severely taxed, exacerbating the startup latency. The state-of-the-art solution is to decouple the MCP lifecycle from the agent session lifecycle entirely by migrating to Streamable HTTP transport.

The Model Context Protocol specification natively supports Streamable HTTP transport, which relies on standard POST and GET requests paired with Server-Sent Events (SSE) for streaming capabilities. By utilizing a background daemonizer—such as mcp-daemonize—or simply wrapping the MCP implementation in a lightweight ASGI web framework like Uvicorn, the MCP servers can be kept alive indefinitely in the background as independent processes. This allows the agent to connect instantly via a local loopback network request without incurring the initialization penalty of a cold subprocess spawn.

When an MCP server fails to handshake within a configured timeout, the protocol dictates evaluating the failure based on the transport type. For stdio transports, a timeout usually indicates a catastrophic process failure requiring a hard block. However, by daemonizing the servers behind HTTP, the connection becomes an instantaneous TCP handshake. If the HTTP endpoint is unreachable, the system should fail-soft, logging the unavailability and allowing the agent to proceed without the specific toolset, rather than hanging the entire initialization sequence.

**Specific Recommendation for Tankoban 2 Setup:** Immediately migrate the pywinauto-mcp and codex.cmd servers from stdio subprocesses to background daemonized services utilizing Streamable HTTP. Deploy a lightweight local HTTP server to host the pywinauto-mcp on a local port (e.g., localhost:8000). Update the project's .mcp.json configuration to utilize "type": "http" (or the accepted alias "streamable-http") instead of relying on the default stdio execution. This architectural shift allows all eight brotherhood agents to connect to a single, pre-warmed instance of the UI automation server instantly, bypassing the 60000ms timeout, circumventing the Windows parser bug entirely, and eliminating the 45-second thinking lag.

**Effort/Impact Estimate:** Effort: Medium. Impact: High.

### B. Context-Window and Memory Management

**Direct Answer:**
Relying on an append-only chat.md (growing to 5000 lines), a massive 1000-line STATUS.md, and a manual MEMORY.md index represents an unsustainable paradigm that degrades model adherence, accelerates token consumption, and triggers severe prompt-cache invalidation. Best practices dictate transitioning to a progressive disclosure memory architecture and heavily utilizing the .claude/rules/ directory for path-scoped, lazy-loaded context orchestration.

**Detailed Analysis:** In long-running development sessions spanning four to eight hours, the conversational context window fills up rapidly. When this occurs, the underlying engine automatically engages compaction algorithms to summarize older turns. During compaction, nuanced instructions and critical architectural constraints introduced early in the conversation are permanently lost. To combat this, developers frequently rely on a CLAUDE.md file, which is guaranteed to be loaded fresh into the context window at the start of every session and after every compaction event, ensuring that baseline knowledge survives. However, scaling a CLAUDE.md file beyond approximately 200 lines leads to a phenomenon known as attention dilution, where the model's adherence to specific rules degrades significantly because the instructions are buried within a monolithic text block.

The current Tankoban 2 architecture forces every agent to load the entire structural knowledge of the brotherhood. The state-of-the-art alternative to monolithic memory files is a progressive disclosure system like claude-mem, a persistent memory compression architecture designed specifically for this ecosystem. claude-mem operates via a sophisticated three-tier retrieval system consisting of an Index layer, a Timeline narrative layer, and a Full Details layer. During a session, it utilizes background lifecycle hooks to silently capture tool executions and observations. It then compresses these observations using a background Node.js worker service and stores them in a local SQLite database enhanced with FTS5 (Full-Text Search) capabilities, optionally supplemented by a Chroma vector index for semantic search. When a new session begins, the context is injected dynamically based on semantic relevance to the current task, saving up to ten times the token consumption compared to loading an entire project history.

For the chat.md brotherhood communications, append-only logs are an anti-pattern that guarantee eventual context overflow and quadratic cost scaling. Cross-session and cross-agent communications should be handled via native agent team orchestrations. Alternatively, if text-based communication is strictly required, the files must be chunked into time-boxed or feature-boxed markdown files that are ingested on-demand via the agent's explicit Read tool, rather than forced into the context window at initialization. Furthermore, static rules that only apply to specific domains—such as Qt6 UI design standards versus embedded libtorrent scraping logic—should be relocated from the central CLAUDE.md to the .claude/rules/ directory. Files in this directory can be scoped to specific file paths using glob patterns, meaning that the libtorrent scraping rules only load into the context window when an agent actually touches those specific C++ headers.

**Specific Recommendation for Tankoban 2 Setup:**
Systematically dismantle the 230-line CLAUDE.md by abstracting component-specific rules (e.g., ffmpeg sidecar IPC rules, Qt6 UI rules) into the .claude/rules/ directory, utilizing path-targeting so they only load when those specific code domains are being edited. Deprecate the manual MEMORY.md index and install the claude-mem system via the plugin marketplace to handle cross-session intelligence and multi-week project state autonomously. Replace the 5000-line append-only chat.md with a strict daily rotation log, and configure the agents to query past decisions using claude-mem's search capabilities rather than relying on the model's finite attention span to parse thousands of lines of unstructured historical text.

**Effort/Impact Estimate:** Effort: High. Impact: High.

### C. Multi-Agent Coordination

**Direct Answer:**
Coordinating eight parallel agents on a single flat Git working tree using manual chat.md lane locks and filesystem flags is a highly fragile anti-pattern that mathematically guarantees build system collisions and merge conflicts. The published, canonical standard for multi-agent LLM workflow coordination operating concurrently on the same repository is strict hardware-level isolation via Git worktrees.

**Detailed Analysis:** The fundamental problem with running parallel AI coding agents in a shared directory is semantic invalidation and state corruption. When multiple agents attempt to modify the same hotspots, rewrite shared configuration files, or execute compilation commands simultaneously, they inevitably clobber each other's state. The symptoms observed in the Tankoban 2 project—specifically the "premature end of file" warnings from the Ninja build system and inflated rebuilds—are the direct consequence of concurrent processes attempting to write to the exact same .ninja_log and .ninja_deps files. To mitigate this, developers often invent complex polling loops and manual locking mechanisms (such as Rule 22 BUILD LANE LOCK), but these constructs are highly inefficient and prone to race conditions.

The industry consensus and the native tooling approach have universally adopted Git worktrees to resolve this collision problem permanently. A Git worktree is a native Git feature that allows multiple independent working directories to be attached to the same underlying repository database. Native support for this architecture has recently been integrated directly into the agent command-line interface via the --worktree flag. Executing claude --worktree <branch-name> automatically creates an isolated directory inside .claude/worktrees/, checks out a new branch, and launches the session specifically bound to that path. Because the files physically reside in different sectors of the disk, it is impossible for Agent 1 to overwrite a file currently being edited by Agent 2.

When the session closes, the engine automatically detects if changes were made and handles lifecycle cleanup, either discarding the worktree or prompting for a commit and merge. This native isolation completely eliminates the need for Rule 22 and Rule 19. Agents no longer need to serialize their actions via chat.md polls or rely on custom filesystem .lock flags. The disk space tradeoffs for maintaining 10-15 parallel worktrees on a machine with 232 GB of free disk space are generally negligible for source code. However, if the build artifacts or dependency folders (like node_modules or large binary assets) are substantial, they can be mitigated by aggressively symlinking those heavy static dependencies across the worktrees while keeping the C++ source code isolated.

**Specific Recommendation for Tankoban 2 Setup:** Immediately abandon the flat Git working tree architecture on the master branch for the 8-agent brotherhood. Instruct the Coordinator Agent (Agent 0) to dispatch tasks strictly by spawning sub-agents or background sessions within isolated worktrees using the claude --worktree command. Add the .claude/worktrees/ directory to the .gitignore file to prevent the main tree from tracking the isolated environments. This architectural shift physically separates the file systems, completely eradicating file clobbering, eliminating Ninja contention, and allowing Hemanth to safely review independent pull requests or branch merges at his own pace.

**Effort/Impact Estimate:** Effort: Medium. Impact: High.

### D. Sub-agent Dispatch (Agent() tool)

**Direct Answer:**
When managing parent-driven background subagent fanouts, paying the token cost for fresh-context isolation is almost always superior to inheriting massive parent context, as it prevents exponential context pollution and token runaway. Optimal model selection requires explicit alignment with task complexity, strictly utilizing the faster, cheaper models for exploration and reserving flagship models for complex implementation.

**Detailed Analysis:** Subagents serve as the architectural remedy to context degradation in long-running engineering tasks. They isolate noisy, high-iteration tasks into distinct context windows so the parent conversation remains focused and clean. A critical design principle of the native subagent architecture is that when a parent dispatches a subagent, the subagent explicitly does not inherit the parent's conversation history or intermediate tool results. It only inherits the project's CLAUDE.md and the allowed tool definitions. This fresh-context isolation is an intentional safeguard designed to prevent infinite recursive nesting and to ensure that a research task spanning dozens of files does not permanently bloat the parent's memory cache.

Model selection and task complexity are highly correlated parameters that dictate the economic efficiency of the workflow. The built-in subagents illustrate this optimal pattern clearly: the Explore subagent defaults to the fastest available model (haiku) because it is optimized for read-only codebase traversal, maximizing speed and minimizing inference cost. For custom execution, subagents defined in the ~/.claude/agents/ directory can explicitly define their model via the model: YAML parameter (e.g., model: sonnet or model: haiku) and their reasoning budget via the effort: parameter (accepting values of low, medium, high, or max).

Regarding the return of results, it is a known anti-pattern for subagents to bypass their parent and post their own standalone updates to a central coordinator file (such as the shared chat.md). This breaks the encapsulation of the subagent pattern. Instead, subagents should return structured, highly condensed summaries directly to the parent. This allows the parent agent to maintain authoritative state management, synthesize the findings from multiple parallel subagents, and prevents global state desynchronization.

**Specific Recommendation for Tankoban 2 Setup:**
For parallel paint-by-numbers fanouts, continue utilizing the isolation: "worktree" parameter to ensure file safety, but rigorously optimize the model tiers. Configure Agent 1, 2, 3, and 4 (the domain-specific workers) with explicitly defined YAML profiles in .claude/agents/. Set the model parameter to haiku for any manga/comic scraping integrations or purely read-only tasks to drastically cut costs. Reserve the flagship sonnet model for complex Qt6 C++ architectural changes and IPC named pipe implementations. Ensure that the Coordinator Agent (Agent 0) aggregates the subagent returns internally rather than relying on subagents to write directly to the global chat.md.

**Effort/Impact Estimate:** Effort: Low. Impact: High.

### E. Build System + External Tool Coordination

**Direct Answer:**
Parallel C++ build coordination across multiple LLM-driven agents must utilize strict out-of-tree build directories, as the Ninja build system is fundamentally incapable of supporting concurrent processes operating on the same build.ninja configuration and .ninja_log files. For cross-tool sub-agent commissions, relying on human couriers or unbounded wait-loops is an anti-pattern; automated external MCP bridges are required to integrate capabilities seamlessly.

**Detailed Analysis:** The Ninja build system is engineered for extreme speed, achieving sub-second incremental builds by acting more as a fast assembler than a high-level build language. To achieve this performance, Ninja assumes it holds exclusive access to the build directory. It tracks file interdependencies and build metadata within a localized .ninja_log file. When multiple autonomous agents fire build_check.bat against a shared out/ directory concurrently, they create a catastrophic race condition. The processes simultaneously attempt to lock, read, and write to the .ninja_log and .ninja_deps databases. This lock contention leads directly to the "premature end of file" corruption warnings, forcing the build system to abandon its incremental cache and trigger massive, unnecessary global rebuilds.

The industry standard to resolve this within CMake and Ninja ecosystems is to enforce strict out-of-tree builds. Each isolated Git worktree must generate its own dedicated, path-specific build directory rather than relying on a centralized output folder. An alternative approach involves utilizing the CMAKE_NINJA_OUTPUT_PATH_PREFIX variable to namespace the outputs, but physical out-of-tree generation per worktree provides superior hardware-level safety and completely eliminates the need for Rule 22 (BUILD LANE LOCK).

Regarding external tool coordination, utilizing a secondary AI CLI (such as OpenAI's Codex) via a human courier introduces extreme friction and context switching. To streamline this, a direct Model Context Protocol bridge should be established. Architectures like codex-bridge provide a local, stateless MCP server that exposes the Codex CLI capabilities directly to the parent LLM via structured JSON inputs and outputs. This completely eliminates the human courier step, allowing the primary agent to dispatch requests to the Codex engine algorithmically.

**Specific Recommendation for Tankoban 2 Setup:**
Modify the build_check.bat script to dynamically detect the current active Git worktree name (accessible via environment variables or Git commands) and instruct CMake to generate an out-of-tree build artifact path specifically for that agent (e.g., cmake -B out_%WORKTREE_NAME% -G Ninja). This provides hardware-level isolation for the compiler toolchain and immediately terminates build clobbering. Additionally, install a local Codex MCP server wrapper to allow Agent 7 to be invoked directly via an algorithmic MCP tool call, rather than relying on Hemanth to manually paste messages between interfaces.

**Effort/Impact Estimate:** Effort: Low. Impact: High.

### F. Skill and Hook Design

**Direct Answer:**
When the skill count exceeds 30+ project and plugin skills, discovery becomes an active bottleneck, and prioritization requires explicit, highly specific description tagging to ensure accurate routing. For lifecycle hook design, relying on nag-only Stop hooks is fundamentally flawed for discipline enforcement because they allow the token expenditure and system modifications to occur before correcting the behavior. Blocking PreToolUse hooks are the required standard for strict project governance.

**Detailed Analysis:** Skill prioritization and discovery at scale rely entirely on the description: frontmatter field within the skill's markdown definition. The underlying engine does not simply present a list of skills; it actively matches the user's prompt against these descriptions to route tools automatically. To handle an ecosystem exceeding 30 skills, descriptions must act as explicit, unambiguous routing rules (e.g., "Use this proactively when interacting with the ffmpeg native sidecar logic"). Vague or overlapping descriptions lead to context bloat, hallucinated arguments, and incorrect tool invocations.

The current reliance on Stop hooks for post-RTC (Real-Time Communication) discipline enforcement is highly inefficient. A Stop hook fires at the end of a turn, after the agent has finished reasoning, responding, and executing tools. By this point, the API tokens have been burned, and if the agent executed a destructive command, the damage is already done. The state-of-the-art paradigm relies heavily on PreToolUse hooks. A PreToolUse hook intercepts the tool call immediately before it reaches the execution phase.

If the hook script's internal logic determines that the requested action is a violation (e.g., attempting to build without a lock, or executing an unverified command), the script terminates and returns exit code 2. This acts as a hard execution gate: the command never runs on the host system, the model is immediately fed the error message via standard input, and the execution is blocked without wasting full generation tokens. It is critical to note, however, that a known vulnerability exists when running with the --dangerously-skip-permissions flag: PreToolUse hooks may fire asynchronously, causing the system to fail-open and execute the command before the exit code can be evaluated.

**Specific Recommendation for Tankoban 2 Setup:**
Convert the pre-rtc-checker.sh and skill-provenance-detect.sh scripts from passive Stop hooks into active PreToolUse hooks. Configure the shell scripts to evaluate the intended tool action and return exit code 2 if the action violates the Tankoban 2 governance rules. This physically blocks the agent from taking unauthorized actions (such as clobbering a build directory or committing unverified C++ code) before any actual changes are made to the local file system. Furthermore, audit the descriptions of all 30+ skills, rewriting them as explicit conditional triggers to improve automated discovery.

**Effort/Impact Estimate:** Effort: Medium. Impact: High.

### G. Cost Optimization

**Direct Answer:**
The most significant sources of token waste in autonomous multi-agent systems are unbounded wait-loops and premature cache invalidation. Wait-loops must be eradicated entirely by transitioning synchronization logic to the MCP layer. Prompt caching efficiency can be drastically improved by explicitly opting into the extended 1-hour cache Time-To-Live (TTL) setting, and by rigorously avoiding command-line flags that silently destroy the prefix cache.

**Detailed Analysis:** The current while ($true) wait-loops utilized by the Codex sub-agent represent a catastrophic vector for token waste. Every iteration of a polling loop consumes API tokens to process the massive, unchanged context window. Large Language Models should never be forced to poll state. Instead, synchronization should be handled via the aforementioned PreToolUse blocking hooks, or the agent should be commanded to suspend operations and yield execution entirely until a specific event trigger is activated by the coordinator.

Prompt caching is the primary cost-saving mechanism in modern LLM architectures. The API handles caching by matching the start of each request (the prefix) against content it recently processed. By default, the API enforces a strict 5-minute cache TTL. If an agent sits idle during a long C++ MSVC build or while waiting for a heavy ffmpeg sidecar encoding to finish, the 5-minute window easily expires. When the agent resumes activity, the next turn pays the full cost of re-ingesting the 230-line CLAUDE.md, the 5000-line chat.md, and all system prompts from scratch. For API users, this limitation can be circumvented by injecting the environment variable ENABLE_PROMPT_CACHING_1H=1, which extends the cache retention to a full 60 minutes.

Furthermore, developers must be aware of systemic bugs that affect caching behavior. Using the --resume (or --continue) command-line flag has been identified as a critical bug that entirely breaks the cache for the conversation history on the first resumed request, causing a massive one-time token spike that can rapidly deplete usage quotas. Resumptions should be handled carefully, and caching metrics should be monitored in real-time to ensure ephemeral tokens are registering correctly.

**Specific Recommendation for Tankoban 2 Setup:** Inject export ENABLE_PROMPT_CACHING_1H=1 into the Windows environment variables (or within the env block of settings.json) to extend the prompt cache lifetime of the idle brotherhood agents from 5 minutes to 60 minutes. This ensures that cross-agent handoffs and long Ninja compilation wait-times do not trigger massive context-rebuild costs. Replace all polling loops with discrete /session-recap handoffs, where an agent definitively stops execution and notes that it is awaiting an external event, rather than burning tokens in a continuous while loop.

**Effort/Impact Estimate:** Effort: Low. Impact: High.

### H. Surprises and Unasked Insights

1. **The Obsolescence of the "Brotherhood" Manual Pattern via Native Agent Teams:** The most critical surprise uncovered in the research is that the entire custom "brotherhood" architecture developed by Hemanth for Tankoban 2 (utilizing eight separate VS Code tabs, a 5000-line chat.md log, and manual lane locks) has been natively superseded by the newly released "Agent Teams" capability. Rather than manually orchestrating eight separate terminal processes, the engine now supports defining a collaborative team natively. A single command can spawn a Lead agent and multiple specialized teammates (e.g., UX, architecture, video playback). These teammates operate in parallel, each with their own isolated context window, and communicate directly with each other via a shared task list orchestrated by the Lead, drastically reducing communication overhead. This native coordination completely eliminates the need for STATUS.md and chat.md, resolving the context-window pressure organically and rendering the custom IPC brotherhood obsolete.

2. **The Danger of --dangerously-skip-permissions with Blocking Hooks:** If the Tankoban 2 environment is utilizing the bypass flag to enable fully autonomous execution without human intervention, be aware of a critical concurrency bug: when --dangerously-skip-permissions is active, PreToolUse hooks fire asynchronously. This means that if a hook attempts to block an action via exit code 2 (such as preventing a destructive rm command or a clobbered build), the tool command will execute anyway because the underlying execution runtime does not wait for the hook's synchronous return. This fail-open state renders blocking hooks effectively useless during bypass modes, presenting a significant security vulnerability.

3. **The Deprecation of the SSE Transport Terminology:** While investigating the MCP lifecycle configurations for the pywinauto server, it became apparent that the term "SSE" (Server-Sent Events) transport is being officially deprecated in the documentation in favor of the unified term "Streamable HTTP". When configuring the .mcp.json file, the explicit string required is "type": "http". Attempting to use older configuration schemas will result in silent validation failures, where the server simply fails to appear in the available tools list with no accompanying error message.

## Prioritized Recommendations Dossier

The following actionable recommendations are ranked by their calculated Impact-per-Effort ratio, specifically tuned to dismantle the friction points within the hardware-constrained Tankoban 2 environment.

1. **Isolate Ninja Builds via Dynamic Out-of-Tree Generation:** Modify build_check.bat to dynamically parse current active Git worktree name; cmake -B out_%WORKTREE_NAME%. Eliminates Rule 22 BUILD LANE LOCK need.

2. **Enable 1-Hour Prompt Caching TTL via Environment Variables:** Set ENABLE_PROMPT_CACHING_1H=1; slashes API token expenditure during idle waits.

3. **Migrate to Native Git Worktree Dispatch (--worktree):** Transition dispatch to claude --worktree command for agent-level isolation.

4. **Daemonize MCP Servers via Streamable HTTP:** Convert pywinauto-mcp + codex.cmd to long-running ASGI HTTP daemons with "type": "http".

5. **Convert Nag-Hooks to Blocking PreToolUse Hooks:** Port governance checks from Stop hooks to PreToolUse with exit code 2 enforcement.

6. **Refactor CLAUDE.md to Path-Targeted .claude/rules/:** Migrate domain-specific rules into .claude/rules/ for lazy-loading.

7. **Optimize Subagent Model Tiers in YAML Frontmatter:** Define model: parameter per-subagent (haiku/sonnet/opus).

8. **Deploy claude-mem for Progressive Memory Disclosure:** Background SQLite + FTS5 + optional Chroma for cross-session memory compression.

9. **Establish a Local Codex MCP Bridge:** Deploy codex-bridge to eliminate human-courier step for Codex commissions.

10. **Adopt Native "Agent Teams" over Manual Brotherhood:** Migrate from 8-tab manual brotherhood to Lead+teammates single-interface orchestration.

## Open Questions for Future Research

1. **Advanced CMake Cache Sharing Across Worktrees:** Safe ccache/sccache sharing across isolated worktrees without re-introducing race conditions.

2. **Hook Behavior Under Bypass Flags:** Workaround for PreToolUse fail-open bug with --dangerously-skip-permissions.

3. **Background Overhead of claude-mem on Constrained Hardware:** Profile claude-mem worker memory footprint under sustained load on 16GB RAM machine.
