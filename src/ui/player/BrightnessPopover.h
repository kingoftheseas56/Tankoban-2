#pragma once

#include <QFrame>
#include <QPointer>

class QLabel;
class QPushButton;
class QSlider;

// MAKE_MPV_SOLO Task 9 (2026-05-01) — single-control brightness popover.
// Hemanth-narrowed scope: brightness only. Contrast/saturation stay
// removed per VIDEO_HUD_MINIMALIST Phase 1 (2026-04-25). One horizontal
// slider, range -100..+100, default 0 = no change. Live-update on drag
// (mpv brightness property write is cheap; ffmpeg sidecar set_filters
// rebuild may flicker — follow-up).
//
// VideoPlayer owns the absolute state (m_brightness) and pushes back via
// setBrightness after each slider tick. Display-only popover, mirrors
// SettingsPopover chrome (header + dark bg + click-outside dismiss).
class BrightnessPopover : public QFrame
{
    Q_OBJECT

public:
    explicit BrightnessPopover(QWidget* parent = nullptr);

    // Programmatic value sync (signal-blocked so the slider doesn't
    // re-emit brightnessChanged back at VideoPlayer during restore).
    void setBrightness(int value);

    void toggle(QWidget* anchor = nullptr);
    bool isOpen() const { return isVisible(); }

signals:
    // Absolute value, range -100..+100. Fires on every slider tick during
    // drag (Hemanth gate: "Drag the bar → picture brightness changes
    // immediately. No lag.").
    void brightnessChanged(int value);
    // MAKE_MPV_SOLO Task 9 follow-up (2026-05-01) — popover Reset button
    // emits this; VideoPlayer connects to setBrightness(0) + toast.
    void resetClicked();
    void hoverChanged(bool hovered);
    // Mirror SubtitlePopover/AudioPopover/SettingsPopover dismissed signal
    // so the Brightness chip's :checked state stays in lockstep with
    // popover visibility (VIDEO_HUD_MINIMALIST polish 2026-04-25 pattern).
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

    QSlider*     m_slider     = nullptr;
    QLabel*      m_valueLabel = nullptr;
    QPushButton* m_resetBtn   = nullptr;  // MAKE_MPV_SOLO Task 9 follow-up
    bool m_clickFilterInstalled = false;
    QPointer<QWidget> m_anchor;
};
