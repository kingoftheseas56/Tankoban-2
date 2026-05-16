// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — Phase 4 detail view impl.
// See header for fork lineage + Phase 5 forward-decl notes.

#include "ComicsTankoyomiDetailView.h"
#include "SidecarMeta.h"
#include "core/manga/MangaDownloader.h"
#include "core/manga/MangaPosterCache.h"
#include "core/manga/PremiumCatalog.h"
#include "ui/widgets/Toast.h"
#include "core/CoreBridge.h"
#include "core/manga/ComicsLibraryRecord.h"
#include "core/manga/ComicsTankoyomiLibrary.h"
#include "core/manga/MangaDownloadIndex.h"
#include "core/manga/MangaScraper.h"
#include "core/manga/MangaSourceRegistry.h"
#include "ui/pages/tankoyomi/ChapterDownloadIndicator.h"
#include "ui/pages/tankoyomi/ChapterRangeDialog.h"
#include "ui/ContextMenuHelper.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QCryptographicHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace {

QString sanitiseFilename(const QString& s)
{
    QString r = s;
    static const QString kBad = R"(<>:"/\|?*)";
    for (QChar c : kBad) r.replace(c, '_');
    return r.trimmed();
}

} // namespace

ComicsTankoyomiDetailView::ComicsTankoyomiDetailView(
    CoreBridge* bridge,
    MangaSourceRegistry* registry,
    ComicsTankoyomiLibrary* tyLibrary,
    MangaDownloader* downloader,
    MangaDownloadIndex* downloadIndex,
    QNetworkAccessManager* nam,
    QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
    , m_registry(registry)
    , m_tyLibrary(tyLibrary)
    , m_downloader(downloader)
    , m_downloadIndex(downloadIndex)
    , m_nam(nam)
{
    buildUI();
}

void ComicsTankoyomiDetailView::setPremiumCatalog(
    tankoban::manga::premium::PremiumCatalog* catalog)
{
    m_premiumCatalog = catalog;
}

void ComicsTankoyomiDetailView::setAdoptLookup(AdoptLookup fn)
{
    m_adoptLookup = std::move(fn);
}

void ComicsTankoyomiDetailView::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);

    auto* topRow = new QHBoxLayout;
    m_backBtn = new QPushButton("← Back");
    connect(m_backBtn, &QPushButton::clicked, this, &ComicsTankoyomiDetailView::backRequested);
    m_addRemoveBtn = new QPushButton("Add to library");
    m_addRemoveBtn->setObjectName("AddRemoveLibraryBtn");
    connect(m_addRemoveBtn, &QPushButton::clicked, this, &ComicsTankoyomiDetailView::onAddRemoveClicked);
    topRow->addWidget(m_backBtn);
    topRow->addStretch(1);
    topRow->addWidget(m_addRemoveBtn);
    root->addLayout(topRow);

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 37 — wrap the
    // hero row in a QWidget so we can attach a CustomContextMenu policy.
    // Layout structure is otherwise identical to Phase 4.
    auto* heroWidget = new QWidget(this);
    heroWidget->setObjectName("ComicsDetailHero");
    auto* hero = new QHBoxLayout(heroWidget);
    hero->setContentsMargins(0, 0, 0, 0);
    m_coverLabel = new QLabel;
    m_coverLabel->setObjectName("ComicsDetailHeroCover");
    m_coverLabel->setFixedSize(180, 270);
    hero->addWidget(m_coverLabel);

    auto* heroText = new QVBoxLayout;
    m_titleLabel = new QLabel;
    m_titleLabel->setObjectName("ComicsDetailTitle");
    m_metaLabel = new QLabel;
    m_metaLabel->setObjectName("ComicsDetailMeta");
    m_synopsisLabel = new QLabel;
    m_synopsisLabel->setObjectName("ComicsDetailSynopsis");
    m_synopsisLabel->setWordWrap(true);
    m_genresLabel = new QLabel;
    m_genresLabel->setObjectName("ComicsDetailGenres");
    heroText->addWidget(m_titleLabel);
    heroText->addWidget(m_metaLabel);
    heroText->addWidget(m_synopsisLabel);
    heroText->addWidget(m_genresLabel);
    heroText->addStretch(1);
    hero->addLayout(heroText, 1);

    heroWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(heroWidget, &QWidget::customContextMenuRequested,
            this, [this, heroWidget](const QPoint& pos) {
        onSeriesHeaderContextMenu(heroWidget->mapToGlobal(pos));
    });
    root->addWidget(heroWidget);

    m_offlineBanner = new QLabel(
        "Couldn't refresh chapter list from Tankoyomi — showing cached state.");
    m_offlineBanner->setObjectName("ComicsDetailOfflineBanner");
    m_offlineBanner->setVisible(false);
    root->addWidget(m_offlineBanner);

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 36 — chapter-
    // list header row: section label + Range/N-selected action buttons.
    // m_downloadSelectedBtn stays hidden until at least one row is checked.
    auto* chapHeader = new QHBoxLayout;
    auto* chapTitle = new QLabel(QStringLiteral("CHAPTERS"));
    chapTitle->setObjectName("ComicsDetailChaptersTitle");
    chapHeader->addWidget(chapTitle);
    chapHeader->addStretch(1);
    m_downloadRangeBtn = new QPushButton(QStringLiteral("Download Range..."));
    m_downloadSelectedBtn = new QPushButton(QStringLiteral("Download 0 selected"));
    m_downloadSelectedBtn->setVisible(false);
    chapHeader->addWidget(m_downloadRangeBtn);
    chapHeader->addWidget(m_downloadSelectedBtn);
    connect(m_downloadRangeBtn, &QPushButton::clicked,
            this, &ComicsTankoyomiDetailView::onDownloadRangeClicked);
    connect(m_downloadSelectedBtn, &QPushButton::clicked,
            this, &ComicsTankoyomiDetailView::onDownloadSelectedClicked);
    root->addLayout(chapHeader);

    // TANKOYOMI_PREMIUM Phase 7 Task 7.3 -- filter chip row (All / Downloaded
    // / Unread / Premium / Loose). Mutex-exclusive checked state; clicking
    // a chip hides non-matching rows. Inserted between the chapter-header
    // row (chapHeader) and the chapter table. Inert filter modes (Unread,
    // Loose) currently passthrough most rows -- see rowMatchesActiveFilter
    // PHASE 7+ TODOs.
    auto* filterRow = new QWidget(this);
    filterRow->setObjectName("ComicsDetailFilterChips");
    auto* filterLayout = new QHBoxLayout(filterRow);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    m_filterAll        = new QPushButton(QStringLiteral("All"));
    m_filterDownloaded = new QPushButton(QStringLiteral("Downloaded"));
    m_filterUnread     = new QPushButton(QStringLiteral("Unread"));
    m_filterPremium    = new QPushButton(QStringLiteral("Premium"));
    m_filterLoose      = new QPushButton(QStringLiteral("Loose"));
    for (auto* b : { m_filterAll, m_filterDownloaded, m_filterUnread,
                     m_filterPremium, m_filterLoose })
    {
        b->setCheckable(true);
        b->setProperty("filterChip", true);
        filterLayout->addWidget(b);
        connect(b, &QPushButton::clicked,
                this, &ComicsTankoyomiDetailView::onFilterChanged);
    }
    m_filterAll->setChecked(true);
    filterLayout->addStretch(1);
    root->addWidget(filterRow);

    m_chapterTable = new QTableWidget(0, kColCount);
    m_chapterTable->setHorizontalHeaderLabels({"", "", "Chapter", "Date"});
    m_chapterTable->horizontalHeader()->setStretchLastSection(true);
    m_chapterTable->verticalHeader()->setVisible(false);
    m_chapterTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_chapterTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_chapterTable, &QTableWidget::cellClicked,
            this, &ComicsTankoyomiDetailView::onChapterRowClicked);
    connect(m_chapterTable, &QTableWidget::customContextMenuRequested,
            this, &ComicsTankoyomiDetailView::onChapterContextMenu);
    // Phase 5 Task 36: itemChanged drives the live count on the
    // N-selected button. cellWidget changes (the indicator column) don't
    // fire itemChanged — only the checkbox column does, which is what we
    // want.
    connect(m_chapterTable, &QTableWidget::itemChanged,
            this, &ComicsTankoyomiDetailView::updateDownloadSelectedButton);
    root->addWidget(m_chapterTable, 1);
}

void ComicsTankoyomiDetailView::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    // Phase 5 Task 30: re-validate the on-disk index + repaint chapter
    // markers so an external delete since the view was last shown is
    // reflected in the per-chapter chip state.
    if (m_downloadIndex) m_downloadIndex->validateAll();
    refreshChapterMarkers();
}

// ── Task 23: preview-first hero + fetchDetail refresh + cache lookup ────────

void ComicsTankoyomiDetailView::refreshDownloadMarkers()
{
    refreshChapterMarkers();
}

void ComicsTankoyomiDetailView::setCoverFromPath(const QString& path)
{
    if (!m_coverLabel) return;
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        m_coverLabel->clear();
        return;
    }
    QPixmap pix(path);
    if (pix.isNull()) {
        m_coverLabel->clear();
        return;
    }
    m_coverLabel->setPixmap(pix.scaled(m_coverLabel->size(), Qt::KeepAspectRatioByExpanding,
                                       Qt::SmoothTransformation));
}

void ComicsTankoyomiDetailView::loadCoverFromUrl(const QString& imageUrl)
{
    if (imageUrl.isEmpty()) return;
    QPointer<ComicsTankoyomiDetailView> self(this);
    MangaPosterCache::download(m_currentPreview, imageUrl, m_nam, this,
        [self](const QString& path) {
            if (!self) return;
            self->setCoverFromPath(path);
            if (self->m_tyLibrary && self->isInLibrary()) {
                auto rec = self->m_tyLibrary->get(self->m_currentPreview.source,
                                                  self->m_currentPreview.id);
                if (!rec.title.isEmpty() && rec.coverPath != path) {
                    rec.coverPath = path;
                    self->m_tyLibrary->add(rec);
                }
            }
        });
}

void ComicsTankoyomiDetailView::showEntry(const MangaResult& previewHint)
{
    m_currentPreview = previewHint;
    m_currentDetail.reset();
    m_currentChapters.clear();
    m_sourceOffline = false;
    m_offlineBanner->setVisible(false);

    renderPreviewHero(previewHint);

    m_currentScraper = m_registry ? m_registry->find(previewHint.source) : nullptr;
    if (!m_currentScraper) {
        m_synopsisLabel->setText("(Unknown source)");
        return;
    }

    // Cache lookup order (Codex §13): library record → sidecar → preview.
    if (m_tyLibrary && m_tyLibrary->contains(previewHint.source, previewHint.id)) {
        auto rec = m_tyLibrary->get(previewHint.source, previewHint.id);

        // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 6 Task 40 —
        // renamed-folder recovery. If the library record's canonical
        // path no longer points at a real folder (the user renamed or
        // moved it on disk), scan every Comics root for a sidecar with
        // the same (sourceId, seriesId) identity. On hit, the library
        // record is re-pointed and we re-read it before continuing.
        if (!rec.canonicalSeriesPath.isEmpty()
            && !QFileInfo(rec.canonicalSeriesPath).exists()) {
            const auto roots = m_bridge ? m_bridge->rootFolders("comics") : QStringList{};
            if (const auto relocated = m_tyLibrary->findAndRelocateByIdentity(
                    previewHint.source, previewHint.id, roots)) {
                rec = *relocated;
            }
        }

        if (!rec.detailCache.synopsis.isEmpty()) {
            m_currentDetail = rec.detailCache;
            renderDetailHero(rec.detailCache);
        }
        // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 6 Task 39 —
        // sidecar is a RECOVERY HINT (per SidecarMeta.h header / Codex §16);
        // if the user manually deleted .tankoyomi-meta.json from the
        // canonical folder, the library record is still authoritative but
        // future renamed-folder recovery (Task 40) needs the sidecar to be
        // present. Rewrite from the library record on detail-view open.
        if (!rec.canonicalSeriesPath.isEmpty()
            && QFileInfo(rec.canonicalSeriesPath).exists()
            && !sidecar::exists(rec.canonicalSeriesPath)) {
            SidecarMeta sm;
            sm.sourceId  = rec.sourceId;
            sm.seriesId  = rec.seriesId;
            sm.title     = rec.title;
            sm.createdAt = rec.addedAt;
            if (!sidecar::write(rec.canonicalSeriesPath, sm)) {
                // Recovery hint regeneration failed (disk-full, perm-denied,
                // RO mount). Library record still authoritative; Task 40's
                // renamed-folder recovery will not work until the sidecar
                // can be re-written.
                qWarning() << "[ComicsTankoyomiDetailView] failed to rewrite"
                           << "sidecar at" << rec.canonicalSeriesPath;
            }
        }
    }

    // C1+I1 fix: disconnect any prior session's scraper connections BEFORE
    // re-binding. Without this, late detailReady/chaptersReady/errorOccurred
    // emissions from a previous showEntry call would fire stale lambdas
    // (stale detailReady → harmless idempotent re-render but extra
    // libraryChanged; stale chaptersReady → repaints chapter list with the
    // wrong series's chapters; stale errorOccurred → paints offline banner
    // on the now-current series). The shared_ptr<Connection> pattern this
    // replaces handled disconnect-on-fire for detail/chapters but NEVER
    // disconnected error (named in the prior code-comment as a contract
    // gap). Members give us the lifetime + identity for free.
    if (m_detailConn)   QObject::disconnect(m_detailConn);
    if (m_chaptersConn) QObject::disconnect(m_chaptersConn);
    if (m_errConn)      QObject::disconnect(m_errConn);

    QPointer<ComicsTankoyomiDetailView> self(this);

    m_detailConn = connect(m_currentScraper, &MangaScraper::detailReady, this,
        [self](const MangaSeriesDetail& d) {
            if (!self) return;
            QObject::disconnect(self->m_detailConn);
            self->onFetchDetailReady(d);
        });
    m_currentScraper->fetchDetail(previewHint);

    m_chaptersConn = connect(m_currentScraper, &MangaScraper::chaptersReady, this,
        [self](const QList<ChapterInfo>& chs) {
            if (!self) return;
            QObject::disconnect(self->m_chaptersConn);
            self->onChaptersReady(chs);
        });
    m_currentScraper->fetchChapters(previewHint.id);

    // Source-error → offline banner. Kept connected for the whole session;
    // disconnected at the top of the next showEntry call. Stale-session
    // errors from prior series can no longer fire because the connection
    // was disconnected above.
    m_errConn = connect(m_currentScraper, &MangaScraper::errorOccurred, this,
        [self](const QString& msg) {
            if (self) self->onSourceError(msg);
        });

    m_addRemoveBtn->setText(isInLibrary() ? "Remove from library" : "Add to library");
}

void ComicsTankoyomiDetailView::renderPreviewHero(const MangaResult& preview)
{
    m_titleLabel->setText(preview.title);
    QStringList parts;
    if (!preview.author.isEmpty()) parts << preview.author;
    if (!preview.status.isEmpty()) parts << preview.status;
    if (!preview.source.isEmpty()) parts << preview.source;
    m_metaLabel->setText(parts.join(" . "));
    m_synopsisLabel->setText(QString());
    m_genresLabel->setText(QString());
    setCoverFromPath(MangaPosterCache::existingPath(preview.source, preview.id));
    if (m_coverLabel && m_coverLabel->pixmap().isNull())
        loadCoverFromUrl(preview.thumbnailUrl);
    // Cover load — Phase 4 minimum: nothing (Phase 5 wires the actual
    // poster cache lookup + setPixmap). The cover label stays empty.
}

void ComicsTankoyomiDetailView::renderDetailHero(const MangaSeriesDetail& detail)
{
    if (!detail.synopsis.isEmpty()) m_synopsisLabel->setText(detail.synopsis);
    if (!detail.genres.isEmpty())  m_genresLabel->setText(detail.genres.join(" • "));
    QStringList parts;
    const QString author = detail.author.isEmpty() ? detail.preview.author : detail.author;
    const QString status = detail.status.isEmpty() ? detail.preview.status : detail.status;
    if (!author.isEmpty()) parts << author;
    if (!detail.year.isEmpty()) parts << detail.year;
    if (!status.isEmpty()) parts << status;
    if (!detail.preview.source.isEmpty()) parts << detail.preview.source;
    m_metaLabel->setText(parts.join(" . "));
    if (!detail.heroCoverUrl.isEmpty())
        loadCoverFromUrl(detail.heroCoverUrl);
}

void ComicsTankoyomiDetailView::onFetchDetailReady(const MangaSeriesDetail& detail)
{
    m_sourceOffline = false;
    m_offlineBanner->setVisible(false);
    m_currentDetail = detail;
    renderDetailHero(detail);
    if (m_tyLibrary && m_tyLibrary->contains(detail.preview.source, detail.preview.id)) {
        auto rec = m_tyLibrary->get(detail.preview.source, detail.preview.id);
        rec.detailCache = detail;
        const QString cover = MangaPosterCache::existingPath(detail.preview.source, detail.preview.id);
        if (!cover.isEmpty()) rec.coverPath = cover;
        rec.lastValidatedAt = QDateTime::currentMSecsSinceEpoch();
        m_tyLibrary->add(rec);
    }
    if (!detail.cachedChapters.isEmpty() && m_currentChapters.isEmpty()) {
        m_currentChapters = detail.cachedChapters;
        renderChapters(m_currentChapters);
        refreshChapterMarkers();
    }
}

void ComicsTankoyomiDetailView::onChaptersReady(const QList<ChapterInfo>& chs)
{
    m_sourceOffline = false;
    m_offlineBanner->setVisible(false);
    m_currentChapters = chs;
    renderChapters(chs);
    refreshChapterMarkers();
}

void ComicsTankoyomiDetailView::onSourceError(const QString& /*msg*/)
{
    m_sourceOffline = true;
    m_offlineBanner->setVisible(true);
    refreshChapterMarkers();
}

// ── Task 24: renderChapters + refreshChapterMarkers stubs ───────────────────

// TANKOYOMI_PREMIUM Phase 6 -- Premium-entry resolver. Used by renderChapters
// to short-circuit into the volume-row layout when the current series has a
// catalog entry. PremiumCatalog::entryForTitle is case-insensitive across the
// primary + alternate titles per Phase 1 contract.
std::optional<tankoban::manga::premium::PremiumCatalogEntry>
ComicsTankoyomiDetailView::premiumEntryForCurrentSeries() const
{
    if (!m_premiumCatalog) return std::nullopt;
    return m_premiumCatalog->entryForTitle(m_currentPreview.title);
}

// TANKOYOMI_PREMIUM Phase 6 -- volume-row repaint. Wipes the 4-column chapter
// table and re-installs a 5-column (Cover | Volume | Chapters | Status |
// Action) layout sorted descending by vol (newest at top — Tankoyomi/Mihon
// convention per Codex section 26). For ongoing series with a configured
// WeebCentral fallback slug, a centered/bold section-break row is appended
// to flag the loose-tail boundary. The Download action button is inert in
// Phase 6 (Phase 7 wires it to TorrentVolumeProvider::requestVolume).
void ComicsTankoyomiDetailView::populateVolumeAndLooseTailTable(
    const tankoban::manga::premium::PremiumCatalogEntry& entry)
{
    m_chapterTable->clear();
    m_chapterTable->setRowCount(0);
    m_chapterTable->setColumnCount(5);
    m_chapterTable->setHorizontalHeaderLabels(QStringList{
        QStringLiteral("Cover"),
        QStringLiteral("Volume"),
        QStringLiteral("Chapters"),
        QStringLiteral("Status"),
        QStringLiteral("Action")
    });

    // Sort descending by vol (Tankoyomi/Mihon "newest at top" convention).
    QList<tankoban::manga::premium::PremiumVolumeEntry> volumes = entry.volumes;
    std::sort(volumes.begin(), volumes.end(),
              [](const tankoban::manga::premium::PremiumVolumeEntry& a,
                 const tankoban::manga::premium::PremiumVolumeEntry& b) {
                  return a.vol > b.vol;
              });

    // PHASE 7 TODO: wrap the volume fill in a QSignalBlocker (mirroring the
    // non-Premium branch convention) so future per-volume checkboxes won't
    // re-fire itemChanged in O(N) per setItem. Benign today because Premium
    // column 0 placeholder items aren't user-checkable.
    for (const auto& v : volumes) {
        const int row = m_chapterTable->rowCount();
        m_chapterTable->insertRow(row);

        // Cover cell -- placeholder for Phase 6 (real cover extraction lands
        // in Phase 10). Stash a marker in UserRole so the eventual cover
        // setter can target this row.
        auto* coverItem = new QTableWidgetItem();
        coverItem->setData(Qt::UserRole, QStringLiteral("placeholder"));
        m_chapterTable->setItem(row, 0, coverItem);

        m_chapterTable->setItem(row, 1, new QTableWidgetItem(
            QStringLiteral("Volume %1").arg(v.vol)));

        QString chapterRange;
        if (!v.chapters.isEmpty()) {
            chapterRange = QStringLiteral("Chs %1-%2 (%3)")
                .arg(v.chapters.first().chapterNumber)
                .arg(v.chapters.last().chapterNumber)
                .arg(v.chapters.size());
        }
        m_chapterTable->setItem(row, 2, new QTableWidgetItem(chapterRange));

        // Status resolved against MangaDownloadIndex in Phase 7. Phase 6
        // ships the placeholder "Not downloaded" string.
        m_chapterTable->setItem(row, 3, new QTableWidgetItem(
            QStringLiteral("Not downloaded")));

        // Action button -- Phase 7 wires click to
        // TorrentVolumeProvider::requestVolume. Properties carry the routing
        // keys so Phase 7 wiring can resolve volume identity from the sender.
        auto* btn = new QPushButton(QStringLiteral("Download"));
        btn->setProperty("premiumSeriesId", entry.seriesId);
        btn->setProperty("premiumVolume",   v.vol);
        // TANKOYOMI_PREMIUM Phase 7 Task 7.1 -- emit a signal up to ComicsPage,
        // which resolves the catalog entry and calls TorrentVolumeProvider.
        // Capture by value so a queued reshow of populateVolume... can't
        // invalidate `entry` / `v` from under a deferred click.
        connect(btn, &QPushButton::clicked, this, [this, sid = entry.seriesId, vn = v.vol]() {
            emit premiumVolumeDownloadRequested(sid, vn);
        });
        m_chapterTable->setCellWidget(row, 4, btn);
    }

    // Loose tail for ongoing series with a configured WeebCentral fallback
    // slug. A centered/bold section-break row separates the curated volumes
    // (above) from the live WeebCentral chapter tail (below; Phase 7 wires
    // the fetch).
    if (entry.status == QStringLiteral("ongoing") &&
        !entry.postCoverageWeebcentralSlug.isEmpty())
    {
        const int row = m_chapterTable->rowCount();
        m_chapterTable->insertRow(row);
        m_chapterTable->setSpan(row, 0, 1, 5);
        auto* hdr = new QTableWidgetItem(
            QStringLiteral("-- Latest chapters (WeebCentral) --"));
        hdr->setTextAlignment(Qt::AlignCenter);
        QFont f = hdr->font();
        f.setBold(true);
        hdr->setFont(f);
        // TANKOYOMI_PREMIUM Phase 7 Task 7.3 -- sticky-looking section break
        // styling. Faint translucent background + a UserRole+5 sentinel that
        // rowMatchesActiveFilter reads to keep the divider visible across
        // every filter except (eventually) loose-tail-only.
        hdr->setBackground(QColor(255, 255, 255, 10));
        hdr->setData(Qt::UserRole + 5, true);
        m_chapterTable->setItem(row, 0, hdr);

        // The newest-on-top sort means volumes.first() carries the highest
        // volume; its last chapter is the threshold for the loose-tail
        // filter Phase 7 will run against the WeebCentral fetch.
        // PHASE 7 TODO: volumes.first().chapters.last() assumes ascending chapter
        // numbers within the volume. Either enforce that invariant in the Phase 1
        // catalog validator or iterate chapters and pick the numeric max (handles
        // half-chapters like 12.5 + out-of-order side stories).
        const QString lastCoveredChapter =
            (!volumes.isEmpty() && !volumes.first().chapters.isEmpty())
                ? volumes.first().chapters.last().chapterNumber
                : QString();
        appendLooseTailChaptersAfter(entry.postCoverageWeebcentralSlug,
                                     lastCoveredChapter);
    }
}

// TANKOYOMI_PREMIUM Phase 6 -- loose-tail fetch stub. Phase 6 only stashes
// the threshold; Phase 7 will dispatch the actual scraper fetch and filter
// chaptersReady against m_looseTailThresholdChapterNum. Calling the scraper
// here in Phase 6 would intermix two fetch lifecycles, so it's deferred.
void ComicsTankoyomiDetailView::appendLooseTailChaptersAfter(
    const QString& weebcentralSlug, const QString& lastCoveredChapterNum)
{
    m_looseTailThresholdChapterNum = lastCoveredChapterNum;
    Q_UNUSED(weebcentralSlug);
}

// TANKOYOMI_PREMIUM Phase 7 Task 7.1 -- repaint a single volume row in
// response to TorrentVolumeProvider progress/completion/failure/swarm
// signals. Helper walks the table to find the QPushButton in column 4
// matching the (seriesId, volumeNumber) pair; returns -1 if not found
// (user navigated away or table re-populated).
static int findPremiumVolumeRow(QTableWidget* table,
                                 const QString& seriesId, int volumeNumber)
{
    if (!table) return -1;
    for (int r = 0; r < table->rowCount(); ++r) {
        auto* w = table->cellWidget(r, 4);
        if (!w) continue;
        auto* btn = qobject_cast<QPushButton*>(w);
        if (!btn) continue;
        if (btn->property("premiumSeriesId").toString() == seriesId
            && btn->property("premiumVolume").toInt() == volumeNumber)
        {
            return r;
        }
    }
    return -1;
}

void ComicsTankoyomiDetailView::onPremiumVolumeProgress(
    const QString& seriesId, int volumeNumber, double pct)
{
    const auto cur = premiumEntryForCurrentSeries();
    if (!cur || cur->seriesId != seriesId) return;
    const int row = findPremiumVolumeRow(m_chapterTable, seriesId, volumeNumber);
    if (row < 0) return;
    const int percent = static_cast<int>(pct * 100.0);
    if (auto* item = m_chapterTable->item(row, 3)) {
        item->setText(QStringLiteral("Downloading %1%").arg(percent));
    } else {
        m_chapterTable->setItem(row, 3, new QTableWidgetItem(
            QStringLiteral("Downloading %1%").arg(percent)));
    }
}

void ComicsTankoyomiDetailView::onPremiumVolumeCompleted(
    const QString& seriesId, int volumeNumber)
{
    const auto cur = premiumEntryForCurrentSeries();
    if (!cur || cur->seriesId != seriesId) return;
    const int row = findPremiumVolumeRow(m_chapterTable, seriesId, volumeNumber);
    if (row < 0) return;
    if (auto* item = m_chapterTable->item(row, 3)) {
        item->setText(QStringLiteral("Downloaded"));
    } else {
        m_chapterTable->setItem(row, 3, new QTableWidgetItem(
            QStringLiteral("Downloaded")));
    }
    if (auto* btn = qobject_cast<QPushButton*>(m_chapterTable->cellWidget(row, 4))) {
        btn->setText(QStringLiteral("Read"));
    }
}

void ComicsTankoyomiDetailView::onPremiumVolumeFailed(
    const QString& seriesId, int volumeNumber,
    const QString& code, const QString& message)
{
    Q_UNUSED(message);
    const auto cur = premiumEntryForCurrentSeries();
    if (!cur || cur->seriesId != seriesId) return;
    const int row = findPremiumVolumeRow(m_chapterTable, seriesId, volumeNumber);
    if (row < 0) return;
    const QString statusText = QStringLiteral("Failed (%1)").arg(code);
    if (auto* item = m_chapterTable->item(row, 3)) {
        item->setText(statusText);
    } else {
        m_chapterTable->setItem(row, 3, new QTableWidgetItem(statusText));
    }
    if (auto* btn = qobject_cast<QPushButton*>(m_chapterTable->cellWidget(row, 4))) {
        btn->setText(QStringLiteral("Retry"));
    }
}

void ComicsTankoyomiDetailView::onPremiumSwarmStatus(
    const QString& seriesId, int volumeNumber, int piecePeersOnline)
{
    // Positive peer count: don't overwrite an active "Downloading X%" cell.
    if (piecePeersOnline > 0) return;
    const auto cur = premiumEntryForCurrentSeries();
    if (!cur || cur->seriesId != seriesId) return;
    const int row = findPremiumVolumeRow(m_chapterTable, seriesId, volumeNumber);
    if (row < 0) return;
    if (auto* item = m_chapterTable->item(row, 3)) {
        item->setText(QStringLiteral("Waiting for peers"));
    } else {
        m_chapterTable->setItem(row, 3, new QTableWidgetItem(
            QStringLiteral("Waiting for peers")));
    }
}

// TANKOYOMI_PREMIUM Phase 10 -- per-volume cover thumbnail arrived from
// PremiumCoverExtractor (via TorrentVolumeProvider::volumeCoverReady).
// Targets the matching column-0 placeholder cell from
// populateVolumeAndLooseTailTable; early-returns silently if the user
// navigated away or the table was re-populated for a different series
// mid-extraction.
void ComicsTankoyomiDetailView::setPremiumVolumeCover(const QString& seriesId,
                                                     int volumeNumber,
                                                     const QString& coverPath)
{
    if (!m_chapterTable) return;
    auto entryOpt = premiumEntryForCurrentSeries();
    if (!entryOpt || entryOpt->seriesId != seriesId) return;
    for (int r = 0; r < m_chapterTable->rowCount(); ++r) {
        auto* btn = qobject_cast<QPushButton*>(m_chapterTable->cellWidget(r, 4));
        if (!btn) continue;
        if (btn->property("premiumVolume").toInt() != volumeNumber) continue;
        if (btn->property("premiumSeriesId").toString() != seriesId) continue;
        auto* item = m_chapterTable->item(r, 0);
        if (item) {
            item->setData(Qt::DecorationRole, QIcon(coverPath));
            item->setData(Qt::UserRole, coverPath);
        }
        return;
    }
}

// TANKOYOMI_PREMIUM Phase 7 Task 7.3 -- filter chip behavior.
//
// onFilterChanged enforces mutex-exclusive checked state: when one chip is
// clicked it un-checks the others, then re-applies row visibility via
// setRowHidden + rowMatchesActiveFilter. Section-break rows (tagged with
// UserRole+5 = true on column 0) stay visible under All/Downloaded/Unread/
// Premium so the divider doesn't vanish when filtering volumes. Loose
// filter is a stub today -- non-Premium loose-tail rows aren't yet
// populated in the table (Phase 6 left the loose fetch deferred), so
// Loose currently shows only the section-break.
void ComicsTankoyomiDetailView::onFilterChanged()
{
    if (!m_chapterTable) return;
    QPushButton* clicked = qobject_cast<QPushButton*>(sender());
    if (clicked) {
        for (auto* b : { m_filterAll, m_filterDownloaded, m_filterUnread,
                         m_filterPremium, m_filterLoose })
        {
            if (b && b != clicked) {
                QSignalBlocker blocker(b);
                b->setChecked(false);
            }
        }
        if (!clicked->isChecked()) {
            // Don't allow zero-checked state -- fall back to All.
            QSignalBlocker blocker(m_filterAll);
            if (m_filterAll) m_filterAll->setChecked(true);
        }
    }
    for (int r = 0; r < m_chapterTable->rowCount(); ++r) {
        m_chapterTable->setRowHidden(r, !rowMatchesActiveFilter(r));
    }
}

bool ComicsTankoyomiDetailView::rowMatchesActiveFilter(int row) const
{
    if (!m_chapterTable) return true;
    // Section-break rows: visible under All/Downloaded/Unread/Premium so
    // the divider survives. Hidden under Loose (the section break itself
    // is a Premium-side artifact).
    auto* c0 = m_chapterTable->item(row, 0);
    const bool isSectionBreak = c0 && c0->data(Qt::UserRole + 5).toBool();

    // Identify Premium row vs chapter row by inspecting column-4 cell widget.
    auto* w = m_chapterTable->cellWidget(row, 4);
    auto* premBtn = qobject_cast<QPushButton*>(w);
    const bool isPremiumRow = premBtn
        && !premBtn->property("premiumSeriesId").toString().isEmpty();

    if (m_filterAll && m_filterAll->isChecked()) return true;

    if (m_filterPremium && m_filterPremium->isChecked()) {
        return isPremiumRow || isSectionBreak;
    }

    if (m_filterDownloaded && m_filterDownloaded->isChecked()) {
        if (isSectionBreak) return true;
        if (isPremiumRow) {
            auto* statusItem = m_chapterTable->item(row, 3);
            return statusItem && statusItem->text() == QStringLiteral("Downloaded");
        }
        // PHASE 7+ TODO: non-Premium chapter row Downloaded detection requires
        // reaching into the column-1 ChapterDownloadIndicator state; for the
        // v1 chip row we hide non-Premium chapter rows under Downloaded.
        return false;
    }

    if (m_filterUnread && m_filterUnread->isChecked()) {
        if (isSectionBreak) return true;
        if (isPremiumRow) {
            auto* statusItem = m_chapterTable->item(row, 3);
            return !(statusItem && statusItem->text() == QStringLiteral("Downloaded"));
        }
        // PHASE 7+ TODO: non-Premium chapter Unread detection (same blocker
        // as Downloaded). For v1 we treat all non-Premium chapter rows as
        // Unread so the user at least sees them.
        return true;
    }

    if (m_filterLoose && m_filterLoose->isChecked()) {
        // PHASE 7+ TODO: loose-tail rows (WeebCentral fetch deferred) aren't
        // populated yet. For v1 only the section-break itself shows up.
        return isSectionBreak;
    }

    return true;
}

void ComicsTankoyomiDetailView::renderChapters(const QList<ChapterInfo>& chs)
{
    // TANKOYOMI_PREMIUM Phase 6 -- volume-row branch for catalog-backed
    // titles. Premium entries override the WeebCentral/ReadComics chapter
    // list with a curated per-volume layout. Loose-tail rows for ongoing
    // series are appended inside populateVolumeAndLooseTailTable.
    const auto premiumOpt = premiumEntryForCurrentSeries();
    if (premiumOpt) {
        populateVolumeAndLooseTailTable(*premiumOpt);
        return;
    }

    // Phase 6 I1: reset table to chapter-row layout. The Premium branch
    // above may have mutated columnCount + headers; restore the chapter-row
    // configuration so Premium -> non-Premium navigation paints correctly.
    m_chapterTable->setColumnCount(kColCount);
    m_chapterTable->setHorizontalHeaderLabels(QStringList{
        QString(), QString(), QStringLiteral("Chapter"), QStringLiteral("Date")});

    // Phase 5 review I2: each setItem(checkbox) on a fresh row fires
    // QTableWidget::itemChanged, which we connect to updateDownloadSelectedButton
    // (an O(N) walk). For a 200-chapter series that's O(N²) operations during
    // initial render. Block signals around the bulk fill, then settle the
    // selected-count once at the end. QSignalBlocker is RAII — unblocks on
    // scope exit.
    QSignalBlocker blocker(m_chapterTable);
    m_chapterTable->setRowCount(chs.size());
    for (int i = 0; i < chs.size(); ++i) {
        const auto& ch = chs[i];

        // col 0: checkbox (Phase 5 wires multi-select; Phase 4 inert)
        auto* cb = new QTableWidgetItem;
        cb->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        cb->setCheckState(Qt::Unchecked);
        m_chapterTable->setItem(i, kColCheckbox, cb);

        // col 1: ChapterDownloadIndicator (Phase 5 Task 31). Owned by the
        // table-cell via setCellWidget. Initial state derives from the on-disk
        // index — Downloaded if the chapter has a registered canonical path,
        // NotDownloaded otherwise. Queued/Downloading/Errored states are
        // pushed by Task 32+ MangaDownloader subscription.
        auto* ind = new ChapterDownloadIndicator(m_chapterTable);
        const bool onDisk = m_downloadIndex &&
            m_downloadIndex->filePathFor(m_currentPreview.source,
                                          m_currentPreview.id, ch.id).has_value();
        ind->setState(onDisk ? ChapterDownloadIndicator::State::Downloaded
                              : ChapterDownloadIndicator::State::NotDownloaded);
        applyOfflineStateToRow(i, ind, onDisk);
        QObject::connect(ind, &ChapterDownloadIndicator::clicked, this, [this, ch, ind]() {
            onIndicatorClicked(ch, ind);
        });
        m_chapterTable->setCellWidget(i, kColIndicator, ind);

        // col 2: chapter name
        auto* title = new QTableWidgetItem(ch.name);
        title->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_chapterTable->setItem(i, kColTitle, title);

        // col 3: ISO date
        const auto dt = ch.dateUpload > 0
            ? QDateTime::fromMSecsSinceEpoch(ch.dateUpload).toString(Qt::ISODate)
            : QString();
        auto* date = new QTableWidgetItem(dt);
        date->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_chapterTable->setItem(i, kColDate, date);
    }
    // Settle the N-selected button once after the bulk fill (signals are
    // unblocked when the QSignalBlocker dtor runs at scope exit).
    updateDownloadSelectedButton();
}

void ComicsTankoyomiDetailView::refreshChapterMarkers()
{
    // Phase 5 Task 31 + P7 Task 44 (root-change fallback per brainstorm
    // §14): re-derive each chapter's chip state from the on-disk index.
    // Only flips between Downloaded and NotDownloaded — Queued /
    // Downloading / Errored stay sticky between this pass and the next
    // explicit downloader-driven update (Task 32+).
    //
    // On a Comics root change, library records persist; chapter on-disk
    // state revalidates via MangaDownloadIndex::validateAll fired from
    // ComicsPage's rootFoldersChanged subscription (Task 43). Missing
    // chapters revert to NotDownloaded; users re-download per chapter or
    // via the Range modal. Auto-copy is explicitly deferred to v2.
    if (!m_downloadIndex || !m_chapterTable) return;
    for (int i = 0; i < m_currentChapters.size(); ++i) {
        auto* ind = qobject_cast<ChapterDownloadIndicator*>(
            m_chapterTable->cellWidget(i, kColIndicator));
        if (!ind) continue;
        const auto& ch = m_currentChapters[i];
        const bool onDisk = m_downloadIndex
            ->filePathFor(m_currentPreview.source, m_currentPreview.id, ch.id)
            .has_value();
        ind->setState(onDisk ? ChapterDownloadIndicator::State::Downloaded
                              : ChapterDownloadIndicator::State::NotDownloaded);
        applyOfflineStateToRow(i, ind, onDisk);
    }
}

void ComicsTankoyomiDetailView::applyOfflineStateToRow(int row, ChapterDownloadIndicator* ind, bool onDisk)
{
    const bool disabled = m_sourceOffline && !onDisk;
    if (ind) {
        ind->setEnabled(!disabled);
        ind->setToolTip(disabled
            ? QStringLiteral("Connect to the internet to download this chapter")
            : QString());
    }
    if (auto* title = m_chapterTable ? m_chapterTable->item(row, kColTitle) : nullptr) {
        title->setToolTip(disabled
            ? QStringLiteral("Connect to the internet to download this chapter")
            : QString());
        title->setFlags(disabled ? Qt::ItemIsSelectable : (Qt::ItemIsEnabled | Qt::ItemIsSelectable));
    }
}

void ComicsTankoyomiDetailView::onChapterRowClicked(int row, int /*col*/)
{
    if (row < 0 || row >= m_currentChapters.size()) return;
    const auto ch = m_currentChapters[row];
    if (openDownloadedChapter(ch)) return;
    if (m_sourceOffline) return;
    QList<ChapterInfo> single;
    single.append(ch);
    dispatchDownload(single);
}

void ComicsTankoyomiDetailView::onChapterContextMenu(const QPoint& /*pos*/)
{
    // Phase 5/6 wires per-chapter context menu.
}

void ComicsTankoyomiDetailView::onSeriesHeaderContextMenu(const QPoint& globalPos)
{
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 37 — series-
    // level context menu attached to the hero widget. Pause/Resume/Restart/
    // Retry/Cancel only enable when there's an active record in
    // MangaDownloader; Remove always available when the series is in the
    // library. Skipped entirely when the series isn't in the library yet
    // (Right-click on a search-result-detail hero would be a misclick).
    if (!isInLibrary() || !m_tyLibrary || !m_downloader) return;
    const auto rec = m_tyLibrary->get(m_currentPreview.source, m_currentPreview.id);
    const auto activeRec = m_downloader->recordForSeries(rec.sourceId, rec.title);

    // Phase 5 review I3: route through ContextMenuHelper to inherit the
    // ParityContextMenu styling other Comics-side menus use (background
    // #1e1e1e, 12% border, rounded corners). Otherwise this menu would
    // render with default Qt chrome and look visually inconsistent.
    auto* menu = ContextMenuHelper::createMenu(this);
    auto* pauseAct   = menu->addAction(QStringLiteral("Pause series"));
    auto* resumeAct  = menu->addAction(QStringLiteral("Resume series"));
    auto* restartAct = menu->addAction(QStringLiteral("Restart all chapters"));
    auto* retryAct   = menu->addAction(QStringLiteral("Retry failed chapters"));
    auto* cancelAct  = menu->addAction(QStringLiteral("Cancel all"));
    menu->addSeparator();
    // Remove is the destructive action → addDangerAction mirrors the
    // existing Comics context-menu precedent at ComicsPage.cpp:327/1049/1115.
    auto* removeAct  = ContextMenuHelper::addDangerAction(menu, QStringLiteral("Remove from library"));

    const bool hasActive = !activeRec.id.isEmpty();
    pauseAct->setEnabled(hasActive && !activeRec.paused);
    resumeAct->setEnabled(hasActive && activeRec.paused);
    restartAct->setEnabled(hasActive);
    retryAct->setEnabled(hasActive);
    cancelAct->setEnabled(hasActive);

    const auto chosen = menu->exec(globalPos);
    menu->deleteLater();   // createMenu returns heap-allocated menu
    if (!chosen) return;
    if (chosen == pauseAct)        m_downloader->pauseSeries(activeRec.id);
    else if (chosen == resumeAct)  m_downloader->resumeSeries(activeRec.id);
    else if (chosen == restartAct) m_downloader->restartSeries(activeRec.id);
    else if (chosen == retryAct)   m_downloader->retryFailedChapters(activeRec.id);
    else if (chosen == cancelAct)  m_downloader->cancelDownload(activeRec.id);
    else if (chosen == removeAct)  onAddRemoveClicked();
}

// ── Phase 5 Task 36: Range modal + multi-select + shared dispatch ───────────

void ComicsTankoyomiDetailView::updateDownloadSelectedButton()
{
    if (!m_chapterTable || !m_downloadSelectedBtn) return;
    int n = 0;
    for (int i = 0; i < m_chapterTable->rowCount(); ++i) {
        if (auto* item = m_chapterTable->item(i, kColCheckbox))
            if (item->checkState() == Qt::Checked) ++n;
    }
    m_downloadSelectedBtn->setText(QStringLiteral("Download %1 selected").arg(n));
    m_downloadSelectedBtn->setVisible(n > 0);
}

void ComicsTankoyomiDetailView::onDownloadSelectedClicked()
{
    QList<ChapterInfo> picks;
    for (int i = 0; i < m_chapterTable->rowCount(); ++i) {
        if (auto* it = m_chapterTable->item(i, kColCheckbox))
            if (it->checkState() == Qt::Checked && i < m_currentChapters.size())
                picks.append(m_currentChapters[i]);
    }
    if (picks.isEmpty()) return;
    dispatchDownload(picks);
}

void ComicsTankoyomiDetailView::onDownloadRangeClicked()
{
    if (m_currentChapters.isEmpty()) return;
    // ChapterRangeDialog ctor is (allChapters, alreadyHandledIds, parent);
    // v1 has no already-handled tracking yet (downloaded-state filtering
    // would belong to a later phase if we want the modal to skip chapters
    // already on disk), so pass an empty QSet.
    QSet<QString> alreadyHandled;
    ChapterRangeDialog dlg(m_currentChapters, alreadyHandled, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const auto picks = dlg.selectedChapters();
    if (picks.isEmpty()) return;
    dispatchDownload(picks);
}

bool ComicsTankoyomiDetailView::openDownloadedChapter(const ChapterInfo& ch)
{
    if (!m_downloadIndex || !m_tyLibrary) return false;
    const auto pathOpt = m_downloadIndex->filePathFor(m_currentPreview.source,
                                                       m_currentPreview.id, ch.id);
    if (!pathOpt || !QFileInfo::exists(*pathOpt)) return false;

    const auto rec = m_tyLibrary->get(m_currentPreview.source, m_currentPreview.id);
    if (rec.title.isEmpty()) return false;

    QStringList list;
    QSet<QString> seen;
    for (const auto& chapter : m_currentChapters) {
        const auto p = m_downloadIndex->filePathFor(m_currentPreview.source,
                                                     m_currentPreview.id, chapter.id);
        if (p && QFileInfo::exists(*p) && !seen.contains(*p)) {
            list.append(*p);
            seen.insert(*p);
        }
    }
    if (!seen.contains(*pathOpt))
        list.prepend(*pathOpt);

    emit openComicRequested(*pathOpt, list, rec.title.isEmpty() ? m_currentPreview.title : rec.title);
    return true;
}

void ComicsTankoyomiDetailView::dispatchDownload(const QList<ChapterInfo>& picks)
{
    if (picks.isEmpty() || !m_tyLibrary || !m_downloader) return;
    std::optional<ComicsLibraryRecord> added;
    if (!isInLibrary()) {
        added = ensureAddedForDownload();
    }
    if (added && !added->canonicalSeriesPath.isEmpty()) {
        Toast::show(window(),
                    QStringLiteral("Added %1 to your library").arg(m_currentPreview.title));
    }
    const auto rec = m_tyLibrary->get(m_currentPreview.source, m_currentPreview.id);
    if (rec.title.isEmpty() || rec.canonicalSeriesPath.isEmpty()) return;
    // Pass canonicalSeriesPath directly as destinationPath; MangaDownloader
    // writes chapters into destinationPath without an extra series-name
    // segment (per the post-2026-05-15 P0-S2b' fix at MangaDownloader.cpp).
    // This works for both happy-path and collision-disambiguated cases
    // (Title / Title (Source) / Title (Source hash)) because the library
    // record's canonicalSeriesPath already encodes the on-disk folder name
    // exactly via `rootFolder + "/" + seriesFolderName`.
    m_downloader->startDownload(rec.title, rec.sourceId, picks,
                                  rec.canonicalSeriesPath, QStringLiteral("cbz"),
                                  rec.coverPath);
}

void ComicsTankoyomiDetailView::onIndicatorClicked(const ChapterInfo& ch,
                                                     ChapterDownloadIndicator* ind)
{
    if (openDownloadedChapter(ch)) return;
    if (m_sourceOffline) {
        if (ind) {
            ind->setEnabled(false);
            ind->setToolTip(QStringLiteral("Connect to the internet to download this chapter"));
        }
        return;
    }

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 35.
    // Codex §15 wiring rule: the auto-add runs BEFORE startDownload on the
    // GUI thread so UI side-effects (Toast + library tile insert) are
    // sequenced cleanly off the downloader's signal path. Toast fires only
    // when an add actually happened — already-in-library series don't
    // trigger a duplicate "Added to your library" banner.
    std::optional<ComicsLibraryRecord> added;
    if (!isInLibrary()) {
        added = ensureAddedForDownload();
    }
    if (added && !added->canonicalSeriesPath.isEmpty()) {
        Toast::show(window(),
                    QStringLiteral("Added %1 to your library").arg(m_currentPreview.title));
    }

    if (!m_tyLibrary || !m_downloader) return;
    const auto rec = m_tyLibrary->get(m_currentPreview.source, m_currentPreview.id);
    if (rec.title.isEmpty() || rec.canonicalSeriesPath.isEmpty()) return;

    QList<ChapterInfo> single;
    single.append(ch);
    // See dispatchDownload() above — canonicalSeriesPath is the on-disk
    // destination, MangaDownloader no longer appends `/seriesTitle`.
    m_downloader->startDownload(rec.title, rec.sourceId, single,
                                  rec.canonicalSeriesPath, QStringLiteral("cbz"),
                                  rec.coverPath);

    if (ind) ind->setState(ChapterDownloadIndicator::State::Queued);
}

// ── Task 25: Add/Remove silent-bookmark button ──────────────────────────────

bool ComicsTankoyomiDetailView::isInLibrary() const
{
    return m_tyLibrary &&
           m_tyLibrary->contains(m_currentPreview.source, m_currentPreview.id);
}

bool ComicsTankoyomiDetailView::folderCandidateCollides(const QString& folderPath) const
{
    if (!m_tyLibrary) return QFileInfo::exists(folderPath);
    const QString candidate = QDir::cleanPath(QFileInfo(folderPath).absoluteFilePath());
    for (const auto& rec : m_tyLibrary->all()) {
        const QString recPath = QDir::cleanPath(QFileInfo(rec.canonicalSeriesPath).absoluteFilePath());
        if (recPath == candidate &&
            (rec.sourceId != m_currentPreview.source || rec.seriesId != m_currentPreview.id))
            return true;
    }
    if (!QFileInfo::exists(folderPath)) return false;
    const auto meta = sidecar::read(folderPath);
    return !meta || meta->sourceId != m_currentPreview.source || meta->seriesId != m_currentPreview.id;
}

QString ComicsTankoyomiDetailView::uniqueSeriesFolderName(const QString& root) const
{
    const QString base = sanitiseFilename(m_currentPreview.title);
    const QString sourceName = mangaSourceDisplayName(m_currentPreview.source);
    const QString withSource = sanitiseFilename(
        QStringLiteral("%1 (%2)").arg(m_currentPreview.title, sourceName));
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(
        m_currentPreview.id.toUtf8(), QCryptographicHash::Md5).toHex().left(8));
    const QString withHash = sanitiseFilename(
        QStringLiteral("%1 (%2 %3)").arg(m_currentPreview.title, sourceName, hash));

    for (const QString& candidate : {base, withSource, withHash}) {
        if (!folderCandidateCollides(QDir(root).filePath(candidate)))
            return candidate;
    }
    return withHash;
}

std::optional<ComicsLibraryRecord> ComicsTankoyomiDetailView::addCurrentToLibrary()
{
    if (!m_tyLibrary) return std::nullopt;

    const auto roots = m_bridge ? m_bridge->rootFolders("comics") : QStringList{};
    if (roots.isEmpty()) {
        Toast::show(window(),
                    QStringLiteral("Add a Comics folder before adding to library"));
        return std::nullopt;
    }

    ComicsLibraryRecord rec;
    rec.sourceId         = m_currentPreview.source;
    rec.seriesId         = m_currentPreview.id;
    rec.title            = m_currentPreview.title;
    rec.origin           = "tankoyomi";
    rec.rootFolder       = roots.first();
    // TANKOYOMI_PREMIUM Phase 9 adopt path: if a Premium-catalog match
    // exists AND exactly one folder-imported series shares the normalized
    // title, reuse that folder's canonicalSeriesPath instead of creating
    // a new disambiguated folder. ComicsPage owns m_folderSeries; the
    // lookup is injected via setAdoptLookup. Per Codex section 22:
    // adopt, do not migrate. No file move/rename.
    QString adoptedPath;
    if (m_premiumCatalog && m_adoptLookup) {
        const auto premiumEntry = m_premiumCatalog->entryForTitle(m_currentPreview.title);
        if (premiumEntry) {
            adoptedPath = m_adoptLookup(premiumEntry->title);
        }
    }
    if (!adoptedPath.isEmpty()) {
        rec.canonicalSeriesPath = adoptedPath;
        rec.seriesFolderName    = QFileInfo(adoptedPath).fileName();
        qDebug().noquote() << QStringLiteral("[ComicsTankoyomiDetailView] adopting existing folder for")
                           << m_currentPreview.title << QStringLiteral("at") << adoptedPath;
    } else {
        rec.seriesFolderName    = uniqueSeriesFolderName(rec.rootFolder);
        rec.canonicalSeriesPath = QDir(rec.rootFolder).filePath(rec.seriesFolderName);
    }
    rec.coverPath        = MangaPosterCache::existingPath(rec.sourceId, rec.seriesId);

    if (m_currentDetail.has_value()) {
        rec.detailCache = *m_currentDetail;
    } else {
        rec.detailCache.preview = m_currentPreview;
    }

    rec.addedAt         = QDateTime::currentMSecsSinceEpoch();
    rec.lastValidatedAt = rec.addedAt;
    m_tyLibrary->add(rec);

    if (!rec.canonicalSeriesPath.isEmpty()) {
        SidecarMeta sm;
        sm.sourceId  = rec.sourceId;
        sm.seriesId  = rec.seriesId;
        sm.title     = rec.title;
        sm.createdAt = rec.addedAt;
        sidecar::write(rec.canonicalSeriesPath, sm);
    }

    m_addRemoveBtn->setText("Remove from library");
    return rec;
}

std::optional<ComicsLibraryRecord> ComicsTankoyomiDetailView::ensureAddedForDownload()
{
    if (isInLibrary())
        return m_tyLibrary->get(m_currentPreview.source, m_currentPreview.id);
    return addCurrentToLibrary();
}

void ComicsTankoyomiDetailView::onAddRemoveClicked()
{
    if (!m_tyLibrary) return;

    if (isInLibrary()) {
        // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 6 Task 41 —
        // "Remove (keep files)" vs "Remove and delete files" confirm.
        // Mirrors Netflix overhaul Phase 7 cancel-then-evict sequencing.
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("Remove from library"));
        box.setText(QStringLiteral("Remove %1 from your library?")
                        .arg(m_currentPreview.title));
        auto* keepBtn   = box.addButton(QStringLiteral("Remove (keep files)"),
                                          QMessageBox::AcceptRole);
        auto* deleteBtn = box.addButton(QStringLiteral("Remove and delete files"),
                                          QMessageBox::DestructiveRole);
        auto* cancelBtn = box.addButton(QMessageBox::Cancel);
        // Code-quality review Critical: hitting Enter must NEVER delete files.
        // Qt 6 QMessageBox default-button selection with role-typed buttons
        // is platform-dependent; pin keepBtn as default and Cancel as escape
        // so Enter/Esc behave predictably on every platform.
        box.setDefaultButton(keepBtn);
        box.setEscapeButton(cancelBtn);
        box.exec();
        const auto* clicked = box.clickedButton();
        if (clicked != keepBtn && clicked != deleteBtn) return;

        const auto rec = m_tyLibrary->get(m_currentPreview.source,
                                            m_currentPreview.id);

        // Cancel any in-flight downloads first. recordForSeries matches
        // by (sourceId, seriesTitle) — NOT seriesId — per MangaDownloader.h
        // contract (the downloader record doesn't carry a scraper id).
        if (m_downloader) {
            const auto activeRec = m_downloader->recordForSeries(rec.sourceId,
                                                                   rec.title);
            if (!activeRec.id.isEmpty())
                m_downloader->cancelDownload(activeRec.id);
        }

        // Drop the library record + sidecar (sidecar is a recovery hint,
        // so it must go with the record to avoid a ghost claim).
        m_tyLibrary->remove(rec.sourceId, rec.seriesId);
        if (!rec.canonicalSeriesPath.isEmpty()) {
            QFile::remove(QDir(rec.canonicalSeriesPath)
                              .filePath(sidecar::kFileName));
        }

        // Evict from the on-disk chapter index so per-chapter chips
        // don't stay green after a delete.
        if (m_downloadIndex)
            m_downloadIndex->evictBySeries(rec.sourceId, rec.seriesId);

        if (clicked == deleteBtn && !rec.canonicalSeriesPath.isEmpty())
            QDir(rec.canonicalSeriesPath).removeRecursively();

        m_addRemoveBtn->setText(QStringLiteral("Add to library"));
        return;
    }

    // Phase 5 review I1: empty Comics root → halt before writing a half-
    // populated library record. Without this, dispatchDownload would Toast
    // "Added X to your library" but startDownload's empty-canonicalPath
    // guard would silently no-op, leaving a record nobody can download into.
    const auto roots = m_bridge ? m_bridge->rootFolders("comics") : QStringList{};
    if (roots.isEmpty()) {
        Toast::show(window(),
                    QStringLiteral("Add a Comics folder before adding to library"));
        return;
    }

    ComicsLibraryRecord rec;
    rec.sourceId         = m_currentPreview.source;
    rec.seriesId         = m_currentPreview.id;
    rec.title            = m_currentPreview.title;
    rec.origin           = "tankoyomi";

    rec.rootFolder       = roots.first();
    rec.seriesFolderName = uniqueSeriesFolderName(rec.rootFolder);
    rec.canonicalSeriesPath = QDir(rec.rootFolder).filePath(rec.seriesFolderName);
    // Cover path resolves later — Phase 5 ties this to the poster cache.
    rec.coverPath        = MangaPosterCache::existingPath(rec.sourceId, rec.seriesId);

    if (m_currentDetail.has_value()) {
        rec.detailCache = *m_currentDetail;
    } else {
        rec.detailCache.preview = m_currentPreview;
    }

    rec.addedAt         = QDateTime::currentMSecsSinceEpoch();
    rec.lastValidatedAt = rec.addedAt;
    m_tyLibrary->add(rec);

    if (!rec.canonicalSeriesPath.isEmpty()) {
        SidecarMeta sm;
        sm.sourceId  = rec.sourceId;
        sm.seriesId  = rec.seriesId;
        sm.title     = rec.title;
        sm.createdAt = rec.addedAt;
        sidecar::write(rec.canonicalSeriesPath, sm);
    }

    m_addRemoveBtn->setText("Remove from library");
}
