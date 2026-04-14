#include "SmoothScrollArea.h"

#include <QScrollBar>
#include <QWheelEvent>
#include <cmath>

SmoothScrollArea::SmoothScrollArea(QWidget* parent)
    : QScrollArea(parent)
{
    m_drainTimer.setInterval(DRAIN_INTERVAL_MS);
    m_drainTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_drainTimer, &QTimer::timeout, this, &SmoothScrollArea::drainWheel);
    // Sync m_smoothY when scrollbar moves externally (page jump, scrub bar)
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int val) {
        if (!m_draining) m_smoothY = double(val);
    });
}

void SmoothScrollArea::wheelEvent(QWheelEvent* event)
{
    // Bug 1: prefer pixelDelta (trackpad native px) over angleDelta (mouse ticks)
    double px = 0.0;
    QPoint pdp = event->pixelDelta();
    if (!pdp.isNull() && pdp.y() != 0)
        px = -double(pdp.y());
    else
        px = -double(event->angleDelta().y()) * (100.0 / 120.0);

    if (px == 0.0) { event->ignore(); return; }

    // Bug 3: cap backlog so sustained trackpad swipe can't pile up unbounded
    double cap = qMax(2400.0, double(viewport()->height() * 8));
    m_pendingPx = qBound(-cap, m_pendingPx + px, cap);

    if (!m_drainTimer.isActive()) m_drainTimer.start();
    event->accept();
}

void SmoothScrollArea::drainWheel()
{
    if (std::abs(m_pendingPx) < SNAP_THRESHOLD) {
        m_pendingPx = 0.0;
        m_drainTimer.stop();
        return;
    }

    // Bug 2: cap step so a fast burst can't lurch the view in one frame
    double maxStep = qMax(70.0, double(viewport()->height()) * 0.22);
    double step = qBound(-maxStep, m_pendingPx * DRAIN_FRACTION, maxStep);
    if (std::abs(step) < 2.0) step = m_pendingPx;   // snap tiny remainder
    m_pendingPx -= step;

    auto* vbar = verticalScrollBar();
    if (!vbar) return;

    // Bug 4: accumulate in float, round once — prevents integer truncation drift
    m_smoothY += step;
    m_smoothY = qBound(double(vbar->minimum()), m_smoothY, double(vbar->maximum()));
    int newVal = int(std::round(m_smoothY));
    if (newVal != vbar->value()) {
        m_draining = true;
        vbar->setValue(newVal);
        m_draining = false;
    }
}

void SmoothScrollArea::syncExternalScroll(int val)
{
    auto* vbar = verticalScrollBar();
    if (!vbar) return;
    m_smoothY = double(qBound(vbar->minimum(), val, vbar->maximum()));
}
