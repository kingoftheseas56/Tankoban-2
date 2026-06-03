# Whole-Machine Agentic Observability + Actuation — prior-art capture
### 2026-06-03 all-hands (Hemanth opened the door) · seed for the arc · owner @agent5 · A0 synthesizes

**Context.** Hemanth's laptop exists for Tankoban only — nothing to protect. He opened whole-machine access and gave one directive: *"we research, brothers — what can we do with the whole computer?"* — explicitly wanting prior-art / proven reference points, not a spot-invented setup (the Office lesson). Arc = Track D grown up. This doc captures the organic cited research from the room (A1 + A3) + frames the pending authoritative pass, so nothing is lost when the bus archives.

**The split that governs everything (A4, on the record):**
- **OBSERVABILITY** = read-everything machine telemetry → unqualified yes, low risk, *build first*.
- **ACTUATION** = agent changes the machine (browser/proxy/installs/registry) → also yes, but under the same lane-discipline that stops us wiping each other's git work.

---

## Half 1 — OBSERVABILITY (the flight recorder). Researched: @agent3
- **Intel PresentMon** — open-source (`github.com/GameTechDev/PresentMon`, presentmon.com), ETW-based. Frame-pacing + present-cadence + the **HW-decode-vs-SW** ground truth. Documented use: *"integrated into continuous performance tests while the application runs"* to diagnose micro-stutter/latency. THE tool that answers A3's UHD-620 decode question cold (vs days of inference).
- **ETW / Windows Performance Recorder (WPR) / WPA / GPUView** — Microsoft-official, built into Windows + SDK. Low-overhead system tracing; what MS engineers use to debug Windows itself.
- **Sysinternals ProcDump + WinDbg/cdb** (Russinovich) — industry hang/crash capture. A0 already used ProcDump for tonight's 6.4GB dump of the spinning process.
- **Pattern (proven methodology):** continuous low-overhead ETW trace ring + **snapshot-on-anomaly** (dump-on-hang) + **read-after-the-fact** — exactly how GPU-driver teams + AAA studios diagnose a heavy app. We adopt the playbook; we don't write it.
- **Honest gap (A3):** no turnkey "agentic flight recorder for a desktop app" exists to install. The AI-agent-observability wave (Braintrust/LangSmith/Datadog) watches the *agent's reasoning*, not the *app's frames* — different thing. The instruments are all proven; the ASSEMBLY (wire them into an always-on rig an agent reads without Hemanth reproducing) is ours — but it's assembling battle-tested parts, not inventing.

## Half 2 — ACTUATION (agent drives the machine). Researched: @agent1
- **Microsoft Playwright MCP** (`github.com/microsoft/playwright-mcp`) — Microsoft-official MCP server. Drives a real browser via the accessibility tree (no vision model), persistent profile keeps logins/cookies, can attach to an existing tab. Drops straight into Claude Code. The vendor-proven actuation tool.
- **browser-use** (`github.com/browser-use/browser-use`) — 79k+ stars, MIT, 89.1% WebVoyager. The open-source LLM-drives-browser standard — **the vibe-coder community precedent Hemanth asked to see** (thousands already hand an LLM their whole browser).
- **FlareSolverr** (`github.com/FlareSolverr/FlareSolverr`) — open-source proxy that spins an undetected real Chrome to clear Cloudflare / DDoS-Guard challenges, returns HTML + cookies. The *arr/scraping-community standard — **direct answer to tonight's wall** (revives Cloudflare-locked readers like rcostation; hardens readallcomics). Limits (from its docs): cannot solve CAPTCHA; ~100-200MB + 5-15s per solve → clear-once-then-cache.
- **mitmproxy** — read the app's real outbound HTTP (host/status/latency/bytes) so a silently-breaking source shows as a red line, not a guess (A1's source read-set).
- **Reference for "agent drives the machine":** Anthropic **Computer Use** / Claude Code local-machine control (docs.claude.com) — the company that builds us ships agent-controls-computer as a product. Proof it's not fringe.

## Guardrails (A1 + A4)
- Observability is pure read → floodgates open, low risk, first.
- Actuation amplifies whatever's already there, chaos included → lane-discipline (git-style), staged after observability.
- A1's real bottleneck tonight was NOT permission (he had the full shell) — it was **missing tools** (no profiler/proxy/procdump/browser harness installed) + **app spinning while several hammer one shared tree**. So: install the standard kit + fix shared-tree discipline, THEN the rig pays off.

## Coupling (A4)
A5's recorder and A4's sources/streaming reboot (rqbit + heavy torrent I/O = a major NEW load source) must be built **aware of each other** — the recorder has to watch the new weight too, or we re-heavy the app in the dark.

## Pending
- **A0's authoritative deep-research pass** (`wf_47a14f83`) — adversarially-verified, cited, both halves + fencing. Synthesis merges it with the above into the referenced arc for Hemanth to ratify.

## Read-vs-build line (shared honesty)
Truth-faster does NOT write the fix. The UHD-620 HW-decode architecture is still ours to build. The flight recorder is the missing *diagnosis* piece that's cost us days — not the cure.

---

## AUTHORITATIVE DEEP-RESEARCH PASS — VERIFIED (wf_47a14f83, 2026-06-03)
106 agents · 24 sources fetched · 109 claims → 25 adversarially verified · **25 confirmed, 0 refuted**. Verdict: **adopt, don't invent** — exactly the proven-reference-point Hemanth asked for.

**OBSERVABILITY = rock-solid, vendor-official, do-FIRST (all 3-0 verified):**
- **ETW** is the universal low-overhead trace source under the entire stack (PresentMon, WPR, WPA, GPUView, XPerf, CapFrameX). Decades-mature. [learn.microsoft.com WPR; presentmon.com]
- **WPR memory/circular-buffer mode IS the "always-on trace ring + snapshot" mechanism** — events ride in-RAM, materialize to ETL only at `-stop`/merge. CLI-driven (`wpr -start <profile> … wpr -stop x.etl "desc"`), no GUI → directly agent-drivable. *This is literally the flight-recorder Hemanth imagined, already built into Windows.* [devblogs.microsoft.com/performance-diagnostics; learn.microsoft.com WPR cmdline]
- **Intel PresentMon (MIT, open-source)** — per-frame CPU/GPU/Display durations + latencies + dropped-frame, traced from swap-chain ETW without hooking the app → **answers the HW-vs-SW decode question cold**; per-frame CSV; ships a continuous-telemetry Service (backend) + frontends. [github.com/GameTechDev/PresentMon]
- **CapFrameX / OCAT** = proven OSS frontends on PresentMon — and **CapFrameX already ships an MCP server for AI clients** (the agent-readable angle is already a real community direction). [github.com/CXWorld/CapFrameX]
- **GPUView** — CPU-bound vs GPU-bound per frame from the ETL (16ms v-sync fit). [learn.microsoft.com profiling-directx]
- **Sysinternals ProcDump** = snapshot-on-anomaly, purpose-built: auto-dump on **hung window (5s, Task Manager's definition)**, CPU spike, unhandled exception, or **perf-counter threshold** (e.g. `handle count > 10000`). CLI. *This is the auto-fire-on-HANG_DETECTED A3 wanted — it's a built-in ProcDump trigger.* [learn.microsoft.com/sysinternals/procdump]
- **UMDH** (heap leak via snapshot-diff) + **VMMap** (scriptable memory census; handle census = Handle.exe/Process Explorer). [learn.microsoft.com]

**ACTUATION = real + has precedent, but the WINDOWS-NATIVE fencing is under-researched (report's honest weak half):**
- Only the **isolation principle survived hard verification**: Anthropic's official computer-use runs the agent in a **minimal-privilege container/VM** (their documented guardrail #1). [github.com/anthropics/anthropic-quickstarts computer-use-demo]
- Strong primary sources were FETCHED but not in the verified top-25 (budget): **browser-use** (79k★ LLM-drives-browser), **Playwright MCP**, **mitmproxy** + **mitmproxy-mcp** (HTTP capture/replay), and Anthropic's own **Claude Code sandboxing / sandbox-runtime / permissions** docs. Material exists; it just wasn't adversarially confirmed in this pass.
- **Caveat (on the record):** Anthropic's reference is Linux/X11/VNC — does NOT transfer directly to Windows 11.

**Open questions the research itself surfaced (gate items before building the actuation half):**
1. Windows-native isolation equivalent — **Windows Sandbox / Hyper-V VM / restricted token / AppContainer+Job Objects** — none citation-confirmed yet.
2. Concrete browser+proxy read-vs-write fencing (Playwright MCP capabilities, mitmproxy flows) — needs a dedicated follow-up pass.
3. **How the agent PROGRAMMATICALLY reads the traces** (vs the human GUIs) — is there a scriptable trace API (Microsoft **TraceProcessing** .NET lib, PresentMon CSV/MCP)? *This is the make-or-break for "flight recorder an agent reads without you reproducing."*
4. For our libtorrent/ffmpeg+Qt6 workload: which ETW providers + WPR profiles to combine into one ring, and the buffer/overhead tradeoffs for continuous capture.

**What this means for the arc:** OBSERVABILITY is proven, safe, and buildable now (adopt WPR-ring + PresentMon + ProcDump-on-anomaly + a trace-parsing layer). ACTUATION is real and worth doing but its Windows-native fence + browser/proxy design need one more focused research pass before we commit. That ordering (observe first, fence-then-actuate second) is exactly A4's split — and now it's evidence-backed, not assumed.
