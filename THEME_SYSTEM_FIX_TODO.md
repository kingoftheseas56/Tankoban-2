# Theme System TODO — Tankoban 2

**Authored 2026-04-25 by Agent 0** from audit `agents/audits/qt_theme_feasibility_2026-04-25.md` (PATH B verdict, ratified by Hemanth same-day with 6 RESOLVED ANSWERS). **Owner: Agent 5.**

**Spec correction 2026-04-25 (post-Phase-2 smoke):** Hemanth verbatim "before we begin phase 3, remove the colour pallete. we should get the themes right. i've never asked for colour palletes and they just complicate things because now we have 42 looks (6 themes into 7 colours)". The original PATH B audit recommended a two-axis system (6 Modes × 7 Presets). Phase 1 + Phase 2 shipped both axes; on Hemanth's smoke he pulled back from the Preset axis. Pre-Phase-3 removal of the Preset axis applied; system is now a single-axis Mode picker with each Mode owning its own bg + accent. Phase 2/3 scope rewritten below; Phase 1 retains its infrastructure ship credit (the templated stylesheet + applyTheme + QSettings persistence remain load-bearing).

---

## Context

Take Tankoban 2 from "boring minimalist" to "vibrant themes that can be toggled off and on" (Hemanth, 2026-04-25). Single axis:

- **Mode (only axis):** 6 named themes — Dark / Light / Nord / Solarized / Gruvbox / Catppuccin. Cycled via the sun/moon icon in the topbar. Each Mode owns its own bg + accent identity (no app-wide palette layered on top).

App-wide selection (pick "Nord" once, every section uses Nord). Win11 Mica top-level glass + tile hover-lift + tile drop-shadow as additive polish. Win-only Mica via `#ifdef Q_OS_WIN`; macOS/Linux fall back to solid background.

Cost: ~3-4 summons remaining post-correction, ~300-500 LOC. No new Qt module dependency.

---

## Objective

A user cycles through 6 named theme Modes via a single sun/moon icon button in the top-right topbar. Each Mode renders with a distinct bg + accent identity. Selection persists across app restart. Win11 Mica gives the chrome a frosted-glass feel behind everything. Tile cards lift + drop-shadow on hover at 60fps.

---

## Non-Goals (explicit)

- **Preset / palette / swatch axis.** Removed pre-Phase-3 per Hemanth's spec correction. The 6 Modes are the only theme axis. No 7-swatch popover, no palette button, no `Theme::Preset` enum.
- **F-bucket compositing effects** — animated radial-gradient backgrounds, per-element backdrop-filter, mix-blend-mode film grain, multi-layer compound box-shadow stacks beyond a single `QGraphicsDropShadowEffect`. **Not achievable in Qt Widgets.** Win11 Mica (top-level only) is partial substitute for atmospheric depth.
- **macOS/Linux Mica equivalent.** Win-only via `#ifdef Q_OS_WIN`. macOS/Linux MainWindow shows solid `Theme::kBg`. Future macOS port lands `applyMacVibrancy()` separately (out of scope).
- **`feedback_qt_vs_electron_aesthetic.md` memory revision body.** That lands as P4 close-out (in-place narrowing); not a TODO-authoring task.
- **Generic `::before`/`::after` pseudo-elements in QSS.** Infeasible. Replace with real child QWidgets if a specific surface needs it.
- **Tankoban-Max's `body[data-app-theme="dark"]` aliases.** N/A — we don't replicate Tankoban-Max's CSS-class-name structure; we just port the per-mode bg/text/border/accent values.

---

## Reference slate

- **Source audit:** `agents/audits/qt_theme_feasibility_2026-04-25.md` (§5.2 phase plan, RESOLVED ANSWERS, §3.4 risk surface; spec-correction header note added 2026-04-25)
- **Tankoban-Max source:** `C:\Users\Suprabha\Downloads\Tankoban-Max-master\Tankoban-Max-master\src\styles\theme-light.css` — per-mode bg/text/border/accent override layers for Light/Nord/Solarized/Gruvbox/Catppuccin (the historical 7-preset registry is intentionally NOT ported)
- **Memory baseline:** `feedback_qt_vs_electron_aesthetic.md` (revised in-place at P4 close-out)
- **TODO authoring template:** `feedback_fix_todo_authoring_shape.md`
- **Existing Tankoban 2 surfaces:** `src/ui/Theme.h`, `src/ui/Theme.cpp`, `src/main.cpp` (`applyThemeFromSettings`), `src/ui/MainWindow.cpp`, `src/ui/widgets/ThemePicker.{h,cpp}`, `src/ui/pages/TileCard.cpp`

---

## Phases

### P1 — Theme infrastructure (1 summon, Agent 5) — **SHIPPED 2026-04-25 ~16:50** at `18af3bf`

**Scope:**
- Extended `src/ui/Theme.h` with `ThemePalette` struct (color tokens) + `ThemeModeEntry` registry + `applyTheme()` function.
- New `src/ui/Theme.cpp` holds palette factory + QSS template w/ 16 `__PLACEHOLDER__` tokens + `buildStylesheet` substitution loop + `buildQPalette` mapping + `applyTheme(app, mode)` + `applyThemeFromSettings(app)`.
- Templatized the stylesheet to consume `ThemePalette` tokens instead of hardcoded literal colors.
- `QSettings` persistence via `theme/mode` key.

**Files:** `src/ui/Theme.h` (NEW types + API), `src/ui/Theme.cpp` (NEW ~530 LOC final), `src/main.cpp` (-390 LOC; calls `Theme::applyThemeFromSettings(app)` at boot), `CMakeLists.txt` (+1 line).

**Acceptance:** App launches identically to today (no visual diff for Mode::Dark default); `build_check.bat` BUILD OK. **MET.**

### P2 — Topbar Mode picker (1 summon, Agent 5) — **SHIPPED 2026-04-25 ~17:55** at working tree (uncommitted, P2 + P2-follow-up + spec-correction now subsume in single RTC)

**Scope (post-spec-correction, single-axis):**
- New widget `src/ui/widgets/ThemePicker.{h,cpp}` — top-right topbar cluster placement, alongside refresh / add-files. **Single icon button.**
- Sun/moon button — click cycles forward through the 6 Modes (Dark → Light → Nord → Solarized → Gruvbox → Catppuccin → Dark).
- Tooltip shows "Current — click for Next" formula per Tankoban-Max convention.
- Wires to `applyTheme()` from P1; updates `QSettings` on each click.

**Files:** `src/ui/MainWindow.cpp` (+3 LOC topbar wiring) + `src/ui/widgets/ThemePicker.{h,cpp}` (~80 LOC final after spec-correction strip-down).

**Acceptance:**
- Click sun/moon → cycles through 6 Modes; bg + accent retint live (Dark=gold, Light=blue, Nord=cyan, Solarized=blue, Gruvbox=blue-grey, Catppuccin=lavender).
- Restart app → previous selection persists.
- `build_check.bat` BUILD OK. **MET.**

### P3 — Per-Mode override layer (1-2 summons, Agent 5) — **NEXT**

**Scope:**
- Port per-Mode bg/text/border/accent override layers from Tankoban-Max `theme-light.css` for the 5 non-Dark modes (Light, Nord, Solarized, Gruvbox, Catppuccin). Each mode currently resolves only bg0/bg1/accent/accentSoft/accentLine to its own values; the remaining ~13 ThemePalette tokens (text, textDim, muted, border, borderHover, topbarBg, sidebarBg, menuBg, toastBg, cardBg, overlayDim) still render as Dark baseline. P3 fills these per Mode.
- The Light mode override is the heaviest port (white-on-dark inversion across all surfaces). Nord/Solarized/Gruvbox/Catppuccin are dark-on-dark variations and need surface-tint overrides only (no text-color inversion).
- **Source for each Mode's overrides:** `theme-light.css:479-486` (light), `:479-535` (nord), `:585-625` (solarized), `:680-720` (gruvbox), `:780-820` (catppuccin) — the `body[data-app-theme="<mode>"]` blocks. Each block sets ~10-20 CSS variables; map those to Tankoban 2's ThemePalette token names.
- Implementation shape: per-Mode hardcoded function `lightOverlay()` / `nordOverlay()` / etc. each taking the Dark baseline and mutating the relevant tokens. `resolvePalette(Mode)` delegates to the per-Mode overlay function.

**Files:** `src/ui/Theme.cpp` (extend `resolvePalette` with per-mode branches OR add helper functions). `src/ui/Theme.h` if any new types needed.

**Acceptance:**
- Cycle through all 6 Modes — each renders with its own coherent bg + text + border + accent identity.
- Light mode: no "white text on white background" or "dark text on dark background" visual bugs.
- All 5 pages (Comics / Books / Videos / Stream / Sources) render correctly in all 6 Modes.
- `build_check.bat` BUILD OK; Hemanth visual smoke green across at least 3 Modes (Dark + Light + one of the named-color modes).

**Note:** P3 may take 2 summons if the Light port is heavier than estimated.

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

## Decisions (post-spec-correction)

1. **Picker placement:** top-right topbar cluster, single icon button (sun/moon for Mode cycle). Sits LEFT of refresh+add cluster (grouping by intent: appearance vs library-data).
2. **Picker mechanic:** click cycles forward through Modes directly. No popover. Persistence via `Theme::saveMode` immediately on click.
3. **App-wide vs per-section:** app-wide. Pick "Nord" once → every section uses Nord. Per-section accent identity (Comics gold vs Books blue) handled separately by `Theme::accentForSection` and is orthogonal to the theme system.
4. **Modes shipped:** 6 — Dark / Light / Nord / Solarized / Gruvbox / Catppuccin. All 6 enumerated in `Theme::kModes` with their own bg + accent. Phase 3 fills the remaining per-Mode overrides.
5. **Memory revision:** revise `feedback_qt_vs_electron_aesthetic.md` in-place with "narrowed 2026-04-25" clarification at P4 close-out. F-bucket compositing claim preserved; C/D/M-bucket guidance softened.
6. **Mica multi-OS:** Win-only `#ifdef Q_OS_WIN`. macOS/Linux MainWindow shows solid `Theme::kBg`. Future macOS port lands `applyMacVibrancy()` separately.
7. **(Removed) Preset axis:** dropped pre-P3 per Hemanth's spec correction. No 7-swatch palette popover. The QSettings `theme/preset` key is orphaned (not read, not written).

---

## Risk surface

1. **F-bucket ceiling acknowledged.** Animated radial backgrounds + per-element backdrop-blur + mix-blend-mode + multi-layer compound shadows NOT achievable in Qt Widgets. Win11 Mica (top-level only) is partial substitute for atmospheric depth. Memory clarification at P4 close-out preserves the original "don't propose QML-impossible ports" instinct **for F-bucket only.**
2. **`QGraphicsEffect` widget-bounds clipping.** Single-layer drop-shadow via `QGraphicsDropShadowEffect` clips at widget bounds (unlike CSS `box-shadow` which composites with siblings). Accepted trade-off — only visible on edge-adjacent elements.
3. **`QPropertyAnimation` boilerplate.** Hover/scale animations require one animation object per property/widget (heavier than CSS `transition: ...`), but achieves 60fps parity once wired.
4. **Per-Mode override port effort.** 5 Modes × ~13 tokens = ~65 token assignments. P3 budget allows 2 summons.
5. **Memory re-scoping needed at close-out.** `feedback_qt_vs_electron_aesthetic.md` over-generalizes "only QML closes the gap" — narrows to F-bucket only. Lands as P4 close-out task.
6. **Legacy `theme/preset` QSettings key orphan.** Users who picked a non-Noir preset between Phase-1 ship and the spec correction will land on their Mode's default accent on next launch (the preset key is silently ignored). No migration written; the orphan is harmless. Acceptable scope reduction.

---

## Smoke criteria per phase

- **P1 self-smoke:** SHIPPED — app launches identically to pre-P1; `build_check.bat` BUILD OK.
- **P2 self-smoke:** SHIPPED — single sun/moon button cycles through 6 modes; chrome retints; selection persists across restart.
- **P3 Hemanth-smoke:** cycle through all 6 Modes; all 5 pages render correctly in each Mode; no white-on-white or dark-on-dark visual bugs.
- **P4 Hemanth-smoke:** Win11 Mica visible behind chrome; tile lift + drop-shadow at 60fps; no edge-tile shadow regression.

---

## Exit criteria

- All 4 phases shipped + smoked GREEN
- Memory `feedback_qt_vs_electron_aesthetic.md` narrowed in-place to F-bucket scope
- Single-axis system (6 Modes) live in source code; no Preset axis remnants
- Agent 5 STATUS.md section closes; CLAUDE.md dashboard refresh
- `build_check.bat` + main app launch + each-Mode visual verification all green

---

## Sign-off

Authored 2026-04-25 by Agent 0. Spec-corrected 2026-04-25 by Hemanth (Preset axis removal). P1 + P2 SHIPPED; P3 unblocked.
