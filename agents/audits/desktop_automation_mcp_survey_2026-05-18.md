# Audit - desktop automation MCP survey - 2026-05-18

By Agent 7 (Codex). For Agent 4 / Tankoban smoke infrastructure.
Reference comparison: GitHub, MCP server directories, Smithery, mcpservers.org, mcp.so, PyPI, Visual Studio Marketplace, and Anthropic docs.
Scope: Windows desktop UI automation MCP alternatives to pywinauto-mcp, with priority on active 2025+ projects and fit against Tankoban's Qt smoke pain points. Out of scope: building a new MCP, replacing pywinauto-mcp by policy, or surveying Anthropic-internal tooling.

## Ranked top 3

1. **WinApp MCP / desktop-pilot-mcp** - best fit for Tankoban smokes.
   - Install path: `npx -y winapp-mcp`, VS Code extension `BrijesharunG.winapp-mcp`, or self-contained `WinAppMCP.exe` from GitHub releases.
   - URL/package: https://github.com/floatingbrij/desktop-pilot-mcp, https://marketplace.visualstudio.com/items?itemName=BrijesharunG.winapp-mcp.
   - Architecture notes: .NET 10 + FlaUI.UIA3 + MCP 1.1.0, single stdio server, descendant/window caching, HWND-targeted snapshots/screenshots, event monitor, UIA pattern operations, index reads from `find_all_elements`.
   - Pain-point fit: directly addresses slow tree walks with claimed cache speedups (`get_snapshot` 400-800 ms to 50-100 ms; `find_elements` 300-600 ms to 20-50 ms; `click_element` 200-500 ms to 30-80 ms); avoids pywinauto's Python click signature bug; supports HWND screenshots and token-aware screenshot resizing; supports `read_element_by_index` for same-auto-id disambiguation.
   - Estimated speedup vs current pywinauto-mcp: 5-10x for repeated query/click loops if the cache is valid; 2-4x on first uncached scans. This is vendor-reported, not Tankoban-measured.

2. **sbroenne/mcp-windows** - strongest general .NET UIA-native candidate.
   - Install path: Claude Code plugin `copilot plugin install sbroenne/mcp-windows:plugin`, standalone `Sbroenne.WindowsMcp.exe`, or VS Code extension.
   - URL/package: https://github.com/sbroenne/mcp-windows.
   - Architecture notes: C#/.NET Windows UI Automation API server, semantic-first with screenshot/mouse fallback, short JSON fields, JPEG screenshots, and LLM integration tests. Latest release observed: v1.3.8 on 2026-05-13.
   - Pain-point fit: compact responses target the token-budget problem; `ui_find` supports name, regex, automationId, controlType, maxResults, and element IDs; `ui_click` has pattern handling plus coordinate fallback; `screenshot_control` is designed around window handles and compact metadata.
   - Estimated speedup vs current pywinauto-mcp: 2-5x for UIA query/action loops, mostly from .NET-native UIA and compact payloads. It is less Tankoban-specific than WinApp because no explicit descendant TTL or index-by-result workflow was documented.

3. **WinWright / civyk-winwright** - best for repeatable smoke scripts, less proven for ad-hoc exploration.
   - Install path: Claude Code plugin marketplace `civyk-official/civyk-winwright`, binary `Civyk.WinWright.Mcp.exe mcp`, or HTTP `serve --port 8765`.
   - URL/package: https://github.com/civyk-official/civyk-winwright, https://mcp.so/server/winwright/civyk-official.
   - Architecture notes: UIA3 desktop automation plus Chrome/Edge CDP and system tools; single self-contained binary; records AI-driven UI sessions to deterministic JSON scripts; includes selector healing, event watching, and JSONL audit logging.
   - Pain-point fit: replayed scripts can bypass repeated agent tree-walk reasoning entirely after a smoke is captured; desktop tools include snapshots, tree navigation, dialogs, screenshots, event watching, and action recording. It is stronger for repeated regression scripts than exploratory "what is on screen now?" debugging.
   - Estimated speedup vs current pywinauto-mcp: 1.5-3x for live ad-hoc exploration; much larger for repeated smokes if converted to `winwright run` scripts because LLM/MCP loops are removed from the hot path.

## Observed behavior from current smoke pain points

- Agent 4's pywinauto-mcp use is blocked by slow UIA tree walks, a broken `automation_elements::click` path on Qt buttons, a `SetForegroundWindow` screenshot import failure, oversized UIA dumps, recurring HITL prompts, and weak same-auto-id disambiguation.
- `pywinauto-mcp` itself documents broad tools for windows, elements, mouse, keyboard, visual/OCR, and HITL approval via `approve_automation(duration_minutes=...)`, while warning that latency depends on host/backend and old benchmark tables are obsolete: https://github.com/sandraschi/pywinauto-mcp.
- The newly shipped Tankoban `tankoctl v1.1` surface should eliminate many app-internal smoke needs; this survey is about the remaining desktop/UI layer when `dump-ui` or app-native control is not enough.

## Reference behavior and candidate details

**FlaUI-MCP by shanselman** is the cleanest minimal FlaUI reference: `windows_snapshot` returns Playwright-style refs and `windows_click` clicks by ref, with `windows_batch` for multi-action calls. It requires Windows 10/11 and .NET 8, supports Win32/WPF/WinForms/UWP, and is explicitly built on FlaUI UIA3: https://github.com/shanselman/FlaUI-MCP. It is not top-3 only because observed maturity is low (4 commits, small tool surface), but it is a useful reference implementation or fallback if WinApp proves too broad.

**CursorTouch/Windows-MCP** is active and visible in mcpservers.org / MCP directories. It runs via `uvx windows-mcp`, supports Claude Code setup, and reports typical action latency around 0.2-0.5 seconds; it uses UIAutomation plus PyAutoGUI and has screenshot scale/backend environment controls: https://github.com/CursorTouch/Windows-MCP and https://mcpservers.org/servers/CursorTouch/Windows-MCP. It remains Python-based, so it is less attractive than .NET/FlaUI for the exact pywinauto serialization overhead complaint.

**winremote-mcp** is a broad remote Windows MCP with screenshots, clicks, typing, shell, file transfer, OCR, annotated screenshots, and Win32/WMI/PowerShell integration: https://pypi.org/project/winremote-mcp/0.4.21/. Its own docs say annotated UI detection does not work with Qt/Electron/custom frameworks and suggests snapshot + vision for Qt. That makes it useful for remote administration and image fallback, but weak for Tankoban's semantic Qt smoke need.

**agent-rdp** is a Smithery skill / CLI rather than a first-class local MCP server, but it is notable for RDP-hosted Windows automation with UIA refs (`@e5`), depth-limited snapshots, selectors by AutomationId/class/name, OCR fallback, and a fast Dynamic Virtual Channel agent: https://smithery.ai/skills/thisnick/agent-rdp and https://github.com/thisnick/agent-rdp. It fits remote lab machines better than Hemanth's current shared desktop.

**Anthropic Computer Use** is not a near-term Windows Claude Code replacement. Claude Code docs say the built-in `computer-use` MCP server is a macOS research preview requiring Pro/Max and an interactive session; the API computer-use tool is beta and requires implementers to provide the execution environment and action handlers. It is broad and slow by Anthropic's own routing guidance, so it is a fallback for GUI-only tasks, not a faster Tankoban smoke surface: https://code.claude.com/docs/en/computer-use and https://platform.claude.com/docs/en/agents-and-tools/tool-use/computer-use-tool.

## Gaps ranked P0 / P1 / P2

**P0: no surveyed public MCP is proven on Tankoban's Qt hierarchy yet.** WinApp MCP is the strongest candidate on paper, but its benchmark numbers are README claims, not a Tankoban measurement. Impact: a 30-minute local bakeoff is still required before replacing pywinauto-mcp in smoke runbooks.

**P1: the fastest semantic options are young.** WinApp, WinWright, and FlaUI-MCP are 2026-era projects with small repo histories. They may solve latency and indexing while introducing stability risk.

**P1: none of the off-the-shelf tools understand Tankoban domain state.** They can click and read UIA trees, but they cannot answer "which torrent is downloading?" or "which episode is playable?" as cheaply as `tankoctl` now can.

**P2: image/vision MCPs are useful fallback, not primary for Tankoban.** Tankoban has strong AutomationId coverage and now an app-native control bridge. Vision helps custom-rendered surfaces, screenshots, and other apps, but it is slower and less deterministic for smoke assertions.

## Hypothesized root causes (Agent 4 to validate)

- **Hypothesis -** pywinauto-mcp's 1-3 second `list` path is dominated by repeated full-depth UIA descendant enumeration plus Python object serialization; WinApp MCP's descendant TTL should materially reduce this if Qt exposes stable UIA descendants. **Agent 4 to validate.**
- **Hypothesis -** the Qt button click failure is pywinauto-mcp tool/schema drift rather than a Qt accessibility limitation, because .NET UIA/FlaUI candidates expose InvokePattern/ref-click paths that avoid the failing Python `button` parameter. **Agent 4 to validate.**
- **Hypothesis -** same-auto-id TileCard ambiguity is best handled either by WinApp's indexed result tools or by app-native `dump-ui`, not by raw UIA name matching. **Agent 4 to validate.**

## Recommended follow-ups (advisory)

- Run a 20-minute bakeoff on the same Tankoban screen: `get_snapshot`, find 11 TileCards, click TileCard index 5, capture window-only screenshot, and read a specific button state. Measure wall-clock and token payloads for WinApp MCP, sbroenne/mcp-windows, and pywinauto-mcp.
- Keep `tankoctl` as the primary stream/torrent smoke interface and reserve desktop MCPs for actual layout, focus, and fallback interaction.
- If WinApp MCP passes the bakeoff, prefer it as the first non-pywinauto install because it directly targets the exact cache, HWND, index, and screenshot issues reported today.
