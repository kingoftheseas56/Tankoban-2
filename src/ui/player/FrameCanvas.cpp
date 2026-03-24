#include "ui/player/FrameCanvas.h"

#include <QPainter>

FrameCanvas::FrameCanvas(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 180);
}

void FrameCanvas::setFrame(const QImage& frame, qint64 /*ptsMs*/)
{
    {
        QMutexLocker lock(&m_frameMutex);
        m_frame = frame;
    }
    update();
}

void FrameCanvas::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), Qt::black);

    QImage frame;
    {
        QMutexLocker lock(&m_frameMutex);
        frame = m_frame;
    }

    if (frame.isNull())
        return;

    p.drawImage(targetRect(), frame);
}

QRect FrameCanvas::targetRect() const
{
    if (m_frame.isNull())
        return rect();

    QSize frameSize = m_frame.size();
    QSize widgetSize = size();

    frameSize.scale(widgetSize, Qt::KeepAspectRatio);

    int x = (widgetSize.width()  - frameSize.width())  / 2;
    int y = (widgetSize.height() - frameSize.height()) / 2;

    return QRect(QPoint(x, y), frameSize);
}

QRect FrameCanvas::frameRect() const
{
    return targetRect();
}
