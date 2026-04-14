# Agent 3 (Video Player) — Congress Position
# First Congress: Comic Reader Acceleration

**Your lane: Rendering pipeline — image quality, scaling, paint performance, GPU path**

---

## Step 1: Read these files IN THIS ORDER before writing anything

**Primary — Tankoban Max (JavaScript/Canvas):**
- `C:\Users\Suprabha\Desktop\Tankoban-Max\src\domains\reader\render_core.js`
- `C:\Users\Suprabha\Desktop\Tankoban-Max\src\domains\reader\render_two_page.js`
- `C:\Users\Suprabha\Desktop\Tankoban-Max\src\domains\reader\render_portrait.js`

**Secondary — Groundwork (PyQt6/QPainter):**
- `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\app_qt\ui\readers\comic_reader.py`
  Focus on: `paintEvent`, `drawImage`, `drawPixmap`, any `SmoothTransformation` equivalent, gutter shadows, spread rendering

**Current implementation:**
- `src\ui\readers\ComicReader.h`
- `src\ui\readers\ComicReader.cpp`
- `src\ui\readers\ScrollStripCanvas.h`
- `src\ui\readers\ScrollStripCanvas.cpp`

Do not write a single word of your position until you have read all of the above.

---

## Step 2: Your assignment

You are the rendering expert. You built D3D11 GPU rendering from scratch for the video player. Your job is to look at how Max and Groundwork handle image rendering and compare it to what Agent 1 has built in C++. Then write fixes and solutions — in real C++, not pseudo-code.

**Part A — Rough edges already implemented (find the gaps and fix them)**

For each rendering-related feature already in ComicReader/ScrollStripCanvas, compare the behavior in Max and Groundwork to what we have. Where there is a quality or performance gap, document it and write the fix.

Rendering areas to check:
- Image scaling mode — what does Max use on its canvas? What does Groundwork use for QPainter? What are we using and where? Is `Qt::SmoothTransformation` applied correctly and consistently across all three render paths (single page, double page, scroll strip)?
- Scroll strip paint performance — is scaling happening inside `paintEvent`? It should not be. Where exactly should it happen instead?
- Double-page gutter shadow — does Max draw one? Does Groundwork? Do we? If not, what is the exact QPainter approach?
- Double-page layout quality — how does Max compose two pages side by side? Does it center them, align baselines, or fill the canvas differently from what we do?
- Zoom and pan quality — when zoomed in, does Max apply any additional filtering? Do we degrade at high zoom levels?

For each gap, use this format:

```
## Rough Edge: [name]
What Max does: ...
What Groundwork does: ...
What we have: ...
The gap: ...
Fix:
[C++ code — specific, compilable, with file and approximate line reference]
```

**Part B — Unimplemented rendering features (what Max and Groundwork have that we don't)**

Look at Max and Groundwork for rendering behaviors that are NOT yet in our reader. For each one:
- Describe what it is and how it behaves
- Decide: worth implementing? (exclude: image filters, loupe — blacklisted by Hemanth)
- If yes: write the C++ approach

Things to look for (not exhaustive — read the code and find what we're missing):
- Page transition animations (fade, slide) — does Max do these? Should we?
- Loading placeholder while a page decodes — what does Max show while an image is loading?
- Spread page rendering differences — does Max treat a detected spread differently from two paired pages?
- Spread detection override UI — when Max auto-detects a spread, does it show a visual indicator? Can the user override it manually from the reader? If yes, how is it surfaced and what should Agent 1 build?
- Any rendering behavior in `render_two_page.js` that has no equivalent in ComicReader.cpp

Use this format:

```
## Unimplemented Feature: [name]
What Max does: ...
What Groundwork does: ...
Implement? Yes / No / Modified
C++ approach:
[C++ code or detailed spec]
```

---

## Step 3: When you are done

Post ONE LINE in `agents/chat.md`:

`Agent 3 congress position complete — [N] rough-edge fixes, [M] unimplemented features. See congress_positions/agent3_position.md.`

Then stop. Do not build anything. Do not touch Agent 1's files. Do not post anywhere else.

---

## Your Specific Checklist (from master_checklist.md)

These are your assigned items. Address every one. Use the master checklist for Max/Groundwork file references.

**Rough Edges:**
- R1: Image scaling quality — SmoothTransformation not consistently used. Max: createImageBitmap (GPU). Groundwork: Qt.SmoothTransformation explicitly in paintEvent. Find every place we scale and fix it.
- R2: Double-page gutter shadow — does ours draw one? Groundwork: QLinearGradient lines 106–126. Max: twoPageGutterShadow in render_two_page.js.
- R3: HiDPI fractional scaling — Max sets canvas transform from backing-store/CSS ratio. Does our QPainter approach handle fractional DPI correctly?
- R4: Double-page scroll jank — root cause analysis. Is it step size, synchronous render work in wheel handler, or missing smooth pump? Give the specific Qt fix.

**Missing Features:**
- M1: Fit Width mode for double page — pages scale to fill viewport width, vertical pan enabled when content overflows. Max: getTwoPageImageFit(). Groundwork: DoublePageCanvas zoom.
- M2: MangaPlus zoom mode — extra 100–260% multiplier on top of base fit. Drag pan on middle zone. Arrow key pan. Max: render_two_page.js zoomPct/zoomFactor. Groundwork: set_zoom() + set_pan().
- M3: Page dimensions cache — parse header bytes to get dimensions before full decode. Needed for Two-Page Scroll row layout without stalling. Groundwork: parse_image_dimensions() lines 213–346. Give the C++ equivalent (PNG/JPEG/WebP header parsing).
- M4: Scroll position fractional preservation on resize — when window is resized, current position should be preserved as a fraction of page height and re-projected. Max: resizeCanvasToDisplaySize() lines 22–61.
- M5: Spread detection from dims cache — in Two-Page Scroll, detect spreads from cached dimensions without requiring full decode. Max: isStitchedSpread() lines 5–43.

---

*(Write your position below this line)*

---

# Agent 3 (Video Player) — Congress Position
# First Congress: Comic Reader Acceleration — Rendering Lead

Read: render_core.js, render_two_page.js, render_portrait.js (Tankoban-Max)
Read: comic_reader.py (Groundwork) — PageWidget.paintEvent, TwoPageScrollRow.paintEvent, SmoothScrollArea._drain_wheel, parse_image_dimensions()
Read: ComicReader.cpp, ComicReader.h, ScrollStripCanvas.cpp, ScrollStripCanvas.h, SmoothScrollArea.cpp

---

## Rough Edges

---

## Rough Edge: R1 — Image scaling mode consistency

**What Max does:** Calls `applyScaleQualityToCtx(ctx)` — which sets `imageSmoothingEnabled=true, imageSmoothingQuality='high'` — before EVERY `drawImage` call. Comment in code explicitly says: "re-apply right before each drawImage" because canvas context state resets after resizes. Applied in portrait strip, single-page, two-page flip, and two-page scroll render paths.

**What Groundwork does:** In `PageWidget.paintEvent`, pre-scales the image to exact render size using `Qt.SmoothTransformation`, caches as `_scaled_pixmap`, and sets `painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)` before drawing. `TwoPageScrollRow.paintEvent` also sets `SmoothPixmapTransform` before drawing.

**What we have:**
- Single-page: `m_currentPixmap.scaledToHeight(availH, Qt::SmoothTransformation)` — correct, pre-scaled
- Double-page canvas: `p.setRenderHint(QPainter::SmoothPixmapTransform, true)` set before drawing both pages — correct
- ScrollStripCanvas::paintEvent: `p.drawPixmap(drawX, drawY, *it)` — **NO `setRenderHint` call**. Pixmaps in `m_scaledCache` are pre-scaled with `Qt::SmoothTransformation` (critical scaling already done). But at fractional DPI (125%, 150%), Qt may apply sub-pixel repositioning during blit, and `SmoothPixmapTransform` controls that path.

**The gap:** `ScrollStripCanvas::paintEvent` is missing `p.setRenderHint(QPainter::SmoothPixmapTransform, true)`.

**Fix:**
```cpp
// ScrollStripCanvas.cpp — paintEvent(), ~line 142, after `QPainter p(this);`
QPainter p(this);
p.setRenderHint(QPainter::SmoothPixmapTransform, true);  // ADD THIS
```

---

## Rough Edge: R2 — Double-page gutter shadow

**What Max does:** Draws a gutter shadow between paired pages only when `gutter > 0` (`TWO_PAGE_GUTTER_PX = 0` in Max, so shadow is effectively disabled in flip mode). Gradient: edge alpha = 0.10×strength, mid alpha = 0.28×strength.

**What Groundwork does:** Always draws gutter shadow at the center seam (0-px gutter, 4px bleed into each page, 8px total). Default strength = 0.35. User-adjustable via `GUTTER_SHADOW_PRESETS` = [Off 0.0, Subtle 0.22, Medium 0.35, Strong 0.55].

**What we have:** `drawGutterShadow(p, halfW, 0, contentH, 0.35)` — present, correctly positioned. Gradient math matches Groundwork (4px bleed each side, alphas 0.10/0.28 × 0.35). Mathematically correct.

**The gap:** Strength hardcoded at 0.35. No user control. Not a rendering quality issue — a UX completeness gap.

**Fix (deferred to Agent 1):** Add `m_gutterShadow` member (default 0.35). Add preset submenu in right-click context menu. Pass to `drawGutterShadow()`.

---

## Rough Edge: R3 — HiDPI fractional scaling

**What Max does:** `drawTwoPageFrame()` sets canvas backing store to `Math.round(cssSize × dpr)`, then `ctx.setTransform(actualRatio_x, 0, 0, actualRatio_y, 0, 0)` where ratio = actualBackingPx / cssPx. Handles fractional DPI (Windows 125%, Chromium zoom) by separating CSS space from device pixels. Comment: "On fractional scaling, r.width/r.height can be non-integer. If we Math.round CSS size and then assume transform=dpr, we can introduce a tiny resample step → softness."

**What Groundwork does:** Qt6 QPainter handles DPI internally. Pre-scaled pixmaps at logical resolution rendered without explicit DPR handling. Qt's compositor applies HiDPI scaling transparently for QLabel-displayed pixmaps.

**What we have:** `displayCurrentPage()` creates `QPixmap canvas(canvasW, contentH)` in logical pixels. At DPR=1.25 (Windows 125%), canvas is 1920-logical-px pixmap → Qt scales it up 1.25× for display → rendered at 80% of device resolution → soft by 20%.

**The gap:** All canvas-based rendering (double-page, single-page when using canvas) creates pixmaps at logical resolution. At fractional DPI this is noticeably soft.

**Fix:**
```cpp
// ComicReader::displayCurrentPage() — wrap ALL QPixmap canvas creation:
double dpr = devicePixelRatioF();
int physW = static_cast<int>(canvasW * dpr + 0.5);
int physH = static_cast<int>(contentH * dpr + 0.5);
QPixmap canvas(physW, physH);
canvas.setDevicePixelRatio(dpr);
canvas.fill(Qt::black);
QPainter p(&canvas);
p.setRenderHint(QPainter::SmoothPixmapTransform, true);
// All draw coordinates stay in logical pixels — QPainter applies DPR automatically
// when the destination pixmap has devicePixelRatio set.
m_imageLabel->setPixmap(canvas);
m_imageLabel->resize(QSize(canvasW, contentH));  // logical size for layout
```

```cpp
// ScrollStripCanvas::onPageDecoded() — scale pixmaps at physical resolution:
double dpr = devicePixelRatioF();
int physW = static_cast<int>(pw * dpr + 0.5);
QPixmap scaled = fullRes.scaledToWidth(physW, Qt::SmoothTransformation);
scaled.setDevicePixelRatio(dpr);
m_scaledCache[pageIndex] = scaled;
// IMPORTANT: targetPageWidth() and all geometry (yOffset, height, drawX)
// must stay in logical px. Only the pixmap backing store is physical.
```

---

## Rough Edge: R4 — Scroll strip jank (root cause analysis)

"Wheel events feel like spikes in the road." This is the scroll strip mode (continuous vertical strip).

**What Max does (portrait/two-page-scroll):**
1. Prefers `pixelDelta` (trackpad precise input) over `angleDelta`
2. Per-event input cap: `max(1200.0, vh × 3)` — one aggressive spin cannot inject > 3 viewport heights
3. Backlog cap: `max(2400.0, vh × 8)` — total pending never exceeds 8 viewport heights
4. Drain: 38% per 16ms tick, capped at `max(70.0, vh × 0.22)` per tick
5. Float accumulator (`appState.y`) — integer rounding only at final display

**What Groundwork does (SmoothScrollArea._drain_wheel):** Identical pattern — all five points above.

**What we have (SmoothScrollArea.cpp):**
```cpp
// wheelEvent: no pixelDelta check, no per-event cap, no backlog cap
double px = -event->angleDelta().y() * 0.8;
m_pendingPx += px;

// drainWheel: no max_step cap, integer-only accumulation
double step = m_pendingPx * DRAIN_FRACTION;
vbar->setValue(vbar->value() + static_cast<int>(step));
```

**Four bugs causing the jank:**

**Bug 1 — No max_step cap:** `m_pendingPx = 2000` → drain takes `760px` in one 16ms tick (nearly full HD height in one frame → visible spike). Fix: cap step at `max(70.0, vh × 0.22)`.

**Bug 2 — No backlog cap:** Aggressive spinning accumulates unlimited pending px. Long sticky tail. Fix: cap `m_pendingPx` at `max(2400.0, vh × 8)`.

**Bug 3 — Integer-only accumulation:** `int(step)` discards sub-pixel remainder every frame. At DRAIN_FRACTION=0.38, rounds 2.4px to 2px → 16% lost per frame → scroll feels mechanical and snappy at the end. Fix: float accumulator `m_smoothY`, apply `int(round(m_smoothY))` to scrollbar.

**Bug 4 — No pixelDelta support:** Trackpads send pixel deltas. `angleDelta()` only → trackpad input incorrectly converted. Fix: prefer `pixelDelta()` when nonzero.

**Fix — SmoothScrollArea rewrite (src/ui/readers/SmoothScrollArea.h/.cpp):**

```cpp
// SmoothScrollArea.h — new members:
double m_smoothY  = 0.0;
bool   m_draining = false;

// SmoothScrollArea.cpp — new wheelEvent:
void SmoothScrollArea::wheelEvent(QWheelEvent* event)
{
    double deltaX = event->pixelDelta().x();
    double deltaY = event->pixelDelta().y();
    if (deltaX == 0.0 && deltaY == 0.0) {
        deltaX = event->angleDelta().x() * (100.0 / 120.0);
        deltaY = event->angleDelta().y() * (100.0 / 120.0);
    }
    double dominant = (std::abs(deltaY) >= std::abs(deltaX)) ? deltaY : deltaX;
    if (dominant == 0.0) { QScrollArea::wheelEvent(event); return; }

    int vh = std::max(1, viewport()->height());
    double maxInput = std::max(1200.0, static_cast<double>(vh) * 3.0);
    double step = std::max(-maxInput, std::min(maxInput, -dominant));
    double cap = std::max(2400.0, static_cast<double>(vh) * 8.0);
    m_pendingPx = std::max(-cap, std::min(cap, m_pendingPx + step));

    if (!m_drainTimer.isActive()) m_drainTimer.start();
    event->accept();
}

// SmoothScrollArea.cpp — new drainWheel:
void SmoothScrollArea::drainWheel()
{
    if (std::abs(m_pendingPx) < SNAP_THRESHOLD) {
        m_pendingPx = 0.0;
        m_drainTimer.stop();
        return;
    }
    auto* bar = verticalScrollBar();
    double vh = static_cast<double>(std::max(1, viewport()->height()));
    double maxStep = std::max(70.0, vh * 0.22);
    double take = m_pendingPx * DRAIN_FRACTION;
    take = std::max(-maxStep, std::min(maxStep, take));
    if (std::abs(take) < 2.0) take = m_pendingPx;
    m_pendingPx -= take;
    m_smoothY += take;
    m_smoothY = std::max(static_cast<double>(bar->minimum()),
                         std::min(static_cast<double>(bar->maximum()), m_smoothY));
    int newVal = static_cast<int>(std::round(m_smoothY));
    if (newVal != bar->value()) {
        m_draining = true;
        bar->setValue(newVal);
        m_draining = false;
    }
}

// SmoothScrollArea constructor — add sync for external scroll moves:
// connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
//     if (!m_draining) m_smoothY = static_cast<double>(v);
// });
```

Also add `void syncExternalScroll(int val) { m_smoothY = val; }` public method for `reflowScrollStrip()` to call after page jumps.

---

## Unimplemented Features

---

## Unimplemented Feature: M1 — Fit Width mode for double page

**What Max does:** `getTwoPageImageFit(mode)` returns 'width' — pages scale so combined width fills viewport, vertical pan when overflowing.

**What Groundwork does:** Double-page scales each page to half-viewport-width; tall pages scroll vertically via QScrollArea.

**What we have:** Double-page scales each page to `halfW = availW/2`. Canvas height = `max(rh, lh)`. If overflow, QScrollArea scrollbar appears. Vertical pan works.

**Implement? No — already implemented.** Our double-page IS fit-width. No gap vs Groundwork or Max's base behavior.

---

## Unimplemented Feature: M2 — Horizontal pan when zoomed

**What Max does:** `twoPageMangaPlus` — extra zoom 100–260%. When zoomed and content overflows width, `allowX = true` → pan state `twoPageFlickPanX`. Drag to pan. Starts at reading-start edge.

**What Groundwork does:** `set_zoom()` + `set_pan()` on DoublePageCanvas.

**What we have:** Zoom state exists (`m_zoomPct`, Ctrl+wheel). `totalW = availW × zoomPct/100`. But `setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff)` → horizontal overflow clips silently. No pan offset state. Content cut off at zoom > 100%.

**Implement? Yes — Modified.**

```cpp
// ComicReader.h — add:
int m_panX = 0;  // logical px, clamped to [0, maxPanX]

// ComicReader.cpp — displayCurrentPage(), double-page pair case:
// After computing dxL/dxR:
int maxPanX = std::max(0, static_cast<int>(totalW) - availW);
m_panX = qBound(0, m_panX, maxPanX);
int dxL_final = dxL - m_panX;
int dxR_final = dxR - m_panX;
// Draw pages at dxL_final/dxR_final, canvas = QPixmap(availW, contentH)
// (content outside canvas clips naturally — no horizontal scrollbar needed)

// showPage(): reset m_panX = 0; on every page turn
// mouseMoveEvent(): when m_isDoublePage && dragging center zone, update m_panX, call displayCurrentPage()
// Arrow Left/Right when zoomed: adjust m_panX by 40px, displayCurrentPage()
```

---

## Unimplemented Feature: M3 — Page dimensions cache (header parsing)

**What Max does:** `getTwoPageScrollDimsAtIndex(i)` — reads first bytes of archive entry, parses image header for dimensions. Async, cached in `twoPageScrollDimsCache`. Used for scroll row layout and spread detection without full decode.

**What Groundwork does:** `parse_image_dimensions(data)` — PNG/JPEG/WebP/GIF/BMP header parsers on first 96KB. Falls back to full decode if headers unrecognized.

**What we have:** `PageMeta.width/height` only after full decode. `ScrollStripCanvas` uses `DEFAULT_ASPECT = 1.4` for undecoded pages → layout jumps when spreads are discovered post-decode.

**Implement? Yes.** New file `src/core/ImageDimensions.h/.cpp`:

```cpp
// Returns {0,0} if format unrecognized
QSize parseImageDimensions(const QByteArray& data) {
    const int n = data.size();

    // PNG: signature at [0..7], width at [16..19] BE, height at [20..23] BE
    if (n >= 24 && (uchar)data[0]==0x89 && data[1]=='P' && data[2]=='N' && data[3]=='G') {
        int w = ((uchar)data[16]<<24)|((uchar)data[17]<<16)|((uchar)data[18]<<8)|(uchar)data[19];
        int h = ((uchar)data[20]<<24)|((uchar)data[21]<<16)|((uchar)data[22]<<8)|(uchar)data[23];
        if (w>0 && h>0) return {w,h};
    }

    // JPEG: scan SOF markers (0xC0, 0xC1, 0xC2, ... 0xCF except 0xC4/0xC8)
    if (n >= 4 && (uchar)data[0]==0xFF && (uchar)data[1]==0xD8) {
        static const QSet<int> sof = {0xC0,0xC1,0xC2,0xC3,0xC5,0xC6,0xC7,0xC9,0xCA,0xCB,0xCD,0xCE,0xCF};
        int i = 2;
        while (i+9 < n) {
            if ((uchar)data[i] != 0xFF) { i++; continue; }
            int mk = (uchar)data[i+1]; i += 2;
            if (mk==0xD8||mk==0xD9) continue;
            if (mk>=0xD0&&mk<=0xD7) continue;
            if (i+2 > n) break;
            int sl = ((uchar)data[i]<<8)|(uchar)data[i+1];
            if (sl<2 || i+sl>n) break;
            if (sof.contains(mk) && sl>=7) {
                int h = ((uchar)data[i+3]<<8)|(uchar)data[i+4];
                int w = ((uchar)data[i+5]<<8)|(uchar)data[i+6];
                if (w>0&&h>0) return {w,h};
            }
            i += sl;
        }
    }

    // WebP: RIFF....WEBP
    if (n>=30 && data[0]=='R'&&data[1]=='I'&&data[2]=='F'&&data[3]=='F'
        && data[8]=='W'&&data[9]=='E'&&data[10]=='B'&&data[11]=='P') {
        auto chunk = data.mid(12,4);
        if (chunk=="VP8X" && n>=30) {
            int w = 1+((uchar)data[24]|((uchar)data[25]<<8)|((uchar)data[26]<<16));
            int h = 1+((uchar)data[27]|((uchar)data[28]<<8)|((uchar)data[29]<<16));
            if (w>0&&h>0) return {w,h};
        }
        if (chunk=="VP8 " && n>=30) {
            int off=20;
            if ((uchar)data[off+3]==0x9D&&(uchar)data[off+4]==0x01&&(uchar)data[off+5]==0x2A) {
                int w=((uchar)data[off+6]|((uchar)data[off+7]<<8))&0x3FFF;
                int h=((uchar)data[off+8]|((uchar)data[off+9]<<8))&0x3FFF;
                if (w>0&&h>0) return {w,h};
            }
        }
        if (chunk=="VP8L" && n>=25) {
            int off=20;
            if ((uchar)data[off]==0x2F) {
                quint32 bits=(uchar)data[off+1]|((uchar)data[off+2]<<8)|((uchar)data[off+3]<<16)|((uchar)data[off+4]<<24);
                int w=1+(bits&0x3FFF); int h=1+((bits>>14)&0x3FFF);
                if (w>0&&h>0) return {w,h};
            }
        }
    }

    // GIF: GIF87a/GIF89a, width/height at [6..9] LE
    if (n>=10 && (data.startsWith("GIF87a")||data.startsWith("GIF89a"))) {
        int w=(uchar)data[6]|((uchar)data[7]<<8);
        int h=(uchar)data[8]|((uchar)data[9]<<8);
        if (w>0&&h>0) return {w,h};
    }

    return {};
}
```

`ArchiveReader` needs `readHead(name, maxBytes)` method (already exists in Groundwork: `read_head()`). On `openBook()`, fire a background thread that reads 96KB headers for all pages and populates `PageMeta.width/height/isSpread` before full decode completes. Emit signal to trigger scroll strip reflow when dims arrive.

---

## Unimplemented Feature: M4 — Scroll position fractional preservation on resize

**What Max does:** `resizeCanvasToDisplaySize()` — saves `sySrc = appState.y / scaleOld` (position in source image space), then after resize: `appState.y = sySrc × scaleNew`. Float throughout.

**What Groundwork does:** Partial — `_smooth_y` float is preserved but not re-projected through scale change.

**What we have:** `resizeEvent → reflowScrollStrip()` — scrollbar integer value preserved, but total canvas height changes → absolute position drifts.

**Implement? Yes.** Fix in `ComicReader::reflowScrollStrip()`:

```cpp
// BEFORE reflow:
double oldTotal = m_stripCanvas->totalHeight();
double oldScroll = m_scrollArea->verticalScrollBar()
                   ? static_cast<double>(m_scrollArea->verticalScrollBar()->value()) : 0.0;
double fraction = (oldTotal > 1.0) ? (oldScroll / oldTotal) : 0.0;

// ... existing reflow code ...

// AFTER reflow:
double newTotal = m_stripCanvas->totalHeight();
if (newTotal > 0 && m_scrollArea->verticalScrollBar()) {
    int newVal = static_cast<int>(fraction * newTotal + 0.5);
    m_scrollArea->verticalScrollBar()->setValue(newVal);
    m_scrollArea->syncExternalScroll(newVal);  // sync float accumulator
}
```

Requires `SmoothScrollArea::syncExternalScroll(int val)` from R4 fix.

---

## Unimplemented Feature: M5 — Spread detection from dims cache

**What Max does:** `isStitchedSpread(index)` checks `twoPageScrollDimsCache` first — spread known from header bytes, no full decode required.

**What Groundwork does:** `PageMeta.spread` set from `parse_image_dimensions()` at index time.

**What we have:** `PageMeta.isSpread` only set in `onPageDecoded()`. Scroll strip treats all undecoded pages as portrait (DEFAULT_ASPECT 1.4). Layout jumps when spreads are discovered after decode → scroll position appears to shift under the user.

**Implement? Yes — requires M3 first.** After M3 populates `PageMeta.width/height`, add:

```cpp
// In the dims background thread callback:
m_pageMeta[i].isSpread = (m_pageMeta[i].height > 0 &&
    static_cast<double>(m_pageMeta[i].width) / m_pageMeta[i].height >= 1.08);

// Then feed dims to ScrollStripCanvas without waiting for full decode:
// m_stripCanvas->onDimsKnown(i, m_pageMeta[i].origW, m_pageMeta[i].origH);
// ScrollStripCanvas::onDimsKnown() sets slot.origW/H/isSpread, marks decoded=true
// for layout purposes only, triggers rebuildYOffsets → no layout jump later
```

---

## Summary

**4 rough-edge fixes, 4 unimplemented features (M1 already implemented).**

Priority order for Agent 1:
1. **R4** — SmoothScrollArea jank fix (biggest UX impact, self-contained rewrite of SmoothScrollArea.h/.cpp)
2. **R3** — HiDPI-aware pixmap creation in displayCurrentPage() and ScrollStripCanvas::onPageDecoded()
3. **R1** — Add `p.setRenderHint(QPainter::SmoothPixmapTransform, true)` to ScrollStripCanvas::paintEvent (one-liner)
4. **M3 → M5** — Header-parsing dims cache → spread detection (eliminates scroll layout jumps)
5. **M2** — Horizontal pan offset when zoomed in double-page mode
6. **R2** — Gutter shadow strength user control (low priority, cosmetic)
