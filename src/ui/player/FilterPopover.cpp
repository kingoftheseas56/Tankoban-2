#include "FilterPopover.h"

#include <QApplication>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPushButton>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

FilterPopover::FilterPopover(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("FilterPopover");
    setMinimumWidth(220);
    setMaximumWidth(320);

    setStyleSheet(
        "QFrame#FilterPopover {"
        "  background: rgba(16, 16, 16, 240);"
        "  border: 1px solid rgba(255, 255, 255, 31);"
        "  border-radius: 8px;"
        "}"
    );

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(6);

    // --- Video section ---
    lay->addWidget(makeHeader("Video"));

    auto* diRow = new QHBoxLayout();
    diRow->setSpacing(4);
    auto* diLabel = new QLabel("Deinterlace");
    diLabel->setStyleSheet("color: rgba(255,255,255,140); font-size: 10px; border: none;");
    diLabel->setFixedWidth(62);
    diRow->addWidget(diLabel);
    m_deinterlaceMode = new QComboBox();
    m_deinterlaceMode->addItem("Off",      "");
    m_deinterlaceMode->addItem("Auto",     "yadif=mode=0");
    m_deinterlaceMode->addItem("Bob",      "yadif=mode=1");
    m_deinterlaceMode->addItem("Adaptive", "bwdif=mode=0");
    m_deinterlaceMode->addItem("W3FDIF",   "w3fdif");
    m_deinterlaceMode->setStyleSheet(
        "QComboBox { background: rgba(255,255,255,18); color: rgba(255,255,255,220);"
        "  font-size: 10px; border: 1px solid rgba(255,255,255,30); border-radius: 3px;"
        "  padding: 2px 6px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: rgb(30,30,30);"
        "  color: rgba(255,255,255,220); selection-background-color: rgba(255,255,255,30); }"
    );
    diRow->addWidget(m_deinterlaceMode, 1);
    lay->addLayout(diRow);

    m_interpolate = new QCheckBox("Motion smoothing");
    m_interpolate->setStyleSheet(
        "QCheckBox { color: rgba(255,255,255,234); font-size: 11px; border: none; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
    );
    lay->addWidget(m_interpolate);

    auto brRow = addSliderRow(lay, "Brightness", -100, 100, 0);
    m_brightness    = brRow.slider;
    m_brightnessVal = brRow.valueLabel;

    auto ctRow = addSliderRow(lay, "Contrast", 0, 200, 100);
    m_contrast    = ctRow.slider;
    m_contrastVal = ctRow.valueLabel;

    auto satRow = addSliderRow(lay, "Saturation", 0, 200, 100);
    m_saturation    = satRow.slider;
    m_saturationVal = satRow.valueLabel;

    // Divider
    auto* div = new QFrame();
    div->setFrameShape(QFrame::HLine);
    div->setFixedHeight(1);
    div->setStyleSheet("background: rgba(255,255,255,20); border: none;");
    lay->addWidget(div);

    // --- Audio section ---
    lay->addWidget(makeHeader("Audio"));

    m_normalize = new QCheckBox("Volume Normalization");
    m_normalize->setStyleSheet(
        "QCheckBox { color: rgba(255,255,255,234); font-size: 11px; border: none; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
    );
    lay->addWidget(m_normalize);

    // --- HDR section (hidden by default, shown when HDR content detected) ---
    m_hdrDivider = new QFrame();
    m_hdrDivider->setFrameShape(QFrame::HLine);
    m_hdrDivider->setFixedHeight(1);
    m_hdrDivider->setStyleSheet("background: rgba(255,255,255,20); border: none;");
    lay->addWidget(m_hdrDivider);

    m_hdrHeader = makeHeader("HDR Tone Mapping");
    lay->addWidget(m_hdrHeader);

    auto* algoRow = new QHBoxLayout();
    algoRow->setSpacing(4);
    auto* algoLbl = new QLabel("Algorithm");
    algoLbl->setObjectName("HdrAlgoLabel");
    algoLbl->setStyleSheet("color: rgba(255,255,255,140); font-size: 10px; border: none;");
    algoLbl->setFixedWidth(62);
    algoRow->addWidget(algoLbl);
    m_toneMapAlgo = new QComboBox();
    m_toneMapAlgo->addItems({"hable", "reinhard", "bt2390", "mobius", "clip", "linear"});
    m_toneMapAlgo->setCurrentText("hable");
    m_toneMapAlgo->setStyleSheet(
        "QComboBox { background: rgba(255,255,255,18); color: rgba(255,255,255,220);"
        "  font-size: 10px; border: 1px solid rgba(255,255,255,30); border-radius: 3px;"
        "  padding: 2px 6px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: rgb(30,30,30);"
        "  color: rgba(255,255,255,220); selection-background-color: rgba(255,255,255,30); }"
    );
    algoRow->addWidget(m_toneMapAlgo, 1);
    lay->addLayout(algoRow);

    m_peakDetect = new QCheckBox("Peak detection");
    m_peakDetect->setChecked(true);
    m_peakDetect->setStyleSheet(
        "QCheckBox { color: rgba(255,255,255,234); font-size: 11px; border: none; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
    );
    lay->addWidget(m_peakDetect);

    // Hide HDR section by default
    setHdrMode(false);

    // Debounce timer
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(300);
    connect(&m_debounce, &QTimer::timeout, this, &FilterPopover::emitFiltersChanged);

    // Connect controls
    connect(m_deinterlaceMode, &QComboBox::currentIndexChanged, this, &FilterPopover::onControlChanged);
    connect(m_interpolate, &QCheckBox::toggled, this, &FilterPopover::onControlChanged);
    connect(m_brightness, &QSlider::valueChanged, this, &FilterPopover::onSliderChanged);
    connect(m_contrast,   &QSlider::valueChanged, this, &FilterPopover::onSliderChanged);
    connect(m_saturation, &QSlider::valueChanged, this, &FilterPopover::onSliderChanged);
    connect(m_normalize,  &QCheckBox::toggled, this, &FilterPopover::onControlChanged);
    connect(m_toneMapAlgo, &QComboBox::currentTextChanged, this, [this]() {
        emit toneMappingChanged(m_toneMapAlgo->currentText(), m_peakDetect->isChecked());
    });
    connect(m_peakDetect, &QCheckBox::toggled, this, [this]() {
        emit toneMappingChanged(m_toneMapAlgo->currentText(), m_peakDetect->isChecked());
    });

    hide();
}

FilterPopover::~FilterPopover()
{
    removeClickFilter();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QLabel* FilterPopover::makeHeader(const QString& text)
{
    auto* lbl = new QLabel(text);
    lbl->setStyleSheet(
        "color: rgba(214,194,164,240); font-size: 10px; font-weight: 700; border: none;"
    );
    return lbl;
}

FilterPopover::SliderRow FilterPopover::addSliderRow(
    QVBoxLayout* parentLayout, const QString& label,
    int minVal, int maxVal, int defaultVal)
{
    auto* row = new QHBoxLayout();
    row->setSpacing(4);

    auto* name = new QLabel(label);
    name->setStyleSheet("color: rgba(255,255,255,140); font-size: 10px; border: none;");
    name->setFixedWidth(62);
    row->addWidget(name);

    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(minVal, maxVal);
    slider->setValue(defaultVal);
    slider->setStyleSheet(
        "QSlider::groove:horizontal {"
        "  height: 4px; background: rgba(255,255,255,25); border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 12px; height: 12px; margin: -4px 0;"
        "  background: #ccc; border-radius: 6px;"
        "}"
    );
    row->addWidget(slider, 1);

    auto* val = new QLabel(QString::number(defaultVal / 100.0, 'f', 1));
    val->setStyleSheet("color: rgba(255,255,255,140); font-size: 10px; border: none;");
    val->setFixedWidth(32);
    val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(val);

    parentLayout->addLayout(row);
    return { slider, val };
}

// ---------------------------------------------------------------------------
// State accessors
// ---------------------------------------------------------------------------

bool FilterPopover::deinterlace() const { return m_deinterlaceMode->currentIndex() > 0; }
QString FilterPopover::deinterlaceFilter() const { return m_deinterlaceMode->currentData().toString(); }
bool FilterPopover::interpolate() const { return m_interpolate->isChecked(); }
bool FilterPopover::normalize() const { return m_normalize->isChecked(); }

void FilterPopover::setDeinterlace(bool v)
{
    m_deinterlaceMode->setCurrentIndex(v ? 1 : 0);  // Off or Auto
}

void FilterPopover::setInterpolate(bool v)
{
    m_interpolate->setChecked(v);
}

void FilterPopover::setNormalize(bool v)
{
    m_normalize->setChecked(v);
}

void FilterPopover::setHdrMode(bool hdr)
{
    m_hdrDivider->setVisible(hdr);
    m_hdrHeader->setVisible(hdr);
    m_toneMapAlgo->setVisible(hdr);
    m_peakDetect->setVisible(hdr);
    // Also show/hide the algo label
    if (auto* lbl = findChild<QLabel*>("HdrAlgoLabel"))
        lbl->setVisible(hdr);
}

QString FilterPopover::toneMapAlgorithm() const
{
    return m_toneMapAlgo->currentText();
}

bool FilterPopover::peakDetect() const
{
    return m_peakDetect->isChecked();
}

// ---------------------------------------------------------------------------
// Filter string builders
// ---------------------------------------------------------------------------

QString FilterPopover::buildVideoFilter() const
{
    QStringList parts;
    // Deinterlace filter string from combo ("" if Off, else yadif/bwdif/w3fdif spec)
    QString diFilter = m_deinterlaceMode->currentData().toString();
    if (!diFilter.isEmpty())
        parts << diFilter;

    double b = m_brightness->value() / 100.0;
    double c = m_contrast->value()   / 100.0;
    double s = m_saturation->value() / 100.0;

    if (qAbs(b) > 0.001 || qAbs(c - 1.0) > 0.001 || qAbs(s - 1.0) > 0.001)
        parts << QString("eq=brightness=%1:contrast=%2:saturation=%3")
                     .arg(b, 0, 'f', 2).arg(c, 0, 'f', 2).arg(s, 0, 'f', 2);

    return parts.join(QStringLiteral(","));
}

QString FilterPopover::buildAudioFilter() const
{
    if (m_normalize->isChecked())
        return QStringLiteral("loudnorm=I=-16");
    return {};
}

int FilterPopover::activeFilterCount() const
{
    int count = 0;
    if (m_deinterlaceMode->currentIndex() > 0) ++count;
    if (m_interpolate->isChecked())   ++count;
    if (m_brightness->value() != 0)   ++count;
    if (m_contrast->value()   != 100) ++count;
    if (m_saturation->value() != 100) ++count;
    if (m_normalize->isChecked())     ++count;
    return count;
}

// ---------------------------------------------------------------------------
// Debounce / control change
// ---------------------------------------------------------------------------

void FilterPopover::onSliderChanged(int /*value*/)
{
    updateValueLabels();
    m_debounce.start();
}

void FilterPopover::onControlChanged()
{
    m_debounce.start();
}

void FilterPopover::emitFiltersChanged()
{
    // Signal carries "deinterlace on/off" bool; listeners query deinterlaceFilter()
    // on the popover to get the actual filter string (yadif/bwdif/w3fdif).
    emit filtersChanged(
        m_deinterlaceMode->currentIndex() > 0,
        m_brightness->value(),
        m_contrast->value(),
        m_saturation->value(),
        m_normalize->isChecked()
    );
}

void FilterPopover::updateValueLabels()
{
    m_brightnessVal->setText(QString::number(m_brightness->value() / 100.0, 'f', 1));
    m_contrastVal->setText(QString::number(m_contrast->value()     / 100.0, 'f', 1));
    m_saturationVal->setText(QString::number(m_saturation->value() / 100.0, 'f', 1));
}

void FilterPopover::blockAll(bool block)
{
    m_deinterlaceMode->blockSignals(block);
    m_interpolate->blockSignals(block);
    m_brightness->blockSignals(block);
    m_contrast->blockSignals(block);
    m_saturation->blockSignals(block);
    m_normalize->blockSignals(block);
}

// ---------------------------------------------------------------------------
// Toggle / anchor
// ---------------------------------------------------------------------------

void FilterPopover::toggle(QPushButton* anchor)
{
    if (isVisible()) {
        dismiss();
        return;
    }
    m_anchor = anchor;
    if (anchor)
        anchorAbove(anchor);
    show();
    raise();
    installClickFilter();
}

void FilterPopover::showAbove(QPushButton* anchor)
{
    m_anchor = anchor;
    anchorAbove(anchor);
    show();
    raise();
    installClickFilter();
}

void FilterPopover::anchorAbove(QWidget* anchor)
{
    QWidget* p = parentWidget();
    if (!p) return;
    QPoint anchorPos = anchor->mapTo(p, anchor->rect().topRight());
    int pw = sizeHint().width();
    int ph = sizeHint().height();
    int x = qMax(0, anchorPos.x() - pw);
    int y = qMax(0, anchorPos.y() - ph - 8);
    setGeometry(x, y, pw, ph);
}

// ---------------------------------------------------------------------------
// Click-outside dismiss
// ---------------------------------------------------------------------------

void FilterPopover::dismiss()
{
    removeClickFilter();
    hide();
    m_anchor.clear();
}

void FilterPopover::installClickFilter()
{
    if (m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance()) {
        app->installEventFilter(this);
        m_clickFilterInstalled = true;
    }
}

void FilterPopover::removeClickFilter()
{
    if (!m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance())
        app->removeEventFilter(this);
    m_clickFilterInstalled = false;
}

bool FilterPopover::eventFilter(QObject* /*obj*/, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint gp = me->globalPosition().toPoint();
        const QPoint local = mapFromGlobal(gp);
        if (rect().contains(local)) {
            return false;  // click inside popover — let child widgets handle it
        }
        // Outside the popover: dismiss. If the click fell on the anchor
        // button that opened us, swallow the event (return true) so the
        // button's clicked() signal doesn't immediately re-toggle us back
        // open on the same user click.
        const bool onAnchor = m_anchor
            && QRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size()).contains(gp);
        dismiss();
        return onAnchor;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Enter / leave (HUD auto-hide prevention)
// ---------------------------------------------------------------------------

void FilterPopover::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);
    emit hoverChanged(true);
}

void FilterPopover::leaveEvent(QEvent* event)
{
    QFrame::leaveEvent(event);
    emit hoverChanged(false);
}
