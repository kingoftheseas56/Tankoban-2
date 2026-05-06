#include "MainWindow.h"
#include "GlassBackground.h"
#include "RootFoldersOverlay.h"
#include "widgets/ThemePicker.h"
#include "pages/ComicsPage.h"
#include "pages/BooksPage.h"
#include "pages/VideosPage.h"
#include "pages/OrganisePage.h"
#include "pages/StreamPage.h"
#include "pages/TankorentPage.h"
#include "pages/TankoyomiPage.h"
#include "pages/TankoLibraryPage.h"
#include "widgets/SidebarDrawer.h"
#include "core/torrent/TorrentClient.h"
#include "readers/ComicReader.h"
#include "readers/BookReader.h"
#include "player/VideoPlayer.h"
#include "core/CoreBridge.h"
#include "core/DebugLogBuffer.h"
#include "devtools/DevControlServer.h"

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
#include <QEvent>
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
static constexpr const char *PAGE_TANKOYOMI    = "tankoyomi";
static constexpr const char *PAGE_TANKOLIBRARY = "tankolibrary";

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
    // Toggles the left slide-in drawer holding Tankorent / Tankoyomi /
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
        { PAGE_VIDEOS,  "Videos"  },
        { PAGE_STREAM,  "Stream"  },
        // SOURCES_SIDEBAR 2026-05-05 — Sources entry removed; the three sub-pages
        // (Tankorent / Tankoyomi / TankoLibrary) are now reachable via the
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
            activatePage(pageId);
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
    m_topBarLeftSlot->setFixedWidth(m_topBarRightSlot->sizeHint().width());
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
    dbg("4b-comicspage-created");

    auto *booksPage = new BooksPage(m_bridge);
    m_pageStack->addWidget(booksPage);
    dbg("4c-bookspage-created");

    m_videosPage = new VideosPage(m_bridge);
    m_pageStack->addWidget(m_videosPage);
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
    dbg("4e-torrentclient-created");

    // Stream page — m_streamPage cache (STREAM_ADD_TO_TANKORENT 2026-05-06)
    // so we can wire the magnet-handoff signal without a qobject_cast walk.
    m_streamPage = new StreamPage(m_bridge, torrentClient->engine());
    m_pageStack->addWidget(m_streamPage);
    dbg("4f-streampage-created");

    // Share StreamPage's MetaAggregator with VideosPage for "Fetch poster
    // from internet" context-menu action on folder tiles (Agent 5 Batch 1,
    // per HELP.md 2026-04-15 handshake with Agent 4).
    m_videosPage->setMetaAggregator(m_streamPage->metaAggregator());

    // Share TorrentClient with VideosPage so the (auto-)rename path can
    // release any active libtorrent record before the folder is moved on
    // disk — without this libtorrent silently re-creates the original
    // folder + re-downloads, producing the "multiplying folders" symptom.
    m_videosPage->setTorrentClient(torrentClient);

    m_tankorentPage = new TankorentPage(m_bridge, torrentClient);
    m_tankorentPage->setObjectName(PAGE_TANKORENT);
    m_pageStack->addWidget(m_tankorentPage);
    dbg("4g-tankorentpage-created");

    // STREAM_ADD_TO_TANKORENT (2026-05-06) — cross-page magnet hand-off.
    // Connect after both pages exist; routes the right-click "Add torrent
    // to Tankorent" action through MainWindow's nav layer rather than
    // having StreamPage talk directly to TankorentPage (cross-page
    // coordination convention).
    connect(m_streamPage, &StreamPage::addToTankorentRequested,
            this, &MainWindow::onAddToTankorentRequested);

    auto *tankoyomiPage = new TankoyomiPage(m_bridge);
    tankoyomiPage->setObjectName(PAGE_TANKOYOMI);
    m_pageStack->addWidget(tankoyomiPage);
    dbg("4g2-tankoyomipage-created");

    auto *tankoLibraryPage = new TankoLibraryPage(m_bridge, torrentClient);
    tankoLibraryPage->setObjectName(PAGE_TANKOLIBRARY);
    m_pageStack->addWidget(tankoLibraryPage);
    dbg("4g3-tankolibrarypage-created");
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
}

// ── Page activation ─────────────────────────────────────────────────────────
void MainWindow::activatePage(const QString &pageId)
{
    if (pageId == m_activePageId)
        return;

    m_activePageId = pageId;

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

// ── Root folders overlay ────────────────────────────────────────────────────
QString MainWindow::domainForPage(const QString& pageId) const
{
    if (pageId == PAGE_COMICS)  return "comics";
    if (pageId == PAGE_BOOKS)   return "books";
    if (pageId == PAGE_VIDEOS)  return "videos";
    if (pageId == PAGE_ORGANISE) return "videos";
    if (pageId == PAGE_TANKORENT || pageId == PAGE_TANKOYOMI || pageId == PAGE_TANKOLIBRARY)
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
}

// ── Book reader ──────────────────────────────────────────────────────────────
void MainWindow::openBookReader(const QString& filePath)
{
    m_bookReader->openBook(filePath);
    m_bookReader->setGeometry(centralWidget()->rect());
    m_bookReader->show();
    m_bookReader->raise();
    m_bookReader->setFocus();
}

void MainWindow::closeBookReader()
{
    m_bookReader->hide();
}

// ── Video player ─────────────────────────────────────────────────────────────
void MainWindow::openVideoPlayer(const QString& filePath)
{
    m_videoPlayer->openFile(filePath);
    m_videoPlayer->setGeometry(centralWidget()->rect());
    m_videoPlayer->show();
    m_videoPlayer->raise();
    m_videoPlayer->setFocus();
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

    if (cmd == QLatin1String("ping")) {
        QJsonArray cmds{ "ping","get_state","open_page","scan_videos",
                         "get_videos","play_file","close_player",
                         "get_player","logs" };
        return reply({
            {"schema",     "tankoban.dev.v1"},
            {"appVersion", QApplication::applicationVersion()},
            {"commands",   cmds},
            {"features",   QJsonArray{}}
        });
    }

    if (cmd == QLatin1String("get_state"))
        return reply({{"snapshot", devSnapshot()}});

    if (cmd == QLatin1String("open_page")) {
        const QString pageId = payload.value("pageId").toString();
        const QStringList valid{"comics","books","videos","organise","stream",
                                "tankorent","tankoyomi","tankolibrary"};
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

    if (cmd == QLatin1String("logs")) {
        const int limit = payload.value("limit").toInt(100);
        return reply({{"entries", DebugLogBuffer::instance().recent(limit)}});
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
