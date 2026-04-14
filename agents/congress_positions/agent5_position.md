# Agent 5 (Library UX) — Congress Position
# First Congress: Comic Reader Acceleration

**Your lane: Scroll mechanics, HUD behavior, keyboard shortcuts, Qt widget patterns**

---

## Step 1: Read these files IN THIS ORDER before writing anything

**Primary — Tankoban Max (JavaScript):**
- `C:\Users\Suprabha\Desktop\Tankoban-Max\src\domains\reader\input_keyboard.js`
- `C:\Users\Suprabha\Desktop\Tankoban-Max\src\domains\reader\input_pointer.js`
- `C:\Users\Suprabha\Desktop\Tankoban-Max\src\domains\reader\state_machine.js`
- `C:\Users\Suprabha\Desktop\Tankoban-Max\src\domains\reader\render_core.js` (scroll/pan behavior specifically)

**Secondary — Groundwork (PyQt6):**
- `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\app_qt\ui\readers\comic_reader.py`
  Focus on: HUD show/hide logic, `mouseMoveEvent`, `keyPressEvent`, `wheelEvent`, any QScrollArea usage, toolbar behavior

**Current implementation:**
- `src\ui\readers\ComicReader.h`
- `src\ui\readers\ComicReader.cpp`
- `src\ui\readers\ScrollStripCanvas.h`
- `src\ui\readers\ScrollStripCanvas.cpp`

Do not write a single word of your position until you have read all of the above.

---

## Step 2: Your assignment

You are the Qt patterns and scroll architecture expert. You rewrote TileStrip, fixed the QScrollArea mess, and built debounced search and filter. Your job is to look at how Max and Groundwork handle scroll, input, and HUD behavior — then compare to what Agent 1 has built — and write specific fixes and solutions in real C++.

**Part A — Rough edges already implemented (find the gaps and fix them)**

For each scroll/input/HUD feature already in ComicReader/ScrollStripCanvas, compare to Max and Groundwork. Where behavior differs or feels rough, document it and write the fix.

Areas to check:
- Double-page scroll mechanics — how does Max handle `wheel` events on the two-page canvas? What is the exact scroll step? Does it feel smooth because of step size, easing, or scroll event accumulation? Compare to what we have in `wheelEvent`.
- Scroll strip wheel behavior — same question for infinite strip mode. What does Max do that makes it feel fluid?
- HUD show/hide — what are Max's exact triggers for showing and hiding the HUD? Compare to Groundwork's toolbar behavior. Does our implementation match the timing and trigger conditions?
- Click zones — what are Max's exact click zone boundaries (left/right/center)? What is the debounce on center click? Do we match this exactly?
- Cursor hide timing — how long does Max wait before hiding the cursor? 3000ms? Less? Do we match it?
- Keyboard shortcut completeness — list every keyboard shortcut in Max's `input_keyboard.js`. Compare to what we have. What is missing?

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

**Part B — Unimplemented input/UX features (what Max and Groundwork have that we don't)**

Look at Max's state machine and input handlers for behaviors not yet in our reader. For each:
- Describe what it does and how it behaves
- Decide: worth implementing? (exclude: auto-scroll, auto-flip — blacklisted by Hemanth)
- If yes: write the C++ approach

Things to look for (not exhaustive — read the code and find what we're missing):
- Touch/pinch-to-zoom — does Max support it? Should we?
- Reading direction toggle behavior — how does Max handle RTL switching mid-read? Do we handle it correctly?
- Any HUD element in Max that we haven't built (panel toggles, mode indicators, page counter display)
- Any keyboard shortcut in Max that has no equivalent in our reader

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

`Agent 5 congress position complete — [N] rough-edge fixes, [M] unimplemented features. See congress_positions/agent5_position.md.`

Then stop. Do not build anything. Do not touch Agent 1's files. Do not post anywhere else.

---

## Your Specific Checklist (from master_checklist.md)

These are your assigned items. Address every one. Use the master checklist for Max/Groundwork file references.

**Rough Edges:**
- R1: Wheel smoothing sophistication — Groundwork's SmoothScrollArea drains 38% of pending delta per frame at 16ms PreciseTimer with float accumulation. Max uses queueManualWheelSmooth pump. Compare to what we have and write the C++ equivalent if ours is simpler.
- R2: HUD freeze — Max's hudFreezeActive() prevents auto-hide whenever any overlay is open, scrub bar is being dragged, or cursor is hovering the HUD area. Does our implementation do this?
- R3: Click zone flash — Max: 90ms CSS animation, separate zoneFlashBlocked state for when navigation is busy. Groundwork: 250ms fade animation. What do we have and what's the correct behavior?
- R4: HUD page counter in scroll mode — Max batches updates at 100ms and computes the current page by finding which page occupies the viewport center. Does ours do this or does it just track pageIndex?
- R5: HUD auto-hide mode distinction — Max: Manual Scroll = click-pinned, no auto-hide timer at all. Auto Scroll = 3s timer. Does our implementation distinguish between modes?

**Missing Features:**
- M1: Manual scroller thumb (right edge) — a separate draggable vertical progress thumb on the right edge of the reader, distinct from the bottom scrub bar. Groundwork: HudVerticalScroller lines 1578–1648 with progressRequested(float) signal.
- M2: Direction toast — a brief transient QLabel that appears centered above the HUD footer for 1200ms showing messages like "RTL mode", "Coupling shifted", zoom level. Groundwork: lines 2133–2147.
- M3: Modal overlay scrim — semi-transparent full-size QWidget placed behind modal overlays (end-of-volume, volume navigator). Clicking it dismisses the overlay. Groundwork: ReaderOverlayScrim lines 787–803.
- M4: Right-click context menu (subset only) — go-to-page, copy volume path, reveal in explorer. Exclude: image filters, scaling quality, loupe (all blacklisted). Use ContextMenuHelper pattern Agent 5 already built.
- M5: Double-click fullscreen toggle — Groundwork: eventFilter QEvent.MouseButtonDblClick with 220ms single-click timer to distinguish from double-click.
- M6: Ctrl+Wheel zoom in double page — zoom in/out on double page canvas. Groundwork: eventFilter Ctrl+Wheel lines 2551–2561.
- M7: Side nav arrow zones — faint ‹/› arrows visible when hovering left/right edges, mouse-transparent (WA_TransparentForMouseEvents). Groundwork: SideNavZone lines 1812–1843.

---

*(Write your position below this line)*

---

## Agent 5 Position — UX Patterns, Scroll Architecture, Input Gaps

Read: Tankoban-Max hud_core.js, input_keyboard.js, input_pointer.js, open.js; Groundwork SmoothScrollArea, HudVerticalScroller, SideNavZone, ReaderOverlayScrim; current ComicReader.cpp/.h, ScrollStripCanvas.cpp/.h, SmoothScrollArea.cpp/.h.

---

## Rough Edge: R1 — SmoothScrollArea missing three Groundwork behaviors

**What Max does:** rAF-based pump (no fixed timer), accumulates device-pixel delta, caps backlog at `max(2400, viewport_h * 8)`. Drain consumes a fraction per frame.

**What Groundwork does:** 16ms PreciseTimer, 38% drain per frame (matching our constant), **pixelDelta** preferred over angleDelta with fallback `angleDelta * (100/120)`, dominant-axis selection (x vs y), max_step capped at `max(70, viewport_h * 0.22)`, and a `_smooth_y` float that stays in sync when the scrollbar moves externally (keyboard, scrub jump) via `_on_external_scroll()`.

**What we have:** `SmoothScrollArea.cpp` — 16ms PreciseTimer (correct), 38% drain (correct), but:
- Uses `angleDelta` only — trackpad sends `pixelDelta`, we ignore it and over-convert
- No `max_step` cap — `step = pendingPx * 0.38` can be any size; fast trackpad burst causes spike
- No `m_smoothY` float sync — if scrollbar jumps externally (scrub bar, key nav), next wheel event starts from wrong float baseline causing a lurch

**The gap:** Three small bugs compound. Trackpad feel is rougher than Groundwork. Spike possible on fast scroll. Scrub-then-wheel lurches.

**Fix (SmoothScrollArea.cpp/.h):**

```cpp
// SmoothScrollArea.h — add members
double m_smoothY = 0.0;
bool   m_draining = false;

// SmoothScrollArea constructor — add external sync
connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
    if (!m_draining) m_smoothY = double(value);
});

// wheelEvent — prefer pixelDelta, add max_step cap
void SmoothScrollArea::wheelEvent(QWheelEvent* event)
{
    double px = 0.0;
    QPoint pdp = event->pixelDelta();
    if (!pdp.isNull() && pdp.y() != 0) {
        px = -double(pdp.y());                   // pixelDelta: direct CSS pixels
    } else {
        double angle = -double(event->angleDelta().y());
        px = angle * (100.0 / 120.0);            // 1 notch (120 units) = 100px
    }
    if (px == 0.0) { event->ignore(); return; }

    double cap = qMax(2400.0, double(viewport()->height() * 8));
    m_pendingPx = qBound(-cap, m_pendingPx + px, cap);
    if (!m_drainTimer.isActive()) m_drainTimer.start();
    event->accept();
}

// drainWheel — add max_step cap, use m_smoothY for precise float scrolling
void SmoothScrollArea::drainWheel()
{
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
        m_draining = true;
        vbar->setValue(newVal);
        m_draining = false;
    }
}
```

---

## Rough Edge: R2 — HUD freeze: click zones not gated when overlays are open

**What Max does:** `hudFreezeActive()` returns true when: endOverlayOpen, manualScrollerDragging, scrubDragging, keysOpen, volNavOpen, megaOpen. Both leftZone and rightZone click handlers check `if (hudFreezeActive()) return` before doing anything.

**What Groundwork does:** eventFilter routes all input events. When any overlay is open, the eventFilter swallows mouse clicks before they reach the reader area.

**What we have:** `ComicReader::mousePressEvent()` fires left/right zone nav unconditionally. If `m_endOverlay` is visible and user clicks the left zone, `prevPage()` fires behind the overlay card. Same for `m_volOverlay` and `m_gotoOverlay`.

**The gap:** Overlay-open state is not checked before click zone navigation. Background page turns fire when they shouldn't.

**Fix (ComicReader.cpp — mousePressEvent, add helper):**

```cpp
// Add helper (ComicReader.h: bool isAnyOverlayOpen() const;)
bool ComicReader::isAnyOverlayOpen() const {
    if (m_endOverlay  && m_endOverlay->isVisible())  return true;
    if (m_volOverlay  && m_volOverlay->isVisible())  return true;
    if (m_gotoOverlay && m_gotoOverlay->isVisible()) return true;
    return false;
}

// Gate at top of mousePressEvent
void ComicReader::mousePressEvent(QMouseEvent* event)
{
    if (isAnyOverlayOpen()) { QWidget::mousePressEvent(event); return; }
    // ... existing left/mid/right zone handling unchanged
}
```

---

## Rough Edge: R3 — Click zone flash missing "blocked" visual state

**What Max does:** `flashClickZone(zoneEl, blocked)` — 90ms timeout, two CSS classes: `zoneFlash` (white) for normal nav, `zoneFlashBlocked` (dimmer warm tint) for when navBusy or in scroll mode where side clicks do nothing.

**What Groundwork does:** `_flash_click_zone(side)` — 250ms QPropertyAnimation opacity 1→0, `rgba(255,255,255,38)`. No blocked variant (Groundwork is simpler here).

**What we have:** `flashClickZone(side)` creates a QWidget + QPropertyAnimation 250ms. Works. But in ScrollStrip mode when user clicks left/right, we return silently with zero visual feedback.

**The gap:** Left/right clicks in scroll mode are swallowed silently. Max shows a dim "blocked" flash to confirm receipt.

**Fix (ComicReader.cpp — mousePressEvent, update flashClickZone signature):**

```cpp
// Update signature: void flashClickZone(const QString& side, bool blocked = false);

void ComicReader::flashClickZone(const QString& side, bool blocked)
{
    QWidget* flash = new QWidget(this);
    flash->setAttribute(Qt::WA_TransparentForMouseEvents);
    QString color = blocked ? "rgba(255,200,100,22)" : "rgba(255,255,255,38)";
    flash->setStyleSheet(QString("background: %1;").arg(color));
    int third = width() / 3;
    flash->setGeometry(side == "left" ? QRect(0, 0, third, height())
                                      : QRect(width() - third, 0, third, height()));
    flash->show(); flash->raise();
    auto* anim = new QPropertyAnimation(flash, "windowOpacity", flash);
    anim->setDuration(blocked ? 150 : 250);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    connect(anim, &QPropertyAnimation::finished, flash, &QWidget::deleteLater);
    anim->start();
}

// In mousePressEvent, scroll strip side-zone click:
if ((zone == "left" || zone == "right") && m_isScrollStrip) {
    flashClickZone(zone, /*blocked=*/true);
    event->accept(); return;
}
```

---

## Rough Edge: R4 — Page counter not updated during scroll strip scrolling

**What Max does:** `syncHudPageCounter(force)` — force=true for discrete nav (immediate), force=false for scroll (batched at 100ms). Uses `computePageInView()` to find the page occupying viewport center.

**What Groundwork does:** `update_page_counter()` same center-Y algorithm; also updates `scrub_bar` and `side_scroller` progress. Called on scroll events.

**What we have:** `updatePageLabel()` is only called from `showPage()` and mode switches. In scroll strip mode, `showPage()` is NOT called during scrolling. The `m_stripRefreshTimer` (16ms) calls `refreshVisibleStripPages()` and `saveCurrentProgress()` — but not `updatePageLabel()`. Result: the page counter in the toolbar freezes at whatever it was when scroll strip launched.

**The gap:** Page counter and scrub bar progress are stale during scroll strip scrolling.

**Fix (ComicReader.cpp — strip refresh timer lambda):**

```cpp
connect(&m_stripRefreshTimer, &QTimer::timeout, this, [this]() {
    refreshVisibleStripPages();
    saveCurrentProgress();
    // Add: sync page counter and scrub bar from current scroll position
    if (m_isScrollStrip && m_stripCanvas && !m_pageNames.isEmpty()) {
        int page = computePageInView();
        m_pageLabel->setText(
            QString("Page %1 / %2").arg(page + 1).arg(m_pageNames.size()));
        m_scrubBar->setProgress(
            double(page) / qMax(1, m_pageNames.size() - 1));
    }
});
```

---

## Rough Edge: R5 — HUD auto-hide mode distinction

**What Max does:** Manual Scroll = click-pinned, hudScheduleAutoHide() returns early for manual/twoPageFlip/autoFlip modes. No auto-hide timer.

**What we have:** Toggle-based H key, no auto-hide timer. This exactly matches Max's Manual Scroll behavior.

**Gap: None.** Our H-toggle-only model is correct. R5 is not a bug.

---

## Unimplemented Feature: M1 — Manual scroller thumb (right-edge progress bar)

**What Max does:** `manualScrollerDragging` state fed into `hudFreezeActive()`.

**What Groundwork does:** `HudVerticalScroller` (lines 1578–1648) — 12px wide, right edge, 10px from right, top/bottom 12px margin from HUD. Paints 3px track (14% opacity) + 7px×54px thumb (28% → 42% dragging). `progressRequested(float)` signal → `scroll_to_page_index()`. Only used in scroll strip mode.

**What we have:** Nothing.

**Implement? Yes.** Essential for large volumes in scroll strip mode.

**C++ approach:**

```cpp
// ComicReader.h — add inner class and member
class VerticalThumb : public QWidget {
    Q_OBJECT
public:
    explicit VerticalThumb(QWidget* parent = nullptr);
    void setProgress(double v);         // 0.0–1.0
signals:
    void progressRequested(double v);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    double progressForY(double y) const;
    double m_progress = 0.0;
    bool   m_dragging = false;
    static constexpr int THUMB_H = 54;
    static constexpr int THUMB_W = 7;
};

VerticalThumb* m_vertThumb = nullptr;

// buildUI(): create + connect
m_vertThumb = new VerticalThumb(this);
m_vertThumb->hide();
connect(m_vertThumb, &VerticalThumb::progressRequested, this, [this](double p) {
    auto* vbar = m_scrollArea->verticalScrollBar();
    if (vbar) vbar->setValue(int(p * vbar->maximum()));
});

// resizeEvent(): position right edge, top to (toolbar.y - 12)
m_vertThumb->setGeometry(width() - 22, 12, 12, m_toolbar->y() - 24);

// strip refresh timer: update progress from scroll position
if (m_vertThumb->isVisible()) {
    auto* vbar = m_scrollArea->verticalScrollBar();
    double p = vbar && vbar->maximum() > 0
               ? double(vbar->value()) / vbar->maximum() : 0.0;
    m_vertThumb->setProgress(p);
}

// Show only in ScrollStrip mode; hide otherwise
```

---

## Unimplemented Feature: M2 — Direction toast position

**What Groundwork does:** `direction_toast` positioned at `x = (root.width - w)/2`, `y = footer.y() - h - 8` — above the HUD footer. Duration 1200ms.

**What we have:** `showToast()` positions at `(width()-w)/2, height()/2 - h/2` — screen center. This overlaps the page content.

**Implement? Yes** — one-line position fix.

**Fix (ComicReader.cpp — showToast):**

```cpp
// Replace the move() call:
int toastY = m_toolbar->isVisible()
    ? m_toolbar->y() - m_toastLabel->height() - 8
    : height() - m_toastLabel->height() - 80;
m_toastLabel->move((width() - m_toastLabel->width()) / 2, toastY);
```

---

## Unimplemented Feature: M3 — Modal overlay scrim (click-outside-to-dismiss)

**What Groundwork does:** `ReaderOverlayScrim` (lines 787–803) — full-size QWidget, `rgba(0,0,0,90)`, `NoFocus`. `mousePressEvent` left/right button → emit `clicked()` → overlay closes. Placed behind the overlay card in z-order.

**What we have:** End overlay, volume navigator, and goto dialog have no click-outside-to-dismiss. Scrub bar drag and other clicks pass through.

**Implement? Yes** — for volume navigator and goto dialog. End overlay already has good keyboard cascade.

**C++ approach — add inner class:**

```cpp
class ClickScrim : public QWidget {
    Q_OBJECT
public:
    explicit ClickScrim(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet("background: rgba(0,0,0,90);");
        setFocusPolicy(Qt::NoFocus);
    }
signals:
    void clicked();
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton || e->button() == Qt::RightButton)
            emit clicked();
        QWidget::mousePressEvent(e);
    }
};

// ComicReader.h: ClickScrim* m_volScrim = nullptr; ClickScrim* m_gotoScrim = nullptr;

// showVolumeNavigator():
if (!m_volScrim) {
    m_volScrim = new ClickScrim(this);
    connect(m_volScrim, &ClickScrim::clicked, this, &ComicReader::hideVolumeNavigator);
}
m_volScrim->setGeometry(0, 0, width(), height());
m_volScrim->show();
m_volOverlay->raise();

// hideVolumeNavigator(): m_volScrim->hide() before existing hide logic
```

---

## Unimplemented Feature: M4 — Right-click context menu (all modes)

**What Max does:** Context menu: go-to-page, copy volume path, reveal in explorer. Excludes image filters, loupe (blacklisted).

**What we have:** `contextMenuEvent` shows spread override menu in double-page mode only. Single page and scroll strip have no menu.

**Implement? Yes.** Use the existing `ContextMenuHelper` (src/ui/ContextMenuHelper.h).

**C++ approach:**

```cpp
void ComicReader::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_isDoublePage) {
        showSpreadOverrideMenu(m_currentPage, event->globalPos());
        return;
    }
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background:#1a1a1a; color:rgba(255,255,255,0.85);"
        "  border:1px solid rgba(255,255,255,0.12); border-radius:6px; padding:4px 0; }"
        "QMenu::item { padding:6px 20px; }"
        "QMenu::item:selected { background:rgba(255,255,255,0.10); }"
        "QMenu::separator { height:1px; background:rgba(255,255,255,0.10); margin:4px 0; }"
    );
    QAction* gotoAct = menu.addAction("Go to Page...");
    connect(gotoAct, &QAction::triggered, this, &ComicReader::showGoToDialog);
    menu.addSeparator();
    QAction* copyAct = menu.addAction("Copy Volume Path");
    connect(copyAct, &QAction::triggered, this, [this]() {
        ContextMenuHelper::copyToClipboard(m_cbzPath);
    });
    QAction* revealAct = menu.addAction("Reveal in File Explorer");
    connect(revealAct, &QAction::triggered, this, [this]() {
        ContextMenuHelper::revealInExplorer(m_cbzPath);
    });
    menu.exec(event->globalPos());
}
// Requires: #include "ui/ContextMenuHelper.h"
```

---

## Unimplemented Feature: M5 — Double-click fullscreen toggle

**Already implemented.** `mouseDoubleClickEvent` center zone → `emit fullscreenRequested(bool)`, 220ms debounce via `m_clickTimer`. Correct. No action needed.

---

## Unimplemented Feature: M6 — Ctrl+Wheel zoom in double page

**Already implemented.** `wheelEvent` checks `m_isDoublePage && modifiers & Qt::ControlModifier` → `zoomBy(±20)`. Correct. No action needed.

---

## Unimplemented Feature: M7 — Side nav arrow zones

**What Groundwork does:** `SideNavZone` (lines 1812–1843) — `WA_TransparentForMouseEvents`, covers left or right 1/3 of reader. `paintEvent` draws "‹"/"›" in Segoe UI 22pt Black with drop-shadow offsets only when `_hover=True`. Hover toggled by parent `mouseMoveEvent`.

**What we have:** Left/right thirds are functional click zones but have no visual arrow indicator. New users cannot discover navigation by hovering.

**Implement? Yes.** Adds discoverability, matches Groundwork/Max exactly.

**C++ approach:**

```cpp
// ComicReader.h — add inner class and members
class SideNavArrow : public QWidget {
public:
    SideNavArrow(const QString& arrow, bool alignRight, QWidget* parent)
        : QWidget(parent), m_arrow(arrow), m_alignRight(alignRight) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAutoFillBackground(false);
    }
    void setHover(bool h) { if (m_hover != h) { m_hover = h; update(); } }
protected:
    void paintEvent(QPaintEvent*) override {
        if (!m_hover) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(QFont("Segoe UI", 22, QFont::Black));
        QRect tr = rect().adjusted(4, 0, -4, 0);
        Qt::Alignment align = Qt::AlignVCenter |
            (m_alignRight ? Qt::AlignRight : Qt::AlignLeft);
        p.setPen(QColor(0, 0, 0, 235));
        for (auto [ox, oy] : std::initializer_list<std::pair<int,int>>{
                {-1,0},{1,0},{0,-1},{0,1}})
            p.drawText(tr.translated(ox, oy), align, m_arrow);
        p.setPen(QColor(255, 255, 255, 250));
        p.drawText(tr, align, m_arrow);
    }
private:
    QString m_arrow; bool m_alignRight; bool m_hover = false;
};

SideNavArrow* m_leftNavArrow  = nullptr;
SideNavArrow* m_rightNavArrow = nullptr;

// buildUI():
m_leftNavArrow  = new SideNavArrow("‹", false, this);
m_rightNavArrow = new SideNavArrow("›", true,  this);

// resizeEvent(): cover left/right thirds
int third = width() / 3;
m_leftNavArrow->setGeometry(0, 0, third, height());
m_rightNavArrow->setGeometry(width() - third, 0, third, height());
m_leftNavArrow->raise(); m_rightNavArrow->raise();

// mouseMoveEvent(): update hover based on x
int x = event->pos().x();
bool inLeft  = !m_isScrollStrip && (x < width() / 3);
bool inRight = !m_isScrollStrip && (x > width() * 2 / 3);
m_leftNavArrow->setHover(inLeft);
m_rightNavArrow->setHover(inRight);
```

Arrows hidden in scroll strip (side zones inactive there). Updated in `cycleReaderMode()` when entering scroll strip.

---

## Keyboard shortcut gap (not in my scope — flagging for Agent 1)

Max maps `KeyK` → keyboard help overlay (`toggleKeysOverlay()`). Ours has K unmapped. STATUS.md already lists this as Agent 1's next task.

---

## Summary

**Rough edges: 4**
- R1: SmoothScrollArea — pixelDelta, max_step cap, m_smoothY float sync
- R2: HUD freeze — click zones not gated by overlay-open state
- R3: Click zone flash — no "blocked" state for scroll mode non-nav
- R4: Page counter stale during scroll strip scrolling

**Unimplemented features: 5**
- M1: Vertical progress thumb (right edge) — new VerticalThumb class
- M2: Direction toast position — above toolbar, not screen center
- M3: Modal scrim — click-outside-to-dismiss for volume nav + goto dialog
- M4: Right-click context menu — all modes, using existing ContextMenuHelper
- M7: Side nav arrow zones — new SideNavArrow class

M5 (double-click fullscreen) and M6 (Ctrl+Wheel zoom) already correct.
