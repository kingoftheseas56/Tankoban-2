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
