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

void SeekSlider::paintEvent(QPaintEvent* e)
{
    QSlider::paintEvent(e);

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
