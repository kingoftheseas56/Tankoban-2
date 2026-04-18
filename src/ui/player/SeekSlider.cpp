#include "ui/player/SeekSlider.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>

SeekSlider::SeekSlider(Qt::Orientation o, QWidget* parent)
    : QSlider(o, parent)
{
    setRange(0, RANGE);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);

    // Groundwork seek slider style: warm amber fill, 5px groove, warm gradient handle
    setStyleSheet(R"(
        QSlider::groove:horizontal {
            height: 5px;
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 rgba(80, 80, 80, 230),
                stop:1 rgba(30, 30, 30, 242));
            border: 1px solid rgba(0, 0, 0, 166);
            border-radius: 2px;
        }
        QSlider::sub-page:horizontal {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 rgba(214, 194, 164, 230),
                stop:1 rgba(160, 140, 110, 230));
            border-radius: 2px;
        }
        QSlider::add-page:horizontal {
            background: rgba(20, 20, 20, 230);
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 rgba(240, 230, 210, 250),
                stop:1 rgba(170, 150, 120, 250));
            width: 12px;
            margin: -5px 0;
            border: 1px solid rgba(0, 0, 0, 166);
            border-radius: 6px;
        }
        QSlider::handle:horizontal:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 rgba(255, 245, 225, 250),
                stop:1 rgba(200, 180, 145, 250));
        }
    )");
}

void SeekSlider::setDurationSec(double dur)
{
    m_durationSec = qMax(0.0, dur);
}

void SeekSlider::setChapterMarkers(const QList<qint64>& markersMs)
{
    m_chapterMarkersMs = markersMs;
    update();
}

// PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.4 — buffered-range overlay
// setter. Stores the ranges + total file size, triggers repaint. Empty
// list + zero totalBytes hides the overlay (library-file mode path).
// Dedup is caller-side (StreamPlayerController's pollBufferedRangesOnce);
// we accept whatever arrives and update unconditionally — a no-op repaint
// from same-data is cheap at 1-2 Hz cadence, not worth guarding locally.
void SeekSlider::setBufferedRanges(const QList<QPair<qint64, qint64>>& ranges,
                                   qint64 totalBytes)
{
    m_bufferedRanges     = ranges;
    m_bufferedTotalBytes = totalBytes;
    update();
}

void SeekSlider::paintEvent(QPaintEvent* e)
{
    QSlider::paintEvent(e);

    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.4 — buffered-range overlay.
    // Paints BEFORE chapter ticks so ticks overlay on top of buffered
    // fills (piece boundaries don't occlude chapter boundaries). Paint
    // order: groove (QSlider default) → buffered fills (here) → chapter
    // ticks (below) → handle (QSlider default, last). Short-circuits when
    // no buffered data (library-file mode: setBufferedRanges({}, 0) was
    // called, OR stream hasn't received its first emit yet).
    if (!m_bufferedRanges.isEmpty() && m_bufferedTotalBytes > 0) {
        const QRect buffGroove = grooveRect();
        if (buffGroove.width() > 0) {
            QPainter bp(this);
            bp.setRenderHint(QPainter::Antialiasing, false);
            bp.setPen(Qt::NoPen);
            // Warm-amber semi-transparent — lighter than sub-page amber
            // fill (styled groundwork palette), warmer than add-page dark.
            // Visible on dark add-page (past-end-of-played zone); subtle
            // on amber sub-page (played zone; played bytes are by
            // definition buffered — overlap is semantically redundant
            // but visually harmonizes). Rule 14 technical default;
            // flip at smoke per Hemanth preference.
            bp.setBrush(QColor(180, 160, 120, 120));
            const int fillY = buffGroove.y() + 1;
            const int fillH = buffGroove.height() - 2;
            if (fillH > 0) {
                for (const auto& range : m_bufferedRanges) {
                    // Range is {startByte, endByte} file-local, endByte exclusive.
                    // Clamp to total-file bounds defensively (Batch 1.1 already
                    // clamps but belt-and-suspenders against future API drift).
                    const qint64 startByte = qBound<qint64>(0, range.first,  m_bufferedTotalBytes);
                    const qint64 endByte   = qBound<qint64>(0, range.second, m_bufferedTotalBytes);
                    if (endByte <= startByte) continue;
                    const double fracStart = static_cast<double>(startByte) / m_bufferedTotalBytes;
                    const double fracEnd   = static_cast<double>(endByte)   / m_bufferedTotalBytes;
                    const int xStart = buffGroove.x() + static_cast<int>(fracStart * buffGroove.width());
                    const int xEnd   = buffGroove.x() + static_cast<int>(fracEnd   * buffGroove.width());
                    const int fillW  = qMax(1, xEnd - xStart);  // min 1px so tiny ranges stay visible
                    bp.drawRect(xStart, fillY, fillW, fillH);
                }
            }
        }
    }

    if (m_chapterMarkersMs.isEmpty() || m_durationSec <= 0.0)
        return;

    const QRect groove = grooveRect();
    if (groove.width() <= 0)
        return;

    const double totalMs = m_durationSec * 1000.0;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    // Light gray tick, slightly taller than the 5px groove. Groundwork
    // palette is warm-desaturated; picking a near-white tick keeps the
    // markers visible on both the amber fill and the dark add-page
    // without introducing a color accent.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(230, 220, 200, 200));

    const int tickW = 2;
    const int tickExtendPx = 2;  // how far above/below the groove the tick pokes
    const int y = groove.y() - tickExtendPx;
    const int h = groove.height() + tickExtendPx * 2;

    for (qint64 ms : m_chapterMarkersMs) {
        if (ms <= 0 || ms >= static_cast<qint64>(totalMs))
            continue;
        const double frac = static_cast<double>(ms) / totalMs;
        const int x = groove.x() + static_cast<int>(frac * groove.width()) - tickW / 2;
        p.drawRect(x, y, tickW, h);
    }
}

QRect SeekSlider::grooveRect() const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    return style()->subControlRect(
        QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
}

double SeekSlider::fractionForX(int x) const
{
    QRect groove = grooveRect();
    if (groove.width() <= 0)
        return 0.0;
    return qBound(0.0, double(x - groove.x()) / groove.width(), 1.0);
}

// ── Mouse events ─────────────────────────────────────────────────────────────

void SeekSlider::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        // Click-to-seek: jump directly to clicked position (groundwork behavior)
        // setSliderDown emits sliderPressed(); setSliderPosition emits sliderMoved()
        setSliderDown(true);
        setSliderPosition(int(fractionForX(e->pos().x()) * RANGE));
        emit hoverPositionChanged(fractionForX(e->pos().x()));
        e->accept();
        return;
    }
    QSlider::mousePressEvent(e);
}

void SeekSlider::mouseMoveEvent(QMouseEvent* e)
{
    double frac = fractionForX(e->pos().x());
    emit hoverPositionChanged(frac);

    if (isSliderDown()) {
        // Live seek while dragging
        setSliderPosition(int(frac * RANGE));
    }
    e->accept();
}

void SeekSlider::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && isSliderDown()) {
        // setSliderDown(false) emits sliderReleased()
        setSliderDown(false);
        e->accept();
        return;
    }
    QSlider::mouseReleaseEvent(e);
}

void SeekSlider::leaveEvent(QEvent* e)
{
    emit hoverLeft();
    QSlider::leaveEvent(e);
}
