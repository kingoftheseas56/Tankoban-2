#pragma once

#include <QScrollArea>
#include <QTimer>

class SmoothScrollArea : public QScrollArea {
    Q_OBJECT
public:
    explicit SmoothScrollArea(QWidget* parent = nullptr);

    // Public forwarding for external wheel events
    void handleWheel(QWheelEvent* event) { wheelEvent(event); }

    // Called by page jumps and scrub bar to reset float baseline
    void syncExternalScroll(int val);

protected:
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void drainWheel();

private:
    double m_pendingPx = 0.0;
    double m_smoothY   = 0.0;
    bool   m_draining  = false;
    QTimer m_drainTimer;
    static constexpr int    DRAIN_INTERVAL_MS = 16;      // ~60fps
    static constexpr double DRAIN_FRACTION    = 0.38;    // ease per frame
    static constexpr double SNAP_THRESHOLD    = 0.5;     // stop when < 0.5px
};
