// src/ui/widgets/CenterSearchBar.cpp
//
// HARBOR_REDESIGN Phase 1 Task 5 (2026-06-15, Agent 0). See CenterSearchBar.h.
// Widget body produced via the DeepSeek grunt engine off the Max pool (HOT
// quota day); reviewed + integrated by Agent 0. Glyphs use QChar(code) so the
// source stays pure-ASCII (the project sets no /utf-8 compile flag).

#include "ui/widgets/CenterSearchBar.h"

#include "ui/Theme.h"

#include <QChar>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QToolButton>

namespace tankoban::ui {

CenterSearchBar::CenterSearchBar(QWidget* parent)
    : QFrame(parent) {
    setObjectName(QStringLiteral("CenterSearch"));
    setFixedHeight(30);
    setMinimumWidth(280);
    setMaximumWidth(520);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 2, 8, 2);
    layout->setSpacing(6);

    // Search icon — tinted to the active theme text color (mirror NavRail).
    auto* iconLabel = new QLabel(this);
    iconLabel->setObjectName(QStringLiteral("SearchIcon"));
    const auto& palette = Theme::current();
    iconLabel->setPixmap(
        Theme::tintedSvgIcon(QStringLiteral(":/icons/search.svg"),
                             QColor(palette.text), 16)
            .pixmap(16, 16));
    layout->addWidget(iconLabel);

    // Search edit (stretch) — frameless; styling via QSS objectName.
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("CenterSearchEdit"));
    m_searchEdit->setFrame(false);
    layout->addWidget(m_searchEdit, 1);

    // Clear button — hidden until the edit has text. QChar(0x2715) = ✕.
    m_clearButton = new QToolButton(this);
    m_clearButton->setObjectName(QStringLiteral("SearchClear"));
    m_clearButton->setText(QString(QChar(0x2715)));
    m_clearButton->setCursor(Qt::PointingHandCursor);
    m_clearButton->setFocusPolicy(Qt::NoFocus);
    m_clearButton->setAutoRaise(true);  // QToolButton's flat/borderless look (QSS does the rest)
    m_clearButton->hide();
    layout->addWidget(m_clearButton);

    // "/" hotkey hint chip — shown only when the edit is empty.
    m_hintLabel = new QLabel(QStringLiteral("/"), this);
    m_hintLabel->setObjectName(QStringLiteral("SearchHint"));
    layout->addWidget(m_hintLabel);

    // --- Connections ---
    connect(m_searchEdit, &QLineEdit::textChanged, this,
            [this](const QString& text) {
                const bool hasText = !text.isEmpty();
                m_clearButton->setVisible(hasText);
                m_hintLabel->setVisible(!hasText);
            });

    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        const QString trimmed = m_searchEdit->text().trimmed();
        if (!trimmed.isEmpty())
            emit searchSubmitted(trimmed);
    });

    connect(m_clearButton, &QToolButton::clicked, this, [this]() {
        m_searchEdit->clear();
        emit searchCleared();
        m_searchEdit->setFocus();
    });
}

void CenterSearchBar::setPlaceholder(const QString& modeLabel) {
    m_placeholderBase = modeLabel;
    // QChar(0x2026) = … (horizontal ellipsis); appended so source stays ASCII.
    m_searchEdit->setPlaceholderText(
        QStringLiteral("Search %1").arg(modeLabel) + QChar(0x2026));
}

void CenterSearchBar::focusSearch() {
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

QString CenterSearchBar::text() const {
    return m_searchEdit->text().trimmed();
}

void CenterSearchBar::clearSearch() {
    m_searchEdit->clear();
    m_clearButton->hide();
}

QString CenterSearchBar::devSnapshot() const {
    auto escape = [](const QString& s) -> QString {
        QString out;
        out.reserve(s.size());
        for (const QChar ch : s) {
            if (ch == QLatin1Char('\\'))
                out.append(QStringLiteral("\\\\"));
            else if (ch == QLatin1Char('"'))
                out.append(QStringLiteral("\\\""));
            else
                out.append(ch);
        }
        return out;
    };

    const QString escText = escape(m_searchEdit->text());
    const QString escPlaceholder = escape(m_placeholderBase);
    const QString hasFocus = m_searchEdit->hasFocus()
                                 ? QStringLiteral("true")
                                 : QStringLiteral("false");

    return QStringLiteral(
               "{\"text\":\"%1\",\"placeholder\":\"%2\",\"hasFocus\":%3}")
        .arg(escText, escPlaceholder, hasFocus);
}

} // namespace tankoban::ui
