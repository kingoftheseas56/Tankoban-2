#include "ComicsPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "SeriesView.h"
#include "core/CoreBridge.h"
#include "core/LibraryScanner.h"
#include "core/ScannerUtils.h"
#include "core/manga/ComicsTankoyomiLibrary.h"
#include "core/manga/MangaDownloadIndex.h"
#include "core/manga/MangaDownloader.h"
#include "core/manga/PremiumCatalog.h"
#include "core/manga/TorrentRequestLedger.h"
#include "core/manga/TorrentVolumeProvider.h"
#include "core/net/NetSeam.h"
#include "core/manga/MangaTransferCoordinator.h"
#include "core/manga/MangaResult.h"
#include "core/manga/MangaScraper.h"
#include "core/manga/MangaSourceRegistry.h"
#include "core/manga/NyaaRuntimeSource.h"
#include "core/manga/WeebCentralVolumePacker.h"
#include "core/manga/anilist/AniListCache.h"
#include "core/manga/anilist/AniListClient.h"
#include "core/manga/anilist/AniListTypes.h"
#include "core/manga/anilist/AniListVolumeMapper.h"
#include "core/manga/mangaupdates/MangaUpdatesClient.h"
#include "core/manga/mangaupdates/VolumeMetadataResolver.h"
#include "core/manga/MangaCatalogTypes.h"
#include "core/manga/LocalMangaCatalogLoader.h"
#include "core/manga/WesternCatalogLoader.h"
#include "core/manga/WesternVolumeDownloader.h"
#include "core/manga/ReadComicsScraper.h"
#include "core/manga/ReadAllComicsScraper.h"
#include "core/manga/mangafire/MangaFireCatalogClient.h"
#include "core/manga/mangafire/MangaWeebCentralResolver.h"
#include "core/torrent/TorrentClient.h"
#include "core/torrent/TorrentEngine.h"
#include "comics/ComicsTankoyomiSearchWidget.h"
#include "comics/ComicsSeriesView.h"
#include "comics/ComicsSourcesPanel.h"
#include "comics/VolumeTile.h"

#include "ui/ContextMenuHelper.h"
#include "ui/readers/comic_progress_key.h"
#include "ui/widgets/FadingStackedWidget.h"
#include "ui/widgets/LibraryListView.h"
#include "ui/widgets/Toast.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QPointer>
#include <memory>
#include <QCoreApplication>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QDebug>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QIcon>
#include <QScrollArea>
#include <QScrollBar>
#include <QMetaObject>
#include <QSettings>
#include <QInputDialog>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QTextStream>

// COMICS_OPEN_TRACE (Agent 1, 2026-05-24 evening debug session). Timestamped
// trace at the key phase boundaries of a series open. Sibling helper in
// ComicsSeriesView.cpp; both write to %TEMP%/comics_open_trace.log. Stripped
// after the bottleneck is identified + fixed. Defined here at file scope so
// the constructor-side lambdas (searchSucceeded enrichment listener etc.)
// see it; otherwise the helper would be unresolved at the constructor lines.
namespace {
void comicsOpenTrace(const QString& event)
{
    static const QString path = QDir::temp().absoluteFilePath(QStringLiteral("comics_open_trace.log"));
    QFile f(path);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentMSecsSinceEpoch() << '\t' << event << '\n';
    }
}
} // namespace
#include <QFileInfo>
#include <QRegularExpression>
#include <QCollator>
#include <QShortcut>
#include <QShowEvent>
#include <QMessageBox>

// P4-3: COMIC_EXTS covers all reader-supported archive formats. Engine
// (ArchiveReader) and library scanner both handle CBZ + CBR + RAR.
static const QStringList COMIC_EXTS = {"*.cbz", "*.cbr", "*.rar"};
static constexpr const char* TANKOYOMI_PREMIUM_SOURCE_ID = "tankoyomi_premium";
static constexpr const char* WEEBCENTRAL_PACKER_SOURCE_ID = "weebcentral";
static constexpr const char* MANGAFIRE_CATALOG_SOURCE_ID = "mangafire_catalog";
static constexpr const char* GETCOMICS_SOURCE_ID = "getcomics";

namespace {
// PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- construct a LayerEntry for
// the Comics page. All emit sites call this helper so the pageId is always
// "comics" and the struct fields are assembled consistently.
tankoban::ui::LayerEntry makeComicsLayer(const QString& kind, const QString& label,
                                         const QJsonObject& blob = {}) {
    return tankoban::ui::LayerEntry{
        QStringLiteral("comics"),
        kind,
        label,
        blob
    };
}

int parseAnilistSeriesId(const QString& raw)
{
    QString s = raw.trimmed();
    if (s.startsWith(QStringLiteral("anilist_")))
        s = s.mid(QStringLiteral("anilist_").size());
    bool ok = false;
    const int id = s.toInt(&ok);
    return ok ? id : 0;
}

QJsonObject mangaDownloadEntryJson(const MangaDownloadIndex::Entry& e)
{
    QJsonObject obj;
    obj[QStringLiteral("sourceId")] = e.sourceId;
    obj[QStringLiteral("seriesId")] = e.seriesId;
    obj[QStringLiteral("chapterId")] = e.chapterId;
    obj[QStringLiteral("volume")] = e.volumeNumber;
    obj[QStringLiteral("canonicalPath")] = e.canonicalPath;
    obj[QStringLiteral("fileSizeBytes")] = static_cast<double>(e.fileSizeBytes);
    obj[QStringLiteral("addedAt")] = static_cast<double>(e.addedAt);

    QJsonArray served;
    for (const QString& key : e.servedChapterKeys)
        served.append(key);
    obj[QStringLiteral("servedChapterKeys")] = served;
    return obj;
}

QJsonObject mediaPreviewJson(const tankoban::manga::anilist::MediaPreview& p)
{
    QJsonObject obj;
    obj[QStringLiteral("anilistId")] = p.anilistId;
    obj[QStringLiteral("title")] = p.title;
    obj[QStringLiteral("year")] = p.yearStarted;
    obj[QStringLiteral("format")] = p.format;
    obj[QStringLiteral("status")] = p.status;
    obj[QStringLiteral("poster")] = p.coverThumbUrl;
    obj[QStringLiteral("banner")] = p.bannerUrl;
    return obj;
}
} // namespace

static QString fandomSeriesSlugFromTitle(const QString& title);

ComicsPage::ComicsPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setObjectName("comics");
    qRegisterMetaType<SeriesInfo>("SeriesInfo");
    qRegisterMetaType<QList<SeriesInfo>>("QList<SeriesInfo>");
    qRegisterMetaType<tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey>(
        "tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey");

    buildUI();

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — Tankoyomi-origin record
    // store. Must exist before the scanner so the initial scan can be
    // primed with the current claimed-path set (suppresses double-tiling
    // of folders already owned by a Tankoyomi record).
    m_tyLibrary = new ComicsTankoyomiLibrary(&m_bridge->store(), this);
    connect(m_tyLibrary, &ComicsTankoyomiLibrary::libraryChanged,
            this, &ComicsPage::onTankoyomiLibraryChanged);

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 18+20 -- shared NAM +
    // scraper registry. Scraper registry is retained because MangaDownloader
    // still drives WeebCentral chapter downloads; the search widget now
    // bypasses scrapers entirely (Phase 9 AniList backbone).
    m_nam = tankoban::net::NetSeam::instance()->createManager(this, QStringLiteral("comics-page"));
    m_sourceRegistry = new MangaSourceRegistry(m_nam, this);

    // TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- new providers. AniList
    // client + cache (Phases 1+2) drive the search widget and the new
    // series-detail view; NyaaRuntimeSource (Phase 4) feeds runtime torrent
    // search rows into ComicsSourcesPanel; WeebCentralVolumePacker (Phase 5)
    // synthesizes off-catalog volume packs from WeebCentral chapter fetches.
    m_anilistClient = new tankoban::manga::anilist::AniListClient(m_nam, this);
    m_anilistCache  = new tankoban::manga::anilist::AniListCache(
                          m_bridge->dataDir() + QStringLiteral("/anilist_cache"), this);

    // COMICS_MANGAFIRE_ON_DEMAND_FETCH 2026-05-23 (Agent 1). Live MangaFire
    // scraper for search-first architecture. Sleeping until dispatchCatalogResolve
    // fails the local-first lookup; then fetches one series from mangafire.to
    // and writes data/mangafire_catalog/<slug>.json so the next resolve hits.
    m_mangafireClient = new tankoban::manga::mangafire::MangaFireCatalogClient(
                            m_nam, this);
    connect(m_mangafireClient,
            &tankoban::manga::mangafire::MangaFireCatalogClient::catalogReady,
            this, &ComicsPage::onMangaFireCatalogReady);
    connect(m_mangafireClient,
            &tankoban::manga::mangafire::MangaFireCatalogClient::catalogFailed,
            this, &ComicsPage::onMangaFireCatalogFailed);
    m_wcResolver = new tankoban::manga::mangafire::MangaWeebCentralResolver(
        m_nam, this);
    connect(m_wcResolver,
            &tankoban::manga::mangafire::MangaWeebCentralResolver::viable,
            this, &ComicsPage::onWcResolverViable);
    connect(m_wcResolver,
            &tankoban::manga::mangafire::MangaWeebCentralResolver::skip,
            this, &ComicsPage::onWcResolverSkip);
    // NOTE: the seriesClassified -> ComicsSeriesView::onSeriesClassified wire
    // lives below, AFTER m_tyVolumeSeriesView is constructed (~line 384).
    // Connecting here (receiver still null) silently no-ops the connection,
    // which is exactly what kept RAW-SCAN tags + the Volume X row from ever
    // rendering even though classification fired correctly.
    m_mangaUpdatesClient = new tankoban::manga::mangaupdates::MangaUpdatesClient(
        m_anilistClient ? m_anilistClient->networkManager() : nullptr, this);
    m_volumeResolver = new tankoban::manga::mangaupdates::VolumeMetadataResolver(
        m_mangaUpdatesClient, m_anilistCache, this);
    connect(m_volumeResolver,
            &tankoban::manga::mangaupdates::VolumeMetadataResolver::resolved,
            this, &ComicsPage::onVolumeMetadataResolved);
    connect(m_volumeResolver,
            &tankoban::manga::mangaupdates::VolumeMetadataResolver::unresolved,
            this, &ComicsPage::onVolumeMetadataUnresolved);
    const QString trustJsonPath = QCoreApplication::applicationDirPath()
                                + QStringLiteral("/resources/manga_uploader_trust.json");
    m_nyaaRuntime = new tankoban::manga::NyaaRuntimeSource(m_nam, trustJsonPath, this);

    // WEEBCENTRAL_IDENTITY_PIVOT Tasks 9+10 (2026-05-19): search widget now
    // routes through MangaSourceRegistry (WeebCentral scraper) instead of
    // AniListClient. seriesActivated(MediaPreview) replaced by
    // resultPicked(MangaResult).
    m_searchTakeover = new ComicsTankoyomiSearchWidget(m_sourceRegistry, m_nam, this);
    m_stack->addWidget(m_searchTakeover);

    connect(m_searchTakeover, &ComicsTankoyomiSearchWidget::backRequested,
            this, [this]() {
                // Stream-bar parity 2026-05-22: defensive reset when the user
                // bails out of the search-takeover surface mid-flight — kills
                // the busy spinner and any lingering history dropdown that
                // somehow survived the focus-out path. Mirrors StreamPage's
                // showBrowse defensive reset at StreamPage.cpp:1955-1957.
                setSearchBusy(false);
                hideSearchHistoryDropdown();
                showLibraryMode();
            });
    connect(m_searchTakeover, &ComicsTankoyomiSearchWidget::resultPicked,
            this, &ComicsPage::onSearchResultActivated);

    // Scraper error toasts retained -- downloads in flight still route
    // through MangaDownloader for legacy chapter pulls. Stream-bar parity
    // 2026-05-22: also flip the busy spinner off on either successful
    // search finish or error so the indeterminate progress bar doesn't
    // pin forever after the scraper returns. Mirrors StreamPage's
    // MetaAggregator hook pattern at StreamPage.cpp:305-312.
    for (auto* s : m_sourceRegistry->scrapers()) {
        connect(s, &MangaScraper::errorOccurred, this, [this, s](const QString& msg) {
            Q_UNUSED(msg);
            setSearchBusy(false);
            QWidget* anchor = window() ? window() : this;
            Toast::show(anchor, QStringLiteral("%1 didn't respond").arg(s->sourceName()));
        });
        connect(s, &MangaScraper::searchFinished, this,
                [this](const QList<MangaResult>&) {
                    setSearchBusy(false);
                });
    }

    // COMICS_WESTERN_ADD 2026-06-01 (Agent 2). Live Western (RCO) search pick.
    // Grab the registry-owned ReadComicsScraper so onSearchResultActivated can
    // route an RCO result into fetchWesternSeries() (live page-scrape -> schema-v2
    // JSON), and wire westernSeriesReady to the render-only open. The scraper is
    // owned by the registry; this is a non-owning observer pointer.
    for (auto* s : m_sourceRegistry->scrapers()) {
        if (s->sourceId() == QLatin1String("readcomicsonline")) {
            m_readComicsScraper = qobject_cast<ReadComicsScraper*>(s);
        } else if (s->sourceId() == QLatin1String("readallcomics")) {
            m_readAllComicsScraper = qobject_cast<ReadAllComicsScraper*>(s);
        }
    }
    if (m_readComicsScraper) {
        connect(m_readComicsScraper, &ReadComicsScraper::westernSeriesReady,
                this, [this](const QJsonObject& seriesJson) {
            setSearchBusy(false);
            const auto catalog =
                tankoban::manga::WesternCatalogLoader::loadFromJsonObject(seriesJson);
            if (catalog.seriesId.isEmpty()) {
                // Defense-in-depth: only reachable now if the fetched JSON is
                // truly malformed (no seriesId). Never leave the series-view
                // "Loading" overlay up — bounce back to the Western grid so the
                // user is not stranded (2026-06-02 hang fix).
                qInfo("ComicsPage: westernSeriesReady -> empty/invalid series, returning to grid");
                showWesternMode();
                return;
            }
            // Stash the raw JSON verbatim so addWesternToLibraryRequested can
            // persist exactly what was fetched (synopsis + editions + cover).
            m_pendingWesternJson     = seriesJson;
            m_pendingWesternSeriesId = catalog.seriesId;
            // Already on the shelf if a baked file with this seriesId exists.
            const QString shelfPath =
                QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
                    .absoluteFilePath(catalog.seriesId + QStringLiteral(".json"));
            const bool onShelf = QFile::exists(shelfPath);
            // jsonPath empty until persisted (the restore path re-fetches).
            openWesternSeriesFromCatalog(catalog, QString(), onShelf);
        });
    }

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 26 -- manga downloader.
    // Single downloader owned by this page; the old TankoyomiPage duplicate
    // was retired in Phase 8. Detail view replaced by ComicsSeriesView below
    // (TANKOYOMI_VOLUME_PIVOT Phase 9).
    m_mangaDownloader = new MangaDownloader(&m_bridge->store(), this);
    for (auto* s : m_sourceRegistry->scrapers()) {
        m_mangaDownloader->setScraper(s->sourceId(), s);
    }

    // TANKOYOMI_PREMIUM Phase 1 -- Premium catalog loader. Still in use:
    // catalog hits surface as tier-1 rows inside ComicsSourcesPanel and the
    // adopt-existing-folder lookup walks it. The detail-view-side injection
    // call (setPremiumCatalog) is gone with the legacy view; the new
    // ComicsSeriesView receives the catalog pointer via its ctor.
    const QString catalogsDir = QCoreApplication::applicationDirPath()
                              + QStringLiteral("/resources/manga_premium_catalogs");
    m_premiumCatalog = new tankoban::manga::premium::PremiumCatalog(catalogsDir, this);

    // TANKOYOMI_PREMIUM Phase 3 -- persistent request ledger lives next to
    // CoreBridge's dataDir tree. Ledger is engine-independent so it spins up
    // here; the TorrentVolumeProvider waits for setTorrentClient().
    const QString premiumLedgerPath = m_bridge->dataDir()
                                    + QStringLiteral("/manga_premium_requests.json");
    m_premiumLedger = new tankoban::manga::premium::TorrentRequestLedger(premiumLedgerPath, this);

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 34 -- drive the
    // DOWNLOADING chip on Tankoyomi-origin tiles from the downloader's state
    // stream.
    connect(m_mangaDownloader, &MangaDownloader::downloadUpdated,
            this, [this](const QString& recordId) {
        refreshTileChips();
        updateWesternMangaStatus(recordId);   // RCO Western status (no-op otherwise)
    });
    connect(m_mangaDownloader, &MangaDownloader::downloadCompleted,
            this, [this](const QString&) { refreshTileChips(); });
    connect(m_mangaDownloader, &MangaDownloader::chapterCompleted,
            this, &ComicsPage::onChapterCompleted);
    // COMICS_WESTERN readallcomics-as-page-source (2026-06-03): a completed
    // readallcomics chapter that is the in-flight Western download flips the
    // Western tile to Read (via the proven provider path) + updates the Sources
    // panel. Separate connect from onChapterCompleted (which registers
    // Tankoyomi-library chapters and skips Western shelf series — they are not
    // in m_tyLibrary). The rco catalog drives browse; readallcomics drives the
    // actual page fetch, so a finished Western download arrives under that source.
    connect(m_mangaDownloader, &MangaDownloader::chapterCompleted, this,
            [this](const QString& source, const QString& seriesTitle,
                   const QString& /*chapterId*/, const QString& finalPath, qint64) {
        if (source != QLatin1String("readallcomics")) return;
        if (m_pendingWesternSeriesId.isEmpty() || m_pendingWesternDownloadVolume <= 0) return;
        if (seriesTitle != m_currentDetailSeriesTitle || finalPath.isEmpty()) return;
        onProviderVolumeCompleted(m_pendingWesternSeriesId, m_pendingWesternDownloadVolume,
            finalPath, static_cast<int>(PendingVolumeSourceKind::WesternGetComics));
        if (m_tyVolumeSeriesView)
            m_tyVolumeSeriesView->updateWesternDownloadStatus(
                m_westernDownloadEdition, tr("Downloaded - open to read"));
    });

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 30 -- on-disk
    // chapter index. Persists at <appDataDir>/manga_downloads_index.json.
    m_mangaDownloadIndex = new MangaDownloadIndex(&m_bridge->store(), this);

    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- DOWNLOADED + BOOKMARKED
    // landing sections refresh when either source mutates. Bookmark mutation
    // (add/remove from ComicsSeriesView) fires bookmarksChanged. Download
    // index mutations (registerChapter/registerVolume/evictBy*) fire
    // entriesChanged. Both land on the GUI thread via Qt::AutoConnection
    // because both signal sources live on this thread (ctor here, mutations
    // come from MangaDownloader/TorrentVolumeProvider which already marshal
    // back to GUI via QueuedConnection elsewhere).
    connect(m_mangaDownloadIndex, &MangaDownloadIndex::entriesChanged,
            this, [this]() { refreshLibraryStrips(); });
    connect(m_anilistCache, &tankoban::manga::anilist::AniListCache::bookmarksChanged,
            this, [this]() { refreshLibraryStrips(); });

    // TANKOYOMI_VOLUME_PIVOT Phase 5 -- WeebCentralVolumePacker. Requires
    // m_mangaDownloadIndex to be live first (Phase 9 wires registerVolume
    // off the volumeCompleted signal in a follow-up phase). Picks the first
    // available scraper as its source -- v1 packer is WeebCentral-specific
    // anyway; if WeebCentral isn't in the registry, the packer simply never
    // gets dispatch hits routed to it.
    {
        MangaScraper* wcScraper = nullptr;
        for (auto* s : m_sourceRegistry->scrapers()) {
            if (s->sourceId() == QLatin1String("weebcentral")) { wcScraper = s; break; }
        }
        const QString wcStagingRoot = m_bridge->dataDir()
                                    + QStringLiteral("/manga_wc_staging");
        QDir().mkpath(wcStagingRoot);
        // PHASE 12: shared cover-thumbnail output dir (same dir
        // TorrentVolumeProvider writes its premium covers to). Each provider
        // fires its own PremiumCoverExtractor internally; output filename
        // namespacing (premium_<seriesId>_v<NN>.jpg) keeps the two backends
        // from colliding even when they target the same series.
        const QString coversDir = m_bridge->dataDir()
                                + QStringLiteral("/manga_posters");
        QDir().mkpath(coversDir);
        m_weebCentralPacker = new tankoban::manga::WeebCentralVolumePacker(
            wcScraper, m_nam, wcStagingRoot, coversDir, this);

        // PHASE 12: WC packer's per-vol cover thumbnail repaints the
        // ComicsSeriesView volume row's Cover cell. Wired via QueuedConnection
        // so the slot lands on the UI thread regardless of which thread the
        // extractor's signaller landed on.
        connect(m_weebCentralPacker,
                &tankoban::manga::WeebCentralVolumePacker::volumeCoverReady,
                this, [this](const QString& seriesId, int volNumber,
                             const QString& coverPath) {
            if (m_tyVolumeSeriesView) {
                m_tyVolumeSeriesView->setVolumeCoverFromDisk(seriesId, volNumber, coverPath);
            }
        }, Qt::QueuedConnection);
        connect(m_weebCentralPacker,
                &tankoban::manga::WeebCentralVolumePacker::volumeCompleted,
                this, [this](const QString& seriesId, int volNumber,
                             const QString& cbzPath) {
            onProviderVolumeCompleted(seriesId, volNumber, cbzPath,
                static_cast<int>(PendingVolumeSourceKind::WeebCentralPacker));
        }, Qt::QueuedConnection);
        connect(m_weebCentralPacker,
                &tankoban::manga::WeebCentralVolumePacker::volumeFailed,
                this, [this](const QString& seriesId, int volNumber,
                             const QString& code, const QString& message) {
            onProviderVolumeFailed(seriesId, volNumber, code, message,
                static_cast<int>(PendingVolumeSourceKind::WeebCentralPacker));
        }, Qt::QueuedConnection);
    }

    // TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- ComicsSeriesView replaces
    // the deleted ComicsTankoyomiDetailView. Named with a "_ty" prefix to
    // disambiguate from the pre-existing m_seriesView (SeriesView*, folder-
    // imported series; constructed later at m_stack index 1).
    m_tyVolumeSeriesView = new tankoban::manga::comics::ComicsSeriesView(
        m_anilistClient, m_anilistCache, m_premiumCatalog, m_nyaaRuntime,
        m_mangaDownloadIndex, this);
    m_stack->addWidget(m_tyVolumeSeriesView);

    // Phase 9: route the Sources-panel dispatch signal to the dispatch slot.
    connect(m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::downloadDispatchRequested,
            this, &ComicsPage::onDownloadDispatchRequested);
    connect(m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::openVolume,
            this, &ComicsPage::onComicsSeriesOpenVolume);
    // STREAM_PORT 2026-05-18 Bug-1 fix: wire the new in-view "<- Back" button
    // (added by Task 1) to the existing onDetailBack slot. The slot was
    // shipped 2026-05-16 with a comment ("the new ComicsSeriesView does not
    // currently emit a Back signal... future deep-link recovery paths land
    // here") that was waiting on exactly this wire. Stream-blueprint parity:
    // StreamPage.cpp:445 connects StreamDetailView::backRequested -> goBack
    // the same way.
    connect(m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::backRequested,
            this, &ComicsPage::onDetailBack);

    // VOLUME_X_QUALITY 2026-05-28 (Agent 1). Route WeebCentral classification
    // verdicts into the series view for RAW-SCAN tags + the Volume X row. MUST
    // be here (not at m_wcResolver construction) — m_tyVolumeSeriesView does
    // not exist yet up there, so the connection would silently no-op.
    connect(m_wcResolver,
            &tankoban::manga::mangafire::MangaWeebCentralResolver::seriesClassified,
            m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::onSeriesClassified);

    // Task 8 (WEEBCENTRAL_IDENTITY_PIVOT): wire MangaSourceRegistry into the
    // series view so showSeries(MangaResult) can dispatch fetchDetail() to the
    // correct scraper. m_sourceRegistry is owned by ComicsPage (constructed above).
    m_tyVolumeSeriesView->setSourceRegistry(m_sourceRegistry);

    // COMICS_MANGAFIRE_PIVOT Phase B.2 (2026-05-23). Wire forceRefreshRequested
    // from the series view so the user can re-scan the local catalog.
    connect(m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::forceRefreshRequested,
            this, &ComicsPage::onForceRefreshRequested);
    connect(m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::weebCentralResolveRequested,
            this, &ComicsPage::onWcResolveRequested);
    connect(m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::weebCentralResolveRangeRequested,
            this, &ComicsPage::onWcResolveRangeRequested);
    // COMICS_WC_LIBRARY_ENRICH 2026-05-24 (Agent 1). MangaFire-catalog-only
    // series (anilistId=0) can't bookmark via the AniList-keyed path. Wire
    // the best-effort search-by-title enrichment here.
    connect(m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::addToLibraryByTitleRequested,
            this, &ComicsPage::onAddToLibraryByTitleRequested);
    // COMICS_WESTERN_ADD 2026-06-01 (Agent 2). Add-to-shelf for a LIVE Western
    // series: persist the stashed raw JSON (m_pendingWesternJson, set by the
    // westernSeriesReady slot) verbatim to data/western_catalogue/<seriesId>.json,
    // then flip the view's button to "On shelf" and refresh the Western grid so
    // the new shelf card appears. Writing the exact fetched JSON keeps the disk
    // record identical to the baked-catalogue schema (loadFromFile re-reads it).
    connect(m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::addWesternToLibraryRequested,
            this, [this]() {
        if (m_pendingWesternJson.isEmpty() || m_pendingWesternSeriesId.isEmpty()) {
            qInfo("ComicsPage: addWesternToLibraryRequested with no pending series");
            return;
        }
        // Validate (NOT strip) the REMOTE-derived seriesId as a safe filename
        // stem matching the baked-catalogue naming ([a-z0-9-], dash-separated).
        // Rejecting rather than stripping avoids BOTH path traversal (no '\\',
        // '/', ':', '.', '..') AND collisions (stripping 'a.b' -> 'ab' could
        // false-match a different series' file). (Codex review, 2026-06-01.)
        // Validate the id AS-IS (no toLower/normalisation — that would collapse
        // distinct ids onto one file). fetchWesternSeries already emits a
        // lowercased, dash-separated seriesId, so a well-formed series passes
        // unchanged and the filename is exactly the validated value.
        const QString& id = m_pendingWesternSeriesId;
        static const QRegularExpression safeIdRe(QStringLiteral("^[a-z0-9][a-z0-9-]*$"));
        if (!safeIdRe.match(id).hasMatch()) {
            qInfo("ComicsPage: unsafe Western seriesId '%s', refusing to write",
                  qUtf8Printable(id));
            return;
        }
        const QString dir = tankoban::manga::WesternCatalogLoader::canonicalDataDir();
        if (!QDir().mkpath(dir)) {
            qInfo("ComicsPage: failed to create Western catalogue dir %s",
                  qUtf8Printable(dir));
            return;
        }
        const QString path = QDir(dir).absoluteFilePath(id + QStringLiteral(".json"));
        // Skip-if-present (spec §8 locked default): never clobber an existing
        // shelf entry (incl. the baked 13). The button is already gated on
        // !onShelf, so this is belt-and-suspenders against any re-entry path.
        if (QFile::exists(path)) {
            if (m_tyVolumeSeriesView) m_tyVolumeSeriesView->setWesternOnShelf(true);
            return;
        }
        // Atomic write (QSaveFile temp + commit): a failed/partial write can never
        // leave a corrupt shelf JSON, and commit() is the single success point —
        // we only mark on-shelf / refresh the grid AFTER it succeeds.
        const QByteArray bytes =
            QJsonDocument(m_pendingWesternJson).toJson(QJsonDocument::Indented);
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
            qInfo("ComicsPage: failed to write Western shelf file %s",
                  qUtf8Printable(path));
            return;   // do NOT mark on-shelf on a failed write
        }
        if (m_tyVolumeSeriesView) m_tyVolumeSeriesView->setWesternOnShelf(true);
        refreshWesternGrid();  // surface the new card on the Western grid
    });
    // COMICS_WC_AUTOENRICH 2026-05-24 — fires automatically on series-open
    // from search results so the hero block renders without requiring an
    // explicit Add to Library click.
    connect(m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::enrichSeriesByTitleRequested,
            this, &ComicsPage::onEnrichSeriesByTitleRequested);
    connect(m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::detailResolvedForCatalog,
            this, [this](int anilistId, const QString& title) {
        if (!m_tyVolumeSeriesView || m_stack->currentWidget() != m_tyVolumeSeriesView)
            return;
        if (anilistId > 0 && m_currentDetailAnilistId > 0 &&
            anilistId != m_currentDetailAnilistId) {
            return;
        }

        const QString resolvedTitle = title.trimmed();
        if (resolvedTitle.isEmpty() ||
            resolvedTitle.startsWith(QStringLiteral("anilist_"),
                                     Qt::CaseInsensitive)) {
            return;
        }
        if (m_currentDetailSeriesTitle == resolvedTitle) {
            return;
        }

        qInfo("ComicsPage::detailResolvedForCatalog: retrying MangaFire resolve for \"%s\"",
              qUtf8Printable(resolvedTitle));
        m_currentDetailSeriesTitle = resolvedTitle;
        dispatchCatalogResolve(fandomSeriesSlugFromTitle(resolvedTitle),
                               /*titleHint*/resolvedTitle);
    });

    // Build the local MangaFire catalog index from data/mangafire_catalog/*.json.
    // One-shot scan at construction; refresh() is idempotent.
    m_localCatalogIndex.refresh();

    connect(m_anilistClient,
            &tankoban::manga::anilist::AniListClient::seriesSucceeded,
            this, [this](int, const tankoban::manga::anilist::MediaDetail& detail) {
        if (!m_volumeResolver || !m_tyVolumeSeriesView) return;
        if (m_tyVolumeSeriesView->currentAnilistId() != detail.preview.anilistId) return;

        const bool isOngoing = detail.preview.status == QStringLiteral("RELEASING") ||
                               detail.preview.status == QStringLiteral("HIATUS");
        const bool nullTotals = detail.totalVolumes <= 0 || detail.totalChapters <= 0;
        if (isOngoing && nullTotals) {
            // PHASE 2+: persist AniList staff/authors into MediaPreview so
            // MangaUpdatesDisambiguator can use author signal. v1 falls
            // back to exact title + start year.
            m_volumeResolver->resolveForAnilist(
                detail.preview.anilistId, detail.preview, QStringList{});
        }
    }, Qt::QueuedConnection);

    // COMICS_WC_LIBRARY_ENRICH 2026-05-24 (Agent 1). Persistent listeners
    // for the Add-to-Library search-by-title enrichment. Filtered by
    // m_pendingLibraryEnrichReqId so the search-bar / other AniList
    // searches don't trample this flow.
    connect(m_anilistClient,
            &tankoban::manga::anilist::AniListClient::searchSucceeded,
            this, [this](int reqId,
                          const QList<tankoban::manga::anilist::MediaPreview>& results) {
        if (reqId != m_pendingLibraryEnrichReqId || m_pendingLibraryEnrichReqId == 0) return;
        comicsOpenTrace(QStringLiteral("CP::searchSucceeded LANDED reqId=%1 results=%2")
                            .arg(reqId).arg(results.size()));
        const QString pendingTitle = m_pendingLibraryEnrichTitle;
        const bool addBookmark = m_pendingLibraryEnrichAddBookmark;
        const bool searchOpenRequest = (reqId == m_pendingSearchOpenEnrichReqId);
        const MangaResult searchOpenFallback = m_pendingSearchOpenFallback;
        m_pendingLibraryEnrichReqId = 0;
        m_pendingLibraryEnrichTitle.clear();
        m_pendingLibraryEnrichAddBookmark = false;
        if (searchOpenRequest) {
            m_pendingSearchOpenEnrichReqId = 0;
            m_pendingSearchOpenFallback = MangaResult{};
        }

        if (!m_anilistCache || !m_tyVolumeSeriesView) return;

        // No match: re-enable the button (if Add-to-Library path) or just
        // leave the series view as-is (if auto-enrichment path).
        if (results.isEmpty() || results.first().anilistId <= 0) {
            qInfo("ComicsPage::AniListEnrich: no AniList match for \"%s\" (addBookmark=%d)",
                  qUtf8Printable(pendingTitle), addBookmark ? 1 : 0);
            if (searchOpenRequest) {
                renderSearchOpenFallback(searchOpenFallback);
                return;
            }
            if (addBookmark) m_tyVolumeSeriesView->refreshLibraryButton();
            return;
        }

        const auto& preview = results.first();
        qInfo("ComicsPage::AniListEnrich: matched \"%s\" -> anilistId=%d title=\"%s\" (addBookmark=%d)",
              qUtf8Printable(pendingTitle), preview.anilistId,
              qUtf8Printable(preview.title), addBookmark ? 1 : 0);

        // Seed the cache with the preview so a future bookmarked tile (and
        // the current view's hero) render with title + cover immediately.
        tankoban::manga::anilist::MediaDetail seed;
        seed.preview = preview;
        m_anilistCache->put(seed);

        // Only the Add-to-Library path commits a bookmark. Auto-enrichment
        // is read-only — same hero rendering, no library commit.
        if (addBookmark) {
            m_anilistCache->addBookmark(preview.anilistId);
        }

        // Re-show the series so m_currentAnilistId picks up the new id and
        // the hero block (banner, poster, synopsis, tags) renders via the
        // existing AniList-driven flow. showSeries also fires the seriesById
        // fetch in the background which deep-enriches once it lands.
        m_currentDetailAnilistId   = preview.anilistId;
        m_currentDetailSeriesTitle = preview.title;
        m_tyVolumeSeriesView->showSeries(preview);
        const QString catalogTitle = searchOpenRequest && !searchOpenFallback.title.isEmpty()
            ? searchOpenFallback.title
            : preview.title;
        dispatchCatalogResolve(fandomSeriesSlugFromTitle(catalogTitle),
                               catalogTitle);
    }, Qt::QueuedConnection);

    connect(m_anilistClient,
            &tankoban::manga::anilist::AniListClient::searchFailed,
            this, [this](int reqId, const QString& reason) {
        if (reqId != m_pendingLibraryEnrichReqId || m_pendingLibraryEnrichReqId == 0) return;
        const QString pendingTitle = m_pendingLibraryEnrichTitle;
        const bool addBookmark = m_pendingLibraryEnrichAddBookmark;
        const bool searchOpenRequest = (reqId == m_pendingSearchOpenEnrichReqId);
        const MangaResult searchOpenFallback = m_pendingSearchOpenFallback;
        m_pendingLibraryEnrichReqId = 0;
        m_pendingLibraryEnrichTitle.clear();
        m_pendingLibraryEnrichAddBookmark = false;
        if (searchOpenRequest) {
            m_pendingSearchOpenEnrichReqId = 0;
            m_pendingSearchOpenFallback = MangaResult{};
        }
        qWarning("ComicsPage::AniListEnrich: AniList search failed for \"%s\" (addBookmark=%d): %s",
                 qUtf8Printable(pendingTitle), addBookmark ? 1 : 0, qUtf8Printable(reason));
        if (searchOpenRequest) {
            renderSearchOpenFallback(searchOpenFallback);
            return;
        }
        if (addBookmark && m_tyVolumeSeriesView) m_tyVolumeSeriesView->refreshLibraryButton();
    }, Qt::QueuedConnection);

    // Background scanner thread
    m_scanThread = new QThread(this);
    m_scanner = new LibraryScanner(m_bridge->dataDir() + "/thumbs");
    // Prime the scanner with claimed paths BEFORE moveToThread so the very
    // first scan respects Tankoyomi-origin ownership. Direct call is safe
    // here because the scanner still lives on the GUI thread.
    m_scanner->setClaimedPaths(m_tyLibrary->claimedCanonicalPaths());
    m_scanner->moveToThread(m_scanThread);

    connect(m_scanner, &LibraryScanner::seriesFound,
            this, &ComicsPage::onSeriesFound, Qt::QueuedConnection);
    connect(m_scanner, &LibraryScanner::scanFinished,
            this, &ComicsPage::onScanFinished, Qt::QueuedConnection);

    // REPO_HYGIENE Phase 4 P4.2 (2026-04-26) — race-safe scanner ownership.
    connect(m_scanThread, &QThread::finished, m_scanner, &QObject::deleteLater);

    m_scanThread->start();

    // Re-scan when root folders change.
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 P7 Task 43 — root-change
    // fallback (safe-fallback v1 per Codex §14). Order matters here:
    //   1. Push fresh claimed-paths to the scanner FIRST (queued — the
    //      scanner lives on a worker thread post-moveToThread; a direct
    //      call would race the in-flight walk). Posting before
    //      triggerScan keeps the scanner event-queue FIFO ordered:
    //      claimed-paths land before the scan walk that should respect
    //      them. Mirrors onTankoyomiLibraryChanged pattern.
    //   2. triggerScan() walks the new roots; folder-origin tiles
    //      re-render via the scan's terminal rebuildTiles inside
    //      onScanFinished. Calling rebuildTiles here too would paint
    //      orphans from the pre-change cached m_folderSeries (folder
    //      slice from the prior scan) until the scan completes — code
    //      review I1.
    //   3. validateAll re-checks every chapter's on-disk presence so
    //      chips flip back to NotDownloaded for files no longer
    //      reachable under any current root.
    //   4. refreshTileChips re-derives DOWNLOADING badges from the
    //      downloader state, independent of the scan.
    connect(m_bridge, &CoreBridge::rootFoldersChanged, this, [this](const QString& domain) {
        if (domain == "comics") {
            if (m_scanner) {
                QMetaObject::invokeMethod(m_scanner, "setClaimedPaths", Qt::QueuedConnection,
                                          Q_ARG(QStringList, m_tyLibrary->claimedCanonicalPaths()));
            }
            triggerScan();
            if (m_mangaDownloadIndex) m_mangaDownloadIndex->validateAll();
            refreshTileChips();
        }
    });
}

ComicsPage::~ComicsPage()
{
    m_scanThread->quit();
    m_scanThread->wait();
    // REPO_HYGIENE Phase 4 P4.2: m_scanner auto-deleted via deleteLater on
    // thread::finished. No manual delete. All other members are QObjects
    // parented to this — Qt deletes them.
}

namespace {
inline bool replyOkComics(QJsonObject& reply, QJsonObject fields)
{
    for (auto it = fields.begin(); it != fields.end(); ++it)
        reply.insert(it.key(), it.value());
    return true;
}
inline bool replyErrComics(QJsonObject& reply, const char* code, const QString& msg)
{
    reply["type"]    = QStringLiteral("error");
    reply["code"]    = QString::fromLatin1(code);
    reply["message"] = msg;
    return true;
}
}  // namespace

bool ComicsPage::dispatchDevCommand(const QString& cmd,
                                    const QJsonObject& payload,
                                    QJsonObject& reply)
{
    // v1.6 Phase D.4 (2026-05-19) — library-side bridge. Existing comics_*
    // commands are dispatched directly from MainWindow's handleDevCommand
    // (ComicsPage::dev* helpers), so this method only handles the cross-mode
    // library_* surface.
    if (!cmd.startsWith(QLatin1String("library_")))
        return false;

    if (cmd == QLatin1String("library_get_section"))
        return replyOkComics(reply, {{"section", devLibrarySection()}});
    if (cmd == QLatin1String("library_get_continue_reading")) {
        return replyOkComics(reply,
            {{"cr_strip", devLibrarySection().value("cr_strip").toObject()}});
    }
    if (cmd == QLatin1String("library_get_recently_added")) {
        return replyOkComics(reply,
            {{"recently_added", devLibrarySection().value("recently_added").toObject()}});
    }
    if (cmd == QLatin1String("library_get_search_state")) {
        return replyOkComics(reply, {
            {"query", m_searchBar ? m_searchBar->text() : QString()},
            {"search_state", devLibrarySection().value("search_state").toObject()}
        });
    }
    if (cmd == QLatin1String("library_get_scan_state")) {
        return replyOkComics(reply,
            {{"scan_state", devLibrarySection().value("scan_state").toObject()}});
    }
    if (cmd == QLatin1String("library_trigger_scan")) {
        const bool was = m_scanning;
        triggerScan();
        return replyOkComics(reply, {{"triggered", true},
                                     {"wasScanning", was},
                                     {"scanning", m_scanning},
                                     {"buffered", m_rescanPending}});
    }
    if (cmd == QLatin1String("library_get_sort"))
        return replyOkComics(reply, {{"sortKey",
            m_sortCombo ? m_sortCombo->currentData().toString() : QString()}});
    if (cmd == QLatin1String("library_set_sort")) {
        const QString key = payload.value("key").toString();
        if (key.isEmpty())
            return replyErrComics(reply, "BAD_REQUEST", "payload.key required");
        if (!m_sortCombo)
            return replyErrComics(reply, "INTERNAL", "sort combo not constructed");
        for (int i = 0; i < m_sortCombo->count(); ++i) {
            if (m_sortCombo->itemData(i).toString() == key) {
                m_sortCombo->setCurrentIndex(i);
                return replyOkComics(reply, {{"sortKey", key}});
            }
        }
        return replyErrComics(reply, "BAD_REQUEST",
            QStringLiteral("unknown sort key '%1'").arg(key));
    }
    if (cmd == QLatin1String("library_set_density")) {
        const int val = payload.value("value").toInt(-1);
        if (val < 0 || val > 2)
            return replyErrComics(reply, "BAD_REQUEST", "value must be 0|1|2");
        if (!m_densitySlider)
            return replyErrComics(reply, "INTERNAL", "density slider not constructed");
        m_densitySlider->setValue(val);
        return replyOkComics(reply, {{"density", val}});
    }
    if (cmd == QLatin1String("library_set_search_query")) {
        if (!m_searchBar)
            return replyErrComics(reply, "INTERNAL", "search bar not constructed");
        const QString q = payload.value("query").toString();
        m_searchBar->setText(q);
        return replyOkComics(reply, {{"query", q}});
    }
    if (cmd == QLatin1String("library_get_active_layer"))
        return replyOkComics(reply, {{"layer",
            devLibrarySection().value("active_layer").toString()}});
    if (cmd == QLatin1String("library_reset_mode")) {
        resetToRoot();
        return replyOkComics(reply, {{"reset", true},
            {"layer", devLibrarySection().value("active_layer").toString()}});
    }
    if (cmd == QLatin1String("library_get_selected_items"))
        return replyOkComics(reply, {{"selection", QJsonArray{}}});
    return false;  // unknown library_*
}

QJsonObject ComicsPage::devLibrarySection() const
{
    QJsonObject sec;
    QJsonObject cr;
    cr["visible"] = m_continueSection && m_continueSection->isVisible();
    cr["count"]   = static_cast<int>(m_progressKeyMap.size());
    sec["cr_strip"] = cr;

    QJsonObject ra;
    ra["count"]   = m_tileStrip ? m_tileStrip->totalCount() : 0;
    ra["visible"] = m_tileStrip && m_tileStrip->totalCount() > 0;
    sec["recently_added"] = ra;

    QJsonObject ss;
    ss["query"]    = m_searchBar ? m_searchBar->text() : QString();
    ss["visibleSeries"] = m_tileStrip ? m_tileStrip->visibleCount() : 0;
    sec["search_state"] = ss;

    QJsonObject scan;
    scan["scanning"]      = m_scanning;
    scan["hasScanned"]    = m_hasScanned;
    scan["rescanPending"] = m_rescanPending;
    sec["scan_state"] = scan;

    QJsonArray roots;
    if (m_bridge)
        for (const QString& p : m_bridge->rootFolders(QStringLiteral("comics")))
            roots.append(p);
    sec["root_folders"] = roots;

    sec["sort_key"] = m_sortCombo ? m_sortCombo->currentData().toString() : QString();
    sec["density"]  = m_densitySlider ? m_densitySlider->value() : -1;
    sec["selection"] = QJsonArray{};

    // active_layer reflects the page's Mode + folder-series sub-stack state.
    QString layer = QStringLiteral("library");
    switch (m_mode) {
    case Mode::Library: layer = QStringLiteral("library"); break;
    case Mode::SearchResults: layer = QStringLiteral("search-results"); break;
    case Mode::TankoyomiDetail: layer = QStringLiteral("series-view"); break;
    }
    if (m_stack && m_stack->currentWidget() == m_seriesView)
        layer = QStringLiteral("folder-series-view");
    sec["active_layer"] = layer;
    return sec;
}

void ComicsPage::setTorrentClient(TorrentClient* client)
{
    // TANKOYOMI_PREMIUM Phase 3 -- MainWindow wires this AFTER both ComicsPage
    // and TorrentClient exist (MainWindow.cpp construction order: pages first,
    // then TorrentClient hoisted to MainWindow scope). Idempotent: a second
    // call with a different client is a no-op for now (no documented re-wire
    // scenario in v1).
    if (m_torrentClient == client) return;
    if (m_premiumProvider) return;   // already wired; ignore re-wires for v1
    m_torrentClient = client;
    if (!client) return;
    TorrentEngine* engine = client->engine();
    if (!engine) return;
    if (!m_premiumCatalog || !m_premiumLedger) return;

    const QString premiumStagingRoot = m_bridge->dataDir()
                                     + QStringLiteral("/manga_premium_staging");
    // TANKOYOMI_PREMIUM Phase 10 -- shared cover-thumbnail output dir. Lives
    // under appDataDir alongside the staging tree; manga_posters is shared
    // with the existing series-level poster cache directory convention.
    const QString premiumCoversDir = m_bridge->dataDir()
                                   + QStringLiteral("/manga_posters");
    QDir().mkpath(premiumCoversDir);
    m_premiumProvider = new tankoban::manga::premium::TorrentVolumeProvider(
        /*engine=*/engine,
        /*catalog=*/m_premiumCatalog,
        /*ledger=*/m_premiumLedger,
        /*index=*/m_mangaDownloadIndex,
        /*stagingRoot=*/premiumStagingRoot,
        /*coversDir=*/premiumCoversDir,
        /*parent=*/this);

    // PHASE 12: TorrentVolumeProvider's per-vol cover thumbnail repaints the
    // ComicsSeriesView volume row's Cover cell. Mirrors the WC packer wiring
    // above; both providers funnel into the same setVolumeCoverFromDisk slot
    // so ComicsSeriesView does not branch on source type.
    connect(m_premiumProvider,
            &tankoban::manga::premium::TorrentVolumeProvider::volumeCoverReady,
            this, [this](const QString& seriesId, int volNumber,
                         const QString& coverPath) {
        if (m_tyVolumeSeriesView) {
            m_tyVolumeSeriesView->setVolumeCoverFromDisk(seriesId, volNumber, coverPath);
        }
    }, Qt::QueuedConnection);
    connect(m_premiumProvider,
            &tankoban::manga::premium::TorrentVolumeProvider::volumeCompleted,
            this, [this](const QString& seriesId, int volNumber,
                         const QString& cbzPath) {
        onProviderVolumeCompleted(seriesId, volNumber, cbzPath,
            static_cast<int>(PendingVolumeSourceKind::Catalog));
    }, Qt::QueuedConnection);
    connect(m_premiumProvider,
            &tankoban::manga::premium::TorrentVolumeProvider::volumeFailed,
            this, [this](const QString& seriesId, int volNumber,
                         const QString& code, const QString& message) {
        onProviderVolumeFailed(seriesId, volNumber, code, message,
            static_cast<int>(PendingVolumeSourceKind::Catalog));
    }, Qt::QueuedConnection);

    // TANKOYOMI_PREMIUM Phase 9 -- thin facade over MangaDownloader +
    // TorrentVolumeProvider so a single "Transfers paused" affordance can
    // fan out to both backends. Constructed AFTER m_premiumProvider is
    // non-null (here). NO UI affordance gets bound yet; Phase 11+ surfaces
    // the "Pause all transfers" button.
    if (!m_transferCoordinator) {
        m_transferCoordinator = new tankoban::manga::premium::MangaTransferCoordinator(
            m_mangaDownloader, m_premiumProvider, m_weebCentralPacker, this);
    }

    // TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- legacy detail-view
    // signal wiring (Premium progress / completion / failure / swarm /
    // cover / volume-download-click) is gone with the deleted view. The
    // new ComicsSeriesView reads progress/cover state from
    // MangaDownloadIndex directly (Phase 10+ work); the dispatch path
    // for volume downloads lives in onDownloadDispatchRequested below
    // and is wired earlier in the ctor (downloadDispatchRequested signal
    // from ComicsSeriesView).

    // Crash-resume entry point. Replay AFTER engine + catalog are alive.
    // TorrentEngine is built inside TorrentClient's ctor and starts on
    // construction; calling replayLedger immediately is fine (any
    // not-yet-discovered peers will be picked up by addMagnet's tracker
    // bootstrap as usual).
    m_premiumProvider->replayLedger();

    // COMICS_WESTERN_DOWNLOAD 2026-06-02 (Agent 1). WesternVolumeDownloader is
    // constructed here so the magnet path has a live TorrentClient. Mirror of
    // TorrentVolumeProvider lazy-wire above: guard against double-construction on
    // hypothetical future re-wire; use the shared NetSeam QNAM.
    if (!m_westernDownloader) {
        m_westernDownloader = new tankoban::manga::WesternVolumeDownloader(
            m_nam, client, this);
        wireWesternDownloader();
    }
}

// COMICS_WESTERN_DOWNLOAD 2026-06-02 (Agent 1).
// Wire the WesternVolumeDownloader signals and connect the series-view trigger.
// Called once from setTorrentClient() after m_westernDownloader is non-null.
void ComicsPage::wireWesternDownloader()
{
    Q_ASSERT(m_westernDownloader);

    // --- Download trigger: series view emits, we call requestVolume ---
    connect(m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::downloadWesternEditionRequested,
            this,
            [this](int volumeNumber, const QString& editionTitle,
                   const QString& tierLabel, const QString& /*sourceHref*/) {
        // GetComics-as-source (2026-06-05): the Western download runs through
        // WesternVolumeDownloader, matching collected editions on series identity
        // with year/tier as hints. It does not use the RCO page-scrape path.
        if (!m_westernDownloader || m_pendingWesternSeriesId.isEmpty()) {
            qInfo("ComicsPage: Western download ignored - no downloader/series");
            if (m_tyVolumeSeriesView)
                m_tyVolumeSeriesView->updateWesternDownloadStatus(QString(), tr("No download found"));
            return;
        }

        // Snapshot the live page state ONCE up front (the shelf write + dispatch
        // must agree on the same series even if a synchronous UI mutation touches
        // a member mid-lambda). Use these copies, not the members, from here on.
        const QString     seriesId    = m_pendingWesternSeriesId;
        const QJsonObject seriesJson  = m_pendingWesternJson;
        const QString     seriesTitle = m_currentDetailSeriesTitle;

        // Compute destination path: comics-root from TorrentClient if available,
        // otherwise fall back next to the Western data dir.
        QString comicsRoot;
        if (m_torrentClient) {
            comicsRoot = m_torrentClient->defaultPaths().value(QStringLiteral("comics"));
        }
        if (comicsRoot.isEmpty()) {
            comicsRoot = QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
                             .absoluteFilePath(QStringLiteral("../western_downloads"));
        }

        // Sanitise the series title to a safe directory name.
        QString safeTitle = seriesTitle;
        static const QRegularExpression kUnsafeDirChars(
            QStringLiteral("[\\\\/:*?\"<>|]"));
        safeTitle.replace(kUnsafeDirChars, QStringLiteral("_"));
        safeTitle = safeTitle.trimmed();
        if (safeTitle.isEmpty()) safeTitle = seriesId;

        const QString destPath = QDir(comicsRoot).absoluteFilePath(safeTitle);
        if (!QDir().mkpath(destPath)) {
            qInfo("ComicsPage: failed to mkpath Western dest %s", qUtf8Printable(destPath));
            return;
        }

        // Auto-add the series to the Western shelf before downloading — mirrors
        // addWesternToLibraryRequested so the shelf card appears without an
        // explicit "Add to Library" click. Idempotent (QSaveFile overwrites).
        const QString dir = tankoban::manga::WesternCatalogLoader::canonicalDataDir();
        const QString shelfPath =
            QDir(dir).absoluteFilePath(seriesId + QStringLiteral(".json"));
        if (!seriesJson.isEmpty() && !QFile::exists(shelfPath)) {
            QDir().mkpath(dir);
            const QByteArray bytes =
                QJsonDocument(seriesJson).toJson(QJsonDocument::Indented);
            QSaveFile sf(shelfPath);
            if (sf.open(QIODevice::WriteOnly) &&
                sf.write(bytes) == bytes.size() &&
                sf.commit()) {
                if (m_tyVolumeSeriesView) m_tyVolumeSeriesView->setWesternOnShelf(true);
                refreshWesternGrid();
            }
        }

        // rcostation's reader pages are browser-locked (dead descramble), so the
        // clicked rco issue is cross-mapped to its readallcomics counterpart for
        // the actual page fetch. Issue number comes from the edition title
        // ("Invincible #144" -> 144), falling back to the volume row number.
        const int year = seriesJson.value(QStringLiteral("yearStart")).toInt();

        qInfo("ComicsPage: Western download - series=%s edition=%s volumeNumber=%d year=%d dest=%s",
              qUtf8Printable(seriesId), qUtf8Printable(editionTitle),
              volumeNumber, year, qUtf8Printable(destPath));

        m_westernDownloader->requestVolume(seriesId, volumeNumber, seriesTitle,
                                           year, tierLabel, destPath);
    });

    // --- volumeResolved: surface the matched collected edition in the panel ---
    connect(m_westernDownloader,
            &tankoban::manga::WesternVolumeDownloader::volumeResolved,
            this,
            [this](const QString& seriesId, int /*volNumber*/, const QString& editionTitle) {
        if (!m_tyVolumeSeriesView) return;
        if (m_pendingWesternSeriesId.isEmpty() || seriesId != m_pendingWesternSeriesId) return;
        m_westernDownloadEdition = editionTitle;
        m_tyVolumeSeriesView->updateWesternDownloadStatus(editionTitle, tr("Downloading..."));
    }, Qt::QueuedConnection);

    // --- volumeCompleted: register in index + flip tile to Read ---
    connect(m_westernDownloader,
            &tankoban::manga::WesternVolumeDownloader::volumeCompleted,
            this,
            [this](const QString& seriesId, int volNumber, const QString& cbzPath) {
        onProviderVolumeCompleted(seriesId, volNumber, cbzPath,
            static_cast<int>(PendingVolumeSourceKind::WesternGetComics));
        if (m_tyVolumeSeriesView && !m_pendingWesternSeriesId.isEmpty()
            && seriesId == m_pendingWesternSeriesId) {
            m_tyVolumeSeriesView->updateWesternDownloadStatus(
                m_westernDownloadEdition, tr("Downloaded - open to read"));
        }
    }, Qt::QueuedConnection);

    // --- volumeProgress: paint percent on the tile ---
    connect(m_westernDownloader,
            &tankoban::manga::WesternVolumeDownloader::volumeProgress,
            this,
            [this](const QString& seriesId, int volNumber, int percent) {
        // Only update the tile when this series is currently displayed.
        if (!m_tyVolumeSeriesView) return;
        const bool isCurrentSeries =
            !m_pendingWesternSeriesId.isEmpty() &&
            seriesId == m_pendingWesternSeriesId;
        if (!isCurrentSeries) return;
        // Reuse setVolumeStatusText to show "N%" on the tile while in flight.
        const QString tileLabel = percent <= 0
            ? QStringLiteral("Finding...")
            : QStringLiteral("%1%").arg(percent);
        m_tyVolumeSeriesView->setVolumeStatusText(volNumber, tileLabel);
        // Mirror into the Sources panel (keeps the matched edition label).
        const QString panelLine = percent <= 0
            ? tr("Finding...")
            : tr("Downloading %1%").arg(percent);
        m_tyVolumeSeriesView->updateWesternDownloadStatus(m_westernDownloadEdition, panelLine);
    }, Qt::QueuedConnection);

    // --- volumeFailed: surface error text on the tile ---
    connect(m_westernDownloader,
            &tankoban::manga::WesternVolumeDownloader::volumeFailed,
            this,
            [this](const QString& seriesId, int volNumber, const QString& reason) {
        // Delegate to the shared failed path (records in dispatch map if any,
        // updates tile status).
        onProviderVolumeFailed(seriesId, volNumber,
                               QStringLiteral("resolve_failed"),
                               reason,
                               static_cast<int>(PendingVolumeSourceKind::WesternGetComics));
        if (m_tyVolumeSeriesView && !m_pendingWesternSeriesId.isEmpty()
            && seriesId == m_pendingWesternSeriesId) {
            // Strict matcher: a miss is the expected fail-safe, not an error.
            m_tyVolumeSeriesView->updateWesternDownloadStatus(
                QString(), tr("No download found"));
        }
    }, Qt::QueuedConnection);

    // --- coverReady: paint per-edition cover on the tile ---
    connect(m_westernDownloader,
            &tankoban::manga::WesternVolumeDownloader::coverReady,
            this,
            [this](const QString& seriesId, int volNumber, const QString& coverUrl) {
        if (!m_tyVolumeSeriesView || coverUrl.isEmpty()) return;
        const bool isCurrentSeries =
            !m_pendingWesternSeriesId.isEmpty() &&
            seriesId == m_pendingWesternSeriesId;
        if (!isCurrentSeries) return;
        // loadCoverUrlForVolume is private in ComicsSeriesView; use the existing
        // setVolumeCoverFromDisk path when the URL is a local path, else call
        // the public async loader via a tankoctl-style NAM fetch is not exposed.
        // For now we log and accept that a follow-up can wire
        // loadCoverUrlForVolume as a public slot if desired.
        Q_UNUSED(volNumber);
        qInfo("ComicsPage: coverReady for Western series=%s vol=%d url=%s",
              qUtf8Printable(seriesId), volNumber, qUtf8Printable(coverUrl));
    }, Qt::QueuedConnection);
}

// COMICS_WESTERN_DOWNLOAD 2026-06-03 (Agent 1). Normalize a Western series title
// for cross-source matching: drop a "(Publisher: …)" / "(2003)" suffix, lower-
// case, keep alphanumerics + single spaces. "Invincible (Publisher: Image
// Comics)" and "Invincible" both normalize to "invincible".
static QString normalizeWesternTitle(const QString& raw)
{
    QString s = raw;
    static const QRegularExpression kParen(QStringLiteral(R"(\s*\([^)]*\))"));
    s.remove(kParen);
    s = s.toLower();
    static const QRegularExpression kNonAlnum(QStringLiteral(R"([^a-z0-9]+)"));
    s.replace(kNonAlnum, QStringLiteral(" "));
    return s.trimmed();
}

// COMICS_WESTERN_DOWNLOAD 2026-06-03 (Agent 1). Cross-map the clicked rco issue
// to its readallcomics counterpart, then drive the existing page->cbz pipeline.
// Chain: search(series) -> pick best category -> fetchChapters -> match issue
// number -> startDownload(source="readallcomics"). The readallcomics scraper is
// a distinct object from the rco browse scraper, so connecting its signals here
// does not disturb the catalog browse path; one-shot connections (disconnected
// on the first terminal event) keep concurrent requests from cross-firing.
void ComicsPage::startWesternIssueDownload(const QString& seriesTitle, double issueNumber,
                                           const QString& editionTitle, int volumeNumber,
                                           const QString& destPath)
{
    if (!m_readAllComicsScraper || !m_mangaDownloader) {
        if (m_tyVolumeSeriesView)
            m_tyVolumeSeriesView->updateWesternDownloadStatus(QString(), tr("No download found"));
        return;
    }

    m_westernDownloadEdition       = editionTitle;
    m_pendingWesternDownloadVolume = volumeNumber;
    if (m_tyVolumeSeriesView)
        m_tyVolumeSeriesView->updateWesternDownloadStatus(editionTitle, tr("Finding source..."));

    auto* scraper = m_readAllComicsScraper;

    auto searchConn = std::make_shared<QMetaObject::Connection>();
    auto chapConn   = std::make_shared<QMetaObject::Connection>();
    auto errConn    = std::make_shared<QMetaObject::Connection>();
    auto cleanup = [searchConn, chapConn, errConn]() {
        QObject::disconnect(*searchConn);
        QObject::disconnect(*chapConn);
        QObject::disconnect(*errConn);
    };
    auto fail = [this](const QString& why) {
        qInfo("ComicsPage: Western readallcomics resolve failed — %s", qUtf8Printable(why));
        if (m_tyVolumeSeriesView)
            m_tyVolumeSeriesView->updateWesternDownloadStatus(QString(), tr("No download found"));
    };

    *errConn = connect(scraper, &MangaScraper::errorOccurred, this,
        [cleanup, fail](const QString& msg) { cleanup(); fail(msg); });

    *searchConn = connect(scraper, &MangaScraper::searchFinished, this,
        [this, scraper, chapConn, cleanup, fail, seriesTitle, issueNumber, editionTitle, destPath]
        (const QList<MangaResult>& results) {
            if (results.isEmpty()) { cleanup(); fail("no series match"); return; }

            // Pick the best series: exact normalized-title match beats a prefix
            // overlap beats the first result (publisher-disambiguation tolerant).
            const QString want = normalizeWesternTitle(seriesTitle);
            QString slug = results.first().id;
            int bestScore = -1;
            for (const auto& r : results) {
                const QString got = normalizeWesternTitle(r.title);
                const int score = (got == want) ? 2
                                : (got.startsWith(want) || want.startsWith(got)) ? 1 : 0;
                if (score > bestScore) { bestScore = score; slug = r.id; }
            }

            *chapConn = connect(scraper, &MangaScraper::chaptersReady, this,
                [this, cleanup, fail, issueNumber, editionTitle, seriesTitle, destPath]
                (const QList<ChapterInfo>& chapters) {
                    cleanup();
                    ChapterInfo match;
                    bool found = false;
                    for (const auto& c : chapters) {
                        if (qAbs(c.chapterNumber - issueNumber) < 0.001) {
                            match = c; found = true; break;
                        }
                    }
                    if (!found) {
                        fail(QStringLiteral("issue %1 not found on readallcomics").arg(issueNumber));
                        return;
                    }

                    ChapterInfo ch;
                    ch.id     = match.id;                       // readallcomics issue-slug
                    ch.name   = editionTitle;
                    ch.source = QStringLiteral("readallcomics");
                    if (m_tyVolumeSeriesView)
                        m_tyVolumeSeriesView->updateWesternDownloadStatus(
                            editionTitle, tr("Downloading..."));
                    m_westernDownloadRecordId = m_mangaDownloader->startDownload(
                        seriesTitle, QStringLiteral("readallcomics"),
                        { ch }, destPath, QStringLiteral("cbz"));
                });
            scraper->fetchChapters(slug);
        });

    scraper->search(seriesTitle, 60);
}

void ComicsPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 30 — re-validate
    // every chapter entry in the on-disk index against the actual filesystem.
    // External deletions (Finder/Explorer, antivirus, "Delete files" confirms)
    // emit entriesChanged via evictByChapter, which surfaces as updated chip
    // state next time the chapter list is repainted.
    if (m_mangaDownloadIndex) m_mangaDownloadIndex->validateAll();
}

// Stream/Theatre-parity search-bar event filter (2026-05-22). Drives the
// search-history dropdown off m_searchBar focus events:
//   FocusIn  + empty input  → show history dropdown (skipped if input has
//                             text, so we don't obscure the user's typing)
//   FocusOut                → start the 150ms delayed hide timer, giving a
//                             clicked dropdown row time to consume its
//                             press/release before the dropdown vanishes
// Mirrors StreamPage.cpp's eventFilter at :1873-1889.
bool ComicsPage::eventFilter(QObject* obj, QEvent* event)
{
    // Shared-recipe generalisation (2026-06-02): track which bar has focus so
    // positionSearchHistoryDropdown + setSearchBusy always target the right bar.
    QLineEdit* bar = (obj == m_searchBar)       ? m_searchBar :
                     (obj == m_westernSearchBar) ? m_westernSearchBar : nullptr;
    if (bar) {
        if (event->type() == QEvent::FocusIn) {
            m_activeSearchBar  = bar;
            m_activeSearchBusy = (bar == m_searchBar) ? m_searchBusy : m_westernSearchBusy;
            if (bar->text().trimmed().isEmpty()) {
                showSearchHistoryDropdown();
            }
        } else if (event->type() == QEvent::FocusOut) {
            if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->start();
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ComicsPage::buildUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_stack = new FadingStackedWidget(this);

    // ── Grid view (index 0) — wrapped in scroll area ──
    auto* gridScroll = new QScrollArea();
    m_gridScroll = gridScroll;  // GLOBAL_NAV_HISTORY Task 8 review fix
    gridScroll->setObjectName("ComicsGridScroll");
    gridScroll->setFrameShape(QFrame::NoFrame);
    gridScroll->setWidgetResizable(true);
    gridScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    gridScroll->setStyleSheet("QScrollArea#ComicsGridScroll { background: transparent; border: none; }");

    auto* gridPage = new QWidget();
    gridPage->setObjectName("ComicsGridPage");
    gridPage->setStyleSheet("QWidget#ComicsGridPage { background: transparent; }");
    auto* gridLayout = new QVBoxLayout(gridPage);
    gridLayout->setContentsMargins(20, 0, 20, 20);
    gridLayout->setSpacing(24);

    // ── 1. Search bar (full width, top) ──
    // Shared-recipe builder (2026-06-02): constructs full manga-parity chrome
    // (input + busy spinner + search icon button) and wires all handlers.
    // Replaces the previous inline block; the tooltip, timer, Ctrl+F, and Esc
    // blocks that follow are unchanged.
    {
        QWidget* mangaSearchRow = buildSearchRow(
            m_searchBar, m_searchBusy, m_searchBtn,
            QStringLiteral("Search Manga"),
            QStringLiteral("weebcentral"));
        // Preserve the 20px top margin that the old inline searchLayout carried.
        mangaSearchRow->setContentsMargins(0, 20, 0, 0);
        gridLayout->addWidget(mangaSearchRow);
    }

    m_searchBar->setToolTip(tr("Press Enter or click the search icon to search Tankoyomi sources"));

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 19 - search bar
    // repurpose. textChanged + returnPressed are now wired inside buildSearchRow.
    // The debounce timer is kept for future use / GLOBAL_NAV_HISTORY restore.
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(250);

    // Stream/Theatre-parity search history (2026-05-22): load persisted
    // queries, construct the floating dropdown widget once, install the
    // event filter that opens/closes it on m_searchBar focus changes.
    loadSearchHistory();
    buildSearchHistoryDropdown();
    m_searchBar->installEventFilter(this);

    // Ctrl+F focuses search bar
    auto* searchShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        m_searchBar->setFocus();
        m_searchBar->selectAll();
    });

    // Escape: clear search if active, else navigate back to grid.
    // Task 7 (2026-05-01) — scope to widget so it doesn't intercept Esc
    // when ComicReader is shown over this page in the QStackedWidget.
    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (!m_searchBar->text().trimmed().isEmpty()) {
            m_searchBar->clear();
        } else if (m_stack->currentWidget() == m_tyVolumeSeriesView) {
            // Route Esc-from-volume-detail through onDetailBack so it clears
            // the view + resets m_enteredDetailFrom + m_currentDetailAnilistId
            // + m_currentDetailSeriesTitle; bypasses the stale-state regression
            // that a bare showGrid() would leave behind.
            onDetailBack();
        } else if (m_stack->currentIndex() != 0) {
            showGrid();
        }
    });

    // F5: trigger rescan
    auto* f5Shortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(f5Shortcut, &QShortcut::activated, this, &ComicsPage::triggerScan);

    // ── 2. Continue Reading section ──
    m_continueSection = new QWidget(gridPage);
    auto* continueLayout = new QVBoxLayout(m_continueSection);
    continueLayout->setContentsMargins(0, 0, 0, 0);
    continueLayout->setSpacing(4);
    auto* continueLabel = new QLabel("CONTINUE READING", m_continueSection);
    continueLabel->setObjectName("LibraryHeading");
    continueLayout->addWidget(continueLabel);
    m_continueStrip = new TileStrip(m_continueSection);
    m_continueStrip->setMode("continue");
    continueLayout->addWidget(m_continueStrip);
    m_continueSection->hide();
    gridLayout->addWidget(m_continueSection);

    // Continue strip context menu
    m_continueStrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_continueStrip, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* card = m_continueStrip->tileAt(pos);
        if (!card) return;

        QString filePath = card->property("filePath").toString();
        QString progKey = QString(QCryptographicHash::hash(
            filePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));

        QMenu* menu = ContextMenuHelper::createMenu(this);
        QAction* removeCr = menu->addAction(tr("Remove from Continue Reading"));
        if (menu->exec(m_continueStrip->mapToGlobal(pos)) == removeCr) {
            if (m_bridge) {
                m_bridge->clearProgress("comics", progKey);
                refreshContinueStrip();
            }
        }
        menu->deleteLater();
    });

    // ── 3. "LIBRARY" header row: label + sort + density ──
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- section relabel from
    // "SERIES" (folder-import + Tankoyomi-library merge) to "DOWNLOADED"
    // (one tile per series in MangaDownloadIndex::entriesForAllSeries).
    // 2026-05-17 -- merged with BOOKMARKED into a single "LIBRARY" section
    // per Theatre's "Shows & Movies" mirror; both source feeds flow into
    // m_tileStrip alongside the existing dedup. m_downloadedLabel variable
    // name preserved for code-stability; only the display text changed.
    auto* seriesRow = new QWidget(gridPage);
    auto* seriesLayout = new QHBoxLayout(seriesRow);
    seriesLayout->setContentsMargins(0, 0, 0, 0);
    seriesLayout->setSpacing(8);

    m_downloadedLabel = new QLabel("LIBRARY", seriesRow);
    m_downloadedLabel->setObjectName("LibraryHeading");
    seriesLayout->addWidget(m_downloadedLabel);
    seriesLayout->addStretch();

    m_sortCombo = new QComboBox(seriesRow);
    m_sortCombo->setObjectName("LibrarySortCombo");
    m_sortCombo->setFixedWidth(150);
    m_sortCombo->setFixedHeight(28);
    m_sortCombo->addItem("Name A\u2192Z",       "name_asc");
    m_sortCombo->addItem("Name Z\u2192A",       "name_desc");
    m_sortCombo->addItem("Recently updated",     "updated_desc");
    m_sortCombo->addItem("Least recent",         "updated_asc");
    m_sortCombo->addItem("Most items",           "count_desc");
    m_sortCombo->addItem("Fewest items",         "count_asc");
    m_sortCombo->setStyleSheet(
        "QComboBox#LibrarySortCombo {"
        "  background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 6px; color: #ccc; padding: 2px 8px; font-size: 12px; }"
        "QComboBox#LibrarySortCombo:hover { border-color: rgba(255,255,255,0.2); }"
        "QComboBox#LibrarySortCombo::drop-down { border: none; }"
        "QComboBox#LibrarySortCombo QAbstractItemView {"
        "  background: #1e1e1e; color: #ccc; selection-background-color: rgba(255,255,255,0.1);"
        "  border: 1px solid rgba(255,255,255,0.12); }");
    QString savedSort = QSettings("Tankoban", "Tankoban").value("library_sort_comics", "name_asc").toString();
    for (int i = 0; i < m_sortCombo->count(); ++i) {
        if (m_sortCombo->itemData(i).toString() == savedSort) {
            m_sortCombo->setCurrentIndex(i);
            break;
        }
    }
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        QString key = m_sortCombo->itemData(idx).toString();
        QSettings("Tankoban", "Tankoban").setValue("library_sort_comics", key);
        m_tileStrip->sortTiles(key);
    });
    seriesLayout->addWidget(m_sortCombo);

    auto* densitySmall = new QLabel("A", seriesRow);
    densitySmall->setObjectName("DensityLabelSmall");
    seriesLayout->addWidget(densitySmall);

    m_densitySlider = new QSlider(Qt::Horizontal, seriesRow);
    m_densitySlider->setRange(0, 2);
    m_densitySlider->setFixedWidth(100);
    m_densitySlider->setFixedHeight(20);
    int savedDensity = QSettings("Tankoban", "Tankoban").value("grid_cover_size", 1).toInt();
    m_densitySlider->setValue(qBound(0, savedDensity, 2));
    connect(m_densitySlider, &QSlider::valueChanged, this, [this](int val) {
        QSettings("Tankoban", "Tankoban").setValue("grid_cover_size", val);
        m_tileStrip->setDensity(val);
        if (m_continueStrip) m_continueStrip->setDensity(val);
        if (m_bookmarkedStrip) m_bookmarkedStrip->setDensity(val);
        if (m_westernGrid) m_westernGrid->setDensity(val);
    });
    seriesLayout->addWidget(m_densitySlider);

    auto* densityLarge = new QLabel("A", seriesRow);
    densityLarge->setObjectName("DensityLabelLarge");
    seriesLayout->addWidget(densityLarge);

    // View toggle button (grid/list)
    m_viewToggle = new QPushButton(seriesRow);
    m_viewToggle->setObjectName("ViewToggle");
    m_viewToggle->setFixedSize(28, 28);
    m_viewToggle->setText("\u2630"); // hamburger icon
    m_viewToggle->setCursor(Qt::PointingHandCursor);
    // Styling lives in Theme.cpp QSS template under QPushButton#ViewToggle —
    // theme-bound (Light/Dark parity).
    connect(m_viewToggle, &QPushButton::clicked, this, &ComicsPage::toggleViewMode);
    seriesLayout->addWidget(m_viewToggle);

    gridLayout->addWidget(seriesRow);

    m_statusLabel = new QLabel("Search to add comics to your library", gridPage);
    m_statusLabel->setObjectName("TileSubtitle");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 60px;");
    gridLayout->addWidget(m_statusLabel);

    m_tileStrip = new TileStrip(gridPage);
    m_tileStrip->hide();
    m_tileStrip->setMode(QStringLiteral("fixedGrid"));
    m_tileStrip->setDensity(savedDensity);
    if (m_continueStrip) m_continueStrip->setDensity(savedDensity);
    gridLayout->addWidget(m_tileStrip);

    // ── 4. "BOOKMARKED" section ──
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- second library section
    // backed by AniListCache::bookmarkedPreviews(). Hidden until the user
    // bookmarks at least one series (refreshLibraryStrips toggles visibility).
    m_bookmarkedSection = new QWidget(gridPage);
    auto* bookmarkedLayout = new QVBoxLayout(m_bookmarkedSection);
    bookmarkedLayout->setContentsMargins(0, 0, 0, 0);
    bookmarkedLayout->setSpacing(4);
    m_bookmarkedLabel = new QLabel("BOOKMARKED", m_bookmarkedSection);
    m_bookmarkedLabel->setObjectName("LibraryHeading");
    bookmarkedLayout->addWidget(m_bookmarkedLabel);
    m_bookmarkedStrip = new TileStrip(m_bookmarkedSection);
    m_bookmarkedStrip->setMode(QStringLiteral("fixedGrid"));
    m_bookmarkedStrip->setDensity(savedDensity);
    bookmarkedLayout->addWidget(m_bookmarkedStrip);
    m_bookmarkedSection->hide();
    gridLayout->addWidget(m_bookmarkedSection);

    // List view (hidden by default — V-key toggles)
    m_listView = new LibraryListView(gridPage);
    m_listView->hide();
    connect(m_listView, &LibraryListView::itemActivated, this, [this](const QString& path) {
        // Find series name from path
        QDir dir(path);
        QString name = dir.dirName();
        openSeriesByPath(path, name);
    });
    gridLayout->addWidget(m_listView, 1);

    // Right-click on tiles (selection-aware via TileStrip signal)
    connect(m_tileStrip, &TileStrip::tileRightClicked, this, [this](TileCard* card, const QPoint& globalPos) {
        QList<TileCard*> sel = m_tileStrip->selectedTiles();
        if (sel.size() > 1) {
            // ── Multi-select context menu ──
            onMultiSelectContextMenu(sel, globalPos);
        } else {
            // ── Single-select context menu (existing logic) ──
            QPoint localPos = m_tileStrip->mapFromGlobal(globalPos);
            onTileContextMenu(localPos);
        }
    });

    // Double-click opens SeriesView
    connect(m_tileStrip, &TileStrip::tileDoubleClicked, this, [this](TileCard* card) {
        if (!card) return;
        openSeriesByPath(card->property("seriesPath").toString(),
                         card->property("seriesName").toString(),
                         card->property("coverPath").toString());
    });

    // Ctrl+A select all tiles
    auto* selectAllShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_A), this);
    connect(selectAllShortcut, &QShortcut::activated, this, [this]() {
        if (m_stack->currentIndex() == 0)
            m_tileStrip->selectAll();
    });

    // V-key: toggle grid/list view
    auto* viewToggleShortcut = new QShortcut(QKeySequence(Qt::Key_V), this);
    connect(viewToggleShortcut, &QShortcut::activated, this, [this]() {
        if (m_stack->currentIndex() == 0 && !m_searchBar->hasFocus())
            toggleViewMode();
    });

    // Restore persisted view mode
    m_gridMode = QSettings("Tankoban", "Tankoban").value("library_view_mode_comics", "grid").toString() == "grid";
    if (!m_gridMode) toggleViewMode();

    gridLayout->addStretch();
    gridScroll->setWidget(gridPage);
    m_stack->addWidget(gridScroll);

    // ── Series view (index 1) ──
    m_seriesView = new SeriesView(m_bridge);
    connect(m_seriesView, &SeriesView::backRequested, this, &ComicsPage::showGrid);
    connect(m_seriesView, &SeriesView::issueSelected, this, &ComicsPage::openComic);
    m_stack->addWidget(m_seriesView);

    // ── Manga / Western mode toggle (top chrome, COMICS_WESTERN_CATALOGUE
    //    Task 7 2026-05-31, Agent 2). Mode-pill aesthetic: gray/black/white,
    //    no color, no icon (feedback_no_color_no_emoji). Manga is the default
    //    left state and its path is untouched; Western is the additive shelf. ──
    {
        const QString pillQss = QStringLiteral(
            "QPushButton#ComicsModePill {"
            "  background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.12);"
            "  border-radius: 6px; color: rgba(238,238,238,0.70); padding: 5px 16px; font-size: 13px; }"
            "QPushButton#ComicsModePill:hover { border: 1px solid rgba(255,255,255,0.25); }"
            "QPushButton#ComicsModePill:checked {"
            "  background: rgba(255,255,255,0.14); border: 1px solid rgba(255,255,255,0.34); color: #fff; }");

        m_mangaTabBtn = new QPushButton(tr("Manga"));
        m_mangaTabBtn->setObjectName("ComicsModePill");
        m_mangaTabBtn->setCheckable(true);
        m_mangaTabBtn->setChecked(true);
        m_mangaTabBtn->setCursor(Qt::PointingHandCursor);
        m_mangaTabBtn->setStyleSheet(pillQss);

        m_westernTabBtn = new QPushButton(tr("Western"));
        m_westernTabBtn->setObjectName("ComicsModePill");
        m_westernTabBtn->setCheckable(true);
        m_westernTabBtn->setCursor(Qt::PointingHandCursor);
        m_westernTabBtn->setStyleSheet(pillQss);

        connect(m_mangaTabBtn,   &QPushButton::clicked, this, &ComicsPage::showMangaMode);
        connect(m_westernTabBtn, &QPushButton::clicked, this, &ComicsPage::showWesternMode);

        auto* modeRow = new QHBoxLayout();
        modeRow->setContentsMargins(20, 12, 20, 0);
        modeRow->setSpacing(8);
        modeRow->addWidget(m_mangaTabBtn);
        modeRow->addWidget(m_westernTabBtn);
        modeRow->addStretch();
        layout->addLayout(modeRow);
    }

    // Western browse grid as its own m_stack screen (index captured).
    buildWesternScreen();

    layout->addWidget(m_stack, 1);
}

void ComicsPage::activate()
{
    if (!m_hasScanned)
        triggerScan();
}

void ComicsPage::triggerScan()
{
    // REPO_HYGIENE Phase 4 P4.3 (2026-04-26) — buffer rather than drop.
    if (m_scanning) {
        m_rescanPending = true;
        return;
    }
    m_scanning = true;
    m_rescanPending = false;

    QStringList roots = m_bridge->rootFolders("comics");
    if (roots.isEmpty()) {
        // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- the empty-state
        // text "Search to add comics to your library" directs the user
        // at search (folder-import path is gone). With DOWNLOADED + BOOKMARKED
        // sections that no longer holds; m_progressKeyMap still gets cleared
        // because it's keyed off folder paths, but the landing strips are
        // refreshed via refreshLibraryStrips so a user with bookmarks but no
        // comics root still sees their bookmarks.
        m_listView->clear();
        m_progressKeyMap.clear();
        m_hasScanned = true;
        m_scanning = false;
        refreshLibraryStrips();
        refreshContinueStrip();
        return;
    }

    if (!m_hasScanned) {
        // First scan: clear progress-key map; landing tiles are driven by
        // refreshLibraryStrips (called in onScanFinished). No tile-strip
        // wipe here -- the DOWNLOADED + BOOKMARKED sections render whatever
        // the user already has independent of the scan.
        m_listView->clear();
        m_progressKeyMap.clear();
    }
    // Rescan: refreshLibraryStrips happens after scan completes

    QMetaObject::invokeMethod(m_scanner, "scan", Qt::QueuedConnection,
                              Q_ARG(QStringList, roots));
}

void ComicsPage::addSeriesTile(const SeriesInfo& series)
{
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- pre-pivot tile
    // rendering (folder-origin + Tankoyomi-origin merged into m_tileStrip)
    // is REMOVED. The new landing surfaces DOWNLOADED + BOOKMARKED sections
    // via refreshLibraryStrips() driven by MangaDownloadIndex +
    // AniListCache::bookmarkedPreviews.
    //
    // This function survives as a PURE progress-key-map populator: it walks
    // the cbz files inside series.seriesPath and registers each path under
    // its SHA1 progress key so refreshContinueStrip() can resolve
    // "in-progress chapter -> series" without re-walking disk every refresh.
    // Called from onScanFinished (folder-origin) AND addSeriesTile-for-record
    // path which we drop here -- m_folderSeries.length is the only caller now.
    QString thumbsDir = m_bridge->dataDir() + "/thumbs";
    QDir dir(series.seriesPath);
    for (const auto& f : dir.entryList(COMIC_EXTS, QDir::Files)) {
        QString fullPath = dir.absoluteFilePath(f);
        QString progressKey = QString(QCryptographicHash::hash(
            fullPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        QFileInfo fi(fullPath);
        QString fileKey = fullPath + "::" + QString::number(fi.size())
                        + "::" + QString::number(fi.lastModified().toMSecsSinceEpoch());
        QString fileHash = QString(QCryptographicHash::hash(
            fileKey.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        QString fileCover = thumbsDir + "/" + fileHash + ".jpg";
        QString coverPath = QFile::exists(fileCover) ? fileCover : series.coverThumbPath;
        m_progressKeyMap[progressKey] = {fullPath, series.seriesPath, coverPath};
    }
}

void ComicsPage::onSeriesFound(const SeriesInfo& series)
{
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- pre-pivot incremental
    // tile-render is gone. We still walk each discovered series to register
    // its cbz files in m_progressKeyMap (so refreshContinueStrip can resolve
    // in-progress chapters); the landing-tile sections themselves render
    // from MangaDownloadIndex + AniListCache::bookmarkedPreviews.
    if (m_hasScanned) return;
    addSeriesTile(series);  // progress-map population only -- no tile-strip mutation
}

void ComicsPage::onScanFinished(const QList<SeriesInfo>& allSeries)
{
    m_hasScanned = true;
    m_scanning = false;
    // REPO_HYGIENE Phase 4 P4.3 (2026-04-26) — fire pending rescan.
    if (m_rescanPending) {
        m_rescanPending = false;
        QTimer::singleShot(0, this, [this]() { triggerScan(); });
    }

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — cache folder-origin
    // SeriesInfo so legacy lookups (findFolderImportedSeriesPathForTitle)
    // still resolve. The scanner already excluded claimed paths, so
    // allSeries is the folder-origin slice only.
    m_folderSeries = allSeries;

    // Re-walk folder-origin cbz paths into m_progressKeyMap. Without this,
    // a rescan would leave m_progressKeyMap stale after files moved on disk.
    m_progressKeyMap.clear();
    for (const auto& s : m_folderSeries) addSeriesTile(s);

    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- rebuild the new
    // DOWNLOADED + BOOKMARKED tile sections. refreshLibraryStrips handles
    // its own empty-state (hides the section header when its strip is empty).
    refreshLibraryStrips();
    refreshContinueStrip();
}

void ComicsPage::onTankoyomiLibraryChanged()
{
    // Push refreshed claim set to the scanner (queued — scanner runs on
    // worker thread). Doesn't trigger a rescan: the folder-origin slice
    // is already in m_folderSeries; subsequent rescans pick up the new
    // claims automatically.
    if (m_scanner) {
        QMetaObject::invokeMethod(m_scanner, "setClaimedPaths", Qt::QueuedConnection,
                                  Q_ARG(QStringList, m_tyLibrary->claimedCanonicalPaths()));
    }
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- rebuildTiles() was a
    // pre-pivot helper that re-painted folder + Tankoyomi tiles into
    // m_tileStrip. With the new landing, library-state changes that affect
    // the chip-state are picked up by refreshTileChips; the DOWNLOADED +
    // BOOKMARKED sections are driven by their own signal subscriptions.
    refreshTileChips();
}

void ComicsPage::rebuildTiles()
{
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- pre-pivot merge of
    // folder-origin + Tankoyomi-library tiles is GONE. Kept as a thin
    // shim to refreshLibraryStrips for back-compat with any straggling
    // call site (none expected post-Phase-10; remove in Phase 11+ once
    // the burn-down is verified).
    refreshLibraryStrips();
}

void ComicsPage::refreshTileChips()
{
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 34 — for each
    // tile with a seriesKey (set in addSeriesTile only on Tankoyomi-origin
    // tiles), look up the active record in MangaDownloader and flip the
    // DOWNLOADING chip on if status ∈ {queued, downloading}, off otherwise.
    if (!m_tileStrip || !m_tyLibrary || !m_mangaDownloader) return;
    for (auto* card : m_tileStrip->tiles()) {
        if (!card) continue;
        const QString key = card->property("seriesKey").toString();
        if (key.isEmpty()) {
            card->setDownloadingChip(false);
            continue;
        }
        const int sep = key.indexOf(QLatin1Char(':'));
        if (sep <= 0) {
            card->setDownloadingChip(false);
            continue;
        }
        const QString sourceId = key.left(sep);
        const QString seriesId = key.mid(sep + 1);
        const auto libRec = m_tyLibrary->get(sourceId, seriesId);
        if (libRec.title.isEmpty()) {
            card->setDownloadingChip(false);
            continue;
        }
        const auto dlRec = m_mangaDownloader->recordForSeries(sourceId, libRec.title);
        const bool active = !dlRec.id.isEmpty() &&
                            (dlRec.status == QStringLiteral("queued") ||
                             dlRec.status == QStringLiteral("downloading"));
        card->setDownloadingChip(active);
    }
}

SeriesInfo ComicsPage::seriesInfoFromRecord(const ComicsLibraryRecord& r)
{
    // Tankoyomi-origin record → SeriesInfo projection. Shared between
    // onScanFinished (first-scan path) and rebuildTiles. Phase 3+ will
    // extend with fileCount/files once chapter discovery wires up;
    // until then those default to empty/zero from SeriesInfo's defaults.
    SeriesInfo s;
    s.seriesName     = r.title;
    s.seriesPath     = r.canonicalSeriesPath;
    s.coverThumbPath = r.coverPath;
    s.newestMtimeMs  = r.lastValidatedAt;
    s.provenance     = QStringLiteral("tankoyomi");
    return s;
}

QString ComicsPage::canonicalSeriesPathForPremium(
    const tankoban::manga::premium::PremiumCatalogEntry& entry) const
{
    // TANKOYOMI_PREMIUM Phase 7 Task 7.2 -- prefer an existing Tankoyomi
    // library record whose title matches the catalog entry (the merger arc
    // establishes that canonicalSeriesPath with collision disambiguation).
    // Otherwise fall back to <comics-root-0>/<sanitised-title>. The sanitise
    // step is kept local rather than reaching across files to the detail
    // view's file-local sanitiseFilename free function.
    if (m_tyLibrary) {
        const auto records = m_tyLibrary->all();
        for (const auto& r : records) {
            if (r.title.compare(entry.title, Qt::CaseInsensitive) == 0) {
                return r.canonicalSeriesPath;
            }
        }
    }
    const QStringList roots = m_bridge
        ? m_bridge->rootFolders(QStringLiteral("comics"))
        : QStringList{};
    if (roots.isEmpty()) return QString();
    const QString root = roots.first();
    if (root.isEmpty()) return QString();
    QString safe = entry.title;
    static const QString kBad = QStringLiteral("<>:\"/\\|?*");
    for (QChar c : kBad) safe.replace(c, QChar('_'));
    safe = safe.trimmed();
    return root + QChar('/') + safe;
}

QString ComicsPage::normalizeTitleForMatch(const QString& title)
{
    // TANKOYOMI_PREMIUM Phase 9 -- lowercase + strip whitespace + non-word
    // chars + underscores. Folds "Berserk", "Berserk!", " Berserk " into
    // one bucket; does NOT fold typo variants like "Bersek". Used by the
    // adopt-existing-folder lookup to match Premium-catalog titles against
    // pre-existing folder-imported series.
    static const QRegularExpression re(QStringLiteral("[\\s\\W_]+"));
    QString s = title.toLower().trimmed();
    s.remove(re);
    return s;
}

QString ComicsPage::findFolderImportedSeriesPathForTitle(const QString& title) const
{
    // TANKOYOMI_PREMIUM Phase 9 -- exactly-one-match contract. m_folderSeries
    // is folder-imported by construction (Tankoyomi-origin entries live in
    // m_tyLibrary, not here). Ambiguous (zero or many) returns empty so the
    // caller falls back to new-folder-creation. Per Codex section 22:
    // adopt, do not migrate.
    const QString needle = normalizeTitleForMatch(title);
    if (needle.isEmpty()) return QString();
    QStringList matches;
    for (const auto& s : m_folderSeries) {
        if (normalizeTitleForMatch(s.seriesName) == needle) {
            matches.append(s.seriesPath);
        }
    }
    if (matches.size() == 1) return matches.first();
    return QString();
}

QString ComicsPage::pendingVolumeKey(const QString& seriesId, int volumeNumber)
{
    return seriesId + QLatin1Char('|') + QString::number(volumeNumber);
}

void ComicsPage::rememberPendingVolumeDispatch(const QString& seriesId,
                                               int volumeNumber,
                                               PendingVolumeSourceKind kind,
                                               int anilistId,
                                               const QStringList& chapterIds)
{
    if (seriesId.isEmpty() || volumeNumber <= 0) return;
    PendingVolumeDispatch pending;
    pending.kind = kind;
    pending.anilistId = anilistId;
    pending.chapterIds = chapterIds;
    m_pendingVolumeDispatches.insert(pendingVolumeKey(seriesId, volumeNumber), pending);

    if (m_tyVolumeSeriesView && m_tyVolumeSeriesView->currentAnilistId() == anilistId) {
        m_tyVolumeSeriesView->setVolumeStatusText(volumeNumber, QStringLiteral("Downloading..."));
    }
}

void ComicsPage::ensureTankoyomiChapterInMap(const QString& cbzPath)
{
    // TANKOYOMI_CONTINUE_READING 2026-05-15 — bridge between today's
    // two-origin Continue-Reading model (folder-imported scanner walk +
    // Tankoyomi library records) and a future Tankoyomi-exclusive model.
    //
    // m_progressKeyMap normally gets populated only on a full library
    // rescan (via addSeriesTile walking each series' cbz files). A
    // chapter downloaded mid-session sits in canonicalSeriesPath on
    // disk but its progress key is unknown to refreshContinueStrip
    // until the next scan. This helper registers the path right before
    // the user reads it (called from the openComicRequested slot), so
    // ComicReader's saveProgress writes under a key the strip can
    // resolve at its very next refresh.
    //
    // Forward-compat: if Comics mode pivots to Tankoyomi-exclusive and
    // refreshContinueStrip is refactored to join MangaDownloadIndex ↔
    // JsonStoreBridge directly (no intermediate map), this helper +
    // m_progressKeyMap both become deletable.

    if (cbzPath.isEmpty() || !m_tyLibrary) return;

    const QString progressKey = comicProgressKeyForPath(cbzPath);
    if (m_progressKeyMap.contains(progressKey)) return;

    const QString parentDir = QFileInfo(cbzPath).absolutePath();
    const auto rec = m_tyLibrary->getByCanonicalPath(parentDir);
    if (!rec) return;  // not a Tankoyomi-claimed cbz — folder-imported
                       // path handled at scan-time by addSeriesTile.

    m_progressKeyMap[progressKey] = {cbzPath, rec->canonicalSeriesPath, rec->coverPath};
}

ComicsPage::ContinueLabels ComicsPage::continueLabelsForRecord(
    const ComicsLibraryRecord& rec, const QString& cbzPath, int page, int pageCount)
{
    // TANKOYOMI_CONTINUE_READING 2026-05-15 -- Title = series name (rec.title).
    // Subtitle = "<ChapterName> • Page X/Y" historically.
    //
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- volume-keyed format.
    // When the cbz filename encodes a volume number (Premium catalog volumes
    // ship as "<Series> v<NN>.cbz"; WeebCentralVolumePacker ships as
    // "Volume <NN>.cbz"), surface `<Series> - Vol N - Page X/Y` per plan.
    // When the cbz is a per-chapter file (MangaDownloader legacy: chapter
    // names land in the filename), fall back to the chapter-named subtitle
    // so users with pre-pivot downloads keep a recognisable label.
    //
    // Regex inventory:
    //   "v01" / "v1"        -- catalog volume convention
    //   "Vol 1" / "Volume 1" -- WC packer + manual rename convention
    const QString chapterName = QFileInfo(cbzPath).completeBaseName();
    const QString pageLabel = pageCount > 0
        ? QStringLiteral("Page %1/%2").arg(page + 1).arg(pageCount)
        : QStringLiteral("Page %1").arg(page + 1);

    // Volume X cbzs are named "Volume X.cbz" (no digit), so the numeric volRe
    // below misses them and the tile fell back to a bare "Page X/Y". Surface the
    // synthetic-volume label explicitly, matching the series-view tile.
    if (chapterName.compare(QStringLiteral("Volume X"), Qt::CaseInsensitive) == 0) {
        return { rec.title, QStringLiteral("Vol X - %1").arg(pageLabel) };
    }

    static const QRegularExpression volRe(
        QStringLiteral("(?:^|[\\s_-])(?:v|vol(?:ume)?)\\s*(\\d{1,3})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const auto volMatch = volRe.match(chapterName);
    if (volMatch.hasMatch()) {
        bool ok = false;
        const int vol = volMatch.captured(1).toInt(&ok);
        if (ok && vol > 0) {
            return {
                rec.title,
                QStringLiteral("Vol %1 - %2").arg(vol).arg(pageLabel)
            };
        }
    }

    return {
        rec.title,
        chapterName.isEmpty() ? pageLabel
                              : QStringLiteral("%1 - %2").arg(chapterName, pageLabel)
    };
}

int ComicsPage::anilistIdForDownloadEntry(const QString& sourceId,
                                          const QString& seriesId) const
{
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- map a download-index
    // (sourceId, seriesId) tuple back to an AniList id so the DOWNLOADED
    // tile can route to ComicsSeriesView. Two resolution paths:
    //
    //   1. Catalog rows ("tankoyomi_premium:<seriesId>") -- look up the
    //      PremiumCatalog entry; entry.anilistId is the answer.
    //   2. Runtime rows -- the seriesId is the synthesized "anilist_<N>"
    //      slug from onDownloadDispatchRequested. Strip the prefix and
    //      parse N.
    //
    // Returns 0 when no anilistId is resolvable; caller falls back to a
    // non-routable tile (still renders title + cover, click is a no-op).
    if (sourceId.isEmpty() || seriesId.isEmpty()) return 0;

    // 1. "anilist_<N>" synthesized slug carries the id directly. Checked FIRST
    //    so a stale/zero Premium-catalog entry can't shadow the real id.
    if (seriesId.startsWith(QLatin1String("anilist_"))) {
        bool ok = false;
        const int n = seriesId.mid(QStringLiteral("anilist_").size()).toInt(&ok);
        if (ok && n > 0) return n;
    }

    // 2. Premium catalog entry by id.
    if (sourceId == QLatin1String("tankoyomi_premium") && m_premiumCatalog) {
        if (auto entry = m_premiumCatalog->entryById(seriesId)) {
            if (entry->anilistId > 0) return entry->anilistId;
        }
    }

    // NOTE: MangaFire catalog JSONs frequently carry "anilistId": 0 (e.g.
    // one-piece.json), so resolving the id from the catalog is unreliable. The
    // canonical-key merge for catalog-slug series goes through the TITLE path
    // in resolveDisplayTitle + the bookmark cross-ref instead — see
    // COMICS_LIBRARY_DEDUP 2026-05-29 there.
    return 0;
}

// COMICS_DOWNLOAD_DISPLAY_PROJECTION 2026-05-26 (Agent 9) — canonical
// grouping key resolver. Maps (sourceId, seriesId) to a display-grouping
// key so One Piece downloads from Premium + MangaFire merge into one card.
// Priority: anilist:<id> > title:<normalized> > raw:<sourceId>:<seriesId>.
QString ComicsPage::resolveCanonicalGroupKey(const QString& sourceId,
                                              const QString& seriesId) const
{
    // 1. Prefer resolved AniList id from the entry directly.
    const int directAnilistId = anilistIdForDownloadEntry(sourceId, seriesId);
    if (directAnilistId > 0)
        return QStringLiteral("anilist:") + QString::number(directAnilistId);

    // 2. Resolve a human display title.
    const QString title = resolveDisplayTitle(sourceId, seriesId);
    if (!title.isEmpty()) {
        // 2a. Cross-reference AniList bookmarks: if a bookmark has a
        //     matching normalized title, adopt its anilistId so MangaFire
        //     "One Piece" groups under "anilist:30013" instead of
        //     "title:one-piece", merging with Premium's entry.
        //     COMICS_DOWNLOAD_DISPLAY_PROJECTION 2026-05-26 (Agent 9).
        if (m_anilistCache) {
            const auto previews = m_anilistCache->bookmarkedPreviews();
            const QString titleNorm = title.toLower().trimmed();
            for (const auto& p : previews) {
                if (p.title.toLower().trimmed() == titleNorm && p.anilistId > 0)
                    return QStringLiteral("anilist:") + QString::number(p.anilistId);
            }
        }

        // 2b. No bookmark match — group by normalized title.
        return QStringLiteral("title:")
            + title.toLower().trimmed().replace(QRegularExpression("[\\s]+"), QStringLiteral("-"));
    }

    // 3. Fallback to raw (sourceId, seriesId) key.
    return QStringLiteral("raw:") + sourceId + QStringLiteral(":") + seriesId;
}

// COMICS_DOWNLOAD_DISPLAY_PROJECTION 2026-05-26 (Agent 9) — title resolution.
// Order: AniList cache → MangaFire local catalog → Premium catalog →
// Tankoyomi library record → empty (caller humanizes slug).
QString ComicsPage::resolveDisplayTitle(const QString& sourceId,
                                         const QString& seriesId) const
{
    // 1. AniList cache resolution.
    const int anilistId = anilistIdForDownloadEntry(sourceId, seriesId);
    if (anilistId > 0 && m_anilistCache) {
        if (auto detailOpt = m_anilistCache->get(anilistId)) {
            if (!detailOpt->preview.title.isEmpty())
                return detailOpt->preview.title;
        }
    }

    // 2. MangaFire local catalog — SOURCE-AGNOSTIC.
    //    COMICS_LIBRARY_DEDUP 2026-05-29 (Agent 1). The seriesId IS the MangaFire
    //    catalog slug ("one-piece") for BOTH "mangafire_catalog" AND
    //    "weebcentral" downloads (WeebCentralVolumePacker registers with the
    //    slug). Previously this lookup was gated on sourceId=="mangafire_catalog",
    //    so a "weebcentral:one-piece" bucket resolved no title → resolveCanonical-
    //    GroupKey fell to "raw:weebcentral:one-piece", a SEPARATE group from the
    //    mangafire bucket's "anilist:30013" → One Piece showed twice. Resolving
    //    the catalog title for any source lets the bookmark cross-ref collapse
    //    every source's bucket onto one canonical key. filePathForSlug returns
    //    empty for non-slug seriesIds (e.g. "anilist_30013"), so this is safe.
    {
        const QString jsonPath = m_localCatalogIndex.filePathForSlug(seriesId);
        if (!jsonPath.isEmpty()) {
            const auto cat =
                tankoban::manga::LocalMangaCatalogLoader::loadFromFile(jsonPath);
            if (cat.has_value() && !cat->seriesTitle.isEmpty())
                return cat->seriesTitle;
        }
    }

    // 3. Premium catalog.
    if (m_premiumCatalog) {
        if (auto entry = m_premiumCatalog->entryById(seriesId)) {
            if (!entry->title.isEmpty())
                return entry->title;
        }
    }

    // 4. Tankoyomi library record.
    if (m_tyLibrary) {
        const auto rec = m_tyLibrary->get(sourceId, seriesId);
        if (!rec.title.isEmpty())
            return rec.title;
    }

    return {};
}

// COMICS_DOWNLOAD_DISPLAY_PROJECTION 2026-05-26 (Agent 9) — source label.
// Static helper: maps sourceId to a human-readable display name.
QString ComicsPage::resolveSourceLabel(const QString& sourceId)
{
    if (sourceId == QLatin1String("tankoyomi_premium"))
        return QStringLiteral("Premium");
    // COMICS_WC_SOURCE_LABEL_FIX 2026-05-26 (Agent 9).
    // Pre-fix WeebCentralVolumePacker stored downloads with sourceId
    // "mangafire_catalog". Map them to "WeebCentral" since the actual
    // download origin was WeebCentral (not MangaFire). MangaFire is
    // a catalog/series identity, not a download origin, and only
    // WeebCentralVolumePacker ever used this sourceId for downloads.
    if (sourceId == QLatin1String("mangafire_catalog"))
        return QStringLiteral("WeebCentral");
    if (sourceId == QLatin1String("weebcentral_packer")
        || sourceId == QLatin1String("weebcentral"))
        return QStringLiteral("WeebCentral");

    // Fallback: strip known suffixes, then title-case.
    QString label = sourceId;
    static const QStringList suffixes = {
        QStringLiteral("_catalog"), QStringLiteral("_packer"),
        QStringLiteral("_runtime"), QStringLiteral("_source")
    };
    for (const auto& sfx : suffixes) {
        if (label.endsWith(sfx, Qt::CaseInsensitive))
            label = label.left(label.size() - sfx.size());
    }
    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (label.size() > 0) {
        label = label.at(0).toUpper() + label.mid(1).toLower();
    }
    return label;
}

// COMICS_DOWNLOAD_DISPLAY_PROJECTION 2026-05-26 (Agent 9) — slug humanizer.
// "one-piece" → "One Piece", "grand-blue-dreaming" → "Grand Blue Dreaming".
// Returns empty for anilist_<N> slugs (callers must resolve via AniList first).
QString ComicsPage::humanizeSlug(const QString& slug)
{
    if (slug.isEmpty())
        return {};

    // AniList ids are identity hints, not display titles.
    if (slug.startsWith(QLatin1String("anilist_")))
        return {};

    QString s = slug;
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    s.replace(QLatin1Char('-'), QLatin1Char(' '));

    QStringList words = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (auto& w : words) {
        if (w.size() == 1)
            w = w.toUpper();
        else if (w.size() > 1)
            w = w.at(0).toUpper() + w.mid(1).toLower();
    }

    return words.join(QLatin1Char(' '));
}

// COMICS_CANONICAL_COVER 2026-05-26 (Agent 9) — canonical-series-cover
// resolver. Priority: MangaFire catalog Volume 1 coverUrlJapanese → empty
// (caller falls back to existing AniList → CBZ thumbnail → placeholder).
// Looks up the series in m_localCatalogIndex by anilistId or displayTitle,
// loads the catalog JSON, and returns Volume 1's coverUrlJapanese.
QString ComicsPage::resolveCanonicalSeriesCover(int anilistId,
                                                 const QString& displayTitle) const
{
    // 1. Look up the catalog slug via anilistId or title.
    QString slug;
    if (anilistId > 0) {
        slug = m_localCatalogIndex.slugForAnilistId(anilistId);
    }
    if (slug.isEmpty() && !displayTitle.isEmpty()) {
        slug = m_localCatalogIndex.slugForSeriesTitle(displayTitle);
    }
    if (slug.isEmpty())
        return QString();

    const QString path = m_localCatalogIndex.filePathForSlug(slug);
    if (path.isEmpty())
        return QString();

    // 2. Load the catalog JSON and extract Volume 1's cover.
    const auto catalog = tankoban::manga::LocalMangaCatalogLoader::loadFromFile(path);
    if (!catalog.has_value())
        return QString();

    for (const auto& vol : catalog->volumes) {
        if (vol.volumeNumber == 1 && !vol.coverUrlJapanese.isEmpty())
            return vol.coverUrlJapanese;
    }
    return QString();
}

// COMICS_CR_VOLUME_COVER 2026-05-29 (Agent 1) — see header. Returns the given
// volume's catalog cover, else Volume 1's, else empty.
QString ComicsPage::resolveReadVolumeCover(const QString& displayTitle,
                                           int volumeNumber) const
{
    if (displayTitle.isEmpty() || volumeNumber <= 0)
        return QString();

    const QString slug = m_localCatalogIndex.slugForSeriesTitle(displayTitle);
    if (slug.isEmpty())
        return QString();
    const QString path = m_localCatalogIndex.filePathForSlug(slug);
    if (path.isEmpty())
        return QString();
    const auto catalog = tankoban::manga::LocalMangaCatalogLoader::loadFromFile(path);
    if (!catalog.has_value())
        return QString();

    QString vol1Cover;
    for (const auto& vol : catalog->volumes) {
        if (vol.volumeNumber == volumeNumber && !vol.coverUrlJapanese.isEmpty())
            return vol.coverUrlJapanese;                 // the read volume's real cover
        if (vol.volumeNumber == 1 && !vol.coverUrlJapanese.isEmpty())
            vol1Cover = vol.coverUrlJapanese;            // series fallback
    }
    return vol1Cover;
}

void ComicsPage::onProviderVolumeCompleted(const QString& seriesId,
                                           int volumeNumber,
                                           const QString& cbzPath,
                                           int fallbackSourceKind)
{
    PendingVolumeSourceKind kind = static_cast<PendingVolumeSourceKind>(fallbackSourceKind);
    int anilistId = 0;
    QStringList chapterIds;

    const QString key = pendingVolumeKey(seriesId, volumeNumber);
    const auto it = m_pendingVolumeDispatches.find(key);
    if (it != m_pendingVolumeDispatches.end()) {
        kind = it->kind;
        anilistId = it->anilistId;
        chapterIds = it->chapterIds;
        m_pendingVolumeDispatches.erase(it);
    }

    QString sourceId;
    switch (kind) {
        case PendingVolumeSourceKind::Catalog:
            sourceId = QString::fromLatin1(TANKOYOMI_PREMIUM_SOURCE_ID);
            break;
        case PendingVolumeSourceKind::NyaaRuntime:
            sourceId = QString::fromLatin1(TANKOYOMI_PREMIUM_SOURCE_ID);
            break;
        case PendingVolumeSourceKind::WeebCentralPacker:
            // VOLUME_X 2026-05-26 (Agent 9). WeebCentral-packed volumes
            // always use the packer source id regardless of whether the
            // series came through MangaFire catalog or AniList. Previously
            // MangaFire-catalog series were tagged as "mangafire_catalog"
            // which caused the downloads page to show "MangaFire" instead
            // of "WeebCentral" for volumes packed by WeebCentralVolumePacker.
            sourceId = QString::fromLatin1(WEEBCENTRAL_PACKER_SOURCE_ID);
            break;
        case PendingVolumeSourceKind::WesternGetComics:
            // COMICS_WESTERN_DOWNLOAD 2026-06-02 (Agent 1). Western volumes
            // are attributed to GetComics so the downloads index and downloads
            // page can display the correct source label.
            sourceId = QString::fromLatin1(GETCOMICS_SOURCE_ID);
            break;
    }

    if (anilistId <= 0) {
        anilistId = anilistIdForDownloadEntry(sourceId, seriesId);
    }

    if (m_mangaDownloadIndex &&
        (kind == PendingVolumeSourceKind::NyaaRuntime ||
         kind == PendingVolumeSourceKind::WeebCentralPacker ||
         kind == PendingVolumeSourceKind::WesternGetComics)) {
        m_mangaDownloadIndex->registerVolume(sourceId, seriesId, volumeNumber, cbzPath,
                                             QFileInfo(cbzPath).size(), chapterIds);
    }

    const bool currentMangaFireVolume =
        kind == PendingVolumeSourceKind::WeebCentralPacker &&
        seriesId == m_currentWcResolveKey.seriesId &&
        volumeNumber == m_currentWcResolveKey.volumeNumber;
    // COMICS_WESTERN_DOWNLOAD 2026-06-02 (Agent 1). Western volumes have no
    // AniList id (m_currentDetailAnilistId == 0); match them by seriesId
    // against m_pendingWesternSeriesId (the live series currently open).
    const bool currentWesternVolume =
        kind == PendingVolumeSourceKind::WesternGetComics &&
        !m_pendingWesternSeriesId.isEmpty() &&
        seriesId == m_pendingWesternSeriesId;
    if (m_tyVolumeSeriesView &&
        ((anilistId > 0 && m_tyVolumeSeriesView->currentAnilistId() == anilistId) ||
         currentMangaFireVolume ||
         currentWesternVolume)) {
        m_tyVolumeSeriesView->setVolumeDownloadState(volumeNumber, cbzPath, true);
    }

    // COMICS_WC_AUTOBOOKMARK 2026-05-24 (Agent 1). Stremio-parity: downloading
    // a volume implicitly adds the series to the library. Only fires when we
    // have a resolved anilistId (from the dispatch-time enrichment) AND the
    // series isn't already bookmarked. Series with no AniList match still
    // appear in the library via the merged-download-tile path (Bug 1 fix).
    if (anilistId > 0 && m_anilistCache && !m_anilistCache->isBookmarked(anilistId)) {
        m_anilistCache->addBookmark(anilistId);
        qInfo("ComicsPage::onProviderVolumeCompleted: auto-bookmarked anilistId=%d "
              "(download-implies-library)", anilistId);
    }
}

void ComicsPage::onProviderVolumeFailed(const QString& seriesId,
                                        int volumeNumber,
                                        const QString& errorCode,
                                        const QString& errorMessage,
                                        int fallbackSourceKind)
{
    PendingVolumeSourceKind kind = static_cast<PendingVolumeSourceKind>(fallbackSourceKind);
    int anilistId = 0;

    const QString key = pendingVolumeKey(seriesId, volumeNumber);
    const auto it = m_pendingVolumeDispatches.find(key);
    if (it != m_pendingVolumeDispatches.end()) {
        kind = it->kind;
        anilistId = it->anilistId;
        m_pendingVolumeDispatches.erase(it);
    }

    if (anilistId <= 0) {
        const QString sourceId = (kind == PendingVolumeSourceKind::WeebCentralPacker)
            ? QString::fromLatin1(WEEBCENTRAL_PACKER_SOURCE_ID)
            : QString::fromLatin1(TANKOYOMI_PREMIUM_SOURCE_ID);
        anilistId = anilistIdForDownloadEntry(sourceId, seriesId);
    }

    const bool currentMangaFireVolume =
        kind == PendingVolumeSourceKind::WeebCentralPacker &&
        seriesId == m_currentWcResolveKey.seriesId &&
        volumeNumber == m_currentWcResolveKey.volumeNumber;
    const bool currentWesternVolume =
        kind == PendingVolumeSourceKind::WesternGetComics &&
        !m_pendingWesternSeriesId.isEmpty() &&
        seriesId == m_pendingWesternSeriesId;
    if (m_tyVolumeSeriesView &&
        ((anilistId > 0 && m_tyVolumeSeriesView->currentAnilistId() == anilistId) ||
         currentMangaFireVolume ||
         currentWesternVolume)) {
        m_tyVolumeSeriesView->setVolumeStatusText(volumeNumber, QStringLiteral("Failed"));
    }

    qDebug().noquote()
        << "[volumeCompleted adapter] volume failed"
        << seriesId
        << QStringLiteral("v%1").arg(volumeNumber)
        << errorCode
        << errorMessage;
}

void ComicsPage::onComicsSeriesOpenVolume(int volumeNumber, const QString& cbzPath)
{
    Q_UNUSED(volumeNumber);
    if (cbzPath.isEmpty() || !QFileInfo(cbzPath).exists()) return;

    const QFileInfo fi(cbzPath);
    QDir dir(fi.absolutePath());
    QStringList files = dir.entryList(COMIC_EXTS, QDir::Files);
    QCollator col;
    col.setNumericMode(true);
    std::sort(files.begin(), files.end(), [&col](const QString& a, const QString& b) {
        return col.compare(a, b) < 0;
    });

    QStringList cbzList;
    cbzList.reserve(files.size());
    for (const auto& f : files) {
        cbzList.append(dir.absoluteFilePath(f));
    }

    ensureTankoyomiChapterInMap(cbzPath);
    const QString seriesName = !m_currentDetailSeriesTitle.isEmpty()
        ? m_currentDetailSeriesTitle
        : dir.dirName();
    emit openComic(cbzPath, cbzList, seriesName);
}

void ComicsPage::fetchPosterForTile(TileCard* card, int anilistId,
                                     const QString& coverUrl)
{
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- async poster fetch.
    // COMICS_CANONICAL_COVER 2026-05-26 (Agent 9) — extended to handle
    // MangaFire Volume 1 cover URLs where anilistId may be 0. Cache
    // naming: anilist_<id>.jpg when id > 0; mangafire_<base64>.jpg
    // when id == 0 (URL-derived stable key).
    // Mirrors ComicsTankoyomiSearchWidget's NAM-direct path: cache to
    // <writableData>/Tankoban/data/anilist_posters/; reuse an existing
    // cached file when present (no network round-trip on re-entry).
    // Failures stay silent (placeholder remains).
    if (!card || (anilistId <= 0 && coverUrl.isEmpty())) return;

    const QString posterCacheDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/Tankoban/data/anilist_posters");
    QDir().mkpath(posterCacheDir);
    QString outPath;
    if (anilistId > 0) {
        outPath = posterCacheDir + QStringLiteral("/anilist_%1.jpg")
                                       .arg(anilistId);
    } else {
        // Cover keyed by a FULL hash of the URL. A truncated base64 prefix
        // collided across same-host URLs: every rcostation /Uploads/... cover
        // shares a >30-char prefix, so base64(url).left(40) was IDENTICAL for
        // all of them -> every Western cover collapsed to one cache file (all
        // showed whichever downloaded first). MD5 hex of the full URL is
        // collision-free. (COMICS_WESTERN_RICHNESS cover-dup fix 2026-06-01.)
        const QByteArray urlKey = QCryptographicHash::hash(
            coverUrl.toUtf8(), QCryptographicHash::Md5).toHex();
        outPath = posterCacheDir + QStringLiteral("/cover_")
                  + QString::fromLatin1(urlKey)
                  + QStringLiteral(".jpg");
    }

    if (QFile::exists(outPath)) {
        card->setThumbPath(outPath);
        return;
    }

    if (!m_nam || coverUrl.isEmpty()) return;

    QNetworkRequest req{QUrl(coverUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    req.setTransferTimeout(10000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QPointer<TileCard> guard(card);
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, outPath, guard]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const QByteArray data = reply->readAll();
        if (data.isEmpty()) return;
        QFile f(outPath);
        if (!f.open(QIODevice::WriteOnly)) return;
        f.write(data);
        f.close();
        if (guard) guard->setThumbPath(outPath);
    });
}

// COMICS_MANGAFIRE_PIVOT Phase B.2 (2026-05-23). Forward decl for the slug
// helper used by all showSeries call sites below; definition lives at the
// end of this TU next to dispatchCatalogResolve + the slot implementations.
static QString fandomSeriesSlugFromTitle(const QString& title);

void ComicsPage::openSeriesByAnilistId(int anilistId, const QString& fallbackTitle)
{
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- DOWNLOADED + BOOKMARKED
    // tile click resolution. Prefer a fully populated MediaPreview from the
    // AniListCache; if absent (e.g. user bookmarked a series and the cache
    // has only the bookmark id but no series detail yet -- unlikely because
    // bookmark add goes via showSeries which populates the cache, but
    // defensive), synthesize a minimal preview from the (id, title) pair.
    // ComicsSeriesView's showSeries fires a background refetch in the
    // cache-miss case, so the surface lights up after a network round-trip.
    if (anilistId <= 0 || !m_tyVolumeSeriesView) return;

    tankoban::manga::anilist::MediaPreview preview;
    preview.anilistId = anilistId;
    preview.title     = fallbackTitle;

    if (m_anilistCache) {
        if (auto detailOpt = m_anilistCache->get(anilistId)) {
            preview = detailOpt->preview;
        }
    }

    // PHASE 0 NAV CONTRACT RESTORE follow-up 2026-05-17 (Agent 5) — emit
    // BEFORE the in-page state change so MainWindow's recordNavEvent("comics")
    // connection captures the OLD Library state into the current NavHistory
    // entry and pushes a fresh TankoyomiDetail entry for the target. Without
    // this emit, the BOOKMARKED + DOWNLOADED tile-click paths (the two
    // callers of this method, added in TANKOYOMI_VOLUME_PIVOT Phase 10) land
    // the user in a deep series view with no back-stack entry, leaving the
    // topbar Back chevron disabled (canGoBack returns false). The first
    // Phase 0 pass missed this path -- it only emitted in the Phase 9 paths
    // (showSearchMode, onSearchResultActivated, openSeriesByPath). Guarded
    // Guarded against m_inNavRestore so restoreLayer does not re-emit.
    if (!m_inNavRestore) {
        QJsonObject blob;
        blob[QStringLiteral("anilistId")]   = anilistId;
        blob[QStringLiteral("seriesTitle")] = preview.title;
        blob[QStringLiteral("enteredFrom")] = QStringLiteral("library");
        emit enteredLayer(makeComicsLayer(QStringLiteral("seriesView"), preview.title, blob));
    }
    m_enteredDetailFrom        = Mode::Library;
    m_mode                     = Mode::TankoyomiDetail;
    m_currentDetailAnilistId   = anilistId;
    m_currentDetailSeriesTitle = preview.title;
    m_detailEnteredFromWestern = false;  // manga open -> in-view Back goes to manga lib, not the Western grid (Codex review 2026-06-02)
    m_tyVolumeSeriesView->showSeries(preview);
    dispatchCatalogResolve(fandomSeriesSlugFromTitle(preview.title),
                           /*titleHint*/preview.title);
    m_stack->setCurrentWidget(m_tyVolumeSeriesView);
}

void ComicsPage::openSeriesByRecord(const ComicsLibraryRecord& record)
{
    // WEEBCENTRAL_IDENTITY_PIVOT Task 11 (2026-05-19) -- library-tile click
    // for WeebCentral-keyed downloaded series. Reconstructs a MangaResult
    // from the record (sourceId+seriesId identity, local cover) and routes
    // into ComicsSeriesView::showSeries(MangaResult), skipping the AniList
    // resolution chain entirely.
    if (!m_tyVolumeSeriesView) return;

    MangaResult result;
    result.id           = record.seriesId;
    result.url          = QString();   // not stored in library record
    result.title        = record.title;
    result.author       = record.detailCache.author;
    result.thumbnailUrl = record.coverPath.isEmpty()
                          ? QString()
                          : QStringLiteral("file:///") + record.coverPath;
    result.source       = record.sourceId;
    result.status       = record.detailCache.status;
    result.type         = QStringLiteral("manga");

    if (!m_inNavRestore) {
        QJsonObject blob;
        blob[QStringLiteral("sourceId")]    = record.sourceId;
        blob[QStringLiteral("seriesId")]    = record.seriesId;
        blob[QStringLiteral("seriesTitle")] = record.title;
        blob[QStringLiteral("enteredFrom")] = QStringLiteral("library");
        emit enteredLayer(makeComicsLayer(QStringLiteral("seriesView"), record.title, blob));
    }
    m_enteredDetailFrom        = Mode::Library;
    m_mode                     = Mode::TankoyomiDetail;
    m_currentDetailAnilistId   = 0;   // WeebCentral record has no AniList integer id
    m_currentDetailSeriesTitle = record.title;
    m_detailEnteredFromWestern = false;  // manga open -> in-view Back goes to manga lib, not the Western grid (Codex review 2026-06-02)
    m_tyVolumeSeriesView->showSeries(result);
    dispatchCatalogResolve(fandomSeriesSlugFromTitle(record.title),
                           /*titleHint*/record.title);
    m_stack->setCurrentWidget(m_tyVolumeSeriesView);
}

// ── COMICS_WESTERN_CATALOGUE Task 7 (2026-05-31, Agent 2) ───────────────────
// Shared-recipe search row builder (2026-06-02, Agent 5).
// Constructs the standard Comics search chrome (input + busy spinner + icon
// button) that is reused by BOTH the manga shelf and the Western shelf.
// All three live widget pointers are written to the out-params before return.
// The returned QWidget* container is parented to this (ComicsPage) so it
// auto-destructs with the page; each caller adds it to its own layout.
QWidget* ComicsPage::buildSearchRow(QLineEdit*& outBar,
                                    QWidget*&   outBusy,
                                    QPushButton*& outBtn,
                                    const QString& placeholder,
                                    const QString& sourceId)
{
    auto* container = new QWidget(this);
    auto* hbox = new QHBoxLayout(container);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(8);

    auto* bar = new QLineEdit(container);
    bar->setObjectName(QStringLiteral("LibrarySearch"));
    bar->setPlaceholderText(placeholder);
    bar->setClearButtonEnabled(true);
    bar->setFixedHeight(36);
    bar->setStyleSheet(
        "QLineEdit#LibrarySearch { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px; color: #eee; padding: 4px 10px; font-size: 13px; }"
        "QLineEdit#LibrarySearch:focus { border: 1px solid rgba(255,255,255,0.3); }");
    hbox->addWidget(bar, 1);

    auto* busyWidget = new QProgressBar(container);
    busyWidget->setRange(0, 0);
    busyWidget->setTextVisible(false);
    busyWidget->setFixedSize(16, 16);
    busyWidget->setObjectName(QStringLiteral("ComicsSearchBusy"));
    busyWidget->setStyleSheet(
        "#ComicsSearchBusy { background: transparent; border: none; }"
        "#ComicsSearchBusy::chunk { background: rgba(255,255,255,0.5); }");
    busyWidget->hide();
    hbox->addWidget(busyWidget);

    auto* btn = new QPushButton(container);
    btn->setFixedSize(36, 36);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName(QStringLiteral("ComicsSearchBtn"));
    btn->setIcon(QIcon(QStringLiteral(":/icons/search.svg")));
    btn->setIconSize(QSize(18, 18));
    btn->setToolTip(tr("Search"));
    hbox->addWidget(btn);

    // Write out-params before capturing bar by value in lambdas.
    outBar  = bar;
    outBusy = busyWidget;
    outBtn  = btn;

    // Capture bar by value (local copy) so each search row's lambdas
    // reference its OWN QLineEdit, not the out-param reference.
    const QString sid = sourceId;
    auto submit = [this, bar, sid]() {
        const QString q = bar->text().trimmed();
        if (q.isEmpty()) return;
        if (m_searchTakeover) m_searchTakeover->setActiveSourceId(sid);
        hideSearchHistoryDropdown();
        showSearchMode(q);
    };

    connect(btn, &QPushButton::clicked,    this, submit);
    connect(bar, &QLineEdit::returnPressed, this, submit);
    connect(bar, &QLineEdit::textChanged,   this, [this, bar]() {
        const bool hasText = !bar->text().trimmed().isEmpty();
        bar->setProperty("activeSearch", hasText);
        bar->style()->unpolish(bar);
        bar->style()->polish(bar);
        if (hasText) {
            hideSearchHistoryDropdown();
        } else if (bar->hasFocus()) {
            showSearchHistoryDropdown();
        }
    });

    bar->installEventFilter(this);
    return container;
}
// Western browse shelf. A separate browse grid (its own m_stack screen) lists
// the curated Western series from data/western_catalogue/*.json; opening one
// renders its collected editions through the SAME ComicsSeriesView tile path
// that manga uses — but render-only, guarded against the manga enrichment path.

void ComicsPage::buildWesternScreen()
{
    auto* scroll = new QScrollArea();
    scroll->setObjectName("WesternGridScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea#WesternGridScroll { background: transparent; border: none; }");

    auto* page = new QWidget();
    page->setObjectName("WesternGridPage");
    page->setStyleSheet("QWidget#WesternGridPage { background: transparent; }");
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(20, 20, 20, 20);
    v->setSpacing(16);

        // Shared-recipe builder (2026-06-02): Western bar gets full manga parity
    // (spinner + search icon + history dropdown). Heading removed for
    // top-rhythm parity with the manga shelf (pills already label the shelf).
    {
        QWidget* westernSearchRow = buildSearchRow(
            m_westernSearchBar, m_westernSearchBusy, m_westernSearchBtn,
            QStringLiteral("Search Comics"),
            QStringLiteral("readcomicsonline"));
        v->addWidget(westernSearchRow);
    }

    m_westernGrid = new TileStrip(page);
    m_westernGrid->setMode(QStringLiteral("fixedGrid"));
    const int westernDensity = QSettings("Tankoban", "Tankoban").value("grid_cover_size", 1).toInt();
    m_westernGrid->setDensity(qBound(0, westernDensity, 2));
    v->addWidget(m_westernGrid);
    v->addStretch();

    // Single-click opens the Western series (collected-edition detail view).
    connect(m_westernGrid, &TileStrip::tileSingleClicked, this, [this](TileCard* card) {
        if (!card) return;
        openWesternSeriesFromJson(card->property("westernJsonPath").toString());
    });

    scroll->setWidget(page);
    m_westernScroll = scroll;
    m_westernStackIndex = m_stack->addWidget(scroll);
}

void ComicsPage::refreshWesternGrid()
{
    if (!m_westernGrid) return;
    m_westernGrid->clear();

    const QString dirPath = tankoban::manga::WesternCatalogLoader::canonicalDataDir();
    QDir dir(dirPath);
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"),
                                            QDir::Files, QDir::Name);
    for (const QString& f : files) {
        const QString path = dir.absoluteFilePath(f);
        const auto cat = tankoban::manga::WesternCatalogLoader::loadFromFile(path);
        if (!cat.has_value()) continue;

        // Shared series hero cover. The cover is a REMOTE url (rcostation
        // /Uploads absolutised by the loader, or an absolute blogspot url),
        // not a local file -- so it can't go through TileCard's ctor thumbPath
        // (QPixmap(path) only loads local files; a url fails silently -> blank
        // box). Mirror the manga grid: construct with an empty placeholder,
        // then async-fetch + cache the url via fetchPosterForTile, which calls
        // setThumbPath when the download lands. Empty cover -> text-tile
        // placeholder stays (cover-tolerant by design).
        // Prefer the series-level cover (set by WesternCatalogLoader, survives an
        // empty editions list); fall back to volume 1's cover for legacy baked
        // files that predate the seriesCover field. (2026-06-02 — editionless
        // series like The Walking Dead otherwise show a blank grid tile.)
        const QString coverUrl = !cat->seriesCover.isEmpty()
                                     ? cat->seriesCover
                                     : (cat->volumes.isEmpty()
                                            ? QString()
                                            : cat->volumes.first().coverUrlJapanese);
        auto* card = new TileCard(QString(), cat->seriesTitle, tr("Western"));
        card->setProperty("westernJsonPath", path);
        card->setProperty("seriesName", cat->seriesTitle);
        m_westernGrid->addTile(card);
        if (!coverUrl.isEmpty()) fetchPosterForTile(card, 0, coverUrl);
    }
}

void ComicsPage::openWesternSeriesFromJson(const QString& jsonPath)
{
    if (jsonPath.isEmpty() || !m_tyVolumeSeriesView) return;
    const auto catalog = tankoban::manga::WesternCatalogLoader::loadFromFile(jsonPath);
    if (!catalog.has_value()) {
        qInfo("ComicsPage::openWesternSeriesFromJson: loadFromFile failed for %s",
              qUtf8Printable(jsonPath));
        return;
    }
    // A baked file on disk IS the shelf — opening one is always "on shelf".
    openWesternSeriesFromCatalog(*catalog, jsonPath, /*onShelf*/true);
}

void ComicsPage::openWesternSeriesFromCatalog(const tankoban::manga::MangaCatalog& catalog,
                                              const QString& jsonPath,
                                              bool onShelf)
{
    if (!m_tyVolumeSeriesView) return;

    // Make THIS the current Western series so an edition Download knows what to
    // fetch — for BOTH the live-search path (the westernSeriesReady slot pre-set
    // these before calling) AND the baked shelf-open path (jsonPath non-empty:
    // load the JSON here). Before this fix, m_pendingWesternSeriesId was set only
    // on live search, so Download silently no-oped for a series opened from the
    // shelf (2026-06-02). For the live path jsonPath is empty and the slot's
    // m_pendingWesternJson is preserved.
    m_pendingWesternSeriesId = catalog.seriesId;
    if (!jsonPath.isEmpty()) {
        QFile jf(jsonPath);
        if (jf.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(jf.readAll());
            if (doc.isObject()) m_pendingWesternJson = doc.object();
        }
    }

    // Nav entry so the topbar Back chevron works (mirrors openSeriesByRecord,
    // Western enteredFrom blob). jsonPath is empty for a live (unsaved) series;
    // the restore path falls back to a fresh fetch when it's absent.
    if (!m_inNavRestore) {
        QJsonObject blob;
        blob[QStringLiteral("seriesId")]    = catalog.seriesId;
        blob[QStringLiteral("seriesTitle")] = catalog.seriesTitle;
        blob[QStringLiteral("enteredFrom")] = QStringLiteral("western");
        blob[QStringLiteral("jsonPath")]    = jsonPath;  // exact reload on restore
        emit enteredLayer(makeComicsLayer(QStringLiteral("seriesView"),
                                          catalog.seriesTitle, blob));
    }
    m_enteredDetailFrom        = Mode::Library;
    m_detailEnteredFromWestern = true;  // in-view Back -> Western grid, not manga
    m_mode                     = Mode::TankoyomiDetail;
    m_currentDetailAnilistId   = 0;   // no AniList id => the enrichment path has nothing to chase
    m_currentDetailSeriesTitle = catalog.seriesTitle;

    // GUARD (Agent 1, domain owner): render DIRECTLY. Do NOT route through
    // showSeries()/dispatchCatalogResolve() — those auto-fire AniList +
    // mangafire resolution (COMICS_WC_AUTOENRICH), which would bleed MANGA
    // enrichment onto a Western comic. populateVolumeRowsFromCatalog is
    // pure-render/no-network (its own header comment confirms it), so the
    // direct call is clean.
    m_tyVolumeSeriesView->populateVolumeRowsFromCatalog(catalog);
    // CORRECTION (continuation note): populateVolumeRowsFromCatalog RESETS the
    // Western shelf flag, so setWesternOnShelf MUST be called AFTER it — not
    // before — or the "On shelf"/"Add to Library" state gets clobbered.
    m_tyVolumeSeriesView->setWesternOnShelf(onShelf);
    m_stack->setCurrentWidget(m_tyVolumeSeriesView);
}

void ComicsPage::showMangaMode()
{
    if (m_mangaTabBtn)   m_mangaTabBtn->setChecked(true);
    if (m_westernTabBtn) m_westernTabBtn->setChecked(false);
    if (m_stack) m_stack->setCurrentIndex(0);
    // Top search bar searches manga (WeebCentral) while on the manga shelf.
    if (m_searchTakeover) m_searchTakeover->setActiveSourceId(QStringLiteral("weebcentral"));
}

void ComicsPage::showWesternMode()
{
    if (m_mangaTabBtn)   m_mangaTabBtn->setChecked(false);
    if (m_westernTabBtn) m_westernTabBtn->setChecked(true);

    // Push a nav layer when entering the Western grid (user-initiated only —
    // m_inNavRestore suppresses re-emit during Back/restore). Without this,
    // Back from a Western detail would pop straight to manga home; with it, the
    // back-stack is library -> westernGrid -> seriesView, so Back from a Western
    // series returns to the Western grid, and Back again to manga home.
    // Skip the push if already on the Western grid (avoids duplicate layers on
    // repeated pill clicks).
    const bool alreadyWestern = (m_stack && m_westernStackIndex >= 0
                                 && m_stack->currentIndex() == m_westernStackIndex);
    if (!m_inNavRestore && !alreadyWestern) {
        emit enteredLayer(makeComicsLayer(QStringLiteral("westernGrid"),
                                          tr("Western"), QJsonObject{}));
    }

    refreshWesternGrid();
    if (m_stack && m_westernStackIndex >= 0)
        m_stack->setCurrentIndex(m_westernStackIndex);
    // Top search bar searches comics (RCO) while on the Western shelf — this is
    // what makes a Western search query hit readcomicsonline and surface comic
    // results whose source == "readcomicsonline" (routed live in onSearchResultActivated).
    if (m_searchTakeover) m_searchTakeover->setActiveSourceId(QStringLiteral("readcomicsonline"));
}

void ComicsPage::openSeriesForDownloadEntry(const QString& sourceId,
                                             const QString& seriesId,
                                             const QString& displayTitle)
{
    if (sourceId.isEmpty() || seriesId.isEmpty()) return;
    ComicsLibraryRecord rec;
    rec.sourceId = sourceId;
    rec.seriesId = seriesId;
    rec.title    = displayTitle;
    rec.origin   = QStringLiteral("manga_download_index");
    // rootFolder + seriesFolderName are left empty; openSeriesByRecord
    // only uses sourceId + seriesId + title to reconstruct a MangaResult.
    openSeriesByRecord(rec);
}

void ComicsPage::refreshLibraryStrips()
{
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- rebuild DOWNLOADED +
    // BOOKMARKED sections from MangaDownloadIndex + AniListCache.
    //
    // COMICS_DOWNLOAD_DISPLAY_PROJECTION 2026-05-26 (Agent 9) — canonical
    // grouping: entriesForAllSeries() returns one representative per
    // (sourceId, seriesId) bucket. We now group those buckets by
    // resolveCanonicalGroupKey() so One Piece downloads from Premium +
    // MangaFire produce one tile instead of two.
    if (!m_tileStrip || !m_bookmarkedStrip) return;

    m_tileStrip->clear();
    m_bookmarkedStrip->clear();
    m_listView->clear();

    // ── DOWNLOADED (canonical-grouped) ──
    QSet<int> downloadedAnilistIds;
    QSet<QString> downloadedTitleKeysNorm;
    bool hasAnyDownloadedEntry = false;

    if (m_mangaDownloadIndex) {
        const auto entries = m_mangaDownloadIndex->entriesForAllSeries();
        hasAnyDownloadedEntry = !entries.isEmpty();

        // Phase 1: Group entries by canonical group key.
        struct GroupedTile {
            QString displayTitle;
            QString coverUrl;
            QString coverPath;
            int     anilistId = 0;
            // Preserve the first representative entry for click routing.
            MangaDownloadIndex::Entry representative;
            bool hasRep = false;
        };
        QMap<QString, GroupedTile> groups; // groupKey → aggregated tile data

        for (const auto& e : entries) {
            const QString groupKey = resolveCanonicalGroupKey(e.sourceId, e.seriesId);
            GroupedTile& gt = groups[groupKey];

            // Resolve anilistId + title using the shared helper (covers
            // AniList cache, MangaFire catalog, Premium catalog, tyLibrary).
            const int rawAnilistId = anilistIdForDownloadEntry(e.sourceId, e.seriesId);
            const QString resolvedTitle = resolveDisplayTitle(e.sourceId, e.seriesId);

            // Adopt the best identity: prefer a group member with anilistId > 0.
            if (rawAnilistId > 0 && gt.anilistId == 0) {
                gt.anilistId = rawAnilistId;
                gt.displayTitle = resolvedTitle.isEmpty()
                    ? e.seriesId : resolvedTitle;
            }
            // Adopt the first non-empty display title.
            if (gt.displayTitle.isEmpty()) {
                if (!resolvedTitle.isEmpty()) {
                    gt.displayTitle = resolvedTitle;
                } else {
                    const QString humanized = humanizeSlug(e.seriesId);
                    gt.displayTitle = humanized.isEmpty() ? e.seriesId : humanized;
                }
            }
            // Preserve the first representative entry for click routing.
            if (!gt.hasRep) {
                gt.representative = e;
                gt.hasRep = true;
            }

            // COMICS_CANONICAL_COVER 2026-05-26 (Agent 9).
            // Resolve cover: prefer MangaFire catalog Volume 1 cover →
            // AniList cache cover (if anilistId > 0) → file-thumb cover →
            // Tankoyomi library record cover.
            if (gt.coverUrl.isEmpty() && !gt.displayTitle.isEmpty()) {
                gt.coverUrl = resolveCanonicalSeriesCover(gt.anilistId,
                                                           gt.displayTitle);
            }
            if (rawAnilistId > 0 && m_anilistCache && gt.coverUrl.isEmpty()) {
                if (auto detailOpt = m_anilistCache->get(rawAnilistId)) {
                    gt.coverUrl = detailOpt->preview.coverThumbUrl;
                }
            }
            // COMICS_VOL1_THUMBNAIL_PRECEDENCE 2026-05-27 (Agent 1). Hemanth
            // rule: library tile thumbnail is ALWAYS the series Volume 1 cover.
            // The cbz file-thumb is extracted from whatever volume happens to
            // be downloaded (e.g. One Piece Vol 99), whose first page may be an
            // interior/chapter-title page, not a cover. So the cbz file-thumb +
            // tyLibrary cover are now gated on gt.coverUrl.isEmpty() — they only
            // fill in when no canonical (MangaFire Vol 1) cover URL resolved.
            // TileCard renders coverPath over coverUrl when both are set, so
            // leaving coverPath unset lets fetchPosterForTile paint the Vol 1
            // URL. Fallback for series WITHOUT a catalog entry is preserved.
            if (gt.coverUrl.isEmpty() && gt.coverPath.isEmpty()
                && m_bridge && !e.canonicalPath.isEmpty()) {
                const QFileInfo fi(e.canonicalPath);
                if (fi.exists()) {
                    const QString fileKey = QDir::cleanPath(fi.absoluteFilePath())
                                          + QStringLiteral("::")
                                          + QString::number(fi.size())
                                          + QStringLiteral("::")
                                          + QString::number(fi.lastModified().toMSecsSinceEpoch());
                    const QString fileHash = QString(QCryptographicHash::hash(
                        fileKey.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                    const QString fc = m_bridge->dataDir()
                                     + QStringLiteral("/thumbs/")
                                     + fileHash
                                     + QStringLiteral(".jpg");
                    if (QFile::exists(fc))
                        gt.coverPath = fc;
                }
            }
            if (gt.coverUrl.isEmpty() && gt.coverPath.isEmpty() && m_tyLibrary) {
                const auto rec = m_tyLibrary->get(e.sourceId, e.seriesId);
                if (!rec.coverPath.isEmpty() && QFile::exists(rec.coverPath))
                    gt.coverPath = rec.coverPath;
            }
        }

        // Phase 2: For groups with anilistId==0, attempt bookmark-by-title
        // adoption (MangaFire-catalog case).
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            GroupedTile& gt = it.value();
            if (gt.anilistId == 0 && m_anilistCache && !gt.displayTitle.isEmpty()) {
                const auto previews = m_anilistCache->bookmarkedPreviews();
                const QString titleKey = gt.displayTitle.toLower().trimmed();
                for (const auto& p : previews) {
                    if (p.title.toLower().trimmed() == titleKey) {
                        gt.anilistId = p.anilistId;
                        if (gt.coverUrl.isEmpty()) gt.coverUrl = p.coverThumbUrl;
                        break;
                    }
                }
            }
        }

        // Phase 3: Render one tile per canonical group.
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            GroupedTile& gt = it.value();
            const int   anilistId    = gt.anilistId;
            const QString displayTitle = gt.displayTitle;
            const QString coverUrl    = gt.coverUrl;
            const QString coverPath   = gt.coverPath;
            const MangaDownloadIndex::Entry rep = gt.representative;

            // Track for bookmark dedup.
            if (anilistId > 0)
                downloadedAnilistIds.insert(anilistId);
            if (!displayTitle.isEmpty())
                downloadedTitleKeysNorm.insert(displayTitle.toLower().trimmed());

            auto* card = new TileCard(coverPath, displayTitle, QStringLiteral("Downloaded"));
            card->setProperty("anilistId", anilistId);
            card->setProperty("seriesKey",
                              rep.sourceId + QStringLiteral(":") + rep.seriesId);
            card->setProperty("seriesName", displayTitle);
            card->setProperty("coverPath", coverPath);
            // COMICS_DOWNLOAD_DISPLAY_PROJECTION 2026-05-26 (Agent 9) —
            // also stash the canonical group key so the click handler can
            // use it for routing.
            card->setProperty("canonicalGroupKey", it.key());

            // Click handler: prefer AniList route when anilistId > 0; fall
            // back to Tankoyomi library record; otherwise no-op.
            const int   linkedAnilistId = anilistId;
            const QString linkedTitle   = displayTitle;
            connect(card, &TileCard::clicked, this,
                    [this, rep, linkedAnilistId, linkedTitle]() {
                if (m_tyLibrary) {
                    const auto rec = m_tyLibrary->get(rep.sourceId, rep.seriesId);
                    if (!rec.seriesId.isEmpty()) {
                        openSeriesByRecord(rec);
                        return;
                    }
                }
                if (linkedAnilistId > 0) {
                    openSeriesByAnilistId(linkedAnilistId, linkedTitle);
                }
            });
            m_tileStrip->addTile(card);

            // COMICS_CANONICAL_COVER 2026-05-26 (Agent 9) — also trigger
            // the poster fetch for MangaFire Volume 1 covers (anilistId=0
            // but coverUrl is a remote CDN URL).
            if (coverPath.isEmpty() && (!coverUrl.isEmpty() || anilistId > 0))
                fetchPosterForTile(card, anilistId, coverUrl);
        }
    }

    const bool hasDownloaded = !downloadedAnilistIds.isEmpty() || hasAnyDownloadedEntry;

    // ── BOOKMARKED ──
    int bookmarkedCount = 0;
    if (m_anilistCache) {
        const auto previews = m_anilistCache->bookmarkedPreviews();
        for (const auto& p : previews) {
            // Suppress duplicate tile when a bookmarked series is also
            // downloaded (it already appears in the DOWNLOADED section).
            if (downloadedAnilistIds.contains(p.anilistId)) continue;
            // COMICS_WC_LIBRARY_DEDUP 2026-05-24 (Agent 1). Belt-and-
            // suspenders dedup against MangaFire-catalog downloads whose
            // anilistId resolution failed (catalog has anilistId=0 + no
            // bookmark-by-title match was found upstream). Compare by
            // normalized title; if any download has the same title, this
            // bookmark is the duplicate and should be skipped.
            if (downloadedTitleKeysNorm.contains(p.title.toLower().trimmed())) continue;

            auto* card = new TileCard(QString(), p.title, QString());
            card->setProperty("anilistId", p.anilistId);
            card->setProperty("seriesName", p.title);
            const int anilistId = p.anilistId;
            const QString title = p.title;
            connect(card, &TileCard::clicked, this,
                    [this, anilistId, title]() {
                openSeriesByAnilistId(anilistId, title);
            });
            // 2026-05-17 LIBRARY merge -- bookmarked tiles land in the same
            // m_tileStrip as downloaded tiles. m_bookmarkedStrip is kept in
            // the layout (always hidden) for backward-compat; cleanup of the
            // dead widget is a future polish item.
            m_tileStrip->addTile(card);
            fetchPosterForTile(card, p.anilistId, p.coverThumbUrl);
            ++bookmarkedCount;
        }
    }

    // ── Section visibility (2026-05-17 LIBRARY merge) ──
    // Theatre mirror: one combined LIBRARY strip; show whenever EITHER
    // downloaded OR bookmarked has at least one tile. m_bookmarkedSection
    // is always hidden post-merge.
    const bool hasAnyLibraryTile = hasDownloaded || bookmarkedCount > 0;
    if (hasAnyLibraryTile) {
        m_tileStrip->show();
        if (m_downloadedLabel) m_downloadedLabel->show();
        m_tileStrip->sortTiles(m_sortCombo->currentData().toString());
    } else {
        m_tileStrip->hide();
        if (m_downloadedLabel) m_downloadedLabel->hide();
    }
    if (m_bookmarkedSection) m_bookmarkedSection->hide();

    // Empty-state: only show the global empty label when BOTH sections are
    // empty AND no Continue-Reading items light up.
    const bool wholeLibraryEmpty = !hasDownloaded && bookmarkedCount == 0;
    if (wholeLibraryEmpty) {
        m_statusLabel->setObjectName("LibraryEmptyLabel");
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setText("Add titles from Search");
        m_statusLabel->show();
    } else {
        m_statusLabel->hide();
    }
}

void ComicsPage::onTileClicked(const QString& seriesPath, const QString& seriesName)
{
    openSeriesByPath(seriesPath, seriesName);
}

void ComicsPage::openSeriesByPath(const QString& seriesPath, const QString& seriesName,
                                  const QString& coverPath)
{
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 27 -- provenance route.
    // TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16): the Tankoyomi branch
    // formerly opened ComicsTankoyomiDetailView with a cached MangaResult
    // preview. ComicsSeriesView's showSeries() expects an anilist::MediaPreview
    // (volume-pivot data shape, AniList-keyed). The legacy ComicsLibraryRecord
    // does NOT carry an anilistId, so library-tile-click into the Tankoyomi
    // path is currently routed back to the folder SeriesView (treats the
    // tile as a generic folder). The full Phase 10 work re-roots library
    // tiles around anilistId, at which point this branch lights back up.
    // Folder-origin tiles fall through to the existing SeriesView path.
    Q_UNUSED(seriesName);
    // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — emit BEFORE the
    // in-page stack flip so MainWindow's NavHistory captures the OLD library
    // state into the current entry, then pushes a fresh entry for the folder
    // series layer. captureNavState reads m_mode (which stays Library on this
    // legacy path); pressing the topbar Back chevron correctly restores the
    // library-grid state.
    if (!m_inNavRestore) {
        QJsonObject blob;
        blob[QStringLiteral("seriesPath")] = seriesPath;
        blob[QStringLiteral("seriesName")] = seriesName;
        blob[QStringLiteral("coverPath")]  = coverPath;
        emit enteredLayer(makeComicsLayer(QStringLiteral("folderSeriesView"), seriesName, blob));
    }
    m_seriesView->showSeries(seriesPath, seriesName, coverPath);
    m_stack->setCurrentIndexAnimated(1);
}

void ComicsPage::updateWesternMangaStatus(const QString& recordId)
{
    // Route a MangaDownloader progress tick for the in-flight Western (RCO)
    // download to the Sources panel + the clicked volume tile. No-op for any
    // other record (manga/Tankoyomi downloads).
    if (recordId.isEmpty() || recordId != m_westernDownloadRecordId) return;
    if (!m_mangaDownloader || !m_tyVolumeSeriesView) return;

    for (const MangaDownloadRecord& rec : m_mangaDownloader->listActive()) {
        if (rec.id != recordId) continue;
        const ChapterDownload* ch = rec.chapters.isEmpty() ? nullptr : &rec.chapters.first();
        const bool errored = rec.status == QLatin1String("error")
                          || (ch && ch->status == QLatin1String("error"));
        if (errored) {
            m_tyVolumeSeriesView->updateWesternDownloadStatus(QString(), tr("No download found"));
            if (m_pendingWesternDownloadVolume > 0)
                m_tyVolumeSeriesView->setVolumeStatusText(m_pendingWesternDownloadVolume, tr("Failed"));
            return;
        }
        int pct = 0;
        if (ch && ch->totalImages > 0)
            pct = (ch->downloadedImages * 100) / ch->totalImages;
        const QString panelLine = pct <= 0 ? tr("Finding...") : tr("Downloading %1%").arg(pct);
        m_tyVolumeSeriesView->updateWesternDownloadStatus(m_westernDownloadEdition, panelLine);
        if (m_pendingWesternDownloadVolume > 0)
            m_tyVolumeSeriesView->setVolumeStatusText(m_pendingWesternDownloadVolume,
                pct <= 0 ? QStringLiteral("...") : QStringLiteral("%1%").arg(pct));
        return;
    }
}

void ComicsPage::onChapterCompleted(const QString& source, const QString& seriesTitle,
                                    const QString& chapterId, const QString& finalPath,
                                    qint64 fileSize)
{
    if (!m_tyLibrary || !m_mangaDownloadIndex) return;
    if (source.isEmpty() || seriesTitle.isEmpty() || chapterId.isEmpty() || finalPath.isEmpty())
        return;

    const QString completedPath = QDir::cleanPath(QFileInfo(finalPath).absoluteFilePath());
    QList<ComicsLibraryRecord> matches;
    for (const auto& rec : m_tyLibrary->all()) {
        if (rec.sourceId == source && rec.title == seriesTitle)
            matches.append(rec);
    }

    if (matches.size() > 1) {
        QList<ComicsLibraryRecord> pathMatches;
        for (const auto& rec : matches) {
            if (rec.canonicalSeriesPath.isEmpty()) continue;
            const QString root = QDir::cleanPath(QFileInfo(rec.canonicalSeriesPath).absoluteFilePath());
            if (completedPath == root || completedPath.startsWith(root + QLatin1Char('/')))
                pathMatches.append(rec);
        }
        matches = pathMatches;
    }

    if (matches.size() != 1) {
        qWarning() << "[ComicsPage] skipped MangaDownloadIndex registration"
                   << "source" << source << "title" << seriesTitle
                   << "chapter" << chapterId << "matches" << matches.size();
        return;
    }

    const auto& rec = matches.first();
    m_mangaDownloadIndex->registerChapter(rec.sourceId, rec.seriesId,
                                          chapterId, completedPath, fileSize);
    refreshTileChips();
}

void ComicsPage::showGrid()
{
    m_stack->setCurrentIndexAnimated(0);
}

// ── COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 18 ─────────────────────
// Mode-transition slots for the search-takeover state machine. Library
// mode is the default grid + Continue strip (m_stack index 0).
// SearchResults flips to the takeover widget; onSearchResultActivated is
// a Phase-3 qDebug stub awaiting Phase-4 detail wiring.

void ComicsPage::resetToRoot()
{
    // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — public forwarder
    // invoked by MainWindow::resetActivePageToRoot when the user clicks
    // the Comics topbar pill while already on this page. showLibraryMode
    // is internally guarded against same-mode re-entry, so calling this
    // while already on the library grid is a structural no-op.
    showLibraryMode();
}

void ComicsPage::showLibraryMode()
{
    // Landing on the manga library clears any stale Western-detail flag (e.g.
    // user opened a Western series then tapped the Manga pill instead of Back),
    // so a subsequent manga-detail Back doesn't wrongly route to the Western grid.
    m_detailEnteredFromWestern = false;
    // NAV_BACK_ROOT_SEED 2026-05-21 (Agent 5) -- the previous PHASE 0 push of
    // a fresh "library" layer on every transition into Library mode has been
    // removed. MainWindow now seeds Comics with a persistent library root
    // via PerModeNavController::setRootLayer at startup, and page-internal
    // back paths (onDetailBack -> showLibraryMode, search-takeover dismiss)
    // pop the deep entry through exitedLayer so the controller stack returns
    // to [library_root] naturally. Re-emitting library here would stack
    // [library_root, library] and leave the topbar Back chevron enabled at
    // the library landing surface, which violates the "Back disabled at
    // mode root" contract.
    m_mode = Mode::Library;
    m_stack->setCurrentIndexAnimated(0);
    if (m_searchTakeover) m_searchTakeover->clearResults();
}

// ─── Search history (Stream-bar parity 2026-05-22) ───────────────────────
//
// QSettings key is `comics/searchHistory` — deliberately disjoint from
// Stream's `stream/searchHistory` so the two modes don't cross-pollinate
// past queries (Agent 1 guardrail at handoff).

// Per-shelf history routing (2026-06-02). The active list/key follow which bar
// has focus: the Western bar uses a separate QSettings key so its history never
// mixes with the manga shelf's (Hemanth smoke: the two were sharing one store).
QStringList& ComicsPage::activeSearchHistory()
{
    return (m_activeSearchBar && m_activeSearchBar == m_westernSearchBar)
               ? m_westernSearchHistory
               : m_searchHistory;
}

QString ComicsPage::activeSearchHistoryKey() const
{
    return (m_activeSearchBar && m_activeSearchBar == m_westernSearchBar)
               ? QStringLiteral("comics/westernSearchHistory")
               : QStringLiteral("comics/searchHistory");
}

void ComicsPage::loadSearchHistory()
{
    QSettings s;
    m_searchHistory = s.value(QStringLiteral("comics/searchHistory")).toStringList();
    if (m_searchHistory.size() > kMaxSearchHistory)
        m_searchHistory = m_searchHistory.mid(0, kMaxSearchHistory);
    m_westernSearchHistory =
        s.value(QStringLiteral("comics/westernSearchHistory")).toStringList();
    if (m_westernSearchHistory.size() > kMaxSearchHistory)
        m_westernSearchHistory = m_westernSearchHistory.mid(0, kMaxSearchHistory);
}

void ComicsPage::saveSearchHistory()
{
    QSettings s;
    s.setValue(activeSearchHistoryKey(), activeSearchHistory());
}

void ComicsPage::pushSearchHistory(const QString& query)
{
    const QString q = query.trimmed();
    if (q.isEmpty()) return;
    QStringList& hist = activeSearchHistory();
    hist.removeAll(q);
    hist.prepend(q);
    if (hist.size() > kMaxSearchHistory)
        hist = hist.mid(0, kMaxSearchHistory);
    saveSearchHistory();
}

void ComicsPage::removeSearchHistoryEntry(const QString& query)
{
    activeSearchHistory().removeAll(query);
    saveSearchHistory();
    // Rebuild the open dropdown so the row disappears immediately.
    if (m_searchHistoryDropdown && m_searchHistoryDropdown->isVisible()) {
        showSearchHistoryDropdown();
    }
}

void ComicsPage::clearSearchHistory()
{
    QStringList& hist = activeSearchHistory();
    if (hist.isEmpty()) return;
    hist.clear();
    saveSearchHistory();
    hideSearchHistoryDropdown();
}

void ComicsPage::setSearchBusy(bool busy)
{
    // Shared-recipe: toggle the busy widget for whichever bar is active.
    QWidget* busyWidget = m_activeSearchBusy ? m_activeSearchBusy : m_searchBusy;
    if (!busyWidget) return;
    busyWidget->setVisible(busy);
}

void ComicsPage::buildSearchHistoryDropdown()
{
    m_searchHistoryDropdown = new QFrame(this);
    m_searchHistoryDropdown->setObjectName("ComicsSearchHistory");
    m_searchHistoryDropdown->setStyleSheet(
        "QFrame#ComicsSearchHistory { background: #1a1a1a; border: 1px solid #3a3a3a;"
        "  border-radius: 6px; }");
    m_searchHistoryDropdown->hide();

    auto* outer = new QVBoxLayout(m_searchHistoryDropdown);
    outer->setContentsMargins(0, 4, 0, 4);
    outer->setSpacing(0);

    m_searchHistoryList = new QWidget(m_searchHistoryDropdown);
    auto* listLayout = new QVBoxLayout(m_searchHistoryList);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);

    outer->addWidget(m_searchHistoryList);

    // Delayed-hide timer so a click on a dropdown row gets its release
    // event before the dropdown is dismissed (FocusOut → 150ms → hide).
    m_searchHistoryHideTimer = new QTimer(this);
    m_searchHistoryHideTimer->setSingleShot(true);
    m_searchHistoryHideTimer->setInterval(150);
    connect(m_searchHistoryHideTimer, &QTimer::timeout, this, [this]() {
        if (m_searchHistoryDropdown) m_searchHistoryDropdown->hide();
    });
}

void ComicsPage::positionSearchHistoryDropdown()
{
    if (!m_searchHistoryDropdown) return;
    // Shared-recipe: anchor to the bar that currently has focus (or manga bar).
    QLineEdit* bar = m_activeSearchBar ? m_activeSearchBar : m_searchBar;
    if (!bar) return;
    const QPoint topLeft =
        bar->mapTo(this, QPoint(0, bar->height() + 2));
    m_searchHistoryDropdown->setGeometry(
        topLeft.x(), topLeft.y(), bar->width(),
        m_searchHistoryDropdown->sizeHint().height());
}

void ComicsPage::showSearchHistoryDropdown()
{
    if (!m_searchHistoryDropdown || !m_searchHistoryList) return;
    if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->stop();

    // Clear old rows.
    auto* layout = qobject_cast<QVBoxLayout*>(m_searchHistoryList->layout());
    if (!layout) return;
    while (auto* item = layout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    // Per-shelf: show the history for whichever bar has focus.
    const QStringList& hist = activeSearchHistory();
    if (hist.isEmpty()) {
        m_searchHistoryDropdown->hide();
        return;
    }

    const int rows = qMin(hist.size(), kMaxSearchHistory);
    const char* kRowBtnStyle =
        "QPushButton { background: transparent; color: #d0d0d0; border: none;"
        "  text-align: left; padding: 6px 10px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.08); }";
    const char* kRemoveBtnStyle =
        "QPushButton { background: transparent; color: rgba(255,255,255,0.45);"
        "  border: none; font-size: 14px; padding: 0 10px; }"
        "QPushButton:hover { color: #fff; }";
    const char* kClearAllBtnStyle =
        "QPushButton { background: transparent; color: rgba(255,255,255,0.55);"
        "  border: none; text-align: left; padding: 6px 10px;"
        "  font-size: 11px; font-weight: 500; letter-spacing: 0.4px; }"
        "QPushButton:hover { color: #fff; }";

    for (int i = 0; i < rows; ++i) {
        const QString q = hist.at(i);

        auto* row = new QWidget(m_searchHistoryList);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);

        auto* queryBtn = new QPushButton(q, row);
        queryBtn->setCursor(Qt::PointingHandCursor);
        queryBtn->setStyleSheet(kRowBtnStyle);
        queryBtn->setFocusPolicy(Qt::NoFocus);
        connect(queryBtn, &QPushButton::clicked, this, [this, q]() {
            // Shared-recipe: set text on whichever bar has focus.
            QLineEdit* bar = m_activeSearchBar ? m_activeSearchBar : m_searchBar;
            if (bar) bar->setText(q);
            // Re-enter the same submit path used by Enter / search button.
            showSearchMode(q);
            hideSearchHistoryDropdown();
        });
        rowLayout->addWidget(queryBtn, 1);

        auto* removeBtn = new QPushButton(QStringLiteral("×"), row);
        removeBtn->setCursor(Qt::PointingHandCursor);
        removeBtn->setStyleSheet(kRemoveBtnStyle);
        removeBtn->setFocusPolicy(Qt::NoFocus);
        removeBtn->setToolTip(tr("Remove from history"));
        connect(removeBtn, &QPushButton::clicked, this, [this, q]() {
            removeSearchHistoryEntry(q);
        });
        rowLayout->addWidget(removeBtn);

        layout->addWidget(row);
    }

    // Footer: "× Clear search history" wipes the entire list. Single visible
    // affordance — per-entry × on each row above is preserved.
    auto* divider = new QFrame(m_searchHistoryList);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(
        "QFrame { border: none; background: rgba(255,255,255,0.08);"
        "  max-height: 1px; min-height: 1px; }");
    layout->addWidget(divider);

    auto* clearAllBtn = new QPushButton(
        QStringLiteral("×  Clear search history"), m_searchHistoryList);
    clearAllBtn->setCursor(Qt::PointingHandCursor);
    clearAllBtn->setStyleSheet(kClearAllBtnStyle);
    clearAllBtn->setFocusPolicy(Qt::NoFocus);
    connect(clearAllBtn, &QPushButton::clicked,
            this, &ComicsPage::clearSearchHistory);
    layout->addWidget(clearAllBtn);

    m_searchHistoryDropdown->adjustSize();
    positionSearchHistoryDropdown();
    m_searchHistoryDropdown->show();
    m_searchHistoryDropdown->raise();
}

void ComicsPage::hideSearchHistoryDropdown()
{
    if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->stop();
    if (m_searchHistoryDropdown) m_searchHistoryDropdown->hide();
}

void ComicsPage::showSearchMode(const QString& query)
{
    // Stream-bar parity 2026-05-22: this is the canonical submit funnel —
    // Enter, search-icon click, AND history-row click all route through
    // here, so push-to-history runs exactly once per submitted query.
    // Dedup against the prior top entry happens inside pushSearchHistory.
    pushSearchHistory(query);
    hideSearchHistoryDropdown();
    setSearchBusy(true);

    // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — emit before the
    // in-page transition so the SearchResults layer is recorded in
    // NavHistory. Guarded against same-mode re-entry (typing a new query
    // while already in search results is a refinement, not a navigation)
    // and the restoreNavState replay path.
    if (!m_inNavRestore && m_mode != Mode::SearchResults) {
        QJsonObject blob;
        blob[QStringLiteral("query")] = query;
        emit enteredLayer(makeComicsLayer(QStringLiteral("searchResults"), QStringLiteral("Search Results"), blob));
    }
    m_mode = Mode::SearchResults;
    m_searchTakeover->search(query);
    m_stack->setCurrentWidget(m_searchTakeover);
}

void ComicsPage::onSearchResultActivated(const MangaResult& result)
{
    comicsOpenTrace(QStringLiteral("CP::onSearchResultActivated ENTRY source=%1 id=%2 title=\"%3\"")
                        .arg(result.source).arg(result.id).arg(result.title));
    // WEEBCENTRAL_IDENTITY_PIVOT Tasks 9+10 (2026-05-19) -- search-result
    // click now carries a MangaResult (WeebCentral) instead of MediaPreview
    // (AniList). Routes into ComicsSeriesView::showSeries(MangaResult).
    // Origin is recorded so Back returns to the search-takeover.
    //
    // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — emit BEFORE the
    // in-page state change so NavHistory captures the SearchResults state
    // (mode=searchResults + query) into the current entry and pushes a
    // fresh tankoyomiDetail entry for the target.
    // COMICS_WESTERN_ADD 2026-06-02 (Agent 1). Do NOT push this generic
    // enteredFrom="search" layer for an RCO result: the async westernSeriesReady
    // slot -> openWesternSeriesFromCatalog pushes its own enteredFrom="western"
    // layer when the fetch lands, so pushing here too left TWO seriesView entries
    // on the nav stack — the topbar Back chevron then restored the stale
    // anilistId=0 "search" layer (silent no-render, needing two Back presses).
    // The manga branch below still pushes its layer as before.
    if (!m_inNavRestore && result.source != QLatin1String("readcomicsonline")) {
        QJsonObject blob;
        blob[QStringLiteral("seriesId")]    = result.id;
        blob[QStringLiteral("seriesTitle")] = result.title;
        blob[QStringLiteral("enteredFrom")] = QStringLiteral("search");
        emit enteredLayer(makeComicsLayer(QStringLiteral("seriesView"), result.title, blob));
    }
    m_enteredDetailFrom = Mode::SearchResults;
    m_mode = Mode::TankoyomiDetail;
    m_currentDetailAnilistId   = 0;   // MangaResult has no anilist integer id
    // Default: a search pick is NOT Western (the RCO branch below re-sets this
    // true via openWesternSeriesFromCatalog when the live fetch lands). Keeps the
    // in-view Back routing accurate (Codex review 2026-06-02).
    m_detailEnteredFromWestern = false;
    m_currentDetailSeriesTitle = result.title;

    // COMICS_WESTERN_ADD 2026-06-01 (Agent 2). Western (RCO) search pick: route
    // into the LIVE page-scrape (fetchWesternSeries) instead of the AniList/
    // mangafire manga-enrichment path below — a Western collected edition has no
    // AniList identity and that path would corrupt it. The westernSeriesReady
    // connect (ctor) renders the result via openWesternSeriesFromCatalog. We
    // override m_enteredDetailFrom expectations: showSearchResultLoading paints a
    // spinner while the page fetch is in flight; the ready/error slot replaces it.
    if (result.source == QLatin1String("readcomicsonline") && m_readComicsScraper
        && m_tyVolumeSeriesView) {
        m_stack->setCurrentWidget(m_tyVolumeSeriesView);
        m_tyVolumeSeriesView->showSearchResultLoading();
        setSearchBusy(true);
        m_readComicsScraper->fetchWesternSeries(result.id, result.title,
                                                result.thumbnailUrl);
        return;
    }

    if (m_tyVolumeSeriesView) {
        m_stack->setCurrentWidget(m_tyVolumeSeriesView);
        m_tyVolumeSeriesView->showSearchResultLoading();

        if (!m_anilistClient || !m_anilistCache || result.title.trimmed().isEmpty()) {
            renderSearchOpenFallback(result);
            return;
        }

        const int reqId = ++m_nextLibraryEnrichReqId;
        m_pendingLibraryEnrichReqId = reqId;
        m_pendingLibraryEnrichTitle = result.title.trimmed();
        m_pendingLibraryEnrichAddBookmark = false;
        m_pendingSearchOpenEnrichReqId = reqId;
        m_pendingSearchOpenFallback = result;
        qInfo("ComicsPage::onSearchResultActivated: pre-resolving AniList for \"%s\" reqId=%d",
              qUtf8Printable(m_pendingLibraryEnrichTitle), reqId);
        m_anilistClient->searchByTitle(m_pendingLibraryEnrichTitle, reqId);
    }
}

void ComicsPage::renderSearchOpenFallback(const MangaResult& result)
{
    if (!m_tyVolumeSeriesView) return;
    m_currentDetailAnilistId = 0;
    m_currentDetailSeriesTitle = result.title;
    m_tyVolumeSeriesView->showSeries(result, false);
    dispatchCatalogResolve(fandomSeriesSlugFromTitle(result.title),
                           /*titleHint*/result.title);
}

void ComicsPage::onDetailBack()
{
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 9 Task 52 -- Back
    // from detail routes by the origin recorded at entry. Search->Detail->
    // Back lands on the search-takeover; Library->Detail->Back lands on
    // the merged library grid.
    //
    // TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- this slot is the
    // central exit point for "back from detail" and is invoked by:
    //   * the Escape shortcut (ComicsPage.cpp:533)
    //   * STREAM_PORT Bug-1 fix 2026-05-18: ComicsSeriesView::backRequested
    //     signal (the in-view "<- Back" button shipped by Task 1)
    //   * future deep-link recovery paths
    // The topbar Back chevron uses the separate global-nav-controller path
    // (enteredLayer/restoreLayer plumbing) which lands at restoreLayer()
    // below, not here.
    //
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- emit exitedLayer so the
    // controller pops the detail entry from its stack, keeping the
    // back-stack consistent with the in-page state machine. Emitted
    // before the mode flip so the controller state matches what is about
    // to become visible.
    if (!m_inNavRestore) emit exitedLayer();
    if (m_detailEnteredFromWestern) {
        // Western-catalogue detail -> back to the Western grid (not manga lib).
        showWesternMode();
    } else if (m_enteredDetailFrom == Mode::SearchResults && m_searchTakeover) {
        m_mode = Mode::SearchResults;
        m_stack->setCurrentWidget(m_searchTakeover);
    } else {
        showLibraryMode();
    }
    m_currentDetailAnilistId   = 0;
    m_currentDetailSeriesTitle.clear();
    if (m_tyVolumeSeriesView) m_tyVolumeSeriesView->clearView();
    m_enteredDetailFrom = Mode::Library;
    m_detailEnteredFromWestern = false;
}

void ComicsPage::onVolumeMetadataResolved(int anilistId, int volumeCount, int chapterCount)
{
    if (!m_anilistCache || !m_tyVolumeSeriesView) return;
    if (m_tyVolumeSeriesView->currentAnilistId() != anilistId) return;

    const auto detail = m_anilistCache->get(anilistId);
    if (!detail.has_value()) return;

    const auto rows = tankoban::manga::anilist::AniListVolumeMapper::map(
        *detail, volumeCount, chapterCount);
    m_tyVolumeSeriesView->setVolumeRows(rows);
}

void ComicsPage::onVolumeMetadataUnresolved(int anilistId, const QString& reason)
{
    qDebug().noquote() << QStringLiteral("[mangaupdates] anilist %1 unresolved: %2")
                              .arg(anilistId)
                              .arg(reason);
}

void ComicsPage::onDownloadDispatchRequested(
    const tankoban::manga::comics::UnifiedSourceRow& row,
    const QString& seriesTitle,
    int            anilistSeriesId,
    int            volumeNumber,
    const QStringList& chapterIds)
{
    // TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- route the Sources panel
    // selection to the appropriate provider based on row.kind:
    //   - Catalog        -> TorrentVolumeProvider via PremiumCatalog lookup
    //   - NyaaRuntime    -> TorrentVolumeProvider via synthesized magnet/infoHash
    //   - WeebCentralPacker -> WeebCentralVolumePacker with the chapter list
    //
    // seriesId resolution: catalog entries carry a true seriesId; runtime
    // sources fall back to an "anilist_<id>" canonical slug because there is
    // no other stable cross-provider identifier in Phase 9 (PHASE 13 TODO:
    // promote anilistId to a first-class field on PremiumCatalogEntry +
    // VolumePackRequest so this fallback can be replaced with a proper map).
    using namespace tankoban::manga;
    using Kind = comics::UnifiedSourceRow::Kind;

    const QString fallbackSeriesId =
        QStringLiteral("anilist_%1").arg(anilistSeriesId);

    if (row.kind == Kind::Catalog) {
        if (!m_premiumCatalog || !m_premiumProvider) {
            qDebug().noquote()
                << "[Phase9 dispatch] Catalog row but provider not wired"
                << seriesTitle << QStringLiteral("v%1").arg(volumeNumber);
            return;
        }
        // Catalog hit: locate PremiumCatalogEntry by anilistId (catalog v1
        // stores anilistId), then the matching PremiumVolumeEntry. The
        // ComicsSourcesPanel already verified a catalog hit existed when it
        // emitted; defensively re-check here in case state shifted.
        std::optional<premium::PremiumCatalogEntry> catalogEntry;
        for (const auto& e : m_premiumCatalog->allEntries()) {
            if (e.anilistId == anilistSeriesId) { catalogEntry = e; break; }
        }
        if (!catalogEntry) {
            qDebug().noquote()
                << "[Phase9 dispatch] Catalog row no longer matches catalog"
                << anilistSeriesId << seriesTitle;
            return;
        }
        const premium::PremiumVolumeEntry* volEntry = nullptr;
        for (const auto& vv : catalogEntry->volumes) {
            if (vv.vol == volumeNumber) { volEntry = &vv; break; }
        }
        if (!volEntry) {
            qDebug().noquote()
                << "[Phase9 dispatch] Catalog has no volume entry"
                << QStringLiteral("v%1").arg(volumeNumber) << seriesTitle;
            return;
        }
        const QString destinationPath = canonicalSeriesPathForPremium(*catalogEntry);
        if (destinationPath.isEmpty()) {
            qDebug().noquote()
                << "[Phase9 dispatch] Catalog requestVolume aborted -- no comics root configured";
            return;
        }
        rememberPendingVolumeDispatch(catalogEntry->seriesId, volumeNumber,
                                      PendingVolumeSourceKind::Catalog,
                                      anilistSeriesId, chapterIds);
        m_premiumProvider->requestVolume(*catalogEntry, *volEntry, destinationPath);
        Q_UNUSED(chapterIds);
        return;
    }

    if (row.kind == Kind::NyaaRuntime) {
        // Nyaa runtime: synthesize a one-volume PremiumCatalogEntry from the
        // unified-row data so we can reuse TorrentVolumeProvider's existing
        // requestVolume path. cbzFileName follows the same "<Series> v<NN>.cbz"
        // shape the validator already accepts; expectedInfoHash comes from
        // the row directly.
        if (!m_premiumProvider) {
            qDebug().noquote()
                << "[Phase9 dispatch] Nyaa row but Premium provider not wired";
            return;
        }
        if (row.magnetUri.isEmpty() || row.infoHash.isEmpty()) {
            qDebug().noquote()
                << "[Phase9 dispatch] Nyaa row missing magnet/infoHash";
            return;
        }
        // PremiumCatalogEntry carries the magnet + infoHash at the SERIES
        // level (one torrent per catalog series, multiple volumes addressed
        // by fileIndex). For a nyaa-runtime hit we synthesize a single-volume
        // entry where the magnet/infoHash come from the row and fileIndex
        // remains -1 (TorrentVolumeProvider treats negative fileIndex as
        // "pick the only cbz from metadata", per its existing single-file
        // torrent path -- PHASE 13 TODO: confirm this fallback behavior is
        // present; if not, multi-volume nyaa packs need fileIndex resolution
        // from metadata-arrival).
        premium::PremiumCatalogEntry synth;
        synth.seriesId         = fallbackSeriesId;
        synth.title            = seriesTitle;
        synth.anilistId        = anilistSeriesId;
        synth.magnetUri        = row.magnetUri;
        synth.expectedInfoHash = row.infoHash;
        premium::PremiumVolumeEntry vol;
        vol.vol               = volumeNumber;
        vol.fileIndex         = -1;
        vol.cbzFileName       = QStringLiteral("%1 v%2.cbz")
                                    .arg(seriesTitle)
                                    .arg(volumeNumber, 2, 10, QChar('0'));
        synth.volumes.append(vol);

        // Destination: <comics-root>/<sanitised-title>/, mirroring
        // canonicalSeriesPathForPremium's shape. We bypass the catalog-id
        // lookup since this is a synthesized entry; the helper expects a
        // catalog entry so we inline the same sanitisation here.
        const QStringList roots = m_bridge->rootFolders("comics");
        if (roots.isEmpty()) {
            qDebug().noquote()
                << "[Phase9 dispatch] Nyaa requestVolume aborted -- no comics root";
            return;
        }
        QString sanitised = seriesTitle;
        sanitised.replace(QRegularExpression(R"([<>:\"/\\|?*])"), QStringLiteral("_"));
        const QString destinationPath = roots.first() + QLatin1Char('/') + sanitised;
        QDir().mkpath(destinationPath);
        rememberPendingVolumeDispatch(synth.seriesId, volumeNumber,
                                      PendingVolumeSourceKind::NyaaRuntime,
                                      anilistSeriesId, chapterIds);
        m_premiumProvider->requestVolume(synth, vol, destinationPath);
        return;
    }

    if (row.kind == Kind::WeebCentralPacker) {
        if (!m_weebCentralPacker) {
            qDebug().noquote()
                << "[Phase9 dispatch] WC row but packer not wired";
            return;
        }
        const QStringList wcChapterIds = row.weebCentralChapterIds.isEmpty()
            ? chapterIds
            : row.weebCentralChapterIds;
        if (wcChapterIds.isEmpty()) {
            qDebug().noquote()
                << "[Phase9 dispatch] WC row but no chapter ids supplied";
            return;
        }
        QString mangaFireSeriesId;
        if (m_currentWcResolveKey.volumeNumber == volumeNumber &&
            !m_currentWcResolveKey.seriesId.isEmpty()) {
            mangaFireSeriesId = m_currentWcResolveKey.seriesId;
        }
        if (mangaFireSeriesId.isEmpty() && anilistSeriesId > 0) {
            mangaFireSeriesId = m_localCatalogIndex.slugForAnilistId(anilistSeriesId);
        }
        if (mangaFireSeriesId.isEmpty() && !seriesTitle.isEmpty()) {
            mangaFireSeriesId = m_localCatalogIndex.slugForSeriesTitle(seriesTitle);
        }
        if (mangaFireSeriesId.isEmpty()) {
            qDebug().noquote()
                << "[Phase9 dispatch] WC requestVolume aborted -- no MangaFire seriesId";
            return;
        }
        const QStringList roots = m_bridge->rootFolders("comics");
        if (roots.isEmpty()) {
            qDebug().noquote()
                << "[Phase9 dispatch] WC requestVolume aborted -- no comics root";
            return;
        }
        QString sanitised = seriesTitle;
        sanitised.replace(QRegularExpression(R"([<>:\"/\\|?*])"), QStringLiteral("_"));
        const QString seriesDir = roots.first() + QLatin1Char('/') + sanitised;
        QDir().mkpath(seriesDir);
        const QString volumeFileStem =
            (volumeNumber == tankoban::manga::anilist::kVolumeXNumber)
                ? QStringLiteral("Volume X")
                : QStringLiteral("Volume %1").arg(volumeNumber, 2, 10, QChar('0'));
        const QString destinationPath =
            seriesDir + QLatin1Char('/') + volumeFileStem + QStringLiteral(".cbz");

        VolumePackRequest req;
        req.seriesId        = mangaFireSeriesId;
        req.volumeNumber    = volumeNumber;
        req.destinationPath = destinationPath;
        req.chapterIds      = wcChapterIds;
        // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
        // Magazine + Volume X volumes need chapter-boundary pairing (.volx
        // sidecar); clean volumes stitch without it.
        req.needsChapterPairing =
            m_tyVolumeSeriesView
                ? m_tyVolumeSeriesView->isVolumeMagazineSourced(volumeNumber)
                : false;
        rememberPendingVolumeDispatch(req.seriesId, volumeNumber,
                                      PendingVolumeSourceKind::WeebCentralPacker,
                                      anilistSeriesId, wcChapterIds);
        m_weebCentralPacker->requestVolume(req);
        return;
    }
}

void ComicsPage::toggleViewMode()
{
    m_gridMode = !m_gridMode;
    QSettings("Tankoban", "Tankoban").setValue("library_view_mode_comics",
                                                m_gridMode ? "grid" : "list");
    if (m_gridMode) {
        m_listView->hide();
        m_tileStrip->show();
        m_densitySlider->show();
        m_viewToggle->setText("\u2630"); // hamburger
    } else {
        m_tileStrip->hide();
        m_listView->show();
        m_densitySlider->hide();
        m_viewToggle->setText("\u2637"); // dotted square
    }
}

void ComicsPage::applySearch()
{
    QString query = m_searchBar->text();
    m_tileStrip->filterTiles(query);
    m_listView->setTextFilter(query);

    if (m_tileStrip->visibleCount() == 0 && !query.trimmed().isEmpty()) {
        m_statusLabel->setObjectName("LibraryEmptyLabel");
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setText(
            QString("No results for \"%1\"").arg(query.trimmed()));
        m_statusLabel->show();
        m_tileStrip->hide();
    } else if (m_tileStrip->visibleCount() > 0) {
        m_statusLabel->hide();
        m_tileStrip->show();
    }
}

void ComicsPage::refreshContinueStrip()
{
    m_continueStrip->clear();

    QJsonObject allProg = m_bridge->allProgress("comics");
    if (allProg.isEmpty()) {
        m_continueSection->hide();
        return;
    }

    struct ContinueItem {
        qint64 updatedAt;
        QString filePath;
        QString seriesPath;
        QString title;
        QString subtitle;
        QString coverPath;
        int volumeNumber = 0;  // parsed from the read cbz; 0 if unknown
    };
    QList<ContinueItem> items;

    for (auto it = allProg.begin(); it != allProg.end(); ++it) {
        QJsonObject prog = it.value().toObject();
        if (prog.value("finished").toBool())
            continue;
        int page = prog.value("page").toInt(0);
        if (page < 0)
            continue;

        // Look up file from our scan-built map
        auto ref = m_progressKeyMap.find(it.key());
        if (ref == m_progressKeyMap.end())
            continue;

        qint64 updatedAt = prog.value("updatedAt").toVariant().toLongLong();
        int pageCount = prog.value("pageCount").toInt(0);

        // TANKOYOMI_CONTINUE_READING 2026-05-15 — Tankoyomi-origin entries get
        // `<ChapterName> • Page X/Y` subtitle with title = series name. Folder-
        // imported keeps the historical `Page X/Y` shape with title = file basename.
        // The chip badge for Tankoyomi is rendered later (line ~1075) via the same
        // m_tyLibrary->getByCanonicalPath lookup; this branch only handles labels.
        const auto recOpt = m_tyLibrary
                                 ? m_tyLibrary->getByCanonicalPath(ref->seriesPath)
                                 : std::nullopt;
        QString title;
        QString subtitle;
        if (recOpt) {
            const auto labels = continueLabelsForRecord(*recOpt, ref->filePath, page, pageCount);
            title = labels.title;
            subtitle = labels.subtitle;
        } else {
            // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- folder-imported
            // path. Title is the series folder name; subtitle attempts the
            // same volume extraction as continueLabelsForRecord so a
            // folder-imported volume file ("My Series v03.cbz") still gets
            // the "<series> - Vol N - page X/Y" treatment per plan.
            const QString seriesDirName = QDir(ref->seriesPath).dirName();
            title = ScannerUtils::cleanMediaFolderTitle(
                seriesDirName.isEmpty()
                    ? QFileInfo(ref->filePath).completeBaseName()
                    : seriesDirName);
            const QString fileBase = QFileInfo(ref->filePath).completeBaseName();
            const QString pageLabel = pageCount > 0
                ? QString("Page %1/%2").arg(page + 1).arg(pageCount)
                : QString("Page %1").arg(page + 1);
            static const QRegularExpression volRe(
                QStringLiteral("(?:^|[\\s_-])(?:v|vol(?:ume)?)\\s*(\\d{1,3})\\b"),
                QRegularExpression::CaseInsensitiveOption);
            const auto volMatch = volRe.match(fileBase);
            if (fileBase.compare(QStringLiteral("Volume X"), Qt::CaseInsensitive) == 0) {
                // Volume X has no digit for volRe to catch — label it explicitly.
                subtitle = QStringLiteral("Vol X - %1").arg(pageLabel);
            } else if (volMatch.hasMatch()) {
                bool ok = false;
                const int vol = volMatch.captured(1).toInt(&ok);
                if (ok && vol > 0) {
                    subtitle = QStringLiteral("Vol %1 - %2").arg(vol).arg(pageLabel);
                } else {
                    subtitle = pageLabel;
                }
            } else {
                subtitle = pageLabel;
            }
        }

        // TANKOYOMI_PREMIUM Phase 7 Task 7.4 (Codex section 22 + 23) -- when
        // the user's last-read chapter is inside a downloaded Premium volume,
        // prefer the Premium cbz path over the loose WeebCentral cbz. NO-OP
        // in current v1 state because no Premium volume has been downloaded
        // yet (catalog curation lands in Phase 11); the code path lights up
        // automatically when real Premium downloads exist.
        QString preferredFilePath = ref->filePath;
        if (m_premiumCatalog && m_mangaDownloadIndex && recOpt) {
            const QString seriesTitle = recOpt->title;
            if (!seriesTitle.isEmpty()) {
                const auto entryOpt = m_premiumCatalog->entryForTitle(seriesTitle);
                if (entryOpt
                    && m_mangaDownloadIndex->hasAnyForSeries(
                           QStringLiteral("tankoyomi_premium"),
                           entryOpt->seriesId))
                {
                    // PHASE 11+ TODO: trailing-numeric extraction has a known
                    // false-positive case. Basename "Issue 5 Part 2" matches
                    // "2" via the current regex, but the user is reading
                    // chapter 5 -- would mis-route them to whichever Premium
                    // volume contains chapter 2 (if any). Acceptable in v1
                    // because no Premium volumes exist yet (catalog curation
                    // lands in Phase 11), so the swap is a provable NO-OP.
                    // When Phase 11 ships, REVISIT this before users hit it:
                    // either (a) tighten the regex to require a leading
                    // "Chapter "/"Ch."/"c" prefix, or (b) persist the chapter
                    // number explicitly from MangaDownloader's chapterCompleted
                    // signal so the filename round-trip is eliminated entirely.
                    const QString chapName =
                        QFileInfo(ref->filePath).completeBaseName();
                    static const QRegularExpression numRe(
                        QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*$"));
                    const auto m = numRe.match(chapName);
                    const QString readChapter =
                        m.hasMatch() ? m.captured(1) : QString();
                    if (!readChapter.isEmpty()) {
                        for (const auto& v : entryOpt->volumes) {
                            bool inThisVolume = false;
                            for (const auto& ch : v.chapters) {
                                if (ch.chapterNumber == readChapter) {
                                    inThisVolume = true;
                                    break;
                                }
                            }
                            if (!inThisVolume) continue;
                            const auto pathOpt =
                                m_mangaDownloadIndex->filePathFor(
                                    QStringLiteral("tankoyomi_premium"),
                                    entryOpt->seriesId, readChapter);
                            if (pathOpt) preferredFilePath = *pathOpt;
                            break;
                        }
                    }
                }
            }
        }

        // COMICS_CR_VOLUME_COVER 2026-05-29 (Agent 1). Parse the read volume
        // number from the cbz basename so the tile can pull that volume's real
        // catalog cover (a WeebCentral compilation's first page isn't the cover).
        int crVolumeNumber = 0;
        {
            static const QRegularExpression crVolRe(
                QStringLiteral("(?:^|[\\s_-])(?:v|vol(?:ume)?)\\s*(\\d{1,3})\\b"),
                QRegularExpression::CaseInsensitiveOption);
            const auto m = crVolRe.match(QFileInfo(preferredFilePath).completeBaseName());
            if (m.hasMatch()) crVolumeNumber = m.captured(1).toInt();
        }
        items.append({updatedAt, preferredFilePath, ref->seriesPath, title, subtitle,
                      ref->coverPath, crVolumeNumber});
    }

    if (items.isEmpty()) {
        m_continueSection->hide();
        return;
    }

    // Per-series dedup: keep only the most recently updated volume per series
    QMap<QString, int> bestPerSeries;  // seriesPath → index in items
    for (int i = 0; i < items.size(); ++i) {
        auto it = bestPerSeries.find(items[i].seriesPath);
        if (it == bestPerSeries.end() || items[i].updatedAt > items[it.value()].updatedAt)
            bestPerSeries[items[i].seriesPath] = i;
    }

    QList<ContinueItem> deduped;
    for (int idx : bestPerSeries)
        deduped.append(items[idx]);

    std::sort(deduped.begin(), deduped.end(), [](const ContinueItem& a, const ContinueItem& b) {
        return a.updatedAt > b.updatedAt;
    });

    // Groundwork limit: 40 tiles max
    if (deduped.size() > 40)
        deduped = deduped.mid(0, 40);

    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- when at least one
    // Continue tile exists, suppress the global library-empty status label
    // that refreshLibraryStrips() may have shown (e.g. user has Continue
    // history but no completed downloads + no bookmarks).
    if (!deduped.isEmpty() && m_statusLabel) {
        m_statusLabel->hide();
    }

    for (const auto& item : deduped) {
        // COMICS_CR_VOLUME_COVER 2026-05-29 (Agent 1). Prefer the read volume's
        // real catalog cover over the cbz first-page thumbnail (which is interior
        // chapter art for WeebCentral-compiled volumes like One Piece Vol 114).
        // TileCard paints coverPath over coverUrl when both set, so leave
        // coverPath empty when we have a catalog URL and let fetchPosterForTile
        // paint it; keep the cbz thumbnail otherwise.
        const QString volCoverUrl = resolveReadVolumeCover(item.title, item.volumeNumber);
        auto* card = (!volCoverUrl.isEmpty())
            ? new TileCard(QString(), item.title, item.subtitle)
            : new TileCard(item.coverPath, item.title, item.subtitle);
        card->setProperty("filePath", item.filePath);
        card->setProperty("seriesPath", item.seriesPath);
        card->setProperty("seriesName", ScannerUtils::cleanMediaFolderTitle(
            QDir(item.seriesPath).dirName()));
        card->setProperty("coverPath", volCoverUrl.isEmpty() ? item.coverPath : QString());
        connect(card, &TileCard::clicked, this, [this, card]() {
            QString path = card->property("filePath").toString();
            QString seriesPath = card->property("seriesPath").toString();
            QString seriesName = card->property("seriesName").toString();
            QDir dir(seriesPath);
            QStringList files = dir.entryList(COMIC_EXTS, QDir::Files);
            QCollator col;
            col.setNumericMode(true);
            std::sort(files.begin(), files.end(), [&col](const QString& a, const QString& b) {
                return col.compare(a, b) < 0;
            });
            QStringList cbzList;
            for (const auto& f : files)
                cbzList.append(dir.absoluteFilePath(f));
            emit openComic(path, cbzList, seriesName);
        });
        m_continueStrip->addTile(card);
        if (!volCoverUrl.isEmpty())
            fetchPosterForTile(card, /*anilistId*/0, volCoverUrl);
    }

    m_continueSection->show();
}

void ComicsPage::onCardClicked()
{
    auto* card = qobject_cast<TileCard*>(sender());
    if (!card) return;
    openSeriesByPath(card->property("seriesPath").toString(),
                     card->property("seriesName").toString(),
                     card->property("coverPath").toString());
}

void ComicsPage::onTileContextMenu(const QPoint& pos)
{
    auto* card = m_tileStrip->tileAt(pos);
    if (!card) return;

    QString seriesKey  = card->property("seriesKey").toString();
    QString seriesName = card->property("seriesName").toString();
    QString coverPath  = card->property("coverPath").toString();
    int     anilistId  = card->property("anilistId").toInt();

    // Detect MangaDownloadIndex-backed tile vs folder-scanner tile.
    const bool isMdiTile = !seriesKey.isEmpty();

    auto* menu = ContextMenuHelper::createMenu(this);

    // Open series
    auto* openAct = menu->addAction(tr("Open series"));
    menu->addSeparator();

    // Mark as read/unread
    QAction* markAct = nullptr;
    if (!isMdiTile) {
        QString seriesPath = card->property("seriesPath").toString();
        QDir dir(seriesPath);
        QStringList cbzFiles = dir.entryList(COMIC_EXTS, QDir::Files);
        QJsonObject allProg = m_bridge ? m_bridge->allProgress("comics") : QJsonObject();
        bool allFinished = !cbzFiles.isEmpty();
        for (const auto& f : cbzFiles) {
            QString id = QString(QCryptographicHash::hash(
                dir.absoluteFilePath(f).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            if (!allProg.value(id).toObject().value("finished").toBool()) {
                allFinished = false;
                break;
            }
        }
        markAct = menu->addAction(allFinished ? tr("Mark all as unread") : tr("Mark all as read"));
    }

    menu->addSeparator();

    // Rename
    QAction* renameAct = nullptr;
    if (isMdiTile) {
        renameAct = menu->addAction(tr("Rename…"));
    } else {
        renameAct = menu->addAction(tr("Rename series…"));
    }

    // Refresh metadata (D4 — MangaFire catalog re-resolve)
    QAction* refreshAct = nullptr;
    if (isMdiTile && !seriesName.isEmpty()) {
        refreshAct = menu->addAction(tr("Refresh metadata"));
    }

    // Hide series (folder-based only)
    QAction* hideAct = nullptr;
    if (!isMdiTile) {
        hideAct = menu->addAction(tr("Hide series"));
    }

    auto* revealAct = menu->addAction(tr("Reveal in File Explorer"));
    auto* copyAct   = menu->addAction(tr("Copy path"));

    // Resolve paths for enable/disable
    QString displayPath;
    if (isMdiTile && m_mangaDownloadIndex) {
        const int sep = seriesKey.indexOf(QLatin1Char(':'));
        if (sep > 0) {
            const QString src = seriesKey.left(sep);
            const QString sid = seriesKey.mid(sep + 1);
            const auto entries = m_mangaDownloadIndex->entriesForSeries(src, sid);
            if (!entries.isEmpty())
                displayPath = QFileInfo(entries.first().canonicalPath).absolutePath();
        }
    } else {
        displayPath = card->property("seriesPath").toString();
    }
    revealAct->setEnabled(!displayPath.isEmpty());
    copyAct->setEnabled(!displayPath.isEmpty());

    menu->addSeparator();

    auto* removeAct = ContextMenuHelper::addDangerAction(menu, tr("Remove series…"));

    QAction* chosen = menu->exec(m_tileStrip->mapToGlobal(pos));

    if (chosen == openAct) {
        if (isMdiTile && m_mangaDownloadIndex) {
            const int sep = seriesKey.indexOf(QLatin1Char(':'));
            if (sep > 0) {
                const QString src = seriesKey.left(sep);
                const QString sid = seriesKey.mid(sep + 1);
                const auto entries = m_mangaDownloadIndex->entriesForSeries(src, sid);
                if (!entries.isEmpty()) {
                    if (m_tyLibrary) {
                        const auto rec = m_tyLibrary->get(src, sid);
                        if (!rec.seriesId.isEmpty()) {
                            openSeriesByRecord(rec);
                            goto menu_done;
                        }
                    }
                    if (anilistId > 0)
                        openSeriesByAnilistId(anilistId, seriesName);
                }
            }
        } else {
            QString sp = card->property("seriesPath").toString();
            openSeriesByPath(sp, seriesName, coverPath);
        }
    } else if (chosen == markAct && !isMdiTile && m_bridge) {
        QString sp = card->property("seriesPath").toString();
        QDir dir(sp);
        QStringList cbzFiles = dir.entryList(COMIC_EXTS, QDir::Files);
        QJsonObject allProg = m_bridge->allProgress("comics");
        bool allFinished = !cbzFiles.isEmpty();
        for (const auto& f : cbzFiles) {
            QString id = QString(QCryptographicHash::hash(
                dir.absoluteFilePath(f).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            if (!allProg.value(id).toObject().value("finished").toBool()) {
                allFinished = false;
                break;
            }
        }
        bool setFinished = !allFinished;
        for (const auto& f : cbzFiles) {
            QString id = QString(QCryptographicHash::hash(
                dir.absoluteFilePath(f).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            QJsonObject prog = m_bridge->progress("comics", id);
            prog["finished"] = setFinished;
            m_bridge->saveProgress("comics", id, prog);
        }
    } else if (chosen == renameAct) {
        if (isMdiTile && m_mangaDownloadIndex) {
            const int sep = seriesKey.indexOf(QLatin1Char(':'));
            if (sep > 0) {
                const QString src = seriesKey.left(sep);
                const QString sid = seriesKey.mid(sep + 1);
                const auto entries = m_mangaDownloadIndex->entriesForSeries(src, sid);
                if (!entries.isEmpty()) {
                    QString oldDir = QFileInfo(entries.first().canonicalPath).absolutePath();
                    QString oldName = QDir(oldDir).dirName();
                    QString newName = QInputDialog::getText(this, tr("Rename series"),
                        tr("New name:"), QLineEdit::Normal, oldName);
                    if (!newName.isEmpty() && newName != oldName) {
                        QString parentPath = QFileInfo(oldDir).absolutePath();
                        QString newDir = parentPath + "/" + newName.trimmed();
                        if (QDir().rename(oldDir, newDir)) {
                            for (const auto& e : entries) {
                                QString oldPath = e.canonicalPath;
                                QString relPath = oldPath.mid(oldDir.length() + 1);
                                QString newPath = newDir + "/" + relPath;
                                m_mangaDownloadIndex->evictByChapter(e.sourceId, e.seriesId, e.chapterId);
                                m_mangaDownloadIndex->registerChapter(e.sourceId, e.seriesId,
                                    e.chapterId, newPath, e.fileSizeBytes);
                            }
                        } else {
                            QMessageBox::warning(this, tr("Rename failed"),
                                tr("Could not rename \"%1\".\nThe folder may be in use by another program.")
                                    .arg(oldName));
                        }
                    }
                }
            }
        } else {
            QString sp = card->property("seriesPath").toString();
            QString dirName = QDir(sp).dirName();
            QString newName = QInputDialog::getText(this, tr("Rename series"),
                tr("New name:"), QLineEdit::Normal, dirName);
            if (!newName.isEmpty() && newName != dirName) {
                QString parentPath = QFileInfo(sp).absolutePath();
                QString oldPath = parentPath + "/" + dirName;
                QString newPath = parentPath + "/" + newName.trimmed();
                if (QFile::rename(oldPath, newPath)) {
                    triggerScan();
                } else {
                    QMessageBox::warning(this, tr("Rename failed"),
                        tr("Could not rename \"%1\".\nThe folder may be in use by another program.")
                            .arg(dirName));
                }
            }
        }
    } else if (chosen == refreshAct && isMdiTile) {
        dispatchCatalogResolve(
            fandomSeriesSlugFromTitle(seriesName), seriesName);
    } else if (chosen == hideAct && !isMdiTile) {
        QString sp = card->property("seriesPath").toString();
        QSettings settings("Tankoban", "Tankoban");
        QStringList hidden = settings.value("comics_hidden_series").toStringList();
        if (!hidden.contains(sp)) {
            hidden.append(sp);
            settings.setValue("comics_hidden_series", hidden);
        }
        card->hide();
        // Shared-recipe guard: only filter the manga grid when on manga shelf.
        m_tileStrip->filterTiles(m_stack && m_stack->currentIndex() == 0 ? m_searchBar->text() : QString());
    } else if (chosen == revealAct) {
        ContextMenuHelper::revealInExplorer(displayPath);
    } else if (chosen == copyAct) {
        ContextMenuHelper::copyToClipboard(displayPath);
    } else if (chosen == removeAct) {
        if (isMdiTile && m_mangaDownloadIndex && !seriesKey.isEmpty()) {
            const int sep = seriesKey.indexOf(QLatin1Char(':'));
            if (sep > 0) {
                const QString src = seriesKey.left(sep);
                const QString sid = seriesKey.mid(sep + 1);
                const auto entries = m_mangaDownloadIndex->entriesForSeries(src, sid);
                const auto choice = ContextMenuHelper::confirmRemoveWithFile(
                    this, tr("Remove series"),
                    tr("Remove \"%1\" (%2 volume%3) from your library?")
                        .arg(seriesName).arg(entries.size())
                        .arg(entries.size() == 1 ? QString() : QStringLiteral("s")));
                if (choice == ContextMenuHelper::RemoveChoice::Cancel) goto menu_done;
                if (choice == ContextMenuHelper::RemoveChoice::DeleteFile) {
                    for (const auto& e : entries) {
                        QFile::remove(e.canonicalPath);
                        QFile::remove(e.canonicalPath + QStringLiteral(".volx"));
                    }
                }
                m_mangaDownloadIndex->evictBySeries(src, sid);
            }
        } else {
            // BUGFIX 2026-05-30 (Agent 1): non-MDI tiles cover BOTH bookmark-only
            // series (anilistId>0, no local files) and folder-scanned series. The
            // old body only called triggerScan() -- which re-adds folder series and
            // NEVER drops a bookmark, so "Remove series..." silently did nothing for
            // bookmarked titles (Berserk/Bleach/Yu Yu Hakusho/etc.). Now: drop the
            // bookmark when there is one (the grid re-renders via bookmarksChanged ->
            // refreshLibraryStrips); triggerScan stays only for genuine folder tiles.
            QString sp = card->property("seriesPath").toString();
            const bool isBookmark = (anilistId > 0 && m_anilistCache
                                     && m_anilistCache->isBookmarked(anilistId));
            if (ContextMenuHelper::confirmRemove(this, tr("Remove from library"),
                    tr("Remove this series from the library?\nFiles will not be deleted from disk."))) {
                if (isBookmark)
                    m_anilistCache->removeBookmark(anilistId);
                if (!sp.isEmpty())
                    triggerScan();
            }
        }
    }
menu_done:
    menu->deleteLater();
}

void ComicsPage::onMultiSelectContextMenu(const QList<TileCard*>& selected, const QPoint& globalPos)
{
    int count = selected.size();
    if (count < 2) return;

    auto* menu = ContextMenuHelper::createMenu(this);

    auto* openFirstAct = menu->addAction("Open first selected");
    menu->addSeparator();
    auto* markReadAct   = menu->addAction("Mark all as read");
    auto* markUnreadAct = menu->addAction("Mark all as unread");
    menu->addSeparator();
    auto* removeAct = ContextMenuHelper::addDangerAction(menu,
        QString("Remove %1 items").arg(count));

    auto* chosen = menu->exec(globalPos);
    if (chosen == openFirstAct) {
        auto* first = selected.first();
        openSeriesByPath(first->property("seriesPath").toString(),
                         first->property("seriesName").toString(),
                         first->property("coverPath").toString());
    } else if (chosen == markReadAct || chosen == markUnreadAct) {
        bool setFinished = (chosen == markReadAct);
        QJsonObject allProg = m_bridge->allProgress("comics");
        for (auto* card : selected) {
            QString seriesPath = card->property("seriesPath").toString();
            QDir dir(seriesPath);
            for (const auto& f : dir.entryList(COMIC_EXTS, QDir::Files)) {
                QString id = QString(QCryptographicHash::hash(
                    dir.absoluteFilePath(f).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                QJsonObject prog = m_bridge->progress("comics", id);
                prog["finished"] = setFinished;
                m_bridge->saveProgress("comics", id, prog);
            }
        }
        refreshContinueStrip();
    } else if (chosen == removeAct) {
        if (ContextMenuHelper::confirmRemove(this, "Remove from library",
                QString("Remove %1 items from library?\nFiles will not be deleted from disk.").arg(count))) {
            triggerScan();
        }
    }
    menu->deleteLater();
}

// ── PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) ─────────────────────────────
// restoreLayer: re-render the targeted layer WITHOUT emitting enteredLayer.
// Called by MainWindow::onLayerRestoreRequested when the controller fires
// layerRestoreRequested for pageId="comics". Dispatches on target.kind and
// replays the layer state using the same private helpers as restoreNavState
// (which remains alive until Task 12). The QScopedValueRollback on
// m_inNavRestore suppresses re-emission on every code path below.
void ComicsPage::restoreLayer(const tankoban::ui::LayerEntry& target)
{
    QScopedValueRollback<bool> rollback(m_inNavRestore, true);
    const QString kind     = target.kind;
    const QJsonObject blob = target.stateBlob;

    if (kind == QStringLiteral("library")) {
        showLibraryMode();
        if (m_sortCombo) {
            const QString sort = blob.value(QStringLiteral("sort")).toString();
            if (!sort.isEmpty()) {
                for (int i = 0; i < m_sortCombo->count(); ++i) {
                    if (m_sortCombo->itemData(i).toString() == sort) {
                        m_sortCombo->setCurrentIndex(i);
                        break;
                    }
                }
            }
        }
        // NAV_BACK_ROOT_SEED 2026-05-21 (Agent 5) -- only force scrollY if the
        // blob carried it. The persistent library root carries an empty blob
        // (no sort, no scrollY); without this gate, restoring the root would
        // snap scroll to 0 instead of letting Qt's retained widget state hold
        // whatever position the user left the library grid in.
        if (m_gridScroll && blob.contains(QStringLiteral("scrollY"))) {
            if (auto* vsb = m_gridScroll->verticalScrollBar())
                vsb->setValue(blob.value(QStringLiteral("scrollY")).toInt(0));
        }
        return;
    }

    if (kind == QStringLiteral("searchResults")) {
        const QString q = blob.value(QStringLiteral("query")).toString();
        if (m_searchBar) m_searchBar->setText(q);
        showSearchMode(q);
        return;
    }

    if (kind == QStringLiteral("westernGrid")) {
        // m_inNavRestore is active (rollback above) so showWesternMode won't
        // re-push its own layer. Re-renders the Western browse grid.
        showWesternMode();
        return;
    }

    if (kind == QStringLiteral("seriesView") && m_tyVolumeSeriesView) {
        // Western detail restore: re-render via the Western loader, NOT the manga
        // dispatchCatalogResolve path (no enrichment bleed). Must precede the
        // anilistId / library-record branches — a Western series has anilistId=0
        // and no Tankoyomi record, so it would otherwise fall through to a silent
        // no-render. m_inNavRestore is active, so openWesternSeriesFromJson won't
        // re-emit its layer.
        if (blob.value(QStringLiteral("enteredFrom")).toString() == QStringLiteral("western")) {
            QString jsonPath = blob.value(QStringLiteral("jsonPath")).toString();
            if (jsonPath.isEmpty()) {
                const QString sid = blob.value(QStringLiteral("seriesId")).toString();
                if (!sid.isEmpty())
                    jsonPath = QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
                                   .absoluteFilePath(sid + QStringLiteral(".json"));
            }
            openWesternSeriesFromJson(jsonPath);
            return;
        }
        const int anilistId        = blob.value(QStringLiteral("anilistId")).toInt(0);
        const QString seriesTitle  = blob.value(QStringLiteral("seriesTitle")).toString();
        if (anilistId > 0) {
            m_detailEnteredFromWestern = false;  // manga restore -> in-view Back goes to manga lib, not the Western grid (Codex r3 2026-06-02)
            m_enteredDetailFrom = (blob.value(QStringLiteral("enteredFrom")).toString() == QStringLiteral("search")
                                    ? Mode::SearchResults : Mode::Library);
            m_mode = Mode::TankoyomiDetail;
            tankoban::manga::anilist::MediaPreview preview;
            preview.anilistId = anilistId;
            preview.title     = seriesTitle;
            m_currentDetailAnilistId   = anilistId;
            m_currentDetailSeriesTitle = seriesTitle;
            m_tyVolumeSeriesView->showSeries(preview);
            dispatchCatalogResolve(fandomSeriesSlugFromTitle(seriesTitle),
                                   /*titleHint*/seriesTitle);
            m_stack->setCurrentWidget(m_tyVolumeSeriesView);
            return;
        }
        // WEEBCENTRAL_IDENTITY_PIVOT Task 11 (2026-05-19) -- restore path for
        // nav-blobs written by openSeriesByRecord (sourceId+seriesId identity,
        // no anilistId). Reconstruct MangaResult from library record and
        // re-open via showSeries(MangaResult).
        const QString sourceId = blob.value(QStringLiteral("sourceId")).toString();
        const QString seriesId = blob.value(QStringLiteral("seriesId")).toString();
        if (!seriesId.isEmpty() && m_tyLibrary) {
            const auto rec = m_tyLibrary->get(sourceId, seriesId);
            if (!rec.seriesId.isEmpty()) {
                m_detailEnteredFromWestern = false;  // manga restore -> in-view Back goes to manga lib, not the Western grid (Codex r3 2026-06-02)
                m_enteredDetailFrom = (blob.value(QStringLiteral("enteredFrom")).toString() == QStringLiteral("search")
                                        ? Mode::SearchResults : Mode::Library);
                m_mode = Mode::TankoyomiDetail;
                m_currentDetailAnilistId   = 0;
                m_currentDetailSeriesTitle = rec.title;
                MangaResult result;
                result.id           = rec.seriesId;
                result.url          = QString();
                result.title        = rec.title;
                result.author       = rec.detailCache.author;
                result.thumbnailUrl = rec.coverPath.isEmpty()
                                      ? QString()
                                      : QStringLiteral("file:///") + rec.coverPath;
                result.source       = rec.sourceId;
                result.status       = rec.detailCache.status;
                result.type         = QStringLiteral("manga");
                m_tyVolumeSeriesView->showSeries(result);
                dispatchCatalogResolve(fandomSeriesSlugFromTitle(rec.title),
                                       /*titleHint*/rec.title);
                m_stack->setCurrentWidget(m_tyVolumeSeriesView);
                return;
            }
        }
    }

    if (kind == QStringLiteral("folderSeriesView") && m_seriesView) {
        const QString seriesPath = blob.value(QStringLiteral("seriesPath")).toString();
        const QString seriesName = blob.value(QStringLiteral("seriesName")).toString();
        const QString coverPath  = blob.value(QStringLiteral("coverPath")).toString();
        m_seriesView->showSeries(seriesPath, seriesName, coverPath);
        m_stack->setCurrentIndexAnimated(1);
        return;
    }
}

// -----------------------------------------------------------------------
// dev-control bridge
// -----------------------------------------------------------------------

QJsonObject ComicsPage::devSnapshot() const
{
    QJsonObject snap;
    QString layer = QStringLiteral("library");
    switch (m_mode) {
    case Mode::Library: layer = QStringLiteral("library"); break;
    case Mode::SearchResults: layer = QStringLiteral("search-results"); break;
    case Mode::TankoyomiDetail: layer = QStringLiteral("series-view"); break;
    }
    if (m_stack && m_stack->currentWidget() == m_seriesView)
        layer = QStringLiteral("folder-series-view");

    snap[QStringLiteral("layer")] = layer;
    snap[QStringLiteral("stackIndex")] = m_stack ? m_stack->currentIndex() : -1;
    snap[QStringLiteral("searchQuery")] = m_searchBar ? m_searchBar->text() : QString();
    snap[QStringLiteral("gridMode")] = m_gridMode;
    snap[QStringLiteral("hasScanned")] = m_hasScanned;
    snap[QStringLiteral("scanning")] = m_scanning;
    snap[QStringLiteral("folderSeriesCount")] = m_folderSeries.size();
    snap[QStringLiteral("currentAnilistId")] = m_currentDetailAnilistId;
    snap[QStringLiteral("currentSeriesTitle")] = m_currentDetailSeriesTitle;
    snap[QStringLiteral("tyLibraryCount")] = m_tyLibrary ? m_tyLibrary->all().size() : 0;
    snap[QStringLiteral("downloadedSeriesCount")] =
        m_mangaDownloadIndex ? m_mangaDownloadIndex->entriesForAllSeries().size() : 0;
    snap[QStringLiteral("bookmarkedCount")] =
        m_anilistCache ? m_anilistCache->bookmarkedPreviews().size() : 0;
    if (m_tyVolumeSeriesView)
        snap[QStringLiteral("seriesView")] = m_tyVolumeSeriesView->devSnapshot();
    snap[QStringLiteral("library")] = devLibrarySection();
    return snap;
}

QJsonObject ComicsPage::devLibrarySnapshot() const
{
    QJsonObject out;
    QJsonArray entries;

    for (const SeriesInfo& s : m_folderSeries) {
        QJsonObject obj;
        obj[QStringLiteral("origin")] = QStringLiteral("folder");
        obj[QStringLiteral("title")] = s.seriesName;
        obj[QStringLiteral("seriesPath")] = s.seriesPath;
        obj[QStringLiteral("coverPath")] = s.coverThumbPath;
        obj[QStringLiteral("fileCount")] = s.fileCount;
        obj[QStringLiteral("newestMtimeMs")] = static_cast<double>(s.newestMtimeMs);
        entries.append(obj);
    }

    if (m_tyLibrary) {
        for (const ComicsLibraryRecord& r : m_tyLibrary->all()) {
            QJsonObject obj;
            obj[QStringLiteral("origin")] = r.origin.isEmpty()
                ? QStringLiteral("tankoyomi")
                : r.origin;
            obj[QStringLiteral("sourceId")] = r.sourceId;
            obj[QStringLiteral("seriesId")] = r.seriesId;
            obj[QStringLiteral("title")] = r.title;
            obj[QStringLiteral("rootFolder")] = r.rootFolder;
            obj[QStringLiteral("seriesFolderName")] = r.seriesFolderName;
            obj[QStringLiteral("canonicalSeriesPath")] = r.canonicalSeriesPath;
            obj[QStringLiteral("coverPath")] = r.coverPath;
            obj[QStringLiteral("addedAt")] = static_cast<double>(r.addedAt);
            obj[QStringLiteral("lastValidatedAt")] = static_cast<double>(r.lastValidatedAt);
            entries.append(obj);
        }
    }

    if (m_anilistCache) {
        for (const auto& p : m_anilistCache->bookmarkedPreviews()) {
            QJsonObject obj = mediaPreviewJson(p);
            obj[QStringLiteral("origin")] = QStringLiteral("bookmark");
            obj[QStringLiteral("sourceId")] = QStringLiteral("anilist");
            obj[QStringLiteral("seriesId")] = QString::number(p.anilistId);
            obj[QStringLiteral("title")] = p.title;
            entries.append(obj);
        }
    }

    out[QStringLiteral("entries")] = entries;
    out[QStringLiteral("count")] = entries.size();
    return out;
}

QJsonObject ComicsPage::devSeriesSnapshot() const
{
    if (!m_tyVolumeSeriesView ||
        m_stack->currentWidget() != m_tyVolumeSeriesView ||
        m_mode != Mode::TankoyomiDetail) {
        return QJsonObject{{QStringLiteral("series"), QJsonValue::Null}};
    }
    return QJsonObject{{QStringLiteral("series"), m_tyVolumeSeriesView->devSnapshot()}};
}

QJsonObject ComicsPage::devSelectVolume(int row)
{
    if (!m_tyVolumeSeriesView ||
        m_stack->currentWidget() != m_tyVolumeSeriesView ||
        m_mode != Mode::TankoyomiDetail) {
        return QJsonObject{{QStringLiteral("status"), QStringLiteral("error")},
                           {QStringLiteral("message"), QStringLiteral("no active ComicsSeriesView")}};
    }
    return m_tyVolumeSeriesView->devSelectVolume(row);
}

QJsonObject ComicsPage::devOpenSeries(const QString& seriesId)
{
    const int anilistId = parseAnilistSeriesId(seriesId);
    if (anilistId <= 0) {
        return QJsonObject{{QStringLiteral("status"), QStringLiteral("error")},
                           {QStringLiteral("message"), QStringLiteral("seriesId must be an AniList id or anilist_<id>")}};
    }

    QString title = QStringLiteral("anilist_%1").arg(anilistId);
    if (m_anilistCache) {
        if (auto detail = m_anilistCache->get(anilistId))
            title = detail->preview.title.isEmpty() ? title : detail->preview.title;
    }
    openSeriesByAnilistId(anilistId, title);
    return QJsonObject{{QStringLiteral("status"), QStringLiteral("ok")},
                       {QStringLiteral("anilistId"), anilistId},
                       {QStringLiteral("title"), title},
                       {QStringLiteral("snapshot"), devSnapshot()}};
}

QJsonObject ComicsPage::devOpenChapter(const QString& seriesId,
                                       int volumeNumber,
                                       int chapterNumber)
{
    Q_UNUSED(chapterNumber);
    const int anilistId = parseAnilistSeriesId(seriesId);
    if (anilistId <= 0 || volumeNumber <= 0) {
        return QJsonObject{{QStringLiteral("status"), QStringLiteral("error")},
                           {QStringLiteral("message"), QStringLiteral("seriesId and positive volume required")}};
    }

    std::optional<MangaDownloadIndex::Entry> entry;
    if (m_mangaDownloadIndex && m_premiumCatalog) {
        if (auto hit = m_premiumCatalog->entryForAnilistIdAndVolume(anilistId, volumeNumber)) {
            entry = m_mangaDownloadIndex->entryForSeriesAndVolume(
                QString::fromLatin1(TANKOYOMI_PREMIUM_SOURCE_ID),
                hit->first.seriesId,
                volumeNumber);
        }
    }
    if (!entry && m_mangaDownloadIndex) {
        const QString fallbackSeriesId = QStringLiteral("anilist_%1").arg(anilistId);
        entry = m_mangaDownloadIndex->entryForSeriesAndVolume(
            QString::fromLatin1(TANKOYOMI_PREMIUM_SOURCE_ID), fallbackSeriesId, volumeNumber);
        if (!entry) {
            entry = m_mangaDownloadIndex->entryForSeriesAndVolume(
                QString::fromLatin1(WEEBCENTRAL_PACKER_SOURCE_ID), fallbackSeriesId, volumeNumber);
        }
    }
    if (!entry || entry->canonicalPath.isEmpty()) {
        return QJsonObject{{QStringLiteral("status"), QStringLiteral("error")},
                           {QStringLiteral("message"), QStringLiteral("downloaded volume not found")}};
    }

    if (m_currentDetailAnilistId != anilistId)
        devOpenSeries(QString::number(anilistId));
    onComicsSeriesOpenVolume(volumeNumber, entry->canonicalPath);
    return QJsonObject{{QStringLiteral("status"), QStringLiteral("ok")},
                       {QStringLiteral("anilistId"), anilistId},
                       {QStringLiteral("volume"), volumeNumber},
                       {QStringLiteral("chapter"), chapterNumber},
                       {QStringLiteral("path"), entry->canonicalPath}};
}

QJsonObject ComicsPage::devSearchTankoyomi(const QString& query, int timeoutMs)
{
    QJsonObject out;
    if (!m_anilistClient || query.trimmed().isEmpty()) {
        out[QStringLiteral("status")] = QStringLiteral("bad_request");
        return out;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QList<tankoban::manga::anilist::MediaPreview> results;
    QString error;
    bool timedOut = false;
    const int requestId =
        static_cast<int>((QDateTime::currentMSecsSinceEpoch() % 1000000000) + 1000);

    QMetaObject::Connection okConn;
    QMetaObject::Connection failConn;
    okConn = connect(m_anilistClient,
        &tankoban::manga::anilist::AniListClient::searchSucceeded,
        &loop,
        [&](int reqId, const QList<tankoban::manga::anilist::MediaPreview>& found) {
            if (reqId != requestId) return;
            results = found;
            loop.quit();
        });
    failConn = connect(m_anilistClient,
        &tankoban::manga::anilist::AniListClient::searchFailed,
        &loop,
        [&](int reqId, const QString& reason) {
            if (reqId != requestId) return;
            error = reason;
            loop.quit();
        });
    connect(&timer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });

    m_anilistClient->searchByTitle(query.trimmed(), requestId);
    timer.start(qBound(1000, timeoutMs, 15000));
    loop.exec();
    disconnect(okConn);
    disconnect(failConn);

    QJsonArray arr;
    for (const auto& p : results)
        arr.append(mediaPreviewJson(p));
    out[QStringLiteral("status")] = timedOut ? QStringLiteral("timeout") : QStringLiteral("ok");
    out[QStringLiteral("timedOut")] = timedOut;
    out[QStringLiteral("error")] = error;
    out[QStringLiteral("results")] = arr;
    return out;
}

QJsonObject ComicsPage::devDownloadsSnapshot() const
{
    QJsonObject out;
    QJsonArray indexed;
    QSet<QString> seenPaths;
    if (m_mangaDownloadIndex) {
        for (const auto& representative : m_mangaDownloadIndex->entriesForAllSeries()) {
            for (const auto& e : m_mangaDownloadIndex->entriesForSeries(
                     representative.sourceId, representative.seriesId)) {
                const QString key = MangaDownloadIndex::computeCanonicalKey(e.canonicalPath);
                if (seenPaths.contains(key)) continue;
                seenPaths.insert(key);
                indexed.append(mangaDownloadEntryJson(e));
            }
        }
    }
    out[QStringLiteral("indexed")] = indexed;

    QJsonArray active;
    if (m_mangaDownloader) {
        for (const MangaDownloadRecord& rec : m_mangaDownloader->listActive()) {
            QJsonObject obj;
            obj[QStringLiteral("id")] = rec.id;
            obj[QStringLiteral("seriesTitle")] = rec.seriesTitle;
            obj[QStringLiteral("source")] = rec.source;
            obj[QStringLiteral("destinationPath")] = rec.destinationPath;
            obj[QStringLiteral("status")] = rec.status;
            obj[QStringLiteral("paused")] = rec.paused;
            obj[QStringLiteral("progress")] = rec.progress;
            obj[QStringLiteral("totalChapters")] = rec.totalChapters;
            obj[QStringLiteral("completedChapters")] = rec.completedChapters;
            active.append(obj);
        }
    }
    out[QStringLiteral("active")] = active;

    QJsonArray pending;
    for (auto it = m_pendingVolumeDispatches.constBegin();
         it != m_pendingVolumeDispatches.constEnd(); ++it) {
        QJsonObject obj;
        obj[QStringLiteral("key")] = it.key();
        obj[QStringLiteral("kind")] = static_cast<int>(it->kind);
        obj[QStringLiteral("anilistId")] = it->anilistId;
        QJsonArray chapters;
        for (const QString& chapter : it->chapterIds)
            chapters.append(chapter);
        obj[QStringLiteral("chapterIds")] = chapters;
        pending.append(obj);
    }
    out[QStringLiteral("pendingVolumeDispatches")] = pending;
    return out;
}

QJsonObject ComicsPage::devDispatchVolume(const QString& seriesId,
                                          int volumeNumber,
                                          const QString& source)
{
    const int anilistId = parseAnilistSeriesId(seriesId);
    if (anilistId <= 0 || volumeNumber <= 0) {
        return QJsonObject{{QStringLiteral("status"), QStringLiteral("error")},
                           {QStringLiteral("message"), QStringLiteral("seriesId and positive volume required")}};
    }
    if (!m_tyVolumeSeriesView ||
        m_tyVolumeSeriesView->currentAnilistId() != anilistId ||
        m_stack->currentWidget() != m_tyVolumeSeriesView) {
        devOpenSeries(QString::number(anilistId));
    }
    if (!m_tyVolumeSeriesView || m_tyVolumeSeriesView->currentAnilistId() != anilistId) {
        return QJsonObject{{QStringLiteral("status"), QStringLiteral("error")},
                           {QStringLiteral("message"), QStringLiteral("series view not ready")}};
    }
    return m_tyVolumeSeriesView->devDispatchVolume(volumeNumber, source);
}

QJsonObject ComicsPage::devSourcesSnapshot() const
{
    if (!m_tyVolumeSeriesView)
        return QJsonObject{{QStringLiteral("sources"), QJsonValue::Null}};
    return QJsonObject{{QStringLiteral("sources"), m_tyVolumeSeriesView->devSourcesSnapshot()}};
}

// -----------------------------------------------------------------------
// v1.11 Western download smoke harness (2026-06-02).
// Three headless commands so a smoke harness can open a baked Western series,
// trigger an edition download, and poll volume state without touching the UI.
// -----------------------------------------------------------------------

QJsonObject ComicsPage::devOpenWesternSeries(const QString& seriesId)
{
    if (seriesId.trimmed().isEmpty()) {
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"), QStringLiteral("seriesId required")}};
    }
    const QString path =
        QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
            .absoluteFilePath(seriesId + QLatin1String(".json"));
    if (!QFile::exists(path)) {
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"),
                            QStringLiteral("catalogue file not found: ") + path}};
    }
    openWesternSeriesFromJson(path);
    // openWesternSeriesFromJson sets m_pendingWesternSeriesId + m_currentDetailSeriesTitle.
    const int editionCount = m_tyVolumeSeriesView
        ? m_tyVolumeSeriesView->devSnapshot().value(QStringLiteral("tileCount")).toInt()
        : 0;
    return QJsonObject{{QStringLiteral("ok"),           true},
                       {QStringLiteral("seriesId"),     m_pendingWesternSeriesId},
                       {QStringLiteral("seriesTitle"),  m_currentDetailSeriesTitle},
                       {QStringLiteral("editionCount"), editionCount}};
}

QJsonObject ComicsPage::devDownloadWesternEdition(int volumeNumber)
{
    if (m_pendingWesternSeriesId.isEmpty()) {
        return QJsonObject{{QStringLiteral("ok"),    false},
                           {QStringLiteral("error"), QStringLiteral("no western series open")}};
    }
    if (volumeNumber <= 0) {
        return QJsonObject{{QStringLiteral("ok"),    false},
                           {QStringLiteral("error"), QStringLiteral("volumeNumber must be positive")}};
    }
    if (!m_tyVolumeSeriesView) {
        return QJsonObject{{QStringLiteral("ok"),    false},
                           {QStringLiteral("error"), QStringLiteral("series view not ready")}};
    }
    // Codex review: don't report ok:true unless we actually trigger a real
    // Western download. Verify (a) the currently-shown series IS the Western one
    // (not a manga series with a stale m_pendingWesternSeriesId), and (b) the
    // requested edition exists in it — else populateSourcesForVolume would either
    // do nothing or hit the manga path.
    if (!m_detailEnteredFromWestern) {
        return QJsonObject{{QStringLiteral("ok"),    false},
                           {QStringLiteral("error"), QStringLiteral("current series is not Western")}};
    }
    const QJsonArray vols = devSeriesSnapshot()
                                .value(QStringLiteral("series")).toObject()
                                .value(QStringLiteral("volumes")).toArray();
    bool editionExists = false;
    for (const auto& v : vols) {
        if (v.toObject().value(QStringLiteral("volume")).toInt(-1) == volumeNumber) {
            editionExists = true;
            break;
        }
    }
    if (!editionExists) {
        return QJsonObject{{QStringLiteral("ok"),    false},
                           {QStringLiteral("error"),
                            QStringLiteral("edition %1 not found in current series").arg(volumeNumber)}};
    }
    // populateSourcesForVolume on an rco catalog emits downloadWesternEditionRequested,
    // which the ComicsPage lambda routes to m_westernDownloader->requestVolume — the
    // real download path. This mirrors the user clicking a volume row.
    m_tyVolumeSeriesView->populateSourcesForVolume(volumeNumber);
    return QJsonObject{{QStringLiteral("ok"),           true},
                       {QStringLiteral("seriesId"),     m_pendingWesternSeriesId},
                       {QStringLiteral("volumeNumber"), volumeNumber}};
}

QJsonObject ComicsPage::devWesternDownloadState(int volumeNumber) const
{
    // devSeriesSnapshot guards on m_mode == TankoyomiDetail; openWesternSeriesFromCatalog
    // sets that mode, so this works for an open Western series.
    QJsonObject series = devSeriesSnapshot();
    if (series.value(QStringLiteral("series")).isNull()) {
        return QJsonObject{{QStringLiteral("ok"),    false},
                           {QStringLiteral("error"), QStringLiteral("no series view active")}};
    }

    QJsonObject out;
    out[QStringLiteral("ok")]          = true;
    out[QStringLiteral("seriesId")]    = m_pendingWesternSeriesId;
    out[QStringLiteral("seriesTitle")] = m_currentDetailSeriesTitle;

    // Augment each volume row with the full VolumeTileState fields that
    // volumeRowJson omits (state enum, statusText, progressPct).
    QJsonArray volumes = series.value(QStringLiteral("series"))
                               .toObject()
                               .value(QStringLiteral("volumes"))
                               .toArray();
    if (m_tyVolumeSeriesView) {
        for (int i = 0; i < volumes.size(); ++i) {
            QJsonObject row = volumes.at(i).toObject();
            const int vol = row.value(QStringLiteral("volume")).toInt(-1);
            if (vol <= 0) { volumes.replace(i, row); continue; }
            const auto* tile = m_tyVolumeSeriesView->tileForVolume(vol);
            if (tile) {
                const auto st = tile->volumeState();
                row[QStringLiteral("tileState")]   = static_cast<int>(st.state);
                row[QStringLiteral("statusText")]  = st.statusText;
                row[QStringLiteral("progressPct")] = st.progressPct;
            }
            volumes.replace(i, row);
        }
    }

    // Optionally filter to a single volume when the caller passes one.
    if (volumeNumber > 0) {
        QJsonArray filtered;
        for (const QJsonValue& v : std::as_const(volumes)) {
            if (v.toObject().value(QStringLiteral("volume")).toInt(-1) == volumeNumber)
                filtered.append(v);
        }
        out[QStringLiteral("volumes")] = filtered;
    } else {
        out[QStringLiteral("volumes")] = volumes;
    }
    return out;
}

// -----------------------------------------------------------------------
// COMICS_MANGAFIRE_PIVOT Phase B.2 (2026-05-23).
// -----------------------------------------------------------------------
// Local-only catalog resolve. No network fallback chain: FallbackChainResolver,
// FandomVolumeResolver, WikipediaResolver were all removed. Local-first is the
// only resolution path for MangaFire-sourced catalogs.
//
// seriesId derivation: slug-from-title (lowercase ASCII + spaces→dashes
// + drop non-[a-z0-9-]). Matches the data/mangafire_catalog/*.json filename
// convention. The slugForAnilistId / slugForSeriesTitle index paths are more
// reliable for series with exact AniList id or exact title match.
// -----------------------------------------------------------------------

static QString fandomSeriesSlugFromTitle(const QString& title)
{
    QString out;
    out.reserve(title.size());
    bool lastWasDash = false;
    for (QChar ch : title) {
        const QChar lower = ch.toLower();
        if (lower.isLetterOrNumber()) {
            out.append(lower);
            lastWasDash = false;
        } else if (lower.isSpace() || lower == QChar('-') || lower == QChar('_')) {
            if (!lastWasDash && !out.isEmpty()) {
                out.append(QChar('-'));
                lastWasDash = true;
            }
        }
        // drop everything else (punctuation, symbols, non-Latin marks)
    }
    while (out.endsWith(QChar('-'))) out.chop(1);
    return out;
}

// Hook to trace dispatch entry — body unchanged otherwise.
void ComicsPage::dispatchCatalogResolve(const QString& seriesId,
                                        const QString& titleHint)
{
    if (seriesId.isEmpty()) {
        qInfo("ComicsPage::dispatchCatalogResolve: empty seriesId — skipping");
        return;
    }
    comicsOpenTrace(QStringLiteral("CP::dispatchCatalogResolve ENTRY seriesId=%1 titleHint=\"%2\"")
                        .arg(seriesId).arg(titleHint));
    m_pendingCatalogSeriesId   = seriesId;
    m_pendingCatalogTitleHint  = titleHint;
    qInfo("ComicsPage::dispatchCatalogResolve: seriesId=%s titleHint=%s",
          qUtf8Printable(seriesId),
          qUtf8Printable(titleHint));

    // Two lookup paths because callers arrive with different identities:
    //   (a) AniList-id path: library / bookmarked / AniList-cached series
    //       carry m_currentDetailAnilistId > 0.
    //   (b) Title path: WeebCentral search-result series open via
    //       showSeries(MangaResult) which does NOT set m_currentDetailAnilistId.
    QString slug;
    QString matchedBy;
    if (m_currentDetailAnilistId > 0) {
        slug = m_localCatalogIndex.slugForAnilistId(m_currentDetailAnilistId);
        if (!slug.isEmpty()) matchedBy = QStringLiteral("anilistId=%1").arg(m_currentDetailAnilistId);
    }
    if (slug.isEmpty() && !titleHint.isEmpty()) {
        slug = m_localCatalogIndex.slugForSeriesTitle(titleHint);
        if (!slug.isEmpty()) matchedBy = QStringLiteral("titleHint=\"%1\"").arg(titleHint);
    }
    // Final fallback: the seriesId itself may be the slug (catalog-tile path).
    if (slug.isEmpty()) {
        slug = m_localCatalogIndex.filePathForSlug(seriesId).isEmpty() ? QString() : seriesId;
        if (!slug.isEmpty()) matchedBy = QStringLiteral("directSlug");
    }

    if (!slug.isEmpty()) {
        const QString path = m_localCatalogIndex.filePathForSlug(slug);
        const auto local = tankoban::manga::LocalMangaCatalogLoader::loadFromFile(path);
        if (local.has_value()) {
            qInfo("ComicsPage::dispatchCatalogResolve: catalog hit (%s -> slug=%s)",
                  qUtf8Printable(matchedBy), qUtf8Printable(slug));
            m_tyVolumeSeriesView->populateVolumeRowsFromCatalog(*local);
            // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
            // Kick off async classification. Cache-hit resolves synchronously;
            // cache-miss fetches chapters then emits seriesClassified, which
            // re-renders with RAW tags + Volume X row.
            if (m_wcResolver) m_wcResolver->classifySeries(*local);
            return;
        }
        qInfo("ComicsPage::dispatchCatalogResolve: slug=%s matched but loadFromFile failed",
              qUtf8Printable(slug));
    } else {
        qInfo("ComicsPage::dispatchCatalogResolve: no local catalog entry for seriesId=%s titleHint=%s",
              qUtf8Printable(seriesId), qUtf8Printable(titleHint));
    }

    // COMICS_MANGAFIRE_ON_DEMAND_FETCH 2026-05-23 (Agent 1). No local hit
    // and no slug match — fire the live MangaFire scrape. The reply lands at
    // onMangaFireCatalogReady which refreshes m_localCatalogIndex and
    // re-runs dispatchCatalogResolve so the cached path renders the rows.
    // First-click latency: ~3–4s while the three HTTP calls fly. Every
    // subsequent open of the same series is instant.
    if (m_mangafireClient && !titleHint.isEmpty()) {
        qInfo("ComicsPage::dispatchCatalogResolve: firing on-demand MangaFire fetch for \"%s\"",
              qUtf8Printable(titleHint));
        m_mangafireClient->fetchByTitle(titleHint);
    }
}

// COMICS_MANGAFIRE_ON_DEMAND_FETCH 2026-05-23 (Agent 1).
// Hook for trace at MangaFire on-demand fetch arrival.
void ComicsPage::onMangaFireCatalogReady(
    const tankoban::manga::MangaCatalog& catalog, const QString& writtenPath)
{
    comicsOpenTrace(QStringLiteral("CP::onMangaFireCatalogReady ENTRY slug=%1 volumes=%2")
                        .arg(catalog.seriesId)
                        .arg(catalog.volumes.size()));
    qInfo("ComicsPage::onMangaFireCatalogReady: wrote %s (%lld volumes)",
          qUtf8Printable(writtenPath),
          static_cast<long long>(catalog.volumes.size()));

    // Refresh the in-memory index so the freshly-written JSON is discoverable.
    m_localCatalogIndex.refresh();

    // Stale-guard: if the user has already navigated away (different series in
    // flight, or back to library), don't clobber whatever they're now looking at.
    if (m_pendingCatalogSeriesId.isEmpty()) {
        qInfo("ComicsPage::onMangaFireCatalogReady: no pending dispatch — fetched but skipping render");
        return;
    }

    // The reverse-dispatch will succeed via direct-slug match because we just
    // wrote data/mangafire_catalog/<catalog.seriesId>.json. Use the freshly-
    // written seriesId so the slug matches exactly even if title-normalization
    // would have picked a different one.
    if (!m_tyVolumeSeriesView) return;
    const auto loaded = tankoban::manga::LocalMangaCatalogLoader::loadFromFile(writtenPath);
    if (!loaded.has_value()) {
        qWarning("ComicsPage::onMangaFireCatalogReady: just-written JSON failed to reload");
        return;
    }
    m_tyVolumeSeriesView->populateVolumeRowsFromCatalog(*loaded);
    // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
    if (m_wcResolver) m_wcResolver->classifySeries(*loaded);
}

void ComicsPage::onMangaFireCatalogFailed(const QString& title,
                                           const QString& reason)
{
    qWarning("ComicsPage::onMangaFireCatalogFailed: title=\"%s\" reason=%s",
             qUtf8Printable(title), qUtf8Printable(reason));
    // No UI surfacing — the series view already shows WeebCentral/AniList
    // content + a hero cover. The catalog rows just stay empty for series
    // MangaFire doesn't host (extremely rare given the 53K-series corpus).
}

void ComicsPage::onWcResolveRequested(const QString& mangaFireSeriesId,
                                      int volumeNumber)
{
    if (!m_wcResolver || volumeNumber <= 0) {
        return;
    }

    QString seriesId = mangaFireSeriesId.trimmed();
    if (seriesId.isEmpty() && !m_currentDetailSeriesTitle.isEmpty()) {
        seriesId = m_localCatalogIndex.slugForSeriesTitle(m_currentDetailSeriesTitle);
    }
    if (seriesId.isEmpty()) {
        qInfo("ComicsPage::onWcResolveRequested: no MangaFire seriesId for volume %d",
              volumeNumber);
        return;
    }

    const QString path = m_localCatalogIndex.filePathForSlug(seriesId);
    if (path.isEmpty()) {
        qInfo("ComicsPage::onWcResolveRequested: no catalog path for slug=%s volume=%d",
              qUtf8Printable(seriesId), volumeNumber);
        return;
    }

    const auto catalog = tankoban::manga::LocalMangaCatalogLoader::loadFromFile(path);
    if (!catalog.has_value() || !catalog->isValid()) {
        qWarning("ComicsPage::onWcResolveRequested: failed to load catalog %s",
                 qUtf8Printable(path));
        return;
    }

    tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key;
    key.seriesId = catalog->seriesId;
    key.volumeNumber = volumeNumber;
    key.requestSerial = ++m_wcResolveSerial;
    m_currentWcResolveKey = key;

    m_wcResolver->resolve(*catalog, volumeNumber, key);
}

void ComicsPage::onWcResolveRangeRequested(const QString& mangaFireSeriesId,
                                           int volumeNumber,
                                           int rangeStart,
                                           int rangeEnd)
{
    if (!m_wcResolver || rangeStart <= 0 || rangeEnd < rangeStart) {
        return;
    }

    QString seriesId = mangaFireSeriesId.trimmed();
    if (seriesId.isEmpty() && !m_currentDetailSeriesTitle.isEmpty()) {
        seriesId = m_localCatalogIndex.slugForSeriesTitle(m_currentDetailSeriesTitle);
    }
    if (seriesId.isEmpty()) {
        return;
    }

    const QString path = m_localCatalogIndex.filePathForSlug(seriesId);
    if (path.isEmpty()) {
        return;
    }

    const auto catalog = tankoban::manga::LocalMangaCatalogLoader::loadFromFile(path);
    if (!catalog.has_value() || !catalog->isValid()) {
        return;
    }

    tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key;
    key.seriesId = catalog->seriesId;
    key.volumeNumber = volumeNumber;
    key.requestSerial = ++m_wcResolveSerial;
    m_currentWcResolveKey = key;

    m_wcResolver->resolveChapterRange(*catalog, volumeNumber,
                                      rangeStart, rangeEnd, key);
}

void ComicsPage::onWcResolverViable(
    tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key,
    QStringList chapterIds)
{
    if (!(key == m_currentWcResolveKey)) {
        qInfo("ComicsPage::onWcResolverViable: dropped stale result slug=%s volume=%d serial=%llu",
              qUtf8Printable(key.seriesId),
              key.volumeNumber,
              static_cast<unsigned long long>(key.requestSerial));
        return;
    }
    if (!m_tyVolumeSeriesView || m_stack->currentWidget() != m_tyVolumeSeriesView) {
        return;
    }
    m_tyVolumeSeriesView->onWeebCentralViable(key.volumeNumber, chapterIds);
}

void ComicsPage::onWcResolverSkip(
    tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key,
    QString reasonCode)
{
    if (!(key == m_currentWcResolveKey)) {
        qInfo("ComicsPage::onWcResolverSkip: dropped stale result slug=%s volume=%d serial=%llu reason=%s",
              qUtf8Printable(key.seriesId),
              key.volumeNumber,
              static_cast<unsigned long long>(key.requestSerial),
              qUtf8Printable(reasonCode));
        return;
    }
    qInfo("ComicsPage::onWcResolverSkip: slug=%s volume=%d reason=%s",
          qUtf8Printable(key.seriesId),
          key.volumeNumber,
          qUtf8Printable(reasonCode));
}

// COMICS_WC_LIBRARY_ENRICH 2026-05-24 (Agent 1). Allocates a fresh reqId,
// stashes the title + addBookmark flag, fires AniList searchByTitle. The
// persistent connects in the constructor (filtered by reqId) land the result
// — on match they seed the cache + re-show the series; addBookmark=true
// ALSO commits a bookmark (Add-to-Library path); addBookmark=false leaves
// the library untouched (auto-enrichment on series-open path).
void ComicsPage::onAddToLibraryByTitleRequested(const QString& title)
{
    if (!m_anilistClient || !m_anilistCache || title.trimmed().isEmpty()) {
        if (m_tyVolumeSeriesView) m_tyVolumeSeriesView->refreshLibraryButton();
        return;
    }
    const int reqId = ++m_nextLibraryEnrichReqId;
    m_pendingLibraryEnrichReqId = reqId;
    m_pendingLibraryEnrichTitle = title.trimmed();
    m_pendingLibraryEnrichAddBookmark = true;
    qInfo("ComicsPage::onAddToLibraryByTitleRequested: searching AniList for \"%s\" reqId=%d (with bookmark)",
          qUtf8Printable(m_pendingLibraryEnrichTitle), reqId);
    m_anilistClient->searchByTitle(m_pendingLibraryEnrichTitle, reqId);
}

// COMICS_WC_AUTOENRICH 2026-05-24 (Agent 1). Auto-fired on every
// showSeries(MangaResult) where anilistId is 0. Same search + cache-seed +
// re-show pattern as the Add-to-Library path, but the addBookmark flag is
// false so the library isn't touched — purely a data-enrichment so the
// hero block paints. Coalesces against any already-pending request: if a
// search is already in flight for any title, this is a no-op (the prior
// request's result will land and trigger the re-show).
void ComicsPage::onEnrichSeriesByTitleRequested(const QString& title)
{
    comicsOpenTrace(QStringLiteral("CP::onEnrichSeriesByTitleRequested ENTRY title=\"%1\"").arg(title));
    if (!m_anilistClient || !m_anilistCache || title.trimmed().isEmpty()) return;
    if (m_pendingLibraryEnrichReqId != 0) {
        qInfo("ComicsPage::onEnrichSeriesByTitleRequested: skipping \"%s\" (request %d already in flight)",
              qUtf8Printable(title), m_pendingLibraryEnrichReqId);
        return;
    }
    const int reqId = ++m_nextLibraryEnrichReqId;
    m_pendingLibraryEnrichReqId = reqId;
    m_pendingLibraryEnrichTitle = title.trimmed();
    m_pendingLibraryEnrichAddBookmark = false;
    qInfo("ComicsPage::onEnrichSeriesByTitleRequested: searching AniList for \"%s\" reqId=%d (enrich-only)",
          qUtf8Printable(m_pendingLibraryEnrichTitle), reqId);
    m_anilistClient->searchByTitle(m_pendingLibraryEnrichTitle, reqId);
}

void ComicsPage::onForceRefreshRequested()
{
    if (m_pendingCatalogSeriesId.isEmpty()) {
        qInfo("ComicsPage::onForceRefreshRequested: no series in flight — skip");
        return;
    }
    // Re-scan the index (picks up any newly-dropped JSON) then re-resolve.
    m_localCatalogIndex.refresh();
    qInfo("ComicsPage::onForceRefreshRequested: re-resolving %s",
          qUtf8Printable(m_pendingCatalogSeriesId));
    dispatchCatalogResolve(m_pendingCatalogSeriesId, m_currentDetailSeriesTitle);
}
