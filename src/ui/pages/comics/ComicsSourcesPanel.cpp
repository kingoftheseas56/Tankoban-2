// src/ui/pages/comics/ComicsSourcesPanel.cpp
#include "ComicsSourcesPanel.h"

#include "core/manga/PremiumCatalog.h"
#include "core/manga/PremiumCatalogSchema.h"

#include <QAbstractItemView>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedLayout>
#include <QVariant>
#include <Qt>

#include <algorithm>

namespace tankoban::manga::comics {

namespace {

// Format byte count to a compact "1.4 GiB" / "245 MiB" string. Mirrors the
// existing Stream blueprint source-row formatting (binary units). Returns
// empty string for zero/unknown -- caller hides the row's size field.
QString formatSize(qint64 bytes)
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

// Build the user-facing label for a UnifiedSourceRow. The list widget uses
// QListWidgetItem text directly; no custom delegate in v1.
QString rowLabel(const UnifiedSourceRow& row)
{
    QString tierTag;
    switch (row.kind) {
    case UnifiedSourceRow::Kind::Catalog:           tierTag = QStringLiteral("[Catalog]"); break;
    case UnifiedSourceRow::Kind::NyaaRuntime:
        tierTag = (row.tier == 1) ? QStringLiteral("[Tier 1]") : QStringLiteral("[Tier 2]");
        break;
    case UnifiedSourceRow::Kind::WeebCentralPacker: tierTag = QStringLiteral("[WeebCentral]"); break;
    }

    QString seedersField;
    if (row.kind == UnifiedSourceRow::Kind::WeebCentralPacker) {
        seedersField = QStringLiteral("synthesized");
    } else if (row.seeders >= 0) {
        seedersField = QString::number(row.seeders) + QStringLiteral(" seeders");
    }

    const QString sizeField = formatSize(row.sizeBytes);

    QStringList tail;
    if (!row.uploaderHint.isEmpty()) tail << row.uploaderHint;
    if (!seedersField.isEmpty())     tail << seedersField;
    if (!sizeField.isEmpty())        tail << sizeField;

    QString label = tierTag + QStringLiteral("  ") + row.title;
    if (!tail.isEmpty()) {
        label += QStringLiteral("\n      ") + tail.join(QStringLiteral("  -  "));
    }
    return label;
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

    // Stacked layout: empty-label on top when m_rows is empty, list widget
    // when ranked rows exist. Cleaner than show/hide juggling on two widgets
    // sharing a single QVBoxLayout slot.
    auto* stack = new QStackedLayout(this);
    stack->setContentsMargins(0, 0, 0, 0);

    m_emptyLabel = new QLabel(tr("Select a volume to see sources"), this);
    m_emptyLabel->setObjectName(QStringLiteral("ComicsSourcesEmptyLabel"));
    {
        QFont f = m_emptyLabel->font();
        f.setPointSize(11);
        m_emptyLabel->setFont(f);
    }
    m_emptyLabel->setStyleSheet(QStringLiteral("color: #888;"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    stack->addWidget(m_emptyLabel);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("ComicsSourcesList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(false);
    m_list->setWordWrap(true);
    stack->addWidget(m_list);

    stack->setCurrentWidget(m_emptyLabel);

    // Activate on double-click OR enter key OR single-click (treat as
    // "user picked this row + wants to download"). Stremio source-tab UX
    // uses single-click commit; mirror that here for parity.
    connect(m_list, &QListWidget::itemClicked,
            this,    &ComicsSourcesPanel::onRowActivated);
    connect(m_list, &QListWidget::itemActivated,
            this,    &ComicsSourcesPanel::onRowActivated);

    if (m_nyaa) {
        // QueuedConnection: the nyaa source emits from the NAM finished
        // slot which lands on the network thread under Qt6 default; the
        // panel touches m_list / m_rows which are UI-thread state.
        connect(m_nyaa, &NyaaRuntimeSource::searchSucceeded,
                this,    &ComicsSourcesPanel::onNyaaResults,
                Qt::QueuedConnection);
        connect(m_nyaa, &NyaaRuntimeSource::searchFailed,
                this,    &ComicsSourcesPanel::onNyaaFailed,
                Qt::QueuedConnection);
    }
}

void ComicsSourcesPanel::clear()
{
    m_rows.clear();
    m_list->clear();
    m_pendingNyaaReqId   = -1;
    m_currentSeriesTitle.clear();
    m_currentAnilistId   = 0;
    m_currentVolNumber   = 0;
    m_currentChapterIds.clear();
    renderEmpty();
}

void ComicsSourcesPanel::populate(const QString& seriesTitle,
                                  int            anilistSeriesId,
                                  const anilist::VolumeRow& vol,
                                  const QStringList& chapterIds)
{
    // Drop any prior state before starting the new lookup. A pending nyaa
    // reqId from the previous populate() is implicitly stale; onNyaaResults
    // discards it via the m_pendingNyaaReqId mismatch guard.
    clear();

    m_currentSeriesTitle = seriesTitle;
    m_currentAnilistId   = anilistSeriesId;
    m_currentVolNumber   = vol.volumeNumber;
    m_currentChapterIds  = chapterIds;

    // --- (1) Catalog hit, if any -----------------------------------------
    // PHASE 8 plan adaptation: populate() only carries anilistSeriesId (int);
    // PremiumCatalog stores both seriesId (string slug) AND anilistId (int)
    // per PremiumCatalogSchema.h. Use the anilistId-keyed helper here so the
    // panel does not have to round-trip through a separate slug-lookup.
    if (m_catalog && anilistSeriesId > 0) {
        const auto entryOpt = m_catalog->entryForAnilistIdAndVolume(
            anilistSeriesId, vol.volumeNumber);
        if (entryOpt.has_value()) {
            const auto& volEntry = entryOpt->second;
            const auto& entry    = entryOpt->first;

            UnifiedSourceRow row;
            row.kind         = UnifiedSourceRow::Kind::Catalog;
            row.tier         = 1;
            row.title        = QStringLiteral("%1 - Volume %2")
                                   .arg(entry.releaseEdition.isEmpty()
                                            ? entry.title
                                            : entry.releaseEdition)
                                   .arg(volEntry.vol);
            row.uploaderHint = entry.trustedUploader.isEmpty()
                                   ? entry.releaseEdition
                                   : entry.trustedUploader;
            row.seeders      = 0; // PHASE 13: live nyaa probe could fill this
            row.sizeBytes    = volEntry.fileSizeBytes; // catalog ships fileSizeBytes per vol
            row.magnetUri    = entry.magnetUri;
            row.infoHash     = entry.expectedInfoHash;
            appendRow(row);
        }
    }

    // --- (2) Fire async nyaa runtime search ------------------------------
    // Always fire when m_nyaa is wired; results land on onNyaaResults via
    // QueuedConnection. Filtered to the current pending reqId so a late
    // result from a prior volume's search gets dropped on the floor.
    if (m_nyaa) {
        m_pendingNyaaReqId = m_nextNyaaReqId++;
        m_nyaa->search(seriesTitle, vol.volumeNumber, m_pendingNyaaReqId);
    }

    // --- (3) WeebCentralPacker fallback ----------------------------------
    // Synthesizable when chapterIds is non-empty (Phase 5 packer requires
    // at least one chapter to fetch). For volumes with no chapter mapping
    // (e.g. AniList "Volume X" sentinel with no bound chapters), skip the
    // WC row entirely -- there is nothing to pack.
    if (!chapterIds.isEmpty()) {
        UnifiedSourceRow wcRow;
        wcRow.kind         = UnifiedSourceRow::Kind::WeebCentralPacker;
        wcRow.tier         = 99;
        wcRow.title        = QStringLiteral("WeebCentral (synthesized) - %1 ch")
                                 .arg(chapterIds.size());
        wcRow.uploaderHint = QStringLiteral("WeebCentral");
        wcRow.seeders      = -1;
        wcRow.sizeBytes    = 0;
        appendRow(wcRow);
    }

    // Render whatever we have so far. onNyaaResults will re-render once
    // nyaa results land.
    if (m_rows.isEmpty()) {
        renderEmpty();
    } else {
        renderRanked();
    }
}

void ComicsSourcesPanel::onNyaaResults(int reqId,
                                       const QList<NyaaSourceCandidate>& results)
{
    // Stale-result guard: drop anything not from the current populate().
    if (reqId != m_pendingNyaaReqId) {
        return;
    }
    m_pendingNyaaReqId = -1;

    for (const auto& cand : results) {
        UnifiedSourceRow row;
        row.kind         = UnifiedSourceRow::Kind::NyaaRuntime;
        row.tier         = cand.tier;
        row.title        = cand.title;
        row.uploaderHint = cand.uploader;
        row.seeders      = cand.seeders;
        row.sizeBytes    = cand.sizeBytes;
        row.magnetUri    = cand.magnetUri;
        row.infoHash     = cand.infoHash;
        appendRow(row);
    }

    if (m_rows.isEmpty()) {
        renderEmpty();
    } else {
        renderRanked();
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

    // Leave catalog + WC rows in place if they were appended. If both are
    // absent (no catalog hit + no chapterIds), surface empty state.
    if (m_rows.isEmpty()) {
        renderEmpty();
    } else {
        renderRanked();
    }
}

void ComicsSourcesPanel::onRowActivated(QListWidgetItem* item)
{
    if (!item) return;

    // The list item stores an int index into m_rows via UserRole. Cheaper
    // and safer than registering UnifiedSourceRow with the QMetaType system
    // for QVariant round-tripping.
    bool ok = false;
    const int idx = item->data(Qt::UserRole).toInt(&ok);
    if (!ok || idx < 0 || idx >= m_rows.size()) {
        return;
    }
    const UnifiedSourceRow row = m_rows.at(idx);

    emit downloadRequested(row,
                           m_currentSeriesTitle,
                           m_currentAnilistId,
                           m_currentVolNumber,
                           m_currentChapterIds);
}

void ComicsSourcesPanel::appendRow(const UnifiedSourceRow& row)
{
    m_rows.append(row);
}

void ComicsSourcesPanel::renderEmpty()
{
    m_list->clear();
    // Flip the stacked layout back to the empty label. We use the parent
    // QStackedLayout (set in the ctor) for the actual swap.
    if (auto* stack = qobject_cast<QStackedLayout*>(layout())) {
        stack->setCurrentWidget(m_emptyLabel);
    }
}

void ComicsSourcesPanel::renderRanked()
{
    // Stable sort by (tier asc, then seeders desc within tier). Catalog
    // rows insert FIRST among tier=1 entries because of insertion order
    // (catalog appended in populate() before any nyaa result lands); the
    // stable sort preserves that ordering boost when tiers tie.
    std::stable_sort(m_rows.begin(), m_rows.end(),
                     [](const UnifiedSourceRow& a, const UnifiedSourceRow& b) {
                         if (a.tier != b.tier) return a.tier < b.tier;
                         return a.seeders > b.seeders;
                     });

    m_list->clear();
    for (int i = 0; i < m_rows.size(); ++i) {
        auto* item = new QListWidgetItem(rowLabel(m_rows.at(i)), m_list);
        item->setData(Qt::UserRole, i);
    }

    if (auto* stack = qobject_cast<QStackedLayout*>(layout())) {
        stack->setCurrentWidget(m_list);
    }
}

} // namespace tankoban::manga::comics
