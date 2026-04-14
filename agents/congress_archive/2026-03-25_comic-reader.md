# Congress

One motion at a time. When resolved, Agent 0 archives to `congress_archive/YYYY-MM-DD_[topic].md` and resets this file to the empty template. Then posts one line in chat.md.

---

## CONGRESS — STATUS: OPEN
Opened by: Agent 0
Date: 2026-03-25

## Motion

Accelerate Agent 1's comic reader to completion. Agents 2, 3, 4, and 5 each review Tankoban-Max and the Groundwork comic reader, compare against the current C++ implementation, and return specific actionable input — including C++ solutions where possible. Agent 1 joins to respond. Agent 0 synthesizes. This is a technical brief, not a feedback session.

## Two Problems to Solve First

1. **Double-page scroll jank** — wheel events feel like spikes in the road, scrolling is mechanical
2. **Image quality** — FastTransformation in scroll mode looks like 2002; quality in other modes is still below Max standard

## Features Not To Address

Auto-scroll, auto-flip, image filters (brightness/contrast/invert/grayscale/sepia), loupe magnifier. Explicitly cut. Do not propose them.

## How This Congress Works

Agents 2, 3, 4, 5 are summoned in parallel — each writes to their own position file (no conflicts):

| Agent | Position File | Assignment |
|-------|--------------|------------|
| Agent 3 | `congress_positions/agent3_position.md` | Rendering lead — image quality + scroll jank fixes |
| Agent 5 | `congress_positions/agent5_position.md` | UX patterns lead — HUD, shortcuts, missing UI |
| Agent 2 | `congress_positions/agent2_position.md` | Navigation + missing features ordered list |
| Agent 4 | `congress_positions/agent4_position.md` | Format edge cases + integration pipeline |

After all four positions are in, Agent 1 is summoned to respond:
- `congress_positions/agent1_response.md` — Agent 1 accepts, challenges, or asks questions

Agent 0 goes last — reads all five files, writes synthesis below.

## Agent 0 Synthesis

All five position files read. Agent 1 accepted every finding — zero rejections.
Agent 1 requested batching by file to minimize clean rebuilds. Synthesis is structured accordingly.

---

### Batch A — DecodeTask (one combined pass)
**Files:** `src/ui/readers/DecodeTask.h`, `src/ui/readers/DecodeTask.cpp`

All three changes touch the same signal signature. Implementing separately means touching this file three times and rebuilding three times. Do in one pass.

**A1 — Stale decode detection (Agent 4 R4)**
Add `int m_volumeId` to `DecodeTask` constructor. Add `int volumeId` parameter to `DecodeTaskSignals::decoded` signal. In `ComicReader::onPageDecoded()`, guard: `if (volumeId != m_currentVolumeId) return`. In `ComicReader::openBook()`, increment `m_currentVolumeId` and clear `m_inflightDecodes` before loading new volume.

**A2 — EXIF rotation (Agent 4 R3)**
Replace `QPixmap::loadFromData(data)` in `DecodeTask::run()` with:
```cpp
QBuffer buf(&data);
buf.open(QIODevice::ReadOnly);
QImageReader reader(&buf);
reader.setDecideFormatFromContent(true);
reader.setAutoTransform(true);
QImage image = reader.read();
if (image.isNull()) { emit notifier.failed(m_pageIndex); return; }
QPixmap pix = QPixmap::fromImage(std::move(image));
emit notifier.decoded(m_pageIndex, pix, pix.width(), pix.height(), m_volumeId);
```
Add `#include <QImageReader>`, `#include <QBuffer>`.

**A3 — Fast dimension signal (Agent 4 M2)**
Add `void dimensionsReady(int pageIndex, int w, int h)` to `DecodeTaskSignals`. In `DecodeTask::run()`, before full decode, read first 4KB and call `ArchiveReader::parseImageDimensions()` (already exists). Emit `dimensionsReady` with result. In `ComicReader::requestDecode()`, connect `dimensionsReady` via `QueuedConnection` to `ScrollStripCanvas::updatePageDimensions()`.

---

### Batch B — SmoothScrollArea (one combined pass)
**Files:** `src/ui/readers/SmoothScrollArea.h`, `src/ui/readers/SmoothScrollArea.cpp`

Agent 3 R4 and Agent 5 R1 diagnosed the same four bugs. This is one fix, implemented once.

**Four bugs:**
1. `angleDelta` only — trackpad sends `pixelDelta`, we over-convert and ignore it
2. No `maxStep` cap — fast trackpad burst causes spike (`pendingPx * 0.38` unbounded)
3. No backlog cap — unlimited `m_pendingPx` accumulation
4. No `m_smoothY` float sync — scrub/key jump then wheel event lurches

**Fix:**
```cpp
// SmoothScrollArea.h — add members
double m_smoothY = 0.0;
bool   m_draining = false;
void   syncExternalScroll(int val);  // public — for page jumps and scrub bar

// Constructor — sync m_smoothY when scrollbar moves externally
connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int val) {
    if (!m_draining) m_smoothY = double(val);
});

// wheelEvent — prefer pixelDelta, add backlog cap
void SmoothScrollArea::wheelEvent(QWheelEvent* event) {
    double px = 0.0;
    QPoint pdp = event->pixelDelta();
    if (!pdp.isNull() && pdp.y() != 0) px = -double(pdp.y());
    else px = -double(event->angleDelta().y()) * (100.0 / 120.0);
    if (px == 0.0) { event->ignore(); return; }
    double cap = qMax(2400.0, double(viewport()->height() * 8));
    m_pendingPx = qBound(-cap, m_pendingPx + px, cap);
    if (!m_drainTimer.isActive()) m_drainTimer.start();
    event->accept();
}

// drainWheel — add maxStep cap, use m_smoothY for precise float scrolling
void SmoothScrollArea::drainWheel() {
    if (std::abs(m_pendingPx) < SNAP_THRESHOLD) {
        m_pendingPx = 0.0; m_drainTimer.stop(); return;
    }
    double maxStep = qMax(70.0, double(viewport()->height()) * 0.22);
    double step = qBound(-maxStep, m_pendingPx * DRAIN_FRACTION, maxStep);
    if (std::abs(step) < 2.0) step = m_pendingPx;
    m_pendingPx -= step;
    auto* vbar = verticalScrollBar();
    if (!vbar) return;
    m_smoothY += step;
    m_smoothY = qBound(double(vbar->minimum()), m_smoothY, double(vbar->maximum()));
    int newVal = int(std::round(m_smoothY));
    if (newVal != vbar->value()) {
        m_draining = true; vbar->setValue(newVal); m_draining = false;
    }
}

// syncExternalScroll — called by page jumps and scrub bar to reset float baseline
void SmoothScrollArea::syncExternalScroll(int val) {
    m_smoothY = double(qBound(verticalScrollBar()->minimum(), val, verticalScrollBar()->maximum()));
}
```

---

### Batch C — Progress payload
**Files:** `src/ui/readers/ComicReader.h`, `src/ui/readers/ComicReader.cpp`

Ship C1 and C2 together — save writes the fields, restore reads them.

**C1 — saveCurrentProgress() rewrite (Agent 2 R1 — CONTRACTS violation)**
The current implementation saves only `page` and `pageCount`. This violates CONTRACTS.md (path is non-negotiable) and breaks Agent 5's continue strip. Agent 1 confirmed the violation in their response.

Replace `saveCurrentProgress()` with the full payload:
```cpp
void ComicReader::saveCurrentProgress() {
    if (!m_bridge || m_pageNames.isEmpty()) return;
    QJsonObject prev = m_bridge->progress("comics", itemIdForPath(m_cbzPath));
    int maxSeen = qMax(prev["maxPageIndexSeen"].toInt(0), m_currentPage);
    // double-page: also count left page of current pair
    QJsonArray spreadsArr, normalsArr;
    for (int i = 0; i < m_pageMeta.size(); ++i) {
        if (m_pageMeta[i].decoded) {
            if (m_pageMeta[i].isSpread) spreadsArr.append(i);
            else normalsArr.append(i);
        }
    }
    QJsonArray bookmarksArr;
    for (int b : m_bookmarks) bookmarksArr.append(b);
    QJsonObject data;
    data["page"]               = m_currentPage;
    data["pageCount"]          = m_pageNames.size();
    data["path"]               = m_cbzPath;          // CONTRACTS.md — non-negotiable
    data["maxPageIndexSeen"]   = maxSeen;
    data["knownSpreadIndices"] = spreadsArr;
    data["knownNormalIndices"] = normalsArr;
    data["updatedAt"]          = QDateTime::currentMSecsSinceEpoch();
    data["couplingMode"]       = m_couplingMode;
    data["couplingPhase"]      = m_couplingPhase;
    data["bookmarks"]          = bookmarksArr;
    if (prev.contains("finished"))   data["finished"]   = prev["finished"];
    if (prev.contains("finishedAt")) data["finishedAt"] = prev["finishedAt"];
    int total = m_pageNames.size();
    if (total > 0 && maxSeen >= total - 1 && !data["finished"].toBool(false)) {
        data["finished"]   = true;
        data["finishedAt"] = QDateTime::currentMSecsSinceEpoch();
    }
    m_bridge->saveProgress("comics", itemIdForPath(m_cbzPath), data);
}
```
Add `#include <QDateTime>`.

**C2 — restoreSavedPage() rewrite (Agent 2 R2)**
```cpp
int ComicReader::restoreSavedPage() {
    if (!m_bridge) return 0;
    QJsonObject data = m_bridge->progress("comics", itemIdForPath(m_cbzPath));
    if (data.isEmpty()) return 0;
    // Restore coupling — prevents re-detection on re-open
    QString savedMode  = data["couplingMode"].toString();
    QString savedPhase = data["couplingPhase"].toString();
    if (!savedMode.isEmpty() && savedMode != "auto") {
        m_couplingMode       = savedMode;
        m_couplingPhase      = savedPhase.isEmpty() ? "normal" : savedPhase;
        m_couplingConfidence = 1.0f;
        m_couplingResolved   = true;
        if (m_isDoublePage) { invalidatePairing(); buildCanonicalPairingUnits(); }
    }
    // Restore spread knowledge — prevents layout jump on scroll strip re-open
    for (const auto& v : data["knownSpreadIndices"].toArray()) {
        int i = v.toInt();
        if (i >= 0 && i < m_pageMeta.size()) m_pageMeta[i].isSpread = true;
    }
    // Restore bookmarks
    m_bookmarks.clear();
    for (const auto& v : data["bookmarks"].toArray()) m_bookmarks.insert(v.toInt());
    int page = data["page"].toInt(0);
    if (page >= m_pageNames.size()) page = 0;
    return page;
}
```

---

### Batch D — Missing keyboard features
**Files:** `src/ui/readers/ComicReader.h`, `src/ui/readers/ComicReader.cpp`

All new methods and `keyPressEvent` cases. No other files touched.

**D1 — Keys help overlay / K key (Agent 2 M2)**
Add `QWidget* m_keysOverlay = nullptr` and `void toggleKeysOverlay()`. Lazy-created two-column QWidget listing all shortcuts. Key-blocking gate in `keyPressEvent`:
```cpp
if (m_keysOverlay && m_keysOverlay->isVisible()) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_K)
        toggleKeysOverlay();
    event->accept(); return;
}
```
Add `case Qt::Key_K: toggleKeysOverlay(); break;` to switch.

**D2 — Bookmarks / B key (Agent 2 M3)**
Add `QSet<int> m_bookmarks` and `void toggleBookmark()`. Toggle current page index, show toast, call `saveCurrentProgress()`. Add `case Qt::Key_B: toggleBookmark(); break;`.

**D3 — Instant Replay / Z key (Agent 2 M4)**
```cpp
void ComicReader::instantReplay() {
    if (m_pageNames.isEmpty()) return;
    if (m_isScrollStrip) {
        if (auto* vbar = m_scrollArea->verticalScrollBar())
            vbar->setValue(qMax(0, vbar->value() - int(m_scrollArea->viewport()->height() * 0.30)));
    } else { prevPage(); }
    showToast("Replay");
}
```
Add `case Qt::Key_Z: instantReplay(); break;`.

**D4 — Clear Resume / R key (Agent 2 M5)**
Reset progress to zero, clear `m_bookmarks`, show toast "Resume cleared". Add `case Qt::Key_R: clearResume(); break;`.

**D5 — Checkpoint Save / S key (Agent 2 M6)**
Call `saveCurrentProgress()`, show toast "Checkpoint saved". Add `case Qt::Key_S: saveCheckpoint(); break;`.

**D6 — Alt+Left/Right volume nav (Agent 2 M11)**
Before main switch, after all overlay gates:
```cpp
if (event->modifiers() & Qt::AltModifier) {
    if (event->key() == Qt::Key_Left && !isAnyOverlayOpen())  { prevVolume(); event->accept(); return; }
    if (event->key() == Qt::Key_Right && !isAnyOverlayOpen()) { nextVolume(); event->accept(); return; }
}
```

**D7 — Resumed/Ready toast (Agent 2 M12)**
In `openBook()`, after `showPage(startPage)`:
```cpp
bool hadProgress = m_bridge && m_bridge->progress("comics", itemIdForPath(cbzPath)).contains("updatedAt");
QTimer::singleShot(250, this, [this, hadProgress]() { showToast(hadProgress ? "Resumed" : "Ready"); });
```

**D8 — PageDown in end overlay (Agent 2 R3 — one-liner)**
Add `case Qt::Key_PageDown:` to the end overlay advance group at `keyPressEvent` line ~1646.

**D9 — Volume navigator search selection (Agent 2 R4)**
Replace connect lambda: always jump selection to first match on type; restore to current volume index on clear.

**D10 — GoToPage key-blocking gate (Agent 2 M1)**
Add gate before vol overlay check in `keyPressEvent`:
```cpp
if (m_gotoOverlay && m_gotoOverlay->isVisible()) {
    if (event->key() == Qt::Key_Escape) hideGoToDialog();
    event->accept(); return;
}
```

**D11 — Series settings skeleton (Agent 2 M9)**
`seriesSettingsKey()` via SHA1 of series name or folder path. `saveSeriesSettings()` writes 3 fields (`portraitWidthPct`, `readerMode`, `couplingPhase`) to QSettings. `applySeriesSettings()` reads them. Call apply in `openBook()` before `showPage()`, call save at end of `saveCurrentProgress()`.

---

### Batch E — ScrollStripCanvas rendering
**Files:** `src/ui/readers/ScrollStripCanvas.h`, `src/ui/readers/ScrollStripCanvas.cpp`

**E1 — SmoothPixmapTransform (Agent 3 R1 — one-liner)**
```cpp
QPainter p(this);
p.setRenderHint(QPainter::SmoothPixmapTransform, true);  // add this line
```

**E2 — HiDPI physical-resolution scaling (Agent 3 R3 strip path)**
In `onPageDecoded()`, replace logical-pixel scale with:
```cpp
double dpr = devicePixelRatioF();
int physW = static_cast<int>(pw * dpr + 0.5);
QPixmap scaled = fullRes.scaledToWidth(physW, Qt::SmoothTransformation);
scaled.setDevicePixelRatio(dpr);
m_scaledCache[pageIndex] = scaled;
// geometry (yOffset, drawX, heights) stays in logical px
```

**E3 — updatePageDimensions() (Agent 4 M2 wiring)**
New method called from `dimensionsReady` signal. Updates height slot for that page index. Triggers partial reflow. Eliminates layout jump when pages decode.

**E4 — Spread detection from dims (Agent 3 M5)**
In `updatePageDimensions()`: if `w > 0 && h > 0 && double(w)/h >= 1.08`, mark page as spread. No full decode needed. Feeds Two-Page Scroll row layout.

**E5 — Page counter sync (Agent 5 R4)**
In `m_stripRefreshTimer` lambda, add `computePageInView()` (binary search y-offsets for viewport center). Update `m_pageLabel->setText(...)` and `m_scrubBar->setProgress(...)` from computed page.

---

### Batch F — displayCurrentPage HiDPI + resize preservation
**Files:** `src/ui/readers/ComicReader.cpp`

**F1 — HiDPI canvas creation (Agent 3 R3 display path)**
All canvas `QPixmap(canvasW, contentH)` construction:
```cpp
double dpr = devicePixelRatioF();
int physW = static_cast<int>(canvasW * dpr + 0.5);
int physH = static_cast<int>(contentH * dpr + 0.5);
QPixmap canvas(physW, physH);
canvas.setDevicePixelRatio(dpr);
canvas.fill(Qt::black);
// QPainter draw coords stay in logical px — DPR applied automatically
m_imageLabel->resize(QSize(canvasW, contentH));  // logical size for layout
```

**F2 — Scroll position fractional preservation on resize (Agent 3 M4)**
In `resizeEvent()`, before `reflowScrollStrip()`:
```cpp
auto* vbar = m_scrollArea->verticalScrollBar();
double fraction = (vbar && vbar->maximum() > 0) ? double(vbar->value()) / vbar->maximum() : 0.0;
reflowScrollStrip();
if (vbar->maximum() > 0)
    m_scrollArea->syncExternalScroll(int(fraction * vbar->maximum()));
```
Depends on Batch B (`syncExternalScroll`).

---

### Batch G — Input gating and overlay polish
**Files:** `src/ui/readers/ComicReader.h`, `src/ui/readers/ComicReader.cpp`

**G1 — isAnyOverlayOpen() gate (Agent 5 R2)**
```cpp
bool ComicReader::isAnyOverlayOpen() const {
    if (m_endOverlay  && m_endOverlay->isVisible())  return true;
    if (m_volOverlay  && m_volOverlay->isVisible())  return true;
    if (m_gotoOverlay && m_gotoOverlay->isVisible()) return true;
    if (m_keysOverlay && m_keysOverlay->isVisible()) return true;
    return false;
}
```
Gate at top of `mousePressEvent`: `if (isAnyOverlayOpen()) { QWidget::mousePressEvent(event); return; }`

**G2 — Blocked flash for scroll strip side clicks (Agent 5 R3)**
Update `flashClickZone(const QString& side, bool blocked = false)`. Use `rgba(255,200,100,22)` when `blocked`, `rgba(255,255,255,38)` normal. In scroll strip side-click path, call `flashClickZone(zone, true)` before returning.

**G3 — Toast position above toolbar (Agent 5 M2 — one-liner)**
```cpp
int toastY = m_toolbar->isVisible()
    ? m_toolbar->y() - m_toastLabel->height() - 8
    : height() - m_toastLabel->height() - 80;
m_toastLabel->move((width() - m_toastLabel->width()) / 2, toastY);
```

---

### Batch H — New UI widgets (inner classes, no new files)
**Files:** `src/ui/readers/ComicReader.h`, `src/ui/readers/ComicReader.cpp`

**H1 — ClickScrim (Agent 5 M3)**
Inner class: full-size `QWidget`, `rgba(0,0,0,90)`, `NoFocus`, emits `clicked()` on mouse press. Apply behind `m_volOverlay` and `m_gotoOverlay`. Connect `clicked()` to hide the overlay.

**H2 — SideNavArrow (Agent 5 M7)**
Inner class: `WA_TransparentForMouseEvents`, hover-driven. `paintEvent` draws ‹ or › in Segoe UI 22pt Black with drop-shadow offsets when `m_hover`. Toggled from `mouseMoveEvent` zone tracking (already computes left/right zone). Hidden in scroll strip mode.

**H3 — Right-click context menu (Agent 5 M4 + Agent 3 R2)**
`contextMenuEvent`: double-page mode keeps existing spread override menu, adds gutter shadow submenu (Off 0.0 / Subtle 0.22 / Medium 0.35 / Strong 0.55). All other modes show: "Go to Page..." / separator / "Copy Volume Path" / "Reveal in File Explorer" using `ContextMenuHelper`. Add `double m_gutterShadow = 0.35` member. Pass to `drawGutterShadow()` instead of hardcoded 0.35. Persist in series settings.

**H4 — VerticalThumb (Agent 5 M1)**
Inner class: 12px wide, positioned at right edge, visible only in scroll strip mode. `paintEvent` draws 3px track + 7px×54px thumb. `progressRequested(double)` signal → vbar setValue. `m_thumbDragging` bool feeds `isAnyOverlayOpen()` (so HUD doesn't auto-hide while dragging). Update progress from strip refresh timer.

---

### Batch I — Zoom pan in double page
**Files:** `src/ui/readers/ComicReader.h`, `src/ui/readers/ComicReader.cpp`

**Agent 3 M2 — Horizontal pan when zoomed**
Add `int m_panX = 0`. In `displayCurrentPage()` for double-page, apply `m_panX` as draw offset, clamped to `[0, canvasW - viewportW]`. Update `m_panX` on center-zone mouse drag. Arrow left/right when zoomed adjusts `m_panX` by step. Reset `m_panX = 0` on page turn and mode switch. Currently zoomed content clips silently because `ScrollBarAlwaysOff`.

---

### Batch J — Phase C (do last, conditional on external deps)
**Files:** `src/ui/readers/ComicReader.cpp`, `src/ui/readers/ArchiveReader.cpp`, `CMakeLists.txt`

**J1 — Loading status label (Agent 4 R5 — minimal)**
In `openBook()`: show "Loading..." label before `ArchiveReader::pageList()`, hide after. Error distinction: `fi.exists() ? "No image pages found" : "File not found"`. Quick win, defer async rewrite.

**J2 — Memory saver toggle (Agent 4 M3)**
`toggleMemorySaver()` → `PageCache::setBudget(isMemorySaver ? 256*1024*1024 : 512*1024*1024)`. Wire to right-click menu. Persist via QSettings.

**J3 — CBR/RAR support (Agent 4 M1 — conditional)**
`#ifdef HAS_LIBARCHIVE` guard. `ArchiveReader::isCbr()` dispatch. No API change. CMakeLists conditional — degrades gracefully without the library.

---

## One-Line Directive to Agent 1

**Start with Batch A. Compile and verify clean after each batch before moving to the next.**

---

## Hemanth

Final word: Approved. Execute the synthesis. Start with Batch A.
