#pragma once

#include <QWidget>
#include <QImage>
#include <QPixmap>

class FrameCanvas : public QWidget {
    Q_OBJECT

public:
    explicit FrameCanvas(QWidget* parent = nullptr);

    QRect frameRect() const;

public slots:
    void setFrame(const QImage& frame, qint64 ptsMs);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuildScaled();
    QRect targetRect(const QSize& pixSize) const;

    QImage  m_frame;
    QPixmap m_scaled;       // cached scaled pixmap — hardware accelerated paint
    QSize   m_lastWidgetSize;
};
