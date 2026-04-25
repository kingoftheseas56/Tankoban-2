# Theme System TODO — Tankoban 2

**Authored 2026-04-25 by Agent 0** from audit `agents/audits/qt_theme_feasibility_2026-04-25.md` (PATH B verdict, ratified by Hemanth same-day with 6 RESOLVED ANSWERS). **Owner: Agent 5.** Awaiting Hemanth ratification of this body via `ratified` / `APPROVES` / `Final Word` / `Execute` to kick off P1.

---

## Context

Take Tankoban 2 from "boring minimalist" to "vibrant themes that can be toggled off and on" (Hemanth, 2026-04-25). Port Tankoban-Max's two-axis theme system to Qt6 + QSS + QGraphicsEffect + QPropertyAnimation:

- **Axis A — mode:** dark ↔ light toggle (sun/moon icon button)
- **Axis B — palette:** 7 vibrant color swatches (paint-palette icon button → swatch popover)

App-wide preset (pick "Ember" once, every section uses Ember). Win11 Mica top-level glass + tile hover-lift + tile drop-shadow as additive polish. Win-only Mica via `#ifdef Q_OS_WIN`; macOS/Linux fall back to solid background.

Cost: ~4-6 summons, ~400-700 LOC. No new Qt module dependency.

---

## Objective

A user can toggle dark↔light and pick from 7 vibrant palette swatches via two icon buttons in the top-right topbar. Selection persists across app restart. Win11 Mica gives the chrome a frosted-glass feel behind everything. Tile cards lift + drop-shadow on hover at 60fps. Dropped down from Tankoban-Max's 6 named theme aliases (nord/solarized/gruvbox/catppuccin) to just `dark` + `light` + the 7 swatches because the 4 aliases visually aliased the dark layer without distinct rendering.

---

## Non-Goals (explicit)

- **F-bucket compositing effects** — animated radial-gradient backgrounds, per-element backdrop-filter, mix-blend-mode film grain, multi-layer compound box-shadow stacks beyond a single `QGraphicsDropShadowEffect`. **Not achievable in Qt Widgets.** Win11 Mica (top-level only) is partial substitute for atmospheric depth.
- **Per-mode theme override.** App-wide preset only. Mode identity (if wanted later) via section-title color tinting, NOT per-mode override.
- **Tankoban-Max's 4 dark-layer aliases** (nord/solarized/gruvbox/catppuccin) — dropped. Axis A is `dark` + `light` only. The 7 swatches deliver color variety.
- **macOS/Linux Mica equivalent.** Win-only via `#ifdef Q_OS_WIN`. macOS/Linux MainWindow shows solid `Theme::kBg`. Future macOS port lands `applyMacVibrancy()` separately (out of scope).
- **`feedback_qt_vs_electron_aesthetic.md` memory revision body.** That lands as P4 close-out (in-place narrowing); not a TODO-authoring task.
- **Generic `::before`/`::after` pseudo-elements in QSS.** Infeasible. Replace with real child QWidgets if a specific surface needs it.

---

## Reference slate

- **Source audit:** `agents/audits/qt_theme_feasibility_2026-04-25.md` (§5.2 phase plan, RESOLVED ANSWERS, §3.4 risk surface)
- **Tankoban-Max source:** `C:\Users\Suprabha\Downloads\Tankoban-Max-butterfly\Tankoban-Max-butterfly\` — 7 swatch palettes, `theme-light.css` 55 effect-overrides, tile hover-lift CSS
- **Memory baseline:** `feedback_qt_vs_electron_aesthetic.md` (revised in-place at P4 close-out)
- **TODO authoring template:** `feedback_fix_todo_authoring_shape.md`
- **Existing Tankoban 2 surfaces:** `src/ui/Theme.h`, `src/ui/TankobanFont.h`, `src/main.cpp` (`noirStylesheet()`), `src/ui/MainWindow.cpp`, `src/ui/pages/TileCard.cpp`

---

## Phases

### P1 — Theme infrastructure (1 summon, Agent 5)

**Scope:**
- Extend `src/ui/Theme.h` with `ThemePalette` struct (color tokens) + `ThemePreset` registry (7 swatches) + `applyTheme(mode, preset)` function.
- Templatize `noirStylesheet()` in `src/main.cpp` to consume `Theme::current()` tokens instead of hardcoded literal colors.
- `QSettings` persistence with **two separate keys** (`theme/mode` and `theme/preset`) — fixes Tankoban-Max's shared-key boot-reset bug where both axes stored to `localStorage.appTheme` and the second write clobbered the first.
- Hardcode initial palette to current Noir at compile time so behavior is unchanged this phase.

**Files:** `src/ui/Theme.h`, `src/main.cpp`. No UI yet.

**Acceptance:**
- App launches identically to today (no visual diff).
- `build_check.bat` BUILD OK.
- QSettings keys `theme/mode` + `theme/preset` exist (can be inspected via `regedit` under HKCU/Software/Tankoban) but unset until P2 writes them.

### P2 — Settings UI + swatch picker (1 summon, Agent 5)

**Scope:**
- New widget `src/ui/widgets/ThemePicker.{h,cpp}` — top-right topbar cluster placement, alongside refresh / add-files / window-controls.
- Two icon buttons:
  - Sun/moon (axis A) — click cycles dark ↔ light directly.
  - Paint-palette (axis B) — click opens popover with 7-swatch grid.
- Wire both to `applyTheme()` from P1; updates `QSettings` immediately.

**Files:** `src/ui/MainWindow.cpp` + new `src/ui/widgets/ThemePicker.{h,cpp}`.

**Acceptance:**
- Click each swatch → chrome retints across all pages (Comics, Books, Videos, Stream, Sources).
- Click sun/moon → toggles dark ↔ light visually.
- Restart app → previous selection persists (separate-key fix verified).
- `build_check.bat` BUILD OK.

### P3 — Light mode override layer (1-2 summons, Agent 5)

**Scope:**
- Port Tankoban-Max's `theme-light.css` 55 effect-overrides as a separate templated stylesheet section that activates when `mode == light`. Chip backgrounds, text colors, border alphas, hover states, scrollbar colors, popover bg, etc.
- Light-mode-specific palette token branches in `Theme.h` (per-token light/dark variants).

**Files:** `src/ui/Theme.h`, `src/main.cpp` — OR split into a new `src/ui/StylesheetTemplate.{h,cpp}` if `noirStylesheet()` becomes too long. Agent 5's call.

**Acceptance:**
- Toggle light/dark — all 55 overrides applied side-by-side. No "dark text on white background" or "white text on white background" visual bugs.
- All 5 pages render correctly in both modes.
- 7 swatches × 2 modes = 14 combinations all visually coherent.

**Note:** P3 may take 2 summons if the 55-override port is heavier than estimated. Budget allows it (4-6 total summons across phases).

### P4 — Win11 Mica + tile shadow polish (1 summon, Agent 5)

**Scope:**
- `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE, &DWMSBT_MAINWINDOW, sizeof)` on MainWindow under `#ifdef Q_OS_WIN`. Falls back to solid `Theme::kBg` on macOS/Linux.
- `QGraphicsDropShadowEffect` on TileCard hover state.
- Hover-scale animation via `QPropertyAnimation` (Tankoban-Max-butterfly groundwork already had this — simple port).
- **TODO close-out task:** narrow `feedback_qt_vs_electron_aesthetic.md` memory in-place — clarify "only QML closes the gap" applies to F-bucket compositing only (backdrop-filter, mix-blend-mode, ::before/::after, multi-layer compound shadows). C/D/M buckets achievable in Qt6.

**Files:** `src/main.cpp`, `src/ui/pages/TileCard.cpp`, memory file at TODO close.

**Acceptance:**
- Win11 Mica visible behind window chrome on Win11; macOS/Linux fall back to solid `Theme::kBg` cleanly.
- Tile cards lift + drop-shadow on hover at 60fps.
- No shadow clipping on edge tiles (or accepted as the documented `QGraphicsEffect` widget-bounds-clip trade-off).
- Memory file `feedback_qt_vs_electron_aesthetic.md` updated in-place with "narrowed 2026-04-25" clarification.
- `build_check.bat` BUILD OK.

---

## Decisions (the 6 RESOLVED ANSWERS, locked Hemanth 2026-04-25 ~16:30)

1. **Picker placement:** top-right topbar cluster, two icon buttons (sun/moon for axis A, paint-palette for axis B). Sit alongside refresh/add-files/window-controls. Click axis-B icon → 7-swatch grid popover. Click axis-A icon → cycle dark ↔ light directly.
2. **Per-mode default:** app-wide. Pick "Ember" once → every mode (Comics/Books/Videos/Stream/Sources) uses Ember. Mode identity (if wanted later) via section-title color tinting, not per-mode theme override.
3. **Light mode coverage:** full override layer. P3 ports Tankoban-Max's `theme-light.css` 55 effect-overrides faithfully.
4. **Dead theme aliases:** dropped. Axis A is `dark` + `light` only. The 4 Tankoban-Max names (nord/solarized/gruvbox/catppuccin) that aliased the dark layer without distinct visuals are not exposed in Tankoban 2.
5. **Memory revision:** revise `feedback_qt_vs_electron_aesthetic.md` in-place with "narrowed 2026-04-25" clarification at P4 close-out. F-bucket compositing claim preserved; C/D/M-bucket guidance softened.
6. **Mica multi-OS:** Win-only `#ifdef Q_OS_WIN`. macOS/Linux MainWindow shows solid `Theme::kBg`. Future macOS port lands `applyMacVibrancy()` separately.

---

## Risk surface

1. **F-bucket ceiling acknowledged.** Animated radial backgrounds + per-element backdrop-blur + mix-blend-mode + multi-layer compound shadows NOT achievable in Qt Widgets. Win11 Mica (top-level only) is partial substitute for atmospheric depth. Memory clarification at P4 close-out preserves the original "don't propose QML-impossible ports" instinct **for F-bucket only.**
2. **Tankoban-Max shared-key bug** (both axes wrote to same `localStorage.appTheme`, boot reset axis A). **Fixed in P1** via two separate QSettings keys.
3. **`QGraphicsEffect` widget-bounds clipping.** Single-layer drop-shadow via `QGraphicsDropShadowEffect` clips at widget bounds (unlike CSS `box-shadow` which composites with siblings). Accepted trade-off — only visible on edge-adjacent elements.
4. **`QPropertyAnimation` boilerplate.** Hover/scale animations require one animation object per property/widget (heavier than CSS `transition: ...`), but achieves 60fps parity once wired.
5. **Light-mode override port effort.** 55 effect-overrides is more porting work than initial single-summon estimate. P3 budget allows 2 summons.
6. **Memory re-scoping needed at close-out.** `feedback_qt_vs_electron_aesthetic.md` over-generalizes "only QML closes the gap" — narrows to F-bucket only. Lands as P4 close-out task.

---

## Smoke criteria per phase

- **P1 self-smoke (Agent 5):** app launches identically to pre-P1; `build_check.bat` BUILD OK; QSettings keys present but unset.
- **P2 Hemanth-smoke:** click each of 7 swatches → chrome retints; click sun/moon → toggles; restart app → selection persists.
- **P3 Hemanth-smoke:** toggle dark/light → all 55 effect-overrides visible (chip bg, text colors, hover states, border alphas); all 5 pages render correctly in both modes.
- **P4 Hemanth-smoke:** Win11 Mica visible behind chrome; tile lift + drop-shadow at 60fps; no edge-tile shadow regression.

---

## Exit criteria

- All 4 phases shipped + smoked GREEN
- Memory `feedback_qt_vs_electron_aesthetic.md` narrowed in-place to F-bucket scope
- All 6 RESOLVED ANSWERS honored in source code (separate QSettings keys, app-wide preset, full light-mode override, dark+light only, Win-only Mica)
- Agent 5 STATUS.md section closes; CLAUDE.md dashboard refresh
- `build_check.bat` + main app launch + each-mode visual verification all green

---

## Sign-off

Authored 2026-04-25 by Agent 0. Awaiting Hemanth ratification of this body. Agent 5 P1 kickoff on ratification phrase (`ratified` / `APPROVES` / `Final Word` / `Execute`).
