#pragma once

#include <QFrame>
#include <QPointer>

class QLabel;
class QPushButton;
class QCheckBox;

// Three-row settings popover: Audio delay + Subtitle delay + Subtitle
// position, +/- buttons only. No reset, no slider, no presets —
// Hemanth verbatim "nothing fancy" 2026-04-25 + "keep it minimalistic"
// when the position row was added.
//
// Step asymmetry is intentional:
//  - Audio delay  ±50 ms  (matches existing Ctrl+= / Ctrl+- / Ctrl+0
//    keybinds and the per-Bluetooth-device persistence model in
//    VideoPlayer::adjustAudioDelay).
//  - Subtitle delay ±100 ms (matches mpv default + the prior
//    TrackPopover step the muscle memory is trained on).
//  - Subtitle position ±5% (mpv `r`/`R` parity; range clamped 0..100
//    in VideoPlayer::adjustSubPosition; default 100 = bottom).
//
// The popover is display-only: VideoPlayer owns the absolute state
// (m_audioDelayMs / m_subDelayMs / m_subPositionPct) and pushes new
// values back via setAudioDelay / setSubtitleDelay /
// setSubtitlePosition after the helper applies the delta.
class SettingsPopover : public QFrame
{
    Q_OBJECT

public:
    explicit SettingsPopover(QWidget* parent = nullptr);

    void setAudioDelay(int ms);
    void setSubtitleDelay(int ms);
    void setSubtitlePosition(int pct);
    // MPV_FFMPEG_PARITY Phase 2.F (2026-04-30) — sync the Force-position
    // checkbox with persisted state on popover construction / restore.
    // mode = "standard" or "force".
    void setSubtitlePositionMode(const QString& mode);
    // MPV_FFMPEG_PARITY Phase 2.G (2026-04-30) — subtitle size scale.
    // Multiplier on the base font size (1.0 = sidecar default, range
    // 0.5..2.0 clamped). Display formats as "1.0x" / "1.2x" etc.
    void setSubtitleSize(double scale);

    void toggle(QWidget* anchor = nullptr);
    bool isOpen() const { return isVisible(); }

signals:
    // Deltas: ±50 for audio, ±100 for subtitle delay, ±5 for subtitle
    // position (see step rationale above).
    void audioDelayAdjusted(int deltaMs);
    void subtitleDelayAdjusted(int deltaMs);
    void subtitlePositionAdjusted(int deltaPct);
    // MPV_FFMPEG_PARITY Phase 2.F (2026-04-30) — Force-position checkbox.
    // Emitted when the user toggles the "Force position" checkbox.
    // mode is "standard" (unchecked) or "force" (checked).
    void subtitlePositionModeChanged(const QString& mode);
    // MPV_FFMPEG_PARITY Phase 2.G (2026-04-30) — subtitle size +/-. Delta
    // is +/- 0.1 (10% step). VideoPlayer clamps the absolute value 0.5..2.0
    // and pushes the new scale back via setSubtitleSize.
    void subtitleSizeAdjusted(double deltaScale);
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
    QLabel*      m_subPosValue     = nullptr;
    QPushButton* m_audioMinus      = nullptr;
    QPushButton* m_audioPlus       = nullptr;
    QPushButton* m_subMinus        = nullptr;
    QPushButton* m_subPlus         = nullptr;
    QPushButton* m_subPosMinus     = nullptr;
    QPushButton* m_subPosPlus      = nullptr;
    QCheckBox*   m_forcePosCheckbox = nullptr; // MPV_FFMPEG_PARITY Phase 2.F
    // MPV_FFMPEG_PARITY Phase 2.G (2026-04-30) — subtitle size row.
    QLabel*      m_subSizeValue    = nullptr;
    QPushButton* m_subSizeMinus    = nullptr;
    QPushButton* m_subSizePlus     = nullptr;
    bool m_clickFilterInstalled = false;
    QPointer<QWidget> m_anchor;
};
