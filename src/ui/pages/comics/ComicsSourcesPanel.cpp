// src/ui/pages/comics/ComicsSourcesPanel.cpp
#include "ComicsSourcesPanel.h"

#include "ComicsSourceCard.h"
#include "core/manga/PremiumCatalog.h"
#include "core/manga/PremiumCatalogSchema.h"

#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <Qt>

#include <algorithm>

namespace tankoban::manga::comics {

namespace {

QString sourceKindString(UnifiedSourceRow::Kind kind)
{
    switch (kind) {
    case UnifiedSourceRow::Kind::Catalog: return QStringLiteral("catalog");
    case UnifiedSourceRow::Kind::NyaaRuntime: return QStringLiteral("nyaa");
    case UnifiedSourceRow::Kind::WeebCentralPacker: return QStringLiteral("weebcentral");
    }
    return QStringLiteral("unknown");
}

QJsonObject sourceRowJson(const UnifiedSourceRow& row)
{
    QJsonObject obj;
    obj[QStringLiteral("kind")] = sourceKindString(row.kind);
    obj[QStringLiteral("tier")] = row.tier;
    obj[QStringLiteral("title")] = row.title;
    obj[QStringLiteral("uploaderHint")] = row.uploaderHint;
    obj[QStringLiteral("seeders")] = row.seeders;
    obj[QStringLiteral("sizeBytes")] = static_cast<double>(row.sizeBytes);
    obj[QStringLiteral("magnetUri")] = row.magnetUri;
    obj[QStringLiteral("infoHash")] = row.infoHash;
    QJsonArray wcChapterIds;
    for (const QString& chapterId : row.weebCentralChapterIds) {
        wcChapterIds.append(chapterId);
    }
    obj[QStringLiteral("weebCentralChapterIds")] = wcChapterIds;
    return obj;
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
        // STREAM_PORT Bug-2 fix 2026-05-18: drop font-weight 600 + soften
        // colour to match StreamSourceList::m_statusLabel inline style at
        // src/ui/pages/stream/StreamSourceList.cpp:133-134 (color: #9ca3af,
        // normal weight). Hemanth's verbatim: the prior bold + bright
        // status text "looks incredibly ugly... very different from
        // theatre/stream mode." Stream parity = muted gray, normal weight,
        // 12px.
        "#ComicsSourcesStatus { color: #9ca3af;"
        " font-size: 12px; background: transparent; }"
        "#ComicsSourcesStatusSub { color: rgba(255,255,255,0.40);"
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

    // STREAM_PORT Bug-2 fix 2026-05-18: port StreamSourceList's status-label
    // pattern exactly (StreamSourceList.cpp:96-101). Three precise changes:
    //   1) Add `setContentsMargins(8, 16, 8, 16)` (was none) -- the 16px top
    //      margin gives breathing room below the (now-hidden) scroll area
    //      so the status text doesn't collide with the header.
    //   2) Drop the `Qt::AlignCenter` flag from `addWidget(...)` -- Stream
    //      lets the layout position the label naturally. The label's own
    //      `setAlignment(Qt::AlignCenter)` centers the text inside the
    //      label's allocated cell, but the layout-cell flag was forcing an
    //      additional vertical-center treatment that fought the panel's
    //      Expanding size policy and created the "text floating mid-panel"
    //      ugliness Hemanth flagged.
    //   3) Same treatment for m_statusSubLabel.
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("ComicsSourcesStatus"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setContentsMargins(8, 16, 8, 16);
    root->addWidget(m_statusLabel);

    m_statusSubLabel = new QLabel(this);
    m_statusSubLabel->setObjectName(QStringLiteral("ComicsSourcesStatusSub"));
    m_statusSubLabel->setAlignment(Qt::AlignCenter);
    m_statusSubLabel->setWordWrap(true);
    m_statusSubLabel->setContentsMargins(8, 0, 8, 8);
    root->addWidget(m_statusSubLabel);

    // STREAM_PORT Bug-3 fix 2026-05-19: pin "Sources" header to top.
    // When the scroll area is hidden (placeholder / empty state) its
    // stretch-1 slot collapses to zero but the QScrollArea's default
    // Expanding size-policy can still cause Qt to distribute remaining
    // vertical space above the status labels, visually centering the
    // header+status block instead of anchoring the header at the top.
    // A trailing addStretch() consumes all leftover space after the
    // status labels, keeping the header pinned at position 0 in every
    // state.  The populated state is unaffected: m_scroll is shown with
    // stretch-1 and fills the space; this trailing stretch gets none.
    root->addStretch(1);

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
    setContext(0, QString());
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
        m_headerLabel->setText(
            vol.volumeNumber == tankoban::manga::anilist::kVolumeXNumber
                ? tr("Volume X")
                : (vol.volumeNumber > 0 ? tr("Volume %1").arg(vol.volumeNumber)
                                        : tr("Sources")));
    }

    if (vol.volumeNumber <= 0) {
        m_pendingNyaaReqId = -1;
        setContext(0, QString());
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

void ComicsSourcesPanel::setContext(int /*volumeNumber*/, const QString& /*volumeTitle*/)
{
    // No-op stub: label removed per Hemanth's UI cleanup.
    // Callers in ComicsSeriesView, clear(), and populate() still compile.
}

void ComicsSourcesPanel::appendWeebCentralRow(int volumeNumber, const QStringList& chapterIds)
{
    if (volumeNumber <= 0 || volumeNumber != m_currentVolNumber || chapterIds.isEmpty()) {
        return;
    }

    m_rows.erase(std::remove_if(m_rows.begin(), m_rows.end(),
                                [](const UnifiedSourceRow& row) {
                                    return row.kind == UnifiedSourceRow::Kind::WeebCentralPacker;
                                }),
                 m_rows.end());

    UnifiedSourceRow wcRow;
    wcRow.kind = UnifiedSourceRow::Kind::WeebCentralPacker;
    wcRow.tier = 99;
    wcRow.title = QStringLiteral("WeebCentral");
    wcRow.seeders = -1;
    wcRow.sizeBytes = 0;
    wcRow.weebCentralChapterIds = chapterIds;

    appendRow(wcRow);
    setSources(m_rows, m_pendingNyaaReqId >= 0);
}

QJsonObject ComicsSourcesPanel::devSnapshot() const
{
    QJsonObject snap;
    snap[QStringLiteral("visible")] = isVisible();
    snap[QStringLiteral("seriesTitle")] = m_currentSeriesTitle;
    snap[QStringLiteral("anilistId")] = m_currentAnilistId;
    snap[QStringLiteral("volume")] = m_currentVolNumber;
    snap[QStringLiteral("sourceCount")] = m_rows.size();
    snap[QStringLiteral("nyaaInFlight")] = m_pendingNyaaReqId >= 0;
    snap[QStringLiteral("autoPickArmed")] = m_autoPickArmed;
    snap[QStringLiteral("statusText")] = m_statusLabel ? m_statusLabel->text() : QString();
    snap[QStringLiteral("statusSubText")] = m_statusSubLabel ? m_statusSubLabel->text() : QString();

    QJsonArray chapters;
    for (const QString& id : m_currentChapterIds)
        chapters.append(id);
    snap[QStringLiteral("chapterIds")] = chapters;

    QJsonArray rows;
    for (const UnifiedSourceRow& row : m_rows)
        rows.append(sourceRowJson(row));
    snap[QStringLiteral("sources")] = rows;
    return snap;
}

bool ComicsSourcesPanel::devDispatchSource(const QString& source, QString* errorMessage)
{
    if (m_rows.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("no sources available");
        return false;
    }

    const QString normalized = source.trimmed().toLower();
    int rowIndex = 0;
    bool ok = false;
    if (!normalized.isEmpty()) {
        const int parsed = normalized.toInt(&ok);
        if (ok) {
            rowIndex = parsed;
        } else {
            rowIndex = -1;
            for (int i = 0; i < m_rows.size(); ++i) {
                const UnifiedSourceRow& row = m_rows.at(i);
                if (sourceKindString(row.kind) == normalized ||
                    row.title.toLower().contains(normalized)) {
                    rowIndex = i;
                    break;
                }
            }
        }
    }

    if (rowIndex < 0 || rowIndex >= m_rows.size()) {
        if (errorMessage) *errorMessage = QStringLiteral("source not found");
        return false;
    }

    m_autoPickSuppressed = true;
    cancelAutoPick();
    emitRowDownload(m_rows.at(rowIndex));
    return true;
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
        card->setVolumeNumber(m_currentVolNumber);
        connect(card, &ComicsSourceCard::clicked,
                this, [this](const UnifiedSourceRow& clickedRow) {
            m_autoPickSuppressed = true;
            cancelAutoPick();
            emitRowDownload(clickedRow);
        });
        connect(card, &ComicsSourceCard::downloadClicked,
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

void ComicsSourcesPanel::setWesternDownloadStatus(const QString& editionFound,
                                                  const QString& statusLine)
{
    // COMICS_WESTERN_DOWNLOAD 2026-06-02 — live GetComics download status. The
    // main label names the GetComics source / the matched collected edition; the
    // sub label carries the live state. Reuses the existing status labels (no new
    // card type) and keeps the indexer scroll hidden — Western has one source.
    clearCards();
    if (m_headerLabel) {
        m_headerLabel->setText(tr("Download"));
    }
    if (m_statusLabel) {
        m_statusLabel->setText(editionFound.isEmpty()
                                   ? tr("GetComics")
                                   : editionFound);
        m_statusLabel->show();
    }
    if (m_statusSubLabel) {
        m_statusSubLabel->setText(statusLine);
        m_statusSubLabel->setVisible(!statusLine.isEmpty());
    }
    if (m_scroll) {
        m_scroll->hide();
    }
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
