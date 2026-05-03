#pragma once

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

class ToastHud : public QWidget {
    Q_OBJECT

public:
    explicit ToastHud(QWidget* parent = nullptr);

    void showToast(const QString& message);

    // MAKE_MPV_BEAT_FFMPEG Task 8 (2026-05-03) — backend-aware backdrop
    // alpha. Same reason as VolumeHud — the toast pill's rgba(10,10,10,217)
    // background composites against the parent's backing store, which on
    // the mpv path has the library page bleeding through. Set true on mpv
    // backend to flip alpha to 255 (fully opaque pill).
    void setBackdropOpaque(bool opaque);

private slots:
    void startFadeOut();

private:
    QLabel*                m_label      = nullptr;
    QGraphicsOpacityEffect* m_effect    = nullptr;
    QPropertyAnimation*    m_fadeAnim   = nullptr;
    QTimer                 m_holdTimer;
    bool                   m_backdropOpaque = false;  // Task 8 — mpv-backend opacity gate.
};
