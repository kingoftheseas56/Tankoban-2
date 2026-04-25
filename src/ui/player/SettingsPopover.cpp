#include "SettingsPopover.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

const int kAudioStepMs    = 50;
const int kSubtitleStepMs = 100;

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

const char* BTN_SS =
    "QPushButton {"
    "  background: rgba(40,40,40,230);"
    "  color: #ccc;"
    "  border: 1px solid rgba(255,255,255,0.10);"
    "  border-radius: 4px;"
    "  padding: 4px 10px;"
    "  font-size: 14px;"
    "  font-weight: 700;"
    "}"
    "QPushButton:hover {"
    "  background: rgba(60,60,60,230);"
    "}"
    "QPushButton:disabled {"
    "  background: rgba(30,30,30,0.55);"
    "  color: rgba(255,255,255,0.25);"
    "  border: 1px solid rgba(255,255,255,0.05);"
    "}";

QString formatDelay(int ms)
{
    if (ms == 0) return QStringLiteral("0 ms");
    return QStringLiteral("%1%2 ms").arg(ms > 0 ? "+" : "").arg(ms);
}

} // namespace

SettingsPopover::SettingsPopover(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("SettingsPopover");
    setStyleSheet(
        "#SettingsPopover {"
        "  background: rgba(16,16,16,240);"
        "  border: 1px solid rgba(255,255,255,31);"
        "  border-radius: 8px;"
        "}"
    );
    setMinimumWidth(260);
    setMaximumWidth(320);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(8);

    auto* header = new QLabel("Settings");
    header->setStyleSheet(HEADER_SS);
    lay->addWidget(header);

    // --- Audio delay row ---
    auto* audioRow = new QHBoxLayout;
    audioRow->setSpacing(8);

    auto* audioLbl = new QLabel("Audio delay");
    audioLbl->setStyleSheet(LABEL_SS);
    audioLbl->setMinimumWidth(96);
    audioRow->addWidget(audioLbl);

    m_audioMinus = new QPushButton(QStringLiteral("−"));
    m_audioMinus->setFixedSize(36, 30);
    m_audioMinus->setStyleSheet(BTN_SS);
    m_audioMinus->setToolTip(QStringLiteral("Audio delay -%1 ms").arg(kAudioStepMs));
    m_audioMinus->setFocusPolicy(Qt::NoFocus);
    audioRow->addWidget(m_audioMinus);

    m_audioDelayValue = new QLabel(formatDelay(0));
    m_audioDelayValue->setStyleSheet(VALUE_SS);
    m_audioDelayValue->setAlignment(Qt::AlignCenter);
    m_audioDelayValue->setMinimumWidth(72);
    audioRow->addWidget(m_audioDelayValue, 1);

    m_audioPlus = new QPushButton("+");
    m_audioPlus->setFixedSize(36, 30);
    m_audioPlus->setStyleSheet(BTN_SS);
    m_audioPlus->setToolTip(QStringLiteral("Audio delay +%1 ms").arg(kAudioStepMs));
    m_audioPlus->setFocusPolicy(Qt::NoFocus);
    audioRow->addWidget(m_audioPlus);

    lay->addLayout(audioRow);

    // --- Subtitle delay row ---
    auto* subRow = new QHBoxLayout;
    subRow->setSpacing(8);

    auto* subLbl = new QLabel("Subtitle delay");
    subLbl->setStyleSheet(LABEL_SS);
    subLbl->setMinimumWidth(96);
    subRow->addWidget(subLbl);

    m_subMinus = new QPushButton(QStringLiteral("−"));
    m_subMinus->setFixedSize(36, 30);
    m_subMinus->setStyleSheet(BTN_SS);
    m_subMinus->setToolTip(QStringLiteral("Subtitle delay -%1 ms").arg(kSubtitleStepMs));
    m_subMinus->setFocusPolicy(Qt::NoFocus);
    subRow->addWidget(m_subMinus);

    m_subDelayValue = new QLabel(formatDelay(0));
    m_subDelayValue->setStyleSheet(VALUE_SS);
    m_subDelayValue->setAlignment(Qt::AlignCenter);
    m_subDelayValue->setMinimumWidth(72);
    subRow->addWidget(m_subDelayValue, 1);

    m_subPlus = new QPushButton("+");
    m_subPlus->setFixedSize(36, 30);
    m_subPlus->setStyleSheet(BTN_SS);
    m_subPlus->setToolTip(QStringLiteral("Subtitle delay +%1 ms").arg(kSubtitleStepMs));
    m_subPlus->setFocusPolicy(Qt::NoFocus);
    subRow->addWidget(m_subPlus);

    lay->addLayout(subRow);

    connect(m_audioMinus, &QPushButton::clicked,
            this, [this]() { emit audioDelayAdjusted(-kAudioStepMs); });
    connect(m_audioPlus, &QPushButton::clicked,
            this, [this]() { emit audioDelayAdjusted(kAudioStepMs); });
    connect(m_subMinus, &QPushButton::clicked,
            this, [this]() { emit subtitleDelayAdjusted(-kSubtitleStepMs); });
    connect(m_subPlus, &QPushButton::clicked,
            this, [this]() { emit subtitleDelayAdjusted(kSubtitleStepMs); });

    hide();
}

void SettingsPopover::setAudioDelay(int ms)
{
    if (m_audioDelayValue) m_audioDelayValue->setText(formatDelay(ms));
}

void SettingsPopover::setSubtitleDelay(int ms)
{
    if (m_subDelayValue) m_subDelayValue->setText(formatDelay(ms));
}

void SettingsPopover::toggle(QWidget* anchor)
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

bool SettingsPopover::eventFilter(QObject* /*obj*/, QEvent* event)
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

void SettingsPopover::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);
    emit hoverChanged(true);
}

void SettingsPopover::leaveEvent(QEvent* event)
{
    QFrame::leaveEvent(event);
    emit hoverChanged(false);
}

void SettingsPopover::wheelEvent(QWheelEvent* event)
{
    event->accept();
}

void SettingsPopover::dismiss()
{
    removeClickFilter();
    hide();
    m_anchor.clear();
    // VIDEO_HUD_MINIMALIST 1.x bug-fix 2026-04-25 — see AudioPopover::dismiss
    // for rationale; mirror the hoverChanged(false) emit so the HUD auto-
    // hide timer restarts with a fresh 3s window post-dismiss.
    emit hoverChanged(false);
    // VIDEO_HUD_MINIMALIST polish 2026-04-25 — see SubtitlePopover::dismiss
    // for full rationale; emit dismissed so VideoPlayer can drive the
    // Settings chip's :checked state in lockstep with popover visibility.
    emit dismissed();
}

void SettingsPopover::installClickFilter()
{
    if (m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance()) {
        app->installEventFilter(this);
        m_clickFilterInstalled = true;
    }
}

void SettingsPopover::removeClickFilter()
{
    if (!m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance()) app->removeEventFilter(this);
    m_clickFilterInstalled = false;
}

void SettingsPopover::anchorAbove(QWidget* anchor)
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
