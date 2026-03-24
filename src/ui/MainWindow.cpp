#include "MainWindow.h"
#include "GlassBackground.h"
#include "RootFoldersOverlay.h"
#include "pages/ComicsPage.h"
#include "pages/BooksPage.h"
#include "pages/VideosPage.h"
#include "readers/ComicReader.h"
#include "core/CoreBridge.h"

#include <QVBoxLayout>
#include <QFrame>
#include <QApplication>
#include <QResizeEvent>
#include <QScreen>
#include <QButtonGroup>

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

    rootLayout->addWidget(content, 1);

    // Root folders overlay (hidden by default)
    m_rootFoldersOverlay = new RootFoldersOverlay(m_bridge, root);
    m_rootFoldersOverlay->hide();
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
    m_comicReader = new ComicReader(root);
    m_comicReader->hide();
    connect(m_comicReader, &ComicReader::closeRequested, this, &MainWindow::closeComicReader);

    setCentralWidget(root);
    bindShortcuts();
    setupTrayIcon();

    // Connect comics page to reader
    if (auto *comics = m_pageStack->findChild<ComicsPage*>()) {
        connect(comics, &ComicsPage::openComic, this, &MainWindow::openComicReader);
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
    m_pageStack = new QStackedWidget(this);

    // Real pages
    auto *comicsPage = new ComicsPage(m_bridge);
    m_pageStack->addWidget(comicsPage);

    auto *booksPage = new BooksPage(m_bridge);
    m_pageStack->addWidget(booksPage);

    auto *videosPage = new VideosPage(m_bridge);
    m_pageStack->addWidget(videosPage);

    // Placeholder pages for the rest
    struct PageDef { const char *id; const char *title; const char *subtitle; };
    const PageDef pages[] = {
        { PAGE_STREAM,  "Stream", "Stream content will appear here."     },
        { PAGE_SOURCES, "Sources", "Browse and search content sources."  },
    };

    for (const auto &def : pages) {
        auto *page = new QWidget();
        page->setObjectName(def.id);

        auto *layout = new QVBoxLayout(page);
        layout->setAlignment(Qt::AlignCenter);
        layout->setSpacing(12);

        auto *title = new QLabel(def.title);
        title->setObjectName("SectionTitle");
        title->setAlignment(Qt::AlignCenter);
        layout->addWidget(title);

        auto *subtitle = new QLabel(def.subtitle);
        subtitle->setObjectName("TileSubtitle");
        subtitle->setAlignment(Qt::AlignCenter);
        subtitle->setWordWrap(true);
        layout->addWidget(subtitle);

        m_pageStack->addWidget(page);
    }
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
    if (m_wasMaximizedBeforeHide)
        showMaximized();
    else
        showNormal();
    raise();
    activateWindow();
}

void MainWindow::quitFromTray()
{
    m_quitRequested = true;
    close();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // If tray is available and user didn't explicitly quit, hide to tray
    bool trayAvailable = !m_quitRequested
                      && m_trayIcon != nullptr
                      && m_trayIcon->isVisible();
    if (trayAvailable) {
        event->ignore();
        hideToTray();
        return;
    }

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
void MainWindow::openComicReader(const QString& cbzPath)
{
    m_comicReader->openBook(cbzPath);
    m_comicReader->setGeometry(centralWidget()->rect());
    m_comicReader->show();
    m_comicReader->raise();
    m_comicReader->setFocus();
}

void MainWindow::closeComicReader()
{
    m_comicReader->hide();
}
