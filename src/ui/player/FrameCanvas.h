#pragma once

#include <QWidget>
#include <QImage>
#include <QMutex>

class FrameCanvas : public QWidget {
    Q_OBJECT

public:
    explicit FrameCanvas(QWidget* parent = nullptr);

    QRect frameRect() const;

public slots:
    void setFrame(const QImage& frame, qint64 ptsMs);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRect targetRect() const;

    QImage m_frame;
    QMutex m_frameMutex;
};
