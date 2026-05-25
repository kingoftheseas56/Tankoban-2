#include "MainWindow.h"
#include "GlassBackground.h"
#include "RootFoldersOverlay.h"
#include "widgets/ThemePicker.h"
#include "pages/ComicsPage.h"
#include "pages/BooksPage.h"
#include "pages/VideosPage.h"
#include "pages/OrganisePage.h"
#include "pages/StreamPage.h"
#include "pages/stream/StreamDownloadsPage.h"
#include "pages/TankorentPage.h"
#include "pages/TankoLibraryPage.h"
#include "widgets/SidebarDrawer.h"
#include "core/torrent/TorrentClient.h"
#include "core/stream/StreamLibrary.h"
#include "core/stream/StreamDownloadIndex.h"
#include "core/stream/StreamProgress.h"
#include "core/stream/StreamRescueScanner.h"
#include "readers/ComicReader.h"
#include "readers/BookReader.h"
#include "player/VideoPlayer.h"
#include "core/CoreBridge.h"
#include "core/DebugLogBuffer.h"
#include "core/JsonlEventLog.h"
#include "core/JsonStore.h"
#include "devtools/DevControlServer.h"
#include "devtools/UiInteractionDispatcher.h"
#include "devtools/SystemIntrospection.h"
#include "Theme.h"
#include "PerModeNavController.h"
#include "LayerEntry.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QTimer>
#include <QFrame>
#include <QApplication>
#include <QResizeEvent>
#include <QScreen>
#include <QButtonGroup>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QIcon>
#include <QDialog>
#include <QEvent>
#include <QMouseEvent>
#include <QMetaObject>
#include <QWindowStateChangeEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#endif

// ── Page id constants ───────────────────────────────────────────────────────
static constexpr const char *PAGE_COMICS       = "comics";
static constexpr const char *PAGE_BOOKS        = "books";
static constexpr const char *PAGE_VIDEOS       = "videos";
static constexpr const char *PAGE_ORGANISE     = "organise";
static constexpr const char *PAGE_STREAM       = "stream";
// SOURCES_SIDEBAR 2026-05-05 — PAGE_SOURCES removed; replaced by three peer
// pages reachable via the slide-in left sidebar drawer.
static constexpr const char *PAGE_TANKORENT    = "tankorent";
static constexpr const char *PAGE_TANKOLIBRARY = "tankolibrary";
static constexpr const char *PAGE_STREAM_DOWNLOADS = "streamDownloads";

// ── Constructor ─────────────────────────────────────────────────────────────
MainWindow::MainWindow(CoreBridge* bridge, QWidget *parent)
    : QMainWindow(parent)
    , m_bridge(bridge)
{
    // FRAMELESS_CHROME_FIX 2026-05-01: drop OS title bar; chrome buttons
    // (min/max/close) live in m_topBar's right edge. On Windows we keep
    // WS_THICKFRAME (re-added in showEvent) and zero out WM_NCCALCSIZE so
    // Aero snap / Win+Arrow / drag / double-click-max / right-click system
    // menu all stay native via WM_NCHITTEST returning HTCAPTION on empty
    // topbar zones.
    setWindowFlag(Qt::FramelessWindowHint, true);

    setWindowTitle("Tankoban");
    // Set a sane default geometry centered on screen
    if (auto *screen = QApplication::primaryScreen()) {
        auto avail = screen->availableGeometry();
        int w = qMin(1280, static_cast<int>(avail.width() * 0.85));
        int h = qMin(800,  static_cast<int>(avail.height() * 0.85));
        int x = avail.x() + (avail.width() - w) / 2;
        int y = avail.y() + (avail.height() - h) / 2;
        setGeometry(x, y, w, h);
    } else {
        resize(1280, 800);
    }

    auto *root = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // Glass gradient background (sits behind everything)
    m_glassBg = new GlassBackground(root);
    m_glassBg->lower();

    // Content wrapper (topbar + page stack)
    auto *content = new QFrame(root);
    content->setObjectName("Content");
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    buildTopBar();
    contentLayout->addWidget(m_topBar);

    // STREAM_DOWNLOADED_LIBRARY 2026-05-10 Phase 1 — persistent index of
    // bulk-downloaded episodes. Constructed BEFORE buildPageStack() so the
    // setStreamDownloadIndex(m_streamDownloadIndex) wirings inside that
    // method (TorrentClient at line ~571, StreamPage at ~584, VideosPage
    // at ~591) all see a non-null pointer and propagate it down. Prior
    // construction order placed this AFTER buildPageStack, leaving the
    // pointer nullptr at the moment the wirings ran — silently skipping
    // them via the `if (m_streamDownloadIndex)` guards and breaking the
    // on-publish registerEpisode path for every bulk-downloaded episode.
    // Bug surfaced 2026-05-12 when Hemanth's Daredevil S02 E5-E8 finished
    // downloading + published correctly to disk yet StreamDetailView
    // showed them as not-downloaded (no Status marker, click opened
    // sources panel instead of auto-playing local file).
    // Phase 3.4 (2026-05-20) — StreamDownloadIndex no longer owns a JsonStore.
    // Constructed empty here; setRepository() is called below (after the
    // TorrentClient that owns the SQLite repo exists) to inject the durable
    // backing store and trigger the in-memory map rebuild. Reads between this
    // line and that wire return empty defaults — every existing consumer of
    // the index already tolerates that via the nullptr/empty-return guards.
    m_streamDownloadIndex = new StreamDownloadIndex(this);
    DebugLogBuffer::instance().info(QStringLiteral("boot"),
        QStringLiteral("stream-download-index-created"));

    // CW_NAMESPACE_BOUNDARY 2026-05-13 — one-shot migration. Walks
    // video_progress.json and evicts/re-domains entries whose `path`
    // is registered in StreamDownloadIndex (i.e., downloaded streams
    // wrongly persisted in the "videos" namespace by the pre-fix
    // launch path). Runs synchronously after the index has loaded
    // its on-disk state, before buildPageStack so VideosPage's
    // refreshContinueStrip never sees the legacy pollution.
    migrateLegacyStreamProgressEntries();

    buildPageStack();
    contentLayout->addWidget(m_pageStack, 1);
    DebugLogBuffer::instance().info("mainwindow", "5a-pagestack-added");

    rootLayout->addWidget(content, 1);

    m_sidebar = new SidebarDrawer(root);
    m_sidebar->hide();
    connect(m_hamburgerBtn, &QPushButton::clicked, m_sidebar, &SidebarDrawer::toggle);
    connect(m_sidebar, &SidebarDrawer::sourceClicked, this, [this](const QString& pageId) {
        activatePage(pageId);
        if (m_sidebar)
            m_sidebar->close();
    });

    // Root folders overlay (hidden by default)
    m_rootFoldersOverlay = new RootFoldersOverlay(m_bridge, root);
    m_rootFoldersOverlay->hide();
    DebugLogBuffer::instance().info("mainwindow", "5b-rootfolders-overlay");
    connect(m_rootFoldersOverlay, &RootFoldersOverlay::closeRequested, this, &MainWindow::hideRootFolders);
    connect(m_rootFoldersOverlay, &RootFoldersOverlay::foldersChanged, this, [this]() {
        if (auto *comics = m_pageStack->findChild<ComicsPage*>())
            comics->triggerScan();
        if (auto *books = m_pageStack->findChild<BooksPage*>())
            books->triggerScan();
        if (auto *videos = m_pageStack->findChild<VideosPage*>())
            videos->triggerScan();
    });

    // Comic reader overlay (hidden by default)
    DebugLogBuffer::instance().info("mainwindow", "5c-before-comicreader");
    m_comicReader = new ComicReader(m_bridge, root);
    m_comicReader->hide();
    connect(m_comicReader, &ComicReader::closeRequested, this, &MainWindow::closeComicReader);
    // PER_VIEW_CHROME_FIX 2026-05-02 P3 — chrome cluster signals routed to
    // MainWindow chrome slots. closeRequested above is BACK (exit reader);
    // chromeCloseRequested below is full app close.
    connect(m_comicReader, &ComicReader::chromeMinimizeRequested,        this, &MainWindow::showMinimized);
    connect(m_comicReader, &ComicReader::chromeMaximizeToggleRequested,  this, &MainWindow::onChromeMaximizeToggle);
    connect(m_comicReader, &ComicReader::chromeCloseRequested,           this, &MainWindow::close);
    connect(m_comicReader, &ComicReader::fullscreenRequested, this, [this](bool enter) {
        if (enter) {
            m_wasMaximizedBeforeFullscreen = isMaximized();
            showFullScreen();
        } else {
            if (m_wasMaximizedBeforeFullscreen)
                showMaximized();
            else
                showNormal();
        }
    });

    // Book reader overlay (hidden by default)
    m_bookReader = new BookReader(m_bridge, root);
    m_bookReader->hide();
    connect(m_bookReader, &BookReader::closeRequested, this, &MainWindow::closeBookReader);
    // PER_VIEW_CHROME_FIX 2026-05-02 P4 — chrome cluster signals routed to
    // MainWindow chrome slots (mirrors comic-reader wiring; chrome Close
    // closes the entire app via MainWindow::close, distinct from
    // closeRequested above which is the BACK button → exit reader to lib).
    connect(m_bookReader, &BookReader::chromeMinimizeRequested,        this, &MainWindow::showMinimized);
    connect(m_bookReader, &BookReader::chromeMaximizeToggleRequested,  this, &MainWindow::onChromeMaximizeToggle);
    connect(m_bookReader, &BookReader::chromeCloseRequested,           this, &MainWindow::close);
    connect(m_bookReader, &BookReader::fullscreenRequested, this, [this](bool enter) {
        if (enter) {
            m_wasMaximizedBeforeFullscreen = isMaximized();
            showFullScreen();
        } else {
            if (m_wasMaximizedBeforeFullscreen)
                showMaximized();
            else
                showNormal();
        }
    });

    // Video player overlay (hidden by default)
    DebugLogBuffer::instance().info("mainwindow", "5e-before-videoplayer");
    m_videoPlayer = new VideoPlayer(m_bridge, root);
    m_videoPlayer->hide();
    connect(m_videoPlayer, &VideoPlayer::closeRequested, this, &MainWindow::closeVideoPlayer);
    // PER_VIEW_CHROME_FIX 2026-05-02 P2 — chrome cluster signals routed to
    // MainWindow chrome slots (mirrors comic + book reader wiring).
    // closeRequested above is BACK arrow (exit player → return to library);
    // chromeCloseRequested below is full app close.
    connect(m_videoPlayer, &VideoPlayer::chromeMinimizeRequested,        this, &MainWindow::showMinimized);
    connect(m_videoPlayer, &VideoPlayer::chromeMaximizeToggleRequested,  this, &MainWindow::onChromeMaximizeToggle);
    connect(m_videoPlayer, &VideoPlayer::chromeCloseRequested,           this, &MainWindow::close);
    connect(m_videoPlayer, &VideoPlayer::fullscreenRequested, this, [this](bool enter) {
        if (enter) {
            m_wasMaximizedBeforeFullscreen = isMaximized();
            showFullScreen();
        } else {
            if (m_wasMaximizedBeforeFullscreen)
                showMaximized();
            else
                showNormal();
        }
        // SOURCES_SIDEBAR 2026-05-05 — disable hamburger toggle while video is
        // fullscreen + force-close any already-open drawer (avoids drawer
        // floating over a fullscreen video).
        if (m_hamburgerBtn)
            m_hamburgerBtn->setEnabled(!enter);
        if (enter && m_sidebar && m_sidebar->isOpen())
            m_sidebar->close();
    });

    DebugLogBuffer::instance().info("mainwindow", "5f-before-central");
    setCentralWidget(root);
    bindShortcuts();
    setupTrayIcon();
    DebugLogBuffer::instance().info("mainwindow", "5g-constructor-done");

    // Connect comics page to reader
    if (auto *comics = m_pageStack->findChild<ComicsPage*>()) {
        connect(comics, &ComicsPage::openComic, this, &MainWindow::openComicReader);
    }

    // Connect books page to reader
    if (auto *books = m_pageStack->findChild<BooksPage*>()) {
        connect(books, &BooksPage::openBook, this, &MainWindow::openBookReader);
    }

    // Connect videos page to player
    if (auto *videos = m_pageStack->findChild<VideosPage*>()) {
        connect(videos, &VideosPage::playVideo, this, &MainWindow::openVideoPlayer);
        // Forward player progress to VideosPage for continue strip refresh
        connect(m_videoPlayer, &VideoPlayer::progressUpdated, videos, [videos]() {
            videos->refreshContinueOnly();
        });
    }

    activatePage(PAGE_COMICS);

    // STREAM_DOWNLOADED_LIBRARY 2026-05-10 Phase 6 — first-launch migration
    // gate. Reads <dataDir>/stream_downloads_meta.json: if migrationVersion
    // >= 1 nothing happens; otherwise we walk Videos roots, regex-match the
    // canonical bulk-download layout, query Cinemeta to resolve each show,
    // register per-episode entries in StreamDownloadIndex, and materialize
    // StreamLibrary entries. Spec §9.1.
    //
    // Deferred to QTimer::singleShot(0, ...) so the constructor finishes,
    // the window paints once, and the modal dialog has a sane parent that's
    // already shown. The gate is also a no-op when there are no Videos
    // roots (writes the version pin so we don't re-check on every launch).
    QTimer::singleShot(0, this, [this]() {
        if (!m_bridge) return;
        JsonStore& store = m_bridge->store();
        const QJsonObject migMeta = store.read(QStringLiteral("stream_downloads_meta.json"));
        const int storedVersion = migMeta.value(QStringLiteral("migrationVersion")).toInt(0);
        if (storedVersion >= 1) return;  // already migrated

        const QStringList videoRoots = m_bridge->rootFolders(QStringLiteral("videos"));
        if (videoRoots.isEmpty()) {
            // No Videos roots — nothing to migrate. Pin migrationVersion so
            // we don't re-check on every launch.
            QJsonObject pin;
            pin[QStringLiteral("migrationVersion")] = 1;
            pin[QStringLiteral("completedAt")] =
                static_cast<double>(QDateTime::currentMSecsSinceEpoch());
            store.write(QStringLiteral("stream_downloads_meta.json"), pin);
            return;
        }

        // m_streamPage is the canonical home for the StreamLibrary +
        // MetaAggregator instances; both shared via the public accessors
        // mirroring the existing metaAggregator() pattern. Either being
        // null at this point is a structural bug — bail rather than crash.
        if (!m_streamPage) return;
        StreamLibrary* library = m_streamPage->streamLibrary();
        tankostream::stream::MetaAggregator* meta = m_streamPage->metaAggregator();
        if (!m_streamDownloadIndex || !library || !meta) return;

        // STREAM_BULK_DOWNLOAD_V2 2026-05-10 — run the migration silently.
        // Hemanth verbatim: "make sure i don't see this notification anymore"
        // re: the StreamRescueProgressDialog popup. The migration adds
        // matched shows to the Stream library either way; the popup only
        // surfaced "0 shows / 0 episodes" feedback the user didn't want
        // visible. Scanner runs in the background as before; results
        // appear silently in the Stream library when matches succeed.
        auto* scanner = new StreamRescueScanner(
            m_streamDownloadIndex, library, meta,
            &store, videoRoots, this);
        connect(scanner, &StreamRescueScanner::complete,
                scanner, &QObject::deleteLater);
        scanner->start();
    });

    // FRAMELESS_CHROME_FIX 2026-05-01 — re-add WS_THICKFRAME / WS_MAXIMIZEBOX
    // / WS_MINIMIZEBOX so Aero snap (drag-to-edge), Win+Arrow snap, taskbar
    // hover thumbnails, and resize cursors stay native. Qt::FramelessWindow-
    // Hint stripped these styles; without re-adding them WM_NCHITTEST + WM_-
    // NCCALCSIZE alone won't bring snap back. winId() forces native HWND
    // creation so SetWindowLong has a valid handle.
    // FRAMELESS_CHROME_FIX 2026-05-01 — initial Win32 style application.
    // Factored into applyFramelessWin32Style() helper 2026-05-04 so the
    // same block can be re-fired on fullscreen-exit transitions
    // (changeEvent below) — fixes the brief OS-title-bar flash Hemanth
    // observed when exiting fullscreen on the mpv video player.
    applyFramelessWin32Style();

    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- per-mode back-stack controller.
    m_navController = new tankoban::ui::PerModeNavController(this);
    connect(m_navController, &tankoban::ui::PerModeNavController::layerRestoreRequested,
            this, &MainWindow::onLayerRestoreRequested);
    connect(m_navController, &tankoban::ui::PerModeNavController::backAvailableChanged,
            this, &MainWindow::onBackAvailabilityChanged);
    connect(m_navController, &tankoban::ui::PerModeNavController::backDestinationLabelChanged,
            this, &MainWindow::onBackDestinationLabelChanged);

    // Bootstrap: tell the controller the current active mode so its
    // emitBackAvailability fires correctly on first signal-driver attach.
    if (!m_activePageId.isEmpty()) {
        m_navController->setActiveMode(m_activePageId);
    }
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- seed the initial layer
    // for Comics so that the FIRST deep transition (e.g. clicking a
    // BOOKMARKED tile) has a "library" entry beneath it and canGoBack
    // returns true. Without this push the stack starts empty and the
    // chevron stays gray after the first enteredLayer emit.
    if (m_activePageId == QStringLiteral("comics") && m_navController) {
        m_navController->pushLayer(QStringLiteral("comics"),
            tankoban::ui::LayerEntry{QStringLiteral("comics"),
                                     QStringLiteral("library"),
                                     QStringLiteral("Library"),
                                     {}});
    }
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- Task 9: seed the initial
    // layer for Stream so the first deep transition (e.g. clicking a Detail
    // tile) has a "browse" entry beneath it and canGoBack returns true.
    if (m_activePageId == QStringLiteral("stream") && m_navController) {
        m_navController->pushLayer(QStringLiteral("stream"),
            tankoban::ui::LayerEntry{QStringLiteral("stream"),
                                     QStringLiteral("browse"),
                                     QStringLiteral("Theatre Home"),
                                     {}});
    }
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- Task 10: Books / Videos /
    // Tankorent initial-root bootstrap. Each pushes one root layer so the
    // mode's stack starts with [root] rather than empty; any future deep
    // view that emits enteredLayer lands on top with canGoBack true.
    if (m_activePageId == QStringLiteral("books") && m_navController) {
        m_navController->pushLayer(QStringLiteral("books"),
            tankoban::ui::LayerEntry{QStringLiteral("books"),
                                     QStringLiteral("library"),
                                     QStringLiteral("Books"),
                                     {}});
    }
    if (m_activePageId == QStringLiteral("videos") && m_navController) {
        m_navController->pushLayer(QStringLiteral("videos"),
            tankoban::ui::LayerEntry{QStringLiteral("videos"),
                                     QStringLiteral("library"),
                                     QStringLiteral("Videos"),
                                     {}});
    }
    if (m_activePageId == QStringLiteral("tankorent") && m_navController) {
        m_navController->pushLayer(QStringLiteral("tankorent"),
            tankoban::ui::LayerEntry{QStringLiteral("tankorent"),
                                     QStringLiteral("home"),
                                     QStringLiteral("Tankorent"),
                                     {}});
    }

    // GLOBAL_NAV_HISTORY Task 7: gray chevrons while a modal dialog is up.
    qApp->installEventFilter(this);
}

// ── Resize ──────────────────────────────────────────────────────────────────
void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_glassBg && centralWidget()) {
        m_glassBg->setGeometry(centralWidget()->rect());
    }
    if (m_rootFoldersOverlay && centralWidget()) {
        m_rootFoldersOverlay->setGeometry(centralWidget()->rect());
    }
    if (m_comicReader && centralWidget()) {
        m_comicReader->setGeometry(centralWidget()->rect());
    }
    if (m_bookReader && centralWidget()) {
        m_bookReader->setGeometry(centralWidget()->rect());
    }
    if (m_videoPlayer && centralWidget()) {
        m_videoPlayer->setGeometry(centralWidget()->rect());
    }
}

// ── Top bar ─────────────────────────────────────────────────────────────────
void MainWindow::buildTopBar()
{
    auto *bar = new QFrame(this);
    bar->setObjectName("TopBar");
    bar->setFixedHeight(56);

    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(12);

    // ── Left slot (Brand label, left-aligned in a fixed-width container) ───
    // FRAMELESS_CHROME_FIX 2026-05-01: structural counterweight. The right
    // slot holds Theme + scan + add + chrome cluster, which is wider than
    // the brand label alone. To keep nav horizontally centered on the
    // window we wrap each side in its own slot and force leftSlot's width
    // to mirror rightSlot's sizeHint() at the end of buildTopBar(). Robust
    // to future button additions on either side — no magic numbers.
    auto* leftSlot = new QWidget(bar);
    m_topBarLeftSlot = leftSlot;
    leftSlot->setObjectName("TopBarLeftSlot");
    auto* leftLayout = new QHBoxLayout(leftSlot);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    // SOURCES_SIDEBAR 2026-05-05 — hamburger button before brand label.
    // Toggles the left slide-in drawer holding Tankorent /
    // TankoLibrary. Sized to match IconButton precedent (28x24); not
    // checkable, not part of m_navGroup (it's a toggle, not a content-mode peer).
    m_hamburgerBtn = new QPushButton(leftSlot);
    m_hamburgerBtn->setObjectName("HamburgerButton");
    m_hamburgerBtn->setFixedSize(28, 24);
    m_hamburgerBtn->setCursor(Qt::PointingHandCursor);
    m_hamburgerBtn->setIcon(QIcon(":/icons/hamburger.svg"));
    m_hamburgerBtn->setIconSize(QSize(16, 16));
    m_hamburgerBtn->setToolTip("Open sidebar (Ctrl+5)");
    m_hamburgerBtn->setFocusPolicy(Qt::NoFocus);
    leftLayout->addWidget(m_hamburgerBtn, 0, Qt::AlignVCenter);
    leftLayout->addSpacing(8);

    // Global Back chevron (spec: docs/superpowers/specs/2026-05-14-global-nav-history-design.md §6)
    m_backBtn = new QPushButton(leftSlot);
    m_backBtn->setObjectName("TopBarBackBtn");
    m_backBtn->setFixedSize(28, 24);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setIcon(QIcon(":/icons/chevron_left.svg"));
    m_backBtn->setIconSize(QSize(16, 16));
    m_backBtn->setToolTip("Back (Alt+Left)");
    m_backBtn->setAccessibleName("Back");
    m_backBtn->setAccessibleDescription("Navigate to the previous page. Keyboard: Alt+LeftArrow.");
    m_backBtn->setFocusPolicy(Qt::NoFocus);
    m_backBtn->setEnabled(false);  // initial: stack empty
    leftLayout->addWidget(m_backBtn, 0, Qt::AlignVCenter);
    connect(m_backBtn, &QPushButton::clicked,
            this, &MainWindow::onBackChevronClicked);

    // Global Forward chevron
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- Forward chevron physically
    // removed per spec piece B + matrix invariant. Hemanth verbatim: "nah
    // fuck the front button, remove it. doesn't serve purpose with our new
    // navigation style." Brand label shifts left ~42px to close the gap
    // (spec B1, "Back chevron stays put, space collapses"). The Alt+Right
    // shortcut + Qt::ForwardButton mouse handler are also removed below
    // (spec B2 / B3 / B4 -- all unbound, no remap).

    // Visual separator: bump the gap between Forward and Brand from
    // the default 6px iconBtn spacing to ~12px to read as "nav cluster"
    // vs "brand identity".
    leftLayout->addSpacing(6);

    m_brandLabel = new QLabel("Tankoban", leftSlot);
    m_brandLabel->setObjectName("Brand");
    leftLayout->addWidget(m_brandLabel);
    leftLayout->addStretch(1);
    layout->addWidget(leftSlot);

    layout->addStretch(1);

    // Nav button group inside its own frame
    auto *nav = new QFrame(bar);
    nav->setObjectName("TopNav");
    auto *navLayout = new QHBoxLayout(nav);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(6);

    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);

    struct NavDef { const char *id; const char *label; };
    const NavDef navDefs[] = {
        { PAGE_COMICS,  "Comics"  },
        { PAGE_BOOKS,   "Books"   },
        // TANKORENT_STREAM_INTEGRATION 2026-05-15 — Videos sidebar entry
        // removed; VideosPage stays linked for VideosScanner reuse by the
        // Theatre Local files row (Task E4). Ctrl+3 keybind preserved as a
        // power-user escape hatch.
        { PAGE_STREAM,  "Theatre" },
        // SOURCES_SIDEBAR 2026-05-05 — Sources entry removed; the two sub-pages
        // (Tankorent / TankoLibrary) are now reachable via the
        // hamburger-toggled left drawer.
    };

    for (const auto &def : navDefs) {
        auto *btn = new QPushButton(def.label, nav);
        btn->setObjectName("TopNavButton");
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        navLayout->addWidget(btn);

        m_navGroup->addButton(btn);

        QString pageId = def.id;
        connect(btn, &QPushButton::clicked, this, [this, pageId]() {
            // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — standing
            // Tankoban contract (Tankoban Max -> Tankoban Max Butterfly ->
            // Tankoban QT Groundworks -> Tankoban 2): mode pills are
            // end-all-be-all hard resets. activatePage early-returns on
            // pageId == m_activePageId, which silently swallows pill
            // clicks while the user is in a deep sub-view (Comics search
            // results, Comics series view, Stream detail, ...). Route the
            // same-page case through resetActivePageToRoot so the pill
            // ALWAYS returns the user to that mode's root home. The
            // cross-mode case falls through to activatePage's normal
            // page-swap path (whose own activate() handlers reset by
            // construction for pages that need it).
            // PHASE 1 NAV REDESIGN 2026-05-17 -- mode pill is end-all-be-all hard
            // reset. resetMode wipes the target mode's per-mode stack to [] BEFORE
            // we decide whether to dispatch to resetActivePageToRoot (same-page) or
            // activatePage (cross-mode). Both branches benefit: same-page in-mode
            // resets clear any stale layer entries; cross-mode jumps start the new
            // mode from a clean stack.
            if (m_navController) m_navController->resetMode(pageId);
            if (pageId == m_activePageId) {
                resetActivePageToRoot();
            } else {
                activatePage(pageId);
            }
        });

        m_navButtons.append({ pageId, btn });
    }

    layout->addWidget(nav);
    layout->addStretch(1);

    // Right slot wraps Theme + scan + add + chrome cluster so its sizeHint
    // can be mirrored onto leftSlot's fixed width (see end of buildTopBar).
    auto* rightSlot = new QWidget(bar);
    m_topBarRightSlot = rightSlot;
    rightSlot->setObjectName("TopBarRightSlot");
    auto* rightLayout = new QHBoxLayout(rightSlot);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);

    // Theme picker (mode toggle): theme is app-appearance, scan/add are
    // library-data; group by intent per Hemanth 2026-04-25.
    auto* themePicker = new ThemePicker(rightSlot);
    rightLayout->addWidget(themePicker, 0, Qt::AlignVCenter);

    m_organiseBtn = new QPushButton(rightSlot);
    m_organiseBtn->setObjectName("IconButton");
    m_organiseBtn->setFixedSize(28, 24);
    m_organiseBtn->setIcon(QIcon(":/icons/organise.svg"));
    m_organiseBtn->setIconSize(QSize(16, 16));
    m_organiseBtn->setCursor(Qt::PointingHandCursor);
    m_organiseBtn->setToolTip("Organise video categories");
    m_organiseBtn->hide();
    connect(m_organiseBtn, &QPushButton::clicked, this, [this]() {
        if (m_videosPage)
            m_videosPage->activate();
        if (m_organisePage && m_videosPage)
            m_organisePage->setShows(m_videosPage->currentShows());
        activatePage(PAGE_ORGANISE);
    });
    rightLayout->addWidget(m_organiseBtn, 0, Qt::AlignVCenter);

    // Rescan button
    auto *scanBtn = new QPushButton(QString::fromUtf8("\u21BB"), rightSlot);
    scanBtn->setObjectName("IconButton");
    scanBtn->setFixedSize(28, 24);
    scanBtn->setCursor(Qt::PointingHandCursor);
    scanBtn->setToolTip("Rescan library (F5)");
    connect(scanBtn, &QPushButton::clicked, this, [this]() {
        if (auto *c = m_pageStack->findChild<ComicsPage*>()) c->triggerScan();
        if (auto *b = m_pageStack->findChild<BooksPage*>())  b->triggerScan();
        if (auto *v = m_pageStack->findChild<VideosPage*>()) v->triggerScan();
    });
    rightLayout->addWidget(scanBtn, 0, Qt::AlignVCenter);

    // Add folder button (+)
    auto *addBtn = new QPushButton("+", rightSlot);
    addBtn->setObjectName("IconButton");
    addBtn->setFixedSize(28, 24);
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setToolTip("Add root folder");
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::showRootFolders);
    rightLayout->addWidget(addBtn, 0, Qt::AlignVCenter);

    // FRAMELESS_CHROME_FIX 2026-05-01: window-action cluster (min / max /
    // close). 8-px gap separates library actions from window actions.
    rightLayout->addSpacing(8);
    auto makeChrome = [rightSlot](const char* objName, const QString& iconPath,
                                  const QString& tip) {
        auto* btn = new QPushButton(rightSlot);
        btn->setObjectName(objName);
        btn->setFixedSize(36, 24);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(QIcon(iconPath));
        btn->setIconSize(QSize(16, 16));
        btn->setToolTip(tip);
        btn->setFocusPolicy(Qt::NoFocus);
        return btn;
    };
    m_chromeMin   = makeChrome("ChromeMin",   ":/icons/chrome_min.svg",   "Minimize");
    m_chromeMax   = makeChrome("ChromeMax",   ":/icons/chrome_max.svg",   "Maximize");
    m_chromeClose = makeChrome("ChromeClose", ":/icons/chrome_close.svg", "Close");

    connect(m_chromeMin,   &QPushButton::clicked, this, &MainWindow::showMinimized);
    connect(m_chromeMax,   &QPushButton::clicked, this, &MainWindow::onChromeMaximizeToggle);
    connect(m_chromeClose, &QPushButton::clicked, this, &MainWindow::close);

    rightLayout->addWidget(m_chromeMin,   0, Qt::AlignVCenter);
    rightLayout->addWidget(m_chromeMax,   0, Qt::AlignVCenter);
    rightLayout->addWidget(m_chromeClose, 0, Qt::AlignVCenter);

    layout->addWidget(rightSlot);

    // Width-mirror: leftSlot fixed to rightSlot's preferred width once
    // child widgets have published their sizeHints (next event-loop tick).
    // Re-runs on every activatePage() since right-slot content (Organise btn)
    // changes visibility per page — without re-mirroring the central nav pills
    // shift off-center when entering Videos/Organise mode.
    QTimer::singleShot(0, this, [this]() { mirrorTopBarSlotWidths(); });

    m_topBar = bar;
}

void MainWindow::mirrorTopBarSlotWidths()
{
    if (!m_topBarLeftSlot || !m_topBarRightSlot)
        return;
    // Both slots take the WIDER of the two sizeHints, so the central nav
    // pills stay geometrically centered even after either side grows.
    // GLOBAL_NAV_HISTORY Task 4 (2026-05-14) grew the left slot by ~76px
    // when the Back / Forward chevrons landed, which previously would
    // have clipped them under the right-mirror-only rule.
    const int leftHint  = m_topBarLeftSlot->sizeHint().width();
    const int rightHint = m_topBarRightSlot->sizeHint().width();
    const int target    = qMax(leftHint, rightHint);
    m_topBarLeftSlot->setFixedWidth(target);
    m_topBarRightSlot->setFixedWidth(target);
}

// ── Page stack ──────────────────────────────────────────────────────────────
void MainWindow::buildPageStack()
{
    auto dbg = [](const char* msg) {
        DebugLogBuffer::instance().info("mainwindow", QString::fromLatin1(msg));
    };

    m_pageStack = new QStackedWidget(this);
    dbg("4a-pagestack-created");

    auto *comicsPage = new ComicsPage(m_bridge);
    m_pageStack->addWidget(comicsPage);
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- ComicsPage emits a full
    // LayerEntry per in-page transition. The controller pushes onto the
    // "comics" stack; the topbar Back chevron then has a destination to
    // walk to. Cross-mode pill clicks reset the comics stack to [].
    connect(comicsPage, &ComicsPage::enteredLayer, this,
            [this](const tankoban::ui::LayerEntry& e) {
                if (m_navController) m_navController->pushLayer(e.pageId, e);
            });
    connect(comicsPage, &ComicsPage::exitedLayer, this, [this]() {
        if (m_navController) m_navController->popLayer(QStringLiteral("comics"));
    });
    dbg("4b-comicspage-created");

    auto *booksPage = new BooksPage(m_bridge);
    m_pageStack->addWidget(booksPage);
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- minimal layer wiring for
    // Books (no internal deep state today).
    connect(booksPage, &BooksPage::enteredLayer, this,
            [this](const tankoban::ui::LayerEntry& e) {
                if (m_navController) m_navController->pushLayer(e.pageId, e);
            });
    connect(booksPage, &BooksPage::exitedLayer, this, [this]() {
        if (m_navController) m_navController->popLayer(QStringLiteral("books"));
    });
    dbg("4c-bookspage-created");

    m_videosPage = new VideosPage(m_bridge);
    m_pageStack->addWidget(m_videosPage);
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- minimal layer wiring for
    // Videos. ShowView sub-page reached via cross-mode pill, not layer push.
    connect(m_videosPage, &VideosPage::enteredLayer, this,
            [this](const tankoban::ui::LayerEntry& e) {
                if (m_navController) m_navController->pushLayer(e.pageId, e);
            });
    connect(m_videosPage, &VideosPage::exitedLayer, this, [this]() {
        if (m_navController) m_navController->popLayer(QStringLiteral("videos"));
    });
    dbg("4d-videospage-created");

    m_organisePage = new OrganisePage(m_bridge);
    m_pageStack->addWidget(m_organisePage);
    connect(m_organisePage, &OrganisePage::backToVideosRequested, this, [this]() {
        activatePage(PAGE_VIDEOS);
    });
    connect(m_organisePage, &OrganisePage::assignmentsChanged, this, [this]() {
        if (m_videosPage)
            m_videosPage->refreshFromCategoryStore();
        if (m_organisePage && m_videosPage)
            m_organisePage->setShows(m_videosPage->currentShows());
    });
    connect(m_videosPage, &VideosPage::categoryAssignmentsChanged, this, [this]() {
        if (m_organisePage && m_videosPage)
            m_organisePage->setShows(m_videosPage->currentShows());
    });
    dbg("4d2-organisepage-created");

    // TorrentClient (shared by StreamPage, VideosPage, TankorentPage,
    // TankoLibraryPage). Hoisted at MainWindow scope post-SOURCES_SIDEBAR.
    auto *torrentClient = new TorrentClient(m_bridge, this);
    m_torrentClient = torrentClient;
    dbg("4e-torrentclient-created");

    // STREAM_DOWNLOADED_LIBRARY 2026-05-10 Phase 2 — wire the stream-side
    // download index into TorrentClient so onFileRenamed() registers each
    // published bulk-episode file as it lands. Both objects exist by now
    // (m_streamDownloadIndex was constructed at line ~180; TorrentClient just
    // created above). Non-owning pointer; nullptr-tolerant on the receiver.
    if (m_streamDownloadIndex)
        torrentClient->setStreamDownloadIndex(m_streamDownloadIndex);

    // TORRENT_PERSISTENCE_COLLAPSE Phase 3.4 (2026-05-20) — inject the SQLite
    // repository the StreamDownloadIndex now reads from / writes through to.
    // Triggers a one-time load() that rebuilds the in-memory byPath/byEpisode/
    // imdbHasAny maps from the stream_downloads_index table populated by the
    // Phase 1.6 first-boot importer (and kept in sync by the index's own
    // per-mutation upserts going forward).
    if (m_streamDownloadIndex)
        m_streamDownloadIndex->setRepository(&torrentClient->repository());

    // Stream page — m_streamPage cache (STREAM_ADD_TO_TANKORENT 2026-05-06)
    // so we can wire the magnet-handoff signal without a qobject_cast walk.
    m_streamPage = new StreamPage(m_bridge, torrentClient);
    m_pageStack->addWidget(m_streamPage);
    dbg("4f-streampage-created");

    // STREAM_DOWNLOADED_LIBRARY 2026-05-10 Phase 3 — wire the download
    // index into StreamPage so (a) the home board's tiles render the
    // DOWNLOADED chip and refresh on entriesChanged, and (b) StreamLibrary's
    // remove() evicts per-episode rows when a show leaves the library.
    if (m_streamPage && m_streamDownloadIndex)
        m_streamPage->setStreamDownloadIndex(m_streamDownloadIndex);

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL 2026-05-12 Phase 7 — wire the
    // TorrentClient into StreamLibrary so remove(imdb) can engine-cancel
    // any active cohort for the show before clearing the entry. Closes
    // spec §9.3. UI-level confirmation prompts live at the remove() call
    // site; this is the engine guarantee that any caller gets the
    // cancel-with-delete behavior consistently.
    if (m_streamPage && m_streamPage->streamLibrary())
        m_streamPage->streamLibrary()->setTorrentClient(torrentClient);

    // STREAM_DOWNLOADED_LIBRARY 2026-05-10 Phase 5 — wire the download index
    // into VideosPage so the Videos scanner skips Stream-owned files (spec
    // §3 P1 + §6.4 + §8) and the page rescans (debounced 500ms) when the
    // index changes (bulk completion / Remove-from-Library batches).
    if (m_videosPage && m_streamDownloadIndex)
        m_videosPage->setStreamDownloadIndex(m_streamDownloadIndex);

    // Share StreamPage's MetaAggregator with VideosPage for "Fetch poster
    // from internet" context-menu action on folder tiles (Agent 5 Batch 1,
    // per HELP.md 2026-04-15 handshake with Agent 4).
    m_videosPage->setMetaAggregator(m_streamPage->metaAggregator());

    // Share TorrentClient with VideosPage so the (auto-)rename path can
    // release any active libtorrent record before the folder is moved on
    // disk — without this libtorrent silently re-creates the original
    // folder + re-downloads, producing the "multiplying folders" symptom.
    m_videosPage->setTorrentClient(torrentClient);

    // TANKOYOMI_PREMIUM Phase 3 (2026-05-15) — same pattern as VideosPage
    // above: hand ComicsPage the shared TorrentClient so its internal
    // TorrentVolumeProvider can reach TorrentEngine for premium-volume
    // downloads. ComicsPage caches both ledger + provider; signals fire on
    // metadataReady / pieceFinished / torrentError via queued connections.
    comicsPage->setTorrentClient(torrentClient);

    m_tankorentPage = new TankorentPage(m_bridge, torrentClient);
    m_tankorentPage->setObjectName(PAGE_TANKORENT);
    m_pageStack->addWidget(m_tankorentPage);
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- minimal layer wiring for
    // Tankorent (search + results on same surface, no deep layers today).
    connect(m_tankorentPage, &TankorentPage::enteredLayer, this,
            [this](const tankoban::ui::LayerEntry& e) {
                if (m_navController) m_navController->pushLayer(e.pageId, e);
            });
    connect(m_tankorentPage, &TankorentPage::exitedLayer, this, [this]() {
        if (m_navController) m_navController->popLayer(QStringLiteral("tankorent"));
    });
    dbg("4g-tankorentpage-created");

    // STREAM_ADD_TO_TANKORENT (2026-05-06) — cross-page magnet hand-off.
    // Connect after both pages exist; routes the right-click "Add torrent
    // to Tankorent" action through MainWindow's nav layer rather than
    // having StreamPage talk directly to TankorentPage (cross-page
    // coordination convention).
    connect(m_streamPage, &StreamPage::addToTankorentRequested,
            this, &MainWindow::onAddToTankorentRequested);
    connect(m_streamPage, &StreamPage::streamBulkDispatchRequested,
            this, &MainWindow::onStreamBulkDispatchRequested);
    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — downloaded-episode
    // click. StreamPage has already kicked the SubtitlesAggregator fan-out
    // by the time this fires; MainWindow's slot handles the VideoPlayer open.
    connect(m_streamPage, &StreamPage::playLocalFileFromStreamRequested,
            this, &MainWindow::onPlayLocalFileFromStreamRequested);
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- per-mode controller wiring
    // for Stream. Mirror of the Comics wiring above (Task 8 pattern).
    connect(m_streamPage, &StreamPage::enteredLayer, this,
            [this](const tankoban::ui::LayerEntry& e) {
                if (m_navController) m_navController->pushLayer(e.pageId, e);
            });
    connect(m_streamPage, &StreamPage::exitedLayer, this, [this]() {
        if (m_navController) m_navController->popLayer(QStringLiteral("stream"));
    });

    auto *tankoLibraryPage = new TankoLibraryPage(m_bridge, torrentClient);
    tankoLibraryPage->setObjectName(PAGE_TANKOLIBRARY);
    m_pageStack->addWidget(tankoLibraryPage);
    dbg("4g3-tankolibrarypage-created");

    // STREAM_DOWNLOADS_SIDEBAR_PAGE 2026-05-25 - aggregate Theatre downloads
    // view accessible from SidebarDrawer's "Downloads" entry. Reads from
    // TorrentClient::streamBulkGroups (active) + StreamDownloadIndex (history).
    m_streamDownloadsPage = new StreamDownloadsPage(this);
    m_streamDownloadsPage->setObjectName(PAGE_STREAM_DOWNLOADS);
    m_streamDownloadsPage->setTorrentClient(torrentClient);
    if (m_streamPage) {
        m_streamDownloadsPage->setStreamDownloadIndex(m_streamPage->streamDownloadIndex());
    }
    m_pageStack->addWidget(m_streamDownloadsPage);
    connect(m_streamDownloadsPage, &StreamDownloadsPage::backRequested, this, [this]() {
        activatePage(PAGE_STREAM);
    });
    connect(m_streamDownloadsPage, &StreamDownloadsPage::playLocalFileRequested,
            this, &MainWindow::onPlayLocalFileFromStreamRequested);
    dbg("4g4-streamdownloadspage-created");
    dbg("4h-pagestack-complete");
}

// ── Keyboard shortcuts ──────────────────────────────────────────────────────
void MainWindow::bindShortcuts()
{
    auto bind = [this](const QKeySequence &seq, const char *pageId) {
        auto *sc = new QShortcut(seq, this);
        QString id = pageId;
        connect(sc, &QShortcut::activated, this, [this, id]() {
            activatePage(id);
        });
    };

    bind(QKeySequence("Ctrl+1"), PAGE_COMICS);
    bind(QKeySequence("Ctrl+2"), PAGE_BOOKS);
    bind(QKeySequence("Ctrl+3"), PAGE_VIDEOS);
    bind(QKeySequence("Ctrl+4"), PAGE_STREAM);
    auto* sidebarShortcut = new QShortcut(QKeySequence("Ctrl+5"), this);
    connect(sidebarShortcut, &QShortcut::activated, this, [this]() {
        if (m_sidebar)
            m_sidebar->toggle();
    });

    // F11 fullscreen toggle
    auto *fs = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(fs, &QShortcut::activated, this, [this]() {
        // Reader/player overlays own F11 themselves. Letting the top-level
        // window shortcut fire at the same time causes a double-toggle on
        // one keypress (overlay enters fullscreen, MainWindow bounces back to
        // maximized), which leaves the taskbar visible and clips bottom-edge
        // video HUD/content on Windows.
        if ((m_comicReader && m_comicReader->isVisible())
            || (m_bookReader && m_bookReader->isVisible())
            || (m_videoPlayer && m_videoPlayer->isVisible())) {
            return;
        }
        if (isFullScreen())
            showMaximized();
        else
            showFullScreen();
    });

    // GLOBAL_NAV_HISTORY Task 5 — Global Back / Forward keyboard shortcuts
    // (spec §3.5). Qt::ApplicationShortcut context so they fire from any
    // focused widget. Plain LeftArrow / RightArrow are consumed by text
    // inputs for cursor movement; Alt+arrow is free.
    auto* backShortcut = new QShortcut(QKeySequence("Alt+Left"), this);
    backShortcut->setContext(Qt::ApplicationShortcut);
    connect(backShortcut, &QShortcut::activated,
            this, &MainWindow::onBackChevronClicked);

    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- Alt+Right shortcut
    // unbound per spec B2 ("Unbind cleanly -- does nothing"). Forward
    // chevron is physically gone; no remap to any other action.
}

// ── Page activation ─────────────────────────────────────────────────────────
void MainWindow::activatePage(const QString &pageId)
{
    if (pageId == m_activePageId)
        return;

    const QString previousPageId = m_activePageId;
    if (!previousPageId.isEmpty()) {
        JsonlEventLog::instance().emitEvent(
            QStringLiteral("ui.layer_exited"),
            QStringLiteral("page_deactivated"),
            QJsonObject{{QStringLiteral("pageId"), previousPageId}});
    }
    m_activePageId = pageId;
    JsonlEventLog::instance().emitEvent(
        QStringLiteral("ui.layer_entered"),
        QStringLiteral("page_activated"),
        QJsonObject{{QStringLiteral("pageId"), pageId},
                    {QStringLiteral("previousPageId"), previousPageId}});
    if (m_navController) m_navController->setActiveMode(pageId);

    for (auto &nav : m_navButtons) {
        nav.button->setChecked(nav.pageId == pageId);
    }
    if (m_organiseBtn)
        m_organiseBtn->setVisible(pageId == PAGE_VIDEOS || pageId == PAGE_ORGANISE);
    // Right-slot width changes when Organise btn shows/hides — re-mirror
    // leftSlot via the next event-loop tick so nav pills stay centered.
    QTimer::singleShot(0, this, [this]() { mirrorTopBarSlotWidths(); });

    for (int i = 0; i < m_pageStack->count(); ++i) {
        if (m_pageStack->widget(i)->objectName() == pageId) {
            m_pageStack->setCurrentIndex(i);
            // Activate page on switch
            if (auto *comics = qobject_cast<ComicsPage*>(m_pageStack->widget(i)))
                comics->activate();
            if (auto *books = qobject_cast<BooksPage*>(m_pageStack->widget(i)))
                books->activate();
            if (auto *videos = qobject_cast<VideosPage*>(m_pageStack->widget(i)))
                videos->activate();
            if (auto *organise = qobject_cast<OrganisePage*>(m_pageStack->widget(i))) {
                if (m_videosPage)
                    organise->setShows(m_videosPage->currentShows());
                organise->activate();
            }
            if (auto *stream = qobject_cast<StreamPage*>(m_pageStack->widget(i)))
                stream->activate();
            if (m_sidebar)
                m_sidebar->setActiveSource(pageId);
            break;
        }
    }

}

// ── PHASE 0 NAV CONTRACT RESTORE (Agent 5, 2026-05-17) ─────────────────────
// Standing Tankoban contract: topbar mode pills are end-all-be-all hard
// resets. activatePage early-returns when pageId == m_activePageId, which
// silently swallows pill clicks while the user is in a deep sub-view
// (Comics search results, Comics series view, Stream detail, etc.). The
// pill click handler routes the same-page case through this method so the
// user always lands back on the mode root. Cross-mode pill clicks keep
// using activatePage's normal page-swap path. Polymorphic dispatch by
// page type — pages without an internal deep state are a no-op here
// (already on root). Phase 1+ may extend this when additional pages grow
// sub-stacks.
void MainWindow::resetActivePageToRoot() {
    if (!m_pageStack) return;
    QWidget* cur = m_pageStack->currentWidget();
    if (!cur) return;
    if (auto* comics = qobject_cast<ComicsPage*>(cur)) {
        comics->resetToRoot();
        return;
    }
    if (auto* stream = qobject_cast<StreamPage*>(cur)) {
        stream->resetToRoot();
        return;
    }
    // BooksPage / VideosPage / OrganisePage / TankorentPage / TankoLibraryPage:
    // no internal deep state today; pill-reset is a structural no-op.
}

// ── Global Nav (GLOBAL_NAV_HISTORY Task 4) ──────────────────────────────────

bool MainWindow::isReaderOrPlayerActive() const {
    // Reader/player widgets are kept around between opens; "active"
    // means visible on top of m_pageStack.
    if (m_comicReader && m_comicReader->isVisible()) return true;
    if (m_bookReader  && m_bookReader->isVisible())  return true;
    if (m_videoPlayer && m_videoPlayer->isVisible()) return true;
    return false;
}

void MainWindow::onBackChevronClicked() {
    // Spec §3.12: no-op while modal dialog is open.
    if (QApplication::activeModalWidget()) return;
    // Spec §3.10: Back closes the reader/player if one is active.
    // Reader/player are not history entries — they're modal overlays.
    if (isReaderOrPlayerActive()) {
        if (m_comicReader && m_comicReader->isVisible()) {
            closeComicReader();
            return;
        }
        if (m_bookReader && m_bookReader->isVisible()) {
            closeBookReader();
            return;
        }
        if (m_videoPlayer && m_videoPlayer->isVisible()) {
            closeVideoPlayer();
            return;
        }
    }
    if (m_navController) m_navController->goBack(m_activePageId);
}

// PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- onForwardChevronClicked
// removed. The Forward chevron is gone, the Alt+Right shortcut is unbound,
// and Qt::ForwardButton (mouse btn 5) input is silently swallowed in
// mousePressEvent below. No code path reaches forward semantics.

void MainWindow::onBackAvailabilityChanged(bool available) {
    if (m_backBtn) {
        const bool inModal = (QApplication::activeModalWidget() != nullptr);
        const bool inOverlay = isReaderOrPlayerActive();
        m_backBtn->setEnabled(available && !inModal && !inOverlay);
    }
}

// PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- onForwardAvailabilityChanged
// removed. Forward chevron no longer exists; nothing to drive.

// ── PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- PerModeNavController slots ──

void MainWindow::onLayerRestoreRequested(const tankoban::ui::LayerEntry& target) {
    // Dispatch the restore back to the originating page. Each page is
    // responsible for honoring the target in its own restoreLayer slot.
    // Comics is wired in Task 8; other pages (stream, books, videos,
    // tankorent) are handled in Tasks 9-10.
    if (target.pageId == QStringLiteral("comics")) {
        if (auto* comics = m_pageStack->findChild<ComicsPage*>())
            comics->restoreLayer(target);
        return;
    }
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- Task 9: Stream page restore.
    if (target.pageId == QStringLiteral("stream") && m_streamPage) {
        m_streamPage->restoreLayer(target);
        return;
    }
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- Task 10: Books / Videos /
    // Tankorent. Each restoreLayer is a no-op today (no deep state); the
    // dispatch exists so future deep-view additions can plug in without
    // touching MainWindow.
    if (target.pageId == QStringLiteral("books") && m_pageStack) {
        if (auto* p = m_pageStack->findChild<BooksPage*>()) p->restoreLayer(target);
        return;
    }
    if (target.pageId == QStringLiteral("videos") && m_videosPage) {
        m_videosPage->restoreLayer(target);
        return;
    }
    if (target.pageId == QStringLiteral("tankorent") && m_tankorentPage) {
        m_tankorentPage->restoreLayer(target);
        return;
    }
}

void MainWindow::onBackDestinationLabelChanged(const QString& label) {
    if (!m_backBtn) return;
    if (label.isEmpty()) {
        m_backBtn->setToolTip(QStringLiteral("Back (Alt+Left)"));
    } else {
        m_backBtn->setToolTip(QStringLiteral("Back to %1 (Alt+Left)").arg(label));
    }
}

// ── STREAM_ADD_TO_TANKORENT cross-page hand-off ─────────────────────────────
// (2026-05-06) StreamPage emits addToTankorentRequested after the user
// right-clicks a magnet stream card and picks "Add torrent to Tankorent".
// This handler activates the Tankorent page and forwards the magnet through
// TankorentPage's existing addMagnetBatch path (wrapped as
// addMagnetFromExternal). Page-switch only — does NOT pause/teardown any
// active stream playback session.
//
// Routing simplified vs. Agent 8's wake brief: brief assumed StreamPage →
// MainWindow → SourcesPage → TankorentPage (4 hops). SourcesPage was
// `git rm`'d in the SOURCES_SIDEBAR REPLACEMENT ship 2026-05-05;
// TankorentPage is now a peer page in m_pageStack, so the nav goes
// straight there.
void MainWindow::onAddToTankorentRequested(const QString& magnetUri,
                                           const QString& displayName)
{
    if (magnetUri.isEmpty()) {
        qWarning() << "MainWindow::onAddToTankorentRequested: empty magnet, ignoring";
        return;
    }
    if (!m_tankorentPage) {
        qWarning() << "MainWindow::onAddToTankorentRequested: m_tankorentPage null";
        return;
    }

    activatePage(PAGE_TANKORENT);
    m_tankorentPage->addMagnetFromExternal(magnetUri, displayName);
}

// STREAM_DOWNLOADS_NETFLIX_OVERHAUL 2026-05-12 Phase 6 — renamed from
// onAddToTankorentBulkRequested. The slot name now reflects what it
// actually does: dispatch a stream bulk group to the engine via the
// existing TankorentPage::addMagnetGroupFromExternal pass-through. There
// is no page-switch (preserves the V2 Phase 1 stay-on-current-page
// behavior); Tankorent's UI no longer renders these groups (filter in
// TankorentPage::refreshTransfers from Phase 2 Task 8). The single-add
// path onAddToTankorentRequested keeps the activate-to-Tankorent
// behavior because that flow IS a distinct user gesture (manual
// right-click "Add torrent to Tankorent" on a single stream source).
void MainWindow::onStreamBulkDispatchRequested(
    const StreamBulkGroupRecord& group,
    const tankostream::stream::BulkPackVerificationResult& verifierOutput,
    const QString& displayLabel)
{
    if (group.groupId.isEmpty()) {
        qWarning() << "MainWindow::onStreamBulkDispatchRequested: empty group id, ignoring";
        return;
    }
    if (!m_tankorentPage) {
        qWarning() << "MainWindow::onStreamBulkDispatchRequested: m_tankorentPage null";
        return;
    }

    m_tankorentPage->addMagnetGroupFromExternal(group, verifierOutput, displayLabel);
}

// ── Root folders overlay ────────────────────────────────────────────────────
QString MainWindow::domainForPage(const QString& pageId) const
{
    if (pageId == PAGE_COMICS)  return "comics";
    if (pageId == PAGE_BOOKS)   return "books";
    if (pageId == PAGE_VIDEOS)  return "videos";
    if (pageId == PAGE_ORGANISE) return "videos";
    if (pageId == PAGE_TANKORENT || pageId == PAGE_TANKOLIBRARY)
        return "sources";
    return "";
}

void MainWindow::showRootFolders()
{
    QString domain = domainForPage(m_activePageId);
    if (domain.isEmpty())
        domain = "comics"; // fallback

    m_rootFoldersOverlay->refresh(domain);
    m_rootFoldersOverlay->setGeometry(centralWidget()->rect());
    m_rootFoldersOverlay->show();
    m_rootFoldersOverlay->raise();
    m_rootFoldersOverlay->setFocus();
}

void MainWindow::hideRootFolders()
{
    m_rootFoldersOverlay->hide();
}

// ── System tray ─────────────────────────────────────────────────────────────
void MainWindow::setupTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    QIcon icon = QApplication::windowIcon();
    if (icon.isNull())
        icon = windowIcon();

    m_trayMenu = new QMenu(this);
    auto* showAction = m_trayMenu->addAction("Show Tankoban");
    m_trayMenu->addSeparator();
    auto* quitAction = m_trayMenu->addAction("Quit Tankoban");

    connect(showAction, &QAction::triggered, this, &MainWindow::restoreFromTray);
    connect(quitAction, &QAction::triggered, this, &MainWindow::quitFromTray);

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip("Tankoban");
    m_trayIcon->setContextMenu(m_trayMenu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger ||
            reason == QSystemTrayIcon::DoubleClick) {
            restoreFromTray();
        }
    });

    m_trayIcon->show();

    // If tray failed to show, clean up
    if (!m_trayIcon->isVisible()) {
        delete m_trayIcon;
        m_trayIcon = nullptr;
        delete m_trayMenu;
        m_trayMenu = nullptr;
    }
}

void MainWindow::hideToTray()
{
    m_wasMaximizedBeforeHide = isMaximized();
    hide();
}

void MainWindow::restoreFromTray()
{
    bringToFront();
}

void MainWindow::quitFromTray()
{
    // REPO_HYGIENE P1.5 (2026-04-26): m_quitRequested flag removed — closeEvent
    // unconditionally calls QApplication::quit so the prior flag-set was dead
    // state. Tray "Quit" menu now just exits cleanly via the standard close path.
    close();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Stop video playback immediately so sidecar audio dies before destructors run
    if (m_videoPlayer)
        m_videoPlayer->stopPlayback();

    // Dispose tray
    if (m_trayIcon) {
        m_trayIcon->hide();
        m_trayIcon->deleteLater();
        m_trayIcon = nullptr;
    }
    if (m_trayMenu) {
        m_trayMenu->deleteLater();
        m_trayMenu = nullptr;
    }

    QMainWindow::closeEvent(event);
    QApplication::quit();
}

// ── Comic reader ────────────────────────────────────────────────────────────
void MainWindow::openComicReader(const QString& cbzPath, const QStringList& seriesCbzList, const QString& seriesName)
{
    // Set geometry and show BEFORE opening book so viewport has valid dimensions
    m_comicReader->setGeometry(centralWidget()->rect());
    m_comicReader->show();
    m_comicReader->raise();
    // Open book after widget is visible and has real geometry
    m_comicReader->openBook(cbzPath, seriesCbzList, seriesName);
    m_comicReader->setFocus();
    // GLOBAL_NAV_HISTORY Task 6: Back stays enabled to close the reader;
    // Forward disabled while overlay is up. Restored in closeComicReader.
    if (m_backBtn)    m_backBtn->setEnabled(true);
    // PHASE 1 NAV REDESIGN -- m_forwardBtn removed; no-op.
}

void MainWindow::closeComicReader()
{
    // Hide reader FIRST so the library is already visible during the window-state
    // restore — avoids a visible flash of the reader resizing from fullscreen to
    // maximized before it disappears.
    m_comicReader->hide();
    // Exit fullscreen if we're in it — library has no fullscreen mode of its own
    if (isFullScreen()) {
        if (m_wasMaximizedBeforeFullscreen)
            showMaximized();
        else
            showNormal();
    }
    // Restore chevron state now that the overlay is gone.
    if (m_backBtn && m_navController)
        m_backBtn->setEnabled(m_navController->canGoBack(m_activePageId));
}

// ── Book reader ──────────────────────────────────────────────────────────────
void MainWindow::openBookReader(const QString& filePath)
{
    m_bookReader->openBook(filePath);
    m_bookReader->setGeometry(centralWidget()->rect());
    m_bookReader->show();
    m_bookReader->raise();
    m_bookReader->setFocus();
    // GLOBAL_NAV_HISTORY Task 6: Back stays enabled to close the reader;
    // Forward disabled while overlay is up. Restored in closeBookReader.
    if (m_backBtn)    m_backBtn->setEnabled(true);
    // PHASE 1 NAV REDESIGN -- m_forwardBtn removed; no-op.
}

void MainWindow::closeBookReader()
{
    m_bookReader->hide();
    // Restore chevron state now that the overlay is gone.
    if (m_backBtn && m_navController)
        m_backBtn->setEnabled(m_navController->canGoBack(m_activePageId));
}

// ── Video player ─────────────────────────────────────────────────────────────
void MainWindow::openVideoPlayer(const QString& filePath)
{
    openVideoPlayerWithOptions(filePath, 0.0, {});
}

void MainWindow::openVideoPlayerWithOptions(const QString& filePath,
                                            double startPositionSec,
                                            const QString& displayTitle)
{
    // CW_NAMESPACE_BOUNDARY 2026-05-13 — this is the library-videos
    // entry point. Tear down any stream-mode progress wiring left
    // over from a prior downloaded-stream playback and reset
    // persistence to LibraryVideos so progress lands in the "videos"
    // domain as expected. onPlayLocalFileFromStreamRequested rewires
    // both these knobs AFTER calling openVideoPlayer for the
    // downloaded-stream case.
    if (m_streamPlaybackProgressConn) {
        QObject::disconnect(m_streamPlaybackProgressConn);
        m_streamPlaybackProgressConn = QMetaObject::Connection{};
    }
    if (m_videoPlayer)
        m_videoPlayer->setPersistenceMode(VideoPlayer::PersistenceMode::LibraryVideos);

    m_videoPlayer->openFile(filePath, {}, 0, startPositionSec, displayTitle);
    m_videoPlayer->setGeometry(centralWidget()->rect());
    m_videoPlayer->show();
    m_videoPlayer->raise();
    m_videoPlayer->setFocus();
    // GLOBAL_NAV_HISTORY Task 6: Back stays enabled to close the player;
    // Forward disabled while overlay is up. Restored in closeVideoPlayer.
    if (m_backBtn)    m_backBtn->setEnabled(true);
    // PHASE 1 NAV REDESIGN -- m_forwardBtn removed; no-op.
}

// STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — Stream-mode playback
// gateway for bulk-downloaded episodes. Spec §6.2.
//
// StreamPage has already done the SubtitlesAggregator fan-out (Spec §10.2)
// against a synthetic Stream by the time this slot fires; MainWindow's
// responsibility is the actual VideoPlayer open. The imdbId/season/episode
// args are kept on the signal so a future iteration can teach VideoPlayer
// to write Stream-domain progress under the canonical
// `stream:imdb:sNN:eMM` key (Spec §10.1 Continue Watching surfacing) — that
// bridging is a separable follow-up; the open + signal flow is the Phase 4
// critical path.
void MainWindow::onPlayLocalFileFromStreamRequested(
    const QString& localPath, const QString& imdbId,
    const QString& showTitle, int season, int episode)
{
    if (localPath.isEmpty() || !QFileInfo::exists(localPath)) {
        DebugLogBuffer::instance().warning(
            QStringLiteral("stream"),
            QStringLiteral("onPlayLocalFileFromStreamRequested: file missing"),
            QJsonObject{{QStringLiteral("path"), localPath}});
        return;
    }

    // CW_NAMESPACE_BOUNDARY 2026-05-13 — downloaded-stream playback
    // must NOT pollute Video-mode CW. Pre-fix: imdbId/season/episode
    // were Q_UNUSED'd and the player ran in default
    // PersistenceMode::LibraryVideos; the file exists on disk so
    // videoIdForFile() resolved and VideoPlayer::saveProgress
    // (line ~3193) wrote to the "videos" domain on every tick,
    // causing the show to appear in Video-mode Continue Watching.
    // Stream-side saveProgress("stream", epKey, ...) never fired
    // because StreamPage::onReadyToPlay (which installs that
    // subscriber for HTTP playback) is bypassed by the Layer 3
    // Rule C auto-play path.
    //
    // Two-step rewire below — order matters because openVideoPlayer
    // resets both knobs to library-videos defaults.
    //
    //   (1) openVideoPlayer first — this disconnects any prior
    //       stream-progress conn and sets PersistenceMode to
    //       LibraryVideos.
    //   (2) Override PersistenceMode to None — suppresses the
    //       "videos" write at VideoPlayer.cpp:3193.
    //   (3) Connect progressUpdated to a lambda that writes to
    //       "stream" with the same epKey + makeWatchState pipeline
    //       StreamPage's HTTP flow uses (StreamPage.cpp:2972).
    //       Lambda captures bridge + epKey by value; the stored
    //       QMetaObject::Connection is torn down on the next library
    //       playback or downloaded-stream playback via the
    //       openVideoPlayer reset.
    const bool isSeries = (season > 0 && episode > 0);
    const QString epKey = isSeries
        ? StreamProgress::episodeKey(imdbId, season, episode)
        : StreamProgress::movieKey(imdbId);

    double streamResumeSec = 0.0;
    if (m_bridge && !epKey.isEmpty()) {
        const QJsonObject prog = m_bridge->progress(QStringLiteral("stream"), epKey);
        const double savedPos = prog.value(QStringLiteral("positionSec")).toDouble(0.0);
        const double savedDur = prog.value(QStringLiteral("durationSec")).toDouble(0.0);
        if (savedPos > 2.0 && savedDur > 0.0 && savedPos < savedDur * 0.95)
            streamResumeSec = savedPos;
    }

    const QString displayTitle = isSeries
        ? QStringLiteral("%1 S%2E%3")
            .arg(showTitle)
            .arg(season, 2, 10, QLatin1Char('0'))
            .arg(episode, 2, 10, QLatin1Char('0'))
        : showTitle;
    openVideoPlayerWithOptions(localPath, streamResumeSec, displayTitle);

    m_videoPlayer->setPersistenceMode(VideoPlayer::PersistenceMode::None);

    CoreBridge* bridge = m_bridge;
    m_streamPlaybackProgressConn = connect(
        m_videoPlayer, &VideoPlayer::progressUpdated, this,
        [bridge, epKey](const QString& path, double posSec, double durSec) {
            if (!bridge || epKey.isEmpty()) return;
            if (posSec < 5.0) return;   // mirrors StreamPage 5s floor
            const bool finished = (durSec > 0 && posSec / durSec >= 0.9);
            QJsonObject state = StreamProgress::makeWatchState(posSec, durSec, finished);
            state[QStringLiteral("path")] = path;
            bridge->saveProgress(QStringLiteral("stream"), epKey, state);
        });

    // VideoPlayer currently has no public setVideoTitle()/setTitle() entry
    // point — its m_titleLabel + m_fullTitle are private and populated from
    // the file's basename inside openFile(). The Cinemeta-rich
    // "<Show> · S01E03" title is a cosmetic improvement; the canonical
    // filename ("Show - S01E03 - Title.mkv") that openFile derives is
    // already a sensible display string post-Phase-3 canonical-naming.
    // TODO STREAM_DOWNLOADED_LIBRARY follow-up: add VideoPlayer::setVideoTitle
    // and route the rich title here. Out of scope for Phase 4.
}

// CW_NAMESPACE_BOUNDARY 2026-05-13 — one-shot migration sweep called
// once at startup right after m_streamDownloadIndex is constructed.
// Pre-fix, onPlayLocalFileFromStreamRequested left the player in
// PersistenceMode::LibraryVideos so downloaded-stream playback
// persisted progress in video_progress.json under a file-path hash.
// Those entries leaked into Video-mode Continue Watching. The forward
// fix above prevents new writes; this sweep evicts the legacy ones
// and seeds the stream namespace with their position state so
// Stream-mode CW picks up where the user left off.
void MainWindow::migrateLegacyStreamProgressEntries()
{
    if (!m_bridge || !m_streamDownloadIndex) return;

    const QJsonObject allVideos = m_bridge->allProgress(QStringLiteral("videos"));
    if (allVideos.isEmpty()) return;

    // canonicalKey → Entry, built from the index for O(1) path lookup.
    const auto entries = m_streamDownloadIndex->all();
    if (entries.isEmpty()) return;
    QHash<QString, StreamDownloadIndex::Entry> byKey;
    byKey.reserve(entries.size());
    for (const auto& e : entries) {
        byKey.insert(StreamDownloadIndex::computeCanonicalKey(e.canonicalPath), e);
    }

    int migrated = 0;
    int evictedNoSeed = 0;
    for (auto it = allVideos.constBegin(); it != allVideos.constEnd(); ++it) {
        const QJsonObject prog = it.value().toObject();
        const QString path = prog.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) continue;
        const QString canon = StreamDownloadIndex::computeCanonicalKey(path);
        const auto idx = byKey.constFind(canon);
        if (idx == byKey.constEnd()) continue;   // not a downloaded stream

        const QString epKey = idx->type == QLatin1String("movie")
            ? StreamProgress::movieKey(idx->imdbId)
            : StreamProgress::episodeKey(idx->imdbId, idx->season, idx->episode);

        const QJsonObject existingStream =
            m_bridge->progress(QStringLiteral("stream"), epKey);
        const qint64 videosUpdatedAt =
            prog.value(QStringLiteral("updatedAt")).toVariant().toLongLong();
        const qint64 streamUpdatedAt =
            existingStream.value(QStringLiteral("updatedAt")).toVariant().toLongLong();

        if (videosUpdatedAt > streamUpdatedAt) {
            const double posSec  = prog.value(QStringLiteral("positionSec")).toDouble(0.0);
            const double durSec  = prog.value(QStringLiteral("durationSec")).toDouble(0.0);
            const bool finished  = prog.value(QStringLiteral("finished")).toBool(false);
            const QJsonObject seed =
                StreamProgress::makeWatchState(posSec, durSec, finished);
            m_bridge->saveProgress(QStringLiteral("stream"), epKey, seed);
            ++migrated;
        } else {
            ++evictedNoSeed;
        }
        m_bridge->clearProgress(QStringLiteral("videos"), it.key());
    }

    if (migrated > 0 || evictedNoSeed > 0) {
        DebugLogBuffer::instance().info(
            QStringLiteral("stream"),
            QStringLiteral("CW_NAMESPACE_BOUNDARY migration"),
            QJsonObject{
                {QStringLiteral("migratedToStream"), migrated},
                {QStringLiteral("evictedNoSeed"),    evictedNoSeed},
            });
    }
}

// FFMPEG_KEEP_OR_REMOVE_DECISION 2026-05-04 — public wrapper for the
// `--play-file` CLI flag (compare-mpv.bat / compare-ffmpeg.bat entry
// point). Just forwards to the private overload above; exists to keep
// openVideoPlayer itself private.
void MainWindow::openVideoFromCli(const QString& filePath)
{
    openVideoPlayer(filePath);
}

void MainWindow::closeVideoPlayer()
{
    // Stop playback (kills audio)
    m_videoPlayer->stopPlayback();

    // Exit fullscreen if we're in it
    if (isFullScreen()) {
        if (m_wasMaximizedBeforeFullscreen)
            showMaximized();
        else
            showNormal();
    }
    m_videoPlayer->hide();

    // Refresh continue strip after playback ends
    if (auto *videos = m_pageStack->findChild<VideosPage*>())
        videos->refreshContinueOnly();

    // Restore chevron state now that the overlay is gone.
    if (m_backBtn && m_navController)
        m_backBtn->setEnabled(m_navController->canGoBack(m_activePageId));
}

// ── Bring to front (single-instance raise) ──────────────────────────────────
void MainWindow::bringToFront()
{
    // Restore from tray/minimized state
    if (isHidden()) {
        if (m_wasMaximizedBeforeHide)
            showMaximized();
        else
            showNormal();
    } else if (isMinimized()) {
        showMaximized();
    }

    raise();
    activateWindow();

#ifdef Q_OS_WIN
    // Windows often blocks foreground window changes — force it
    HWND hwnd = reinterpret_cast<HWND>(winId());
    SetForegroundWindow(hwnd);
#endif
}

// ── REPO_HYGIENE Phase 3 — dev-control bridge ───────────────────────────────

void MainWindow::enableDevControl()
{
    if (m_devControl)
        return;  // idempotent

    m_devControl = new DevControlServer(this, this);
    if (!m_devControl->start()) {
        DebugLogBuffer::instance().error(
            "devcontrol",
            QStringLiteral("DevControlServer failed to listen on %1")
                .arg(QString::fromLatin1(DevControlServer::kSocketName)));
    } else {
        DebugLogBuffer::instance().info(
            "devcontrol",
            QStringLiteral("DevControlServer listening on %1")
                .arg(QString::fromLatin1(DevControlServer::kSocketName)));
    }

    // Phase D.5 (2026-05-19) — v1.8 synthetic UI layer. Same --dev-control
    // gate as the server itself; write-capable ui_* commands additionally
    // require TANKOBAN_DEV_UI_SIM=1 (enforced in handleDevCommand before
    // forwarding).
    if (!m_uiDispatcher)
        m_uiDispatcher = new UiInteractionDispatcher(this, this);

    // Phase D.6 (2026-05-19) — v1.9 cross-cutting system state +
    // introspection layer (27 commands across 11 prefixes). Same
    // --dev-control gate as the server itself; write-capable commands
    // additionally require TANKOBAN_DEV_WRITE=1 (enforced in handleDevCommand
    // before forwarding). DEV_WRITE is DELIBERATELY DISTINCT from D.5's
    // DEV_UI_SIM — synthetic clicks and state writes / cache busts are
    // independent risk surfaces and should not share a flag.
    if (!m_systemIntrospection)
        m_systemIntrospection = new SystemIntrospection(this, this);
}

QJsonObject MainWindow::devSnapshot() const
{
    QJsonObject snap;
    snap["activePageId"]    = m_activePageId;
    snap["currentPageIndex"] = m_pageStack ? m_pageStack->currentIndex() : -1;
    snap["isFullScreen"]    = isFullScreen();
    snap["isMaximized"]     = isMaximized();
    snap["windowVisible"]   = isVisible();
    snap["videoPlayerVisible"] = m_videoPlayer && m_videoPlayer->isVisible();
    snap["comicReaderVisible"] = m_comicReader && m_comicReader->isVisible();
    snap["bookReaderVisible"]  = m_bookReader && m_bookReader->isVisible();

    QJsonArray nav;
    for (const auto& nb : m_navButtons) {
        QJsonObject o;
        o["pageId"]  = nb.pageId;
        o["checked"] = nb.button ? nb.button->isChecked() : false;
        nav.append(o);
    }
    snap["navButtons"] = nav;
    return snap;
}

bool MainWindow::forwardToDispatch(QObject* page,
                                   const QString& cmd,
                                   const QJsonObject& payload,
                                   QJsonObject& reply)
{
    if (!page)
        return false;

    bool handled = false;
    const bool invoked = QMetaObject::invokeMethod(
        page,
        "dispatchDevCommand",
        Qt::DirectConnection,
        Q_RETURN_ARG(bool, handled),
        Q_ARG(QString, cmd),
        Q_ARG(QJsonObject, payload),
        Q_ARG(QJsonObject&, reply));
    return invoked && handled;
}

QJsonObject MainWindow::handleDevCommand(const QString& cmd, int seq, const QJsonObject& payload)
{
    auto reply = [seq](QJsonObject extras) {
        extras["type"] = "reply";
        extras["seq"]  = seq;
        return extras;
    };
    auto err = [seq](const char* code, const QString& msg) {
        QJsonObject e;
        e["type"]    = "error";
        e["seq"]     = seq;
        e["code"]    = QString::fromLatin1(code);
        e["message"] = msg;
        return e;
    };
    auto comicsPage = [this]() -> ComicsPage* {
        return m_pageStack ? m_pageStack->findChild<ComicsPage*>() : nullptr;
    };

    if (cmd == QLatin1String("ping")) {
        QJsonArray cmds{ "ping","get_state","open_page","scan_videos",
                         "get_videos","play_file","close_player",
                         "get_player","logs","get_torrents","get_library",
                         "get_downloads","get_bulk_groups","search",
                         "dispatch_episode","dispatch_season","dump_ui",
                         "comics_get_state","comics_get_library",
                         "comics_get_series","comics_select_volume",
                         "comics_open_series","comics_open_chapter",
                         "comics_search_tankoyomi","comics_get_downloads",
                         "comics_dispatch_volume","comics_get_sources",
                         // v1.3 books-side bridge (Phase D.1, 2026-05-19).
                         "books_get_state","books_get_library",
                         "books_refresh_library","books_search_library",
                         "books_clear_search","books_open_book",
                         "books_open_series","books_get_series_state",
                         "books_set_sort","books_set_density",
                         "books_get_progress","books_seek_page",
                         "books_set_layout","books_get_chapters",
                         "books_open_chapter","books_tts_state",
                         "books_get_listen_state","books_tts_play",
                         "books_tts_pause","books_tts_resume",
                         "books_tts_stop","books_tts_set_voice",
                         "books_tts_set_speed","books_tts_cancel_stream",
                         // v1.5 sources-side bridge (Phase D.3, 2026-05-19).
                         "sources_search_tankorent",
                         "sources_search_tankolibrary",
                         "sources_get_indexer_health",
                         "sources_get_pending_downloads",
                         "sources_cancel_download",
                         "sources_force_indexer_refresh",
                         "sources_get_tankorent_state",
                         "sources_get_tankolibrary_state",
                         "sources_add_magnet",
                         "sources_add_url",
                         "sources_pause_torrent",
                         "sources_resume_torrent",
                         "sources_remove_torrent",
                         "sources_set_speed_limits",
                         "sources_set_queue_limits",
                         "sources_get_tankolibrary_results",
                         "sources_open_tankolibrary_detail",
                         "sources_download_tankolibrary_selected",
                         "sources_cancel_search",
                         "sources_set_tankolibrary_filters",
                         // v1.6 library-side bridge (Phase D.4, 2026-05-19).
                         "library_get_continue_reading",
                         "library_get_recently_added",
                         "library_get_search_state",
                         "library_apply_theme",
                         "library_get_active_theme",
                         "library_set_density",
                         "library_get_settings",
                         "library_set_setting",
                         "library_get_active_mode_pill",
                         "library_trigger_scan",
                         "library_get_scan_state",
                         "library_get_root_folders",
                         "library_get_active_layer",
                         "library_reset_mode",
                         "library_set_sort",
                         "library_get_sort",
                         "library_get_selected_items",
                         // v1.7 player-side deeper bridge (Phase D.2, 2026-05-19).
                         "player_get_audio_tracks",
                         "player_get_subtitle_tracks",
                         "player_select_audio_track",
                         "player_select_subtitle_track",
                         "player_set_audio_delay",
                         "player_set_sub_delay",
                         "player_set_sub_size",
                         "player_set_sub_position",
                         "player_get_chapters",
                         "player_seek_chapter",
                         "player_set_volume",
                         "player_set_speed",
                         "player_get_hud_state",
                         "player_get_decoder_stats",
                         "player_get_canvas_size",
                         "player_screenshot",
                         "player_simulate_seek_drag",
                         "player_pause",
                         "player_resume",
                         "player_toggle_play",
                         "player_seek",
                         "player_frame_step",
                         "player_stop",
                         "player_set_mute",
                         "player_get_volume_state",
                         "player_set_aspect",
                         "player_set_crop",
                         "player_get_loading_overlay",
                         "player_get_buffering_state",
                         "player_get_keybindings",
                         "sidecar_get_process_state",
                         "sidecar_get_current_stream_info",
                         "sidecar_get_decoder_queue",
                         "sidecar_get_render_queue",
                         "sidecar_restart",
                         "sidecar_get_ipc_latency",
                         "subs_get_active_track",
                         "subs_get_positioning",
                         "subs_get_fonts_loaded",
                         "osd_get_state",
                         // v1.8 synthetic UI interaction layer (Phase D.5,
                         // 2026-05-19). 5 read-only + 9 write-capable. Write-
                         // capable commands gate on TANKOBAN_DEV_UI_SIM=1 or
                         // return UI_SIM_DISABLED.
                         "ui_query_widget",
                         "ui_query_focus",
                         "ui_active_layer",
                         "ui_list_widgets",
                         "ui_dry_run",
                          "ui_click",
                          "ui_keypress",
                          "ui_text_input",
                          "ui_simulate_scroll",
                          "ui_simulate_mouse",
                          "ui_wait_for",
                          "ui_set_checkbox",
                          "ui_set_combo",
                          "ui_select_table_row",
                          // v1.10 lease registry (2026-05-21). Machine-readable
                          // agent lane coordination with token-gated mutation.
                          "lease_acquire",
                          "lease_release",
                          "lease_heartbeat",
                          "lease_get",
                          "lease_list" };
        // v1.9 cross-cutting system state + introspection layer (Phase D.6,
        // 2026-05-19). 27 commands across 11 prefixes. Backed by
        // SystemIntrospection. Write-capable subset (8) gates on
        // TANKOBAN_DEV_WRITE=1 or returns DEV_WRITE_DISABLED. The 12 spec-
        // catalogue commands deferred from v1.9 (network-*/perf-frame/cpu/
        // gpu/scanner-pause/resume/trigger/cache-get-stats/app-trace-signals/
        // jsonstore-dump) are intentionally absent — see DevControlServer.h
        // v1.9 block.
        for (const QString& c : SystemIntrospection::commandList())
            cmds.append(c);
        return reply({
            {"schema",     "tankoban.dev.v1.10"},
            {"appVersion", QApplication::applicationVersion()},
            {"commands",   cmds},
            {"features",   QJsonArray{}}
        });
    }

    if (cmd.startsWith(QLatin1String("lease_"))) {
        if (!m_devControl) {
            return err("INTERNAL",
                QStringLiteral("DevControlServer not initialized; "
                               "enableDevControl() must run first"));
        }
        return m_devControl->handleLeaseCommand(cmd, seq, payload);
    }

    if (cmd == QLatin1String("get_state"))
        return reply({{"snapshot", devSnapshot()}});

    if (cmd == QLatin1String("open_page")) {
        const QString pageId = payload.value("pageId").toString();
        const QStringList valid{"comics","books","videos","organise","stream",
                                "tankorent","tankolibrary","streamDownloads"};
        if (!valid.contains(pageId)) {
            return err("UNKNOWN_PAGE",
                QStringLiteral("pageId '%1' not in [%2]")
                    .arg(pageId, valid.join(',')));
        }
        activatePage(pageId);
        return reply({{"activePageId", m_activePageId}});
    }

    if (cmd == QLatin1String("scan_videos")) {
        if (!m_videosPage)
            return err("INTERNAL", "VideosPage not initialized");
        m_videosPage->triggerScan();
        return reply({{"triggered", true}});
    }

    if (cmd == QLatin1String("get_videos")) {
        if (!m_videosPage)
            return err("INTERNAL", "VideosPage not initialized");
        const int limit = payload.value("limit").toInt(50);
        return reply({{"snapshot", m_videosPage->devSnapshot(limit)}});
    }

    if (cmd == QLatin1String("play_file")) {
        const QString path = payload.value("path").toString();
        if (path.isEmpty())
            return err("BAD_REQUEST", "payload.path required (non-empty string)");
        if (!QFileInfo::exists(path))
            return err("BAD_REQUEST",
                QStringLiteral("file does not exist: %1").arg(path));
        openVideoPlayer(path);
        return reply({{"opened", true}, {"path", path}});
    }

    if (cmd == QLatin1String("close_player")) {
        if (!m_videoPlayer || !m_videoPlayer->isVisible())
            return reply({{"closed", true}, {"alreadyClosed", true}});
        closeVideoPlayer();
        return reply({{"closed", true}});
    }

    if (cmd == QLatin1String("get_player")) {
        if (!m_videoPlayer || !m_videoPlayer->isVisible())
            return reply({{"snapshot", QJsonValue::Null}});
        return reply({{"snapshot", m_videoPlayer->devSnapshot()}});
    }

    if (cmd == QLatin1String("get_torrents")) {
        if (!m_torrentClient)
            return err("INTERNAL", "TorrentClient not initialized");
        const bool activeOnly = payload.value("activeOnly").toBool(true);
        return reply({{"torrents", m_torrentClient->devTorrentsSnapshot(activeOnly)}});
    }

    if (cmd == QLatin1String("get_library")) {
        if (!m_streamPage || !m_streamPage->streamLibrary())
            return err("INTERNAL", "StreamLibrary not initialized");
        QJsonArray entries;
        for (const StreamLibraryEntry& e : m_streamPage->streamLibrary()->getAll()) {
            QJsonObject obj;
            obj["imdb"] = e.imdb;
            obj["name"] = e.name;
            obj["type"] = e.type;
            obj["year"] = e.year;
            obj["description"] = e.description;
            obj["addedAt"] = static_cast<double>(e.addedAt);

            QJsonArray playable;
            if (m_streamDownloadIndex) {
                for (const auto& d : m_streamDownloadIndex->entriesForImdb(e.imdb)) {
                    QJsonObject p;
                    p["s"] = d.season;
                    p["e"] = d.episode;
                    p["path"] = d.canonicalPath;
                    playable.append(p);
                }
            }
            obj["playableEpisodes"] = playable;
            entries.append(obj);
        }
        return reply({{"entries", entries}});
    }

    if (cmd == QLatin1String("get_downloads")) {
        QJsonArray entries;
        if (m_streamDownloadIndex) {
            for (const auto& d : m_streamDownloadIndex->all()) {
                QJsonObject obj;
                obj["imdb"] = d.imdbId;
                obj["season"] = d.season;
                obj["episode"] = d.episode;
                obj["canonicalPath"] = d.canonicalPath;
                obj["fileSizeBytes"] = static_cast<double>(d.fileSizeBytes);
                obj["sourceGroupId"] = d.sourceGroupId;
                obj["addedAt"] = static_cast<double>(d.addedAt);
                entries.append(obj);
            }
        }
        return reply({{"entries", entries}});
    }

    if (cmd == QLatin1String("get_bulk_groups")) {
        if (!m_torrentClient)
            return err("INTERNAL", "TorrentClient not initialized");
        return reply({{"groups", m_torrentClient->devBulkGroupsSnapshot()}});
    }

    if (cmd == QLatin1String("search")) {
        if (!m_streamPage)
            return err("INTERNAL", "StreamPage not initialized");
        const QString query = payload.value("query").toString();
        if (query.trimmed().isEmpty())
            return err("BAD_REQUEST", "payload.query required");
        const QString type = payload.value("type").toString();
        if (!type.isEmpty() && type != QLatin1String("movie") && type != QLatin1String("series"))
            return err("BAD_REQUEST", "payload.type must be movie or series");
        return reply(m_streamPage->devSearch(query, type, 15000));
    }

    if (cmd == QLatin1String("dispatch_episode")) {
        if (!m_streamPage)
            return err("INTERNAL", "StreamPage not initialized");
        const QString imdb = payload.value("imdbId").toString();
        const int season = payload.value("season").toInt();
        const int episode = payload.value("episode").toInt();
        if (imdb.isEmpty() || season <= 0 || episode <= 0)
            return err("BAD_REQUEST", "imdbId, positive season, and positive episode required");
        return reply(m_streamPage->devDispatchEpisode(imdb, season, episode));
    }

    if (cmd == QLatin1String("dispatch_season")) {
        if (!m_streamPage)
            return err("INTERNAL", "StreamPage not initialized");
        const QString imdb = payload.value("imdbId").toString();
        const int season = payload.value("season").toInt();
        if (imdb.isEmpty() || season <= 0)
            return err("BAD_REQUEST", "imdbId and positive season required");
        return reply(m_streamPage->devDispatchSeason(imdb, season));
    }

    if (cmd == QLatin1String("comics_get_state")) {
        auto* comics = comicsPage();
        if (!comics)
            return err("INTERNAL", "ComicsPage not initialized");
        return reply({{"snapshot", comics->devSnapshot()}});
    }

    if (cmd == QLatin1String("comics_get_library")) {
        auto* comics = comicsPage();
        if (!comics)
            return err("INTERNAL", "ComicsPage not initialized");
        return reply(comics->devLibrarySnapshot());
    }

    if (cmd == QLatin1String("comics_get_series")) {
        auto* comics = comicsPage();
        if (!comics)
            return err("INTERNAL", "ComicsPage not initialized");
        return reply(comics->devSeriesSnapshot());
    }

    if (cmd == QLatin1String("comics_select_volume")) {
        auto* comics = comicsPage();
        if (!comics)
            return err("INTERNAL", "ComicsPage not initialized");
        if (!payload.contains(QStringLiteral("row")))
            return err("BAD_REQUEST", "payload.row required");
        return reply(comics->devSelectVolume(payload.value("row").toInt(-1)));
    }

    if (cmd == QLatin1String("comics_open_series")) {
        auto* comics = comicsPage();
        if (!comics)
            return err("INTERNAL", "ComicsPage not initialized");
        const QString seriesId = payload.value("seriesId").toString();
        if (seriesId.isEmpty())
            return err("BAD_REQUEST", "payload.seriesId required");
        activatePage(QStringLiteral("comics"));
        return reply(comics->devOpenSeries(seriesId));
    }

    if (cmd == QLatin1String("comics_open_chapter")) {
        auto* comics = comicsPage();
        if (!comics)
            return err("INTERNAL", "ComicsPage not initialized");
        const QString seriesId = payload.value("seriesId").toString();
        const int volume = payload.value("volume").toInt();
        const int chapter = payload.value("chapter").toInt();
        if (seriesId.isEmpty() || volume <= 0 || chapter <= 0)
            return err("BAD_REQUEST", "seriesId, positive volume, and positive chapter required");
        activatePage(QStringLiteral("comics"));
        return reply(comics->devOpenChapter(seriesId, volume, chapter));
    }

    if (cmd == QLatin1String("comics_search_tankoyomi")) {
        auto* comics = comicsPage();
        if (!comics)
            return err("INTERNAL", "ComicsPage not initialized");
        const QString query = payload.value("query").toString();
        if (query.trimmed().isEmpty())
            return err("BAD_REQUEST", "payload.query required");
        activatePage(QStringLiteral("comics"));
        return reply(comics->devSearchTankoyomi(query, payload.value("timeout").toInt(15000)));
    }

    if (cmd == QLatin1String("comics_get_downloads")) {
        auto* comics = comicsPage();
        if (!comics)
            return err("INTERNAL", "ComicsPage not initialized");
        return reply(comics->devDownloadsSnapshot());
    }

    if (cmd == QLatin1String("comics_dispatch_volume")) {
        auto* comics = comicsPage();
        if (!comics)
            return err("INTERNAL", "ComicsPage not initialized");
        const QString seriesId = payload.value("seriesId").toString();
        const int volume = payload.value("volume").toInt();
        if (seriesId.isEmpty() || volume <= 0)
            return err("BAD_REQUEST", "seriesId and positive volume required");
        activatePage(QStringLiteral("comics"));
        return reply(comics->devDispatchVolume(
            seriesId, volume, payload.value("source").toString()));
    }

    if (cmd == QLatin1String("comics_get_sources")) {
        auto* comics = comicsPage();
        if (!comics)
            return err("INTERNAL", "ComicsPage not initialized");
        return reply(comics->devSourcesSnapshot());
    }

    if (cmd == QLatin1String("dump_ui")) {
        const QString pageId = payload.value("pageId").toString(m_activePageId);
        if (pageId == QLatin1String("comics")) {
            auto* comics = comicsPage();
            if (!comics)
                return err("INTERNAL", "ComicsPage not initialized");
            return reply({{"pageId", pageId}, {"snapshot", comics->devSnapshot()}});
        }
        if (pageId == QLatin1String("stream")) {
            if (!m_streamPage)
                return err("INTERNAL", "StreamPage not initialized");
            return reply({{"pageId", pageId}, {"snapshot", m_streamPage->devSnapshot()}});
        }
        if (pageId == QLatin1String("videos")) {
            if (!m_videosPage)
                return err("INTERNAL", "VideosPage not initialized");
            return reply({{"pageId", pageId}, {"snapshot", m_videosPage->devSnapshot(50)}});
        }
        if (pageId == QLatin1String("books")) {
            // v1.3 Phase D.1 (2026-05-19) — books-side dump-ui.
            auto* books = m_pageStack ? m_pageStack->findChild<BooksPage*>() : nullptr;
            if (!books)
                return err("INTERNAL", "BooksPage not initialized");
            return reply({{"pageId", pageId}, {"snapshot", books->devSnapshot()}});
        }
        if (pageId == QLatin1String("tankorent")) {
            // v1.5 Phase D.3 (2026-05-19) — tankorent dump-ui.
            if (!m_tankorentPage)
                return err("INTERNAL", "TankorentPage not initialized");
            return reply({{"pageId", pageId},
                          {"snapshot", m_tankorentPage->devSnapshot()}});
        }
        if (pageId == QLatin1String("tankolibrary")) {
            // v1.5 Phase D.3 (2026-05-19) — tankolibrary dump-ui.
            auto* tl = m_pageStack ? m_pageStack->findChild<TankoLibraryPage*>() : nullptr;
            if (!tl)
                return err("INTERNAL", "TankoLibraryPage not initialized");
            return reply({{"pageId", pageId}, {"snapshot", tl->devSnapshot()}});
        }
        if (pageId == QLatin1String("sources")) {
            // v1.5 Phase D.3 (2026-05-19) — composite sources dump-ui.
            QJsonObject composite;
            if (m_tankorentPage)
                composite["tankorent"] = m_tankorentPage->devSnapshot();
            auto* tl = m_pageStack ? m_pageStack->findChild<TankoLibraryPage*>() : nullptr;
            if (tl)
                composite["tankolibrary"] = tl->devSnapshot();
            return reply({{"pageId", pageId}, {"snapshot", composite}});
        }
        if (pageId == QLatin1String("library")) {
            // v1.6 Phase D.4 (2026-05-19) — composite library dump-ui.
            QJsonObject composite;
            composite["activeModePill"] = m_activePageId;
            composite["activeTheme"]    = Theme::slugFor(Theme::currentMode());
            if (m_videosPage)
                composite["videos"] = m_videosPage->devSnapshot(50);
            if (m_streamPage)
                composite["stream"] = m_streamPage->devSnapshot();
            if (auto* comics = comicsPage())
                composite["comics"] = comics->devSnapshot();
            if (auto* books = m_pageStack ? m_pageStack->findChild<BooksPage*>() : nullptr)
                composite["books"] = books->devSnapshot();
            return reply({{"pageId", pageId}, {"snapshot", composite}});
        }
        return reply({{"pageId", pageId}, {"snapshot", devSnapshot()}});
    }

    if (cmd == QLatin1String("logs")) {
        const int limit = payload.value("limit").toInt(100);
        return reply({{"entries", DebugLogBuffer::instance().recent(limit)}});
    }

    // ── v1.6 library-side bridge — global commands ──────────────────────────
    // Phase D.4 (2026-05-19). Theme + active-mode-pill + settings are global
    // (cross-mode); per-mode library_* commands are routed below via the
    // payload's `mode` field to the matching landing page.

    if (cmd == QLatin1String("library_get_active_mode_pill"))
        return reply({{"pageId", m_activePageId}});

    if (cmd == QLatin1String("library_get_active_theme"))
        return reply({{"themeId", Theme::slugFor(Theme::currentMode())}});

    if (cmd == QLatin1String("library_apply_theme")) {
        const QString themeId = payload.value("themeId").toString();
        if (themeId.isEmpty())
            return err("BAD_REQUEST", "payload.themeId required");
        // Aliasing: "noir" is the legacy/docs synonym for "dark"; everything
        // else must match a known kModes slug exactly.
        QString canonical = themeId;
        if (themeId.compare(QStringLiteral("noir"), Qt::CaseInsensitive) == 0)
            canonical = QStringLiteral("dark");
        bool known = false;
        for (const auto& entry : Theme::kModes) {
            if (canonical.compare(QString::fromLatin1(entry.slug),
                                  Qt::CaseInsensitive) == 0) {
                known = true;
                canonical = QString::fromLatin1(entry.slug);
                break;
            }
        }
        if (!known)
            return err("BAD_REQUEST",
                QStringLiteral("unknown themeId '%1'; valid: dark/nord/"
                               "solarized/gruvbox/catppuccin").arg(themeId));
        const Theme::Mode mode = Theme::modeFromSlug(canonical, Theme::Mode::Dark);
        if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
            Theme::applyTheme(*app, mode);
            Theme::saveMode(mode);
        }
        return reply({{"themeId", Theme::slugFor(Theme::currentMode())},
                      {"requested", themeId}});
    }

    if (cmd == QLatin1String("library_get_settings")) {
        // Composite snapshot via each landing page's library_get_section
        // delegate. Read-only keys span the full library-section JSON;
        // writableKeys enumerates the allowlist accepted by library_set_setting.
        QJsonObject settings;
        settings["theme.id"] = Theme::slugFor(Theme::currentMode());
        settings["active_mode_pill"] = m_activePageId;
        const QStringList modes{"comics", "books", "videos", "stream"};
        QJsonObject perMode;
        for (const QString& mode : modes) {
            QObject* page = nullptr;
            if (mode == QLatin1String("videos"))      page = m_videosPage;
            else if (mode == QLatin1String("stream")) page = m_streamPage;
            else if (mode == QLatin1String("comics")) page = comicsPage();
            else if (mode == QLatin1String("books"))
                page = m_pageStack ? m_pageStack->findChild<BooksPage*>() : nullptr;
            if (!page) continue;
            QJsonObject p{{"mode", mode}};
            QJsonObject r{
                {"type", QStringLiteral("reply")},
                {"seq", seq}
            };
            if (forwardToDispatch(page, QStringLiteral("library_get_section"), p, r))
                perMode[mode] = r.value("section").toObject();
        }
        settings["perMode"] = perMode;
        QJsonArray writable{"theme.id"};
        for (const QString& mode : modes) {
            writable.append(QStringLiteral("density.") + mode);
            writable.append(QStringLiteral("sort_key.") + mode);
            writable.append(QStringLiteral("search.query.") + mode);
        }
        return reply({{"settings", settings}, {"writableKeys", writable}});
    }

    if (cmd == QLatin1String("library_set_setting")) {
        const QString key   = payload.value("key").toString();
        const QJsonValue v  = payload.value("value");
        if (key.isEmpty())
            return err("BAD_REQUEST", "payload.key required");
        // Allowlist-gated write surface. Default read-only; only the keys
        // below mutate state. Anything else returns READONLY_KEY.
        if (key == QLatin1String("theme.id")) {
            QJsonObject p{{"themeId", v.toString()}};
            return handleDevCommand(QStringLiteral("library_apply_theme"), seq, p);
        }
        auto parseMode = [](const QString& key, const QString& prefix) -> QString {
            if (!key.startsWith(prefix)) return QString();
            return key.mid(prefix.size());
        };
        QString mode;
        QString innerCmd;
        QJsonObject innerPayload;
        if ((mode = parseMode(key, QStringLiteral("density."))).size()) {
            innerCmd = QStringLiteral("library_set_density");
            innerPayload[QStringLiteral("value")] = v.toInt();
        } else if ((mode = parseMode(key, QStringLiteral("sort_key."))).size()) {
            innerCmd = QStringLiteral("library_set_sort");
            innerPayload[QStringLiteral("key")] = v.toString();
        } else if ((mode = parseMode(key, QStringLiteral("search.query."))).size()) {
            innerCmd = QStringLiteral("library_set_search_query");
            innerPayload[QStringLiteral("query")] = v.toString();
        } else {
            return err("READONLY_KEY",
                QStringLiteral("key '%1' is not in the library-set-setting "
                               "allowlist; writable keys: theme.id, "
                               "density.<mode>, sort_key.<mode>, "
                               "search.query.<mode>").arg(key));
        }
        innerPayload[QStringLiteral("mode")] = mode;
        return handleDevCommand(innerCmd, seq, innerPayload);
    }

    if (cmd == QLatin1String("library_get_root_folders")) {
        const QString mode = payload.value("mode").toString();
        if (mode.isEmpty())
            return err("BAD_REQUEST", "payload.mode required (comics|books|videos|stream)");
        if (mode == QLatin1String("stream"))
            return reply({{"mode", mode}, {"roots", QJsonArray{}},
                          {"note", QStringLiteral("stream is network-driven; "
                                                  "no root folders concept")}});
        const QStringList valid{"comics", "books", "videos"};
        if (!valid.contains(mode))
            return err("BAD_REQUEST",
                QStringLiteral("mode must be one of comics/books/videos/stream"));
        QJsonArray arr;
        for (const QString& p : m_bridge->rootFolders(mode))
            arr.append(p);
        return reply({{"mode", mode}, {"roots", arr}});
    }

    // ── v1.6 library-side bridge — per-mode routing ─────────────────────────
    // Mode-specific library_* commands (continue-reading, recently-added,
    // search-state, scan-state, sort, density, active-layer, reset-mode,
    // selection, trigger-scan) take payload.mode = comics|books|videos|stream
    // and route to that landing page's dispatchDevCommand.

    if (cmd.startsWith(QLatin1String("library_"))) {
        const QString mode = payload.value("mode").toString();
        if (mode.isEmpty())
            return err("BAD_REQUEST",
                QStringLiteral("library_* commands require payload.mode "
                               "(comics|books|videos|stream)"));
        QObject* page = nullptr;
        if (mode == QLatin1String("videos"))      page = m_videosPage;
        else if (mode == QLatin1String("stream")) page = m_streamPage;
        else if (mode == QLatin1String("comics")) page = comicsPage();
        else if (mode == QLatin1String("books"))
            page = m_pageStack ? m_pageStack->findChild<BooksPage*>() : nullptr;
        else
            return err("BAD_REQUEST",
                QStringLiteral("mode '%1' not in [comics,books,videos,stream]")
                    .arg(mode));
        if (!page)
            return err("INTERNAL",
                QStringLiteral("landing page for mode '%1' not initialized")
                    .arg(mode));
        QJsonObject delegatedReply{
            {"type", QStringLiteral("reply")},
            {"seq", seq}
        };
        if (forwardToDispatch(page, cmd, payload, delegatedReply))
            return delegatedReply;
        return err("UNKNOWN_CMD",
            QStringLiteral("library_* command '%1' not handled by mode '%2'")
                .arg(cmd, mode));
    }

    QJsonObject delegatedReply;
    auto tryForward = [&](bool matches, QObject* page) -> bool {
        if (!matches)
            return false;
        delegatedReply = QJsonObject{
            {"type", QStringLiteral("reply")},
            {"seq", seq}
        };
        return forwardToDispatch(page, cmd, payload, delegatedReply);
    };

    if (tryForward(cmd.startsWith(QLatin1String("books_")),
                   m_pageStack ? m_pageStack->findChild<BooksPage*>() : nullptr))
        return delegatedReply;
    if (tryForward(cmd.startsWith(QLatin1String("comics_")),
                   m_pageStack ? m_pageStack->findChild<ComicsPage*>() : nullptr))
        return delegatedReply;
    if (tryForward(cmd.startsWith(QLatin1String("videos_")), m_videosPage))
        return delegatedReply;
    if (tryForward(cmd.startsWith(QLatin1String("stream_")), m_streamPage))
        return delegatedReply;
    if (tryForward(cmd.startsWith(QLatin1String("tankorent_")), m_tankorentPage))
        return delegatedReply;
    if (tryForward(cmd.startsWith(QLatin1String("tankolibrary_")),
                   m_pageStack ? m_pageStack->findChild<TankoLibraryPage*>() : nullptr))
        return delegatedReply;
    if (cmd.startsWith(QLatin1String("sources_"))) {
        if (tryForward(true, m_tankorentPage))
            return delegatedReply;
        if (tryForward(true, m_pageStack ? m_pageStack->findChild<TankoLibraryPage*>() : nullptr))
            return delegatedReply;
    }

    // ── v1.7 player-side deeper bridge — Phase D.2 (2026-05-19) ─────────────
    // player_/sidecar_/subs_/osd_ all route through VideoPlayer's single
    // dispatchDevCommand entry point. VideoPlayer internally forwards
    // sidecar_* to the live SidecarProcess backend, subs_* to m_subOverlay,
    // and osd_* to its own overlay-state surface. NO_PLAYER fires when
    // the user hasn't opened a file yet (m_videoPlayer null or not
    // visible) — agents are expected to `play-file <path>` first.
    if (cmd.startsWith(QLatin1String("player_"))
        || cmd.startsWith(QLatin1String("sidecar_"))
        || cmd.startsWith(QLatin1String("subs_"))
        || cmd.startsWith(QLatin1String("osd_"))) {
        if (!m_videoPlayer || !m_videoPlayer->isVisible()) {
            return err("NO_PLAYER",
                QStringLiteral("video player not open; play a file first via "
                               "play_file <path>"));
        }
        QJsonObject delegated{
            {"type", QStringLiteral("reply")},
            {"seq", seq}
        };
        if (forwardToDispatch(m_videoPlayer, cmd, payload, delegated))
            return delegated;
        return err("UNKNOWN_CMD",
            QStringLiteral("player-side command '%1' not handled by VideoPlayer")
                .arg(cmd));
    }

    // ── v1.8 synthetic UI interaction layer — Phase D.5 (2026-05-19) ────────
    // ui_* commands are looked up against MainWindow's QObject tree by
    // objectName. Read-only commands (query/list/dry-run/active-layer/
    // query-focus) gate on --dev-control only. Write-capable commands
    // (click/keypress/text-input/scroll/mouse/wait/set-checkbox/set-combo/
    // select-table-row) additionally require TANKOBAN_DEV_UI_SIM=1 or return
    // UI_SIM_DISABLED — the gate check happens here BEFORE forwarding so
    // we never partially execute and then bail.
    if (cmd.startsWith(QLatin1String("ui_"))) {
        if (!m_uiDispatcher) {
            return err("INTERNAL",
                QStringLiteral("UiInteractionDispatcher not initialized; "
                               "enableDevControl() must run first"));
        }
        if (UiInteractionDispatcher::isWriteCapable(cmd)) {
            const QByteArray sim = qgetenv("TANKOBAN_DEV_UI_SIM");
            if (sim != "1") {
                return err("UI_SIM_DISABLED",
                    QStringLiteral("synthetic UI write '%1' requires "
                                   "TANKOBAN_DEV_UI_SIM=1").arg(cmd));
            }
        }
        QJsonObject delegated{
            {"type", QStringLiteral("reply")},
            {"seq", seq}
        };
        if (m_uiDispatcher->dispatch(cmd, payload, delegated))
            return delegated;
        return err("UNKNOWN_CMD",
            QStringLiteral("ui_* command '%1' not recognised by UiInteractionDispatcher")
                .arg(cmd));
    }

    // ── v1.9 system state + introspection layer — Phase D.6 (2026-05-19) ────
    // Eleven prefixes (app_/settings_/jsonstore_/cache_/scanner_/log_/
    // events_/theme_/font_/perf_/dev_) all route through SystemIntrospection.
    // Wire format is snake_case per existing v1.x convention (player_*,
    // ui_*, books_*, ...); tankoctl's CLI surface accepts the natural
    // kebab-case (app-get-active-modals) and converts hyphens to
    // underscores before sending. Write-capable commands (settings_set/
    // reset, jsonstore_set, cache_clear, log_set_level, theme_reload,
    // dev_inject_error, dev_toggle_feature) additionally require
    // TANKOBAN_DEV_WRITE=1 or return DEV_WRITE_DISABLED — the gate check
    // happens HERE BEFORE forwarding so write-capable commands never
    // partially execute and then bail. The gate name is DELIBERATELY
    // DISTINCT from D.5's TANKOBAN_DEV_UI_SIM — synthetic UI clicks and
    // state writes / cache busts are independent risk surfaces.
    {
        const bool isSysCmd =
               cmd.startsWith(QLatin1String("app_"))
            || cmd.startsWith(QLatin1String("settings_"))
            || cmd.startsWith(QLatin1String("jsonstore_"))
            || cmd.startsWith(QLatin1String("cache_"))
            || cmd.startsWith(QLatin1String("scanner_"))
            || cmd.startsWith(QLatin1String("log_"))
            || cmd.startsWith(QLatin1String("events_"))
            || cmd.startsWith(QLatin1String("theme_"))
            || cmd.startsWith(QLatin1String("font_"))
            || cmd.startsWith(QLatin1String("perf_"))
            || cmd.startsWith(QLatin1String("dev_"));
        if (isSysCmd) {
            if (!m_systemIntrospection) {
                return err("INTERNAL",
                    QStringLiteral("SystemIntrospection not initialized; "
                                   "enableDevControl() must run first"));
            }
            if (SystemIntrospection::isWriteCapable(cmd)) {
                const QByteArray write = qgetenv("TANKOBAN_DEV_WRITE");
                if (write != "1") {
                    return err("DEV_WRITE_DISABLED",
                        QStringLiteral("write-capable command '%1' requires "
                                       "TANKOBAN_DEV_WRITE=1").arg(cmd));
                }
            }
            QJsonObject delegated{
                {"type", QStringLiteral("reply")},
                {"seq", seq}
            };
            if (m_systemIntrospection->dispatch(cmd, payload, delegated))
                return delegated;
            return err("UNKNOWN_CMD",
                QStringLiteral("v1.9 command '%1' not recognised by SystemIntrospection")
                    .arg(cmd));
        }
    }

    return err("UNKNOWN_CMD",
        QStringLiteral("command '%1' not implemented in v1").arg(cmd));
}

// ── FRAMELESS_CHROME_FIX 2026-05-01 — chrome event plumbing ─────────────────
//
// Strategy: Qt::FramelessWindowHint strips the visible OS title bar; on
// Windows we re-add WS_THICKFRAME via SetWindowLong so Aero snap, Win+Arrow
// snap, taskbar previews, and resize cursors keep working natively. WM_NCCALC-
// SIZE returns 0 to make the client area cover the full window (no reserved
// title-bar strip), and WM_NCHITTEST returns HTCAPTION on empty topbar zones
// so drag, double-click-maximize, and right-click system menu all fall out
// for free without any Qt-side mouse plumbing.
//
// Reference: Microsoft "Custom title bar" Win11 guidance + the well-known
// Edge / Notion / WinUI3 pattern.

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        // FRAMELESS_CHROME_FULLSCREEN_EXIT_FIX 2026-05-04 — when the
        // window leaves Qt::WindowFullScreen, Win32 has just restored
        // the chrome window styles (WS_CAPTION / WS_THICKFRAME / etc.)
        // that showFullScreen() stripped on entry. Qt does NOT fire
        // SWP_FRAMECHANGED on its own, so WM_NCCALCSIZE doesn't re-
        // execute; the OS draws one or two frames with the title bar
        // visible before something else (resize, paint) wakes the
        // message up. Visible as a brief title-bar flash on every
        // fullscreen exit — Hemanth-observed on mpv player exit, also
        // covers the carry-forward S2 takeover-flash bug from MAKE_MPV
        // _BEAT_FFMPEG arc (any state transition that rebuilt the
        // window style hit the same race). Re-applying the helper here
        // forces SWP_FRAMECHANGED → WM_NCCALCSIZE re-fires immediately
        // → no flash.
        auto* sce = static_cast<QWindowStateChangeEvent*>(event);
        const bool wasFullscreen   = sce->oldState() & Qt::WindowFullScreen;
        const bool isFullscreenNow = windowState()    & Qt::WindowFullScreen;
        if (wasFullscreen && !isFullscreenNow) {
            applyFramelessWin32Style();
        }
        updateMaxRestoreIcon();
    }
    QMainWindow::changeEvent(event);
}

// FRAMELESS_CHROME_FULLSCREEN_EXIT_FIX 2026-05-04 — extracted from
// MainWindow constructor (lines ~210-225 of the original FRAMELESS_-
// CHROME_FIX 2026-05-01 ship) so the same Win32 hack can be re-applied
// on fullscreen-exit transitions. See changeEvent above for the second
// call site, and the function-pointer comment in MainWindow.h for the
// full motivation.
void MainWindow::applyFramelessWin32Style()
{
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd) {
        LONG style = ::GetWindowLong(hwnd, GWL_STYLE);
        ::SetWindowLong(hwnd, GWL_STYLE,
            style | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_CAPTION);
        ::SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
#endif
}

void MainWindow::updateMaxRestoreIcon()
{
    const bool isMax = isMaximized();
    if (m_chromeMax) {
        m_chromeMax->setIcon(QIcon(isMax ? ":/icons/chrome_restore.svg"
                                         : ":/icons/chrome_max.svg"));
        m_chromeMax->setToolTip(isMax ? "Restore" : "Maximize");
    }
    // PER_VIEW_CHROME_FIX 2026-05-02 — fan out to takeover surfaces so their
    // chrome icons stay in sync with the underlying window state.
    if (m_comicReader) m_comicReader->updateChromeMaxIcon(isMax);
    if (m_bookReader)  m_bookReader->updateChromeMaxIcon(isMax);
    if (m_videoPlayer) m_videoPlayer->updateChromeMaxIcon(isMax);
}

void MainWindow::onChromeMaximizeToggle()
{
    if (isMaximized()) {
#ifdef Q_OS_WIN
        // Win32 direct path. Qt's showNormal() leaves the saved
        // normalGeometry equal to the maximized rect when the window was
        // opened straight to maximized (no windowed history), so the
        // restore is a visual no-op. SetWindowPlacement(.showCmd =
        // SW_SHOWNORMAL, .rcNormalPosition = our centered default) tells
        // Win32 directly to enter normal state at a specific rect — single
        // synchronous call, no event-queue race.
        HWND hwnd = reinterpret_cast<HWND>(winId());
        if (hwnd) {
            WINDOWPLACEMENT wp = {};
            wp.length = sizeof(wp);
            if (::GetWindowPlacement(hwnd, &wp)) {
                if (auto* scr = screen()) {
                    const QRect avail = scr->availableGeometry();
                    const int normalW = wp.rcNormalPosition.right  - wp.rcNormalPosition.left;
                    const int normalH = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
                    const bool noWindowedHistory =
                        normalW >= avail.width()  - 4 ||
                        normalH >= avail.height() - 4 ||
                        normalW <= 0 || normalH <= 0;
                    if (noWindowedHistory) {
                        const int w = qMin(1280, avail.width()  - 200);
                        const int h = qMin(800,  avail.height() - 150);
                        const int x = avail.x() + (avail.width()  - w) / 2;
                        const int y = avail.y() + (avail.height() - h) / 2;
                        wp.rcNormalPosition.left   = x;
                        wp.rcNormalPosition.top    = y;
                        wp.rcNormalPosition.right  = x + w;
                        wp.rcNormalPosition.bottom = y + h;
                    }
                    wp.showCmd = SW_SHOWNORMAL;
                    ::SetWindowPlacement(hwnd, &wp);
                    return;
                }
            }
        }
#endif
        showNormal();
    } else {
        showMaximized();
    }
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType != "windows_generic_MSG")
        return QMainWindow::nativeEvent(eventType, message, result);

    auto* msg = static_cast<MSG*>(message);

    // ── WM_NCCALCSIZE ────────────────────────────────────────────────────────
    // wParam=TRUE → lParam is NCCALCSIZE_PARAMS; rgrc[0] is the proposed
    // window rect, which we leave alone so the client area covers the full
    // window. We DO inset by the resize-frame thickness when maximized,
    // otherwise the window extends past visible screen edges by the frame
    // thickness (~7-9px) and content gets clipped.
    if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
        if (::IsZoomed(msg->hwnd)) {
            const int frameX  = ::GetSystemMetrics(SM_CXFRAME);
            const int frameY  = ::GetSystemMetrics(SM_CYFRAME);
            const int padding = ::GetSystemMetrics(SM_CXPADDEDBORDER);
            params->rgrc[0].top    += frameY + padding;
            params->rgrc[0].left   += frameX + padding;
            params->rgrc[0].right  -= frameX + padding;
            params->rgrc[0].bottom -= frameY + padding;
        }
        *result = 0;
        return true;
    }

    // ── WM_NCHITTEST ─────────────────────────────────────────────────────────
    // Edge zones return HT*BORDER so the OS draws resize cursors and handles
    // drag-resize. The topbar's empty zones return HTCAPTION so drag,
    // double-click-max, and right-click system menu work natively.
    if (msg->message == WM_NCHITTEST) {
        const QPoint screenPt(GET_X_LPARAM(msg->lParam),
                              GET_Y_LPARAM(msg->lParam));
        const QPoint local = mapFromGlobal(screenPt);

        const int margin = 6;  // resize-zone thickness
        const QRect r = rect();
        const bool onLeft   = local.x() <  margin;
        const bool onRight  = local.x() >= r.width()  - margin;
        const bool onTop    = local.y() <  margin;
        const bool onBottom = local.y() >= r.height() - margin;
        if (onTop && onLeft)         { *result = HTTOPLEFT;     return true; }
        if (onTop && onRight)        { *result = HTTOPRIGHT;    return true; }
        if (onBottom && onLeft)      { *result = HTBOTTOMLEFT;  return true; }
        if (onBottom && onRight)     { *result = HTBOTTOMRIGHT; return true; }
        if (onLeft)                  { *result = HTLEFT;        return true; }
        if (onRight)                 { *result = HTRIGHT;       return true; }
        if (onTop)                   { *result = HTTOP;         return true; }
        if (onBottom)                { *result = HTBOTTOM;      return true; }

        // Caption zone — empty regions of m_topBar become drag-handle.
        if (m_topBar) {
            const QPoint topbarPt = m_topBar->mapFrom(this, local);
            if (m_topBar->rect().contains(topbarPt)) {
                QWidget* hit = m_topBar->childAt(topbarPt);
                // Bare topbar, brand label, slot containers, and TopNav frame
                // all pass through as caption (drag / double-click-max /
                // right-click system menu work natively on these zones).
                // Buttons and the actual nav button widgets sit on top and
                // receive their own clicks because childAt returns the
                // deepest visible child and lands on them directly.
                const QString hitName = hit ? hit->objectName() : QString();
                const bool isCaption =
                    !hit ||
                    hit == m_brandLabel ||
                    hitName == QLatin1String("TopNav") ||
                    hitName == QLatin1String("TopBarLeftSlot") ||
                    hitName == QLatin1String("TopBarRightSlot");
                if (isCaption) {
                    *result = HTCAPTION;
                    return true;
                }
            }
        }

        *result = HTCLIENT;
        return true;
    }

    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

// Gray chevrons while a modal dialog is on screen (spec §3.12).
// Watch every QDialog show/hide app-wide and re-evaluate chevron enabled state.
bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
        if (qobject_cast<QDialog*>(watched)) {
            const bool inModal   = (QApplication::activeModalWidget() != nullptr);
            const bool inOverlay = isReaderOrPlayerActive();
            if (m_backBtn && m_navController) {
                const bool nav = m_navController->canGoBack(m_activePageId);
                m_backBtn->setEnabled(nav && !inModal && !inOverlay);
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- mouse thumb buttons.
// Per spec B3/B4: both Qt::BackButton (mouse btn 4) and Qt::ForwardButton
// (mouse btn 5) are inert. We accept the events to swallow them (so the
// OS doesn't propagate to any default Back/Forward handler) but do NOT
// trigger any nav action. Hemanth's hardware doesn't have these buttons;
// this is purely defensive for users on power-mice.
void MainWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::BackButton || event->button() == Qt::ForwardButton) {
        event->accept();
        return;
    }
    QMainWindow::mousePressEvent(event);
}
