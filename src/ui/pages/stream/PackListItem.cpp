// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task C2) - PackListItem implementation.

#include "ui/pages/stream/PackListItem.h"

#include "core/stream/PackClassifier.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace tankoban::stream::theatre {

PackListItem::PackListItem(const EnrichedPack& pack, QWidget* parent)
    : QFrame(parent), m_pack(pack) {
    setObjectName(QStringLiteral("PackListItem"));
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(82);
    buildUI();
}

void PackListItem::buildUI() {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(12, 10, 12, 10);
    col->setSpacing(4);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("PackListItemTitle"));
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setToolTip(m_pack.raw.title);
    m_titleLabel->setStyleSheet(
        "font-size: 13px; font-weight: 600; color: #f3f4f6;");
    col->addWidget(m_titleLabel);
    relayoutTitle();  // initial elide; will re-elide on every resize

    auto* chipRowWrapper = new QWidget(this);
    chipRowWrapper->setObjectName(QStringLiteral("PackListItemChipRow"));
    m_chipRow = new QHBoxLayout(chipRowWrapper);
    m_chipRow->setContentsMargins(0, 0, 0, 0);
    m_chipRow->setSpacing(6);

    // Pack-type chip.
    m_chipRow->addWidget(makeChip(labelForType(m_pack.classification.type),
                                   QStringLiteral("PackListItemTypeChip")));

    // Source chip.
    m_chipRow->addWidget(makeChip(m_pack.sourceLabel,
                                   QStringLiteral("PackListItemSourceChip")));

    m_chipRow->addStretch();
    col->addWidget(chipRowWrapper);

    m_metaLabel = new QLabel(this);
    m_metaLabel->setObjectName(QStringLiteral("PackListItemMeta"));
    // ASCII-only separator " - " (NOT U+00B7 middle-dot from plan literal).
    const QString sizeStr = m_pack.raw.sizeBytes > 0
        ? QStringLiteral("%1 GB").arg(m_pack.raw.sizeBytes / 1000000000.0, 0, 'f', 1)
        : QStringLiteral("size unknown");
    m_metaLabel->setText(QStringLiteral("%1 seeders - %2 - score %3")
                            .arg(m_pack.raw.seeders)
                            .arg(sizeStr)
                            .arg(static_cast<int>(m_pack.combinedScore)));
    m_metaLabel->setStyleSheet(
        "font-size: 11px; color: rgba(255,255,255,0.48);");
    col->addWidget(m_metaLabel);

    setStyleSheet(
        "PackListItem { background: transparent; border-top: 1px solid rgba(255,255,255,0.06); }"
        "PackListItem:hover { background: rgba(255,255,255,0.04); }");
}

QLabel* PackListItem::makeChip(const QString& text, const QString& objectName) {
    auto* chip = new QLabel(text, this);
    chip->setObjectName(objectName);
    // Style applied directly on the chip widget; scoping is implicit per-instance
    // so we don't need an #ObjectName selector. Both PackListItemTypeChip and
    // PackListItemSourceChip render identically (I4 review fix 2026-05-16).
    chip->setStyleSheet(
        "QLabel {"
        "  background: rgba(255,255,255,0.06);"
        "  border: 1px solid rgba(255,255,255,0.14);"
        "  border-radius: 4px;"
        "  padding: 2px 6px;"
        "  font-size: 10px; font-weight: 600;"
        "  color: rgba(255,255,255,0.78); }");
    return chip;
}

void PackListItem::relayoutTitle() {
    if (!m_titleLabel) return;
    QFontMetrics fm(m_titleLabel->font());
    const int availableWidth = std::max(80, width() - 24);
    m_titleLabel->setText(fm.elidedText(m_pack.raw.title,
                                         Qt::ElideRight,
                                         availableWidth));
}

void PackListItem::setSelected(bool selected) {
    if (m_selected == selected) return;
    m_selected = selected;
    setStyleSheet(
        QStringLiteral("PackListItem { background: %1; border-top: 1px solid rgba(255,255,255,0.06); }"
                       "PackListItem:hover { background: rgba(255,255,255,0.06); }")
            .arg(selected ? "rgba(255,255,255,0.08)" : "transparent"));
}

void PackListItem::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton)
        emit clicked();
    QFrame::mousePressEvent(event);
}

void PackListItem::resizeEvent(QResizeEvent* event) {
    QFrame::resizeEvent(event);
    relayoutTitle();
}

}  // namespace tankoban::stream::theatre
