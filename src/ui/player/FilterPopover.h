#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QPointer>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

class QPushButton;

class FilterPopover : public QFrame {
    Q_OBJECT

public:
    explicit FilterPopover(QWidget* parent = nullptr);
    ~FilterPopover() override;

    void toggle(QPushButton* anchor = nullptr);
    void showAbove(QPushButton* anchor);
    bool isOpen() const { return isVisible(); }

    // State accessors
    bool deinterlace() const;
    QString deinterlaceFilter() const;  // returns ffmpeg filter string or empty
    bool interpolate() const;
    bool normalize() const;
    void setDeinterlace(bool v);
    void setInterpolate(bool v);
    void setNormalize(bool v);

    // HDR tone mapping controls (shown only when HDR content detected)
    void setHdrMode(bool hdr);
    QString toneMapAlgorithm() const;
    bool peakDetect() const;

    // FFmpeg filter string builders
    QString buildVideoFilter() const;
    QString buildAudioFilter() const;

    // Count of non-default filters (for chip badge)
    int activeFilterCount() const;

signals:
    void filtersChanged(bool deinterlace, int brightness, int contrast,
                        int saturation, bool normalize);
    void toneMappingChanged(const QString& algorithm, bool peakDetect);
    void hoverChanged(bool hovered);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void dismiss();
    void installClickFilter();
    void removeClickFilter();
    void onControlChanged();
    void onSliderChanged(int value);
    void emitFiltersChanged();
    void updateValueLabels();
    void blockAll(bool block);
    void anchorAbove(QWidget* anchor);

    static QLabel* makeHeader(const QString& text);

    struct SliderRow { QSlider* slider; QLabel* valueLabel; };
    SliderRow addSliderRow(QVBoxLayout* parent, const QString& label,
                           int minVal, int maxVal, int defaultVal);

    QComboBox* m_deinterlaceMode = nullptr;
    QCheckBox* m_interpolate  = nullptr;
    QSlider*   m_brightness   = nullptr;
    QSlider*   m_contrast     = nullptr;
    QSlider*   m_saturation   = nullptr;
    QCheckBox* m_normalize    = nullptr;

    QLabel*    m_brightnessVal = nullptr;
    QLabel*    m_contrastVal   = nullptr;
    QLabel*    m_saturationVal = nullptr;

    // HDR tone mapping (hidden when not HDR)
    QLabel*    m_hdrHeader    = nullptr;
    QComboBox* m_toneMapAlgo  = nullptr;
    QCheckBox* m_peakDetect   = nullptr;
    QFrame*    m_hdrDivider   = nullptr;

    QTimer     m_debounce;
    bool       m_clickFilterInstalled = false;
    // Anchor tracked across toggle()/dismiss() so eventFilter can swallow
    // clicks that land on the opener button — prevents the dismiss-then-
    // reopen race where the outside-click dismiss fires AND the button's
    // clicked() signal re-toggles the popover in the same event cycle.
    QPointer<QWidget> m_anchor;
};
