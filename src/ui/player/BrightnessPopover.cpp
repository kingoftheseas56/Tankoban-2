#include "BrightnessPopover.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

const char* HEADER_SS =
    "color: rgba(214,194,164,0.95);"
    "font-size: 11px;"
    "font-weight: 700;"
    "border: none;";

const char* LABEL_SS =
    "color: rgba(255,255,255,0.55);"
    "font-size: 11px;"
    "border: none;";

const char* VALUE_SS =
    "color: rgba(255,255,255,0.92);"
    "font-size: 12px;"
    "font-weight: 600;"
    "border: none;";

const char* RESET_BTN_SS =
    "QPushButton {"
    "  background: rgba(40,40,40,230);"
    "  color: #ccc;"
    "  border: 1px solid rgba(255,255,255,0.10);"
    "  border-radius: 4px;"
    "  padding: 4px 10px;"
    "  font-size: 11px;"
    "  font-weight: 600;"
    "}"
    "QPushButton:hover {"
    "  background: rgba(60,60,60,230);"
    "}";

const char* SLIDER_SS =
    "QSlider::groove:horizontal {"
    "  background: rgba(255,255,255,0.10);"
    "  height: 4px;"
    "  border-radius: 2px;"
    "}"
    "QSlider::sub-page:horizontal {"
    "  background: rgba(214,194,164,0.65);"
    "  height: 4px;"
    "  border-radius: 2px;"
    "}"
    "QSlider::handle:horizontal {"
    "  background: rgba(220,220,220,0.95);"
    "  width: 12px;"
    "  margin: -5px 0;"
    "  border-radius: 6px;"
    "}"
    "QSlider::handle:horizontal:hover {"
    "  background: white;"
    "}";

QString formatBrightness(int v)
{
    if (v == 0) return QStringLiteral("0");
    return QStringLiteral("%1%2").arg(v > 0 ? "+" : "").arg(v);
}

} // namespace

BrightnessPopover::BrightnessPopover(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("BrightnessPopover");
    setStyleSheet(
        "#BrightnessPopover {"
        "  background: rgba(16,16,16,240);"
        "  border: 1px solid rgba(255,255,255,31);"
        "  border-radius: 8px;"
        "}"
    );
    setMinimumWidth(280);
    setMaximumWidth(320);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(8);

    auto* header = new QLabel("Brightness");
    header->setStyleSheet(HEADER_SS);
    lay->addWidget(header);

    auto* row = new QHBoxLayout;
    row->setSpacing(8);

    auto* lbl = new QLabel("Level");
    lbl->setStyleSheet(LABEL_SS);
    lbl->setMinimumWidth(40);
    row->addWidget(lbl);

    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(-100, 100);
    m_slider->setValue(0);
    m_slider->setSingleStep(5);
    m_slider->setPageStep(20);
    m_slider->setTickInterval(50);
    m_slider->setTickPosition(QSlider::NoTicks);
    m_slider->setStyleSheet(SLIDER_SS);
    m_slider->setFocusPolicy(Qt::NoFocus);
    row->addWidget(m_slider, 1);

    m_valueLabel = new QLabel(formatBrightness(0));
    m_valueLabel->setStyleSheet(VALUE_SS);
    m_valueLabel->setAlignment(Qt::AlignCenter);
    m_valueLabel->setMinimumWidth(48);
    row->addWidget(m_valueLabel);

    lay->addLayout(row);

    // MAKE_MPV_SOLO Task 9 follow-up (2026-05-01) — Reset button on its
    // own row, right-aligned. Returns brightness to 0 (neutral); mirrors
    // the keyboard 'r' key path. VideoPlayer connects resetClicked to
    // setBrightness(0) + toast in the same dispatch as the keyboard reset.
    auto* resetRow = new QHBoxLayout;
    resetRow->setSpacing(8);
    resetRow->addStretch(1);
    m_resetBtn = new QPushButton(QStringLiteral("Reset"));
    m_resetBtn->setStyleSheet(RESET_BTN_SS);
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    m_resetBtn->setFocusPolicy(Qt::NoFocus);
    m_resetBtn->setToolTip(QStringLiteral("Reset brightness to 0"));
    resetRow->addWidget(m_resetBtn);
    lay->addLayout(resetRow);

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        if (m_valueLabel) m_valueLabel->setText(formatBrightness(v));
        emit brightnessChanged(v);
    });
    connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
        emit resetClicked();
    });

    hide();
}

void BrightnessPopover::setBrightness(int value)
{
    if (!m_slider) return;
    if (value < -100) value = -100;
    if (value > 100) value = 100;
    if (m_slider->value() == value) return;
    QSignalBlocker blocker(m_slider);
    m_slider->setValue(value);
    if (m_valueLabel) m_valueLabel->setText(formatBrightness(value));
}

void BrightnessPopover::toggle(QWidget* anchor)
{
    if (isVisible()) {
        dismiss();
        return;
    }
    m_anchor = anchor;
    if (anchor) anchorAbove(anchor);
    show();
    raise();
    installClickFilter();
}

bool BrightnessPopover::eventFilter(QObject* /*obj*/, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint gp = me->globalPosition().toPoint();
        if (rect().contains(mapFromGlobal(gp))) return false;
        const bool onAnchor = m_anchor
            && QRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size()).contains(gp);
        dismiss();
        return onAnchor;
    }
    return false;
}

void BrightnessPopover::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);
    emit hoverChanged(true);
}

void BrightnessPopover::leaveEvent(QEvent* event)
{
    QFrame::leaveEvent(event);
    emit hoverChanged(false);
}

void BrightnessPopover::wheelEvent(QWheelEvent* event)
{
    event->accept();
}

void BrightnessPopover::dismiss()
{
    removeClickFilter();
    hide();
    m_anchor.clear();
    emit hoverChanged(false);
    emit dismissed();
}

void BrightnessPopover::installClickFilter()
{
    if (m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance()) {
        app->installEventFilter(this);
        m_clickFilterInstalled = true;
    }
}

void BrightnessPopover::removeClickFilter()
{
    if (!m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance()) app->removeEventFilter(this);
    m_clickFilterInstalled = false;
}

void BrightnessPopover::anchorAbove(QWidget* anchor)
{
    QWidget* p = parentWidget();
    if (!p) return;
    const QPoint anchorPos = anchor->mapTo(p, anchor->rect().topRight());
    const int pw = sizeHint().width();
    const int ph = sizeHint().height();
    const int x  = qMax(0, anchorPos.x() - pw);
    const int y  = qMax(0, anchorPos.y() - ph - 8);
    setGeometry(x, y, pw, ph);
}
