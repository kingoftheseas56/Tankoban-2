#include "ScrollStripCanvas.h"

#include <QPainter>
#include <QPaintEvent>
#include <cmath>
#include <algorithm>

ScrollStripCanvas::ScrollStripCanvas(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setStyleSheet("background: #000;");
}

void ScrollStripCanvas::setPageCount(int count)
{
    m_pageCount = count;
    m_slots.resize(count);
    for (int i = 0; i < count; ++i) {
        m_slots[i] = PageSlot{};
    }
    m_scaledCache.clear();
    rebuildYOffsets();
}

// E3+E4: Fast dimension hint — update slot before full decode arrives
void ScrollStripCanvas::updatePageDimensions(int pageIndex, int w, int h)
{
    if (pageIndex < 0 || pageIndex >= m_pageCount) return;
    if (w <= 0 || h <= 0) return;
    auto& slot = m_slots[pageIndex];
    if (slot.decoded) return;  // full decode already arrived — its data takes priority
    slot.origW = w;
    slot.origH = h;
    slot.isSpread = (static_cast<double>(w) / h >= SPREAD_RATIO);  // E4: spread from dims
    m_yOffsetsDirty = true;
    update();
}

void ScrollStripCanvas::setPortraitWidthPct(int pct)
{
    if (m_portraitWidthPct == pct) return;
    m_portraitWidthPct = pct;
}

void ScrollStripCanvas::setViewportWidth(int w)
{
    if (m_viewportWidth == w) return;
    m_viewportWidth = w;
}

void ScrollStripCanvas::onPageDecoded(int pageIndex, const QPixmap& fullRes, int origW, int origH)
{
    if (pageIndex < 0 || pageIndex >= m_pageCount) return;

    auto& slot = m_slots[pageIndex];
    slot.origW = origW;
    slot.origH = origH;
    slot.decoded = true;
    slot.isSpread = (origH > 0 && static_cast<double>(origW) / origH > SPREAD_RATIO);

    // E2: Scale to physical pixels, tag with DPR so Qt draws at logical size
    int pw = targetPageWidth(pageIndex);
    if (pw > 0) {
        double dpr = devicePixelRatioF();
        int physW = static_cast<int>(pw * dpr + 0.5);
        QPixmap scaled = fullRes.scaledToWidth(physW, m_scalingQuality);
        scaled.setDevicePixelRatio(dpr);
        m_scaledCache[pageIndex] = scaled;
    }

    // Defer Y offset rebuild to paintEvent (batches multiple decodes)
    m_yOffsetsDirty = true;
    update();
}

void ScrollStripCanvas::invalidateScaledCache()
{
    m_scaledCache.clear();
    m_scaledCacheWidth = m_viewportWidth;
    rebuildYOffsets();
    update();
}

void ScrollStripCanvas::evictScaledOutsideZone(int viewportH, double prefetchMult)
{
    double margin = viewportH * prefetchMult;
    double loadTop = m_scrollOffset - margin;
    double loadBot = m_scrollOffset + viewportH + margin;

    QList<int> toRemove;
    for (auto it = m_scaledCache.begin(); it != m_scaledCache.end(); ++it) {
        int idx = it.key();
        if (idx < 0 || idx >= m_pageCount) { toRemove.append(idx); continue; }
        double pageY = m_slots[idx].yOffset;
        double pageBot = pageY + m_slots[idx].height;
        if (pageBot < loadTop || pageY > loadBot)
            toRemove.append(idx);
    }
    for (int idx : toRemove)
        m_scaledCache.remove(idx);
}

void ScrollStripCanvas::setScrollOffset(double offset)
{
    m_scrollOffset = offset;
    update();
}

double ScrollStripCanvas::totalHeight() const
{
    if (m_pageCount == 0) return 0.0;
    const auto& last = m_slots[m_pageCount - 1];
    return last.yOffset + last.height;
}

int ScrollStripCanvas::pageAtCenter(int viewportH) const
{
    if (m_pageCount == 0) return 0;
    double centerY = m_scrollOffset + viewportH / 2.0;
    int idx = firstVisiblePage(centerY);
    return qBound(0, idx, m_pageCount - 1);
}

double ScrollStripCanvas::pageTopY(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= m_pageCount) return 0.0;
    return m_slots[pageIndex].yOffset;
}

QVector<int> ScrollStripCanvas::pagesNeedingDecode(int viewportH, double prefetchMult) const
{
    QVector<int> result;
    if (m_pageCount == 0) return result;

    double margin = viewportH * prefetchMult;
    double loadTop = m_scrollOffset - margin;
    double loadBot = m_scrollOffset + viewportH + margin;

    int first = firstVisiblePage(qMax(0.0, loadTop));
    for (int i = first; i < m_pageCount; ++i) {
        double pageY = m_slots[i].yOffset;
        if (pageY > loadBot) break;
        result.append(i);
    }
    return result;
}

// ── Core rendering ──────────────────────────────────────────────────────────

void ScrollStripCanvas::paintEvent(QPaintEvent* event)
{
    // Deferred Y offset rebuild (batches multiple onPageDecoded calls)
    if (m_yOffsetsDirty) {
        rebuildYOffsets();
        m_yOffsetsDirty = false;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_scalingQuality == Qt::SmoothTransformation);  // E1 + C4

    if (m_pageCount == 0) {
        p.fillRect(rect(), Qt::black);
        return;
    }

    // QScrollArea physically moves this widget — the painter is in widget coordinates.
    // Use the paint event's exposed rect for culling (what Qt needs repainted).
    QRect exposed = event->rect();
    double viewTop = exposed.top();
    double viewBot = exposed.bottom();

    // Fill only the exposed area with black (not the entire widget)
    p.fillRect(exposed, Qt::black);

    int first = firstVisiblePage(viewTop);

    for (int i = first; i < m_pageCount; ++i) {
        double pageY = m_slots[i].yOffset;
        double pageH = m_slots[i].height;

        if (pageY > viewBot) break;

        // Paint at actual widget Y position (QScrollArea handles the viewport offset)
        int drawY = static_cast<int>(pageY);

        auto it = m_scaledCache.find(i);
        if (it != m_scaledCache.end()) {
            int logW = qRound(it->width() / it->devicePixelRatioF());
            int drawX = (width() - logW) / 2;
            p.drawPixmap(drawX, drawY, *it);
        } else {
            int pw = targetPageWidth(i);
            int drawX = (width() - pw) / 2;
            p.fillRect(drawX, drawY, pw, static_cast<int>(pageH), QColor(0x11, 0x11, 0x11));
        }
    }
}

// ── Binary search ───────────────────────────────────────────────────────────

int ScrollStripCanvas::firstVisiblePage(double viewTop) const
{
    if (m_pageCount == 0) return 0;

    int lo = 0, hi = m_pageCount - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        double bottom = m_slots[mid].yOffset + m_slots[mid].height;
        if (bottom <= viewTop)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

// ── Geometry ────────────────────────────────────────────────────────────────

void ScrollStripCanvas::rebuildYOffsets()
{
    double y = 0.0;
    for (int i = 0; i < m_pageCount; ++i) {
        m_slots[i].yOffset = y;

        int pw = targetPageWidth(i);
        if (m_slots[i].decoded && m_slots[i].origW > 0) {
            double ratio = static_cast<double>(m_slots[i].origH) / m_slots[i].origW;
            m_slots[i].height = pw * ratio;
        } else {
            m_slots[i].height = pw * DEFAULT_ASPECT;
        }

        y += m_slots[i].height + SPACING;
    }

    // Tell Qt the total content size so QScrollArea gets proper scrollbar range
    setFixedHeight(static_cast<int>(std::ceil(y)));
}

int ScrollStripCanvas::targetPageWidth(int pageIndex) const
{
    if (m_viewportWidth <= 0) return 1;
    if (pageIndex < 0 || pageIndex >= m_pageCount) return 1;

    const auto& slot = m_slots[pageIndex];
    bool spread = slot.isSpread;
    double frac = spread ? 1.0 : (m_portraitWidthPct / 100.0);
    int target = static_cast<int>(m_viewportWidth * frac);

    // No-upscale: never scale beyond original dimensions
    if (slot.decoded && slot.origW > 0)
        target = qMin(target, slot.origW);

    return qMax(1, target);
}
