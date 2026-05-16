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
#include "core/torrent/TorrentClient.h"
#include "core/torrent/TorrentEngine.h"
#include "comics/ComicsTankoyomiSearchWidget.h"
#include "comics/ComicsTankoyomiDetailView.h"

#include "ui/ContextMenuHelper.h"
#include "ui/readers/comic_progress_key.h"
#include "ui/widgets/FadingStackedWidget.h"
#include "ui/widgets/LibraryListView.h"
#include "ui/widgets/Toast.h"
#include <QNetworkAccessManager>
#include <QCoreApplication>
#include <QPushButton>
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

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 18+20 — search-takeover
    // widget + scraper registry. Lives at m_stack index 2 (post grid+series).
    // Each scraper's errorOccurred fires a Toast with the source's display
    // name; the search widget itself only updates its status line.
    m_nam = new QNetworkAccessManager(this);
    m_sourceRegistry = new MangaSourceRegistry(m_nam, this);
    m_searchTakeover = new ComicsTankoyomiSearchWidget(m_sourceRegistry, m_nam, this);
    m_stack->addWidget(m_searchTakeover);

    connect(m_searchTakeover, &ComicsTankoyomiSearchWidget::backRequested,
            this, &ComicsPage::showLibraryMode);
    connect(m_searchTakeover, &ComicsTankoyomiSearchWidget::seriesActivated,
            this, &ComicsPage::onSearchResultActivated);

    // Task 20: per-source error surfacing — Toast with the source's
    // display name. Anchor on the top-level window so the toast floats
    // over the whole app, not just our tab.
    for (auto* s : m_sourceRegistry->scrapers()) {
        connect(s, &MangaScraper::errorOccurred, this, [this, s](const QString& msg) {
            Q_UNUSED(msg);
            QWidget* anchor = window() ? window() : this;
            Toast::show(anchor, QStringLiteral("%1 didn't respond").arg(s->sourceName()));
        });
    }

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 26 — manga downloader +
    // Tankoyomi-origin detail view. Single downloader owned by this page;
    // the old TankoyomiPage duplicate was retired in Phase 8. Detail view
    // lives at m_stack index 3 (after grid + seriesView + searchTakeover).
    m_mangaDownloader = new MangaDownloader(&m_bridge->store(), this);
    for (auto* s : m_sourceRegistry->scrapers()) {
        m_mangaDownloader->setScraper(s->sourceId(), s);
    }

    // TANKOYOMI_PREMIUM Phase 1 -- bring the Premium catalog loader up at
    // construction time. It walks resources/manga_premium_catalogs/*.json,
    // runs strict per-field validation, and exposes a read-only lookup
    // surface. No UI consumes it yet; later phases drive search dedup +
    // detail-view badging off this.
    const QString catalogsDir = QCoreApplication::applicationDirPath()
                              + QStringLiteral("/resources/manga_premium_catalogs");
    m_premiumCatalog = new tankoban::manga::premium::PremiumCatalog(catalogsDir, this);

    // TANKOYOMI_PREMIUM Phase 8 -- inject the loaded catalog into the search
    // widget. Routing decisions (Premium vs Manga/Comics) and synthetic
    // catalog-only tile injection both no-op silently if the catalog failed
    // to load (m_premiumCatalog is still non-null here -- it just has zero
    // entries on load failure -- but the isPremiumSeries() / allEntries()
    // calls are zero-cost in that case).
    m_searchTakeover->setPremiumCatalog(m_premiumCatalog);

    // TANKOYOMI_PREMIUM Phase 3 -- persistent request ledger lives next to
    // CoreBridge's dataDir tree (main.cpp already mkpath'd manga_premium_*
    // siblings). Ledger is engine-independent so it's safe to spin up here;
    // the TorrentVolumeProvider waits for setTorrentClient() before it gets
    // built (MainWindow calls setTorrentClient post-TorrentClient ctor).
    const QString premiumLedgerPath = m_bridge->dataDir()
                                    + QStringLiteral("/manga_premium_requests.json");
    m_premiumLedger = new tankoban::manga::premium::TorrentRequestLedger(premiumLedgerPath, this);

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 34 — drive the
    // DOWNLOADING chip on Tankoyomi-origin tiles from the downloader's
    // state stream. downloadUpdated fires on per-chapter progress;
    // downloadCompleted fires once at the end of a series. Both routes
    // call the same refresh — chip flips Off only when status escapes
    // {queued, downloading} (i.e. completed/cancelled/error).
    connect(m_mangaDownloader, &MangaDownloader::downloadUpdated,
            this, [this](const QString&) { refreshTileChips(); });
    connect(m_mangaDownloader, &MangaDownloader::downloadCompleted,
            this, [this](const QString&) { refreshTileChips(); });
    connect(m_mangaDownloader, &MangaDownloader::chapterCompleted,
            this, &ComicsPage::onChapterCompleted);

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 30 — instantiate
    // the on-disk chapter index BEFORE the detail view so the view receives
    // a non-null pointer. Same JsonStore as everything else on this page
    // (CoreBridge::store()) — index persists at <appDataDir>/manga_downloads_index.json.
    m_mangaDownloadIndex = new MangaDownloadIndex(&m_bridge->store(), this);

    m_tyDetailView = new ComicsTankoyomiDetailView(
        m_bridge, m_sourceRegistry, m_tyLibrary, m_mangaDownloader,
        m_mangaDownloadIndex,
        m_nam, this);
    // TANKOYOMI_PREMIUM Phase 6 -- inject the Premium catalog so the detail
    // view can flip into volume-row mode on catalog-backed titles.
    m_tyDetailView->setPremiumCatalog(m_premiumCatalog);
    // TANKOYOMI_PREMIUM Phase 9 -- adopt-existing-folder lookup. When the
    // user clicks Add-to-Library on a Premium-catalog title and exactly one
    // folder-imported series already exists with a matching normalized
    // title, the detail view reuses that folder's path instead of
    // computing a fresh disambiguated folder. ComicsPage is the only place
    // that can walk m_folderSeries, so the callback is injected here.
    m_tyDetailView->setAdoptLookup([this](const QString& title) {
        return findFolderImportedSeriesPathForTitle(title);
    });
    m_stack->addWidget(m_tyDetailView);

    connect(m_tyDetailView, &ComicsTankoyomiDetailView::backRequested,
            this, &ComicsPage::onDetailBack);
    connect(m_tyDetailView, &ComicsTankoyomiDetailView::openComicRequested,
            this,
            [this](const QString& cbzPath, const QStringList& cbzList, const QString& seriesName) {
        // TANKOYOMI_CONTINUE_READING 2026-05-15 — register the Tankoyomi
        // cbz in m_progressKeyMap before forwarding to MainWindow. Must
        // happen synchronously here (same-thread direct connection) so
        // that by the time ComicReader::saveProgress writes the first
        // progress entry, refreshContinueStrip can resolve the SHA1 key.
        ensureTankoyomiChapterInMap(cbzPath);
        emit openComic(cbzPath, cbzList, seriesName);
    });
    connect(m_mangaDownloadIndex, &MangaDownloadIndex::entriesChanged,
            m_tyDetailView, &ComicsTankoyomiDetailView::refreshDownloadMarkers);

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

    // TANKOYOMI_PREMIUM Phase 9 -- thin facade over MangaDownloader +
    // TorrentVolumeProvider so a single "Transfers paused" affordance can
    // fan out to both backends. Constructed AFTER m_premiumProvider is
    // non-null (here). NO UI affordance gets bound yet; Phase 11+ surfaces
    // the "Pause all transfers" button.
    if (!m_transferCoordinator) {
        m_transferCoordinator = new tankoban::manga::premium::MangaTransferCoordinator(
            m_mangaDownloader, m_premiumProvider, this);
    }

    // TANKOYOMI_PREMIUM Phase 7 Task 7.2 -- replace the Phase 3 qDebug stubs
    // with real consumers in the detail view. All connections are
    // QueuedConnection because TorrentVolumeProvider emits from the engine's
    // alert worker thread (per Codex section 18 + the provider header).
    using P = tankoban::manga::premium::TorrentVolumeProvider;
    connect(m_premiumProvider, &P::volumeProgress,
            m_tyDetailView, &ComicsTankoyomiDetailView::onPremiumVolumeProgress,
            Qt::QueuedConnection);
    // Drop the cbzPath arg -- the detail view doesn't need it (the path is
    // re-resolved through MangaDownloadIndex on read).
    connect(m_premiumProvider, &P::volumeCompleted,
            m_tyDetailView, [this](const QString& s, int v, const QString& p){
                Q_UNUSED(p);
                m_tyDetailView->onPremiumVolumeCompleted(s, v);
            },
            Qt::QueuedConnection);
    connect(m_premiumProvider, &P::volumeFailed,
            m_tyDetailView, &ComicsTankoyomiDetailView::onPremiumVolumeFailed,
            Qt::QueuedConnection);
    connect(m_premiumProvider, &P::swarmStatus,
            m_tyDetailView, &ComicsTankoyomiDetailView::onPremiumSwarmStatus,
            Qt::QueuedConnection);
    // TANKOYOMI_PREMIUM Phase 10 -- per-volume cover thumbnail; emitted AFTER
    // volumeCompleted by the provider (cover extraction does NOT gate
    // completion per Codex section 21). Queued connection because the
    // extractor signals via QMetaObject::invokeMethod on the provider.
    connect(m_premiumProvider, &P::volumeCoverReady,
            m_tyDetailView, &ComicsTankoyomiDetailView::setPremiumVolumeCover,
            Qt::QueuedConnection);

    // Phase 7 Task 7.2 -- volume-download button click. Detail view emits the
    // (seriesId, volumeNumber) pair; we resolve the catalog entry + matching
    // PremiumVolumeEntry, derive a canonical destination folder, then dispatch
    // to TorrentVolumeProvider::requestVolume.
    connect(m_tyDetailView, &ComicsTankoyomiDetailView::premiumVolumeDownloadRequested,
            this, [this](const QString& seriesId, int volumeNumber){
                if (!m_premiumCatalog || !m_premiumProvider) return;
                const auto entry = m_premiumCatalog->entryById(seriesId);
                if (!entry) return;
                const tankoban::manga::premium::PremiumVolumeEntry* volEntry = nullptr;
                for (const auto& vv : entry->volumes) {
                    if (vv.vol == volumeNumber) { volEntry = &vv; break; }
                }
                if (!volEntry) return;
                const QString destinationPath = canonicalSeriesPathForPremium(*entry);
                if (destinationPath.isEmpty()) {
                    qDebug().noquote()
                        << QStringLiteral("[Premium] requestVolume aborted -- no comics root configured")
                        << seriesId << QStringLiteral("v%1").arg(volumeNumber);
                    return;
                }
                m_premiumProvider->requestVolume(*entry, *volEntry, destinationPath);
            });

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
    m_searchBar->setPlaceholderText("Search Tankoyomi");
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

    // ── 3. "SERIES" header row: label + sort + density ──
    auto* seriesRow = new QWidget(gridPage);
    auto* seriesLayout = new QHBoxLayout(seriesRow);
    seriesLayout->setContentsMargins(0, 0, 0, 0);
    seriesLayout->setSpacing(8);

    auto* seriesLabel = new QLabel("SERIES", seriesRow);
    seriesLabel->setObjectName("LibraryHeading");
    seriesLayout->addWidget(seriesLabel);
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
        m_tileStrip->clear();
        m_listView->clear();
        m_progressKeyMap.clear();
        m_tileStrip->hide();
        m_statusLabel->setText("Add a comics folder to get started");
        m_statusLabel->show();
        m_hasScanned = true;
        m_scanning = false;
        return;
    }

    if (!m_hasScanned) {
        // First scan: clear tiles, show scanning label for progressive loading
        m_tileStrip->clear();
        m_listView->clear();
        m_progressKeyMap.clear();
        m_statusLabel->setText("Scanning...");
        m_statusLabel->show();
        m_tileStrip->hide();
    }
    // Rescan: keep old tiles visible — atomic swap happens in onScanFinished

    QMetaObject::invokeMethod(m_scanner, "scan", Qt::QueuedConnection,
                              Q_ARG(QStringList, roots));
}

void ComicsPage::addSeriesTile(const SeriesInfo& series)
{
    // Build progress key map for continue strip (with per-file cover paths)
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

    QString subtitle = QString::number(series.fileCount)
                     + (series.fileCount == 1 ? " issue" : " issues");

    auto* card = new TileCard(series.coverThumbPath, series.seriesName, subtitle);

    card->setProperty("seriesPath", series.seriesPath);
    card->setProperty("seriesName", series.seriesName);
    card->setProperty("coverPath", series.coverThumbPath);
    card->setProperty("fileCount", series.fileCount);
    card->setProperty("newestMtime", series.newestMtimeMs);
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 33 — real
    // setProvenance call replaces the Phase 2 setProperty shim. Folder-
    // origin tiles get "folder" (no chip paints); Tankoyomi-origin tiles
    // get "tankoyomi" (paints the top-left [Tankoyomi] chip).
    const QString provenance = series.provenance.isEmpty()
        ? QStringLiteral("folder") : series.provenance;
    card->setProvenance(provenance);

    // Phase 5 Task 34: seriesKey for the DOWNLOADING-chip subscription path.
    // Looked up post-creation via ComicsTankoyomiLibrary::getByCanonicalPath
    // since folder-origin SeriesInfo doesn't carry source/seriesId; only set
    // on Tankoyomi-origin tiles (folder tiles don't have a downloader query).
    if (provenance == QStringLiteral("tankoyomi") && m_tyLibrary) {
        if (const auto rec = m_tyLibrary->getByCanonicalPath(series.seriesPath)) {
            card->setProperty("seriesKey",
                              rec->sourceId + QStringLiteral(":") + rec->seriesId);
        }
    }
    connect(card, &TileCard::clicked, this, &ComicsPage::onCardClicked);

    card->setIsFolder(true);
    {
        QJsonObject allProg = m_bridge->allProgress("comics");
        int totalPages = 0, readPages = 0;
        bool anyInProgress = false;
        bool allFinished = !series.files.isEmpty();
        bool anyNew = false;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 sevenDaysMs = 7LL * 24 * 60 * 60 * 1000;

        for (const auto& fe : series.files) {
            QString pk = QString(QCryptographicHash::hash(
                fe.path.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            QJsonObject prog = allProg.value(pk).toObject();
            bool finished = prog.value("finished").toBool();
            int page = prog.value("page").toInt(-1);
            int pc = fe.pageCount > 0 ? fe.pageCount : prog.value("pageCount").toInt(0);
            totalPages += pc;
            if (finished) {
                readPages += pc;
            } else if (page >= 0 && pc > 0) {
                readPages += page + 1;
                anyInProgress = true;
                allFinished = false;
            } else {
                allFinished = false;
            }
            if (fe.mtimeMs > 0 && (now - fe.mtimeMs) < sevenDaysMs)
                anyNew = true;
        }

        double fraction = totalPages > 0 ? static_cast<double>(readPages) / totalPages : 0.0;
        QString status = allFinished ? "finished" : (anyInProgress ? "reading" : "");
        // Dropped countBadge — the "N issues" subtitle already conveys the count
        // and the pill rendered as text "bleeding into" the thumbnail (2026-04-15 Hemanth).
        card->setBadges(fraction, QString(), QString(), status);
        card->setIsNew(anyNew);
    }

    m_tileStrip->addTile(card);

    LibraryListView::ItemData listItem;
    listItem.name = series.seriesName;
    listItem.path = series.seriesPath;
    listItem.itemCount = series.fileCount;
    listItem.lastModifiedMs = series.newestMtimeMs;
    m_listView->addItem(listItem);
}

void ComicsPage::onSeriesFound(const SeriesInfo& series)
{
    // On rescan: skip incremental tiles — atomic rebuild in onScanFinished
    if (m_hasScanned) return;

    // First scan: progressive loading
    if (m_statusLabel->isVisible()) {
        m_statusLabel->hide();
        m_tileStrip->show();
    }
    addSeriesTile(series);
}

void ComicsPage::onScanFinished(const QList<SeriesInfo>& allSeries)
{
    bool wasRescan = m_hasScanned;
    m_hasScanned = true;
    m_scanning = false;
    // REPO_HYGIENE Phase 4 P4.3 (2026-04-26) — fire pending rescan.
    if (m_rescanPending) {
        m_rescanPending = false;
        QTimer::singleShot(0, this, [this]() { triggerScan(); });
    }

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — cache folder-origin
    // SeriesInfo for rebuildTiles(). The scanner already excluded
    // claimed paths, so allSeries is the folder-origin slice only.
    m_folderSeries = allSeries;

    // Cache Tankoyomi-origin slice once: m_tyLibrary->all() takes the
    // library mutex + copies every record. Reuse the snapshot below.
    const auto tyRecords = m_tyLibrary->all();

    if (wasRescan) {
        // Atomic swap: clear old tiles, rebuild from complete list
        // (folder-origin + Tankoyomi-origin merged via rebuildTiles,
        // which clears m_listView + m_progressKeyMap internally).
        rebuildTiles();
    } else {
        // First scan: tiles were emitted incrementally via onSeriesFound.
        // Append the Tankoyomi-origin slice now that the folder slice is
        // complete. rebuildTiles() would also work but would double-add
        // the folder rows that onSeriesFound already inserted.
        for (const auto& r : tyRecords) addSeriesTile(seriesInfoFromRecord(r));
    }

    const bool empty = m_folderSeries.isEmpty() && tyRecords.isEmpty();
    if (empty) {
        m_tileStrip->hide();
        m_statusLabel->setObjectName("LibraryEmptyLabel");
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setText("No comics found\nAdd a root folder via the + button or browse Sources for content");
        m_statusLabel->show();
    } else {
        m_statusLabel->hide();
        m_tileStrip->show();
        m_tileStrip->sortTiles(m_sortCombo->currentData().toString());
    }

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
    rebuildTiles();
}

void ComicsPage::rebuildTiles()
{
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — assemble merged tile
    // set from (a) cached folder-origin SeriesInfo (scanner already
    // suppressed claimed paths) and (b) Tankoyomi-origin records from
    // m_tyLibrary. Folder rows render with no provenance badge;
    // Tankoyomi rows render with the [Tankoyomi] chip (TileCard
    // property-shim until Phase 5 Task 33 adds setProvenance).
    m_tileStrip->clear();
    m_listView->clear();
    m_progressKeyMap.clear();

    for (const auto& s : m_folderSeries) addSeriesTile(s);

    for (const auto& r : m_tyLibrary->all()) addSeriesTile(seriesInfoFromRecord(r));
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
    // TANKOYOMI_CONTINUE_READING 2026-05-15 — Title = series name (rec.title).
    // Subtitle = "<ChapterName> • Page X/Y". Chapter name comes from the
    // cbz filename (e.g. "Prologue 1.cbz" → "Prologue 1"), which is the
    // sanitised chapter name MangaDownloader writes to disk at write-time.
    // For chapter names containing characters in `[<>:"/\\|?*]` (sanitised
    // to `_`), this gives a near-display-quality result; the rare case
    // where the original name is meaningfully nicer (e.g. "Ep. 5: Crisis"
    // vs on-disk "Ep. 5_ Crisis") is acceptable display loss for v1.
    const QString chapterName = QFileInfo(cbzPath).completeBaseName();
    const QString pageLabel = pageCount > 0
        ? QStringLiteral("Page %1/%2").arg(page + 1).arg(pageCount)
        : QStringLiteral("Page %1").arg(page + 1);
    return {
        rec.title,
        chapterName.isEmpty() ? pageLabel
                              : QStringLiteral("%1 • %2").arg(chapterName, pageLabel)
    };
}

void ComicsPage::onTileClicked(const QString& seriesPath, const QString& seriesName)
{
    openSeriesByPath(seriesPath, seriesName);
}

void ComicsPage::openSeriesByPath(const QString& seriesPath, const QString& seriesName,
                                  const QString& coverPath)
{
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 27 — provenance route.
    // Tankoyomi-origin tile (canonical path matches a library record) →
    // open the new detail view; folder-origin tile → today's SeriesView.
    // Uses ComicsTankoyomiLibrary::getByCanonicalPath (O(1) hash lookup via
    // the internal m_canonicalToKey map) instead of the previous all()
    // linear scan — matters at 100+ records (code-quality review I3).
    if (m_tyLibrary) {
        if (const auto rec = m_tyLibrary->getByCanonicalPath(seriesPath)) {
            // Phase 9 Task 52 — tile-click is always a Library→Detail
            // transition (Tankoyomi tiles live inside the merged library
            // grid). Record origin so onDetailBack routes to library.
            m_enteredDetailFrom = Mode::Library;
            m_mode = Mode::TankoyomiDetail;
            m_tyDetailView->showEntry(rec->detailCache.preview);
            m_stack->setCurrentWidget(m_tyDetailView);
            return;
        }
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

void ComicsPage::showLibraryMode()
{
    m_mode = Mode::Library;
    m_stack->setCurrentIndexAnimated(0);
    if (m_searchTakeover) m_searchTakeover->clearResults();
}

void ComicsPage::showSearchMode(const QString& query)
{
    m_mode = Mode::SearchResults;
    m_searchTakeover->search(query);
    m_stack->setCurrentWidget(m_searchTakeover);
}

void ComicsPage::onSearchResultActivated(const MangaResult& preview)
{
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 26 — Phase 3 qDebug
    // stub replaced with the real detail-view open. Phase 9 Task 52 —
    // record the SearchResults origin BEFORE flipping mode so onDetailBack
    // can route Back back to the search-takeover (not the library grid).
    m_enteredDetailFrom = Mode::SearchResults;
    m_mode = Mode::TankoyomiDetail;
    m_tyDetailView->showEntry(preview);
    m_stack->setCurrentWidget(m_tyDetailView);
}

void ComicsPage::onDetailBack()
{
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 9 Task 52 — Back
    // from detail routes by the origin recorded at entry. Search→Detail→
    // Back lands on the search-takeover (m_searchTakeover at m_stack idx 2);
    // Library→Detail→Back lands on the merged library grid.
    if (m_enteredDetailFrom == Mode::SearchResults && m_searchTakeover) {
        m_mode = Mode::SearchResults;
        m_stack->setCurrentWidget(m_searchTakeover);
    } else {
        showLibraryMode();
    }
    // Code-quality review I3: defensive reset. Today every detail-entry
    // site (onSearchResultActivated, onTileClicked Tankoyomi branch)
    // overwrites m_enteredDetailFrom, so a stale value can't leak forward.
    // Reset on exit forecloses the trap if a future entry site lands
    // (deep-link, notification handler, continue-strip click) and forgets
    // to set the origin — the field defaults to Library and Back goes to
    // library, the safe fallback.
    m_enteredDetailFrom = Mode::Library;
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
            title = ScannerUtils::cleanMediaFolderTitle(QFileInfo(ref->filePath).completeBaseName());
            subtitle = pageCount > 0
                ? QString("Page %1/%2").arg(page + 1).arg(pageCount)
                : QString("Page %1").arg(page + 1);
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

    if (m_mode == Mode::TankoyomiDetail && m_tyDetailView) {
        const auto& p = m_tyDetailView->currentPreview();
        blob["sourceId"]    = p.source;
        blob["seriesId"]    = p.id;
        blob["enteredFrom"] = (m_enteredDetailFrom == Mode::SearchResults
                                ? "search" : "library");
        return blob;
    }

    return blob;
}

bool ComicsPage::restoreNavState(const QJsonObject& blob)
{
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

    if (mode == "tankoyomiDetail" && m_tyLibrary && m_tyDetailView) {
        const QString sid      = blob.value("sourceId").toString();
        const QString seriesId = blob.value("seriesId").toString();
        if (!sid.isEmpty() && !seriesId.isEmpty()
            && m_tyLibrary->contains(sid, seriesId)) {
            const auto rec = m_tyLibrary->get(sid, seriesId);
            m_enteredDetailFrom = (blob.value("enteredFrom").toString() == "search"
                                    ? Mode::SearchResults : Mode::Library);
            m_mode = Mode::TankoyomiDetail;
            // Code-quality review I2: if the record was Add'd from search
            // but fetchDetail never landed (user Back'd before the network
            // call returned), rec.detailCache.preview is a default-
            // constructed MangaResult with empty source/id — feeding that
            // to showEntry strands the user on "(Unknown source)" hero.
            // Synthesize a minimal MangaResult from the validated (sid,
            // seriesId) and let showEntry's library/sidecar/preview cache
            // chain rehydrate it via the normal path.
            MangaResult preview = rec.detailCache.preview;
            if (preview.source.isEmpty() || preview.id.isEmpty()) {
                preview = MangaResult{};
                preview.source = sid;
                preview.id     = seriesId;
                preview.title  = rec.title;
            }
            m_tyDetailView->showEntry(preview);
            m_stack->setCurrentWidget(m_tyDetailView);
            return true;
        }
        // Brief CRITICAL fix #2: cache miss — fall back inline to whichever
        // mode entered originally. Plan template recursed with literal
        // "search" which would NOT match the "searchResults" discriminator
        // and silently return false. Inline routing avoids that trap.
        const QString enteredFrom = blob.value("enteredFrom").toString();
        if (enteredFrom == "search") {
            showSearchMode(QString());  // empty query — user can re-type
            return true;
        }
        showLibraryMode();
        return true;
    }

    return false;
}
