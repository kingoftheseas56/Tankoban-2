#include "ThemePicker.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QPushButton>

#include "../Theme.h"

// SVG-tinting helper formerly anonymous-namespace-local here is now a public
// utility at Theme::tintedSvgIcon (Theme.cpp). This widget calls through to
// that for sun/moon icon rendering — same behavior, shared infrastructure.

// ── ThemePicker ──────────────────────────────────────────────────────────────

ThemePicker::ThemePicker(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("ThemePicker");
    setStyleSheet(QStringLiteral("QFrame#ThemePicker { background: transparent; border: none; }"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_modeBtn = new QPushButton(this);
    m_modeBtn->setObjectName("IconButton");  // picks up existing topbar QSS
    m_modeBtn->setFixedSize(28, 24);
    m_modeBtn->setCursor(Qt::PointingHandCursor);
    m_modeBtn->setIconSize(QSize(16, 16));
    refreshModeButtonIcon();
    connect(m_modeBtn, &QPushButton::clicked, this, &ThemePicker::onModeButtonClicked);
    layout->addWidget(m_modeBtn, 0, Qt::AlignVCenter);
}

void ThemePicker::refreshModeButtonIcon()
{
    if (!m_modeBtn) return;
    const Theme::Mode cur = Theme::loadMode();

    // Sun icon for all 5 modes (Dark/Nord/Solarized/Gruvbox/Catppuccin) —
    // moon swap was used when Light was a Mode; Light is currently removed
    // pending the planned full-coverage re-add (per Theme.h history note).
    // When Light returns, restore the sun/moon swap predicate here. Tint with
    // active theme's primary text color so the icon contrasts against the
    // topbar surface in any mode.
    const QString iconPath = QStringLiteral(":/icons/sun.svg");
    const QColor tint = QColor(Theme::current().text);
    m_modeBtn->setIcon(Theme::tintedSvgIcon(iconPath, tint));

    // Tooltip — Tankoban-Max convention "Current — click for Next" (per
    // shell_bindings.js:62 themeToggleBtn.title formula).
    QString curLabel = QStringLiteral("Dark");
    QString nextLabel = QStringLiteral("Nord");
    int curIdx = 0;
    for (size_t i = 0; i < Theme::kModes.size(); ++i) {
        if (Theme::kModes[i].id == cur) {
            curLabel = QString::fromLatin1(Theme::kModes[i].label);
            curIdx = static_cast<int>(i);
            break;
        }
    }
    const int nextIdx = (curIdx + 1) % static_cast<int>(Theme::kModes.size());
    nextLabel = QString::fromLatin1(Theme::kModes[nextIdx].label);
    m_modeBtn->setToolTip(QStringLiteral("%1 — click for %2").arg(curLabel, nextLabel));
}

void ThemePicker::onModeButtonClicked()
{
    // Cycle forward through Theme::kModes — Tankoban-Max-style. Mirrors the
    // applyAppTheme cycle at shell_bindings.js:65-69. Direct apply on click;
    // no popover. Persistence happens via Theme::saveMode.
    const Theme::Mode cur = Theme::loadMode();
    int curIdx = 0;
    for (size_t i = 0; i < Theme::kModes.size(); ++i) {
        if (Theme::kModes[i].id == cur) { curIdx = static_cast<int>(i); break; }
    }
    const int nextIdx = (curIdx + 1) % static_cast<int>(Theme::kModes.size());
    const Theme::Mode next = Theme::kModes[nextIdx].id;

    Theme::saveMode(next);
    if (auto* app = qobject_cast<QApplication*>(QApplication::instance())) {
        Theme::applyTheme(*app, next);
    }
    refreshModeButtonIcon();
}
