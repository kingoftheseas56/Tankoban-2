#pragma once
#include <QSlider>
#include <QList>
#include <QPair>

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

    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.4 — buffered-range overlay
    // for torrent-backed stream playback. Caller passes file-local
    // {startByte, endByte} pairs (endByte exclusive) of fully-downloaded
    // pieces + the total file byte size for fraction mapping. Semi-
    // transparent warm-amber fill renders between the groove base and
    // chapter ticks (groove → buffered fills → chapter ticks → handle
    // paint order). Empty list OR totalBytes <= 0 hides the overlay
    // (library-file mode sends empty to suppress). Paint is integer-pixel-
    // exact (no AA), matches existing chapter-tick style.
    void setBufferedRanges(const QList<QPair<qint64, qint64>>& ranges,
                           qint64 totalBytes);

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

    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.4 — buffered-range storage.
    // Written by setBufferedRanges; read by paintEvent. Cleared on teardown
    // via setBufferedRanges({}, 0) at VideoPlayer::teardownUi.
    QList<QPair<qint64, qint64>> m_bufferedRanges;
    qint64                       m_bufferedTotalBytes = 0;
};
