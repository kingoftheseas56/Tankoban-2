# UIA Inspection Audit — Tankoban — 2026-04-22

**Owner:** Agent 0 (coordinator), self-executed live-smoke recon.
**Purpose:** Answer the prerequisite question from Hemanth's 2026-04-22 tool-stack recommendation list: *"does Tankoban expose enough UIA metadata for pywinauto to be a useful smoke alternative to raw-pixel windows-mcp?"*
**Scope:** Live-running Tankoban.exe on current HEAD, driven via Windows-MCP PowerShell + `Inspect.exe` (Windows SDK 10.0.26100.0). Tankoban surfaces inspected: Comics tab (default landing) + Stream tab (post-UIA-invoke on nav button).
**Tooling:** `C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/inspect.exe` (Microsoft Inspect.exe — already installed on this machine as part of Windows 11 SDK 10.0.26100.0). PowerShell `System.Windows.Automation` namespace for programmatic enumeration + pattern invocation.

---

## 1. Executive summary

**Verdict: STRONG YES on pywinauto fit.** Tankoban auto-publishes rich UIA metadata via Qt's built-in UI Automation bridge, driven incidentally by the codebase's `setObjectName()` discipline (which exists for CSS scoping per `feedback_css_scoping`, not for automation). Every widget in the Stream tab's 66-descendant UIA tree carries a structured `AutomationId` path, 39% carry human-readable `Name`, and 100% support the `InvokePattern`. End-to-end programmatic interaction has been proven live this wake: the Stream nav tab was UIA-invoked (not pixel-clicked) and Tankoban responded correctly.

**Implications:**
- pywinauto (or raw `System.Windows.Automation` PowerShell) is a viable alternative to pixel-based Windows-MCP for any widget that's addressable by objectName path, class, or Name — which is effectively the entire UI surface.
- Pixel clicks remain necessary for pure-visual verification (FrameCanvas D3D11 render contents, cinemascope letterbox margins, PGS subtitle position) and for cases where multiple widgets share an AutomationId (the 5 top-nav buttons all use `QApplication.MainWindow.QWidget.Content.TopBar.TopNav.TopNavButton` — pywinauto would disambiguate by Name or index).
- The `setAccessibleName` gap Codex flagged in the 2026-04-19 MCP smoke harness audit (`agents/audits/mcp_smoke_harness_2026-04-19.md` item #3) is real but **less load-bearing than previously assumed** — most widgets are addressable without an explicit accessible name because the objectName path is unique.

**Recommended actions:**
- **FC-1 (smoke-stack adoption):** Add pywinauto to the smoke toolkit alongside Windows-MCP. Agents pick per task — pywinauto for structural/state verification (button exists, slider value, tile count), Windows-MCP for visual verification (screenshots, letterbox math, subtitle render).
- **FC-2 (setAccessibleName low-priority):** Still worth doing for widgets where Name would help humans debugging (e.g. Inspect.exe readability), but not a blocker for pywinauto adoption.
- **FC-3 (MCP LOCK protocol — orthogonal):** Codify in `feedback_mcp_lane_lock.md` + GOVERNANCE rule. Applies regardless of tool choice (pywinauto also drives the same desktop).

**Non-recommendations:** AutoHotkey, Appium/WinAppDriver, Power Automate Desktop, Playwright MCP, SikuliX, Microsoft UFO — see closing section §9 for per-tool rationale.

---

## 2. Methodology

### 2.1 Tool setup

1. **Inspect.exe** — launched from `C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/inspect.exe`. Qt windows automatically picked up via Windows SDK UIA framework. `FrameworkId: Qt` confirmed in the Inspect.exe right-pane details for Tankoban elements, proving Qt's UIA bridge is active.
2. **PowerShell UIA** — `Add-Type -AssemblyName UIAutomationClient; [System.Windows.Automation.AutomationElement]::RootElement`. Reusable enumerator helper saved at `scripts/uia-dump.ps1` for future recon passes.

### 2.2 Launch discipline

- Direct-exec recipe per `project_windows_mcp_live.md` (not `build_and_run.bat` due to known `tankoban_tests` linker issue):
  ```powershell
  $env:Path = "C:\tools\qt6sdk\6.10.2\msvc2022_64\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;<project>\third_party\sherpa-onnx\...\lib;" + $env:Path
  $env:TANKOBAN_STREAM_TELEMETRY = "1"
  $env:TANKOBAN_ALERT_TRACE = "1"
  Start-Process "c:\Users\Suprabha\Desktop\Tankoban 2\out\Tankoban.exe" -WorkingDirectory "c:\Users\Suprabha\Desktop\Tankoban 2"
  ```
- Tankoban PID 29736, Inspect PID 20792 during this recon. Tracked for Rule 17 cleanup at audit close.

### 2.3 MCP lane discipline

This was the inaugural use of the `MCP LOCK` protocol proposed earlier this session — Agent 0 claimed the MCP desktop-interaction lane via `agents/chat.md` line before launching Tankoban + Inspect. Released at audit commit.

---

## 3. Tankoban top-level window (Comics tab default landing)

```
[Window] Name='Tankoban' AutoId='QApplication.MainWindow' Class='MainWindow' FrameworkId='Qt'
  [Group] AutoId='QApplication.MainWindow.QWidget' Class='QWidget'
    [Group] AutoId='QApplication.MainWindow.QWidget.GlassBackground' Class='GlassBackground'
    [Custom] AutoId='QApplication.MainWindow.QWidget.Content' Class='QFrame'
      [Custom] AutoId='QApplication.MainWindow.QWidget.Content.TopBar' Class='QFrame'
        [Text] Name='Tankoban' AutoId='...TopBar.Brand' Class='QLabel'
        [Custom] AutoId='...TopBar.TopNav' Class='QFrame'
          [CheckBox] Name='Comics' AutoId='...TopNav.TopNavButton' Class='QPushButton'
          [CheckBox] Name='Books' AutoId='...TopNav.TopNavButton' Class='QPushButton'
          [CheckBox] Name='Videos' AutoId='...TopNav.TopNavButton' Class='QPushButton'
          [CheckBox] Name='Stream' AutoId='...TopNav.TopNavButton' Class='QPushButton'
          [CheckBox] Name='Sources' AutoId='...TopNav.TopNavButton' Class='QPushButton'
        [Button] Name='↻' AutoId='...TopBar.IconButton' Class='QPushButton'
        [Button] Name='+' AutoId='...TopBar.IconButton' Class='QPushButton'
      [Pane] AutoId='...Content.QStackedWidget' Class='QStackedWidget'
        [Group] AutoId='...QStackedWidget.comics' Class='ComicsPage'
          [Pane] AutoId='...comics.FadingStackedWidget' Class='FadingStackedWidget'
            [Group] AutoId='...comics.FadingStackedWidget.ComicsGridScroll' Class='QScrollArea'
```

### Key observations

- `FrameworkId='Qt'` on the top-level window — Qt's UIA bridge is automatically active. No explicit accessibility framework adoption needed in Tankoban's code.
- `AutomationId` paths are dot-separated `objectName()` chains. `QApplication.MainWindow.QWidget.Content.TopBar.Brand` directly mirrors the widget tree with setObjectName calls that exist for CSS targeting.
- Top-nav buttons have shared `AutomationId` (`TopNavButton`) but unique `Name` (`Comics` / `Books` / ...). Disambiguation is trivial via Name.
- `ControlType` is semantically accurate: `Window`, `CheckBox` for checkable QPushButton, `Button` for non-checkable QPushButton, `Pane` for QStackedWidget, `Text` for QLabel, `Group` for QFrame/QWidget, `Custom` for widgets with custom class names.

### Addressability verdict per widget

Every widget inspected was addressable via one or more of:
1. Unique `AutomationId` (e.g. `...Content.TopBar.Brand` for the brand label).
2. Shared `AutomationId` + unique `Name` (e.g. the 5 nav buttons).
3. Unique `ClassName` (e.g. `ComicsPage`, `StreamPage`, `StreamHomeBoard`, `TileCard`).
4. Combination of parent + ControlType + index (for unnamed grouping widgets).

Any of these four strategies works with pywinauto's selector API.

---

## 4. Stream page post-UIA-invoke

### Invoke test (the critical proof point)

Code executed live:
```powershell
$streamBtn = $tankoban.FindFirst([TreeScope]::Descendants,
    (New-Object PropertyCondition([AutomationElement]::NameProperty, "Stream")))
$streamBtn.GetCurrentPattern([InvokePattern]::Pattern).Invoke()
```

Result: `"Invoked via InvokePattern"`. Tankoban switched to Stream tab within ~100ms. No pixel click, no keyboard event, pure UIA programmatic interaction.

This is the load-bearing proof. pywinauto (which wraps the same UIA framework PowerShell used here) can programmatically drive Tankoban without vision-based automation.

### Stream page tree

```
[Group] AutoId='...QStackedWidget.stream' Class='StreamPage'
  [Pane] AutoId='...stream.QStackedWidget' Class='QStackedWidget'
    [Group] AutoId='...stream.QStackedWidget.QWidget' Class='QWidget'
      [Group] AutoId='...QScrollArea' Class='QScrollArea'
        [Group] AutoId='...qt_scrollarea_viewport' Class='QWidget'
          [Group] AutoId='...QWidget' Class='QWidget'
            [Custom] AutoId='...streamSearchBar' Class='QFrame'
            [Group] AutoId='...StreamHomeBoard' Class='tankostream::stream::StreamHomeBoard'
            [Group] AutoId='...StreamLibraryLayout' Class='StreamLibraryLayout'
        [Group] AutoId='...qt_scrollarea_vcontainer' Class='QWidget'
          [ScrollBar] AutoId='...QScrollBar' Class='QScrollBar'
```

### Observations

- Custom Qt widgets publish their full C++ class name: `tankostream::stream::StreamHomeBoard`, `StreamLibraryLayout`, `StreamPage`, `StreamContinueStrip`, `TileCard`, `TileStrip`. pywinauto `class_name=` selectors can target them directly.
- `qt_scrollarea_viewport` + `qt_scrollarea_vcontainer` — Qt internal widget names also exposed. Scroll bar targetable for programmatic scrolling.
- `streamSearchBar` — widget objectName matches the CSS selector convention (`#streamSearchBar` would also target it via QSS).

---

## 5. UIA coverage stats (Stream tab active)

Measured via `FindAll(TreeScope.Descendants)` walk:

| Metric | Count | Percentage |
|---|---|---|
| Total descendants | 66 | 100% |
| Non-empty `AutomationId` | 66 | **100%** |
| Non-empty `Name` | 26 | 39% |
| Both | 26 | 39% |

Every single descendant under Tankoban's window has an addressable `AutomationId`. The 39% Name coverage is concentrated on user-visible text widgets (QLabel text content, QPushButton captions, tile titles) — widgets where Name is meaningful. Structural containers (QFrame, QWidget, QScrollArea) legitimately have empty Name because they're not user-visible as named elements.

### Top classes (Stream tab)

| Class | Count |
|---|---|
| QLabel | 20 |
| QPushButton | 11 |
| QFrame | 9 |
| QWidget | 6 |
| TileCard | 5 |
| QStackedWidget | 2 |
| TileStrip | 2 |
| StreamLibraryLayout | 1 |
| StreamContinueStrip | 1 |
| StreamHomeBoard | 1 |
| StreamPage | 1 |
| QScrollArea, QScrollBar, QGroupBox, QLineEdit, QComboBox, QSlider, GlassBackground | 1 each |

Every custom Tankoban widget (Glass, TileCard, TileStrip, Stream*) exposes its C++ class name directly to UIA. No opacity — pywinauto can target any by `class_name=`.

---

## 6. Pattern support (Stream tab)

UIA defines a pattern set for interacting with widgets. Tankoban supports:

| Pattern | Count | Implication |
|---|---|---|
| **Invoke** | 66 | Every descendant supports `.Invoke()` (equivalent to click). pywinauto `.invoke()` or `.click_input()` both work. |
| **Toggle** | 5 | The 5 top-nav buttons (Comics/Books/Videos/Stream/Sources — they're checkable QPushButtons). pywinauto `.toggle()` / `.get_toggle_state()` available. |
| **Value** | 51 | 51 widgets support get/set Value. Covers QLabel text content, QLineEdit contents, QComboBox selection, tile metadata. Read what's on-screen programmatically. |
| **Scroll** | 0 | Qt QScrollArea does NOT implement UIA Scroll pattern. Workaround: find QScrollBar descendant + use its ValuePattern to set scroll position. Or use keyboard arrows via pywinauto's `type_keys()`. Pattern coverage gap (FC-4). |
| **Selection** | 0 | No explicit selection containers in Stream tab. TileCard selection uses visual highlight only. Not blocking; selection state queryable via Value or through custom Qt signals. |

### Gap analysis

Only one real gap: **Scroll pattern is unimplemented.** This is a Qt framework-level issue (QScrollArea doesn't bridge Scroll pattern to UIA), not a Tankoban-specific one. Workarounds:
- Walk to the QScrollBar child, read/set its ValuePattern (proven to work via PowerShell UIA).
- Use `type_keys()` with Page Up/Down/Home/End or mouse wheel.
- Neither is a blocker — just slightly more verbose than a true ScrollPattern.

Everything else (Invoke, Toggle, Value) is well-supported. The bulk of smoke-test scenarios (click button, read label, type in search, toggle tab) works out of the box.

---

## 7. Inspect.exe visual confirmation

Screenshot captured at `agents/audits/_uia_inspection_work/` shows Inspect.exe's right-pane details for the Stream button after UIA invocation:

```
How found:        Focus
Name:             Stream...
ControlType:      UIA_CheckBoxControlTypeId
LocalizedControlType: "check box"
BoundingRectangle: {l:1009 t:...}
IsEnabled:         true
IsOffscreen:       false
IsKeyboardFocusable: true
HasKeyboardFocus:  true
ProcessId:         29736    ← Tankoban's PID
AutomationId:      QAppl... (Qt path)
FrameworkId:       Qt
```

`FrameworkId: Qt` is the key field. Qt's UIA bridge is live. Nothing in Tankoban's code needs to change for pywinauto to work.

Full screenshots preserved in `_uia_inspection_work/` (comics-landing.png, stream-tab.png).

---

## 8. Fix candidates (ratification ask)

### FC-1 (RECOMMENDED): Adopt pywinauto as a complementary smoke path

~0 dev effort on Tankoban's side (it's already automation-friendly). Documentation + onboarding work only.

- Install pywinauto via `pip install pywinauto` (or vendor it into a smoke-tooling dir).
- Write a ~30-line smoke helper module at `scripts/smoke_uia.py` with wrappers: `find_button(name)`, `find_tile_at_index(i)`, `click_tab(name)`, `wait_for_window("StreamPage")`.
- New memory `feedback_pywinauto_when.md` — rules of thumb for when to pick pywinauto vs Windows-MCP.
- Agents pick per task: structural/state verification → pywinauto (fast, programmatic, no pixel parsing). Visual verification → Windows-MCP (screenshot-first).

### FC-2 (OPTIONAL): setAccessibleName on the ~60% of unnamed widgets

Small polish win for human-debugging readability in Inspect.exe but not required for automation. Widgets where it'd help:
- The 11 QPushButton widgets that rely on shared AutomationId + Name disambiguation — already fine, Name is sufficient.
- Structural QFrame / QWidget containers that appear as `[Group] Name=''` in Inspect trees — cosmetic only.

Ship if/when a widget's lack of Name actively causes a smoke flake. Don't do a sweep-rewrite.

### FC-3 (REQUIRED regardless of tool choice): MCP LOCK protocol codification

Per the proposal posted in chat.md this session. Multiple agents share one desktop regardless of whether they use pywinauto or Windows-MCP. Lock applies to any desktop-interacting tool.

- Memory: `feedback_mcp_lane_lock.md`.
- GOVERNANCE rule: "MCP LOCK — claim the desktop lane in chat.md before MCP interaction; release at smoke end or 15-20 min stale timeout."
- Worked reference: this audit is the inaugural use — chat.md line claims the lane; this commit releases it.

### FC-4 (DEFERRED): QScrollArea → Scroll pattern bridge

Qt framework-level gap, not Tankoban-specific. Upstream Qt issue if anything. Workarounds (QScrollBar ValuePattern / keyboard types) are sufficient. Don't invest.

### NOT-RECOMMENDED (per tool-stack evaluation)

- **AutoHotkey** — our smokes are diverse per-task, not "same sequence every time." Revisit only if a specific Windows-MCP step keeps flaking.
- **Appium + WinAppDriver** — Microsoft half-abandoned WinAppDriver (~2020 last release), and we have no CI to feed a regression suite.
- **Power Automate Desktop** — recorder-based flows don't version-control; fights git-native brotherhood discipline.
- **Playwright MCP** — native Qt, not web. Codex's existing web tools cover audit-of-reference-apps.
- **SikuliX** — redundant with Windows-MCP's screenshot + click-at-coordinate.
- **Microsoft UFO** — research-grade agent stack; we already have Claude + Codex orchestration.

---

## 9. Deferred measurements

1. **Video player surface** — not exercised this wake. Playing a stream takes minutes and a live network; deferred to a future pywinauto-first smoke when the seek slider / track popover surfaces are on-test. Expected coverage: identical — QSlider / VideoPlayer / FrameCanvas all carry objectName(), so UIA should expose them the same way Stream page widgets were exposed.
2. **Comic / Book reader surfaces** — not exercised. Same expectation.
3. **Tankorent / Tankoyomi search surfaces** — not exercised. Agent 4B domain; Tankorent's indexer result grid and Tankoyomi's manga grid would likely have similar TileCard/TileStrip structure.
4. **Specific edge cases** — custom-painted widgets (FrameCanvas D3D11 content, LoadingOverlay paint, cinemascope letterbox bars) — pixel output is by nature not UIA-queryable, always needs Windows-MCP screenshot + pixel-scan.

A follow-up pywinauto adoption wake could walk these surfaces and extend this audit, but the conclusion is unlikely to change: Qt's UIA bridge is on by default, and every Tankoban widget with an objectName (effectively all of them) is addressable.

---

## 10. Supporting artifacts

- `scripts/uia-dump.ps1` — reusable PowerShell UIA enumerator (tracked).
- `agents/audits/_uia_inspection_work/*.png` — Inspect.exe screenshots, **local-only** (off-git per `_*/` gitignore rule — matches the brotherhood pattern for audit working dirs; regenerate by re-running the launch recipe in §2.2 + capturing via PowerShell `System.Drawing.Bitmap::CopyFromScreen`).
- `C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/inspect.exe` — Windows SDK Inspect.exe, already installed, available to any future agent doing interactive UIA verification.

---

## 11. Recommended next actions (Agent 0 to execute)

1. Ship this audit + FC-3 (MCP LOCK codification) as this wake's deliverable.
2. Author `feedback_mcp_lane_lock.md` memory + `feedback_pywinauto_when.md` memory.
3. Add a new GOVERNANCE Rule 19 (MCP LOCK) alongside the 18 existing rules.
4. **Defer** pywinauto actual adoption — Hemanth's call on whether he wants me to stand up `scripts/smoke_uia.py` as a follow-on wake, or wait for a specific smoke task to prove the setup with.

Rule 17 cleanup executed at audit-commit close.

---

**Audit closed. MCP LOCK released at commit.**
