#include "SettingsPopover.h"

#include <QApplication>
#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

const int kAudioStepMs       = 50;
const int kSubtitleStepMs    = 100;
const int kSubtitlePosStepPct = 5;
// MPV_FFMPEG_PARITY Phase 2.G (2026-04-30) — subtitle size +/- step.
// 0.1 = 10% of base. SidecarProcess clamps the absolute value 0.5..3.0;
// VideoPlayer narrows to 0.5..2.0 for UI sanity.
const double kSubtitleSizeStep = 0.1;

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

QString formatPercent(int pct)
{
    return QStringLiteral("%1%").arg(pct);
}

QString formatSize(double scale)
{
    // 1.0 → "1.0x"; 0.7 → "0.7x"; format with one decimal even when
    // it'd round to an integer so the column width stays stable.
    return QStringLiteral("%1x").arg(scale, 0, 'f', 1);
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

    // --- Subtitle position row ---
    auto* subPosRow = new QHBoxLayout;
    subPosRow->setSpacing(8);

    auto* subPosLbl = new QLabel("Subtitle position");
    subPosLbl->setStyleSheet(LABEL_SS);
    subPosLbl->setMinimumWidth(96);
    subPosRow->addWidget(subPosLbl);

    m_subPosMinus = new QPushButton(QStringLiteral("−"));
    m_subPosMinus->setFixedSize(36, 30);
    m_subPosMinus->setStyleSheet(BTN_SS);
    m_subPosMinus->setToolTip(QStringLiteral("Subtitle position -%1%").arg(kSubtitlePosStepPct));
    m_subPosMinus->setFocusPolicy(Qt::NoFocus);
    subPosRow->addWidget(m_subPosMinus);

    m_subPosValue = new QLabel(formatPercent(100));
    m_subPosValue->setStyleSheet(VALUE_SS);
    m_subPosValue->setAlignment(Qt::AlignCenter);
    m_subPosValue->setMinimumWidth(72);
    subPosRow->addWidget(m_subPosValue, 1);

    m_subPosPlus = new QPushButton("+");
    m_subPosPlus->setFixedSize(36, 30);
    m_subPosPlus->setStyleSheet(BTN_SS);
    m_subPosPlus->setToolTip(QStringLiteral("Subtitle position +%1%").arg(kSubtitlePosStepPct));
    m_subPosPlus->setFocusPolicy(Qt::NoFocus);
    subPosRow->addWidget(m_subPosPlus);

    lay->addLayout(subPosRow);

    // MPV_FFMPEG_PARITY Phase 2.G (2026-04-30) — Subtitle size row.
    // Mirrors the position row shape (label + - / value / +) so the
    // popover stays consistent. Default 1.0x = sidecar baseline. Step
    // 0.1 emits subtitleSizeAdjusted; VideoPlayer clamps + persists.
    auto* subSizeRow = new QHBoxLayout;
    subSizeRow->setSpacing(8);

    auto* subSizeLbl = new QLabel(QStringLiteral("Sub size"));
    subSizeLbl->setStyleSheet(LABEL_SS);
    subSizeLbl->setMinimumWidth(96);
    subSizeRow->addWidget(subSizeLbl);

    m_subSizeMinus = new QPushButton(QStringLiteral("−"));
    m_subSizeMinus->setFixedSize(36, 30);
    m_subSizeMinus->setStyleSheet(BTN_SS);
    m_subSizeMinus->setToolTip(QStringLiteral("Subtitle size -10%"));
    m_subSizeMinus->setFocusPolicy(Qt::NoFocus);
    subSizeRow->addWidget(m_subSizeMinus);

    m_subSizeValue = new QLabel(formatSize(1.0));
    m_subSizeValue->setStyleSheet(VALUE_SS);
    m_subSizeValue->setAlignment(Qt::AlignCenter);
    m_subSizeValue->setMinimumWidth(72);
    subSizeRow->addWidget(m_subSizeValue, 1);

    m_subSizePlus = new QPushButton("+");
    m_subSizePlus->setFixedSize(36, 30);
    m_subSizePlus->setStyleSheet(BTN_SS);
    m_subSizePlus->setToolTip(QStringLiteral("Subtitle size +10%"));
    m_subSizePlus->setFocusPolicy(Qt::NoFocus);
    subSizeRow->addWidget(m_subSizePlus);

    lay->addLayout(subSizeRow);

    // MPV_FFMPEG_PARITY Phase 2.F (2026-04-30) — Force position checkbox.
    // Default off per Q1 ratification (Standard mode honors authored ASS
    // layout / mpv sub-pos semantics). Toggle on to override per-event
    // placement with the user-position slider — useful for files whose
    // authored MarginV pulls subs too high. Currently ffmpeg-only; mpv
    // backend logs a warning when Force is requested.
    auto* forceRow = new QHBoxLayout();
    forceRow->setContentsMargins(0, 4, 0, 0);
    m_forcePosCheckbox = new QCheckBox(QStringLiteral("Force position"));
    m_forcePosCheckbox->setStyleSheet(LABEL_SS);
    m_forcePosCheckbox->setToolTip(QStringLiteral(
        "Override authored subtitle layout (signs/karaoke).\n"
        "Off = follow author. On = always slide to user position."));
    m_forcePosCheckbox->setFocusPolicy(Qt::NoFocus);
    forceRow->addWidget(m_forcePosCheckbox);
    forceRow->addStretch(1);
    lay->addLayout(forceRow);

    connect(m_audioMinus, &QPushButton::clicked,
            this, [this]() { emit audioDelayAdjusted(-kAudioStepMs); });
    connect(m_audioPlus, &QPushButton::clicked,
            this, [this]() { emit audioDelayAdjusted(kAudioStepMs); });
    connect(m_subMinus, &QPushButton::clicked,
            this, [this]() { emit subtitleDelayAdjusted(-kSubtitleStepMs); });
    connect(m_subPlus, &QPushButton::clicked,
            this, [this]() { emit subtitleDelayAdjusted(kSubtitleStepMs); });
    connect(m_subPosMinus, &QPushButton::clicked,
            this, [this]() { emit subtitlePositionAdjusted(-kSubtitlePosStepPct); });
    connect(m_subPosPlus, &QPushButton::clicked,
            this, [this]() { emit subtitlePositionAdjusted(kSubtitlePosStepPct); });
    connect(m_subSizeMinus, &QPushButton::clicked,
            this, [this]() { emit subtitleSizeAdjusted(-kSubtitleSizeStep); });
    connect(m_subSizePlus, &QPushButton::clicked,
            this, [this]() { emit subtitleSizeAdjusted(kSubtitleSizeStep); });
    connect(m_forcePosCheckbox, &QCheckBox::toggled,
            this, [this](bool checked) {
                emit subtitlePositionModeChanged(
                    checked ? QStringLiteral("force")
                            : QStringLiteral("standard"));
            });

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

void SettingsPopover::setSubtitlePosition(int pct)
{
    if (m_subPosValue) m_subPosValue->setText(formatPercent(pct));
}

void SettingsPopover::setSubtitleSize(double scale)
{
    if (m_subSizeValue) m_subSizeValue->setText(formatSize(scale));
}

void SettingsPopover::setSubtitlePositionMode(const QString& mode)
{
    // MPV_FFMPEG_PARITY Phase 2.F (2026-04-30) — sync Force-position
    // checkbox with persisted state. Block-signals during the programmatic
    // sync so the toggle doesn't emit subtitlePositionModeChanged back at
    // VideoPlayer (which already knows the current mode).
    if (!m_forcePosCheckbox) return;
    const bool wantChecked = (mode == QStringLiteral("force"));
    if (m_forcePosCheckbox->isChecked() == wantChecked) return;
    QSignalBlocker blocker(m_forcePosCheckbox);
    m_forcePosCheckbox->setChecked(wantChecked);
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
