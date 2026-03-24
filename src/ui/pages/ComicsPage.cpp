#include "ComicsPage.h"
#include "TileStrip.h"
#include "TileCard.h"
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

    // Header
    auto* header = new QWidget(this);
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(24, 20, 24, 12);

    auto* title = new QLabel("Comics", header);
    title->setObjectName("SectionTitle");
    headerLayout->addWidget(title);

    layout->addWidget(header);

    // Status label (shown when empty or scanning)
    m_statusLabel = new QLabel("Add a comics folder to get started", this);
    m_statusLabel->setObjectName("TileSubtitle");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 60px;");
    layout->addWidget(m_statusLabel);

    // Tile grid
    m_tileStrip = new TileStrip(this);
    m_tileStrip->hide();
    layout->addWidget(m_tileStrip, 1);
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
    // Show tile strip on first result
    if (m_statusLabel->isVisible()) {
        m_statusLabel->hide();
        m_tileStrip->show();
    }

    QString subtitle = QString::number(series.fileCount)
                     + (series.fileCount == 1 ? " issue" : " issues");

    auto* card = new TileCard(series.coverThumbPath, series.seriesName, subtitle);
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
