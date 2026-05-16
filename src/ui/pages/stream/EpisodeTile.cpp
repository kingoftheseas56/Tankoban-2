#include "ui/pages/stream/EpisodeTile.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>

namespace tankoban::stream::theatre {

EpisodeTile::EpisodeTile(const EpisodeTileData& data, QWidget* parent)
    : QFrame(parent), m_data(data) {
    setObjectName(QStringLiteral("EpisodeTile"));
    setMinimumHeight(36);
    buildUI();
}

void EpisodeTile::buildUI() {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(8);

    m_checkBox = new QCheckBox(this);
    // Default check: true unless the episode is already-have (smart skip).
    m_checkBox->setChecked(!m_data.alreadyHave);
    connect(m_checkBox, &QCheckBox::toggled, this, &EpisodeTile::toggled);
    row->addWidget(m_checkBox);

    m_seLabel = new QLabel(
        QStringLiteral("S%1E%2")
            .arg(m_data.season, 2, 10, QLatin1Char('0'))
            .arg(m_data.episode, 2, 10, QLatin1Char('0')),
        this);
    m_seLabel->setFixedWidth(56);
    m_seLabel->setStyleSheet("font-size: 11px; font-weight: 600; color: rgba(255,255,255,0.78);");
    row->addWidget(m_seLabel);

    m_titleLabel = new QLabel(m_data.title, this);
    m_titleLabel->setStyleSheet("font-size: 11px; color: #e0e0e0;");
    row->addWidget(m_titleLabel, /*stretch=*/1);

    m_sizeLabel = new QLabel(this);
    if (m_data.sizeBytes > 0) {
        const double mb = m_data.sizeBytes / 1'000'000.0;
        m_sizeLabel->setText(QStringLiteral("%1 MB").arg(mb, 0, 'f', 0));
    } else {
        m_sizeLabel->setText(QStringLiteral("—"));
    }
    m_sizeLabel->setStyleSheet("font-size: 10px; color: rgba(255,255,255,0.48);");
    row->addWidget(m_sizeLabel);

    m_haveBadge = new QLabel(tr("Have"), this);
    m_haveBadge->setObjectName(QStringLiteral("EpisodeTileHaveBadge"));
    m_haveBadge->setStyleSheet(
        "QLabel#EpisodeTileHaveBadge {"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.18);"
        "  border-radius: 2px;"
        "  padding: 1px 5px;"
        "  font-size: 9px; font-weight: 700;"
        "  color: rgba(255,255,255,0.62); }");
    m_haveBadge->setVisible(m_data.alreadyHave);
    row->addWidget(m_haveBadge);

    setStyleSheet(
        "EpisodeTile { background: transparent; border-bottom: 1px solid rgba(255,255,255,0.04); }"
        "EpisodeTile:hover { background: rgba(255,255,255,0.03); }");
}

bool EpisodeTile::isChecked() const { return m_checkBox && m_checkBox->isChecked(); }

void EpisodeTile::setChecked(bool checked) {
    if (m_checkBox) m_checkBox->setChecked(checked);
}

}  // namespace tankoban::stream::theatre
