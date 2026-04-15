#include "ScrollStripCanvas.h"

#include <QPainter>
#include <QPaintEvent>
#include <QImage>
#include <cmath>
#include <algorithm>

namespace {
// P2-1: Brightness filter — mirror of ComicReader.cpp's helper. Kept inline
// here to avoid coupling the canvas to ComicReader internals. Math identical.
QPixmap applyBrightness(const QPixmap& src, int delta)
{
    if (delta == 0 || src.isNull()) return src;
    QImage img = src.toImage().convertToFormat(QImage::Format_ARGB32);
    const int shift = qRound(delta * 2.55);
    const int h = img.height();
    const int w = img.width();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb px = line[x];
            const int r = qBound(0, qRed(px)   + shift, 255);
            const int g = qBound(0, qGreen(px) + shift, 255);
            const int b = qBound(0, qBlue(px)  + shift, 255);
            line[x] = qRgba(r, g, b, qAlpha(px));
        }
    }
    QPixmap out = QPixmap::fromImage(std::move(img));
    out.setDevicePixelRatio(src.devicePixelRatioF());
    return out;
}
} // namespace

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

        bool splitting = m_splitOnWide && m_slots[pageIndex].isSpread;
        if (splitting) {
            // P3-3: cut the wide page in half horizontally, scale each half
            // independently, store left in the main scaled cache and right
            // on the slot. paintEvent stacks them with SPLIT_GAP between.
            int halfW = origW / 2;
            QPixmap leftFull  = fullRes.copy(0, 0, halfW, origH);
            QPixmap rightFull = fullRes.copy(halfW, 0, origW - halfW, origH);
            QPixmap leftScaled  = leftFull.scaledToWidth(physW, m_scalingQuality);
            QPixmap rightScaled = rightFull.scaledToWidth(physW, m_scalingQuality);
            leftScaled.setDevicePixelRatio(dpr);
            rightScaled.setDevicePixelRatio(dpr);
            leftScaled  = applyBrightness(leftScaled,  m_filterBrightness);
            rightScaled = applyBrightness(rightScaled, m_filterBrightness);
            m_scaledCache[pageIndex] = leftScaled;
            m_slots[pageIndex].rightHalf = rightScaled;
        } else {
            QPixmap scaled = fullRes.scaledToWidth(physW, m_scalingQuality);
            scaled.setDevicePixelRatio(dpr);
            scaled = applyBrightness(scaled, m_filterBrightness);  // P2-1
            m_scaledCache[pageIndex] = scaled;
            m_slots[pageIndex].rightHalf = QPixmap();  // ensure no stale right half
        }
    }

    // Defer Y offset rebuild to paintEvent (batches multiple decodes)
    m_yOffsetsDirty = true;
    update();
}

void ScrollStripCanvas::invalidateScaledCache()
{
    m_scaledCache.clear();
    // P3-3: right halves live on the slots — wipe them too so a rebuild
    // doesn't paint stale halves alongside fresh left pixmaps.
    for (auto& slot : m_slots) slot.rightHalf = QPixmap();
    m_scaledCacheWidth = m_viewportWidth;
    rebuildYOffsets();
    update();
}

void ScrollStripCanvas::setFilterBrightness(int delta)
{
    delta = qBound(-100, delta, 100);
    if (delta == m_filterBrightness) return;
    m_filterBrightness = delta;
    // Scaled pixmaps already have the old filter baked in — wipe and let
    // paintEvent re-request decodes through the needDecode signal chain.
    m_scaledCache.clear();
    // P3-3: right halves carry baked-in filter too
    for (auto& slot : m_slots) slot.rightHalf = QPixmap();
    update();
}

void ScrollStripCanvas::setSidePadding(int px)
{
    px = qBound(0, px, 400);  // sanity cap — beyond this the page becomes unreadable
    if (px == m_sidePadding) return;
    m_sidePadding = px;
    // Target page width changes → existing scaled cache is for the wrong
    // width. invalidate + rebuild Y offsets to reflect new page heights.
    invalidateScaledCache();
}

void ScrollStripCanvas::setSplitOnWide(bool enabled)
{
    if (enabled == m_splitOnWide) return;
    m_splitOnWide = enabled;
    // Wide pages need re-scaling at portrait fraction (split mode) vs full
    // viewport (non-split). Wipe cache so onPageDecoded re-runs the right
    // branch on next decode feed.
    invalidateScaledCache();
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
    for (int idx : toRemove) {
        m_scaledCache.remove(idx);
        // P3-3: evict the right half too — they live + die together
        if (idx >= 0 && idx < m_slots.size())
            m_slots[idx].rightHalf = QPixmap();
    }
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

    constexpr int SPLIT_GAP_PAINT = 16;  // P3-3 — must match SPLIT_GAP in rebuildYOffsets
    for (int i = first; i < m_pageCount; ++i) {
        double pageY = m_slots[i].yOffset;
        double pageH = m_slots[i].height;

        if (pageY > viewBot) break;

        // Paint at actual widget Y position (QScrollArea handles the viewport offset)
        int drawY = static_cast<int>(pageY);

        auto it = m_scaledCache.find(i);
        bool splitting = m_splitOnWide && m_slots[i].isSpread;
        if (it != m_scaledCache.end()) {
            int logW = qRound(it->width() / it->devicePixelRatioF());
            int drawX = (width() - logW) / 2;
            p.drawPixmap(drawX, drawY, *it);

            if (splitting && !m_slots[i].rightHalf.isNull()) {
                // P3-3: right half stacks below left half
                int leftLogH = qRound(it->height() / it->devicePixelRatioF());
                int rightLogW = qRound(m_slots[i].rightHalf.width()
                                       / m_slots[i].rightHalf.devicePixelRatioF());
                int rightDrawX = (width() - rightLogW) / 2;
                int rightDrawY = drawY + leftLogH + SPLIT_GAP_PAINT;
                p.drawPixmap(rightDrawX, rightDrawY, m_slots[i].rightHalf);
            }
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
    constexpr double SPLIT_GAP = 16.0;  // P3-3 — vertical gap between stacked halves
    double y = 0.0;
    for (int i = 0; i < m_pageCount; ++i) {
        m_slots[i].yOffset = y;

        int pw = targetPageWidth(i);
        bool splitting = m_splitOnWide && m_slots[i].isSpread;
        if (m_slots[i].decoded && m_slots[i].origW > 0) {
            if (splitting) {
                // Each half has aspect ratio (origH / (origW/2)) = 2*origH / origW.
                // Slot height stacks: halfH + SPLIT_GAP + halfH.
                double halfRatio = 2.0 * static_cast<double>(m_slots[i].origH) / m_slots[i].origW;
                double halfH = pw * halfRatio;
                m_slots[i].height = halfH + SPLIT_GAP + halfH;
            } else {
                double ratio = static_cast<double>(m_slots[i].origH) / m_slots[i].origW;
                m_slots[i].height = pw * ratio;
            }
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
    // P3-3: when split-on-wide is on, wide pages render as two stacked
    // half-width portraits — each half wants the portrait fraction, not
    // the full-viewport spread fraction.
    bool splitting = m_splitOnWide && spread;
    // P3-1: webtoon-style side padding reduces usable width before
    // portrait-fraction scaling. Applies to both spreads and portraits so a
    // fully-bled spread still respects user-set bars.
    int usableWidth = qMax(1, m_viewportWidth - 2 * m_sidePadding);
    double frac = (spread && !splitting) ? 1.0 : (m_portraitWidthPct / 100.0);
    int target = static_cast<int>(usableWidth * frac);

    // No-upscale: never scale beyond original dimensions of the half-or-full.
    if (slot.decoded && slot.origW > 0) {
        int origLimit = splitting ? (slot.origW / 2) : slot.origW;
        target = qMin(target, origLimit);
    }

    return qMax(1, target);
}
