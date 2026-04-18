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

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.1 — classified open-pipeline
    // stages. Replaces the binary Loading/Buffering model so the user
    // sees which sub-step of the open is active during the 10-70s
    // window that used to be a silent "Loading — <filename>" pill.
    // Driven by SidecarProcess classified events (Phase 1.2):
    //   Opening            → state_changed{opening}, sendOpen ack'd
    //   Probing            → probe_start (probe_file in flight)
    //   OpeningDecoder     → decoder_open_start (avcodec_open2 in flight)
    //   DecodingFirstFrame → first_decoder_receive (first frame decoded,
    //                        presentation imminent; Rule-14 pick — NOT
    //                        first_packet_read, which only signals
    //                        demuxer/IO motion and can stall on decoder
    //                        back-pressure)
    //   Buffering          → buffering (PLAYER_UX_FIX Phase 2.2 HTTP
    //                        stall on stream URLs, post-first-frame
    //                        recovery path)
    //   TakingLonger       → Phase 2.2 30s watchdog fires from VideoPlayer
    //                        when firstFrame hasn't arrived; non-terminal
    //                        (user can still close; further progress
    //                        events still resolve the stage cleanly)
    enum class Stage {
        Opening,
        Probing,
        OpeningDecoder,
        DecodingFirstFrame,
        Buffering,
        TakingLonger,
    };

    // Set the current stage + optionally update filename. Empty filename
    // keeps the current value (useful for mid-pipeline transitions where
    // the filename was already set by the initial Opening stage). Calling
    // with a new filename at any time refreshes the text. Idempotent-ish:
    // same-stage + same-filename calls update text in place without
    // re-fade.
    void setStage(Stage stage, const QString& filename = {});

    // Backward-compat shortcuts preserved for existing call sites that
    // predate Phase 2.1. Both forward to setStage internally. New code
    // should prefer setStage directly.
    // Connect to VideoPlayer::playerOpeningStarted(QString). Fades in
    // with "Opening source — <basename>" text. Shortcut for
    // setStage(Stage::Opening, filename).
    void showLoading(const QString& filename);

    // Connect to SidecarProcess::bufferingStarted(). Shortcut for
    // setStage(Stage::Buffering). Preserves existing Phase 2.2 wiring
    // semantics; can mutate in place without re-fade if visible.
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
    void fadeIn();
    void fadeOut();
    void repositionCentered();
    QString textForStage() const;

    Stage                m_stage    = Stage::Opening;  // Unused until first show*/setStage.
    bool                 m_visible  = false;           // Drives paint guard + fadeIn-vs-mutate-in-place.
    QString              m_filename;
    qreal                m_opacity  = 0.0;
    QPropertyAnimation*  m_fadeAnim = nullptr;
};
