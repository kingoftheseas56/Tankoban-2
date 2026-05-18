// src/ui/pages/comics/ComicsSourceCard.cpp
#include "ComicsSourceCard.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace tankoban::manga::comics {

namespace {

QString formatSizeChip(qint64 bytes)
{
    if (bytes <= 0) return QString();
    constexpr qint64 KiB = 1024;
    constexpr qint64 MiB = 1024 * KiB;
    constexpr qint64 GiB = 1024 * MiB;
    if (bytes >= GiB) {
        return QString::number(static_cast<double>(bytes) / GiB, 'f', 2) + QStringLiteral(" GiB");
    }
    if (bytes >= MiB) {
        return QString::number(static_cast<double>(bytes) / MiB, 'f', 1) + QStringLiteral(" MiB");
    }
    if (bytes >= KiB) {
        return QString::number(static_cast<double>(bytes) / KiB, 'f', 1) + QStringLiteral(" KiB");
    }
    return QString::number(bytes) + QStringLiteral(" B");
}

QString badgeObjectName(UnifiedSourceRow::Kind kind)
{
    switch (kind) {
    case UnifiedSourceRow::Kind::Catalog:
        return QStringLiteral("ComicsSourceCardBadgeCatalog");
    case UnifiedSourceRow::Kind::NyaaRuntime:
        return QStringLiteral("ComicsSourceCardBadgeNyaa");
    case UnifiedSourceRow::Kind::WeebCentralPacker:
        return QStringLiteral("ComicsSourceCardBadgeWC");
    }
    return QStringLiteral("ComicsSourceCardBadgeNyaa");
}

QString tierObjectName(UnifiedSourceRow::Kind kind)
{
    switch (kind) {
    case UnifiedSourceRow::Kind::Catalog:
        return QStringLiteral("ComicsSourceCardTierCatalog");
    case UnifiedSourceRow::Kind::NyaaRuntime:
        return QStringLiteral("ComicsSourceCardTierNyaa");
    case UnifiedSourceRow::Kind::WeebCentralPacker:
        return QStringLiteral("ComicsSourceCardTierWC");
    }
    return QStringLiteral("ComicsSourceCardTierNyaa");
}

QString buildCardStyleSheet(bool hovered, bool selected, bool skeleton,
                            UnifiedSourceRow::Kind kind)
{
    const QString base = QStringLiteral(
        "#ComicsSourceCard { background: rgba(255,255,255,0.04);"
        " border: 1px solid rgba(255,255,255,0.10);"
        " border-radius: 8px; }"
        "#ComicsSourceCard QLabel { background: transparent; }"
        "#ComicsSourceCardTitle { color: #e5e7eb; font-size: 13px; font-weight: 600; }"
        "#ComicsSourceCardSubtitle { color: rgba(255,255,255,0.55); font-size: 11px; }"
        "#ComicsSourceCardChip { color: rgba(255,255,255,0.55); font-size: 11px; }"
        "#ComicsSourceCardSkeletonBlock { background: rgba(255,255,255,0.08);"
        " border-radius: 4px; }"
        "#ComicsSourceCardBadgeCatalog { background: rgba(212,165,116,0.18);"
        " border: 1px solid rgba(212,165,116,0.40);"
        " border-radius: 6px; color: #d4a574;"
        " font-size: 11px; font-weight: 700; }"
        "#ComicsSourceCardBadgeNyaa { background: rgba(255,255,255,0.06);"
        " border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px; color: rgba(255,255,255,0.65);"
        " font-size: 11px; font-weight: 700; }"
        "#ComicsSourceCardBadgeWC { background: rgba(255,255,255,0.04);"
        " border: 1px solid rgba(255,255,255,0.10);"
        " border-radius: 6px; color: rgba(255,255,255,0.45);"
        " font-size: 11px; font-weight: 700; }"
        "#ComicsSourceCardTierCatalog { background: rgba(212,165,116,0.18);"
        " border: 1px solid rgba(212,165,116,0.40);"
        " border-radius: 4px; color: #d4a574;"
        " font-size: 11px; font-weight: 600; padding: 2px 8px; }"
        "#ComicsSourceCardTierNyaa { background: rgba(255,255,255,0.06);"
        " border: 1px solid rgba(255,255,255,0.16);"
        " border-radius: 4px; color: rgba(255,255,255,0.65);"
        " font-size: 11px; font-weight: 600; padding: 2px 8px; }"
        "#ComicsSourceCardTierWC { background: rgba(255,255,255,0.04);"
        " border: 1px solid rgba(255,255,255,0.10);"
        " border-radius: 4px; color: rgba(255,255,255,0.45);"
        " font-size: 11px; font-weight: 600; padding: 2px 8px; }");

    if (skeleton) {
        return base + QStringLiteral(
            "#ComicsSourceCard { background: rgba(255,255,255,0.03);"
            " border-color: rgba(255,255,255,0.06); }");
    }
    if (selected) {
        return base + QStringLiteral(
            "#ComicsSourceCard { background: rgba(255,255,255,0.08);"
            " border: 1px solid rgba(212,165,116,0.50); }");
    }
    if (hovered) {
        return base + QStringLiteral(
            "#ComicsSourceCard { background: rgba(255,255,255,0.06);"
            " border-color: rgba(255,255,255,0.14); }");
    }
    if (kind == UnifiedSourceRow::Kind::Catalog) {
        return base + QStringLiteral(
            "#ComicsSourceCard { background: rgba(255,255,255,0.06); }");
    }
    return base;
}

QLabel* makeSkeletonBlock(QWidget* parent, int minWidth, int height)
{
    auto* block = new QLabel(parent);
    block->setObjectName(QStringLiteral("ComicsSourceCardSkeletonBlock"));
    block->setMinimumWidth(minWidth);
    block->setFixedHeight(height);
    block->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return block;
}

} // namespace

ComicsSourceCard::ComicsSourceCard(const UnifiedSourceRow& row, QWidget* parent)
    : QFrame(parent)
    , m_row(row)
{
    setObjectName(QStringLiteral("ComicsSourceCard"));
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    buildUiRealRow();
    applyStateStyle();
}

ComicsSourceCard::ComicsSourceCard(bool skeleton, QWidget* parent)
    : QFrame(parent)
    , m_skeleton(skeleton)
{
    setObjectName(QStringLiteral("ComicsSourceCard"));
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumHeight(80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    buildUiSkeleton();
    applyStateStyle();
}

ComicsSourceCard::~ComicsSourceCard() = default;

void ComicsSourceCard::buildUiRealRow()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(6);

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(12);

    m_badgeLabel = new QLabel(badgeText(m_row), this);
    m_badgeLabel->setObjectName(badgeObjectName(m_row.kind));
    m_badgeLabel->setFixedSize(36, 36);
    m_badgeLabel->setAlignment(Qt::AlignCenter);
    topRow->addWidget(m_badgeLabel);

    auto* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(3);

    m_titleLabel = new QLabel(m_row.title, this);
    m_titleLabel->setObjectName(QStringLiteral("ComicsSourceCardTitle"));
    m_titleLabel->setToolTip(m_row.title);
    m_titleLabel->setWordWrap(false);
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    textCol->addWidget(m_titleLabel);

    const QString subtitle = subtitleText(m_row);
    m_subtitleLabel = new QLabel(subtitle, this);
    m_subtitleLabel->setObjectName(QStringLiteral("ComicsSourceCardSubtitle"));
    m_subtitleLabel->setToolTip(subtitle);
    m_subtitleLabel->setWordWrap(false);
    m_subtitleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    textCol->addWidget(m_subtitleLabel);

    topRow->addLayout(textCol, 1);

    m_tierPillLabel = new QLabel(tierPillText(m_row), this);
    m_tierPillLabel->setObjectName(tierObjectName(m_row.kind));
    m_tierPillLabel->setAlignment(Qt::AlignCenter);
    topRow->addWidget(m_tierPillLabel);

    root->addLayout(topRow);

    auto* chipRow = new QHBoxLayout();
    chipRow->setContentsMargins(48, 0, 0, 0);
    chipRow->setSpacing(12);

    auto addChip = [this, chipRow](const QString& text) {
        if (text.isEmpty()) return;
        auto* chip = new QLabel(text, this);
        chip->setObjectName(QStringLiteral("ComicsSourceCardChip"));
        chipRow->addWidget(chip);
    };

    if (m_row.seeders >= 0) {
        addChip(QStringLiteral("%1 seeders").arg(m_row.seeders));
    }
    addChip(formatSizeChip(m_row.sizeBytes));
    addChip(archiveChipText(m_row));
    if (!m_row.uploaderHint.isEmpty()
        && m_row.kind != UnifiedSourceRow::Kind::WeebCentralPacker) {
        addChip(m_row.uploaderHint);
    }
    chipRow->addStretch(1);
    root->addLayout(chipRow);

    reelideLabels();
}

void ComicsSourceCard::buildUiSkeleton()
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(12);

    auto* badgeBlock = makeSkeletonBlock(this, 36, 36);
    badgeBlock->setFixedWidth(36);
    root->addWidget(badgeBlock);

    auto* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(7);
    textCol->addWidget(makeSkeletonBlock(this, 160, 12));
    textCol->addWidget(makeSkeletonBlock(this, 220, 10));
    textCol->addWidget(makeSkeletonBlock(this, 120, 10));
    root->addLayout(textCol, 1);

    auto* effect = new QGraphicsOpacityEffect(this);
    effect->setOpacity(0.45);
    setGraphicsEffect(effect);

    auto* anim = new QPropertyAnimation(effect, "opacity", this);
    anim->setDuration(1500);
    anim->setStartValue(0.30);
    anim->setEndValue(0.70);
    anim->setEasingCurve(QEasingCurve::InOutSine);
    anim->setLoopCount(-1);
    anim->start();
}

void ComicsSourceCard::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    applyStateStyle();
}

void ComicsSourceCard::enterEvent(QEnterEvent* event)
{
    if (!m_skeleton) {
        m_hovered = true;
        applyStateStyle();
    }
    QFrame::enterEvent(event);
}

void ComicsSourceCard::leaveEvent(QEvent* event)
{
    if (!m_skeleton) {
        m_hovered = false;
        applyStateStyle();
    }
    QFrame::leaveEvent(event);
}

void ComicsSourceCard::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_skeleton
        && event->button() == Qt::LeftButton
        && rect().contains(event->pos())) {
        emit clicked(m_row);
    }
    QFrame::mouseReleaseEvent(event);
}

void ComicsSourceCard::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    reelideLabels();
}

void ComicsSourceCard::applyStateStyle()
{
    setStyleSheet(buildCardStyleSheet(m_hovered, m_selected, m_skeleton, m_row.kind));
}

void ComicsSourceCard::reelideLabels()
{
    auto reelide = [](QLabel* label, const QString& fullText) {
        if (!label) return;
        const int width = label->width();
        if (width <= 0) return;
        const QFontMetrics fm(label->font());
        label->setText(fm.elidedText(fullText, Qt::ElideRight, width));
    };
    reelide(m_titleLabel, m_row.title);
    reelide(m_subtitleLabel, subtitleText(m_row));
}

QString ComicsSourceCard::badgeText(const UnifiedSourceRow& row)
{
    switch (row.kind) {
    case UnifiedSourceRow::Kind::Catalog:
        return QStringLiteral("CT");
    case UnifiedSourceRow::Kind::NyaaRuntime:
        return QStringLiteral("NY");
    case UnifiedSourceRow::Kind::WeebCentralPacker:
        return QStringLiteral("WC");
    }
    return QStringLiteral("NY");
}

QString ComicsSourceCard::tierPillText(const UnifiedSourceRow& row)
{
    switch (row.kind) {
    case UnifiedSourceRow::Kind::Catalog:
        return QStringLiteral("CATALOG");
    case UnifiedSourceRow::Kind::NyaaRuntime:
        return QStringLiteral("NYAA");
    case UnifiedSourceRow::Kind::WeebCentralPacker:
        return QStringLiteral("FALLBACK");
    }
    return QStringLiteral("NYAA");
}

QString ComicsSourceCard::subtitleText(const UnifiedSourceRow& row)
{
    switch (row.kind) {
    case UnifiedSourceRow::Kind::Catalog:
        if (!row.uploaderHint.isEmpty()) {
            return row.uploaderHint + QStringLiteral(" - ") + row.title;
        }
        return row.title;
    case UnifiedSourceRow::Kind::NyaaRuntime:
        if (!row.uploaderHint.isEmpty()) {
            return row.uploaderHint;
        }
        return row.title;
    case UnifiedSourceRow::Kind::WeebCentralPacker:
        return row.uploaderHint.isEmpty()
            ? QStringLiteral("chapters unavailable")
            : row.uploaderHint;
    }
    return QString();
}

QString ComicsSourceCard::archiveChipText(const UnifiedSourceRow& row)
{
    const QString probe = (row.title + QLatin1Char(' ') + row.uploaderHint).toLower();
    static const QRegularExpression cbrRe(QStringLiteral("\\.cbr\\b|\\bcbr\\b"));
    static const QRegularExpression cbzRe(QStringLiteral("\\.cbz\\b|\\bcbz\\b"));
    if (probe.contains(cbrRe)) return QStringLiteral("cbr");
    if (probe.contains(cbzRe)) return QStringLiteral("cbz");
    if (row.kind == UnifiedSourceRow::Kind::Catalog
        || row.kind == UnifiedSourceRow::Kind::WeebCentralPacker) {
        return QStringLiteral("cbz");
    }
    return QString();
}

} // namespace tankoban::manga::comics
