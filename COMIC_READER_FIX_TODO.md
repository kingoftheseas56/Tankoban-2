# Comic Reader Fix TODO — Mihon-primary identity with YACReader polish overlay

**Owner:** Agent 1 (Comic Reader). Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this doc + `agents/audits/comic_reader_2026-04-15.md` as co-objective. Cross-agent touches flagged per phase.

**Created:** 2026-04-16 by Agent 0 after Agent 7's comic reader audit + Agent 1's observation-only validation pass (chat.md:13308-13358).

## Context

Congress 1 (comic reader parity) and Congress 4 Track B (library UX) shipped successfully. Core open / read / navigate / continue loop is intact. What's left is **feature-presence and UX polish** at the Qt native reader layer — what a user of Mihon or YACReader would notice missing or feeling thin in our current ComicReader widget.

Agent 7's audit at `agents/audits/comic_reader_2026-04-15.md` identified 0 P0s, 10 P1s, 3 Tankoban-Max regressions, and 9 P2s. Zero-P0 count is truthful — Congress 1/4 legitimately closed the broken-read-loop class of problem. What remains is the Qt reader lagging behind modern manga/webtoon readers (Mihon) and desktop polish readers (YACReader) on: reading-mode breadth + persistence, webtoon tuning, zoom/fit, image filters, thumbnail seek surfaces, bookmark UX, consolidated settings, error typing, archive format parity, mode-pick ergonomics.

The audit also surfaced three literal regressions from our own prior art (Tankoban-Max): image-filter sliders + presets, consolidated Mega Settings surface, loupe/magnifier overlay. Per `feedback_reader_rendering` memory ("Tankoban-Max is source of truth for reader UX, match 1:1") these are high-priority regressions, not external-wishlist items.

**Identity direction locked by Hemanth 2026-04-16:** **Mihon primary, YACReader for polish.** Priority favors Mihon's reading-mode breadth (LTR/RTL/Vertical/Webtoon/Continuous pagers), webtoon tuning, categorized reader settings, reading-mode direct-pick dialog, color-filter affordances. YACReader provides polish overlay: magnifying glass, GoToFlow-flavored thumbnail options, brightness/contrast/gamma metadata persistence, broad CBR/RAR discovery. OpenComic and MComix are consult-only. Tankoban-Max regressions get re-shipped either way — they're our own prior art.

**Scope:** 10 phases, ~26 batches. Phase 1 establishes reading-mode breadth foundation. Phases 2-6 ship per-feature surfaces. Phase 7 consolidates them into a Mega-Settings-equivalent pane per TKMAX-2. Phases 8-10 close error/format/pick/naming polish. Each phase stands alone as a shippable user-visible win; consolidation in Phase 7 retrofits per-feature widgets into a unified surface without double-work (per-feature widgets are designed as self-contained blocks from Phase 1 onwards).

## Objective

After this plan ships, a comic-reader user can:
1. Pick between LTR pager, RTL pager, vertical pager, webtoon, and continuous vertical reading modes — per-series persisted, direction-aware wheel/click semantics, Mihon-style icon-row dialog (`M` long-press / dedicated shortcut).
2. Read webtoons with side-padding, crop-borders, and split-double-page tuning specific to the webtoon mode.
3. Zoom/fit with all of Fit Page, Fit Width, Fit Height, Actual Size (1:1), Stretch, Smart Fit, plus zoom-slider + zoom ±, in every paged mode (not only double-page). Summon a YACReader-style magnifying glass for region-level detail.
4. Adjust brightness, contrast, saturation, sepia, hue, invert, grayscale, and apply filter presets, with persistence per series (TKMAX parity).
5. Seek by page via a Mihon-style page-slider with thumbnails, not just numeric go-to.
6. Manage bookmarks through a dedicated panel (thumbnails, labels, notes, delete) — not a 6-item right-click submenu.
7. Reach every reader control through a single consolidated Mega-Settings-equivalent pane (TKMAX-2) — scattered toolbar / context-menu / K-overlay / popup surfaces retire into it.
8. See typed error messages for password-protected, corrupt, unsupported-format, and missing-image failures — not generic "No image pages found."
9. Open `.cbr` and `.rar` files from SeriesView / ComicsPage / continue-strip — matching what ArchiveReader already supports engine-side.
10. See the reader's volume/chapter naming honor the SeriesView Volumes/Chapters toggle (cross-agent polish).

## Non-Goals (explicitly out of scope for this plan)

- Archive parsing internals (ArchiveReader implementation details) — engine stability already shipped; plan only exposes typed failure reasons up the stack (Phase 8).
- Book reader / video player / stream reader work — separate fix TODOs active.
- Tankoyomi source/download flow — Agent 1's secondary scope at best; not touched here.
- Mobile-only gestures (pinch-zoom trackpad etc.) — desktop-first reader; gesture set stays keyboard + mouse + wheel.
- Web/PWA patterns (OpenComic-style browser-reader parity).
- OCR / translation features — product direction not set.
- Congress 1/4 progress/continue data-shape exceptions — scroll-strip `scrollFraction` is settled, do not re-open.
- YACReader's broad archive set beyond CBR/RAR (7Z, CB7, TAR, ARJ, CBT) — deferred as a later polish sub-phase; Phase 9 caps at CBR/RAR MVP to match our engine.
- Mihon's online catalog / extension-based source browsing — Tankoyomi territory, not reader.
- Cross-device sync / Trakt / MyAnimeList — product direction not set.
- Multi-library roots reconciliation — out of this plan's reach; handled at library-scanner level.
- Any work outside `src/ui/readers/*`, `src/core/ArchiveReader.*`, plus minimally-scoped touches into `src/ui/pages/SeriesView.*`, `src/ui/pages/ComicsPage.*`, `src/core/ScannerUtils.*` where format-parity demands (Phase 9).

## Agent Ownership

All batches are **Agent 1's domain** (Comic Reader). Primary files: `src/ui/readers/*`.

**Cross-agent touches flagged per phase:**
- **Phase 9 (Format parity)** touches `src/ui/pages/SeriesView.*` + `src/ui/pages/ComicsPage.*` + possibly `src/core/ScannerUtils.*` — these are Agent 5's territory (library UX) per `feedback_agent5_scope`. Agent 1 coordinates heads-up in chat.md before the edit.
- **Phase 10 (Volume/chapter naming consistency)** touches the naming-policy linkage between SeriesView (Agent 5) and ComicReader (Agent 1). Either agent can ship; Agent 0 brokers the touch.
- **Phase 8 (Error typing)** adds an `ArchiveReader::OpenResult` / `DecodeReason` enum in `src/core/ArchiveReader.h`. Shared engine file — Agent 1 coordinates heads-up; no other consumer in-tree today.

Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this plan + the audit as co-objective.

---

## Phase 1 — Reading mode breadth + persistence

**Why:** Audit P1-1 CONFIRMED BROKEN (chat.md:13314). Current `ReaderMode` enum is 3-valued at `ComicReader.h:75` (SinglePage / DoublePage / ScrollStrip). Mihon's reading-mode enum covers Default / LtrPager / RtlPager / VerticalPager / Webtoon / ContinuousVertical (`Reader\setting\ReadingMode.kt:14-79`). `m_rtl` flips in-memory only, double-page-gated, not persisted. `saveSeriesSettings` whitelist at `ComicReader.cpp:2854-2872` covers only portraitWidthPct / readerMode / couplingPhase / gutterShadow / scalingQuality — no RTL, no fit mode, no zoom, no future mode state.

This phase is the foundation: enum expansion + persistence whitelist expansion + direction-aware wheel/click semantics. Every later phase piles onto this infrastructure.

### Batch 1.1 — ReaderMode enum expansion + migration

- Expand `ReaderMode` enum at `ComicReader.h:75` to Mihon's six values: `SinglePage` (default LTR pager — audit's "LtrPager" equivalent), `DoublePage` (two-page paged), `RtlPage` (single-page RTL pager — new), `VerticalPager` (one-page-at-a-time vertical — new), `Webtoon` (continuous vertical with webtoon tuning — renamed from `ScrollStrip` semantically but wire-compatible), `ContinuousVertical` (continuous vertical WITHOUT webtoon tuning — separate from `Webtoon` per Mihon).
- Rename `ScrollStrip` → `Webtoon` in the enum values; keep storage int matching (Webtoon == 2) so existing persisted `readerMode` values load cleanly. Add `ContinuousVertical = 3`, `RtlPage = 4`, `VerticalPager = 5`.
- Existing cycle key `M` (`cycleReaderMode` at `ComicReader.cpp:1476-1526`) temporarily cycles through all six modes — Phase 10 replaces this with direct-pick dialog.
- Toolbar mode-button label maps per new enum (single glyph per mode — "1", "2", "R", "V", "W", "C" or per-mode SVG).
- **Files:** `src/ui/readers/ComicReader.h`, `src/ui/readers/ComicReader.cpp`.
- **Success:** open series saved with old `readerMode=0/1/2` → loads as SinglePage/DoublePage/Webtoon respectively. Cycle key `M` walks all six modes. No crash, no UI flicker on existing user libraries.

### Batch 1.2 — Persistence whitelist expansion

- Extend `saveSeriesSettings` at `ComicReader.cpp:2854-2872` to persist: `m_rtl`, `m_fitMode`, `m_zoomPercent` (when > 100), Webtoon-specific tuning keys (deferred defaults in Batch 1.2 — actual widgets in Phase 2).
- Extend `applySeriesSettings` at `ComicReader.cpp:2874-2913` to read same keys with graceful defaults when absent (backwards-compat).
- Centralize the whitelist into a named constant or free helper so future additions have a single truth site — prevents the scatter the audit flagged in P1-1 / P1-7.
- `toggleReadingDirection` at `ComicReader.cpp:1683-1689` calls `saveSeriesSettings` on flip.
- **Files:** `src/ui/readers/ComicReader.h`, `src/ui/readers/ComicReader.cpp`.
- **Success:** open series → flip RTL via `I` → close → reopen → RTL still active. Same for fit mode, zoom level. Old series files (no new keys) open without error at their defaults.

### Batch 1.3 — Direction-aware wheel/click semantics

- `wheelEvent` at `ComicReader.cpp:2353-2387` currently treats LTR; extend to flip prev/next page semantics when `m_rtl == true` AND mode is a pager mode. Reference: Mihon `presentation\reader\components\ChapterNavigator.kt:45-148` (RTL swaps semantics).
- Click-zone semantics at `ComicReader.cpp:2389-2464` flip left/right in RTL modes (right-click = next in LTR, right-click = previous in RTL). Plain `Space` stays "next" irrespective of direction (per Mihon; convention).
- VerticalPager + ContinuousVertical modes: wheel behavior is vertical-only; `Up/Down` arrow keys replace `Left/Right` as page-turn.
- **Files:** `src/ui/readers/ComicReader.cpp`.
- **Success:** RTL-mode playback: right-click advances page, left-click goes back (mirrored vs LTR). Wheel in VerticalPager goes page-down → next-page. Smoke against a manga CBZ ordered right-to-left.

### Phase 1 exit criteria
- Six-mode enum shipped + persisted.
- RTL / fit / zoom persisted per series.
- Wheel/click semantics direction-aware in paged modes.
- Agent 6 review: persistence whitelist + mode-migration cleanliness against audit P1-1 citation chain.
- `READY FOR REVIEW — [Agent 1, COMIC_READER_FIX Phase 1]: Reading mode breadth + persistence | Objective: Phase 1 per COMIC_READER_FIX_TODO.md + agents/audits/comic_reader_2026-04-15.md. Files: ...`

---

## Phase 2 — Webtoon tuning

**Why:** Audit P1-2 CONFIRMED BROKEN (chat.md:13316). `ScrollStripCanvas::paintEvent` lays pages with a fixed `SPACING` constant and no configurable side padding (`ScrollStripCanvas.cpp:151-198`). `targetPageWidth` exposes only `portraitWidthPct` as a width knob (`:241-256`). Mihon's webtoon mode exposes side padding, crop-borders, split-double-page, zoom-out preset (`Reader\setting\ReaderPreferences.kt:71-88`, `presentation\reader\settings\ReadingModePage.kt:140-205`). OpenComic also treats webtoon width adjustment specially (`scripts\reading.js:300-410`).

This phase tunes the Webtoon mode shipped in Phase 1 — it applies only when mode == Webtoon (ContinuousVertical stays general-purpose and unaffected).

### Batch 2.1 — Webtoon side-padding + configurable spacing

- Add `m_webtoonSidePaddingPct` + `m_webtoonPageSpacingPx` members, persisted via Phase 1 whitelist.
- `ScrollStripCanvas::rebuildYOffsets` + `paintEvent` honor the spacing value when Webtoon mode is active.
- `targetPageWidth` adjusts by side padding when Webtoon mode is active: `(viewport_width * (1.0 - 2*sidePaddingPct)) * portraitWidthPct`.
- Default side padding: 5%. Default spacing: 0px (Mihon default; continuous strip).
- **Files:** `src/ui/readers/ScrollStripCanvas.h`, `src/ui/readers/ScrollStripCanvas.cpp`, `src/ui/readers/ComicReader.cpp` (pass tuning values through).
- **Success:** open webtoon (long-strip CBZ) → configurable side padding visible. Existing Webtoon users see no change until they adjust.

### Batch 2.2 — Crop-borders toggle (Webtoon mode only)

- `m_webtoonCropBorders` bool, persisted.
- On page decode, detect top/bottom solid-color borders (sample horizontal strips, threshold brightness variance) and crop during paint. Do NOT modify stored pixmap — store original, draw cropped rect.
- Cache crop-rect per page to avoid re-detecting.
- **Files:** `src/ui/readers/ScrollStripCanvas.cpp` (crop-detect + paint-time rect), `src/ui/readers/ComicReader.cpp` (toggle + cache invalidation on mode/page change).
- **Success:** toggle on → padded webtoon pages show tighter layout (borders trimmed). Toggle off → pages render full-height.
- **Budget note:** this is the heaviest batch in Phase 2 (pixel-level border detection). If detection perf feels slow on large strips, cap the detection sample rate per second and accept a one-frame "settle-in" as borders crop.

### Batch 2.3 — Webtoon zoom-out preset + split-double-page (optional-Mihon carryover)

- Add `m_webtoonZoomOut` preset (fits wide page fully in viewport rather than width-scaling) — Mihon ZoomStart.LEFT / RIGHT / CENTER / AUTOMATIC analogue, simplified to "fit-to-width" (current) vs "fit-full-page" (new).
- Split double-page spreads vertically when Webtoon mode + split toggle on (Mihon `dualPageSplit` in `ReaderPreferences.kt:73-78`). Detected spreads (`knownSpreadIndices`) get rendered as two half-height vertical slices in sequence.
- **Files:** `src/ui/readers/ScrollStripCanvas.cpp`, `src/ui/readers/ComicReader.cpp`.
- **Success:** webtoon with embedded spread page + split toggle on → spread renders as left-half-then-right-half. Zoom-out preset → wide page fits full viewport.

### Phase 2 exit criteria
- Webtoon side padding, spacing, crop-borders, split-double-page, zoom-out preset functional + persistent.
- ContinuousVertical (the non-webtoon strip mode from Phase 1) is explicitly unaffected.
- Agent 6 review against audit P1-2 citation chain.

---

## Phase 3 — Zoom / fit / magnifier

**Why:** Audit P1-5 + TKMAX-3 CONFIRMED BROKEN / REGRESSED (chat.md:13322, :13340). `FitMode` enum at `ComicReader.h:74` omits Actual Size / 1:1 / Original / Stretch / Smart Fit. `setZoom` clamps 100-260% and `zoomBy` early-returns unless `m_isDoublePage == true`. Single-page and ScrollStrip zoom: unsupported. Loupe/magnifier absent — Tankoban-Max had it (`input_keyboard.js:315-327`), YACReader has it (`main_window_viewer.cpp:406-429`), we regressed.

### Batch 3.1 — FitMode expansion

- Expand `FitMode` enum to `{FitPage, FitWidth, FitHeight, ActualSize, Stretch, SmartFit}`. Persist via Phase 1 whitelist (Batch 1.2).
- `ActualSize` renders at 1:1 pixel (no scaling). `Stretch` fills viewport (may distort). `SmartFit` picks FitWidth for portrait pages, FitPage for landscape — Mihon `ReaderPreferences.kt:215-229` match.
- Toolbar fit button cycles through modes or opens a picker popup (defer picker to Phase 7 consolidation).
- Per-mode keybinds retained (existing) or added (new): `Shift+1` = FitPage, `Shift+2` = FitWidth, etc. — optional, low priority.
- **Files:** `src/ui/readers/ComicReader.h`, `src/ui/readers/ComicReader.cpp`.
- **Success:** all six fit modes reachable + persistent per series.

### Batch 3.2 — Zoom generalization + slider

- Remove `zoomBy` double-page gate at `ComicReader.cpp:1730-1740`. Zoom applies to all paged modes (SinglePage, DoublePage, RtlPage, VerticalPager). ScrollStrip / Webtoon / ContinuousVertical: zoom adjusts page width scaling, not viewport transform.
- Add a zoom slider (25%-400% range; default 100%). Lives near the HUD when zoom != 100% (unobtrusive when neutral).
- `Ctrl+0` resets zoom to 100% across all modes (currently resets series settings per K overlay at :2754 — preserve that OR rebind reset-zoom to a different chord; Agent 1's call during execution).
- **Files:** `src/ui/readers/ComicReader.h`, `src/ui/readers/ComicReader.cpp`.
- **Success:** zoom works in SinglePage, DoublePage, RtlPage, VerticalPager. Zoom slider visible. `Ctrl+wheel` still zooms inline. Persisted per series.

### Batch 3.3 — Loupe / magnifying glass overlay (TKMAX-3 + YACReader)

- New `LoupeOverlay` widget child of `ComicReader`. Shows a circular or rectangular magnified region of the current page around the cursor position.
- Default tunables: 200px diameter, 2x zoom, gray-rim border matching overall reader aesthetic per `feedback_no_color_no_emoji`.
- Toggle shortcut: `L` (Tankoban-Max had loupe toggle at `input_keyboard.js:315-327`; check for conflict with current keybinds at `ComicReader.cpp:2215-2351` — `L` appears unused, verify).
- Mouse-move updates magnified region via `grab()` from source page pixmap at cursor + QPainter-to-overlay.
- Hide on `Esc` / second `L` press / mode transition to scroll modes (loupe + vertical scroll is awkward; disable in Webtoon / ContinuousVertical).
- **Files:** NEW `src/ui/readers/LoupeOverlay.h/.cpp`, `src/ui/readers/ComicReader.cpp` (mount + toggle), `src/ui/readers/ComicReader.h` (declare).
- **Success:** paged mode + `L` → magnified circle tracks cursor. Second `L` or Esc hides. Disabled in continuous-scroll modes.
- **Budget note:** if `grab()`-per-mouse-move is too slow on large decoded pages, pre-cache a 1.5-2x-resolution source pixmap when loupe is active.

### Phase 3 exit criteria
- All six FitModes functional + persistent.
- Zoom works in every paged mode.
- Loupe overlay shippable + persistent-toggle optional.
- Agent 6 review against audit P1-5 + TKMAX-3 citation chain.

---

## Phase 4 — Image filters (TKMAX parity + Mihon color-filter overlap)

**Why:** Audit P1-6 + TKMAX-1 CONFIRMED BROKEN / REGRESSED (chat.md:13324, :13336). Grep returns zero filter wiring in `ComicReader.cpp` — only `Qt::TransformationMode` (Smooth/Fast). Tankoban-Max shipped brightness / contrast / saturation / sepia / hue / invert / grayscale with presets and persistence (`mega_settings.js:605-671`, `reader_view.html:76-158`). Mihon has custom brightness + RGB/alpha color filters + blend modes + grayscale + inverted colors (`presentation\reader\settings\ColorFilterPage.kt:23-125`). YACReader persists brightness/contrast/gamma per comic (`viewer.cpp:1199-1235`). MComix has a classic enhance backend (`enhance_backend.py:10-37`).

Identity direction: TKMAX parity on the feature set (brightness/contrast/saturation/sepia/hue/invert/grayscale + presets). Mihon's blend-mode overlap is a polish follow-up if Hemanth flags interest.

### Batch 4.1 — Image filter pipeline plumbing

- Add `ImageFilterState` struct: `{brightness, contrast, saturation, sepia, hue, invert, grayscale}`. All floats in normalized ranges (brightness/contrast/saturation: 0.0-2.0 with 1.0 neutral; sepia/hue/invert/grayscale: 0.0-1.0).
- Per-series persistence via Phase 1 whitelist.
- `ComicReader::applyFilters(QPixmap)` free helper or member takes a source pixmap, returns a filtered pixmap via `QImage` per-pixel transform. Cached result stored in `PageCache` alongside scaled pixmap (may multiply cache keys — design for `(pageIndex, targetSize, filterStateHash)` tuple).
- Default filter state is all-neutral (brightness=1.0, contrast=1.0, etc.) — zero-op until user engages a slider.
- **Files:** `src/ui/readers/ComicReader.h`, `src/ui/readers/ComicReader.cpp`, `src/ui/readers/PageCache.h/.cpp` (filter-state cache key).
- **Success:** filter state plumbed end-to-end. Setting brightness=1.5 → reader renders visibly brighter. No perf regression at default (neutral) state.
- **Budget note:** if per-pixel QImage iteration is too slow on 4K-page webtoon strips, consider `QGraphicsColorizeEffect` or a QSS-less shader path. QImage loop is the simpler starting point.

### Batch 4.2 — Filter UI (sliders block, pre-Phase-7 shape)

- Add a dedicated "Image Filters" context-menu submenu with sliders (pre-Phase-7; retrofitted into Mega Settings in Phase 7).
- Each of 7 filters: labeled slider + numeric spinbox + reset button.
- "All Off" preset button sets everything to neutral.
- Built as a standalone widget (`ImageFilterPanel`) so Phase 7 can relocate it into Mega Settings without refactoring sliders.
- **Files:** NEW `src/ui/readers/ImageFilterPanel.h/.cpp`, `src/ui/readers/ComicReader.cpp` (context-menu wiring).
- **Success:** right-click → Image Filters submenu → sliders adjust in real-time. "All Off" resets.

### Batch 4.3 — Filter presets

- Named presets: Normal (all-neutral), Night (brightness=0.7, contrast=1.1, invert=0.0), Sepia (sepia=1.0), High Contrast (contrast=1.6), Grayscale (grayscale=1.0).
- User-savable custom presets (up to 5) stored in `QSettings("reader/filterPresets")` as a JSON array.
- Preset dropdown in `ImageFilterPanel`.
- **Files:** `src/ui/readers/ImageFilterPanel.h/.cpp`, `src/ui/readers/ComicReader.cpp`.
- **Success:** preset dropdown populated with builtins + user customs. Clicking a preset applies its state. "Save Current as Preset..." prompts for a name.

### Phase 4 exit criteria
- All seven TKMAX filters functional + persistent.
- 5 built-in presets + user preset saving.
- Filter panel is a standalone widget ready for Phase 7 mount.
- Agent 6 review against audit P1-6 + TKMAX-1 citation chain.

---

## Phase 5 — Thumbnail page rail

**Why:** Audit P1-3 CONFIRMED BROKEN (chat.md:13318). Page-seek surfaces are numeric go-to + scrub + vertical thumb + file-level volume navigator. For 500-page CBZ / long webtoons, visual seeking is unavailable.

Mihon primary identity says page-slider-with-thumbnails (Mihon `presentation\reader\components\ChapterNavigator.kt:45-148` + thumbnail preview on hover). YACReader polish overlay suggests GoToFlow coverflow as a later sub-phase. Ship Mihon's page-slider-with-thumbnails first.

### Batch 5.1 — PageThumbnailCache worker

- NEW `src/ui/readers/PageThumbnailCache.h/.cpp`: background worker decoding each page at 160x90-equivalent aspect-preserved resolution, cached to `{AppData}/Tankoban/data/comic_thumbnails/{file_sha1}_{pageIndex}.jpg`.
- Budget: ~50-100ms per thumbnail, batched on idle. Skip for < 20 pages (no payoff). Purge on library remove. LRU eviction at configurable total size (default 500MB).
- Exposes signal `thumbnailReady(pageIndex, QPixmap)` so UI can refresh incrementally.
- **Files:** NEW `src/ui/readers/PageThumbnailCache.h/.cpp`, `src/ui/readers/ComicReader.cpp` (kick extraction on openBook).
- **Success:** open a 200-page CBZ → within ~30 seconds, 200 thumbnails exist on disk. Re-open same series → hits cache, no re-extraction.

### Batch 5.2 — Page slider with hover thumbnail preview

- Extend existing scrub bar surface at `ComicReader.cpp` (in the HUD area) with a hover-preview bubble. On mouse-hover over the scrub, compute nearest page index, show a floating 160x90 thumbnail bubble above the cursor.
- Fallback to existing time/page bubble when thumbnail not yet cached.
- **Files:** `src/ui/readers/ComicReader.cpp` (scrub hover hook), `src/ui/readers/ComicReader.h` (hover-preview member widget).
- **Success:** hover scrub on a file with extracted thumbnails → preview bubble tracks cursor. Drag → bubble updates at ~10fps.

### Batch 5.3 — Thumbnail strip panel (toggleable)

- Toggleable side panel (`Shift+T` or dedicated shortcut) showing a vertical strip of thumbnails with page-number labels. Click a thumbnail → jump to page.
- Current page highlighted. Scrolls to keep current page visible.
- Mihon reference: thumbnail preview surface via ReaderPageIndicator / page slider. YACReader polish alternative (GoToFlow): deferred as later sub-phase.
- **Files:** NEW `src/ui/readers/PageThumbnailStrip.h/.cpp`, `src/ui/readers/ComicReader.cpp` (mount + toggle).
- **Success:** `Shift+T` → strip panel slides in from the side, populated with thumbnails. Click → jump to page. Current page highlighted.

### Phase 5 exit criteria
- Thumbnail cache worker functional + cache pruning.
- Scrub hover preview.
- Toggleable thumbnail strip panel.
- Agent 6 review against audit P1-3 citation chain.

---

## Phase 6 — Bookmark UX rebuild

**Why:** Audit P1-4 CONFIRMED BROKEN (chat.md:13320). Current bookmark storage is `QSet<int>` (page indices); surface is 6-item right-click submenu. Mihon + YACReader + OpenComic all surface bookmarks with thumbnails, labels, delete controls (Mihon via reader panel, YACReader `bookmarks_dialog.cpp:22-158`, OpenComic `reading.elements.menus.collections.bookmarks.html:1-40`).

### Batch 6.1 — Bookmark data-model upgrade

- New `Bookmark` struct: `{ pageIndex, label, timestamp, thumbPath }`. Migration from `QSet<int>` → `QList<Bookmark>` in progress payload at `ComicReader.cpp:1353-1388`. Old payloads load as `{pageIndex=N, label="", timestamp=0, thumbPath=""}`.
- `B` key (bookmark toggle at `ComicReader.cpp:2771-2782`) creates a Bookmark with timestamp=now, label="", thumb cached via Phase 5.
- Label-edit context menu or dialog (later batch).
- **Files:** `src/ui/readers/ComicReader.h`, `src/ui/readers/ComicReader.cpp`.
- **Success:** existing bookmark-set users open their series → old indices load as unlabeled bookmarks. Toggling `B` writes new struct. Data round-trips.

### Batch 6.2 — Bookmark panel (YACReader + OpenComic style)

- New `BookmarkPanel` widget: grid of thumbnails (via Phase 5 cache) + labels + page number + delete button per entry. Opened via dedicated shortcut (e.g. `Shift+B`) or toolbar button.
- Click thumbnail → jump to page. Right-click entry → rename label / delete.
- "Clear All Bookmarks" action with confirm prompt.
- **Files:** NEW `src/ui/readers/BookmarkPanel.h/.cpp`, `src/ui/readers/ComicReader.cpp` (mount + toggle).
- **Success:** add 5 bookmarks → `Shift+B` → panel shows all 5 with thumbnails + page labels. Click one → jump. Rename works. Delete works.

### Batch 6.3 — Bookmark label-edit inline

- Minimal inline rename affordance for bookmark labels — double-click a panel entry or a dedicated pencil button enters edit mode, Enter confirms, Esc cancels.
- **Files:** `src/ui/readers/BookmarkPanel.h/.cpp`.
- **Success:** dbl-click → line-edit appears in place of label. Type → Enter → label saved + panel refreshed.

### Phase 6 exit criteria
- Bookmark struct shipped + migration tested on old payloads.
- Dedicated bookmark panel with thumbnails + labels + delete.
- Inline rename functional.
- Agent 6 review against audit P1-4 citation chain.

---

## Phase 7 — Consolidated settings surface (Mega-Settings parity)

**Why:** Audit P1-7 + TKMAX-2 CONFIRMED BROKEN / REGRESSED (chat.md:13326, :13338). Current reader controls scatter across toolbar / right-click / K-overlay / popup. Tankoban-Max ran Mega Settings (`reader_view.html:306-377`, `mega_settings.js:92-166`). Mihon has categorized reader settings (`ReadingModePage.kt:27-205`). This phase consumes the per-feature widgets Phases 1-6 built (filter panel, zoom slider, bookmark panel, mode picker) into a single overlay.

### Batch 7.1 — ReaderSettingsOverlay shell

- NEW `src/ui/readers/ReaderSettingsOverlay.h/.cpp`: a full-screen-overlay (semi-transparent backdrop, centered pane) with left-side tab list: Mode / Image / Webtoon / Zoom / Bookmarks / Keys / About.
- Opened via gear button in the HUD or `Shift+S` / dedicated shortcut.
- Each tab is a QStackedWidget page; tabs mount existing per-feature widgets from Phases 1-6.
- **Files:** NEW `src/ui/readers/ReaderSettingsOverlay.h/.cpp`, `src/ui/readers/ComicReader.cpp` (mount + gear button).
- **Success:** gear / shortcut → overlay slides in. Left-tab navigation works. Closing via Esc / backdrop-click returns to reader.

### Batch 7.2 — Mount per-feature widgets in tabs

- **Mode tab:** mode icon row (Phase 1 values, prefigures Phase 10 picker); RTL toggle checkbox.
- **Image tab:** `ImageFilterPanel` (Phase 4) as the full Image tab content.
- **Webtoon tab:** side padding slider + spacing slider + crop borders toggle + split-double-page toggle + zoom-out preset (Phase 2).
- **Zoom tab:** fit-mode picker (Phase 3) + zoom slider + zoom range label.
- **Bookmarks tab:** `BookmarkPanel` (Phase 6) embedded.
- **Keys tab:** existing K-overlay keyboard reference, plus click-zone hint text (P2 coupling).
- **About tab:** reader build info, version, credits.
- **Files:** `src/ui/readers/ReaderSettingsOverlay.h/.cpp`, various Phase-X widget files (zero API churn — just mount instances).
- **Success:** every reader setting reachable from within the overlay. Previous per-feature context-menu entries can be retired OR kept as shortcuts into the overlay (Hemanth's call).

### Batch 7.3 — Retirement / shortcuts pass

- Decide per control surface: retire legacy entry (context menu item, toolbar button) or keep as shortcut into the overlay.
- **Default recommendation:** keep HUD toolbar buttons (toolbar stays fast-access); retire right-click-menu's per-feature submenus in favor of "Reader Settings..." entry that opens the overlay; K-overlay becomes a tab inside the overlay.
- **Files:** `src/ui/readers/ComicReader.cpp` (retire context-menu entries).
- **Success:** right-click → single "Reader Settings..." entry opens overlay. Previous scatter retired.

### Phase 7 exit criteria
- Consolidated settings overlay shipped with 7 tabs.
- All per-feature widgets (Phases 1-6) reachable via overlay.
- Legacy scatter retired.
- Agent 6 review against audit P1-7 + TKMAX-2 citation chain.

---

## Phase 8 — Error typing

**Why:** Audit P1-9 CONFIRMED BROKEN (chat.md:13330). Open-path at `ComicReader.cpp:728-734` collapses to two strings. DecodeTask emits same `failed(pageIndex)` for empty archive-data AND null QImageReader result. Password-protected / corrupt / unsupported all indistinguishable. YACReader has distinct loading + page-unavailable states (`viewer.cpp:984-1002`).

### Batch 8.1 — ArchiveReader::OpenResult enum

- New `ArchiveReader::OpenResult` enum: `Ok, NotFound, PasswordProtected, CorruptArchive, UnsupportedFormat, NoImagesFound, UnknownError`.
- `ArchiveReader::pageList` at `src/core/ArchiveReader.cpp:26-82`, `:84-138` returns `std::pair<QStringList, OpenResult>` OR equivalent side-channel (via out-param).
- Map libarchive error codes / QZipReader status to the enum.
- **Files:** `src/core/ArchiveReader.h`, `src/core/ArchiveReader.cpp`.
- **Success:** corrupt test archive → pageList returns `{empty, CorruptArchive}`. Password-protected CBR → `{empty, PasswordProtected}`. Valid archive with no JPG/PNG → `{empty, NoImagesFound}`.

### Batch 8.2 — DecodeTask::Reason enum + propagation

- New `DecodeTask::Reason` enum: `Ok, EmptyArchiveData, DecodeFailed, Corrupt, Unsupported`.
- `DecodeTask::notifier.failed` signal extends to `failed(pageIndex, volumePath, Reason)` — Reason carries per-page failure type.
- `ComicReader` handler at `ComicReader.cpp:868-966` shows reason-specific inline page-error states ("Page unavailable (corrupt)" / "Page decode failed" / "Page missing").
- **Files:** `src/ui/readers/DecodeTask.h`, `src/ui/readers/DecodeTask.cpp`, `src/ui/readers/ComicReader.cpp`.
- **Success:** corrupt-page JPG in CBZ → reader shows "Page 47: corrupt" in place; other pages render normally. Missing page data → "Page N: missing."

### Batch 8.3 — Reader-level typed open messages

- `ComicReader::openBook` at `:692-775` branches on OpenResult from Batch 8.1. Per-case messages:
  - `NotFound` → "File not found. It may have been moved or deleted."
  - `PasswordProtected` → "This archive is password-protected. Password input not yet supported."
  - `CorruptArchive` → "Archive is corrupt or unreadable."
  - `UnsupportedFormat` → "Unsupported archive format."
  - `NoImagesFound` → "No readable pages in this archive."
  - `UnknownError` → "Could not open this file."
- Strings visible in the existing "Loading / Ready / Resumed / ..." status label or a dedicated error overlay.
- **Files:** `src/ui/readers/ComicReader.cpp`.
- **Success:** each error case presents a specific user-visible message.

### Phase 8 exit criteria
- OpenResult + Reason enums plumbed.
- Per-case error messages surface.
- Agent 6 review against audit P1-9 citation chain.

---

## Phase 9 — Archive format parity (CBR + RAR MVP)

**Why:** Audit P1-10 CONFIRMED BROKEN (chat.md:13332). `CBZ_EXTS = {"*.cbz"}` at `SeriesView.cpp:25` + hardcoded `*.cbz` at `ComicsPage.cpp:212, :693`. Meanwhile `ArchiveReader::isCbrPath` recognizes `.cbr, .rar` and routes through libarchive (`:26-30`, `:84-88`). Engine supports more than UI surfaces.

Identity direction: CBR / RAR MVP matching our engine. Broader YACReader set (`.zip, .7z, .cb7, .tar, .arj, .cbt`) deferred as later polish sub-phase. Phase 9 is small-code but cross-agent (touches Agent 5's territory).

### Batch 9.1 — SeriesView + ComicsPage file-filter expansion

- `CBZ_EXTS` at `SeriesView.cpp:25` becomes `COMIC_EXTS = {"*.cbz", "*.cbr", "*.rar"}`.
- Rename semantic (`CBZ_EXTS` → `COMIC_EXTS`); update all callers at `:585-589`, `:756-787`.
- ComicsPage inline `*.cbz` at `:212, :693` references the shared constant.
- Verify `ArchiveReader::pageList` handles all three in Phase 8's typed-error era — missing-libarchive path for `.rar` → `UnsupportedFormat`; password-protected RAR → `PasswordProtected`.
- **Files:** `src/ui/pages/SeriesView.h/.cpp`, `src/ui/pages/ComicsPage.h/.cpp`.
- **Cross-agent note:** Agent 5 owns SeriesView + ComicsPage. Agent 1 posts heads-up in chat.md before the edit, or Agent 5 can ship this Batch 9.1 on Agent 1's behalf. Agent 0 brokers.
- **Success:** drop a .cbr into a tracked folder → appears in SeriesView grid. Double-click → opens via ArchiveReader CBR path → pages render.

### Batch 9.2 — Scanner extension awareness

- `src/core/ScannerUtils.cpp` per-file cover extraction + library scanner file-list awareness for CBR/RAR. If scanner hardcodes CBZ-only anywhere, expand.
- **Cross-agent note:** Agent 5 owns ScannerUtils. Same heads-up pattern.
- **Files:** `src/core/ScannerUtils.cpp`.
- **Success:** library scan sees CBR/RAR files; covers extract.

### Batch 9.3 — Continue-strip + resume-from-cover paths

- Continue strip + cover preview paths (`ComicsPage.cpp:617-708`) handle CBR/RAR files the same as CBZ.
- **Cross-agent note:** Agent 5 territory. Same heads-up.
- **Success:** CBR continue-reading entry shows in the strip + resumes into reader correctly.

### Phase 9 exit criteria
- CBR + RAR enumerable + openable from SeriesView / ComicsPage / continue strip.
- 7Z / CB7 / TAR deferred.
- Agent 6 review against audit P1-10 citation chain.

---

## Phase 10 — Mode direct-pick + volume naming consistency

**Why:** Audit P1-8 + P2 (chat.md:13328, :13345). `cycleReaderMode` at `:1476-1526` is cycle-only — once Phase 1 ships 6 modes, cycling is clunky. Mihon has a `ReadingModeSelectDialog` with icons (`presentation\reader\ReadingModeSelectDialog.kt:29-76`). Separately, `m_volTitle` hardcodes "Volumes" at `:572` + `m_volSearch` placeholder hardcodes "Search volumes" at `:578`, leaking past Congress 4's Volumes/Chapters toggle.

### Batch 10.1 — Mode direct-pick dialog

- NEW `ReadingModeDialog` widget: icon-row of all 6 modes (Phase 1), captions, click-to-pick, immediate apply.
- `M` long-press (or `Shift+M`) opens the dialog; plain `M` press stays cycle for muscle-memory (or retire cycle — Agent 1's call).
- Mihon reference: `ReadingModeSelectDialog.kt:29-76`.
- Icons: 6 simple SVG glyphs in `feedback_no_color_no_emoji` aesthetic.
- Integrated into Phase 7's Mode tab too.
- **Files:** NEW `src/ui/readers/ReadingModeDialog.h/.cpp`, `src/ui/readers/ComicReader.cpp`, `src/ui/readers/ReaderSettingsOverlay.cpp` (mount in Mode tab).
- **Success:** `Shift+M` → dialog with 6 icons → click one → mode applies immediately.

### Batch 10.2 — Volume/chapter naming consistency

- `m_volTitle` + `m_volSearch` at `ComicReader.cpp:553-681` consume the SeriesView Volumes/Chapters toggle (at `SeriesView.cpp:196-235`).
- Route: SeriesView owns the naming policy per series; ComicReader reads it on openBook via a setter or series-settings key.
- **Cross-agent note:** Agent 5 owns SeriesView naming-policy policy. This is a small linkage — Agent 0 brokers the setter / key naming.
- **Files:** `src/ui/readers/ComicReader.cpp`, possibly `src/ui/pages/SeriesView.cpp` (setter surface).
- **Success:** SeriesView toggled to "Chapters" → open a series → reader's volume navigator reads "Chapters" + search placeholder "Search chapters".

### Phase 10 exit criteria
- Direct-pick dialog functional + persistent.
- Volume/chapter naming honors SeriesView toggle.
- Agent 6 review against audit P1-8 + cross-surface P2 citation chain.

---

## Scope decisions locked in

- **Mihon primary identity** for reading-mode breadth, categorized settings, reading-mode direct-pick, webtoon tuning. YACReader polish overlay for magnifier + filter-persistence philosophy + broad archive set (deferred).
- **Thumbnail page rail defaults to Mihon-style slider-with-hover + toggleable strip panel** — not YACReader GoToFlow coverflow. GoToFlow is a later polish sub-phase if Hemanth flags.
- **Image filter pipeline defaults to TKMAX parity** (7 filters + presets) — not Mihon's blend-mode overlap. Blend modes are a later polish sub-phase.
- **Archive discovery caps at CBR + RAR MVP in Phase 9** — matching our engine. Broader YACReader set (`.7z, .cb7, .tar, .arj, .cbt`) is a later sub-phase.
- **Per-feature widgets ship standalone in Phases 2-6** before Phase 7 consolidation — avoids double-work and keeps shippable wins per phase.

## Out-of-scope deferred items (later polish sub-phases, flag if Hemanth wants them sooner)

- YACReader GoToFlow coverflow alternative to Mihon-style thumbnail strip (Phase 5 follow-up)
- Mihon color-filter blend modes (Phase 4 follow-up)
- YACReader extended archive set `.7z, .cb7, .tar, .arj, .cbt, .zip` (Phase 9 follow-up)
- OpenComic-style editable click-zone mapping (P2 coupling — post-Phase-7)
- OpenComic-style persistent bookmark sidebar that auto-opens on every reader entry (P2 polish)
- Mihon-style chapter-gap warning card on volume transitions (P2)
- In-reader percent progress readout (P2 — current page/count is the default)
- First-use navigation overlay / gesture hint (Mihon `ReaderPreferences.kt:164-173`) (P2)
- Performance state badge (decode latency / cache / FPS) (P2)
- Direct file / drag-drop into reader (P2 — library-first is current paradigm)
- Fullscreen auto-persist preference (Mihon `ReaderPreferences.kt:29-37`) (P2)

## Rule 6 + Rule 11 application

- **Rule 6 (build + smoke before declared done):** every batch must compile + smoke on Hemanth's box before posting `READY TO COMMIT`. Agent 1 does not declare a batch done without build verification.
- **Rule 11 (agents flag READY TO COMMIT, Agent 0 batches commits):** per-batch READY TO COMMIT lines in chat.md with file list. Agent 0 batches commits at phase boundaries (exception: isolated-risk batches get isolate-commit — flagged explicitly below).
- **Isolate-commit candidates:**
  - **Batch 1.1** (enum migration) — must commit before 1.2 to validate old-data load behavior on Hemanth's actual library.
  - **Batch 4.1** (filter pipeline + cache multiplication) — perf risk; commit + smoke isolate before 4.2/4.3.
  - **Batch 5.1** (thumbnail extraction worker + disk cache) — disk-usage surprise potential; isolate-commit + smoke on a representative library before 5.2/5.3.
  - **Batch 8.1** (ArchiveReader enum shared file) — engine-boundary change; isolate-commit so rollback is clean if any other consumer reveals itself.
- Single-rebuild-per-batch per `feedback_one_fix_per_rebuild`.

## Review gates

Each phase exits with:

```
READY FOR REVIEW — [Agent 1, COMIC_READER_FIX Phase X]: <phase title> | Objective: Phase X per COMIC_READER_FIX_TODO.md + agents/audits/comic_reader_2026-04-15.md. Files: ...
```

Agent 6 reviews against audit + TODO as co-objective. Blocking P0/P1 per Agent 6's rubric → phase does not advance.

## Open design questions Agent 1 decides as domain master

- **Cycle-key `M` retirement after Phase 10.1:** keep cycle (muscle memory) + add `Shift+M` dialog, OR retire cycle entirely in favor of dialog-only? Agent 1's call during Phase 10.
- **Loupe `L` keybind conflict check:** verify L is free at `ComicReader.cpp:2215-2351`. If conflict, Agent 1 picks an alternative chord.
- **Filter pipeline pixel iteration vs QGraphicsColorizeEffect vs shader:** perf-dependent. Start with QImage loop, benchmark, escalate if sluggish at ScrollStrip/Webtoon speeds.
- **Thumbnail strip panel orientation:** Mihon vertical side strip vs horizontal bottom strip. Vertical matches reader's portrait-page aspect; recommend vertical. Agent 1 confirms.
- **Bookmark panel layout (Phase 6.2):** grid of thumbnails vs vertical list. Grid matches YACReader; list matches OpenComic. Agent 1's call during Phase 6.
- **ScrollStrip → Webtoon enum rename (Batch 1.1):** preserve wire-compat with integer-persisted `readerMode=2`. If rename causes too much grep-churn, keep `ScrollStrip` as the enum name + add `Webtoon` semantics as a behavior flag. Agent 1's call.
- **Phase 9 cross-agent sequencing:** does Agent 5 ship Phase 9 batches on Agent 1's behalf (lighter coordination), or does Agent 1 take the SeriesView/ComicsPage touch under an explicit heads-up? Agent 0 brokers post-Phase-8.

## Verification script (end-to-end regression smoke)

When the full plan ships, run this smoke on Hemanth's real library:

1. Open a CBZ in SinglePage mode → read 10 pages → close → reopen → resumes at page 10 ✓
2. Flip to RTL mode → close → reopen → still RTL ✓
3. Flip through all 6 modes → no crash, each mode renders ✓
4. Open Webtoon CBZ → enable side padding + crop borders → pages render tighter ✓
5. Adjust zoom slider to 150% in SinglePage mode → zoom applies → close → reopen → zoom persists ✓
6. Open Image Filters tab → brightness to 1.5 → applies live → close → reopen → persists ✓
7. Apply Night preset → all filters applied; apply Normal preset → all reset ✓
8. Thumbnail strip panel → click page 250 → jumps correctly ✓
9. `L` → loupe appears → magnifies correctly → `L` hides ✓
10. `B` twice → two bookmarks added → bookmark panel → thumbnails visible → rename one → delete one ✓
11. Settings overlay `Shift+S` → every tab renders correctly → every setting reachable ✓
12. Corrupt test CBZ → reader shows "Archive is corrupt or unreadable." ✓
13. Password-protected CBR → reader shows password-specific message ✓
14. Drop a .cbr into library folder → appears in SeriesView → opens in reader ✓
15. Toggle SeriesView Volumes↔Chapters → reader overlay header reflects toggle on next open ✓

## Identity-direction memory

This TODO inherits project memory `project_comic_reader_identity.md` (2026-04-16): Mihon primary, YACReader polish. Phase ordering, feature priorities, and scope decisions throughout this doc follow that identity. If future Hemanth direction changes (e.g. "drop Mihon priority, go YACReader-first"), this TODO gets re-authored — not patched in place.

---

**End of plan.**
