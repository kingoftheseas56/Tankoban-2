#include "ui/player/FrameCanvas.h"

#include <QPainter>
#include <QResizeEvent>

FrameCanvas::FrameCanvas(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 180);
}

void FrameCanvas::setFrame(const QImage& frame, qint64 /*ptsMs*/)
{
    m_frame = frame;
    rebuildScaled();
    update();
}

void FrameCanvas::rebuildScaled()
{
    if (m_frame.isNull()) {
        m_scaled = QPixmap();
        return;
    }

    QRect r = targetRect(m_frame.size());

    // Fast scale: Qt::FastTransformation uses nearest-neighbor, very cheap.
    // At display resolution the difference from bilinear is invisible.
    m_scaled = QPixmap::fromImage(m_frame).scaled(
        r.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

void FrameCanvas::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    if (m_scaled.isNull())
        return;

    QRect r = targetRect(m_frame.size());
    p.drawPixmap(r.topLeft(), m_scaled);
}

void FrameCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (event->size() != m_lastWidgetSize) {
        m_lastWidgetSize = event->size();
        rebuildScaled();
    }
}

QRect FrameCanvas::targetRect(const QSize& frameSize) const
{
    if (frameSize.isEmpty())
        return rect();

    QSize scaled = frameSize;
    scaled.scale(size(), Qt::KeepAspectRatio);

    int x = (width()  - scaled.width())  / 2;
    int y = (height() - scaled.height()) / 2;

    return QRect(QPoint(x, y), scaled);
}

QRect FrameCanvas::frameRect() const
{
    if (m_frame.isNull())
        return rect();
    return targetRect(m_frame.size());
}
