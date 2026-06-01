# PER_VIEW_CHROME_FIX_TODO

**Owner:** Agent 5 (Library UX + Theme)
**Authored:** 2026-05-02
**Status:** AUTHORED, awaiting Hemanth ratification → P1 kickoff
**Sequencing:** Depends on FRAMELESS_CHROME_FIX (MainWindow chrome cluster) landing first; carries that visual language onto the three takeover surfaces.

---

## 1. Why this exists

The frameless MainWindow chrome work (FRAMELESS_CHROME_FIX, in flight 2026-05-01 → 02) puts min/max/close in the MainWindow's TopBar. That row is fully covered when any of the three takeover surfaces (Video Player, Comic Reader, Book Reader) occupies the content area, leaving the user with no way to minimize, maximize, or close from inside a video, comic, or book.

Each takeover surface needs its own chrome integration. The three surfaces have different HUD architectures and lifecycles, so the integration is per-surface, not a single shared widget.

Hemanth verbatim brief (2026-05-02):

> When the video player is not in fullscreen, there is no min, max, close buttons due to the recently shifting to a window-less mode. Same problem with the book reader and comic reader. I want a really subtle transparent min max close buttons that triggers alongside the bottom hud, for comics and the video. For the book reader the min max close buttons can be part of the top hud, but it needs to at the extreme right where the traditional button for min, max, close is — that means pushing aside the existing elements in the hud to left to make space.

Follow-up clarifications same wake:
- "We don't need two min, max, close — the top right one will suffice." Chrome lives at the top-right corner of the canvas, not embedded in the bottom HUD strip itself.
- "Same baseline for all three. Transparent glass looking min, max, close symbols that appear with the huds of comic reader and video player. For the book reader, it should be part of the top HUD on the top right."

## 2. Goals (functional acceptance, 12 points)

**Video Player:**
1. Bottom HUD shows (mouse move) → chrome buttons fade in.
2. Bottom HUD auto-hides → chrome buttons fade out in lockstep.
3. Min / max-toggle / close all functional and route to MainWindow's chrome slots.
4. In fullscreen mode, no chrome rendered.
5. Doesn't obstruct subtitle area, top-edge frame content, or existing HUD elements.

**Comic Reader:**
6. Same fade-with-HUD sync as video player (bottom toolbar's `m_hudAutoHideTimer` lifecycle).
7. Min / max-toggle / close functional via MainWindow chrome slots.
8. Doesn't break existing comic-reader interactions (page nav, zoom, scrub bar, pinned mode).

**Book Reader:**
9. Top HUD includes min / max / close at the extreme right.
10. Existing top-HUD right-cluster icons shifted left cleanly — no overlap, no clipping.
11. Behavior at narrow window widths verified (responsive overflow handled before chrome cluster, never the other way around).
12. Min / max-toggle / close functional via QWebChannel BookBridge → MainWindow chrome slots.

## 3. Out of scope

- No redesign of any existing HUD beyond the book reader's left-shift of right-cluster icons.
- No reordering of comic-reader bottom-HUD buttons.
- No changes to reader interactions, video controls, or any other UI surface.
- No new color; SVG icons only per `feedback_no_color_no_emoji.md`.
- No bundling of unrelated polish — surgical to the three-chrome ask.
- The Continue Watching scroll arrows queued as Ship 2 in chat.md:3662 stay queued; they're a separate ship.

## 4. Visual language (locked)

- **3 buttons per surface:** minimize → maximize/restore → close, in that order. Reuses the existing chrome SVGs already in the working tree from FRAMELESS_CHROME_FIX (`resources/icons/chrome_min.svg`, `chrome_max.svg`, `chrome_restore.svg`, `chrome_close.svg`).
- **36×36 px hit target each** (matches MainWindow chrome cluster).

The chrome's visual treatment splits by visual context, not by surface — same principle, different execution where the chrome lives next to different neighbors.

### 4.1 Floating-over-canvas treatment (video player + comic reader)

These two surfaces have chrome floating over a canvas with no other icons nearby. The chrome must read as "of the canvas" without competing for attention with content the user is actually consuming.

- **Dark-glass look** with same baseline (revised 2026-05-02 mid-P3 after Hemanth verbatim "barely visible" verdict on the original light-tinted spec — manga pages are predominantly white, light-on-light failed; the dark-glass treatment below reads on both manga (light) and video (dark) canvases):
  - Backdrop plate: `rgba(20, 20, 24, 0.62)` rounded-rect (semi-opaque dark frosted plate, 6px radius).
  - Border: `1px solid rgba(255, 255, 255, 0.10)` (subtle outline gives the cluster definition without a hard edge).
  - Icon stroke: existing SVG `#c6c6c6` light gray (no per-state opacity changes — the icons are already subdued; visibility comes from the dark backdrop, not icon color).
  - On hover: button gets `rgba(255, 255, 255, 0.16)` overlay tint (slight lift). Close button hover shifts to `rgba(232, 17, 35, 0.85)` matching Windows Fluent convention.
  - Pressed: same as hover with a 1px inset (Qt default).
- **Fade duration 200ms**, matched to existing HUD fade.

**Glass-look implementation note** (see §6 Decisions D2): true backdrop-blur is non-trivial in Qt. We approximate via a semi-opaque dark plate plus a subtle white outline, which reads as "frosted glass over canvas" without paying the cost of real `QGraphicsBlurEffect` per-frame. The dark plate is the load-bearing decision — original light-tinted spec failed on manga pages.

### 4.2 Embedded-in-nav-row treatment (book reader only)

The book reader's top HUD already has 7 stroke-only SVG nav icons (book/library, search, font, theme, volume, headphones, fullscreen-toggle). Chrome buttons sit at the extreme right of that same row. To put glass-tinted chrome next to solid stroke icons would make the chrome look like it's visiting from another surface — wrong visual grammar.

- **Solid SVG, matching the existing 7 nav icons exactly:**
  - Same SVG stroke style (stroke-only, no fill, ~1.7px stroke width matching the existing icon set's convention).
  - Same icon size and hit target as the existing nav icons (verify exact dimensions during P5 exploration; typically ~24px viewBox at ~36×36px button).
  - Same rest opacity as the existing icons (read from the existing `.navBtn` / `.hudBtn` CSS values during P5 — match those exactly, don't redefine).
  - Same hover treatment as the existing icons (read existing `.navBtn:hover` rules), **except** the close button which gets the red Fluent accent `rgba(232, 17, 35, 0.85)` background tint on hover (Windows convention; orthogonal to the visual-context axis).
- **No glass tint, no backdrop blur, no low-alpha rest state.** Chrome reads as a natural extension of the existing nav icon row.
- **Window-state reflection:** Maximize button swaps icon (`chrome_max` ↔ `chrome_restore`) when the MainWindow's maximize state changes, mirroring the FRAMELESS_CHROME_FIX TopBar logic.

### 4.3 Cross-surface invariants

- 3 buttons in min → max/restore → close order on all three surfaces.
- Window-state reflection (max ↔ restore icon swap on MainWindow state change) on all three.
- Close button hover gets the red Fluent accent on all three (Windows convention).
- All three use the same SVG sprites (already in the working tree from FRAMELESS_CHROME_FIX). Stroke color is the only variable: white-stroke for floating-over-canvas, dark-stroke matching nav icons for embedded-in-nav-row.

## 5. Surfaces & strategy

### 5.1 Video Player — Qt-native overlay

**File:** `src/ui/player/VideoPlayer.{cpp,h}`

- New private widget class `ChromeOverlay` (or three `QPushButton`s in a `QHBoxLayout` inside a frameless `QFrame`), parented to the canvas (`m_canvas` or equivalent), top-right anchored with 8px inset.
- Resized + repositioned in `resizeEvent` to track the canvas's top-right corner.
- Visibility hooked to existing `m_subOverlay->setControlsVisible(true)` callsite at VideoPlayer.cpp:2878 → `m_chromeOverlay->fadeIn()`; `setControlsVisible(false)` at line 2939 → `m_chromeOverlay->fadeOut()`. No new timer needed — chrome rides the existing controls-visibility lifecycle.
- Hidden when `isFullScreen()` (the canvas reparents to fullscreen window; chrome stays with the windowed canvas only).
- Three new VideoPlayer signals: `requestMinimize()`, `requestToggleMaximize()`, `requestClose()`.
- MainWindow connects these signals to its existing chrome slots (the same slots FRAMELESS_CHROME_FIX wired for the TopBar buttons).
- Z-order: above subtitle overlay, above seekbar, below modal popovers (Subtitle / Audio / Filter / Brightness / Settings).
- Subtitle collision: top-anchored subtitles render below the chrome zone naturally (chrome occupies ~44px from top, subtitles default to safe-area which is below). If a user has shifted subtitles to a position that would collide, the chrome cluster wins z-order — chrome is interactive, subtitles aren't. Pattern D top-edge clipping (Agent 3's MAKE_MPV_SOLO Task 12 watch-item) is unaffected — chrome occupies a 36px-square zone in the top-right corner; Pattern D is about content clipping at the very top edge of the frame, distinct surface.

### 5.2 Comic Reader — Qt-native overlay

**File:** `src/ui/readers/ComicReader.{cpp,h}`

- Mirrors video player pattern. New `ChromeOverlay` widget parented to ComicReader, top-right anchored.
- Visibility hooked to `m_toolbar->show()` / `m_toolbar->hide()` callsites + the `m_hudAutoHideTimer` (3s, pinned-mode-aware) lifecycle. Where `m_toolbar` shows, chrome shows; where it hides, chrome hides.
- **Hover-on-chrome exemption:** the existing auto-hide gates already check `m_toolbar->underMouse()` to keep the HUD alive while cursor hovers it. Add a parallel check `m_chromeOverlay->underMouse()` so cursor parking on chrome keeps both alive together. Same pattern for the cursor-auto-hide guard at ComicReader.cpp:417-418.
- Pinned-mode: when `m_hudPinned` is true (HUD never auto-hides), chrome also stays visible — they share the same lifecycle.
- Three new ComicReader signals: `requestMinimize()`, `requestToggleMaximize()`, `requestClose()`. Wired to MainWindow chrome slots.
- Hidden when `isFullScreen()`.
- Doesn't collide with existing toolbar elements — chrome is overlaid on the canvas, not embedded in the 78px-tall `ComicReaderToolbar`.

### 5.3 Book Reader — HTML/JS via QWebChannel bridge extension

**Files:** `src/ui/readers/BookBridge.{h,cpp}`, `src/ui/readers/BookReader.{cpp,h}`, `resources/book_reader/styles/overhaul.css`, `resources/book_reader/domains/.../reader_core.js` (or wherever the existing `.navHeader` right-cluster is built — to be confirmed during P3 exploration).

**Bridge extensions:**
- New signal: `void windowMinimizeRequested();`
- New signal: `void windowMaximizeToggleRequested();`
- New `Q_INVOKABLE void windowMinimize();` — emits `windowMinimizeRequested()`.
- New `Q_INVOKABLE void windowToggleMaximize();` — emits `windowMaximizeToggleRequested()`.
- New `Q_INVOKABLE bool windowIsMaximized() const;` — reads MainWindow state via the existing `setFullscreen` / cached state mechanism, so JS can render the correct max/restore icon at boot and on resize.
- New signal: `void windowMaximizeChanged(bool isMax);` — emitted whenever MainWindow's max state changes, so JS can swap the max/restore icon live.
- `BookReader.cpp` connects `windowMinimizeRequested` → MainWindow's minimize slot, `windowMaximizeToggleRequested` → MainWindow's max-toggle slot. The existing `closeRequested` → BookReader signal re-emit wiring at BookReader.cpp:64 is the precedent shape (BookBridge → BookReader → MainWindow chain).
- `MainWindow::changeEvent(QEvent::WindowStateChange)` already fires for FRAMELESS_CHROME_FIX's TopBar icon swap — extend its handler to call into BookReader (if open) so it can `bridge->windowMaximizeChanged.emit(isMax)`.

**HTML/CSS:**
- 3 new icon `<button>` elements appended to the right cluster of `.navHeader`, classed `.chromeBtn`. Sit at the extreme right; the 7 existing icons (book/library, search, font, theme, volume, headphones, fullscreen-toggle) shift left as a contiguous group.
- New SVG sprites added to `resources/book_reader/icons/` (or wherever the reader's icons live; existing convention scoped) — chrome_min, chrome_max, chrome_restore, chrome_close. Same shapes as the Qt-side SVGs.
- `.chromeBtn` styling per §4.2: solid stroke-only SVG matching the 7 existing nav icons exactly. Read the existing `.navBtn` / `.hudBtn` CSS rules at P5 kickoff and inherit them — don't redefine baseline opacity, hover tint, padding, or stroke color. The only `.chromeBtn`-specific rule is the close button's hover variant: `rgba(232, 17, 35, 0.85)` background + white stroke (Fluent convention). No `backdrop-filter`, no glass tint — chrome must read as part of the nav row.
- Max/restore icon swap: JS subscribes to `bridge.windowMaximizeChanged` and toggles the icon's `<use href>` (or class) accordingly.
- Narrow-width behavior (CSS): below `~720px` window width, the existing right-cluster icons (volume, headphones — the most "optional" ones) tighten horizontal padding from `8px` → `4px`. Below `~620px`, volume + headphones hide via media query (still accessible via the hamburger menu / settings panel where they already exist as redundant). The chrome cluster never shrinks or hides — it has the same priority as the close button on the MainWindow TopBar.

**JS shim:**
- New tiny module (or appended to existing `reader_core.js`) that wires the 3 new buttons:
  - `chromeMin.onclick = () => window.bridge.windowMinimize()`
  - `chromeMax.onclick = () => window.bridge.windowToggleMaximize()`
  - `chromeClose.onclick = () => window.bridge.requestClose()` (reuses existing `requestClose` — already wired)
- Subscribe to `bridge.windowMaximizeChanged.connect(isMax => updateMaxIcon(isMax))` on init; call `bridge.windowIsMaximized()` once at startup to set the initial icon.

## 6. Decisions

**D1. Placement: top-right corner of canvas, sync with HUD.** Locked by Hemanth 2026-05-02 ("the top right one will suffice"). Rules out embedded-in-bottom-HUD-strip placement for video + comic.

**D2. Glass approximation applies only to video player + comic reader (the floating-over-canvas surfaces).** Rationale: Qt has no native backdrop-blur; `QGraphicsBlurEffect` paints per-frame and tanks GPU. A `0.06`-alpha tint reads as glass against typical canvas content (dark video / comic page) at minimal cost. Locked.

**D3. Visual treatment splits by visual context, not by surface (corrected 2026-05-02 mid-authoring after Hemanth caught the contradiction).** Floating-over-canvas (video + comic) uses dark-glass: `rgba(20, 20, 24, 0.62)` semi-opaque plate + `rgba(255, 255, 255, 0.10)` border + existing `#c6c6c6` icon stroke; hover overlay `rgba(255, 255, 255, 0.16)`. (Revised mid-P3 from `0.06` light-tint baseline after Hemanth flagged "barely visible" on manga.) Embedded-in-nav-row (book reader) uses solid SVG inheriting the existing `.navBtn` / `.hudBtn` rest + hover values exactly — no chrome-specific opacity ramp, chrome must look like part of the row. Common across both treatments: min→max→close order, max/restore icon swap, close-button hover Fluent red `rgba(232, 17, 35, 0.85)`.

**D4. Window-state event wiring: extend the existing `changeEvent(WindowStateChange)` handler that FRAMELESS_CHROME_FIX added.** Don't author a separate maximize-watcher. Single source of truth → all three surfaces get notified consistently.

**D5. Bridge signal naming.** Request signals (JS → Qt → MainWindow) use the `window…Requested` suffix, matching the existing `closeRequested` shape: `windowMinimizeRequested`, `windowMaximizeToggleRequested`. State-change notifications (Qt → JS) use the `…Changed` suffix: `windowMaximizeChanged(bool isMax)`. Two-way bridge stays internally consistent.

**D6. Continue Watching scroll arrows (Ship 2 of FRAMELESS_CHROME_FIX-era queue) stays queued, not bundled here.** Different surface, different scope — keep this PR surgical to the three-chrome ask.

**D7. Smoke discipline: each surface ships with its own MCP smoke run (Rule 19 LOCK).** Eyes-on-screen verification of all 12 acceptance points across three sessions. No bundling of smokes; one phase = one ship = one smoke = one RTC.

## 7. Phases

**P1 — Visual language scaffolding (~1 summon, no UI changes yet)**
- Define the QSS for `.chromeBtn` (Qt) + the matching CSS class (book reader) at one shared color/opacity baseline, exposed via Theme.cpp tokens so future rebrand stays single-source.
- No widget instantiation yet. Compile-only verification.
- Deliverable: Theme.cpp + Theme.h additions, no `.cpp` consumers yet.
- Smoke: BUILD OK only.

**P2 — Video Player chrome overlay (~1-2 summons)**
- Implement `ChromeOverlay` widget (or inline 3-button frame) in VideoPlayer.cpp.
- Hook show/hide to existing `setControlsVisible` callsites.
- Wire signals to MainWindow.
- Fullscreen-hide gate.
- Deliverable: VideoPlayer.{cpp,h} + MainWindow.{cpp,h} signal wiring.
- Smoke: MCP launch, play any library file, mouse-move shows HUD + chrome together; mouse-out hides both; F11 fullscreen → no chrome; Esc → windowed → chrome restored. Click each chrome button → window state changes correctly.

**P3 — Comic Reader chrome overlay (~1 summon)**
- Mirror P2 pattern in ComicReader.cpp.
- Hook to `m_toolbar->setVisible` + `m_hudAutoHideTimer`.
- Hover-on-chrome exemption.
- Deliverable: ComicReader.{cpp,h} + MainWindow signal wiring (extend existing).
- Smoke: MCP launch, open a comic, mouse-move → HUD + chrome appear together; auto-hide → both fade; pinned mode → both stay; click each chrome button → correct.

**P4 — Book Reader bridge extension (~1 summon)**
- Add 3 new `Q_INVOKABLE`s + 3 new signals to BookBridge.
- Wire BookReader.cpp signal connections.
- Extend MainWindow `changeEvent` to push state into open BookReader.
- Deliverable: BookBridge.{cpp,h} + BookReader.{cpp,h} + MainWindow.cpp.
- Smoke: BUILD OK + tankoctl ping. No UI yet (P5 lights it up).

**P5 — Book Reader top-HUD chrome buttons (~1-2 summons)**
- Add `<button>` elements + SVG sprites to book reader resources.
- Add `.chromeBtn` CSS to overhaul.css.
- Wire JS shim.
- Implement narrow-width media queries.
- Deliverable: 4 SVGs + overhaul.css + the JS module touch + qrc updates.
- Smoke: MCP launch, open a book, top HUD shows chrome at extreme right; existing icons shifted left; click each chrome button → window responds; resize window narrow → volume + headphones tighten / hide before chrome touches them; max icon swaps when window state changes via taskbar / Win+Up.

**P6 — Cross-surface integration smoke (~1 summon)**
- End-to-end: launch Tankoban, open a video, minimize via chrome, restore via taskbar, close → back to library; open a comic, same matrix; open a book, same matrix; verify no MainWindow chrome conflicts (TopBar chrome is hidden when a takeover surface is up — this is FRAMELESS_CHROME_FIX's concern, but verify here).
- All 12 functional acceptance points hit.
- RTC bundles closure of the whole arc.

Phase count: **6 phases, ~6-8 summons**. Conservatively 1.5 wakes of work given the bridge + JS + CSS surface in P4+P5 typically takes longer than Qt-native widgets.

## 8. Risks

**R1 — Pattern D top-edge clipping (Agent 3's MAKE_MPV_SOLO Task 12 watch-item).** Chrome occupies the top-right ~44×44px of the canvas. If Pattern D resurfaces (windowed video clipping at top edge on Vinland / Boys), chrome overlay would also be visually clipped. Mitigation: chrome is anchored *inside* the canvas widget, not the parent — clipping affects content but not chrome positioning. If Hemanth flags top-edge clipping during validation, escalate to Agent 3's Task 12 triage queue, not chrome regression.

**R2 — Subtitle collision in video player.** Some users position subtitles at top of frame (rare but possible via the position slider Agent 3 shipped). Chrome z-orders above subtitle overlay. Mitigation: this is by design — chrome is interactive, subtitles aren't. If a user complains, suggest moving subtitle position. No code change.

**R3 — BookReader HUD might not actually be permanent.** Verify at P4/P5 kickoff. If the book reader's top HUD also fades (under some setting or scroll behavior), the `.chromeBtn` CSS may need to share the fade — adjust strategy at P5 kickoff and update this TODO.

**R4 — JS bridge signal latency.** QWebChannel signals can take 10-50ms round-trip. Chrome button click → MainWindow state change might feel slightly laggy in the book reader vs Qt-native surfaces. Mitigation: this is the same latency `requestClose` already has and Hemanth hasn't flagged it. If perceptible, optimize via direct synchronous Q_INVOKABLE call instead of signal-emit pattern.

**R5 — Visual cohesion within each treatment.** Two visual treatments (glass for video+comic; solid SVG for book reader). Drift risk within each: (a) Qt-side video and comic chrome must match each other exactly — P1 scaffolds the glass-look tokens centrally in Theme.cpp so both consumers read the same values; (b) book reader chrome must match the existing nav-row icons exactly — P5 inherits existing `.navBtn` / `.hudBtn` CSS rather than redefining. Side-by-side smoke at P6 catches both kinds of drift. Cross-treatment difference (glass vs solid) is intentional per D3 and is not drift.

**R6 — Cross-agent build collision.** Agent 3's MAKE_MPV_SOLO Task 12 is in operational soak — no code changes — so `out/` is free. But Hemanth may have Tankoban running for soak. Mitigation: confirm Tankoban closed before each P-smoke build (per FRAMELESS_CHROME_FIX precedent); MCP LOCK per Rule 19 around each smoke.

## 9. Files touched (predicted)

**P1:**
- `src/ui/Theme.h` — chrome-overlay color/opacity token decls
- `src/ui/Theme.cpp` — token definitions, QSS string

**P2:**
- `src/ui/player/VideoPlayer.h` — m_chromeOverlay member, 3 new signals
- `src/ui/player/VideoPlayer.cpp` — overlay creation, fade hooks, fullscreen gate
- `src/ui/MainWindow.h` — slot decls if not already present from FRAMELESS_CHROME_FIX
- `src/ui/MainWindow.cpp` — signal connection on VideoPlayer instantiation

**P3:**
- `src/ui/readers/ComicReader.h` — m_chromeOverlay member, 3 new signals
- `src/ui/readers/ComicReader.cpp` — overlay creation, fade hooks, hover exemption
- `src/ui/MainWindow.cpp` — signal connection on ComicReader instantiation

**P4:**
- `src/ui/readers/BookBridge.h` — 3 new Q_INVOKABLEs + 3 signals
- `src/ui/readers/BookBridge.cpp` — implementations
- `src/ui/readers/BookReader.h` — connect-on-init helpers
- `src/ui/readers/BookReader.cpp` — bridge signal → MainWindow slot wiring
- `src/ui/MainWindow.cpp` — extend changeEvent to push state into open BookReader

**P5:**
- `resources/book_reader/icons/chrome_{min,max,restore,close}.svg` (NEW) — or `resources/icons/` if already shared with the Qt path; resolve at P5 start
- `resources/book_reader/styles/overhaul.css` — `.chromeBtn` styling, narrow-width media queries
- `resources/book_reader/domains/.../reader_core.js` (or equivalent — locate during P3 exploration) — 3 button click wires + maximize-state subscription
- `resources/book_reader/ebook_reader.html` — 3 new `<button>` elements in the navHeader right cluster (or via JS DOM injection if the cluster is built dynamically; confirm at P5 start)
- `resources/resources.qrc` — register new SVGs if .qrc-served

**P6:** none beyond smoke artifacts.

## 10. Reference cites

- **FRAMELESS_CHROME_FIX (in-flight 2026-05-01 → 02)** — establishes chrome visual language, MainWindow chrome slots, `changeEvent(WindowStateChange)` icon-swap pattern. This TODO carries that forward onto takeover surfaces.
- `src/ui/player/VideoPlayer.cpp:2878,2939` — `setControlsVisible(true/false)` callsites (the show/hide trigger to hook).
- `src/ui/readers/ComicReader.cpp:417-418, 423-431` — `m_hudAutoHideTimer` lifecycle + `underMouse()` hover guards (the comic reader equivalent).
- `src/ui/readers/BookBridge.h:67-72` — existing window/nav signals (`closeRequested` precedent).
- `feedback_no_color_no_emoji.md` — strictly gray/black/white UI, SVG icons only.
- `feedback_css_scoping.md` — QSS by `#ObjectName`; CSS in readers scoped to avoid leakage.
- `feedback_player_minimalism_pattern.md` — UI-clutter aversion. "Really subtle" is the watchword.
- `feedback_qt_vs_electron_aesthetic.md` — the glass approximation gap is one instance of the broader "Qt structurally can't replicate Electron CSS" pattern; we accept the small drift here.
- `feedback_one_fix_per_rebuild.md` — one phase per rebuild, no batching.
- `feedback_fix_todo_authoring_shape.md` — this TODO's section shape.

## 11. Smoke spec — per phase

Each P2-P5 phase runs an MCP smoke under Rule 19 LOCK. Specific clicks documented in §7 above. Smoke artifacts (screenshots) attached to RTC. P6 is the cross-surface integration pass — runs all 12 functional acceptance points end-to-end, posts evidence in RTC body.

## 12. Success criteria

The arc closes when all 12 §2 functional acceptance points pass on Hemanth's smoke (or MCP-driven smoke for the mechanical points + Hemanth confirms the visual feel). RTC closing P6 contains:
- Evidence of all 12 points hit
- One screenshot per surface (video player chrome visible + hidden; comic reader same; book reader chrome visible at extreme right of top HUD)
- Verification that FRAMELESS_CHROME_FIX MainWindow chrome still works on the library surface (no regression introduced by signal-wiring)
- File list across all 6 phases
- Skills-invoked block per contracts-v3

## 13. Open questions (none — all resolved at authoring time)

All five brainstorm questions resolved:
- Q1 placement → top-right corner of canvas, sync with HUD (Hemanth verbatim)
- Q2 sync trigger → hook existing show/hide signals; no shared opacity animator (agent call, locked in §5.1+5.2)
- Q3 bridge → extend BookBridge with 3 new IPCs + 1 state signal (agent call, locked in §5.3)
- Q4 narrow-width book reader → volume + headphones tighten then hide before chrome (locked in §5.3)
- Q5 visual style → split by visual context: glass-look on floating-over-canvas surfaces (video + comic); solid SVG matching existing nav-row icons on book reader (Hemanth caught the original "single shared baseline" framing as inconsistent — chrome must match its visual neighbors)

## 14. Rollback / risk safety

This work is purely additive — no existing UI is removed or behaviorally changed beyond the book-reader nav icon shift. If Hemanth wants to back out a phase mid-arc, each phase's commit can be reverted independently (P1 → P5 ordering means later phases depend on earlier; reverting P5 leaves P1-P4 functional but invisible until P5 lands). P6 closes the arc; reverting P6 alone reverts only the cross-surface smoke artifacts.

---

End of TODO.
