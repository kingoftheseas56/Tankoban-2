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
#include "core/manga/MangaTransferCoordinator.h"
#include "core/manga/MangaResult.h"
#include "core/manga/MangaScraper.h"
#include "core/manga/MangaSourceRegistry.h"
#include "core/manga/NyaaRuntimeSource.h"
#include "core/manga/WeebCentralVolumePacker.h"
#include "core/manga/anilist/AniListCache.h"
#include "core/manga/anilist/AniListClient.h"
#include "core/manga/anilist/AniListTypes.h"
#include "core/torrent/TorrentClient.h"
#include "core/torrent/TorrentEngine.h"
#include "comics/ComicsTankoyomiSearchWidget.h"
#include "comics/ComicsSeriesView.h"
#include "comics/ComicsSourcesPanel.h"

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
#include <QCoreApplication>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QDebug>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QMetaObject>
#include <QSettings>
#include <QInputDialog>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QJsonObject>
#include <QFile>
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
} // namespace

ComicsPage::ComicsPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setObjectName("comics");
    qRegisterMetaType<SeriesInfo>("SeriesInfo");
    qRegisterMetaType<QList<SeriesInfo>>("QList<SeriesInfo>");

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
    m_nam = new QNetworkAccessManager(this);
    m_sourceRegistry = new MangaSourceRegistry(m_nam, this);

    // TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- new providers. AniList
    // client + cache (Phases 1+2) drive the search widget and the new
    // series-detail view; NyaaRuntimeSource (Phase 4) feeds runtime torrent
    // search rows into ComicsSourcesPanel; WeebCentralVolumePacker (Phase 5)
    // synthesizes off-catalog volume packs from WeebCentral chapter fetches.
    m_anilistClient = new tankoban::manga::anilist::AniListClient(m_nam, this);
    m_anilistCache  = new tankoban::manga::anilist::AniListCache(
                          m_bridge->dataDir() + QStringLiteral("/anilist_cache"), this);
    const QString trustJsonPath = QCoreApplication::applicationDirPath()
                                + QStringLiteral("/resources/manga_uploader_trust.json");
    m_nyaaRuntime = new tankoban::manga::NyaaRuntimeSource(m_nam, trustJsonPath, this);

    // Phase 9 search widget: AniList-backed single-strip variant.
    m_searchTakeover = new ComicsTankoyomiSearchWidget(m_anilistClient, m_nam, this);
    m_stack->addWidget(m_searchTakeover);

    connect(m_searchTakeover, &ComicsTankoyomiSearchWidget::backRequested,
            this, &ComicsPage::showLibraryMode);
    connect(m_searchTakeover, &ComicsTankoyomiSearchWidget::seriesActivated,
            this, &ComicsPage::onSearchResultActivated);

    // Scraper error toasts retained -- downloads in flight still route
    // through MangaDownloader for legacy chapter pulls.
    for (auto* s : m_sourceRegistry->scrapers()) {
        connect(s, &MangaScraper::errorOccurred, this, [this, s](const QString& msg) {
            Q_UNUSED(msg);
            QWidget* anchor = window() ? window() : this;
            Toast::show(anchor, QStringLiteral("%1 didn't respond").arg(s->sourceName()));
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
            this, [this](const QString&) { refreshTileChips(); });
    connect(m_mangaDownloader, &MangaDownloader::downloadCompleted,
            this, [this](const QString&) { refreshTileChips(); });
    connect(m_mangaDownloader, &MangaDownloader::chapterCompleted,
            this, &ComicsPage::onChapterCompleted);

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
    // thread::finished. No manual delete.
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
    m_searchBar = new QLineEdit(gridPage);
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 19 \u2014 repurposed from
    // local library filter to Tankoyomi-search entry point. Press Enter
    // to fan-out across scrapers and flip into the search-takeover view.
    m_searchBar->setPlaceholderText("Search for Comics & Manga");
    m_searchBar->setClearButtonEnabled(true);
    m_searchBar->setObjectName("LibrarySearch");
    m_searchBar->setFixedHeight(36);
    m_searchBar->setStyleSheet(
        "QLineEdit#LibrarySearch { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px; color: #eee; padding: 4px 10px; font-size: 13px; }"
        "QLineEdit#LibrarySearch:focus { border: 1px solid rgba(255,255,255,0.3); }");
    auto* searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0, 12, 0, 0);
    searchLayout->addWidget(m_searchBar);
    gridLayout->addLayout(searchLayout);

    m_searchBar->setToolTip("Press Enter to search Tankoyomi sources");

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 19 — search bar
    // repurpose. textChanged still updates the activeSearch style hint
    // so the input still highlights while typing, but the debounce timer
    // no longer drives applySearch (that hook is preserved for the
    // GLOBAL_NAV_HISTORY restore + hide-series flows). Enter flips into
    // the search-takeover surface via showSearchMode().
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(250);
    connect(m_searchBar, &QLineEdit::textChanged, this, [this]() {
        m_searchBar->setProperty("activeSearch", !m_searchBar->text().trimmed().isEmpty());
        m_searchBar->style()->unpolish(m_searchBar);
        m_searchBar->style()->polish(m_searchBar);
    });
    connect(m_searchBar, &QLineEdit::returnPressed, this, [this]() {
        const QString q = m_searchBar->text().trimmed();
        if (q.isEmpty()) return;
        showSearchMode(q);
    });

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

        QString filePath    = card->property("filePath").toString();
        QString seriesPath  = card->property("seriesPath").toString();
        QString seriesName  = card->property("seriesName").toString();

        // Compute progress key for this file
        QString progKey = QString(QCryptographicHash::hash(
            filePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));

        // Check finished state for toggle label
        bool isFinished = false;
        if (m_bridge) {
            QJsonObject prog = m_bridge->progress("comics", progKey);
            isFinished = prog.value("finished").toBool();
        }

        auto* menu = ContextMenuHelper::createMenu(this);

        // 1. Continue reading
        auto* continueAct = menu->addAction("Continue reading");

        // 2. Open series (visible only if seriesPath exists)
        QAction* openSeriesAct = nullptr;
        if (!seriesPath.isEmpty()) {
            openSeriesAct = menu->addAction("Open series");
        }

        menu->addSeparator();

        // 3. Mark as unread / Mark as read
        auto* markAct = menu->addAction(isFinished ? "Mark as unread" : "Mark as read");

        // 4. Clear from Continue Reading
        auto* clearAct = menu->addAction("Clear from Continue Reading");

        menu->addSeparator();

        // 5. Reveal in File Explorer
        auto* revealAct = menu->addAction("Reveal in File Explorer");
        revealAct->setEnabled(!filePath.isEmpty());

        // 6. Copy path
        auto* copyAct = menu->addAction("Copy path");
        copyAct->setEnabled(!filePath.isEmpty());

        menu->addSeparator();

        // 7. Remove from library... (DANGER)
        auto* removeAct = ContextMenuHelper::addDangerAction(menu, "Remove from library...");
        removeAct->setEnabled(!seriesPath.isEmpty());

        auto* chosen = menu->exec(m_continueStrip->mapToGlobal(pos));
        if (chosen == continueAct) {
            // Open the comic file directly
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
            emit openComic(filePath, cbzList, seriesName);
        } else if (openSeriesAct && chosen == openSeriesAct) {
            openSeriesByPath(seriesPath, seriesName,
                             card->property("coverPath").toString());
        } else if (chosen == markAct && m_bridge) {
            QJsonObject prog = m_bridge->progress("comics", progKey);
            prog["finished"] = !isFinished;
            m_bridge->saveProgress("comics", progKey, prog);
            refreshContinueStrip();
        } else if (chosen == clearAct && m_bridge) {
            m_bridge->clearProgress("comics", progKey);
            refreshContinueStrip();
        } else if (chosen == revealAct) {
            ContextMenuHelper::revealInExplorer(filePath);
        } else if (chosen == copyAct) {
            ContextMenuHelper::copyToClipboard(filePath);
        } else if (chosen == removeAct) {
            if (ContextMenuHelper::confirmRemove(this, "Remove from library",
                    "Remove this series from the library?\n" + seriesPath +
                    "\nFiles will not be deleted from disk.")) {
                triggerScan();
            }
        }
        menu->deleteLater();
    });

    // ── 3. "DOWNLOADED" header row: label + sort + density ──
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- section relabel from
    // "SERIES" (folder-import + Tankoyomi-library merge) to "DOWNLOADED"
    // (one tile per series in MangaDownloadIndex::entriesForAllSeries).
    auto* seriesRow = new QWidget(gridPage);
    auto* seriesLayout = new QHBoxLayout(seriesRow);
    seriesLayout->setContentsMargins(0, 0, 0, 0);
    seriesLayout->setSpacing(8);

    m_downloadedLabel = new QLabel("DOWNLOADED", seriesRow);
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

    m_statusLabel = new QLabel("Add a comics folder to get started", gridPage);
    m_statusLabel->setObjectName("TileSubtitle");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 60px;");
    gridLayout->addWidget(m_statusLabel);

    m_tileStrip = new TileStrip(gridPage);
    m_tileStrip->hide();
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
        // legacy text "Add a comics folder to get started" assumed folder-
        // import was the primary content path. With DOWNLOADED + BOOKMARKED
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

    if (sourceId == QLatin1String("tankoyomi_premium") && m_premiumCatalog) {
        if (auto entry = m_premiumCatalog->entryById(seriesId)) {
            return entry->anilistId;
        }
    }

    if (seriesId.startsWith(QLatin1String("anilist_"))) {
        bool ok = false;
        const int n = seriesId.mid(QStringLiteral("anilist_").size()).toInt(&ok);
        if (ok && n > 0) return n;
    }

    return 0;
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
            sourceId = QString::fromLatin1(WEEBCENTRAL_PACKER_SOURCE_ID);
            break;
    }

    if (anilistId <= 0) {
        anilistId = anilistIdForDownloadEntry(sourceId, seriesId);
    }

    if (m_mangaDownloadIndex &&
        (kind == PendingVolumeSourceKind::NyaaRuntime ||
         kind == PendingVolumeSourceKind::WeebCentralPacker)) {
        m_mangaDownloadIndex->registerVolume(sourceId, seriesId, volumeNumber, cbzPath,
                                             QFileInfo(cbzPath).size(), chapterIds);
    }

    if (m_tyVolumeSeriesView && anilistId > 0 &&
        m_tyVolumeSeriesView->currentAnilistId() == anilistId) {
        m_tyVolumeSeriesView->setVolumeDownloadState(volumeNumber, cbzPath, true);
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

    if (m_tyVolumeSeriesView && anilistId > 0 &&
        m_tyVolumeSeriesView->currentAnilistId() == anilistId) {
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
    // Mirrors ComicsTankoyomiSearchWidget's NAM-direct path: cache to
    // <writableData>/Tankoban/data/anilist_posters/anilist_<id>.jpg; reuse
    // an existing cached file when present (no network round-trip on
    // re-entry). Failures stay silent (placeholder remains).
    if (!card || anilistId <= 0) return;

    const QString posterCacheDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/Tankoban/data/anilist_posters");
    QDir().mkpath(posterCacheDir);
    const QString outPath = posterCacheDir + QStringLiteral("/anilist_%1.jpg")
                                                 .arg(anilistId);

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
    // against m_inNavRestore so restoreNavState's tankoyomiDetail branch
    // (lines 2341-2342) does not re-emit during back-restore replay.
    if (!m_inNavRestore) {
        QJsonObject blob;
        blob[QStringLiteral("anilistId")]   = anilistId;
        blob[QStringLiteral("seriesTitle")] = preview.title;
        blob[QStringLiteral("enteredFrom")] = QStringLiteral("library");
        emit navigationRequested();  // Phase 0 legacy -- kept until Task 12
        emit enteredLayer(makeComicsLayer(QStringLiteral("seriesView"), preview.title, blob));
    }
    m_enteredDetailFrom        = Mode::Library;
    m_mode                     = Mode::TankoyomiDetail;
    m_currentDetailAnilistId   = anilistId;
    m_currentDetailSeriesTitle = preview.title;
    m_tyVolumeSeriesView->showSeries(preview);
    m_stack->setCurrentWidget(m_tyVolumeSeriesView);
}

void ComicsPage::refreshLibraryStrips()
{
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- rebuild DOWNLOADED +
    // BOOKMARKED sections from MangaDownloadIndex + AniListCache. Section
    // header visibility is toggled per data: an empty downloaded set hides
    // the m_downloadedLabel + sort/density row indirectly via m_tileStrip
    // hide; an empty bookmarked set hides m_bookmarkedSection.
    if (!m_tileStrip || !m_bookmarkedStrip) return;

    m_tileStrip->clear();
    m_bookmarkedStrip->clear();
    m_listView->clear();

    // ── DOWNLOADED ──
    QSet<int> downloadedAnilistIds;
    bool hasAnyDownloadedEntry = false;
    if (m_mangaDownloadIndex) {
        const auto entries = m_mangaDownloadIndex->entriesForAllSeries();
        hasAnyDownloadedEntry = !entries.isEmpty();
        for (const auto& e : entries) {
            const int anilistId =
                anilistIdForDownloadEntry(e.sourceId, e.seriesId);

            // Resolve display title + cover. Prefer the AniList cache (so
            // a downloaded Premium series + its AniList metadata both land
            // on the same title); fall back to the catalog entry; fall
            // back to a humanised seriesId; cover falls back to series
            // record cover path if we have a Tankoyomi-library record for
            // this seriesId.
            QString displayTitle;
            QString coverUrl;
            QString coverPath;
            if (anilistId > 0 && m_anilistCache) {
                if (auto detailOpt = m_anilistCache->get(anilistId)) {
                    displayTitle = detailOpt->preview.title;
                    coverUrl     = detailOpt->preview.coverThumbUrl;
                }
            }
            if (displayTitle.isEmpty() && m_premiumCatalog) {
                if (auto cat = m_premiumCatalog->entryById(e.seriesId)) {
                    displayTitle = cat->title;
                }
            }
            if (displayTitle.isEmpty()) {
                displayTitle = e.seriesId;
            }
            if (m_tyLibrary) {
                const auto rec = m_tyLibrary->get(e.sourceId, e.seriesId);
                if (!rec.coverPath.isEmpty() && QFile::exists(rec.coverPath)) {
                    coverPath = rec.coverPath;
                }
            }

            auto* card = new TileCard(coverPath, displayTitle, QStringLiteral("Downloaded"));
            card->setProvenance(QStringLiteral("tankoyomi"));
            card->setProperty("anilistId", anilistId);
            card->setProperty("seriesKey",
                              e.sourceId + QStringLiteral(":") + e.seriesId);
            card->setProperty("seriesName", displayTitle);
            connect(card, &TileCard::clicked, this,
                    [this, anilistId, displayTitle, e]() {
                if (anilistId > 0) {
                    openSeriesByAnilistId(anilistId, displayTitle);
                    return;
                }
                // No anilistId resolvable: legacy fallback -- if we have a
                // Tankoyomi-library record, open as a folder via the
                // SeriesView. Otherwise the tile is non-routable.
                if (m_tyLibrary) {
                    const auto rec = m_tyLibrary->get(e.sourceId, e.seriesId);
                    if (!rec.canonicalSeriesPath.isEmpty()) {
                        openSeriesByPath(rec.canonicalSeriesPath, rec.title,
                                         rec.coverPath);
                    }
                }
            });
            m_tileStrip->addTile(card);

            if (anilistId > 0) {
                downloadedAnilistIds.insert(anilistId);
                fetchPosterForTile(card, anilistId, coverUrl);
            }
        }
    }

    // Reuse the entries-walk result from above instead of re-locking and re-walking
    // MangaDownloadIndex.
    const bool hasDownloaded = !downloadedAnilistIds.isEmpty() || hasAnyDownloadedEntry;

    // ── BOOKMARKED ──
    int bookmarkedCount = 0;
    if (m_anilistCache) {
        const auto previews = m_anilistCache->bookmarkedPreviews();
        for (const auto& p : previews) {
            // Suppress duplicate tile when a bookmarked series is also
            // downloaded (it already appears in the DOWNLOADED section).
            if (downloadedAnilistIds.contains(p.anilistId)) continue;

            auto* card = new TileCard(QString(), p.title,
                                       QStringLiteral("Bookmarked"));
            card->setProperty("anilistId", p.anilistId);
            card->setProperty("seriesName", p.title);
            const int anilistId = p.anilistId;
            const QString title = p.title;
            connect(card, &TileCard::clicked, this,
                    [this, anilistId, title]() {
                openSeriesByAnilistId(anilistId, title);
            });
            m_bookmarkedStrip->addTile(card);
            fetchPosterForTile(card, p.anilistId, p.coverThumbUrl);
            ++bookmarkedCount;
        }
    }

    // ── Section visibility ──
    if (hasDownloaded) {
        m_tileStrip->show();
        if (m_downloadedLabel) m_downloadedLabel->show();
        m_tileStrip->sortTiles(m_sortCombo->currentData().toString());
    } else {
        m_tileStrip->hide();
        if (m_downloadedLabel) m_downloadedLabel->hide();
    }
    if (bookmarkedCount > 0) {
        if (m_bookmarkedSection) m_bookmarkedSection->show();
    } else {
        if (m_bookmarkedSection) m_bookmarkedSection->hide();
    }

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
        emit navigationRequested();  // Phase 0 legacy -- kept until Task 12
        emit enteredLayer(makeComicsLayer(QStringLiteral("folderSeriesView"), seriesName, blob));
    }
    m_seriesView->showSeries(seriesPath, seriesName, coverPath);
    m_stack->setCurrentIndexAnimated(1);
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
    // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — emit before the
    // in-page transition so MainWindow's NavHistory records the Library
    // landing as its own entry whenever we arrive from a deeper layer
    // (search results, tankoyomi detail). Suppressed when (a) already on
    // Library (no-op transition, no history pollution) or (b) called from
    // restoreNavState via the m_inNavRestore guard.
    if (!m_inNavRestore && m_mode != Mode::Library) {
        QJsonObject blob;
        if (m_sortCombo) blob[QStringLiteral("sort")] = m_sortCombo->currentData().toString();
        if (m_gridScroll) {
            if (auto* vsb = m_gridScroll->verticalScrollBar())
                blob[QStringLiteral("scrollY")] = vsb->value();
        }
        emit navigationRequested();  // Phase 0 legacy -- kept until Task 12
        emit enteredLayer(makeComicsLayer(QStringLiteral("library"), QStringLiteral("Library"), blob));
    }
    m_mode = Mode::Library;
    m_stack->setCurrentIndexAnimated(0);
    if (m_searchTakeover) m_searchTakeover->clearResults();
}

void ComicsPage::showSearchMode(const QString& query)
{
    // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — emit before the
    // in-page transition so the SearchResults layer is recorded in
    // NavHistory. Guarded against same-mode re-entry (typing a new query
    // while already in search results is a refinement, not a navigation)
    // and the restoreNavState replay path.
    if (!m_inNavRestore && m_mode != Mode::SearchResults) {
        QJsonObject blob;
        blob[QStringLiteral("query")] = query;
        emit navigationRequested();  // Phase 0 legacy -- kept until Task 12
        emit enteredLayer(makeComicsLayer(QStringLiteral("searchResults"), QStringLiteral("Search Results"), blob));
    }
    m_mode = Mode::SearchResults;
    m_searchTakeover->search(query);
    m_stack->setCurrentWidget(m_searchTakeover);
}

void ComicsPage::onSearchResultActivated(const tankoban::manga::anilist::MediaPreview& preview)
{
    // TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- search-result click
    // routes into the new ComicsSeriesView. Origin is recorded so Back
    // returns to the search-takeover (Phase 9 Task 52 carry-over).
    //
    // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — emit BEFORE the
    // in-page state change so NavHistory captures the SearchResults state
    // (mode=searchResults + query) into the current entry and pushes a
    // fresh tankoyomiDetail entry for the target.
    if (!m_inNavRestore) {
        QJsonObject blob;
        blob[QStringLiteral("anilistId")]   = preview.anilistId;
        blob[QStringLiteral("seriesTitle")] = preview.title;
        blob[QStringLiteral("enteredFrom")] = QStringLiteral("search");
        emit navigationRequested();  // Phase 0 legacy -- kept until Task 12
        emit enteredLayer(makeComicsLayer(QStringLiteral("seriesView"), preview.title, blob));
    }
    m_enteredDetailFrom = Mode::SearchResults;
    m_mode = Mode::TankoyomiDetail;
    m_currentDetailAnilistId    = preview.anilistId;
    m_currentDetailSeriesTitle  = preview.title;
    if (m_tyVolumeSeriesView) {
        m_tyVolumeSeriesView->showSeries(preview);
        m_stack->setCurrentWidget(m_tyVolumeSeriesView);
    }
}

void ComicsPage::onDetailBack()
{
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 9 Task 52 -- Back
    // from detail routes by the origin recorded at entry. Search->Detail->
    // Back lands on the search-takeover; Library->Detail->Back lands on
    // the merged library grid.
    //
    // TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- the new
    // ComicsSeriesView does not currently emit a Back signal (it has no
    // back button in its scoped stub per Phase 7 comments). This slot
    // remains the central exit point and is invoked by Escape-shortcut +
    // future deep-link recovery paths. Direct invocation lands here.
    //
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- emit exitedLayer so the
    // controller pops the detail entry from its stack, keeping the
    // back-stack consistent with the in-page state machine. Emitted
    // before the mode flip so the controller state matches what is about
    // to become visible.
    if (!m_inNavRestore) emit exitedLayer();
    if (m_enteredDetailFrom == Mode::SearchResults && m_searchTakeover) {
        // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — the
        // Detail->Search hop bypasses showSearchMode (no re-search needed),
        // so the navigationRequested emit lives directly on this branch.
        // The Detail->Library branch below funnels through showLibraryMode
        // which emits on its own.
        if (!m_inNavRestore) emit navigationRequested();
        m_mode = Mode::SearchResults;
        m_stack->setCurrentWidget(m_searchTakeover);
    } else {
        showLibraryMode();
    }
    m_currentDetailAnilistId   = 0;
    m_currentDetailSeriesTitle.clear();
    if (m_tyVolumeSeriesView) m_tyVolumeSeriesView->clearView();
    m_enteredDetailFrom = Mode::Library;
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
        if (chapterIds.isEmpty()) {
            qDebug().noquote()
                << "[Phase9 dispatch] WC row but no chapter ids supplied";
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
        const QString destinationPath = seriesDir + QStringLiteral("/Volume %1.cbz")
                                                        .arg(volumeNumber, 2, 10, QChar('0'));

        VolumePackRequest req;
        req.seriesId        = fallbackSeriesId;
        req.volumeNumber    = volumeNumber;
        req.destinationPath = destinationPath;
        req.chapterIds      = chapterIds;
        rememberPendingVolumeDispatch(req.seriesId, volumeNumber,
                                      PendingVolumeSourceKind::WeebCentralPacker,
                                      anilistSeriesId, chapterIds);
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
            if (volMatch.hasMatch()) {
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

        items.append({updatedAt, preferredFilePath, ref->seriesPath, title, subtitle, ref->coverPath});
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
        auto* card = new TileCard(item.coverPath, item.title, item.subtitle);
        card->setProperty("filePath", item.filePath);
        card->setProperty("seriesPath", item.seriesPath);
        card->setProperty("seriesName", ScannerUtils::cleanMediaFolderTitle(
            QDir(item.seriesPath).dirName()));
        card->setProperty("coverPath", item.coverPath);
        if (m_tyLibrary && m_tyLibrary->getByCanonicalPath(item.seriesPath))
            card->setProvenance(QStringLiteral("tankoyomi"));
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

    QString seriesPath = card->property("seriesPath").toString();
    QString seriesName = card->property("seriesName").toString();
    QString coverPath = card->property("coverPath").toString();

    // Check if all volumes in series are finished (for toggle label)
    QDir dir(seriesPath);
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

    auto* menu = ContextMenuHelper::createMenu(this);
    auto* openAct = menu->addAction("Open");
    menu->addSeparator();
    auto* markAct = menu->addAction(allFinished ? "Mark all as unread" : "Mark all as read");
    menu->addSeparator();
    auto* renameAct = menu->addAction("Rename series...");
    auto* hideAct = menu->addAction("Hide series");
    auto* revealAct = menu->addAction("Reveal in File Explorer");
    revealAct->setEnabled(!seriesPath.isEmpty());
    auto* copyAct = menu->addAction("Copy path");
    copyAct->setEnabled(!seriesPath.isEmpty());
    menu->addSeparator();
    auto* removeAct = ContextMenuHelper::addDangerAction(menu, "Remove from library...");
    removeAct->setEnabled(!seriesPath.isEmpty());

    auto* chosen = menu->exec(m_tileStrip->mapToGlobal(pos));
    if (chosen == openAct) {
        openSeriesByPath(seriesPath, seriesName, coverPath);
    } else if (chosen == markAct) {
        bool setFinished = !allFinished;
        for (const auto& f : cbzFiles) {
            QString id = QString(QCryptographicHash::hash(
                dir.absoluteFilePath(f).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            QJsonObject prog = m_bridge->progress("comics", id);
            prog["finished"] = setFinished;
            m_bridge->saveProgress("comics", id, prog);
        }
    } else if (chosen == renameAct) {
        QString dirName = QDir(seriesPath).dirName();
        QString newName = QInputDialog::getText(this, "Rename series", "New name:", QLineEdit::Normal, dirName);
        if (!newName.isEmpty() && newName != dirName) {
            QString parentPath = QFileInfo(seriesPath).absolutePath();
            QString oldPath = parentPath + "/" + dirName;
            QString newPath = parentPath + "/" + newName.trimmed();
            if (QFile::rename(oldPath, newPath)) {
                triggerScan();
            } else {
                QMessageBox::warning(this, "Rename failed",
                    "Could not rename \"" + dirName + "\".\n"
                    "The folder may be in use by another program.");
            }
        }
    } else if (chosen == hideAct) {
        QSettings settings("Tankoban", "Tankoban");
        QStringList hidden = settings.value("comics_hidden_series").toStringList();
        if (!hidden.contains(seriesPath)) {
            hidden.append(seriesPath);
            settings.setValue("comics_hidden_series", hidden);
        }
        card->hide();
        m_tileStrip->filterTiles(m_searchBar->text());
    } else if (chosen == revealAct) {
        ContextMenuHelper::revealInExplorer(seriesPath);
    } else if (chosen == copyAct) {
        ContextMenuHelper::copyToClipboard(seriesPath);
    } else if (chosen == removeAct) {
        if (ContextMenuHelper::confirmRemove(this, "Remove from library",
                "Remove this series from the library?\n" + seriesPath +
                "\nFiles will not be deleted from disk.")) {
            triggerScan();
        }
    }
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

// ── INavStateProvider (GLOBAL_NAV_HISTORY Task 8) ─────────────────────────

QJsonObject ComicsPage::captureNavState() const
{
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 9 Task 50 —
    // 3-mode discriminator. Replaces Agent 5's GLOBAL_NAV_HISTORY Task 8
    // library-only capture so Back/Forward navigate correctly into
    // search-results and tankoyomi-detail surfaces too.
    QJsonObject blob;
    blob["mode"] = (m_mode == Mode::Library          ? "library" :
                    m_mode == Mode::SearchResults    ? "searchResults" :
                                                       "tankoyomiDetail");

    if (m_mode == Mode::Library) {
        // Preserve sort (Agent 5 Phase 8 ship) — independent of search-bar
        // text. Brief CRITICAL fix #1: plan template dropped this on
        // Back/Forward; we keep it for library-mode parity.
        if (m_sortCombo)
            blob["sort"] = m_sortCombo->currentData().toString();
        if (m_gridScroll) {
            if (auto* vsb = m_gridScroll->verticalScrollBar())
                blob["scrollY"] = vsb->value();
        }
        return blob;
    }

    if (m_mode == Mode::SearchResults) {
        if (m_searchBar)
            blob["query"] = m_searchBar->text();
        return blob;
    }

    if (m_mode == Mode::TankoyomiDetail && m_tyVolumeSeriesView) {
        // TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- snapshot the
        // currently-shown anilistId + title. ComicsSeriesView doesn't
        // expose a currentPreview() accessor in its Phase 7 stub, so we
        // mirror the values at onSearchResultActivated time. Restore can
        // rehydrate from anilistId via showSeries with a synthesized
        // minimal MediaPreview (title only -- AniList background fetch
        // will fill the rest).
        blob["anilistId"]   = m_currentDetailAnilistId;
        blob["seriesTitle"] = m_currentDetailSeriesTitle;
        blob["enteredFrom"] = (m_enteredDetailFrom == Mode::SearchResults
                                ? "search" : "library");
        return blob;
    }

    return blob;
}

bool ComicsPage::restoreNavState(const QJsonObject& blob)
{
    // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — raise the
    // m_inNavRestore guard for the duration of the restore. Every
    // showLibraryMode / showSearchMode / direct mode flip below is a
    // replay of a previously-recorded transition, not a fresh navigation,
    // and must NOT re-emit navigationRequested (which would push a
    // duplicate entry through MainWindow's recordNavEvent connection).
    // QScopedValueRollback resets the flag on every return path.
    QScopedValueRollback<bool> rollback(m_inNavRestore, true);

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 9 Task 51 —
    // 3-mode router. Brief CRITICAL fix #3: old blobs persisted by
    // Agent 5's Phase 8 ship have NO "mode" field (just search/sort/
    // scrollY). Treat empty-mode as "library" so existing NavHistory
    // entries keep working after the merger ships.
    QString mode = blob.value("mode").toString();
    if (mode.isEmpty()) mode = "library";

    if (mode == "library") {
        showLibraryMode();
        if (m_sortCombo) {
            const QString sort = blob.value("sort").toString();
            if (!sort.isEmpty()) {
                for (int i = 0; i < m_sortCombo->count(); ++i) {
                    if (m_sortCombo->itemData(i).toString() == sort) {
                        m_sortCombo->setCurrentIndex(i);
                        break;
                    }
                }
            }
        }
        if (m_gridScroll) {
            if (auto* vsb = m_gridScroll->verticalScrollBar())
                vsb->setValue(blob.value("scrollY").toInt(0));
        }
        return true;
    }

    if (mode == "searchResults") {
        const QString q = blob.value("query").toString();
        // Code-quality review I1: NO blockSignals around setText. The only
        // textChanged subscriber is the activeSearch QSS style toggle (see
        // search-bar ctor wire), which must reflect "bar has text" after a
        // Back-restore. Blocking it leaves the activeSearch chip stale
        // until the next keystroke.
        if (m_searchBar) m_searchBar->setText(q);
        showSearchMode(q);
        return true;
    }

    if (mode == "tankoyomiDetail" && m_tyVolumeSeriesView) {
        // TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- restore from
        // anilistId. The fields differ from the legacy (sourceId, seriesId)
        // shape; old nav-state blobs persisted before Phase 9 will not
        // restore into the volume-pivot detail view (treated as a cache miss
        // and routed back to the prior mode). seriesTitle is the user-facing
        // hero text shown during the async detail fetch.
        const int anilistId = blob.value("anilistId").toInt(0);
        const QString seriesTitle = blob.value("seriesTitle").toString();
        if (anilistId > 0) {
            m_enteredDetailFrom = (blob.value("enteredFrom").toString() == "search"
                                    ? Mode::SearchResults : Mode::Library);
            m_mode = Mode::TankoyomiDetail;
            tankoban::manga::anilist::MediaPreview preview;
            preview.anilistId = anilistId;
            preview.title     = seriesTitle;
            m_currentDetailAnilistId   = anilistId;
            m_currentDetailSeriesTitle = seriesTitle;
            m_tyVolumeSeriesView->showSeries(preview);
            m_stack->setCurrentWidget(m_tyVolumeSeriesView);
            return true;
        }
        // Cache miss -- fall back inline to whichever mode entered originally.
        const QString enteredFrom = blob.value("enteredFrom").toString();
        if (enteredFrom == "search") {
            showSearchMode(QString());
            return true;
        }
        showLibraryMode();
        return true;
    }

    return false;
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
        if (m_gridScroll) {
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

    if (kind == QStringLiteral("seriesView") && m_tyVolumeSeriesView) {
        const int anilistId        = blob.value(QStringLiteral("anilistId")).toInt(0);
        const QString seriesTitle  = blob.value(QStringLiteral("seriesTitle")).toString();
        if (anilistId > 0) {
            m_enteredDetailFrom = (blob.value(QStringLiteral("enteredFrom")).toString() == QStringLiteral("search")
                                    ? Mode::SearchResults : Mode::Library);
            m_mode = Mode::TankoyomiDetail;
            tankoban::manga::anilist::MediaPreview preview;
            preview.anilistId = anilistId;
            preview.title     = seriesTitle;
            m_currentDetailAnilistId   = anilistId;
            m_currentDetailSeriesTitle = seriesTitle;
            m_tyVolumeSeriesView->showSeries(preview);
            m_stack->setCurrentWidget(m_tyVolumeSeriesView);
            return;
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
