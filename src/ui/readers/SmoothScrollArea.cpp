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
}

void SmoothScrollArea::wheelEvent(QWheelEvent* event)
{
    // Accumulate pixel delta
    double delta = -event->angleDelta().y();
    // Convert angle to pixels (standard: 120 units = ~100px)
    double px = delta * 0.8;
    m_pendingPx += px;

    if (!m_drainTimer.isActive())
        m_drainTimer.start();

    event->accept();
}

void SmoothScrollArea::drainWheel()
{
    if (std::abs(m_pendingPx) < SNAP_THRESHOLD) {
        m_pendingPx = 0.0;
        m_drainTimer.stop();
        return;
    }

    double step = m_pendingPx * DRAIN_FRACTION;
    m_pendingPx -= step;

    auto* vbar = verticalScrollBar();
    if (vbar)
        vbar->setValue(vbar->value() + static_cast<int>(step));
}
