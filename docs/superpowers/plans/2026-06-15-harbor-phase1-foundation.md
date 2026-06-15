# Harbor Redesign — Phase 1 (Foundation) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the shared foundation of the Harbor redesign — the Harbor design-token layer, bundled Fraunces+Inter fonts, a left NavRail, and a window-centered search — so the existing app runs inside the new Harbor shell with all page-switching intact.

**Architecture:** Pure re-skin + re-layout over the existing native engine. The window is *already* frameless with custom Win32 chrome; we relocate that chrome, swap the centered top-pill bar for a left `NavRail` that drives the unchanged `activatePage()` contract, and add a MainWindow-owned center search. The Theme namespace's `__PLACEHOLDER__` QSS system gets the Harbor token ladder. Nothing in `buildPageStack()`/`activatePage()` page logic changes.

**Tech Stack:** C++17, Qt 6 Widgets, the existing `Theme` namespace (`src/ui/Theme.{h,cpp}`), `PerModeNavController`, GoogleTest (dep-free units only), the `tankoctl` dev bridge for verification.

**Spec:** `docs/superpowers/specs/2026-06-15-harbor-redesign-theatre-flagship-design.md`

**Verification model:** Pure-logic (token resolution) gets a GoogleTest. Visual widgets (NavRail, search) are verified by **build-green + `tankoctl introspect-tree` (visibility/geometry) + Hemanth eyes-on-screen** — Agent 0 builds and launches every smoke (direct-exe fast launch per `feedback_agent_launches_app`); Hemanth only looks + reports. Each task ends green before the next.

---

## File Structure

- **Modify** `src/ui/Theme.h` — add Harbor ladder fields to `ThemePalette`; gold legacy constants; radius-ladder + ease constants; `registerFonts()` decl.
- **Modify** `src/ui/Theme.cpp` — rewrite `darkBaselineNoir()` to the Harbor ladder; add token-table entries; retint `kTemplate` surfaces/accent; font-family split; `registerFonts()` impl.
- **Create** `resources/fonts/Fraunces-VariableFont.ttf`, `resources/fonts/Inter-VariableFont.ttf`; **modify** `resources/resources.qrc`.
- **Modify** `src/main.cpp` — call `Theme::registerFonts()` before `applyThemeFromSettings`.
- **Create** `src/ui/widgets/NavRail.{h,cpp}` — the left rail (modes + pages + collections, collapsible, IDevInspectable).
- **Modify** `src/ui/MainWindow.{h,cpp}` — layout swap (chrome strip + `[NavRail | pageStack]`), relocate chrome buttons, update WM_NCHITTEST whitelist, repurpose collapse, remove `mirrorTopBarSlotWidths`.
- **Create** `src/ui/widgets/CenterSearchBar.{h,cpp}` — window-centered frosted search.
- **Modify** `cmake/TankobanSources.cmake` (register NavRail + CenterSearchBar), `cmake/TankobanTests.cmake` (token test).

---

## Task 1: Harbor design tokens in Theme

**Files:**
- Modify: `src/ui/Theme.h` (struct `ThemePalette` ~:74-97; legacy constants :41-55; radius ints :57-65)
- Modify: `src/ui/Theme.cpp` (`darkBaselineNoir()` :82-103; token table :910-927; `kTemplate` :297-905)
- Test: `tests/ui/test_theme_tokens.cpp` (new) + register in `cmake/TankobanTests.cmake`

The OKLCH ladder (hue 260, low chroma) baked to sRGB (tunable later; comment the OKLCH source):
`bg0` oklch(.18)=`#121317` · `surface` oklch(.22)=`#1a1d24` · `elevated` oklch(.27)=`#232833` · `raised` oklch(.32)=`#2d333f`. Ink: `text` `#f3f1ea` · `textDim` `#cfd4dc` · `muted` `#aab1bd` · subtle `#6b7280`. `accent` `#e8b923`, `onAccent` `#14110a`. `error` `#e50914` (firewalled — never per-mode). Radius ladder `6/10/14/20/28/999`.

- [ ] **Step 1: Write the failing test** — `tests/ui/test_theme_tokens.cpp`:

```cpp
#include <gtest/gtest.h>
#include "ui/Theme.h"
TEST(HarborTokens, DarkLadderAscendsAndAccentIsGold) {
    auto p = tankoban::ui::Theme::resolvePalette(tankoban::ui::Theme::Mode::Dark);
    EXPECT_EQ(p.accent.toLower(), QStringLiteral("#e8b923"));      // gold, not #c7a76b
    EXPECT_EQ(p.onAccent.toLower(), QStringLiteral("#14110a"));
    EXPECT_EQ(p.error.toLower(), QStringLiteral("#e50914"));
    // surface ladder strictly lighter than canvas
    EXPECT_NE(p.surface, p.bg0);
    EXPECT_NE(p.elevated, p.surface);
    EXPECT_NE(p.raised, p.elevated);
}
TEST(HarborTokens, RadiusLadderExists) {
    using namespace tankoban::ui::Theme;
    EXPECT_EQ(kRadXs, 6); EXPECT_EQ(kRadMd, 14); EXPECT_EQ(kRadPill, 999);
}
```

- [ ] **Step 2: Run it, confirm it fails to compile** (fields `onAccent`/`error`/`surface`/`raised` + `kRad*` don't exist yet). Run: `C:/tools/cmake-3.31.6-windows-x86_64/bin/ctest.exe --test-dir out -R HarborTokens -V` after registering the test. Expected: build error (missing members).

- [ ] **Step 3: Extend `ThemePalette`** (`Theme.h` :74-97) — add `QString surface, elevated, raised, onAccent, error;` (keep existing fields; `bg0` stays the canvas).

- [ ] **Step 4: Add radius + ease constants** (`Theme.h` near :57-65):

```cpp
inline constexpr int kRadXs = 6, kRadSm = 10, kRadMd = 14, kRadLg = 20, kRadXl = 28, kRadPill = 999;
// Eases are C++ constants for QPropertyAnimation::setEasingCurve callers (QSS can't consume them).
// kEaseOut  ~ cubic-bezier(0.16,1,0.3,1)  -> QEasingCurve::OutQuint (closest builtin)
// kEasePull ~ cubic-bezier(0.32,0.72,0.24,1) -> custom QEasingCurve (set via addCubicBezierSegment helper, Task 3)
```

- [ ] **Step 5: Rewrite `darkBaselineNoir()`** (`Theme.cpp` :82-103) to assign the baked ladder above (bg0/surface/elevated/raised/text/textDim/muted/accent/onAccent/error + existing chrome slots derived from the ladder, e.g. `topbarBg`/`sidebarBg`/`cardBg` = `surface`/`elevated` at the existing alphas). Keep the OKLCH values in a comment.

- [ ] **Step 6: Update legacy gold constants** (`Theme.h` :48-55) — `kAccent = "#e8b923"`, `kAccentSoft = "rgba(232,185,35,0.22)"`, `kAccentLine = "rgba(232,185,35,0.40)"`, `accentForSection()` returns `QColor(0xe8,0xb9,0x23)` (load-bearing: `TileCard` reads `kAccent`).

- [ ] **Step 7: Add token-table entries** (`Theme.cpp` :910-927) — map `__SURFACE__→surface`, `__ELEVATED__→elevated`, `__RAISED__→raised`, `__ON_ACCENT__→onAccent`, `__ERROR__→error`. Leave the replace loop (:929-933) unchanged.

- [ ] **Step 8: Run the test, confirm PASS.** Run: `C:/tools/cmake-3.31.6-windows-x86_64/bin/ctest.exe --test-dir out -R HarborTokens -V`. Expected: 2/2 passed. (Register the test in `cmake/TankobanTests.cmake` per the addTestRecipe: add `tests/ui/test_theme_tokens.cpp` to the `tankoban_tests` `add_executable` list with `src/ui/Theme.cpp`; Theme is dep-free of libtorrent. Then `rm out/build.ninja` to force reconfigure.)

- [ ] **Step 9: Commit** — `git add -- src/ui/Theme.h src/ui/Theme.cpp tests/ui/test_theme_tokens.cpp cmake/TankobanTests.cmake && git commit` (msg `feat(harbor): design-token ladder + gold accent + radius scale (Phase 1 Task 1)`, Co-Authored-By trailer).

> Note: retinting `kTemplate` surfaces (`#050505`/`rgba(8,8,8,…)` → `__SURFACE__/__ELEVATED__/__RAISED__`) and the font-family split happen in Task 2 (after fonts exist) and Task 4 (visual verify). Task 1 establishes the tokens; the global look shift is verified live at the end of Task 4.

## Task 2: Bundle + register Fraunces (display) + Inter (body)

**Files:**
- Create: `resources/fonts/Fraunces-VariableFont.ttf`, `resources/fonts/Inter-VariableFont.ttf` (Hemanth/Agent supplies the .ttf files — OFL fonts; if absent, fetch from Google Fonts).
- Modify: `resources/resources.qrc` (before `</qresource>` :56); `src/ui/Theme.h` (decl), `src/ui/Theme.cpp` (impl + includes + font-family rule); `src/main.cpp` (:258-259).

- [ ] **Step 1: Add fonts to the qrc** — inside `<qresource prefix="/">`:

```xml
        <file>fonts/Fraunces-VariableFont.ttf</file>
        <file>fonts/Inter-VariableFont.ttf</file>
```

- [ ] **Step 2: Add `registerFonts()`** — decl in `Theme.h` (near the API block ~:123); impl in `Theme.cpp` (add `#include <QFontDatabase>`):

```cpp
void Theme::registerFonts() {
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Inter-VariableFont.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Fraunces-VariableFont.ttf"));
}
```

- [ ] **Step 3: Call it at startup** — `src/main.cpp` :258, immediately BEFORE `Theme::applyThemeFromSettings(app)`: `tankoban::ui::Theme::registerFonts();`

- [ ] **Step 4: Split the font-family rule** (`Theme.cpp` :300-302) — body stays sans, add a display family for titles. Change the `*` rule to `* { font-family: "Inter", "Segoe UI Variable", "Segoe UI", sans-serif; }` and add a display rule used by brand/section/hero titles, e.g.:

```cpp
// appended in kTemplate, applied by objectName
"QLabel#Brand, QLabel#SectionTitle, QLabel#HeroTitle { font-family: \"Fraunces\", Georgia, serif; }\n"
```

- [ ] **Step 5: Build + force-reconfigure** (qrc changed → AUTORCC needs a fresh pass): `rm out/build.ninja` then build. **Agent 0 builds + launches** and confirms the brand label renders in Fraunces serif + body in Inter (visual; `introspect-object QLabel#Brand` to confirm the widget exists). Expected: BUILD OK, app launches, serif brand visible.

- [ ] **Step 6: Commit** — explicit paths (`resources/fonts/*`, `resources/resources.qrc`, `src/ui/Theme.{h,cpp}`, `src/main.cpp`), msg `feat(harbor): bundle + register Fraunces/Inter fonts (Phase 1 Task 2)`.

## Task 3: `NavRail` widget (left rail)

**Files:**
- Create: `src/ui/widgets/NavRail.{h,cpp}`; register in `cmake/TankobanSources.cmake` (SOURCES :32-37 cluster, HEADERS :289-292).

Contract (header):

```cpp
// src/ui/widgets/NavRail.h
#pragma once
#include <QFrame>
#include <QString>
#include <vector>
class QVBoxLayout; class QPushButton;
namespace tankoban::ui {
class NavRail : public QFrame {
    Q_OBJECT
public:
    struct Item { QString id; QString label; QString iconPath; }; // id == pageId for modes
    explicit NavRail(QWidget* parent = nullptr);
    void setModes(const std::vector<Item>& modes);     // top group
    void setPages(const std::vector<Item>& pages);     // active-mode pages (middle group)
    void setCollections(const std::vector<Item>& cols);// bottom group (Library/Downloads/Settings)
    void setActiveId(const QString& id);               // gold-highlights the matching button
    void setCollapsed(bool collapsed);                 // 210 <-> 62 px, animated
    bool isCollapsed() const;
    QString devSnapshot() const;                       // dev-bridge introspection (mirror IDevInspectable)
signals:
    void itemActivated(const QString& id);             // any group click
    void collapseToggled(bool collapsed);
private:
    void rebuild();
    bool m_collapsed = false;
    QString m_activeId;
    std::vector<Item> m_modes, m_pages, m_collections;
    QVBoxLayout* m_layout = nullptr;
    std::vector<std::pair<QString, QPushButton*>> m_buttons;
};
} // namespace
```

- [ ] **Step 1: Implement `NavRail.cpp`** — `QFrame` objectName `"NavRail"`; a `QVBoxLayout` with: a brand `QLabel#Brand` (serif, gold, click → emit `itemActivated("__home__")`), the MODES group, a gradient-hairline divider (`QFrame#NavDivider`), the PAGES group, `addStretch(1)`, the COLLECTIONS group, a collapse `QToolButton` at the bottom (→ `setCollapsed(!m_collapsed)` + emit `collapseToggled`). Each group button: checkable `QPushButton` objectName `"NavRailButton"`, icon via `Theme::tintedSvgIcon`, text shown only when expanded; on click emit `itemActivated(id)`. `setActiveId` toggles a dynamic property `active` (true on the match) and re-polishes so the QSS gold-active rule applies. `setCollapsed` animates `maximumWidth` 210↔62 via `QPropertyAnimation` (duration 320, the kEasePull cubic-bezier built with `QEasingCurve` + `addCubicBezierSegment`), and hides/shows the labels. `devSnapshot()` returns a JSON-ish string: collapsed state, activeId, and the id list per group.

- [ ] **Step 2: Add NavRail QSS to `kTemplate`** (`Theme.cpp`) — `#NavRail { background: __SIDEBAR_BG__; }`, `QPushButton#NavRailButton { ... }`, active rule `QPushButton#NavRailButton[active="true"] { color: __ACCENT__; background: __RAISED__; }`, collapsed-active = icon gold only (no pill). Divider `#NavDivider { background: ... gradient hairline ... }`.

- [ ] **Step 3: Register in cmake** — add `src/ui/widgets/NavRail.cpp` to SOURCES, `src/ui/widgets/NavRail.h` to HEADERS, then `rm out/build.ninja`.

- [ ] **Step 4: Build-verify** — Agent 0 builds. Expected: BUILD OK; `NavRail.cpp.obj` present (new-source false-green guard). NavRail not yet shown (wired in Task 4) — this task only adds the component.

- [ ] **Step 5: Commit** — `feat(harbor): NavRail widget (Phase 1 Task 3)`.

## Task 4: Wire NavRail into MainWindow (the layout swap)

**Files:** Modify `src/ui/MainWindow.{h,cpp}`.

- [ ] **Step 1: Build the rail + collect items** — in the ctor after `buildTopBar()`/before/around `buildPageStack()`, construct `m_navRail = new NavRail(content)`. Populate modes from the EXISTING `navDefs` (MainWindow.cpp :535-554) — reuse verbatim so the mode set stays `{Theatre, Manga, Comics, Books}` (label order per the rail). Pages = the active mode's sub-pages (start with Theatre: Home + the existing per-mode Downloads page). Collections = Library/Downloads/Settings (wire Downloads/Settings to the existing handlers).

- [ ] **Step 2: Layout swap** — replace the vertical `contentLayout` (m_topBar over m_pageStack, :120-160) with: a slim top **chrome strip** (drag region + relocated `m_chromeMin/Max/Close` + the `CenterSearchBar` from Task 5) stacked over a `QHBoxLayout` holding `[m_navRail | m_pageStack(stretch=1)]`. Keep `m_glassBg` lowered behind. Do NOT touch `buildPageStack()` (:710-1039) or `activatePage()`'s `setCurrentIndex` (:1125-1155).

- [ ] **Step 3: Route rail → existing mode-switch contract** — connect `m_navRail::itemActivated(id)` to the SAME logic the pill lambda used (:566-591): `if (m_navController) m_navController->resetMode(id); if (id == m_activePageId) resetActivePageToRoot(); else activatePage(id);` Keep `activatePage()` updating the rail's active id (`m_navRail->setActiveId(pageId)`) where it currently checks the pill buttons (replace the `m_navButtons` check-sync at :1118-ish).

- [ ] **Step 4: Repurpose collapse** — point `m_hamburgerBtn` + the Ctrl+5 binding (:1056-1060) at `m_navRail->setCollapsed(...)` instead of `m_sidebar->toggle()`. (SidebarDrawer can remain for now for sources sub-pages; folding it fully into the rail is Phase 6.)

- [ ] **Step 5: Update the drag hit-test whitelist** — in `nativeEvent` WM_NCHITTEST (:2993-2999), replace the old caption objectNames (`m_brandLabel`,`TopNav`,`TopBarLeftSlot`,`TopBarRightSlot`) with the new chrome-strip objectName(s) so window drag still works. **Critical: skipping this breaks dragging.**

- [ ] **Step 6: Remove dead centering logic** — delete `mirrorTopBarSlotWidths()` (:693-707) and its call sites (:688, :1123) — the centered-pill mirror is meaningless with a rail.

- [ ] **Step 7: Build + LIVE SMOKE (Agent 0 builds+launches; Hemanth confirms)** — Expected: app opens with the left rail; clicking Theatre/Manga/Comics/Books switches modes exactly as before; collapse toggles 210↔62; window still drags + resizes; no blank pages. Verify structurally via `introspect-tree NavRail` (buttons present/visible) + Hemanth eyes-on.

- [ ] **Step 8: Commit** — `feat(harbor): left NavRail shell replaces top-pill nav (Phase 1 Task 4)`.

## Task 5: `CenterSearchBar` (window-centered frosted search)

**Files:** Create `src/ui/widgets/CenterSearchBar.{h,cpp}`; register in cmake; wire into MainWindow chrome strip.

- [ ] **Step 1: Implement the widget** — a frosted `QFrame#CenterSearch` (radius pill, `__SURFACE__` bg + border) containing a search-icon label + `QLineEdit#CenterSearchEdit` (placeholder "Search Theatre…", updated per active mode) + a `/` hotkey hint chip + clear button. Signals: `searchSubmitted(QString)`, `searchCleared()`. `devSnapshot()` for the bridge.

- [ ] **Step 2: Place it dead-center in the chrome strip** — added to the chrome strip with stretch on both sides so it stays window-centered regardless of rail width (mirror the reference's window-centered anchoring). Update placeholder when `activatePage` changes mode.

- [ ] **Step 3: Wire submit → the active page's search** — `searchSubmitted(q)` routes to the active page's search entry (reuse the dev-bridge routing pattern at MainWindow.cpp :2257-2266; each page already owns a search). Add a Ctrl+F shortcut (bindShortcuts :1042-1093) that focuses the bar.

- [ ] **Step 4: Build + LIVE SMOKE** — Agent 0 builds+launches; Hemanth confirms the centered search renders, `/` and Ctrl+F focus it, and submitting a query searches the active mode. `introspect-object QLineEdit#CenterSearchEdit` to confirm.

- [ ] **Step 5: Commit** — `feat(harbor): window-centered search bar (Phase 1 Task 5)`.

---

## Phase 1 Definition of Done

The app launches in the Harbor shell: dark OKLCH ladder + gold accent + Fraunces/Inter fonts applied globally; a collapsible left rail drives all four modes with page-switching and hierarchy-back intact; a window-centered search focuses via `/`/Ctrl+F and searches the active mode; window drag/resize/min/max/close all still work. Verified by Hemanth on the running app. This foundation is then inherited by Phase 2 (widgets) and the remaining Theatre phases.

## Self-Review

- **Spec coverage:** §3 tokens → T1; fonts → T2; §4 shell (rail + center search; frameless already done) → T3/T4/T5; reuse of PerModeNavController + navDefs + activatePage → T4. The hero/rows/cards (§5) + Theatre Home/Detail/Player (§6) are Phases 2–6 (separate plans), per the spec's phasing.
- **Placeholders:** none — each task has concrete file:line anchors, the token values, the NavRail contract, and exact integration edits.
- **Consistency:** `NavRail` API names (setModes/setPages/setCollections/setActiveId/setCollapsed/itemActivated) used consistently in T3 + T4; token names (`surface/elevated/raised/onAccent/error`, `kRad*`) consistent T1↔T3↔T5.
- **Verification honesty:** only Task 1 is unit-tested (dep-free token logic); the visual tasks are build + dev-bridge + Hemanth smoke, as the spec mandates.
