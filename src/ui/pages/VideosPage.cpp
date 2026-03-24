#include "VideosPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "core/CoreBridge.h"
#include "core/VideosScanner.h"

#include <QVBoxLayout>
#include <QMetaObject>
#include <QDir>
#include <QCollator>

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
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(24, 20, 24, 12);
    auto* title = new QLabel("Videos", header);
    title->setObjectName("SectionTitle");
    headerLayout->addWidget(title);
    layout->addWidget(header);

    m_statusLabel = new QLabel("Add a videos folder to get started", this);
    m_statusLabel->setObjectName("TileSubtitle");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 60px;");
    layout->addWidget(m_statusLabel);

    m_tileStrip = new TileStrip(this);
    m_tileStrip->hide();
    layout->addWidget(m_tileStrip, 1);
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
        subtitle = QString::number(show.episodeCount) + " episodes · " + formatSize(show.totalSizeBytes);

    auto* card = new TileCard("", show.showName, subtitle);
    QString showPath = show.showPath;
    connect(card, &TileCard::clicked, this, [this, showPath]() {
        // Find first video file in show folder
        QDir dir(showPath);
        QStringList exts = {"*.mp4","*.mkv","*.avi","*.webm","*.mov","*.wmv","*.flv","*.m4v","*.ts"};
        auto files = dir.entryInfoList(exts, QDir::Files);
        if (!files.isEmpty()) {
            QCollator col;
            col.setNumericMode(true);
            std::sort(files.begin(), files.end(), [&](const QFileInfo& a, const QFileInfo& b) {
                return col.compare(a.fileName(), b.fileName()) < 0;
            });
            emit playVideo(files.first().absoluteFilePath());
        }
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
