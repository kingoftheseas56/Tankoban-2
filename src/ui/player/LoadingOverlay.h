#pragma once

#include <QWidget>
#include <QPropertyAnimation>
#include <QString>

// PLAYER_UX_FIX Phase 2.3 — centered overlay that surfaces player-lifecycle
// state the user would otherwise experience as a blank canvas: the "opening"
// window between sendOpen and first-frame, plus the "buffering" state during
// HTTP stall on stream URLs. Two modes (Loading / Buffering) render the same
// pill-shaped background with different text; transitions fade in/out via
// QPropertyAnimation on the opacity property. Mouse events pass through —
// user controls underneath remain responsive.
//
// Visual style matches VolumeHud / CenterFlash: dark translucent pill,
// hairline white border, off-white text. No color, no emoji per
// feedback_no_color_no_emoji.
class LoadingOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit LoadingOverlay(QWidget* parent = nullptr);

    // Connect to VideoPlayer::playerOpeningStarted(QString). Fades in
    // with "Loading — <basename>" text centered over the parent. Safe
    // to call while Buffering is showing — transitions back to Loading.
    void showLoading(const QString& filename);

    // Connect to SidecarProcess::bufferingStarted(). Shows "Buffering…"
    // text. If already visible in Loading mode, mutates in place (no
    // re-fade). If hidden, fades in.
    void showBuffering();

    // Connect to any of: SidecarProcess::firstFrame, bufferingEnded,
    // VideoPlayer::playerIdle. Fades out + hides on completion.
    // Idempotent — safe to call when already dismissed.
    void dismiss();

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal o);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    enum class Mode { Hidden, Loading, Buffering };

    void fadeIn();
    void fadeOut();
    void repositionCentered();

    Mode                 m_mode = Mode::Hidden;
    QString              m_filename;
    qreal                m_opacity = 0.0;
    QPropertyAnimation*  m_fadeAnim = nullptr;
};
