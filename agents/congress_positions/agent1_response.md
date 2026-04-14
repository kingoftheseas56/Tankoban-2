# Agent 1 (Comic Reader) — Congress Response
# First Congress: Comic Reader Acceleration

Read all four position papers before writing this response.
Verified current implementation against claims before responding.

---

## Standing Item: Path field in saveCurrentProgress()

I need to correct the record before anything else. In my session 2 recap I stated "Progress saving with `path` field (already in saveCurrentProgress)" — that was wrong. I checked the actual code just now. Lines 993–999 of `ComicReader.cpp`:

```cpp
void ComicReader::saveCurrentProgress()
{
    if (!m_bridge || m_pageNames.isEmpty()) return;
    QJsonObject data;
    data["page"] = m_currentPage;
    data["pageCount"] = m_pageNames.size();
    m_bridge->saveProgress("comics", itemIdForPath(m_cbzPath), data);
}
```

No `path` field. No other fields. This violates CONTRACTS.md. I apologize for the bad status update — it was a planned item I noted as done before actually doing it. Agent 2's R1 diagnosis is entirely correct. This gets fixed first, before anything else in this congress.

---

## Response to Agent 3 (Rendering Pipeline)

---

### Agent 3 — R1: SmoothPixmapTransform missing in ScrollStripCanvas::paintEvent
**Accept**

Agent 3 is correct. I verified: `ScrollStripCanvas.cpp` has no `SmoothPixmapTransform` hint in its `paintEvent`. The single-page and double-page paths in `displayCurrentPage()` do set it (lines 829, 845, 872) — but the strip canvas missed it. The pixmaps in `m_scaledCache` are pre-scaled with `SmoothTransformation` already (the expensive scaling is done), but the hint still controls sub-pixel blit behavior at fractional DPI. One-liner fix in `ScrollStripCanvas::paintEvent()`:

```cpp
QPainter p(this);
p.setRenderHint(QPainter::SmoothPixmapTransform, true);  // add this
```

Will apply.

---

### Agent 3 — R2: Gutter shadow strength hardcoded
**Accept (deferred)**

Already have the gutter shadow — `drawGutterShadow(p, halfW, 0, contentH, 0.35)` is present. The gap Agent 3 identified is the hardcoded strength with no user control. Proposal is: `m_gutterShadow` member (default 0.35), preset submenu in right-click context menu. I'll wire this into the context menu work (see Agent 5 M4). One natural place for it: the spread override context menu already exists in double-page mode; add a shadow submenu there.

---

### Agent 3 — R3: HiDPI fractional scaling
**Accept**

Real gap. At DPR=1.25, our `QPixmap canvas(canvasW, contentH)` is created at logical resolution and Qt scales it up 1.25× for display, rendering at 80% of device sharpness. Agent 3's fix is correct: use `devicePixelRatioF()` to create a physical-resolution pixmap and call `setDevicePixelRatio(dpr)` on it so layout geometry stays in logical pixels.

Two sites to fix:
1. `ComicReader::displayCurrentPage()` — wrap all canvas QPixmap creation
2. `ScrollStripCanvas::onPageDecoded()` — scale pixmaps at physical resolution

I'll apply Agent 3's exact code. The important note: ALL draw coordinates stay in logical pixels after `setDevicePixelRatio` — Qt handles the DPR mapping automatically. This is the correct pattern.

---

### Agent 3 — R4: Scroll jank (SmoothScrollArea root cause)
**Accept**

Agent 3's four-bug diagnosis matches Agent 5's R1 finding. Both identified the same four issues:
1. No `max_step` cap → spikes on fast scroll
2. No backlog cap → unlimited pending accumulation
3. Integer-only accumulation → mechanical feel at deceleration tail
4. No `pixelDelta` support → trackpad input incorrectly converted

Both agents provide nearly identical fix code. I'll implement the merged version (Agent 5's R1 implementation covers all four bugs and is slightly cleaner structurally). Will add `m_smoothY`, `m_draining`, the `valueChanged` sync connection, and the pixelDelta preference. Also adding `syncExternalScroll()` public method for `reflowScrollStrip()` to call after page jumps (per Agent 3's M4 dependency).

---

### Agent 3 — M1: Fit Width mode for double page
**Already have it**

Agent 3 reached this conclusion as well: our double-page already fills viewport width with each page at `halfW = availW/2`. No gap vs Max's base fit-width behavior.

---

### Agent 3 — M2: Horizontal pan when zoomed in double-page
**Accept**

Real gap. We have `m_zoomPct` and Ctrl+wheel working, but `setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff)` means zoomed content clips silently. Agent 3's approach: `m_panX` int clamped to `[0, maxPanX]`, applied as offset during canvas drawing, reset on page turn, updated on center-zone drag and arrow keys. This matches what Groundwork and Max both do. Will implement.

---

### Agent 3 — M3: Page dimensions cache (header parsing)
**Accept — use Agent 4's integration plan**

Agent 3 proposes a new `ImageDimensions.h/.cpp` file. Agent 4 points out that `ArchiveReader::parseImageDimensions()` **already exists** but isn't wired. Agent 4's approach (emit `dimensionsReady` from `DecodeTask`, connect in `requestDecode()`) achieves the same goal without a new file. I'll use Agent 4's integration plan — it's the right call. No new file needed; just wire what's already built.

See Agent 4 M2 acceptance for the detail.

---

### Agent 3 — M4: Scroll position fractional preservation on resize
**Accept**

Real gap. `resizeEvent → reflowScrollStrip()` preserves the absolute scrollbar integer but total canvas height changes on reflow, so the reader drifts to a different page. Agent 3's fix — save fraction before reflow, restore after — is correct. Requires `SmoothScrollArea::syncExternalScroll(val)` which comes from the R4/R1 SmoothScrollArea rewrite. The two are naturally coupled.

---

### Agent 3 — M5: Spread detection from dims cache
**Accept — depends on M3 wiring**

Once dims are flowing from `dimensionsReady` signals (M3 from Agent 4), spread detection is a trivial extension: `isSpread = (h > 0 && double(w)/h >= 1.08)`. This eliminates the layout jump that currently fires when a spread page is discovered post-decode in scroll strip mode. Will implement as part of the M3 wiring work.

---

## Response to Agent 5 (UX, Scroll, Input)

---

### Agent 5 — R1: SmoothScrollArea three missing behaviors
**Accept**

Same root cause as Agent 3 R4. The fix is identical. Will implement once (merged version). Agent 5's fix is slightly more idiomatic Qt (uses `qBound`, `qMax`) so I'll use that as the base and verify it covers Agent 3's dominant-axis selection too. It does — Agent 3's dominant-axis logic can be added as a one-line extension if needed, but for vertical-only scroll strip it's not critical.

---

### Agent 5 — R2: HUD freeze — overlays don't gate click zones
**Accept**

Confirmed the bug. `mousePressEvent` fires left/right zone nav unconditionally. If `m_endOverlay` is visible and the user clicks the left zone, `prevPage()` fires behind the card. Agent 5's `isAnyOverlayOpen()` helper is the clean fix. Needs to gate `mousePressEvent`. Will add the helper and the gate.

```cpp
bool ComicReader::isAnyOverlayOpen() const {
    if (m_endOverlay  && m_endOverlay->isVisible())  return true;
    if (m_volOverlay  && m_volOverlay->isVisible())  return true;
    if (m_gotoOverlay && m_gotoOverlay->isVisible()) return true;
    if (m_keysOverlay && m_keysOverlay->isVisible()) return true;  // add keys overlay too
    return false;
}
```

I'll also add `m_keysOverlay` to the check since we're adding that overlay now (Agent 2 M2).

---

### Agent 5 — R3: Click zone flash missing "blocked" state
**Accept**

In ScrollStrip mode, left/right clicks are swallowed silently. Max shows a dim warm-tint flash to confirm receipt. Agent 5's proposal: update `flashClickZone(side, bool blocked = false)` signature, use `rgba(255,200,100,22)` for blocked vs `rgba(255,255,255,38)` for normal. Will apply. Small but important — currently users think their clicks aren't registering.

---

### Agent 5 — R4: Page counter and scrub bar stale in scroll strip
**Accept**

Confirmed. The `m_stripRefreshTimer` lambda calls `refreshVisibleStripPages()` and `saveCurrentProgress()` but never calls `updatePageLabel()` or updates `m_scrubBar`. In scroll strip mode, `showPage()` is not called on scroll, so the page counter freezes at the mode-switch value.

The fix needs a `computePageInView()` method to find which page occupies viewport center. Agent 5's lambda extension is correct. I'll also need to implement `computePageInView()` — it's not currently in my code. Logic: iterate `m_stripCanvas` y-offsets and find the entry that straddles `viewport_center_y`.

---

### Agent 5 — R5: HUD auto-hide mode distinction
**Already have it**

Agent 5 confirms this — our H-toggle-only model correctly matches Max's Manual Scroll mode (no auto-hide timer). No action needed.

---

### Agent 5 — M1: VerticalThumb (right-edge progress bar)
**Accept**

No equivalent exists currently. Agent 5's implementation is complete and ready. `VerticalThumb` as an inner class, 12px wide, right edge, visible only in ScrollStrip mode, `progressRequested(double)` signal → vbar setValue. I'll implement this as designed. One question for Agent 5: should VerticalThumb dragging count as "overlay open" for the `isAnyOverlayOpen()` freeze gate? I think yes — add a `m_thumbDragging` flag. Will handle on implementation.

---

### Agent 5 — M2: Direction toast position
**Accept**

Trivial one-line fix. Toast currently positions at screen center, overlapping page content. Should be `toolbar.y() - toastHeight - 8`. Agent 5's fix is exact. Will apply.

---

### Agent 5 — M3: Modal scrim for overlays
**Accept**

For volume navigator and goto dialog: a full-size `ClickScrim` widget behind the overlay card dismisses on click. Agent 5's `ClickScrim` inner class is clean. End overlay already has adequate keyboard cascade (Esc → back to library) so I'll apply scrim to `m_volOverlay` and `m_gotoOverlay` only, as Agent 5 recommends.

---

### Agent 5 — M4: Right-click context menu in all modes
**Accept**

Currently `contextMenuEvent` only shows the spread override menu in double-page mode. In single-page and scroll strip, right-click does nothing. Agent 5's proposal: go-to-page, copy path, reveal in explorer using `ContextMenuHelper`. Also suggests delegating to `showSpreadOverrideMenu` for double-page, which is the existing behavior. Will keep double-page behavior untouched and add the general menu for other modes.

I'll also add the gutter shadow strength presets here (Agent 3 R2 deferred item). Right-click → "Gutter Shadow" submenu with Off / Subtle / Medium / Strong — but only visible in double-page mode.

---

### Agent 5 — M5: Double-click fullscreen
**Already have it**

Confirmed. `mouseDoubleClickEvent` center zone → `emit fullscreenRequested(bool)` with 220ms debounce via `m_clickTimer`. Correct.

---

### Agent 5 — M6: Ctrl+Wheel zoom in double page
**Already have it**

Confirmed. `wheelEvent` checks `m_isDoublePage && modifiers & Qt::ControlModifier` → `zoomBy(±20)`. Correct.

---

### Agent 5 — M7: Side nav arrow zones
**Accept**

The left/right thirds are functional click zones but have no visual arrow. New users can't discover navigation by hovering. Agent 5's `SideNavArrow` inner class is clean — `WA_TransparentForMouseEvents`, `paintEvent` with drop-shadow text, hover-toggled from `mouseMoveEvent`. Hidden in scroll strip mode (side zones are inactive there). Will implement as designed. The `mouseMoveEvent` already tracks zone entry for cursor hide logic — extending it to update the arrows is natural.

---

## Response to Agent 2 (Navigation, State Machine)

---

### Agent 2 — R1: Progress payload missing path + 5 fields
**Accept — CONTRACTS violation, highest priority**

As I disclosed at the top: `saveCurrentProgress()` currently only saves `page` and `pageCount`. No `path`. This violates CONTRACTS.md and breaks Agent 5's continue strip.

Agent 2's full replacement of `saveCurrentProgress()` is correct. I'll implement it. The fields being added:
- `path` — CONTRACTS.md requirement, Agent 5's continue strip blocker
- `maxPageIndexSeen` — for continue strip completion percentage
- `knownSpreadIndices` / `knownNormalIndices` — avoid re-detection on re-open
- `updatedAt` — for continue strip sort order
- `couplingMode` / `couplingPhase` — avoid re-detection on re-open
- `bookmarks` — pending M3 implementation
- `finished` / `finishedAt` — auto-finish detection + context menu mark-as-read

One note on the `pairForPage()` call in the R1 fix: this returns a struct from my pairing engine. I'll need to verify the exact API name matches what's in my code. Conceptually correct; I'll adjust the call if the method signature differs.

---

### Agent 2 — R2: Coupling state not restored on re-open
**Accept**

`restoreSavedPage()` currently only reads `page`. The coupling fields `m_couplingMode`, `m_couplingPhase`, `m_couplingResolved` are reset to defaults in `openBook()` and never restored. Agent 2's replacement of `restoreSavedPage()` is correct — restore coupling from JSON before `buildCanonicalPairingUnits()`, restore bookmarks from JSON. Also restore `knownSpreadIndices` into `m_pageMeta` (the R1 fix saves them; R2 should load them back to avoid re-detection). Agent 2's code doesn't explicitly restore spread knowledge from JSON — I'll add that to the R2 fix when implementing.

---

### Agent 2 — R3: End overlay missing Qt::Key_PageDown
**Accept**

Confirmed at line 1646: `case Qt::Key_Space: case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Right:` — `Key_PageDown` absent. In normal mode (line 1666), `Key_PageDown` is mapped to `nextPage()`. Inconsistent. One-liner fix. Will apply.

---

### Agent 2 — R4: Volume navigator search selection update
**Accept**

Confirmed the gap. The current lambda only updates selection if the current item is hidden. Agent 2's fix — always jump selection to first match on text change, restore to current volume on clear — matches Max and Groundwork behavior. Will apply.

---

### Agent 2 — M1: GoToPage key-blocking gap
**Accept**

GoToPage dialog is implemented. Ctrl+G works. But `keyPressEvent` only handles Escape for the goto overlay (line 1671). Space, Right, Left, and other nav keys fall through to the switch and fire page navigation while the dialog is open. Agent 2's fix: add a blocking gate at the top of `keyPressEvent` before the vol overlay check, swallowing all keys except Escape while goto is open. Will apply.

---

### Agent 2 — M2: Keys help overlay (K key)
**Accept — not yet built**

STATUS.md lists this as my next task, but it is not implemented yet. Agent 2 provides the complete `ComicReader::toggleKeysOverlay()` implementation with lazy creation, two-column layout, and key-blocking gate. This is exactly what I would have built. Will use Agent 2's implementation.

Note: I'll update the shortcut list to match exactly what we ship — some shortcuts listed (like B, Z, R, S, Alt+Left/Right) are from this congress and not yet in the reader. I'll wire those first, then build the overlay with the correct complete list.

---

### Agent 2 — M3: Bookmarks (B key)
**Accept**

Not yet built. Agent 2's implementation is clean — `QSet<int> m_bookmarks`, `toggleBookmark()`, toast on toggle, `saveCurrentProgress()` call. The `bookmarks` field is already in the R1 progress payload. The R2 `restoreSavedPage()` restores bookmarks from JSON. Will implement as designed.

---

### Agent 2 — M4: Instant Replay (Z key)
**Accept**

Agent 2's simplified port is correct for our architecture. In scroll strip: scroll back 30% of viewport height. In page-flip modes: `prevPage()`. Show toast. The full Max implementation is more complex (crosses page boundaries based on y-coordinate), but our simplified version is correct for the modes we have. Will implement.

---

### Agent 2 — M5: Clear Resume (R key)
**Accept**

Trivial. Agent 2's implementation resets progress to zero and clears `m_bookmarks`. Will implement. Note: `R` could conflict with any existing binding — checking... `Qt::Key_R` is not currently bound in my switch. Safe to assign.

---

### Agent 2 — M6: Manual Checkpoint Save (S key)
**Accept**

Trivial. Our `saveCurrentProgress()` is already synchronous. `saveCheckpoint()` just calls it and shows a toast. Checking... `Qt::Key_S` is not currently bound. Safe to assign.

---

### Agent 2 — M7 + M8: Auto-finish detection + maxPageIndexSeen
**Accept — covered by R1**

Agent 2 correctly notes both are handled by the R1 progress payload fix. No separate code needed beyond what R1 already includes.

---

### Agent 2 — M9: Series settings per-seriesId
**Accept (Modified — defer full system)**

The skeleton Agent 2 provides — three fields via QSettings keyed by series SHA1 — is the right scope. Full Max-style settings system (all mode/coupling/zoom fields, applied on every open) is significant scope. I'll implement the three-field version: `portraitWidthPct`, `readerMode`, `couplingPhase`. `saveSeriesSettings()` called at end of `saveCurrentProgress()`, `applySeriesSettings()` called in `openBook()` before `showPage(startPage)`.

---

### Agent 2 — M10: Home/End keys
**Already have it**

Confirmed at lines 1668–1669:
```cpp
case Qt::Key_Home: showPage(0); break;
case Qt::Key_End: showPage(m_pageNames.size() - 1); break;
```

---

### Agent 2 — M11: Alt+Left / Alt+Right volume navigation
**Accept**

`AltModifier` is not currently checked anywhere in `keyPressEvent`. I have `prevVolume()` and `nextVolume()` methods. Agent 2's two-line fix is correct — check `Qt::AltModifier` before the main switch. Will apply. Note: must be gated by `!isAnyOverlayOpen()` too — Alt+Left inside the volume navigator would be confusing.

---

### Agent 2 — M12: "Resumed" / "Ready" toast on volume open
**Accept**

Small polish touch that Max has. `openBook()` currently calls `showToolbar()` and `showPage(startPage)` with no toast. Agent 2's `QTimer::singleShot(250, ...)` approach fires after the first frame draws. Correct. Will implement. The `updatedAt` field in the R1 progress payload is what I'll use to detect "had prior progress" — cleaner than checking any field.

---

## Response to Agent 4 (File Loading, Format, Archive)

---

### Agent 4 — R1: Natural sort
**Already have it**

Agent 4 confirms: `QCollator` with `setNumericMode(true)` is correct. No action needed.

---

### Agent 4 — R2: Format detection by content
**Already have it**

Agent 4 confirms: `QPixmap::loadFromData(data)` with no format argument probes by magic bytes when given a byte array. The EXIF fix (R3) switches to `QImageReader` explicitly, which reinforces this. No standalone action needed.

---

### Agent 4 — R3: EXIF rotation
**Accept**

Real gap. `QPixmap::loadFromData` doesn't support `setAutoTransform`. Agent 4's fix is a clean rewrite of `DecodeTask::run()` using `QImageReader` with `setDecideFormatFromContent(true)` and `setAutoTransform(true)`. This also satisfies R2 explicitly. Will apply.

Note: Agent 4's fix and Agent 3's R4 `DecodeTaskSignals` changes both touch `DecodeTask`. I'll combine them in one pass:
1. Agent 3/4 R4: add `volumeId` to signal and constructor
2. Agent 4 R3: switch to `QImageReader` with autoTransform
3. Agent 4 M2: emit `dimensionsReady` before full decode

These three changes all happen in `DecodeTask::run()` and `DecodeTaskSignals`. Will apply together.

---

### Agent 4 — R4: Stale decode detection
**Accept**

Confirmed bug: `openBook()` does not clear `m_inflightDecodes`. Old `DecodeTask` results from a previous volume can populate the new volume's cache. Agent 4's `m_volumeId` counter approach is correct — increment on `openBook()`, thread through `DecodeTask` constructor, guard in `onPageDecoded()`. `Qt::QueuedConnection` ensures the guard fires correctly across thread boundary.

Will apply. Breaking changes to `DecodeTask` constructor and signal are contained inside my own files.

---

### Agent 4 — R5: Loading status label
**Accept (minimal fix, async deferred)**

Confirmed: `openBook()` calls `ArchiveReader::pageList()` synchronously on the main thread with no feedback. Agent 4's minimal fix — show "Loading..." label before the call, hide after — is the right scope for now. The full async `IndexerThread` rewrite is the correct long-term solution but is significant scope and should come after the congress items are done.

Will implement Agent 4's minimal fix. Also implementing the error distinction: `fi.exists() ? "No image pages found" : "File not found: " + filename`. Currently both cases return empty and the reader opens blank.

---

### Agent 4 — R6: LRU MB budget
**Already have it**

Agent 4 confirms: `PageCache::pixmapBytes()` returns `width * height * 4`, default 512MB budget, LRU eviction by cost. No gap.

---

### Agent 4 — M1: CBR/RAR support via libarchive
**Accept (conditional, Phase C)**

Agent 4's libarchive approach is clean — no API change to `ArchiveReader.h`, conditional compile via `HAS_LIBARCHIVE`, `isCbr()` dispatch. This is the right way to add it. It's purely additive inside my territory (`ArchiveReader.cpp`) and the CMakeLists addition is conditional so it degrades gracefully without the lib.

However: this depends on libarchive being installed at `C:/tools/libarchive/`. I'll treat this as Phase C — implement the code now so it's ready, but not block other items on the library installation. Will coordinate with Agent 0 if a CMakeLists change is needed (will announce in chat.md before touching it).

---

### Agent 4 — M2: Fast dimension parsing from header bytes (wiring existing parser)
**Accept — this is the right approach**

Agent 4's key insight: `ArchiveReader::parseImageDimensions()` **already exists**. The gap is that it's never called. Agent 4's integration plan adds a `dimensionsReady` signal to `DecodeTaskSignals`, emits it from `DecodeTask::run()` after reading the first 4KB of the data buffer (before the full decode), and handles it in `ComicReader::requestDecode()` via `QueuedConnection`.

This is better than Agent 3's M3 approach (create a new file) since the parser is already there. I'll use Agent 4's plan.

The `ScrollStripCanvas::updatePageDimensions(int index, int w, int h)` method Agent 4 mentions as a dependency — that doesn't exist yet. I'll add it: recalculate the page height slot for that index and trigger a partial reflow. This eliminates the layout jump when pages decode in scroll strip mode.

---

### Agent 4 — M3: Memory saver mode
**Accept**

`PageCache::setBudget()` already exists. This is purely UI wiring. Three lines of logic as Agent 4 says. Will add `toggleMemorySaver()`, wire into right-click context menu, persist via QSettings. Low effort, good feature.

---

## Priority Order (my view going into implementation)

Based on severity and dependencies:

**Immediate (CONTRACTS / correctness):**
1. R1 (Agent 2) — progress payload: add `path` + all missing fields. CONTRACTS violation. Unblocks Agent 5 continue strip.
2. R4 (Agent 4) — stale decode detection. Cache correctness bug.
3. R4 (Agent 3/5) — SmoothScrollArea jank fix. The primary UX complaint from the congress motion.

**High (rendering quality):**
4. R3 (Agent 4) — EXIF rotation: rewrite DecodeTask with QImageReader
5. M2 (Agent 4) — wire existing dims parser: emit dimensionsReady, add ScrollStripCanvas::updatePageDimensions → eliminates scroll strip layout jumps
6. M5 (Agent 3) — spread detection from dims: trivial extension after M2
7. R3 (Agent 3) — HiDPI pixmap creation in displayCurrentPage and ScrollStripCanvas
8. R1 (Agent 3) — SmoothPixmapTransform in ScrollStripCanvas::paintEvent (one-liner)

**Core UX (missing features):**
9. R2 (Agent 2) — coupling restore on re-open
10. M2 (Agent 2) — keys help overlay (K key)
11. M3 (Agent 2) — bookmarks (B key)
12. M4, M5, M6 (Agent 2) — Z/R/S keys (instant replay, clear resume, checkpoint save)
13. M11 (Agent 2) — Alt+Left/Right volume nav
14. R2 (Agent 5) — isAnyOverlayOpen() gate
15. R4 (Agent 5) — page counter sync in scroll strip
16. M12 (Agent 2) — Resumed/Ready toast

**Polish:**
17. R3 (Agent 2) — PageDown in end overlay (one-liner)
18. R4 (Agent 2) — volume navigator search selection
19. M1 (Agent 2) — goto key-blocking gate
20. R3 (Agent 5) — blocked flash in scroll strip side clicks
21. M2 (Agent 5) — toast position above toolbar
22. M3 (Agent 5) — modal scrim
23. M4 (Agent 5) — right-click context menu all modes
24. M7 (Agent 5) — side nav arrow zones
25. M1 (Agent 5) — VerticalThumb right-edge scroller
26. R2 (Agent 3) — gutter shadow user control (in right-click menu)
27. M2 (Agent 3) — horizontal pan when zoomed
28. M4 (Agent 3) — scroll position fractional preservation on resize
29. M9 (Agent 2) — series settings skeleton

**Phase C (conditional on external deps):**
30. M1 (Agent 4) — CBR/RAR via libarchive
31. M3 (Agent 4) — memory saver toggle
32. R5 (Agent 4) — loading label (minimal fix is quick, async rewrite is Phase C)

---

## One thing I want Agent 0 to factor into the synthesis

The four agents produced 30+ actionable items. That is a lot of work. Several of them compound — the DecodeTask changes from Agent 3 R4 + Agent 4 R3 + Agent 4 M2 must be done in one pass because they all modify the same signal signature. Similarly, the SmoothScrollArea rewrite from Agent 3 R4 and Agent 5 R1 is the same change. The progress payload fix from Agent 2 R1 and the restore fix from Agent 2 R2 are tightly coupled.

My request: in the synthesis, please group these into implementation batches that touch the same file in the same pass. Multiple small ordered passes are fine. What I want to avoid is implementing Agent 3's R4 SmoothScrollArea fix, then later implementing Agent 5's R1 and finding I need to redo the same file. Batching reduces the clean-rebuild cost.
