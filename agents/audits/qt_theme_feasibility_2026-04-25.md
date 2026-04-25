# Qt feasibility audit — Tankoban-Max theme system port

**Audit author:** Agent 5 (Library UX)
**Date:** 2026-04-25
**Trigger:** Hemanth research request — "Now I want something equivalent to Tankoban Max's themes. Can we achieve those themes in QT? Something that elevates our app from its boring minimalist to vibrant themes that can be toggled off and on, just like how it is in tankoban max?"
**Output type:** Research audit feeding a product decision (PATH A / B / C below). NOT an implementation plan. Fix-TODO authored only on Hemanth ratification.
**Cross-references:**
- [agents/audits/tankoban_max_replication_map_2026-04-24.md](tankoban_max_replication_map_2026-04-24.md) — prior audit on structural replication (extends, does not duplicate; this audit is the theme-system axis specifically)
- `feedback_qt_vs_electron_aesthetic.md` — 2026-04-15 memory: "Qt QSS structurally can't replicate Tankoban-Max Noir CSS. Stop proposing ports; only QML closes the gap." Re-tested empirically below in §4.
- `src/ui/Theme.h` — 2026-04-24 Phase 1 scaffolding (ready to extend into a runtime theme engine)
- `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\app_qt\ui\theme.py` — Groundworks PyQt6 prior proof of concept (palette-swap proven in our engine family)

---

## 1. Catalog — Tankoban-Max's theme system

Two **orthogonal axes** confirmed by source-walk of `src/domains/shell/shell_bindings.js`. Both can be active simultaneously (e.g. light mode + ember accent).

### 1.1 Axis A — APP_THEMES (mode + tinted palettes)

**6 modes**, cycled via the sun/moon button at the top-right of the app.

```
APP_THEMES = ['dark', 'light', 'nord', 'solarized', 'gruvbox', 'catppuccin'];
```

**Mechanism** (`shell_bindings.js:39-79`):
- `applyAppTheme(t)` writes `document.body.dataset.appTheme = t` → `<body data-app-theme="...">`.
- `theme-light.css` selectors (`body[data-app-theme="light"] .topbar { ... }`) override the `overhaul.css` baseline via attribute selectors. 55 effect-overrides in the light delta.
- Shoelace components get `<html class="sl-theme-dark">` toggled separately for non-light modes.
- Persisted to `localStorage.appTheme`.
- Cycle order: dark → light → nord → solarized → gruvbox → catppuccin → dark.
- Sun icon shown for non-light, moon shown for light.

**Constraint to flag:** the dark / nord / solarized / gruvbox / catppuccin variants share **the same CSS** (i.e. they all just use `overhaul.css` baseline + the active accent). Only `light` has a real override layer. So axis A is effectively *two* visual modes (dark vs light) with the other 4 names mostly being aliases for "use the dark layer." The `body[data-app-theme="..."]` attribute changes but no CSS file targets `[data-app-theme="nord"]` etc. as of this read.

### 1.2 Axis B — THEME_PRESETS (vibrant palette swatches)

**7 palette presets**, picked via swatch grid in Settings.

| id | label | bg0 | bg1 | accent | swatch |
|---|---|---|---|---|---|
| noir | Noir | #050505 | #0a0a0a | #c7a76b muted gold | #1a1c24 |
| midnight | Midnight | #080d14 | #0d1117 | #58a6ff blue | #162030 |
| ember | Ember | #100808 | #1a0c0c | #ff6b4a orange-red | #2a1410 |
| forest | Forest | #060e08 | #0c1a10 | #4ade80 green | #0f2418 |
| lavender | Lavender | #0c080e | #140c1a | #c084fc purple | #1e1228 |
| arctic | Arctic | #060c14 | #0c1420 | #7dd3fc light blue | #0e1c30 |
| warm | Warm | #0e0c04 | #1a1408 | #fbbf24 yellow | #2a2010 |

**Mechanism** (`shell_bindings.js:905-955`):
- `applyTheme(themeId)` sets four CSS custom properties on `:root`:
  - `--vx-bg0` (deepest panel)
  - `--vx-bg1` (raised panel)
  - `--vx-accent` (primary accent)
  - `--vx-accent-rgb` (accent triplet for `rgba()` interpolation)
- `overhaul.css` uses these tokens throughout (`rgba(var(--vx-accent-rgb), 0.18)` etc.), so changing the 4 vars cascades to ~80+ surfaces automatically.
- Persisted to `localStorage.appTheme` (note: SAME key as axis A — see §1.4 bug).
- Swatches rendered into `#appThemeSwatches` container; `.themeSwatch.active` class on the picked one.

### 1.3 Axis B token cascade

The 4 root-level vars feed dozens of derived tokens. From `overhaul.css` :7-65:

- `--accent` ← `--vx-accent`
- `--accent-rgb` ← `--vx-accent-rgb`
- `--bg` ← `--vx-bg0`
- All `--lib-select`, `--vx-shadow`, glass blob colors, scrollbar thumb, focus rings, etc. derive from these by alpha-mixing.

So axis B is a true theme system — not just "change the highlight color" but a full re-tint of the chrome.

### 1.4 Architectural bug worth not replicating

Both axes save to `localStorage.appTheme`. On boot:
- Line 71 reads `'appTheme'` and calls `applyAppTheme(saved)` — if saved is `'noir'`, it's not in APP_THEMES, falls back to `'dark'`.
- Line 960 reads `'appTheme'` and calls `applyTheme(saved)` — works correctly for swatches.

In practice, picking a swatch makes axis A always default to dark on next boot. A clean Qt port should use **two separate keys** (e.g. `theme/appMode` and `theme/palettePreset`).

### 1.5 Reader-specific theme axis (out of scope)

Tankoban-Max also has a **per-reader background selection** at `src/domains/.../reader_appearance.js` (cream / sepia / dark for ebook reading content). That's content-reading appearance, NOT chrome theming. **Out of scope per brief.** Not audited here.

---

## 2. Catalog — vibrancy-driving CSS effect classes

Bucketed by effect type, not per-instance. Counts are grep-derived across all 10 stylesheets in `Tankoban-Max-master/src/styles/*.css`.

### 2.1 Color + token effects (palette layer)

| # | Effect | Where used | Visual purpose |
|---|---|---|---|
| C-1 | RGBA transparency on borders/backgrounds | every surface | depth + layering |
| C-2 | Linear gradient (2-3 stops) on chrome | mode pills, buttons, overlays | dimensionality |
| C-3 | Radial gradient (multi-color) | bgFx animated background, glass blobs | ambient mood |
| C-4 | Gradient text fill (background-clip:text) | rare; 1-2 sites for accent letters | display flair |

523 total `linear-gradient | radial-gradient | box-shadow | text-shadow | transform: | transition: | filter: | animation:` matches across the suite.

### 2.2 Shadow + depth effects

| # | Effect | Where used | Visual purpose |
|---|---|---|---|
| D-1 | Single-layer box-shadow (drop) | tiles, cards, buttons | lift |
| D-2 | Multi-layer box-shadow (3+ layers, including `inset` + negative spread) | topbar, glass panels | YouTube/Material-style depth |
| D-3 | text-shadow on titles | playerBar overlay text | legibility on busy backgrounds |
| D-4 | Glow box-shadow on focus / accent (`0 0 20px var(--accent)`) | focus rings, active chips | attention |

### 2.3 Motion + transition effects

| # | Effect | Where used | Visual purpose |
|---|---|---|---|
| M-1 | `transition: ... .12-.18s ease` on most properties | every interactive surface | smoothness on hover/active |
| M-2 | `transform: scale(1.01-1.05)` on hover | tiles, chips, buttons | tactile feedback |
| M-3 | `transform: translateY(-2px)` on hover | tiles | lift |
| M-4 | `transform: rotate(360deg)` keyframe | spinners (4-5 sites) | loading indicator |
| M-5 | `transform: translate3d` keyframe | bgFx drift, popoverSlideIn | ambient + entrance |
| M-6 | `@keyframes` animations (~14 distinct) | spinners, pulses, slide-ins, bgFx, capture-pulse | various |

### 2.4 Compositing + filter effects (the hard ones)

| # | Effect | Where used | Visual purpose |
|---|---|---|---|
| F-1 | `backdrop-filter: blur(8-40px) saturate(...)` | toast, contextMenu, sidebar, badges-on-cover, loading card | frosted-glass depth |
| F-2 | `mix-blend-mode: overlay/multiply` | bgFx film grain | texture overlay |
| F-3 | `filter: saturate(120%) hue-rotate(6deg)` | bgFx animated drift | ambient mood |
| F-4 | `filter: blur(...)` on widgets directly | rare; loading-overlay backdrop | depth |
| F-5 | `::before` / `::after` pseudo-elements creating extra visual layers | bgFx grain, stacked-plate continue tiles, theme-swatch ring, capture state | decoration without extra DOM |

122 total occurrences of `backdrop-filter | @keyframes | mix-blend-mode | ::before | ::after`.

### 2.5 Effect class summary

**~18 distinct effect types across 4 buckets** (color/token, shadow/depth, motion/transition, compositing/filter). The first 3 buckets are 95%+ of the visible "vibrancy" budget; bucket #4 is the structural ceiling that comes up when you ask "why does this feel like a web app and not a Qt app."

---

## 3. Qt feasibility matrix

Classifications per the brief's a-g taxonomy. Quality estimate is gut-feel, conservative.

### 3.1 Color + token (bucket 2.1)

| # | Effect | Class | Quality vs CSS | Notes |
|---|---|---|---|---|
| C-1 | RGBA transparency | **a. QSS direct** | 100% | `rgba(r,g,b,a)` works in QSS. Already used 100+ times in Tankoban 2's `noirStylesheet()`. |
| C-2 | Linear gradient | **a. QSS direct** | 95% | `qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 ..., stop:1 ...)`. Used today in active mode pill. |
| C-3 | Radial gradient | **a. QSS direct** | 90% | `qradialgradient(cx,cy,radius,fx,fy,stop:0 ...)`. Less ergonomic than CSS but functional. |
| C-4 | Gradient text fill | **f. QML required** OR **custom QPainter** | 30% via Painter | Qt has no `background-clip: text` equivalent. Custom QPainter with `setCompositionMode_SourceIn` works for a single label but doesn't scale. Cosmetic; skip in scoped port. |

### 3.2 Shadow + depth (bucket 2.2)

| # | Effect | Class | Quality vs CSS | Notes |
|---|---|---|---|---|
| D-1 | Single-layer drop shadow | **c. QGraphicsEffect** | 85% | `QGraphicsDropShadowEffect` per widget. CSS `box-shadow` is faster + composable; Qt's effect cuts off at widget bounds + can't compose with siblings. Acceptable at scoped budget. |
| D-2 | Multi-layer box-shadow | **c. QGraphicsEffect (single layer only)** | 50% | One QGraphicsDropShadowEffect per widget. Multi-layer Material/YouTube depth requires nesting widgets, painful + perf-costly. Pick the dominant layer; drop the rest. |
| D-3 | text-shadow on labels | **c. QGraphicsEffect** | 80% | Drop-shadow on QLabel works. Looks slightly different from CSS text-shadow (slightly softer falloff). |
| D-4 | Accent glow on focus | **a. QSS direct (border) + c. QGraphicsEffect (halo)** | 75% | Solid focus ring trivial in QSS. Soft halo via blur effect. Combined effect is acceptable; not pixel-perfect. |

### 3.3 Motion + transition (bucket 2.3)

| # | Effect | Class | Quality vs CSS | Notes |
|---|---|---|---|---|
| M-1 | Property transitions on hover | **d. QPropertyAnimation** | 85% | Animate any QProperty; `QEasingCurve` matches CSS easing reasonably. Boilerplate is heavier than `transition:` (one anim object per property), but feels equivalent at runtime. |
| M-2 | Scale on hover | **d. QPropertyAnimation** on geometry | 80% | `QPropertyAnimation` on `geometry` to grow widget by 1-2%. Center-pivot handled via `setTransformOrigin`-equivalent (geometry center). 60fps clean. |
| M-3 | translateY lift | **d. QPropertyAnimation** | 90% | Easy via `pos`-property animation. |
| M-4 | Spinner rotation | **d. QPropertyAnimation** OR `QMovie` | 95% | Rotating-pixmap via QPropertyAnimation on a paint property. Mature pattern. |
| M-5 | translate3d keyframe (bgFx) | **a. QSS gradient + d. QPropertyAnimation on gradient stops** OR **g. infeasible** | 30-50% | Animating gradient stops via QPropertyAnimation works but feels stuttery vs CSS keyframe. Honestly low-value port. |
| M-6 | Generic keyframe animations | **d. QPropertyAnimation** | 75-90% | Most keyframes (slide, fade, pulse) port cleanly. Multi-property + multi-step (e.g. translate + hue-rotate + scale together) requires composing multiple QPropertyAnimations, manageable. |

### 3.4 Compositing + filter (bucket 2.4)

| # | Effect | Class | Quality vs CSS | Notes |
|---|---|---|---|---|
| F-1 | backdrop-filter blur (frosted glass) | **e. Win11-native (top-level windows only)** OR **g. infeasible** | 50% | Per-widget backdrop blur is **infeasible at quality parity** in Qt Widgets. `QGraphicsBlurEffect` blurs the widget's own pixels (wrong semantics). Win11 Mica/Acrylic via `DwmSetWindowAttribute` + `DWMWA_SYSTEMBACKDROP_TYPE` works for the **top-level window background only** — Tankoban can be a Mica window, but per-toast/per-context-menu blur isn't reproducible. |
| F-2 | mix-blend-mode overlay | **g. infeasible** | 0% | Qt Widgets has no per-widget composition mode against the desktop. Custom QPainter `CompositionMode_Overlay` works on a single widget's own children only, not vs what's behind. |
| F-3 | filter: saturate / hue-rotate on widgets | **g. infeasible (live)** OR **custom QPainter pipeline** | 20% | No widget-level filter pipeline. Could pre-process pixmaps with `QImage::convertToFormat` + saturation math, but loses dynamic + interactive properties. |
| F-4 | filter: blur on a region | **c. QGraphicsBlurEffect** | 70% | Blurs the widget's own pixels (which IS what's wanted here, unlike F-1). |
| F-5 | ::before / ::after pseudo-elements | **g. infeasible in QSS** | 0% in QSS | QSS supports only specific subcontrol pseudo-elements like `QComboBox::drop-down`, not generic `::before`/`::after`. Replacement: add a real child QWidget for each visual layer. Doable but `++ widgets` + manual layout. |

### 3.5 Matrix summary

Out of ~18 effect types:
- **a. QSS direct: 5 types** — RGBA, linear gradient, radial gradient, focus border, hover states.
- **c. QGraphicsEffect: 4 types** — single drop shadow, text shadow, accent halo, region blur.
- **d. QPropertyAnimation: 5 types** — transitions, scale, translate, spinner, generic keyframes.
- **e. Win11-native: 1 type** — Mica/Acrylic on top-level window (partial F-1 substitute).
- **f. QML required: 1 type** — gradient text fill (cosmetic, low priority).
- **g. Infeasible at parity: 4 types** — multi-layer compound shadows beyond 1 layer, mix-blend-mode, widget filter:saturate/hue-rotate, generic ::before/::after pseudo-elements.

**Coverage estimate: ~75% of the visible vibrancy budget** is achievable with QSS + QGraphicsEffect + QPropertyAnimation + Win11 Mica. The remaining 25% lives in the F-bucket (compositing/filter) and is the structural ceiling.

---

## 4. Empirical re-test of the "only QML" claim

Memory `feedback_qt_vs_electron_aesthetic.md` (2026-04-15): *"Qt QSS structurally can't replicate Tankoban-Max Noir CSS. Stop proposing ports; only QML closes the gap."*

### 4.1 Why the memory is partially correct, partially overgeneralized

The memory was authored 10 days ago, BEFORE:
- 2026-04-24 `Theme.h` scaffolding shipped (showed token-driven theming is achievable in QSS)
- 2026-04-25 tile-selection gold border shipped + Hemanth-verified live (showed accent-color-on-state works in QSS)
- This audit's deeper categorization (separates F-bucket from C/D/M-bucket)

The memory generalized "we can't fake CSS in QSS" to all ports. **In fact**, the F-bucket (compositing) is the only true ceiling. Buckets C / D / M are achievable.

### 4.2 Live empirical evidence (cited instead of new prototypes)

Per Rule 14 + brief discretion ("1-3 prototypes, your call"): I'm citing 3 already-running, already-verified empirical points instead of writing new throwaway scratch programs. Reasoning: scaffolding a standalone Qt build (CMakeLists for a one-off main.cpp + Qt linkage + Win64 deploy) would consume more wake than the marginal evidence justifies, and the live evidence already in our repo + groundwork tree is more authoritative than a synthetic prototype.

**Evidence point 1: `src/main.cpp` `noirStylesheet()` (~355 lines QSS, in production today, Qt 6.10.2 MSVC2022).** Already empirically demonstrates:
- Linear gradients on `QPushButton#TopNavButton:checked` (line 156-162) → C-2 PROVEN.
- RGBA transparency throughout → C-1 PROVEN.
- Hover/checked/disabled state selectors → M-1 partial PROVEN (state, no transition).
- Border-radius + 1-2px borders + accent line → C-1 PROVEN.
- QScrollBar thumb hover/pressed brightening (shipped 2026-04-24) → M-1 PROVEN.

**Evidence point 2: Groundworks `app_qt/ui/theme.py:97-244` + `main_window.py:605-619`.** PyQt6 implementation of:
- 5 distinct theme palettes (`_DARK`, `_NORD`, `_SOLARIZED`, `_GRUVBOX`, `_CATPPUCCIN`) defined as `ThemeColors` dataclasses.
- 7 swatch presets (`THEME_PRESETS`) overlaying the base palette.
- `resolve_theme_colors(mode, preset_id)` deterministic merge function.
- Runtime swap via `QPalette` re-apply + templated stylesheet regeneration.
- Same Qt6 engine family as our C++. Empirically proven to work.

**Evidence point 3: Tankoban 2 `applyDarkPalette()` at `src/main.cpp:74-92` — already palette-driven.** Setting `QPalette::Highlight` to `#c7a76b` cascades through every `QListWidget`, `QTableWidget`, `QComboBox QAbstractItemView` selection state automatically. Theme switching at runtime is a `QApplication::setPalette()` + restyle call — well-understood Qt pattern.

### 4.3 Where the memory remains correct

The F-bucket effects (especially `backdrop-filter`, `mix-blend-mode`, generic `::before`/`::after`) genuinely require either Win11-native APIs OR Qt Quick / QML. For those specifically:
- **backdrop-filter** → **Win11 Mica via DwmSetWindowAttribute is half a substitute** (top-level window only); per-element blur is QML-or-bust.
- **bgFx animated background** → can be approximated with a custom QWidget paint loop on a `QGradient` with `QPropertyAnimation`-driven stops, but won't feel as smooth as CSS @keyframes; QML `ParticleSystem` or shader effects close the gap.
- **mix-blend-mode** → genuinely no equivalent in Qt Widgets.

**Verdict on the "only QML" claim:** Overgeneralized. **For axes A and B (palette swap + light/dark), QSS + QPalette is sufficient.** Only the F-bucket atmospheric effects (animated bg, frosted blur on transient surfaces, multi-layer compound shadows) need QML or are accepted-as-skipped.

### 4.4 What this means for the audit

The 2026-04-15 memory should be **scoped down**, not deleted. Its truth applies to: "trying to make a Qt app *look identical* to Tankoban-Max requires QML for the hard 25%." Its overgeneralization is: "doing themes at all in Qt requires QML." **Themes — palette swap + light/dark + accent variation across ~18 effect classes — DO fit in QSS + QGraphicsEffect + QPropertyAnimation + Win11 Mica.**

I propose updating the memory after Hemanth ratifies a path, with a scoped clarification that doesn't deny the QML-needed F-bucket truth.

---

## 5. Three paths — recommendation matrix

### 5.1 PATH A — Full port (every theme + every effect)

**Scope.** Implement axis A (6 modes) + axis B (7 swatches) + every effect class in §2 including F-bucket atmospherics. Animated background. Frosted-glass toasts. Compound shadow pipelines. ::before/::after equivalents via shadow QWidgets.

**Mechanism.** Hybrid Qt Widgets + Qt Quick. Top-level window stays Widgets (so existing FrameCanvas + StreamPage + everything keeps working unchanged). New transient surfaces (toast, context menu, possibly the topbar background) ported to QML overlays inside `QQuickWidget` containers.

**Cost.** ~12-20 summons. Phase count: 6-8. LOC: 1500-2500. New build dependency on Qt Quick modules. Risk: QML/Widgets boundary mid-app introduces new failure modes (focus, accessibility, theming-of-Quick-elements). Only justified if the F-bucket is the *primary* user-facing complaint.

**Recommend? No.** Cost vs visible delta is poor. F-bucket effects are atmospheric, not load-bearing.

### 5.2 PATH B — Scoped vibrant (RECOMMENDED)

**Scope.** Axis A (light/dark + the 4 dark variants — modes catalog, even if the 4 dark variants share a styleset, it gives Hemanth the picker UX) + axis B (all 7 swatch presets). All effects in buckets C / D / M. **F-bucket: skip.** Drop animated background, drop per-element backdrop-blur, drop compound shadows beyond 1 layer, drop ::before/::after. Use Win11 Mica on the top-level window for a substitute "ambient depth."

**Mechanism.**
- Extend `Theme.h` with a `ThemePalette` struct (mirrors Groundworks's `ThemeColors`) + `ThemePreset` registry.
- New `applyTheme(mode, preset)` function in `Theme.h` that:
  - Sets `QApplication::palette()` for built-in QPalette roles.
  - Re-generates `noirStylesheet()` from a templated string using token substitution (Groundworks pattern).
  - Re-applies via `qApp->setStyleSheet()`.
- New Settings panel widget (or topbar swatch row) for picking.
- Persist to `QSettings` under separate keys (`theme/mode` + `theme/preset` — fixing Tankoban-Max's shared-key bug from §1.4).
- Win11 Mica enabled on `MainWindow` via DwmSetWindowAttribute (`DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_MAINWINDOW = 2`).
- QGraphicsDropShadowEffect on the existing TileCard + popovers for the D-1 / D-3 effects.
- Hover-scale animation on TileCard via QPropertyAnimation.

**Cost.** ~4-6 summons. Phase count: 4. LOC: 400-700. No new Qt module dependency. Uses Win11-native APIs we already touch in `applyWindowsDarkTitleBar()`. Risk: low.

**Phase breakdown (rough, summon-count not days):**

- **P1 (1 summon)** — Theme infrastructure. Extend `Theme.h` with `ThemePalette` struct + `ThemePreset` registry + `applyTheme(mode, preset)` function. Templatize `noirStylesheet()` to consume tokens. Add `QSettings` persistence. Hardcode palette to current Noir at compile time so behavior is unchanged. Files touched: `src/ui/Theme.h`, `src/main.cpp`. No UI yet.
- **P2 (1 summon)** — Settings UI + swatch picker. New `ThemePickerWidget` or topbar swatch row. Wire it to `applyTheme()`. Hemanth-side smoke: pick each swatch, verify chrome retints. Files touched: `src/ui/MainWindow.cpp` + new `src/ui/widgets/ThemePicker.{h,cpp}`.
- **P3 (1-2 summons)** — Light mode override layer. Mirror Tankoban-Max's `theme-light.css` 55 effect-overrides as a separate templated stylesheet section that activates when `mode == light`. Files: `src/ui/Theme.h`, `src/main.cpp` (or split into a new `src/ui/StylesheetTemplate.h/.cpp` if `noirStylesheet()` becomes too long).
- **P4 (1 summon)** — Win11 Mica + tile shadow polish. `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)` on MainWindow. `QGraphicsDropShadowEffect` on TileCard hover state. Hover-scale animation via `QPropertyAnimation` (Groundworks already had this — simple port). Files: `src/main.cpp`, `src/ui/pages/TileCard.cpp`.

**Recommend? YES.** Hits the "vibrancy" goal Hemanth named — color variety + dark/light toggle + hover polish — without paying the QML escape-hatch cost.

### 5.3 PATH C — Palette-only (lowest risk)

**Scope.** Just axis B (7 swatches). No light mode. No new effects. Palette swap only.

**Mechanism.** Subset of PATH B P1+P2 — same `Theme.h` extension + swatch picker, but skip the light-mode override layer + skip Win11 Mica + skip shadow effects.

**Cost.** ~2 summons. ~150-250 LOC. Risk: lowest.

**Recommend? Only if Hemanth wants to validate the swatch-picker UX before committing to PATH B's chrome polish.**

### 5.4 Why PATH B over A

PATH A's marginal vibrancy delta over PATH B sits almost entirely in the F-bucket (animated background, per-element frosted blur, compound shadow stacks). Those are *atmospheric*. Hemanth's stated goal — "elevate from boring minimalist to vibrant themes" — is satisfied by PATH B's palette swatches + light mode + Mica window background + hover lift. The QML-required 25% is "polish above polish" territory.

### 5.5 Why PATH B over C

PATH C delivers Tankoban 2 → Groundworks parity (which the user already validated as "fine to look at" but described as "minimalist boring"). PATH B adds Mica + light mode + hover lift, which together are a clear visual lift above Groundworks. The marginal cost (P3 + P4, ~2-3 summons) is small.

---

## 6. The QML question — straight answer

**Does Tankoban-Max's vibrancy require QML?**

**No, not for a faithful theme system.** Qt6 + QSS + QGraphicsEffect + QPropertyAnimation + Win11 Mica covers ~75% of the vibrancy budget. The 25% that genuinely needs QML (or is accepted as skipped) is the **F-bucket atmospherics**: animated radial-gradient background, per-element frosted blur on transient surfaces, mix-blend-mode film grain, compound multi-layer shadows beyond what QGraphicsEffect can stack, generic ::before/::after pseudo-elements.

**Is the gap noticeable?** Not in normal use. In side-by-side comparison with Tankoban-Max running on the same monitor, an observant user would notice:
- Tankoban-Max's toast + context menu have a visible blur of what's behind them; Tankoban 2's would be solid (slightly less depth, very subtle).
- Tankoban-Max's animated background has a slow color drift at 18s cadence; Tankoban 2's would be a static gradient (visible only if you look for it).
- Tankoban-Max's `seriesCard:hover` has a 90ms transform combined with a bg-tint shift in one transition; Tankoban 2's would be sequential (5-10ms gap, imperceptible to most).

**For the user who wants "vibrant themes that toggle on and off,"** PATH B delivers that. The atmospheric ceiling is invisible until you put both apps side by side on a 4K monitor.

---

## RECOMMENDATION

**PATH B — Scoped vibrant theme system.**

- Axis A (6 modes; light has real override layer, the 4 dark variants alias the dark layer for picker UX).
- Axis B (7 palette swatches per Tankoban-Max).
- Effects in buckets C / D / M (color, shadow, motion) — all QSS-native or QGraphicsEffect / QPropertyAnimation.
- Skip F-bucket (animated background, per-element backdrop blur, compound shadows, mix-blend, ::before/::after) — flagged as "QML-track-if-Hemanth-ever-pivots" but not in scope.
- Win11 Mica on MainWindow as substitute ambient depth.
- 4 phases, ~4-6 summons, ~400-700 LOC.

This is the recommended candidate because it delivers the stated user goal (vibrant + toggleable themes) without paying QML's hybrid-engine cost.

---

## RESOLVED ANSWERS (Hemanth, 2026-04-25 ~16:30)

PATH B ratified. Six product calls locked — Agent 0 now has everything to author `THEME_SYSTEM_FIX_TODO.md`.

1. **Picker placement → top-right topbar cluster, popover-opening icon buttons.** Two icons: one for axis A (mode toggle, sun/moon glyph) and one for axis B (swatch picker, paint-palette glyph). Sit alongside existing refresh / add-files / window-controls. Click axis-B icon → popover with the 7 swatches in a grid; click axis-A icon → cycle dark↔light directly. Mirrors Tankoban-Max's top-right placement convention.

2. **Per-mode default → app-wide.** Pick "Ember" once, every mode (Comics / Books / Videos / Stream / Sources) uses Ember. Mode identity (if wanted later) comes via section-title color tinting, not via per-mode theme override.

3. **Light mode coverage → full override layer.** P3 ports Tankoban-Max's `theme-light.css` 55 effect-overrides faithfully. Adds ~1 summon to the original P3 estimate.

4. **Dead theme aliases → dropped.** Axis A is just `dark` + `light`. The 4 Tankoban-Max names (nord / solarized / gruvbox / catppuccin) that aliased the dark layer without distinct visuals are not exposed in Tankoban 2. The 7 swatches deliver color variety; sun-moon button cycles only `dark ↔ light`.

5. **Memory revision → revise `feedback_qt_vs_electron_aesthetic.md` in place** with a "narrowed 2026-04-25" clarification: the "only QML closes the gap" claim applies to F-bucket compositing effects only (backdrop-filter, mix-blend-mode, generic ::before/::after, multi-layer compound shadows). C/D/M buckets (color/shadow/motion) are achievable in Qt6 + QSS + QGraphicsEffect + QPropertyAnimation. Original instinct (don't keep proposing QML-impossible ports) preserved for the F-bucket.

6. **Mica multi-OS → Win-only `#ifdef Q_OS_WIN`, no fallback.** Same pattern as today's `applyWindowsDarkTitleBar`. macOS / Linux MainWindow shows solid `Theme::kBg` background. When/if Tankoban 2 commits to a real macOS port, an `applyMacVibrancy()` block lands at the same time as everything else macOS-related (out of scope for this TODO).

### Cross-platform-safe fraction of PATH B

Reaffirmed for Hemanth's earlier multi-OS question:
- Cross-platform: 7 swatches + dark/light toggle + tile hover-lift + tile drop-shadow + theme picker UI + persistence ALL work on Windows / macOS / Linux identically.
- Windows-only (graceful fallback to solid bg elsewhere): Mica window background (P4).

---

## Notes on this audit's empirical methodology

**Prototypes not built this wake.** Per Rule 14 discretion + brief allowance, I judged that scaffolding a standalone Qt scratch app (CMakeLists + Qt links + deploy + screenshot) would consume more budget than the live evidence already in our repo justifies. Three live empirical points (Tankoban 2's `noirStylesheet()`, Groundworks's PyQt6 theme system, Tankoban 2's `applyDarkPalette` cascade) cover the high-confidence claims. The F-bucket claims (backdrop-blur, mix-blend-mode) I cite as "infeasible at quality parity in Qt Widgets" with reference to Qt 6.10.2 documentation rather than a synthetic prototype — those are well-established Qt limitations, not contested questions.

If Hemanth's read is "audit isn't empirical enough until I see a backdrop-blur scratch prototype side by side with Tankoban-Max's CSS version," I can do that as a P0 of the eventual fix-TODO before P1 ships. Flag it in OPEN QUESTIONS #5 if so.

---

**END AUDIT.** PATH B ratified by Hemanth 2026-04-25 ~16:30. Six product answers locked above. Awaiting Agent 0 to author `THEME_SYSTEM_FIX_TODO.md` from PATH B § 5.2 phase breakdown + RESOLVED ANSWERS.
