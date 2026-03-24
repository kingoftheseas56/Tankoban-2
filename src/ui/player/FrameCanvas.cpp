#include "ui/player/FrameCanvas.h"
#include "ui/player/ShmFrameReader.h"

#include <QPainter>
#include <QResizeEvent>

FrameCanvas::FrameCanvas(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 180);

    // 8ms = ~125Hz poll — same as the Python ffmpeg_frame_canvas
    m_pollTimer.setInterval(8);
    m_pollTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_pollTimer, &QTimer::timeout, this, &FrameCanvas::pollFrame);
}

FrameCanvas::~FrameCanvas()
{
    m_pollTimer.stop();
}

void FrameCanvas::attachShm(ShmFrameReader* reader)
{
    m_reader = reader;
}

void FrameCanvas::detachShm()
{
    m_pollTimer.stop();
    m_reader = nullptr;
    m_frame  = QImage();
    m_scaled = QPixmap();
    update();
}

void FrameCanvas::startPolling()
{
    if (m_reader)
        m_pollTimer.start();
}

void FrameCanvas::stopPolling()
{
    m_pollTimer.stop();
}

void FrameCanvas::pollFrame()
{
    if (!m_reader || !m_reader->isAttached())
        return;

    auto f = m_reader->readLatest();
    if (!f.valid)
        return;

    // Wrap SHM pixels directly — no deep copy.
    // Scale immediately while data is valid (sidecar won't overwrite
    // this slot until it cycles through all 4 slots).
    QImage img(f.pixels, f.width, f.height, f.stride, QImage::Format_ARGB32);

    QRect r = targetRect(QSize(f.width, f.height));
    m_scaled = QPixmap::fromImage(img).scaled(
        r.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);

    // Store dimensions for targetRect in paintEvent
    m_frameSize = QSize(f.width, f.height);

    // Feedback: tell sidecar we consumed this frame
    m_reader->writeConsumerFid(f.frameId);

    update();
}

void FrameCanvas::rebuildScaled()
{
    // Only used on resize — re-scale from m_frame if we have one
    if (!m_frame.isNull()) {
        QRect r = targetRect(m_frame.size());
        m_scaled = QPixmap::fromImage(m_frame).scaled(
            r.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
}

void FrameCanvas::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    if (m_scaled.isNull())
        return;

    QRect r = targetRect(m_frameSize);
    p.drawPixmap(r.topLeft(), m_scaled);
}

void FrameCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (event->size() != m_lastWidgetSize) {
        m_lastWidgetSize = event->size();
        // Don't call rebuildScaled here — next pollFrame will pick up new size
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
    if (m_frameSize.isEmpty())
        return rect();
    return targetRect(m_frameSize);
}
