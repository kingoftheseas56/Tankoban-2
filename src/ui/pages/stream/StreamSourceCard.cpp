#include "StreamSourceCard.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace tankostream::stream {

namespace {

QString buildCardStyleSheet(bool hovered, bool selected)
{
    // Base card + state overlays. Object-name-scoped so the card's child
    // labels don't inherit background styling they shouldn't.
    const QString base = QStringLiteral(
        "#StreamSourceCard { background: rgba(255,255,255,0.04);"
        " border: 1px solid rgba(255,255,255,0.08);"
        " border-radius: 8px; }"
        "#StreamSourceCard QLabel { background: transparent; }"
        "#StreamSourceCardBadge { background: rgba(255,255,255,0.06);"
        " border-radius: 6px; color: #9ca3af;"
        " font-size: 13px; font-weight: 600; }"
        "#StreamSourceCardAddon { color: #e5e7eb; font-size: 13px; font-weight: 600; }"
        "#StreamSourceCardFilename { color: #d1d5db; font-size: 11px; }"
        "#StreamSourceCardChip { color: #9ca3af; font-size: 11px; }"
        "#StreamSourceCardQuality { background: rgba(255,255,255,0.10);"
        " border-radius: 4px; color: #d1d5db;"
        " font-size: 11px; font-weight: 600; padding: 2px 8px; }"
        "#StreamSourceCardBadgeLabel { background: rgba(255,255,255,0.08);"
        " border-radius: 3px; color: #d1d5db;"
        " font-size: 10px; font-weight: 600; padding: 1px 5px; }"
        "#StreamSourceCardSource { color: #6b7280; font-size: 10px; }");

    if (selected) {
        return base + QStringLiteral(
            "#StreamSourceCard { background: rgba(255,255,255,0.12);"
            " border-color: rgba(255,255,255,0.22); }");
    }
    if (hovered) {
        return base + QStringLiteral(
            "#StreamSourceCard { background: rgba(255,255,255,0.08);"
            " border-color: rgba(255,255,255,0.14); }");
    }
    return base;
}

}

StreamSourceCard::StreamSourceCard(const StreamPickerChoice& choice, QWidget* parent)
    : QFrame(parent)
    , m_choice(choice)
{
    setObjectName(QStringLiteral("StreamSourceCard"));
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(76);

    buildUI();
    applyStateStyle();
}

void StreamSourceCard::buildUI()
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(12);

    // ── Badge (addon initials) ────────────────────────────────────────────
    auto* badge = new QLabel(addonInitials(m_choice.displayTitle), this);
    badge->setObjectName(QStringLiteral("StreamSourceCardBadge"));
    badge->setFixedSize(36, 36);
    badge->setAlignment(Qt::AlignCenter);
    root->addWidget(badge);

    // ── Text column ───────────────────────────────────────────────────────
    auto* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(3);

    // Top row: addon name (left, bold) + quality pill (right).
    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(6);

    auto* addonLabel = new QLabel(m_choice.displayTitle, this);
    addonLabel->setObjectName(QStringLiteral("StreamSourceCardAddon"));
    topRow->addWidget(addonLabel);
    topRow->addStretch();

    if (!m_choice.displayQuality.isEmpty()
     && m_choice.displayQuality != QLatin1String("-")) {
        auto* quality = new QLabel(m_choice.displayQuality, this);
        quality->setObjectName(QStringLiteral("StreamSourceCardQuality"));
        quality->setAlignment(Qt::AlignCenter);
        topRow->addWidget(quality);
    }

    textCol->addLayout(topRow);

    // Middle line: filename (elided by Qt automatically via the label width).
    auto* filename = new QLabel(m_choice.displayFilename, this);
    filename->setObjectName(QStringLiteral("StreamSourceCardFilename"));
    filename->setTextInteractionFlags(Qt::NoTextInteraction);
    filename->setWordWrap(false);
    // Eliding requires a fixed-size label or custom paintEvent. Using
    // QLabel::setText with an elided string at layout time — recomputed on
    // resize via resizeEvent would be ideal, but for the initial drop the
    // filename mostly fits in the 260-320px typical pane width. Leaving
    // wordWrap=false + a tooltip with the full filename so truncated rows
    // are still recoverable on hover.
    filename->setToolTip(m_choice.displayFilename);
    textCol->addWidget(filename);

    // Bottom chip row.
    auto* chipRow = new QHBoxLayout();
    chipRow->setContentsMargins(0, 0, 0, 0);
    chipRow->setSpacing(10);

    if (m_choice.seeders >= 0) {
        // U+2191 upward arrow acts as a peer-count glyph; monochrome,
        // no-emoji-palette-compliant.
        auto* peers = new QLabel(
            QStringLiteral("\u2191 %1").arg(m_choice.seeders), this);
        peers->setObjectName(QStringLiteral("StreamSourceCardChip"));
        peers->setToolTip(QStringLiteral("Seeders"));
        chipRow->addWidget(peers);
    }

    if (m_choice.sizeBytes > 0) {
        auto* size = new QLabel(
            QStringLiteral("\u2022 %1").arg(humanSize(m_choice.sizeBytes)), this);
        size->setObjectName(QStringLiteral("StreamSourceCardChip"));
        size->setToolTip(QStringLiteral("File size"));
        chipRow->addWidget(size);
    }

    for (const QString& badgeText : m_choice.badges) {
        auto* b = new QLabel(badgeText, this);
        b->setObjectName(QStringLiteral("StreamSourceCardBadgeLabel"));
        chipRow->addWidget(b);
    }

    chipRow->addStretch();

    if (!m_choice.trackerSource.isEmpty()) {
        auto* trackerSource = new QLabel(m_choice.trackerSource, this);
        trackerSource->setObjectName(QStringLiteral("StreamSourceCardSource"));
        trackerSource->setToolTip(QStringLiteral("Tracker source"));
        chipRow->addWidget(trackerSource);
    }

    textCol->addLayout(chipRow);

    root->addLayout(textCol, 1);
}

void StreamSourceCard::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    applyStateStyle();
}

void StreamSourceCard::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    applyStateStyle();
    QFrame::enterEvent(event);
}

void StreamSourceCard::leaveEvent(QEvent* event)
{
    m_hovered = false;
    applyStateStyle();
    QFrame::leaveEvent(event);
}

void StreamSourceCard::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        emit clicked(m_choice);
    }
    QFrame::mouseReleaseEvent(event);
}

void StreamSourceCard::applyStateStyle()
{
    setStyleSheet(buildCardStyleSheet(m_hovered, m_selected));
}

QString StreamSourceCard::addonInitials(const QString& addonName)
{
    const QString trimmed = addonName.trimmed();
    if (trimmed.isEmpty()) return QStringLiteral("?");

    // Take up to two uppercase-worthy initials. "Torrentio" → "T".
    // "Open Subtitles" → "OS". "com.linvo.cinemeta" → "CI".
    QString cleaned = trimmed;
    // Strip common addon-id prefixes if they leak through.
    if (cleaned.startsWith(QStringLiteral("com."), Qt::CaseInsensitive)) {
        const int dot = cleaned.lastIndexOf('.');
        if (dot >= 0 && dot + 1 < cleaned.size())
            cleaned = cleaned.mid(dot + 1);
    }

    QStringList tokens = cleaned.split(QRegularExpression(QStringLiteral("[\\s\\-_.]+")),
                                        Qt::SkipEmptyParts);
    if (tokens.isEmpty()) return cleaned.left(1).toUpper();

    if (tokens.size() == 1) return tokens.first().left(1).toUpper();
    return (tokens.at(0).left(1) + tokens.at(1).left(1)).toUpper();
}

}
