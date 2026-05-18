// src/ui/pages/comics/ComicsSourcesPanel.cpp
#include "ComicsSourcesPanel.h"

#include "ComicsSourceCard.h"
#include "core/manga/PremiumCatalog.h"
#include "core/manga/PremiumCatalogSchema.h"

#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <Qt>

#include <algorithm>

namespace tankoban::manga::comics {

namespace {

QString wcSubtitle(const QStringList& chapterIds)
{
    if (chapterIds.isEmpty()) {
        return QStringLiteral("chapters unavailable");
    }
    return QStringLiteral("%1 chapters - vol pack on demand").arg(chapterIds.size());
}

} // namespace

ComicsSourcesPanel::ComicsSourcesPanel(premium::PremiumCatalog* catalog,
                                       NyaaRuntimeSource*       nyaa,
                                       QWidget*                 parent)
    : QWidget(parent)
    , m_catalog(catalog)
    , m_nyaa(nyaa)
{
    setObjectName(QStringLiteral("ComicsSourcesPanel"));
    setMinimumWidth(220);
    setAttribute(Qt::WA_StyledBackground, true);

    setStyleSheet(QStringLiteral(
        "#ComicsSourcesPanel { background: transparent; }"
        "#ComicsSourcesPanelHeader { color: rgba(255,255,255,0.82);"
        " font-size: 12px; font-weight: 600; background: transparent; }"
        "#ComicsSourcesPanelScroll { background: transparent; border: none; }"
        "#ComicsSourcesPanelScroll > QWidget > QWidget { background: transparent; }"
        "#ComicsSourcesStatus { color: rgba(255,255,255,0.68);"
        " font-size: 12px; font-weight: 600; background: transparent; }"
        "#ComicsSourcesStatusSub { color: rgba(255,255,255,0.48);"
        " font-size: 11px; background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 4px 0; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,0.15);"
        " border-radius: 4px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.25); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    m_headerLabel = new QLabel(tr("Sources"), this);
    m_headerLabel->setObjectName(QStringLiteral("ComicsSourcesPanelHeader"));
    root->addWidget(m_headerLabel);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("ComicsSourcesPanelScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_cardsContainer = new QWidget(m_scroll);
    m_cardsContainer->setObjectName(QStringLiteral("ComicsSourcesCardsContainer"));
    m_cardsLayout = new QVBoxLayout(m_cardsContainer);
    m_cardsLayout->setContentsMargins(2, 2, 8, 2);
    m_cardsLayout->setSpacing(8);
    m_cardsLayout->addStretch(1);
    m_scroll->setWidget(m_cardsContainer);

    root->addWidget(m_scroll, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("ComicsSourcesStatus"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel, 0, Qt::AlignCenter);

    m_statusSubLabel = new QLabel(this);
    m_statusSubLabel->setObjectName(QStringLiteral("ComicsSourcesStatusSub"));
    m_statusSubLabel->setAlignment(Qt::AlignCenter);
    m_statusSubLabel->setWordWrap(true);
    root->addWidget(m_statusSubLabel, 0, Qt::AlignCenter);

    m_autoPickTimer = new QTimer(this);
    m_autoPickTimer->setSingleShot(true);
    connect(m_autoPickTimer, &QTimer::timeout,
            this, &ComicsSourcesPanel::emitTopRowDownload);

    if (m_nyaa) {
        connect(m_nyaa, &NyaaRuntimeSource::searchSucceeded,
                this,    &ComicsSourcesPanel::onNyaaResults,
                Qt::QueuedConnection);
        connect(m_nyaa, &NyaaRuntimeSource::searchFailed,
                this,    &ComicsSourcesPanel::onNyaaFailed,
                Qt::QueuedConnection);
    }

    setPlaceholder();
}

void ComicsSourcesPanel::clear()
{
    cancelAutoPick();
    m_autoPickSuppressed = false;
    m_rows.clear();
    m_pendingNyaaReqId = -1;
    m_currentSeriesTitle.clear();
    m_currentAnilistId = 0;
    m_currentVolNumber = 0;
    m_currentChapterIds.clear();
    if (m_headerLabel) {
        m_headerLabel->setText(tr("Sources"));
    }
    setPlaceholder();
}

void ComicsSourcesPanel::populate(const QString& seriesTitle,
                                  int            anilistSeriesId,
                                  const anilist::VolumeRow& vol,
                                  const QStringList& chapterIds)
{
    cancelAutoPick();
    m_autoPickSuppressed = false;
    m_rows.clear();

    m_currentSeriesTitle = seriesTitle;
    m_currentAnilistId = anilistSeriesId;
    m_currentVolNumber = vol.volumeNumber;
    m_currentChapterIds = chapterIds;

    if (m_headerLabel) {
        m_headerLabel->setText(vol.volumeNumber > 0
            ? tr("Volume %1").arg(vol.volumeNumber)
            : tr("Sources"));
    }

    if (vol.volumeNumber <= 0) {
        m_pendingNyaaReqId = -1;
        setPlaceholder();
        return;
    }

    if (m_catalog && anilistSeriesId > 0) {
        const auto entryOpt = m_catalog->entryForAnilistIdAndVolume(
            anilistSeriesId, vol.volumeNumber);
        if (entryOpt.has_value()) {
            const auto& entry = entryOpt->first;
            const auto& volEntry = entryOpt->second;

            UnifiedSourceRow row;
            row.kind = UnifiedSourceRow::Kind::Catalog;
            row.tier = 1;
            row.title = volEntry.cbzFileName.isEmpty()
                ? QStringLiteral("%1 - Volume %2")
                      .arg(entry.releaseEdition.isEmpty() ? entry.title : entry.releaseEdition)
                      .arg(volEntry.vol)
                : volEntry.cbzFileName;
            row.uploaderHint = entry.trustedUploader.isEmpty()
                ? entry.releaseEdition
                : entry.trustedUploader;
            row.seeders = 0;
            row.sizeBytes = volEntry.fileSizeBytes;
            row.magnetUri = entry.magnetUri;
            row.infoHash = entry.expectedInfoHash;
            appendRow(row);
        }
    }

    if (!chapterIds.isEmpty()) {
        UnifiedSourceRow wcRow;
        wcRow.kind = UnifiedSourceRow::Kind::WeebCentralPacker;
        wcRow.tier = 99;
        wcRow.title = QStringLiteral("WeebCentral pack");
        wcRow.uploaderHint = wcSubtitle(chapterIds);
        wcRow.seeders = -1;
        wcRow.sizeBytes = 0;
        appendRow(wcRow);
    }

    const bool nyaaInFlight = (m_nyaa != nullptr);
    if (m_nyaa) {
        m_pendingNyaaReqId = m_nextNyaaReqId++;
        m_nyaa->search(seriesTitle, vol.volumeNumber, m_pendingNyaaReqId);
    } else {
        m_pendingNyaaReqId = -1;
    }

    if (m_rows.isEmpty()) {
        if (nyaaInFlight) {
            setLoading();
        } else {
            setEmpty();
        }
    } else {
        setSources(m_rows, nyaaInFlight);
    }
}

void ComicsSourcesPanel::onNyaaResults(int reqId,
                                       const QList<NyaaSourceCandidate>& results)
{
    if (reqId != m_pendingNyaaReqId) {
        return;
    }
    m_pendingNyaaReqId = -1;

    for (const auto& cand : results) {
        UnifiedSourceRow row;
        row.kind = UnifiedSourceRow::Kind::NyaaRuntime;
        row.tier = cand.tier > 0 ? cand.tier : 2;
        row.title = cand.title;
        row.uploaderHint = cand.uploader;
        row.seeders = cand.seeders;
        row.sizeBytes = cand.sizeBytes;
        row.magnetUri = cand.magnetUri;
        row.infoHash = cand.infoHash;
        appendRow(row);
    }

    if (m_rows.isEmpty()) {
        setEmpty();
    } else {
        setSources(m_rows, /*nyaaStillInFlight=*/false);
    }
}

void ComicsSourcesPanel::onNyaaFailed(int reqId, const QString& reason)
{
    if (reqId != m_pendingNyaaReqId) {
        return;
    }
    m_pendingNyaaReqId = -1;

    qWarning("ComicsSourcesPanel: nyaa search failed (reqId=%d) reason=%s",
             reqId, reason.toUtf8().constData());

    if (m_rows.isEmpty()) {
        setEmpty();
    } else {
        setSources(m_rows, /*nyaaStillInFlight=*/false);
    }
}

void ComicsSourcesPanel::appendRow(const UnifiedSourceRow& row)
{
    m_rows.append(row);
}

void ComicsSourcesPanel::clearCards()
{
    if (!m_cardsLayout) return;
    for (ComicsSourceCard* card : m_cards) {
        m_cardsLayout->removeWidget(card);
        card->deleteLater();
    }
    m_cards.clear();
}

void ComicsSourcesPanel::setPlaceholder()
{
    clearCards();
    if (m_statusLabel) {
        m_statusLabel->setText(tr("Select a volume to see sources"));
        m_statusLabel->show();
    }
    if (m_statusSubLabel) {
        m_statusSubLabel->clear();
        m_statusSubLabel->hide();
    }
    if (m_scroll) {
        m_scroll->hide();
    }
}

void ComicsSourcesPanel::setLoading()
{
    clearCards();
    if (m_statusLabel) {
        m_statusLabel->clear();
        m_statusLabel->hide();
    }
    if (m_statusSubLabel) {
        m_statusSubLabel->clear();
        m_statusSubLabel->hide();
    }
    if (m_scroll) {
        m_scroll->show();
    }
    for (int i = 0; i < 2; ++i) {
        auto* skeleton = new ComicsSourceCard(true, m_cardsContainer);
        const int insertPos = qMax(0, m_cardsLayout->count() - 1);
        m_cardsLayout->insertWidget(insertPos, skeleton);
        m_cards.push_back(skeleton);
    }
}

void ComicsSourcesPanel::setSources(const QList<UnifiedSourceRow>& rows,
                                    bool nyaaStillInFlight)
{
    m_rows = rows;
    sortRows();
    clearCards();

    if (m_rows.isEmpty()) {
        if (nyaaStillInFlight) {
            setLoading();
        } else {
            setEmpty();
        }
        return;
    }

    if (m_statusLabel) {
        m_statusLabel->clear();
        m_statusLabel->hide();
    }
    if (m_statusSubLabel) {
        m_statusSubLabel->clear();
        m_statusSubLabel->hide();
    }
    if (m_scroll) {
        m_scroll->show();
    }

    for (const UnifiedSourceRow& row : std::as_const(m_rows)) {
        auto* card = new ComicsSourceCard(row, m_cardsContainer);
        connect(card, &ComicsSourceCard::clicked,
                this, [this](const UnifiedSourceRow& clickedRow) {
            m_autoPickSuppressed = true;
            cancelAutoPick();
            emitRowDownload(clickedRow);
        });
        const int insertPos = qMax(0, m_cardsLayout->count() - 1);
        m_cardsLayout->insertWidget(insertPos, card);
        m_cards.push_back(card);
    }

    if (nyaaStillInFlight) {
        for (int i = 0; i < 2; ++i) {
            auto* skeleton = new ComicsSourceCard(true, m_cardsContainer);
            const int insertPos = qMax(0, m_cardsLayout->count() - 1);
            m_cardsLayout->insertWidget(insertPos, skeleton);
            m_cards.push_back(skeleton);
        }
    }

    if (m_scroll && m_scroll->verticalScrollBar()) {
        m_scroll->verticalScrollBar()->setValue(0);
    }
    armAutoPickIfEligible();
}

void ComicsSourcesPanel::setEmpty()
{
    clearCards();
    if (m_statusLabel) {
        m_statusLabel->setText(tr("No sources found for this volume"));
        m_statusLabel->show();
    }
    if (m_statusSubLabel) {
        m_statusSubLabel->setText(tr("Try a different volume or check back as indexers refresh."));
        m_statusSubLabel->show();
    }
    if (m_scroll) {
        m_scroll->hide();
    }
}

void ComicsSourcesPanel::sortRows()
{
    std::stable_sort(m_rows.begin(), m_rows.end(),
                     [](const UnifiedSourceRow& a, const UnifiedSourceRow& b) {
                         if (a.tier != b.tier) return a.tier < b.tier;
                         return a.seeders > b.seeders;
                     });
}

void ComicsSourcesPanel::armAutoPickIfEligible()
{
    if (m_autoPickSuppressed || m_autoPickArmed || m_rows.isEmpty() || m_cards.isEmpty()) {
        return;
    }

    const UnifiedSourceRow& row = m_rows.first();
    if (row.kind != UnifiedSourceRow::Kind::Catalog
        || row.tier != 1
        || row.magnetUri.isEmpty()) {
        return;
    }

    m_autoPickArmed = true;
    if (auto* firstCard = m_cards.first()) {
        if (!firstCard->isSkeleton()) {
            firstCard->setSelected(true);
        }
    }
    if (m_autoPickTimer) {
        m_autoPickTimer->start(300);
    }
}

void ComicsSourcesPanel::cancelAutoPick()
{
    if (m_autoPickTimer) {
        m_autoPickTimer->stop();
    }
    m_autoPickArmed = false;
}

void ComicsSourcesPanel::emitTopRowDownload()
{
    if (!m_autoPickArmed || m_rows.isEmpty()) {
        return;
    }

    const UnifiedSourceRow row = m_rows.first();
    cancelAutoPick();
    if (row.kind != UnifiedSourceRow::Kind::Catalog
        || row.tier != 1
        || row.magnetUri.isEmpty()) {
        return;
    }
    emitRowDownload(row);
}

void ComicsSourcesPanel::emitRowDownload(const UnifiedSourceRow& row)
{
    emit downloadRequested(row,
                           m_currentSeriesTitle,
                           m_currentAnilistId,
                           m_currentVolNumber,
                           m_currentChapterIds);
}

} // namespace tankoban::manga::comics
