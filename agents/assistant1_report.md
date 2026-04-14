# Assistant 1 — Comic Reader Parity Audit
Date: 2026-03-25

## Summary

The C++ comic reader has a solid structural foundation covering all three reader modes (SinglePage, DoublePage, ScrollStrip), double-page pairing with auto-coupling, spread overrides, volume navigation, progress persistence, bookmarks, and keyboard shortcuts. However, compared to Tankoban-Max several significant behavioral gaps exist. The most critical are: (1) the HUD auto-hide logic is mode-unaware — Max uses a different strategy per control mode (pinned vs. auto-hide), our code collapses this to a single 3s timer regardless of mode; (2) the portrait/single-page rendering does not implement Max's no-upscale metric system — we scale to height unconditionally and can upscale small images; (3) the Two-Page Scroll mode (stacked pair rows, entry-sync hold) is entirely absent; (4) the Auto-Flip timer mode (configurable interval, countdown display) is entirely absent; (5) the Auto Scroll mode (rAF-based continuous scroll with speed levels) is entirely absent; (6) zoom and pan in double-page mode uses scroll-bar position math instead of the device-pixel canvas pan system Max uses, with no Y-axis pan; (7) the context menu is dramatically thinner than Max's — missing export, clipboard copy, open-in-new-window, image scaling quality selector, loupe controls, and bookmarks jump list; (8) page-pair rendering uses independent scale factors for each page instead of a unified minimum scale, causing height mismatches. There are also multiple missing overlays (Loupe, Image Filters, Speed Slider, Mega Settings panel) and several keyboard mappings absent.

---

## P0 — Critical (wrong behavior, broken features)

| # | Feature | Max behavior | Our behavior | Gap |
|---|---------|-------------|-------------|-----|
| 1 | HUD auto-hide: mode-aware pinning | Manual Scroll / Two-Page Flip / AutoFlip modes: HUD is **pinned** — click to toggle only, never auto-hides. Auto Scroll: YouTube-style auto-hide after 3s of inactivity. Separate `hudPinned` flag gates this. `hudNoteActivity()` called on every input event. | Single `m_hudAutoHideTimer` (3s, one-shot) used for all modes with no mode distinction. No pinned concept. | DoublePage mode (our main mode) hides the HUD after 3s even during active keyboard navigation, making the HUD unreliable. |
| 2 | Portrait / single-page no-upscale rendering | `getNoUpscaleMetrics`: cap `drawW = min(maxW, bmp.width * dpr)` — never upscale beyond native resolution. Portrait pages: `maxW = cw * portraitWidthPct`. Wide spreads: `maxW = cw` (full width). Scale derived from draw width. | `scaledToHeight(availH)` then optionally scale to fit `portraitWidthPct * availW`. No native-resolution cap. No DPR-aware ceiling. Spread detection uses the same path as portrait. | Small/low-res images get upscaled and appear blurry. Wide spreads are incorrectly constrained by the portrait-width cap instead of using the full viewport width. |
| 3 | No "Two-Page Scroll" mode | Full stacked-rows layout: all pages as left/right pairs in a continuous vertical stream. Entry-sync hold while rows build from start. Per-row smooth vertical scrolling. Vertical thumb reflects stream Y position (not page index). | Not implemented. The three modes are SinglePage / DoublePage / ScrollStrip. | Missing an entire control mode that is the primary two-page reading experience in Max. |
| 4 | No "Auto Flip" timer mode | Auto-flip mode: pages turn automatically on a configurable interval (5–600s). Countdown display in top-left. Space/Enter pauses timer. Play/Pause transport controls the timer. Auto-flip resets on any manual navigation. | Not implemented. | Missing a full control mode. |
| 5 | No "Auto Scroll" mode | Continuous auto-scroll with rAF `tick()` loop. Configurable speed levels 1–10 stored as `scrollPxPerSec`. Shift key = 2.5× speed, Ctrl = 0.2× speed. Space/Enter = play/pause. Comma/Period keys adjust speed. `showEndOverlay()` on reaching end. | Not implemented. No playback loop exists. | Missing the primary auto-reading mode. |
| 6 | Two-page zoom/pan system | Pan stored in **device pixels** (`twoPageFlickPanX`, `twoPageFlickPanY`). Applied as CSS coordinate offsets during canvas draw. Pan state keyed by a signature string (mode + imageFit + zoom + pair identity) — pan resets to edge/center snap on page change. Separate X and Y pan axes. Fit-Width mode enables vertical overflow pan. Drag-to-pan on center zone. | Pan stored as `m_panX` (logical scroll-bar integer). Only X axis. Applied via `hbar->setValue(m_panX)`. No signature-based reset on page turn. No Y-axis pan. | Horizontal pan only; no vertical pan for fit-width overflow. Pan value does not reset correctly when navigating to a new page (drifts). No device-pixel precision. |
| 7 | Two-page gutter: explicit gap between pages | `VIEW_CONSTANTS.TWO_PAGE_GUTTER_PX` (non-zero). Left half = `floor((cw - gutter) / 2)`, right half = `cw - gutter - leftW`. Gutter shadow rendered over this gap region. | `halfW = totalW / 2` — no gutter variable. Center line at exactly the midpoint with no gap. | Pages touch at the center line with no physical gutter. Visual quality of paired pages is worse. |
| 8 | Page-pair rendering: unified scale factor | `baseScaleR = min(scaleByWidth, scaleByHeight)` and `baseScaleL = min(scaleByWidth, scaleByHeight)` computed for each page, then `baseScale = min(baseScaleR, baseScaleL)` applied to **both** pages so they share the same height. | `rh = pixmapR.height() * scaleR` and `lh = pixmapL.height() * scaleL` computed independently. Pages can have different rendered heights. | In real manga volumes where paired pages have different native heights (scan imperfections, chapter headers), the two pages render at different heights — baseline misalignment. |
| 9 | Navigation coalescing / nav-busy guard | `appState.navBusy` + drain-loop: rapid navigation coalesces all queued requests; only the most recent target is decoded. Nav token system (`appState.tokens.nav`) discards stale decode results. | `m_currentVolumeId` guards against stale decodes from previous volumes. No navBusy coalescing for same-volume rapid navigation. Multiple rapid keypresses queue multiple `showPage()` calls which can decode and display intermediate pages. | Rapid navigation (holding arrow key) shows intermediate pages momentarily and can leave the cache in an inconsistent state. |

---

## P1 — Polish (visual differences, minor wrong behavior)

| # | Feature | Max behavior | Our behavior | Gap |
|---|---------|-------------|-------------|-----|
| 1 | HUD timer restart on key events | `hudNoteActivity()` called on every `keydown` event — resets the 3s auto-hide timer so the HUD stays visible during keyboard navigation. | `keyPressEvent` does not restart `m_hudAutoHideTimer`. Only mouse movement restarts it. | HUD disappears after 3s of keyboard-only navigation (arrow keys, Page Up/Down). |
| 2 | HUD freeze during scrub drag | `hudFreezeActive()` returns true while `appState.scrubDragging` is true — auto-hide timer cannot fire. | No equivalent freeze check. Scrub dragging does not extend the HUD timer. | HUD can hide mid-scrub, making the scrub bar disappear while the user drags it. |
| 3 | HUD freeze while hovering HUD/scrub | `hudHoverScrub` and `hudHoverHud` flags block auto-hide while mouse is over HUD or scrub area. | Only `m_toolbar->underMouse()` check in the timer lambda. Functionally similar but less precise. | Minor: hovering the scrub bar but not the toolbar container can still trigger auto-hide. |
| 4 | Overlay single-open rule | `closeOtherOverlays(except)` — opening any overlay closes all others first, carrying the `wasPlaying*` state. | No cascade-close. Multiple overlays can be open simultaneously. | Visual clutter; no defined single-open discipline. |
| 5 | Volume navigator: progress metadata | Each row shows "Continue · page N" and "Last read N days ago" from `progressAll`. Current volume marked with "Current" pill. | Plain `fi.completeBaseName()` only. No progress metadata, no last-read time, no current-volume indicator. | Volume navigator significantly less informative than Max. |
| 6 | Volume navigator: title with count | Title shows `"${series.name} · ${all.length} volumes"`. | Shows `m_seriesName` or "Volumes" with no volume count. | Minor visual gap. |
| 7 | Volume navigator: numeric search | Search supports "vol12" style — extracts numbers from query and matches book titles containing those numbers. | Only `contains(text, CaseInsensitive)` substring match. | Searching "12" doesn't find "Vol. 12" in our implementation if the spacing differs. |
| 8 | Context menu: right-click opens floater Mega Settings | Right-click on canvas opens Mega Settings overlay as a floater positioned 12px from cursor, clamped to viewport, opening directly on "tools" sub-panel. | Right-click opens a plain `QMenu`. No floater. No Mega Settings panel. | Completely different right-click UX. Our menu is much less capable. |
| 9 | Two-page cover alignment | Cover alone: drawn flush to the center line in the left half (`dxBase = leftW - dw`), centered vertically. Uses the gutter-aware `leftW` width. | `dx = totalW/2 - halfW` — attempts center alignment but uses `totalW` not gutter-split `leftW`. | Cover page is not precisely flush to the spine line when a gutter gap is introduced. |
| 10 | HUD page counter batching | `syncHudPageCounter`: forces immediate flush on discrete actions (force=true), otherwise batches at 100ms intervals using rAF + setTimeout to avoid DOM churn during rapid scroll. | `updatePageLabel()` called synchronously on every page change and every scrub drag event. | Excessive label redraws during fast scroll/scrub. Noticeably choppy on lower-end hardware. |
| 11 | Scroll position preserved across resize (strip) | For portrait strip: capture source image Y (`sYsrc = appState.y / scale_old`), recompute `appState.y = sYsrc * scale_new` after resize to keep the same image row visible. | `resizeEvent` for strip mode captures `fraction = vbar->value() / vbar->maximum()` and restores it — this is scroll-bar fraction, not image-source fraction. Slight position drift occurs when image scale changes significantly. | Minor visible jump in strip mode when resizing the window. |
| 12 | Scrub bar: bubble shows image preview | Max has infrastructure for thumbnail previews at scrub hover positions (`scrubBubbleW` tracking, re-sync on resize). | Bubble shows only the page number as text. | Acceptable for now but a gap vs. Max's future-proof infrastructure. |
| 13 | `instantReplay` behavior | Max: `instantReplay()` goes to previous page (in flip modes). Our strip implementation scrolls back 30% of the viewport height — which is the right behavior for strip mode but Max's replay is always "previous page". | In strip mode we scroll back 30% viewport. In single/double we call `prevPage()`. | Functionally reasonable but strip replay behavior diverges from Max. |
| 14 | End overlay: Replay button shows only page 0 | Max: `el.endReplayBtn.click()` → replay from page 0. | Our replay button calls `showPage(0)` — same behavior. | Correctly implemented. |

---

## P2 — Missing features (not implemented at all)

| # | Feature | Max has it | We have it? | Notes |
|---|---------|-----------|------------|-------|
| 1 | Auto Scroll mode ("auto") | Yes — rAF `tick()` loop, configurable speed levels 1–10 (`scrollPxPerSec`), Shift=2.5× speed, Ctrl=0.2× speed, Space/Enter=play/pause, Comma/Period = speed adjust, `showEndOverlay()` on completion | No | Entire control mode missing |
| 2 | Auto Flip mode ("autoFlip") | Yes — configurable 5–600s interval, countdown element in HUD, play/pause transport, timer resets on manual navigation | No | Entire control mode missing |
| 3 | Two-Page Scroll mode ("twoPageScroll") | Yes — stacked pair rows layout, continuous vertical stream, entry-sync hold while rows build, per-row smooth scroll, Y-based vertical thumb | No | Entire control mode missing |
| 4 | Mega Settings overlay | Yes — hierarchical settings panel (Speed, Mode, Width, Spreads, Progress, Bookmarks, AutoFlip interval, Image Fit, Tools sub-panels). Keyboard navigable with arrow/enter. Floater mode + corner mode. Opened by clicking settings button or pressing Ctrl+comma. | No | Entire settings system missing |
| 5 | Loupe / magnifier | Yes — L key toggles, configurable zoom 0.5–3.5×, overlay at cursor position, context menu toggle + zoom picker overlay (`openLoupeZoomOverlay`) | No | |
| 6 | Image Filters overlay (ImgFx) | Yes — brightness/contrast/saturation/hue sliders, per-series persisted | No | |
| 7 | Scroll speed slider overlay | Yes — opens from HUD speed button, Escape to close, keyboard navigable | No | |
| 8 | Export: Save current page to file | Yes — context menu "Export: Save current page…" via main-process IPC, no re-encode | No | |
| 9 | Export: Copy current page to clipboard | Yes — context menu "Export: Copy current page to clipboard" | No | |
| 10 | Open in new window | Yes — context menu "Open in new window" via `Tanko.api.window.openBookInNewWindow(book.id)` | No | |
| 11 | Image scaling quality selector | Yes — context menu shows Off / Smoother / Sharper / Pixel options applied to canvas `imageSmoothingQuality`; persisted per series | No | We always use `Qt::SmoothTransformation` with no user control |
| 12 | Bookmarks jump list in context menu | Yes — last 6 bookmarks listed in right-click menu for direct page jump | No | We have bookmarks (B key) and storage but no jump list in the menu |
| 13 | Bookmarks sub-panel in Mega Settings | Yes — `openMegaSub('bookmarks')` shows a dedicated bookmarks management view | No | |
| 14 | MangaPlus zoom sub-mode (twoPageMangaPlus) | Yes — additional Two-Page Flip variant with 100–260% zoom, separate X+Y pan via drag or arrow keys, reading-start edge snap on page turn | No | Sub-mode of Double-Page not present at all |
| 15 | Image Fit toggle for Two-Page Flip | Yes — "Fit Page" vs "Fit Width" per mode in Mega Settings, shown only in flip modes. Affects vertical overflow pan. | No | Our double-page always uses fit-page math; no user-selectable fit mode |
| 16 | Auto Flip interval setting | Yes — 5/10/15/20/30/45/60/90/120s options in Mega Settings sub-panel | No | |
| 17 | Single-page scroll sub-mode within Auto Scroll | Yes — `scrollMode: 'singlePage'` within auto-scroll has top/bottom hold phases, per-page discrete advance | No | Our SinglePage is a discrete flip mode, not an auto-scroll sub-mode |
| 18 | `twoPageMangaPlusNextOnLeft` navigation inversion | Yes — I key (in two-page flip modes) inverts which click zone / arrow key means "next". Toast shows current state. | Partial — I key currently calls `toggleReadingDirection()` (RTL/LTR swap). In Max, I key in two-page mode flips click-zone assignment without affecting layout. | Different semantic. Max: I = direction of navigation. Our: I = RTL/LTR layout. |
| 19 | `Ctrl+0` reset to defaults | Yes — resets all reader settings to `DEFAULTS` | No | |
| 20 | `Ctrl+R` force library rescan | Yes — global shortcut triggers `library.scan({force:true})` + `refreshLibrary()` | No | Library-level shortcut, not reader-specific, but present in Max's keyboard handler |
| 21 | `Ctrl+Q` close window | Yes — global shortcut | No | We have Escape for close-reader but no Ctrl+Q |
| 22 | `Ctrl+M` minimize window | Yes — global shortcut | No | |
| 23 | `formatTimeAgo` in volume navigator | Yes — "just now" / "N minutes/hours/days ago" for last-read display per volume | No | |
| 24 | `Continue Reading` touch on open | Yes — on `openBook`, `updatedAt` is immediately bumped and persisted before the decode completes so "Continue Reading" picks up the most recently opened volume even mid-session | No | We save progress after a 250ms delay (toast timer) by which point the user may have already navigated away |
| 25 | Series settings migration | Yes — if a series has no saved settings, seeds from the last-saved volume's settings object | No | We fall back silently to defaults |
| 26 | Two-Page Scroll: Up/Down arrow key smooth scroll | Yes — `queueTwoPageScrollSmooth(dy)` with `baseFrac=0.08` step in twoPageScroll, `0.12` in manual scroll. Shift key = 0.25 frac step. | Partial — Up/Down in double-page calls `vbar->setValue(±80)`. No smooth deceleration, no Shift modifier for larger step. | Not smooth. No Shift modifier. |
| 27 | Loupe/zoom overlay (L key) | Yes — L key toggles loupe, `syncLoupeEnabled()`, persisted in settings | No | |
| 28 | `Reveal volume in Explorer` in context menu | Yes — context menu option for both double-page and single-page modes | Partial — only in non-double-page mode's `else` branch. Missing from double-page context menu. | |
| 29 | `Copy volume path` in context menu | Yes — always available | Partial — same issue: only in non-double-page mode branch. Not in double-page context menu. | |
| 30 | Spread override tools in Mega Settings | Yes — "Mark this page as Spread / Normal / Reset all" accessible from Mega Settings > Spreads sub-panel (in addition to right-click in double-page mode) | Partial — only via right-click in double-page mode | |

---

## Confirmed parity (things that correctly match Max)

- Three reader modes present: SinglePage (discrete flip), DoublePage (paired pages), ScrollStrip (continuous vertical)
- Double-page canonical pairing algorithm: cover-alone, spread-alone, nudge/shift parity (extraSlots for stitched spreads) — logic matches Max's `getTwoPagePair` / `twoPageExtraSlotsBefore`
- Coupling nudge (P key): toggles normal/shifted phase, invalidates and rebuilds pairing, persisted in progress
- Auto-coupling detection: edge continuity cost sampling across candidate pairs, confidence threshold, resolves to normal or shifted phase
- Spread overrides: right-click menu to mark page as spread/normal, reset all, rebuild pairing on change
- Portrait width presets: 50/60/70/74/78/90/100% — exact same set as Max
- Volume navigator: opens with O key, live search filtering, arrow-key navigation, Enter to select, Escape to close, scrim dismisses
- End-of-volume overlay: shows next volume name, Next Volume / Replay / Back to Library buttons; Space/Enter/Right navigates forward
- Go-to-page dialog: Ctrl+G opens, Enter commits, Escape closes, scrim click dismisses
- Keys overlay (K key): shows shortcuts in two-column layout, Escape closes, gates other shortcuts while open
- Bookmarks (B key): toggle, persisted in progress JSON
- Session keys: Z = instant replay, R = clear resume, S = save checkpoint — all present and functional
- Volume token (`m_currentVolumeId`) to discard stale decoded pages on rapid volume switch
- Memory saver: 256MB vs 512MB budget, persisted in QSettings, toggled from context menu
- Gutter shadow: 4 presets (Off/Subtle/Medium/Strong) with matching alpha values (0.0/0.22/0.35/0.55), same as Max's shadow gradient math
- ScrubBar: styled track + fill + thumb, bubble showing page number on hover/drag, scrubRequested signal
- VerticalThumb: drag-to-scroll in strip mode, progress fraction, correct geometry
- Double-click center zone = toggle fullscreen
- Single-click center zone = toggle HUD with 220ms debounce for double-click disambiguation
- Left/right zone clicks = prev/next page; blocked with amber flash in strip mode
- Side nav arrows: appear on mouse proximity to left/right thirds, hidden in strip mode
- Series settings persisted in QSettings: portraitWidthPct, readerMode, couplingPhase, gutterShadow; restored on open
- `scrollFraction` saved/restored for scroll-strip resume across sessions
- Toolbar bottom-edge proximity trigger (60px) with 600ms cooldown to prevent rapid re-show
- Progress format compatibility: `page`, `pageCount`, `path`, `maxPageIndexSeen`, `knownSpreadIndices`, `knownNormalIndices`, `updatedAt`, `couplingMode`, `couplingPhase`, `bookmarks`, `finished`, `finishedAt`, `scrollFraction`

---

## Recommended fix order

Ranked by user-visible impact, feasibility, and dependency:

1. **HUD auto-hide: mode-aware pinning** — Add `m_hudPinned` flag. In DoublePage mode the HUD should be click-pinned (never auto-hides). Currently the HUD hides after 3s in the most-used mode, making it unreliable. Also add `m_hudAutoHideTimer.start()` in `keyPressEvent` so keyboard navigation resets the timer when the HUD is in auto-hide mode.

2. **No-upscale portrait rendering** — Implement a `noUpscaleMetrics` helper: compute `drawW = min(availW * portraitWidthPct/100, pixmap.width() * dpr)` for portrait pages; for spreads use `min(availW, pixmap.width() * dpr)`. Derive scale from drawW instead of scaledToHeight. This fixes both the upscaling bug and the spread-width bug.

3. **Unified scale factor for page pairs** — In double-page pair rendering compute `baseScale = min(baseScaleR, baseScaleL)` and apply the same scale to both pages. Three-line fix with significant visual improvement on real manga volumes.

4. **Gutter gap constant** — Define `constexpr int TWO_PAGE_GUTTER_PX = 8;`. Split viewport as `leftW = (availW - gutter)/2`, `rightW = availW - gutter - leftW`. Update all pair layout math. The gutter shadow code already exists; it just needs the gap to be real.

5. **HUD scrub-drag freeze** — Check `m_scrubBar` drag state in the `m_hudAutoHideTimer` lambda. Prevents the HUD from disappearing while the user is scrubbing. Two-line fix.

6. **Volume navigator: progress metadata** — Add "Continue · page N" and "last read N days ago" rows to each `QListWidgetItem` or replace with a custom delegate. Requires reading progress JSON per series entry from `CoreBridge`. High user-visibility, moderate effort.

7. **Context menu: Go-to-Page and file actions in all modes** — Move `showGoToDialog`, `copyPath`, and `revealInExplorer` actions out of the `else` branch so they appear in double-page mode as well. Currently those actions are absent when double-page mode is active.

8. **Two-page cover alignment fix** — Change cover-alone `dx` calculation to use the gutter-aware `leftW` (half of viewport minus gutter), not `totalW/2`. Ensures cover is spine-flush after gutter gap is introduced.

9. **Image scaling quality selector** — Add a setting (stored in series settings) for scaling quality: Off (no smoothing) / Smooth (SmoothTransformation) / Fast (FastTransformation). Expose in context menu. Applied in `displayCurrentPage` and `ScrollStripCanvas::paintEvent`.

10. **Auto Scroll mode** — Implement an `Auto` control mode with `QTimer`-driven scroll (or a `paintEvent`-driven loop), speed levels 1–10 mapped to pixel-per-second values, Shift/Ctrl speed modifiers, Space = play/pause, and `showEndOverlay()` on reaching the bottom of the last page. This is the largest item but is a fundamental Max feature that is completely absent.
