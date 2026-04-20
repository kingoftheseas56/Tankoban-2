# Audit - MCP smoke harness - 2026-04-19

By Agent 7 (Codex). For Agent 0 / Hemanth. Reference comparison: windows-mcp, Claude computer use, Qt accessibility, Playwright MCP, Appium Windows Driver, FlaUI.

## Executive Summary

Stay on windows-mcp. Ship two cheap improvements: a `runtime-health.ps1` smoke digest and stable Tankoban launch/deep-state flags. Add Qt accessibility names for smoke-critical widgets when touched. Defer a custom MCP server until smoke cadence stays high after STREAM_STALL_FIX closure. Do not migrate to Playwright MCP. Use Claude computer use only as fallback for purely visual validation. The known seek-slider precision pain is better solved by app-owned hooks than by changing MCPs.

## Q1 - Comprehension

`windows-mcp` is registered through `uvx windows-mcp` with telemetry disabled (`.mcp.json:3-8`); timeout is intentionally 60s (`.claude/settings.json:3-5`). Its docs describe native Windows UI interaction and UI-state capture, with vision optional, not required ([PyPI](https://pypi.org/project/windows-mcp/), lines 117-123). Source confirms `Snapshot` returns desktop metadata, focused/open windows, interactive elements, scrollable elements, optional screenshot, and virtual-desktop coordinates; `Screenshot` skips UI tree extraction for speed ([snapshot.py](https://raw.githubusercontent.com/CursorTouch/Windows-MCP/main/src/windows_mcp/tools/snapshot.py), lines 1-7; [_snapshot_helpers.py](https://raw.githubusercontent.com/CursorTouch/Windows-MCP/main/src/windows_mcp/tools/_snapshot_helpers.py), lines 1-3).

So Claude literally sees per-call text from the accessibility tree plus optional PNG, not a persistent app model. Gemini's "add screenshots" is satisfied; its implied "Snapshot is a screenshot" framing is wrong. Reachable comprehension wins without forking: a PowerShell runtime digest modeled after `repo-health.ps1` (`scripts/repo-health.ps1:62-97`), semantic CLI launch targets, and better Qt accessibility metadata. Current code has many `setObjectName` anchors (`src/ui/pages/StreamPage.cpp:204-209`, `src/ui/pages/StreamPage.cpp:366-408`) but no `setAccessibleName` hits in `src/`, so UIA names likely fall back to text/role. Qt explicitly supports custom widget metadata through `QAccessibleInterface`, including value/action interfaces for sliders ([Qt QAccessible](https://doc.qt.io/qt-6/qaccessible.html), lines 74-77, 199-201).

## Q2 - Speed

Latency has four buckets: LLM planning/tool-choice; MCP execution; app settle; and log/PowerShell I/O. windows-mcp claims typical action latency of 0.2-0.9s ([GitHub README](https://github.com/CursorTouch/Windows-MCP), lines 322-323), and exposes `WINDOWS_MCP_PROFILE_SNAPSHOT` to profile Snapshot/Screenshot stages ([GitHub README](https://github.com/CursorTouch/Windows-MCP), lines 643-646). For Tankoban, app settle dominates many smokes: launch runs build, sets telemetry env vars, and starts the exe (`build_and_run.bat:45-88`); stream cold-open can take tens of seconds per the chat evidence. Hypothesis - exact split should be measured by enabling `WINDOWS_MCP_PROFILE_SNAPSHOT=1` plus timestamping smoke scripts; Agent 0 to validate.

Fastest reachable fixes: avoid UI hops with CLI/deep-state flags; collapse log greps into one runtime-health command; prefer `Screenshot` over `Snapshot` when only visual confirmation is needed; reduce repeated global Snapshot calls after every click.

## Q3 - Best Use In Our Stack

Native desktop changes the answer: web DOM tools do not see Qt widgets, D3D11 pixels, or sidecar state. Multi-agent sharing means one Tankoban window must remain serialized; speedups should reduce per-smoke turns, not encourage parallel GUI smokes. The strongest pattern is hybrid: MCP for launch/click/screenshot, PowerShell for logs, and app-owned debug surfaces for state. The seek slider proves the limit: it maps raw `x` to fraction in `SeekSlider::fractionForX` and click-to-seek uses local mouse coordinates (`src/ui/player/SeekSlider.cpp:162-179`), so coordinate clicks are inherently imprecise for narrow in-cache tests. Expose deterministic state/actions rather than demanding better human-like clicks.

## Q4 - MCP Choice

Verdict: hybrid, windows-mcp primary. Claude computer use is useful when UIA misses custom-painted pixels; it is broader but slowest per Claude Code docs, and runs on the actual desktop with permission tiers ([Claude Code Desktop](https://code.claude.com/docs/en/desktop), lines 303-321, 352-356). Playwright MCP is browser-focused; its own README says web pages, structured browser accessibility snapshots, and no screenshots/vision models ([Playwright MCP](https://github.com/microsoft/playwright-mcp), lines 259-272). Appium/WinAppDriver and FlaUI are credible Windows UIA stacks, but Appium's Windows path depends on WinAppDriver and warns that server is unmaintained ([Appium Windows Driver](https://github.com/appium/appium-windows-driver), lines 270-277); FlaUI is a good C# UIA library, not an MCP replacement out of the box ([FlaUI](https://github.com/FlaUI/FlaUI), lines 303-318).

## Ranked Tactic Table

| Tactic | Owner-agent | LOC-estimate | Expected delta | Risk | Dependencies |
|---|---:|---:|---|---|---|
| `scripts/runtime-health.ps1`: process, window, recent telemetry, sidecar, last errors | Agent 0 | 60-120 | High comprehension, medium speed | Low | Existing log paths |
| Tankoban CLI flags: open mode/media/source, stream test target | Agent 0 + domain owner | 80-180 | High speed | Medium | MainWindow routing |
| Add `setAccessibleName` / accessible value for smoke-critical widgets | Domain owners | 5-30 per widget | Medium comprehension | Low | Touch-when-owned |
| Enable `WINDOWS_MCP_PROFILE_SNAPSHOT` during one A/B smoke | Agent 0 | 0-5 | Measures bottleneck | Low | MCP env |
| Custom Qt MCP/QTest hook for deterministic actions | Agent 0 + domain owner | 400-800 | High precision | Medium-high | Debug build guard |

## Alternative Comparison Table

| MCP option | Covers our smoke pattern | Latency vs windows-mcp | Qt custom-widget handling | Migration cost | Risk |
|---|---|---|---|---:|---|
| windows-mcp | Yes | Baseline | UIA good, custom paint weak | 0 | Current limits known |
| Claude computer use | Partial fallback | Slower | Vision can see pixels, less structured | 0-1 wakes | Plan/access gating |
| Custom Qt-integrated MCP | Yes | Faster for scripted actions | Best; QTest/QAccessible | 400-800 LOC, 2-4 wakes | Maintenance |
| Playwright MCP | No for native Qt | N/A | Browser only | 1 wake to prove no | Wrong domain |
| Appium/FlaUI-derived MCP | Yes, if built | Similar | UIA-based; mature selectors | 300-700 LOC | Extra stack |

## Mandatory Dissent

Gemini dissent: global health after every tool call is too broad; run health at smoke boundaries or on failure. Macro tools are premature until post-stall smoke volume proves sustained.

Agent 0 dissent: I would not prioritize Tankoban CLI args above runtime-health. The last week's pain was interpretation and evidence stitching more than click count; a digest makes every future smoke cheaper even when no deep-link exists. I also would cost custom MCP lower if limited to one QTest-backed debug bridge for seek/playback, not a general windows-mcp wrapper.

Parked: forking windows-mcp, tool-description rewrites, response-shape minification, and wholesale MCP migration.
