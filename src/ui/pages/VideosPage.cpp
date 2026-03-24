#include "VideosPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "ShowView.h"
#include "core/CoreBridge.h"
#include "core/VideosScanner.h"

#include <QVBoxLayout>
#include <QMetaObject>

VideosPage::VideosPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setObjectName("videos");
    qRegisterMetaType<ShowInfo>("ShowInfo");
    qRegisterMetaType<QList<ShowInfo>>("QList<ShowInfo>");

    buildUI();

    m_scanThread = new QThread(this);
    m_scanner = new VideosScanner();
    m_scanner->moveToThread(m_scanThread);

    connect(m_scanner, &VideosScanner::showFound,
            this, &VideosPage::onShowFound, Qt::QueuedConnection);
    connect(m_scanner, &VideosScanner::scanFinished,
            this, &VideosPage::onScanFinished, Qt::QueuedConnection);

    m_scanThread->start();

    connect(m_bridge, &CoreBridge::rootFoldersChanged, this, [this](const QString& domain) {
        if (domain == "videos")
            triggerScan();
    });
}

VideosPage::~VideosPage()
{
    m_scanThread->quit();
    m_scanThread->wait();
    delete m_scanner;
}

void VideosPage::buildUI()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_stack = new QStackedWidget(this);

    // ── Grid view (index 0) ──
    auto* gridPage = new QWidget();
    auto* gridLayout = new QVBoxLayout(gridPage);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(0);

    auto* header = new QWidget(gridPage);
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(24, 20, 24, 12);
    auto* title = new QLabel("Videos", header);
    title->setObjectName("SectionTitle");
    headerLayout->addWidget(title);

    m_searchBar = new QLineEdit(header);
    m_searchBar->setPlaceholderText("Search shows\u2026");
    m_searchBar->setClearButtonEnabled(true);
    m_searchBar->setObjectName("LibrarySearch");
    m_searchBar->setFixedHeight(32);
    m_searchBar->setStyleSheet(
        "QLineEdit { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px; color: #eee; padding: 4px 10px; font-size: 13px; }"
        "QLineEdit:focus { border: 1px solid rgba(255,255,255,0.3); }");
    headerLayout->addWidget(m_searchBar);

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(250);
    connect(m_searchBar, &QLineEdit::textChanged, this, [this]() { m_searchTimer->start(); });
    connect(m_searchTimer, &QTimer::timeout, this, &VideosPage::applySearch);

    gridLayout->addWidget(header);

    m_statusLabel = new QLabel("Add a videos folder to get started", gridPage);
    m_statusLabel->setObjectName("TileSubtitle");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 60px;");
    gridLayout->addWidget(m_statusLabel);

    m_tileStrip = new TileStrip(gridPage);
    m_tileStrip->hide();
    gridLayout->addWidget(m_tileStrip, 1);

    m_stack->addWidget(gridPage);

    // ── Show view (index 1) ──
    m_showView = new ShowView(m_bridge);
    connect(m_showView, &ShowView::backRequested, this, &VideosPage::showGrid);
    connect(m_showView, &ShowView::episodeSelected, this, [this](const QString& filePath) {
        emit playVideo(filePath);
    });
    m_stack->addWidget(m_showView);

    outerLayout->addWidget(m_stack, 1);
}

void VideosPage::activate()
{
    if (!m_hasScanned)
        triggerScan();
}

void VideosPage::triggerScan()
{
    if (m_scanning) return;
    m_scanning = true;

    QStringList roots = m_bridge->rootFolders("videos");
    if (roots.isEmpty()) {
        m_tileStrip->clear();
        m_tileStrip->hide();
        m_statusLabel->setText("Add a videos folder to get started");
        m_statusLabel->show();
        m_hasScanned = true;
        m_scanning = false;
        return;
    }

    m_tileStrip->clear();
    m_statusLabel->setText("Scanning...");
    m_statusLabel->show();
    m_tileStrip->hide();

    QMetaObject::invokeMethod(m_scanner, "scan", Qt::QueuedConnection,
                              Q_ARG(QStringList, roots));
}

void VideosPage::onShowFound(const ShowInfo& show)
{
    if (m_statusLabel->isVisible()) {
        m_statusLabel->hide();
        m_tileStrip->show();
    }

    QString subtitle;
    if (show.episodeCount == 1)
        subtitle = formatSize(show.totalSizeBytes);
    else
        subtitle = QString::number(show.episodeCount) + " episodes \u00B7 " + formatSize(show.totalSizeBytes);

    auto* card = new TileCard("", show.showName, subtitle);
    card->setProperty("seriesPath", show.showPath);
    card->setProperty("seriesName", show.showName);

    connect(card, &TileCard::clicked, this, [this, card]() {
        onTileClicked(card->property("seriesPath").toString(),
                      card->property("seriesName").toString());
    });
    m_tileStrip->addTile(card);
}

void VideosPage::onScanFinished(const QList<ShowInfo>& allShows)
{
    m_hasScanned = true;
    m_scanning = false;

    if (allShows.isEmpty()) {
        m_tileStrip->hide();
        m_statusLabel->setText("No videos found in your library folders");
        m_statusLabel->show();
    }
}

void VideosPage::onTileClicked(const QString& showPath, const QString& showName)
{
    m_showView->showFolder(showPath, showName);
    m_stack->setCurrentIndex(1);
}

void VideosPage::showGrid()
{
    m_stack->setCurrentIndex(0);
}

void VideosPage::applySearch()
{
    m_tileStrip->filterTiles(m_searchBar->text());

    if (m_tileStrip->visibleCount() == 0 && !m_searchBar->text().trimmed().isEmpty()) {
        m_statusLabel->setText(
            QString("No results for \"%1\"").arg(m_searchBar->text().trimmed()));
        m_statusLabel->show();
        m_tileStrip->hide();
    } else if (m_tileStrip->visibleCount() > 0) {
        m_statusLabel->hide();
        m_tileStrip->show();
    }
}

QString VideosPage::formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024) + " KB";
    if (bytes < 1024LL * 1024 * 1024)
        return QString::number(bytes / (1024 * 1024)) + " MB";
    double gb = bytes / (1024.0 * 1024.0 * 1024.0);
    return QString::number(gb, 'f', 1) + " GB";
}
