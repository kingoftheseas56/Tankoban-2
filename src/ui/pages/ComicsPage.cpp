#include "ComicsPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "SeriesView.h"
#include "core/CoreBridge.h"
#include "core/LibraryScanner.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QSettings>

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
    headerLayout->setSpacing(8);

    // Title row: title left, sort combo right
    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("Comics", header);
    title->setObjectName("SectionTitle");
    titleRow->addWidget(title);
    titleRow->addStretch();

    m_sortCombo = new QComboBox(header);
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
    // Load saved sort preference
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
    titleRow->addWidget(m_sortCombo);
    headerLayout->addLayout(titleRow);

    // Search bar
    m_searchBar = new QLineEdit(header);
    m_searchBar->setPlaceholderText("Search series and volumes\u2026");
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
    connect(m_searchTimer, &QTimer::timeout, this, &ComicsPage::applySearch);

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

    // Store series data for click handling and sorting
    card->setProperty("seriesPath", series.seriesPath);
    card->setProperty("seriesName", series.seriesName);
    card->setProperty("fileCount", series.fileCount);
    card->setProperty("newestMtime", series.newestMtimeMs);
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
    } else {
        // Apply current sort order
        m_tileStrip->sortTiles(m_sortCombo->currentData().toString());
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

void ComicsPage::applySearch()
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
