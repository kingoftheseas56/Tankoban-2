#pragma once

#include <QWidget>
#include <QImage>
#include <QPixmap>
#include <QTimer>

class ShmFrameReader;

class FrameCanvas : public QWidget {
    Q_OBJECT

public:
    explicit FrameCanvas(QWidget* parent = nullptr);
    ~FrameCanvas() override;

    void attachShm(ShmFrameReader* reader);
    void detachShm();
    void startPolling();
    void stopPolling();

    QRect frameRect() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void pollFrame();
    void rebuildScaled();
    QRect targetRect(const QSize& frameSize) const;

    ShmFrameReader* m_reader = nullptr;
    QTimer          m_pollTimer;

    QImage  m_frame;       // only used for resize rebuild
    QPixmap m_scaled;      // what we actually paint
    QSize   m_frameSize;   // current frame dimensions
    QSize   m_lastWidgetSize;
};
