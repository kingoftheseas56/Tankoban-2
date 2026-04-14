#pragma once

#include <QFrame>
#include <QLabel>
#include <QSlider>
#include <QTimer>
#include <QPushButton>

// 10-band audio equalizer popover.
// Frequencies: 31, 62, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz.
// Emits filterString() as an ffmpeg audio filter chain.
class EqualizerPopover : public QFrame {
    Q_OBJECT

public:
    explicit EqualizerPopover(QWidget* parent = nullptr);

    void toggle(QWidget* anchor = nullptr);
    bool isOpen() const { return isVisible(); }

    // Build ffmpeg audio filter string (e.g. "equalizer=f=31:t=o:w=1:g=3,equalizer=...")
    QString filterString() const;

    // Are any bands non-zero?
    bool isActive() const;

signals:
    void eqChanged(const QString& filterString);
    void hoverChanged(bool hovered);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void dismiss();
    void onSliderChanged();
    void resetAll();
    void anchorAbove(QWidget* anchor);

    static constexpr int BAND_COUNT = 10;
    static constexpr int BAND_FREQS[BAND_COUNT] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    static constexpr const char* BAND_LABELS[BAND_COUNT] = {
        "31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"
    };

    QSlider* m_sliders[BAND_COUNT] = {};
    QLabel*  m_valLabels[BAND_COUNT] = {};
    QTimer   m_debounce;
    bool     m_clickFilterInstalled = false;
};
