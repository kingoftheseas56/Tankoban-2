#include "ComicsPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "SeriesView.h"
#include "core/CoreBridge.h"
#include "core/LibraryScanner.h"

#include <QVBoxLayout>
#include <QMetaObject>

ComicsPage::ComicsPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setObjectName("comics");
    qRegisterMetaType<SeriesInfo>("SeriesInfo");
    qRegisterMetaType<QList<SeriesInfo>>("QList<SeriesInfo>");

    buildUI();

    // Background scanner thread
    m_scanThread = new QThread(this);
    m_scanner = new LibraryScanner(m_bridge->dataDir() + "/thumbs");
    m_scanner->moveToThread(m_scanThread);

    connect(m_scanner, &LibraryScanner::seriesFound,
            this, &ComicsPage::onSeriesFound, Qt::QueuedConnection);
    connect(m_scanner, &LibraryScanner::scanFinished,
            this, &ComicsPage::onScanFinished, Qt::QueuedConnection);

    m_scanThread->start();

    // Re-scan when root folders change
    connect(m_bridge, &CoreBridge::rootFoldersChanged, this, [this](const QString& domain) {
        if (domain == "comics")
            triggerScan();
    });
}

ComicsPage::~ComicsPage()
{
    m_scanThread->quit();
    m_scanThread->wait();
    delete m_scanner;
}

void ComicsPage::buildUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_stack = new QStackedWidget(this);

    // ── Grid view (index 0) ──
    auto* gridPage = new QWidget();
    auto* gridLayout = new QVBoxLayout(gridPage);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(0);

    auto* header = new QWidget(gridPage);
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(24, 20, 24, 12);
    auto* title = new QLabel("Comics", header);
    title->setObjectName("SectionTitle");
    headerLayout->addWidget(title);
    gridLayout->addWidget(header);

    m_statusLabel = new QLabel("Add a comics folder to get started", gridPage);
    m_statusLabel->setObjectName("TileSubtitle");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 60px;");
    gridLayout->addWidget(m_statusLabel);

    m_tileStrip = new TileStrip(gridPage);
    m_tileStrip->hide();
    gridLayout->addWidget(m_tileStrip, 1);

    m_stack->addWidget(gridPage);

    // ── Series view (index 1) ──
    m_seriesView = new SeriesView();
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
    if (m_scanning) return;
    m_scanning = true;

    QStringList roots = m_bridge->rootFolders("comics");
    if (roots.isEmpty()) {
        m_tileStrip->clear();
        m_tileStrip->hide();
        m_statusLabel->setText("Add a comics folder to get started");
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

void ComicsPage::onSeriesFound(const SeriesInfo& series)
{
    if (m_statusLabel->isVisible()) {
        m_statusLabel->hide();
        m_tileStrip->show();
    }

    QString subtitle = QString::number(series.fileCount)
                     + (series.fileCount == 1 ? " issue" : " issues");

    auto* card = new TileCard(series.coverThumbPath, series.seriesName, subtitle);

    // Store series data for click handling
    card->setProperty("seriesPath", series.seriesPath);
    card->setProperty("seriesName", series.seriesName);
    connect(card, &TileCard::clicked, this, [this, card]() {
        onTileClicked(card->property("seriesPath").toString(),
                      card->property("seriesName").toString());
    });

    m_tileStrip->addTile(card);
}

void ComicsPage::onScanFinished(const QList<SeriesInfo>& allSeries)
{
    m_hasScanned = true;
    m_scanning = false;

    if (allSeries.isEmpty()) {
        m_tileStrip->hide();
        m_statusLabel->setText("No comics found in your library folders");
        m_statusLabel->show();
    }
}

void ComicsPage::onTileClicked(const QString& seriesPath, const QString& seriesName)
{
    m_seriesView->showSeries(seriesPath, seriesName);
    m_stack->setCurrentIndex(1);
}

void ComicsPage::showGrid()
{
    m_stack->setCurrentIndex(0);
}
