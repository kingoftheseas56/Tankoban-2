#pragma once
#include <QSlider>
#include <QList>

class SeekSlider : public QSlider {
    Q_OBJECT
public:
    explicit SeekSlider(Qt::Orientation o, QWidget* parent = nullptr);
    void setDurationSec(double dur);

    // Batch 2.1 (VIDEO_PLAYER_FIX Phase 2) — chapter tick markers on the
    // seekbar. Caller passes each chapter's start time in ms; the slider
    // renders a 2px vertical tick at each position. Empty list hides all
    // ticks. Duration for position mapping uses m_durationSec; call
    // setDurationSec BEFORE setChapterMarkers to get correct placement.
    void setChapterMarkers(const QList<qint64>& markersMs);

    static constexpr int RANGE = 10000;

signals:
    void hoverPositionChanged(double fraction);
    void hoverLeft();

protected:
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    QRect grooveRect() const;
    double fractionForX(int x) const;

    double         m_durationSec      = 0.0;
    QList<qint64>  m_chapterMarkersMs;
};
