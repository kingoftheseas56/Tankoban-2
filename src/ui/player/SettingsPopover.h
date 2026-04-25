#pragma once

#include <QFrame>
#include <QPointer>

class QLabel;
class QPushButton;

// Two-row delay popover: Audio delay + Subtitle delay, +/- buttons
// only. No reset, no slider, no presets — Hemanth verbatim "nothing
// fancy" 2026-04-25.
//
// Step asymmetry is intentional:
//  - Audio delay  ±50 ms  (matches existing Ctrl+= / Ctrl+- / Ctrl+0
//    keybinds and the per-Bluetooth-device persistence model in
//    VideoPlayer::adjustAudioDelay).
//  - Subtitle delay ±100 ms (matches mpv default + the prior
//    TrackPopover step the muscle memory is trained on).
//
// The popover is display-only: VideoPlayer owns the absolute delay
// state (m_audioDelayMs / m_subDelayMs) and pushes new values back
// via setAudioDelay / setSubtitleDelay after the helper applies the
// delta.
class SettingsPopover : public QFrame
{
    Q_OBJECT

public:
    explicit SettingsPopover(QWidget* parent = nullptr);

    void setAudioDelay(int ms);
    void setSubtitleDelay(int ms);

    void toggle(QWidget* anchor = nullptr);
    bool isOpen() const { return isVisible(); }

signals:
    // Deltas: ±50 for audio, ±100 for subtitle (see step rationale above).
    void audioDelayAdjusted(int deltaMs);
    void subtitleDelayAdjusted(int deltaMs);
    void hoverChanged(bool hovered);
    // VIDEO_HUD_MINIMALIST polish 2026-04-25 — see SubtitlePopover.h
    // for rationale; fired from dismiss() so the Settings chip's
    // :checked state mirrors popover visibility in lockstep.
    void dismissed();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void dismiss();
    void installClickFilter();
    void removeClickFilter();
    void anchorAbove(QWidget* anchor);

    QLabel*      m_audioDelayValue = nullptr;
    QLabel*      m_subDelayValue   = nullptr;
    QPushButton* m_audioMinus      = nullptr;
    QPushButton* m_audioPlus       = nullptr;
    QPushButton* m_subMinus        = nullptr;
    QPushButton* m_subPlus         = nullptr;
    bool m_clickFilterInstalled = false;
    QPointer<QWidget> m_anchor;
};
