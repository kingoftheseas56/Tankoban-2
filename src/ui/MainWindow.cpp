#include "MainWindow.h"
#include "GlassBackground.h"
#include "RootFoldersOverlay.h"
#include "pages/ComicsPage.h"
#include "pages/BooksPage.h"
#include "pages/VideosPage.h"
#include "pages/SourcesPage.h"
#include "pages/StreamPage.h"
#include "core/torrent/TorrentClient.h"
#include "readers/ComicReader.h"
#include "readers/BookReader.h"
#include "player/VideoPlayer.h"
#include "core/CoreBridge.h"

#include <QVBoxLayout>
#include <QFrame>
#include <QApplication>
#include <QResizeEvent>
#include <QScreen>
#include <QButtonGroup>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ── Page id constants ───────────────────────────────────────────────────────
static constexpr const char *PAGE_COMICS  = "comics";
static constexpr const char *PAGE_BOOKS   = "books";
static constexpr const char *PAGE_VIDEOS  = "videos";
static constexpr const char *PAGE_STREAM  = "stream";
static constexpr const char *PAGE_SOURCES = "sources";

// ── Constructor ─────────────────────────────────────────────────────────────
MainWindow::MainWindow(CoreBridge* bridge, QWidget *parent)
    : QMainWindow(parent)
    , m_bridge(bridge)
{
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
    { FILE* f=fopen("_boot_debug.txt","a"); if(f){fprintf(f,"5a-pagestack-added\n");fflush(f);fclose(f);} }

    rootLayout->addWidget(content, 1);

    // Root folders overlay (hidden by default)
    m_rootFoldersOverlay = new RootFoldersOverlay(m_bridge, root);
    m_rootFoldersOverlay->hide();
    { FILE* f=fopen("_boot_debug.txt","a"); if(f){fprintf(f,"5b-rootfolders-overlay\n");fflush(f);fclose(f);} }
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
    { FILE* f=fopen("_boot_debug.txt","a"); if(f){fprintf(f,"5c-before-comicreader\n");fflush(f);fclose(f);} }
    m_comicReader = new ComicReader(m_bridge, root);
    m_comicReader->hide();
    connect(m_comicReader, &ComicReader::closeRequested, this, &MainWindow::closeComicReader);
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
    { FILE* f=fopen("_boot_debug.txt","a"); if(f){fprintf(f,"5e-before-videoplayer\n");fflush(f);fclose(f);} }
    m_videoPlayer = new VideoPlayer(m_bridge, root);
    m_videoPlayer->hide();
    connect(m_videoPlayer, &VideoPlayer::closeRequested, this, &MainWindow::closeVideoPlayer);
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
    });

    { FILE* f=fopen("_boot_debug.txt","a"); if(f){fprintf(f,"5f-before-central\n");fflush(f);fclose(f);} }
    setCentralWidget(root);
    bindShortcuts();
    setupTrayIcon();
    { FILE* f=fopen("_boot_debug.txt","a"); if(f){fprintf(f,"5g-constructor-done\n");fflush(f);fclose(f);} }

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

    m_brandLabel = new QLabel("Tankoban", bar);
    m_brandLabel->setObjectName("Brand");
    layout->addWidget(m_brandLabel);

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
        { PAGE_SOURCES, "Sources" },
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

    // Rescan button (↻)
    auto *scanBtn = new QPushButton(QString::fromUtf8("\u21BB"), bar);
    scanBtn->setObjectName("IconButton");
    scanBtn->setFixedSize(28, 24);
    scanBtn->setCursor(Qt::PointingHandCursor);
    scanBtn->setToolTip("Rescan library (F5)");
    connect(scanBtn, &QPushButton::clicked, this, [this]() {
        if (auto *c = m_pageStack->findChild<ComicsPage*>()) c->triggerScan();
        if (auto *b = m_pageStack->findChild<BooksPage*>())  b->triggerScan();
        if (auto *v = m_pageStack->findChild<VideosPage*>()) v->triggerScan();
    });
    layout->addWidget(scanBtn, 0, Qt::AlignVCenter);

    // Add folder button (+)
    auto *addBtn = new QPushButton("+", bar);
    addBtn->setObjectName("IconButton");
    addBtn->setFixedSize(28, 24);
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setToolTip("Add root folder");
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::showRootFolders);
    layout->addWidget(addBtn, 0, Qt::AlignVCenter);

    m_topBar = bar;
}

// ── Page stack ──────────────────────────────────────────────────────────────
void MainWindow::buildPageStack()
{
    auto dbg = [](const char* msg) {
        FILE* f = fopen("_boot_debug.txt", "a");
        if (f) { fprintf(f, "%s\n", msg); fflush(f); fclose(f); }
    };

    m_pageStack = new QStackedWidget(this);
    dbg("4a-pagestack-created");

    auto *comicsPage = new ComicsPage(m_bridge);
    m_pageStack->addWidget(comicsPage);
    dbg("4b-comicspage-created");

    auto *booksPage = new BooksPage(m_bridge);
    m_pageStack->addWidget(booksPage);
    dbg("4c-bookspage-created");

    auto *videosPage = new VideosPage(m_bridge);
    m_pageStack->addWidget(videosPage);
    dbg("4d-videospage-created");

    // TorrentClient (shared by StreamPage and SourcesPage)
    auto *torrentClient = new TorrentClient(m_bridge, this);
    dbg("4e-torrentclient-created");

    // Stream page
    auto *streamPage = new StreamPage(m_bridge, torrentClient->engine());
    m_pageStack->addWidget(streamPage);
    dbg("4f-streampage-created");

    // Share StreamPage's MetaAggregator with VideosPage for "Fetch poster
    // from internet" context-menu action on folder tiles (Agent 5 Batch 1,
    // per HELP.md 2026-04-15 handshake with Agent 4).
    videosPage->setMetaAggregator(streamPage->metaAggregator());

    // Share TorrentClient with VideosPage so the (auto-)rename path can
    // release any active libtorrent record before the folder is moved on
    // disk — without this libtorrent silently re-creates the original
    // folder + re-downloads, producing the "multiplying folders" symptom.
    videosPage->setTorrentClient(torrentClient);

    auto *sourcesPage = new SourcesPage(m_bridge, torrentClient);
    dbg("4g-sourcespage-created");
    m_pageStack->addWidget(sourcesPage);
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
    bind(QKeySequence("Ctrl+5"), PAGE_SOURCES);

    // F11 fullscreen toggle
    auto *fs = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(fs, &QShortcut::activated, this, [this]() {
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
            if (auto *sources = qobject_cast<SourcesPage*>(m_pageStack->widget(i)))
                sources->activate();
            if (auto *stream = qobject_cast<StreamPage*>(m_pageStack->widget(i)))
                stream->activate();
            break;
        }
    }
}

// ── Root folders overlay ────────────────────────────────────────────────────
QString MainWindow::domainForPage(const QString& pageId) const
{
    if (pageId == PAGE_COMICS)  return "comics";
    if (pageId == PAGE_BOOKS)   return "books";
    if (pageId == PAGE_VIDEOS)  return "videos";
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
    m_quitRequested = true;
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
