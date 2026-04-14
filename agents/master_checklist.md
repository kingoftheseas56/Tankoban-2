# Master Feature Checklist — First Congress
# Compiled by Agent 0 from Assistant 1 (Max) + Assistant 2 (Groundwork) reports

This is Agent 0's reference. Each agent's position template contains their assigned subset.
Check this after the congress to verify nothing was missed.

Blacklisted (do not implement): auto-scroll, auto-flip, image filters, loupe.

---

## STATUS KEY
- ROUGH — implemented but wrong/incomplete vs Max/Groundwork
- MISSING — not implemented, not blacklisted

---

## Agent 3 Lane (Rendering)

| # | Feature | Status | Max file | Groundwork file |
|---|---------|--------|----------|-----------------|
| R1 | Image scaling quality — SmoothTransformation not used consistently | ROUGH | render_core.js | comic_reader.py paintEvent |
| R2 | Double-page gutter shadow | ROUGH | render_two_page.js twoPageGutterShadow | comic_reader.py lines 106–126 QLinearGradient |
| R3 | HiDPI fractional scaling — canvas transform from backing-store/CSS ratio | ROUGH | render_two_page.js lines 202–250 | N/A (Qt handles this differently) |
| R4 | Double-page scroll jank — synchronous work inside wheel handler | ROUGH | queueManualWheelSmooth pump | SmoothScrollArea 38%/frame drain |
| M1 | Fit Width mode for double page — scale to fill width, enable vertical pan | MISSING | render_two_page.js getTwoPageImageFit() | comic_reader.py DoublePageCanvas zoom |
| M2 | MangaPlus zoom mode — extra 100–260% multiplier, drag pan, arrow key pan | MISSING | render_two_page.js zoomPct/zoomFactor | comic_reader.py set_zoom(), set_pan() |
| M3 | Page dimensions cache — parse header bytes for dimensions before full decode | MISSING | bitmaps.js twoPageScrollDimsCache | comic_reader.py parse_image_dimensions() lines 213–346 |
| M4 | Scroll position fractional preservation on window resize | MISSING | render_portrait.js resizeCanvasToDisplaySize() lines 22–61 | Groundwork: adjustSize() + singleShot(50) restore |
| M5 | Spread detection from dims cache (no full decode needed for Two-Page Scroll layout) | MISSING | render_two_page.js isStitchedSpread() lines 5–43 | N/A |

---

## Agent 5 Lane (Scroll / HUD / Shortcuts)

| # | Feature | Status | Max file | Groundwork file |
|---|---------|--------|----------|-----------------|
| R1 | Wheel smoothing sophistication — 38%/frame drain, float accumulation, PreciseTimer | ROUGH | input_pointer.js queueManualWheelSmooth | comic_reader.py SmoothScrollArea lines 1371–1440 |
| R2 | HUD freeze — prevent auto-hide while interacting with scrub bar, scroller, or any open overlay | ROUGH | hud_core.js hudFreezeActive() lines 56–74 | comic_reader.py hud_hide_timer restart on footer/btn hover |
| R3 | Click zone flash — 90ms, separate .zoneFlashBlocked state for busy navigation | ROUGH | input_pointer.js flashClickZone() lines 18–30 | comic_reader.py Click Zone Flash lines 2666–2685 |
| R4 | HUD page counter in scroll mode — 100ms batched, computed from viewport center (which page is at center) | ROUGH | open.js computePageInView() lines 357–489 | comic_reader.py update_page_counter() on_scroll_position_changed |
| R5 | HUD auto-hide mode distinction — Manual Scroll = click-pinned, no timer. Auto Scroll = 3s timer only | ROUGH | hud_core.js hudScheduleAutoHide/hudCancelAutoHide | comic_reader.py hud_hide_timer 3000ms |
| M1 | Manual scroller thumb (right edge) — draggable vertical thumb separate from the scrub bar | MISSING | input_pointer.js beginManualScrollerDrag lines 812–970 | comic_reader.py HudVerticalScroller lines 1578–1648 |
| M2 | Direction toast — brief transient label for RTL/LTR toggle, zoom level, coupling shift | MISSING | N/A (JS toast) | comic_reader.py lines 2133–2147 QLabel 1200ms |
| M3 | Modal overlay scrim — semi-transparent full-size background widget behind overlays | MISSING | N/A (CSS overlay) | comic_reader.py ReaderOverlayScrim lines 787–803 |
| M4 | Right-click context menu (subset: go-to-page, copy volume path, reveal in explorer) | MISSING | input_pointer.js showReaderContextMenuFromEvent() | comic_reader.py QMenu right-click lines 3803–4049 |
| M5 | Double-click fullscreen toggle | MISSING | N/A (Electron window) | comic_reader.py eventFilter QEvent.MouseButtonDblClick lines 2520–2525 |
| M6 | Ctrl+Wheel zoom in double page | MISSING | N/A | comic_reader.py eventFilter Ctrl+Wheel lines 2551–2561 |
| M7 | Side nav arrow zones (‹/› arrows visible on hover at left/right edges) | MISSING | N/A | comic_reader.py SideNavZone lines 1812–1843 WA_TransparentForMouseEvents |

---

## Agent 2 Lane (Navigation / State)

| # | Feature | Status | Max file | Groundwork file |
|---|---------|--------|----------|-----------------|
| R1 | Progress payload completeness — missing: maxPageIndexSeen, finished, finishedAt, knownSpreadIndices, knownNormalIndices, updatedAt, y (scroll offset) | ROUGH | state_machine.js scheduleProgressSave() lines 1019–1095 | comic_reader.py scroll_fraction() + get_spread_overrides() |
| R2 | Coupling state persistence — mode + phase + confidence not saved/restored | ROUGH | open.js loadSeriesSettings | comic_reader.py get_coupling_state() + apply_progress_coupling_preference() |
| R3 | End-of-volume overlay key interception — ALL nav keys should be blocked and remapped while overlay is open | ROUGH | state_machine.js showEndOverlay() | comic_reader.py EndOfVolumeOverlay keyPressEvent |
| R4 | Volume navigator live search — does ours search live on type? Max/Groundwork filter on textChanged | ROUGH | volume_nav_overlay.js renderVolNav | comic_reader.py VolumeNavigatorOverlay QLineEdit.textChanged |
| M1 | Go-to-page dialog (Ctrl+G) — floating input overlay, 1-based input, Enter to jump | MISSING | mega_settings.js openGotoOverlay lines 579–603 | comic_reader.py GoToPageOverlay lines 868–916 |
| M2 | Keyboard shortcuts help overlay (K key) — two-column listing of all key bindings | MISSING | hud_core.js toggleKeysOverlay() | comic_reader.py KeysHelpOverlay lines 806–865 |
| M3 | Bookmarks (B key) — toggle current page index in/out of bookmarks list, toast feedback | MISSING | state_machine.js (saved in progress) | comic_reader.py _toggle_page_bookmark() lines 2419–2423 |
| M4 | Instant Replay (Z key) — jump back ~30% viewport, can cross one page boundary backward | MISSING | state_machine.js instantReplay() lines 565–626 | N/A |
| M5 | Clear Resume (R key) — delete saved progress for current volume, show toast | MISSING | state_machine.js clearResume() lines 1189–1194 | comic_reader.py clearResumeRequested signal |
| M6 | Manual checkpoint save (S key) — immediate save, "Checkpoint saved" toast | MISSING | state_machine.js saveProgressNow() lines 1148–1186 | N/A |
| M7 | Auto-finish detection — set finished:true when maxPageIndexSeen >= last page index | MISSING | state_machine.js scheduleProgressSave() lines 1083–1088 | N/A |
| M8 | Max page index seen tracking — track highest page ever reached for continue strip percentage | MISSING | state_machine.js computeMaxPageIndexSeenNow() lines 1001–1017 | N/A |
| M9 | Series settings per-seriesId — reader prefs (mode, width, coupling, speed) stored per-series not per-volume | MISSING | open.js loadSeriesSettings/saveSeriesSettings | N/A |
| M10 | Home / End keys — jump to first / last page | MISSING | input_keyboard.js lines 464–477 | comic_reader.py Key_Home / Key_End lines 2484–2492 |
| M11 | Alt+Left / Alt+Right — prev/next volume from keyboard | MISSING | N/A | comic_reader.py lines 2392–2400 |
| M12 | "Resumed" / "Ready" toast on volume open | MISSING | open.js openBook() lines 83–140 | N/A |

---

## Agent 4 Lane (File Loading / Format)

| # | Feature | Status | Max file | Groundwork file |
|---|---------|--------|----------|-----------------|
| R1 | Natural sort for archive entries — "page10" must sort after "page9" | ROUGH | open.js naturalCompare() line 209 | comic_reader.py natural_sort_key() lines 95–96 |
| R2 | Format detection by content (not just extension) | ROUGH | N/A | comic_reader.py QImageReader.setDecideFormatFromContent(True) |
| R3 | EXIF rotation — apply EXIF orientation on decode | ROUGH | N/A | comic_reader.py QImageReader.setAutoTransform(True) |
| R4 | Stale decode detection — discard decode result if volume changed while in-flight | ROUGH | bitmaps.js decodePageAtIndex() volume token snapshot | comic_reader.py inflight set[int] |
| R5 | Loading status label — show "Indexing pages..." while archive is being indexed | ROUGH | N/A | comic_reader.py QLabel status lines 2154–2158 |
| R6 | LRU cache with MB budget — evict by memory cost (width*height*4), not entry count | ROUGH | bitmaps.js prunePageCache() 512MB/256MB | comic_reader.py evict_cache() OrderedDict lines 4471–4503 |
| M1 | CBR/RAR archive support | MISSING | open.js cbrOpen path lines 171–225 | comic_reader.py rarfile ArchiveReader lines 360–370 |
| M2 | Fast dimension parsing from header bytes — get page dimensions without full decode | MISSING | bitmaps.js twoPageScrollDimsCache | comic_reader.py parse_image_dimensions() lines 213–346 |
| M3 | Memory saver mode — user-togglable 256MB cache budget | MISSING | bitmaps.js PAGE_CACHE_BUDGET_MEMORY_SAVER | comic_reader.py 256MB evict budget |

---

## Not assigned (skip or explicitly blacklisted)

| Feature | Reason |
|---------|--------|
| Auto-scroll | Blacklisted by Hemanth |
| Auto-flip | Blacklisted by Hemanth |
| Image filters (brightness/contrast/invert/grayscale/sepia) | Blacklisted by Hemanth |
| Loupe / magnifier | Blacklisted by Hemanth |
| Export page to file / copy to clipboard | Desktop-only, low priority |
| Open in new window | Not relevant for Qt single-window app |
| External file open / OS drag-drop | Can be added later, not reader-core |
| Reset series | Low priority, nice-to-have |
| Speed slider overlay | Only relevant for auto-scroll (blacklisted) |
| Auto-flip countdown display | Only relevant for auto-flip (blacklisted) |
| MegaSettings full panel | Overlaps with right-click menu; Agent 5 handles subset |
