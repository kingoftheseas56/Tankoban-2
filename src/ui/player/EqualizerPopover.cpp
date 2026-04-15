#include "EqualizerPopover.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QVBoxLayout>

constexpr int EqualizerPopover::BAND_FREQS[];

EqualizerPopover::EqualizerPopover(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("EqualizerPopover");
    setFixedWidth(340);

    setStyleSheet(
        "QFrame#EqualizerPopover {"
        "  background: rgba(16, 16, 16, 240);"
        "  border: 1px solid rgba(255, 255, 255, 31);"
        "  border-radius: 8px;"
        "}"
    );

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(4);

    auto* header = new QLabel("Equalizer");
    header->setStyleSheet(
        "color: rgba(214,194,164,240); font-size: 10px; font-weight: 700; border: none;"
    );
    lay->addWidget(header);

    // Band sliders (vertical, side by side)
    auto* bandsRow = new QHBoxLayout();
    bandsRow->setSpacing(2);

    for (int i = 0; i < BAND_COUNT; ++i) {
        auto* col = new QVBoxLayout();
        col->setSpacing(2);
        col->setAlignment(Qt::AlignCenter);

        m_valLabels[i] = new QLabel("0");
        m_valLabels[i]->setStyleSheet(
            "color: rgba(255,255,255,120); font-size: 8px; border: none;");
        m_valLabels[i]->setAlignment(Qt::AlignCenter);
        m_valLabels[i]->setFixedWidth(28);
        col->addWidget(m_valLabels[i]);

        m_sliders[i] = new QSlider(Qt::Vertical);
        m_sliders[i]->setRange(-12, 12);  // dB
        m_sliders[i]->setValue(0);
        m_sliders[i]->setFixedHeight(100);
        m_sliders[i]->setStyleSheet(
            "QSlider::groove:vertical {"
            "  width: 4px; background: rgba(255,255,255,25); border-radius: 2px;"
            "}"
            "QSlider::handle:vertical {"
            "  height: 10px; width: 10px; margin: 0 -3px;"
            "  background: #ccc; border-radius: 5px;"
            "}"
        );
        col->addWidget(m_sliders[i], 0, Qt::AlignCenter);

        auto* freqLabel = new QLabel(BAND_LABELS[i]);
        freqLabel->setStyleSheet(
            "color: rgba(255,255,255,100); font-size: 8px; border: none;");
        freqLabel->setAlignment(Qt::AlignCenter);
        freqLabel->setFixedWidth(28);
        col->addWidget(freqLabel);

        bandsRow->addLayout(col);

        connect(m_sliders[i], &QSlider::valueChanged, this, &EqualizerPopover::onSliderChanged);
    }
    lay->addLayout(bandsRow);

    // Reset button
    auto* resetBtn = new QPushButton("Reset");
    resetBtn->setStyleSheet(
        "QPushButton { background: rgba(40,40,40,230); color: #ccc;"
        "  border: 1px solid rgba(255,255,255,0.1); border-radius: 4px;"
        "  padding: 4px 12px; font-size: 10px; }"
        "QPushButton:hover { background: rgba(60,60,60,230); }"
    );
    connect(resetBtn, &QPushButton::clicked, this, &EqualizerPopover::resetAll);
    lay->addWidget(resetBtn, 0, Qt::AlignCenter);

    // Batch 4.3 — Dynamic Range Compression toggle.
    // "Loud movie at low volume keeps dialogue audible" — sidecar-side
    // feed-forward compressor (threshold -12 dB, ratio 3:1, attack 10 ms,
    // release 100 ms) lights up when this is checked. Off by default.
    // Styled in the existing grayscale aesthetic (per feedback_no_color_no_emoji).
    m_drcCheck = new QCheckBox("Dynamic Range Compression");
    m_drcCheck->setStyleSheet(
        "QCheckBox { color: rgba(214,194,164,240); font-size: 10px; border: none; padding-top: 6px; }"
        "QCheckBox::indicator { width: 12px; height: 12px; }"
        "QCheckBox::indicator:unchecked {"
        "  background: rgba(40,40,40,230);"
        "  border: 1px solid rgba(255,255,255,0.2); border-radius: 2px;"
        "}"
        "QCheckBox::indicator:checked {"
        "  background: rgba(214,194,164,240);"
        "  border: 1px solid rgba(214,194,164,240); border-radius: 2px;"
        "}"
    );
    m_drcCheck->setChecked(false);
    connect(m_drcCheck, &QCheckBox::toggled, this, &EqualizerPopover::drcToggled);
    lay->addWidget(m_drcCheck, 0, Qt::AlignCenter);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(200);
    connect(&m_debounce, &QTimer::timeout, this, [this]() {
        emit eqChanged(filterString());
    });

    hide();
}

void EqualizerPopover::onSliderChanged()
{
    for (int i = 0; i < BAND_COUNT; ++i)
        m_valLabels[i]->setText(QString::number(m_sliders[i]->value()));
    m_debounce.start();
}

void EqualizerPopover::resetAll()
{
    for (int i = 0; i < BAND_COUNT; ++i)
        m_sliders[i]->setValue(0);
}

QString EqualizerPopover::filterString() const
{
    QStringList parts;
    for (int i = 0; i < BAND_COUNT; ++i) {
        int gain = m_sliders[i]->value();
        if (gain == 0) continue;
        // octave bandwidth for each band
        parts << QString("equalizer=f=%1:t=o:w=1:g=%2")
                    .arg(BAND_FREQS[i]).arg(gain);
    }
    return parts.join(",");
}

bool EqualizerPopover::isActive() const
{
    for (int i = 0; i < BAND_COUNT; ++i)
        if (m_sliders[i]->value() != 0) return true;
    return false;
}

void EqualizerPopover::toggle(QWidget* anchor)
{
    if (isVisible()) {
        dismiss();
        return;
    }
    m_anchor = anchor;
    if (anchor) anchorAbove(anchor);
    show();
    raise();
    if (auto* app = QApplication::instance()) {
        app->installEventFilter(this);
        m_clickFilterInstalled = true;
    }
}

void EqualizerPopover::anchorAbove(QWidget* anchor)
{
    QWidget* p = parentWidget();
    if (!p) return;
    QPoint pos = anchor->mapTo(p, anchor->rect().topRight());
    int pw = sizeHint().width();
    int ph = sizeHint().height();
    setGeometry(qMax(0, pos.x() - pw), qMax(0, pos.y() - ph - 8), pw, ph);
}

void EqualizerPopover::dismiss()
{
    if (m_clickFilterInstalled) {
        if (auto* app = QApplication::instance())
            app->removeEventFilter(this);
        m_clickFilterInstalled = false;
    }
    hide();
    m_anchor.clear();
}

bool EqualizerPopover::eventFilter(QObject*, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint gp = me->globalPosition().toPoint();
        if (rect().contains(mapFromGlobal(gp)))
            return false;
        // Outside popover — if the press is on the anchor chip, swallow
        // (return true) after dismiss so the chip's clicked() signal
        // doesn't immediately re-toggle us open.
        const bool onAnchor = m_anchor
            && QRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size()).contains(gp);
        dismiss();
        return onAnchor;
    }
    return false;
}

void EqualizerPopover::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);
    emit hoverChanged(true);
}

void EqualizerPopover::leaveEvent(QEvent* event)
{
    QFrame::leaveEvent(event);
    emit hoverChanged(false);
}
