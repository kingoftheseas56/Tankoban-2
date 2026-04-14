#include "TankoyomiPage.h"
#include "core/CoreBridge.h"
#include "core/JsonStore.h"
#include "core/manga/MangaScraper.h"
#include "core/manga/WeebCentralScraper.h"
#include "core/manga/ReadComicsScraper.h"
#include "core/manga/MangaDownloader.h"
#include "ui/dialogs/AddMangaDialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QPalette>
#include <QStyleFactory>

#include "ui/ContextMenuHelper.h"

// ── Constructor ─────────────────────────────────────────────────────────────
TankoyomiPage::TankoyomiPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent), m_bridge(bridge)
{
    qRegisterMetaType<MangaResult>();
    qRegisterMetaType<QList<MangaResult>>();
    qRegisterMetaType<ChapterInfo>();
    qRegisterMetaType<QList<ChapterInfo>>();

    m_nam = new QNetworkAccessManager(this);

    // Create scrapers
    auto* weeb = new WeebCentralScraper(m_nam, this);
    auto* rco  = new ReadComicsScraper(m_nam, this);
    m_scrapers = { weeb, rco };

    // Create downloader
    m_downloader = new MangaDownloader(&m_bridge->store(), this);
    m_downloader->setScraper("weebcentral", weeb);
    m_downloader->setScraper("readcomicsonline", rco);

    buildUI();

    // Populate source combo
    m_sourceCombo->addItem("WeebCentral", "weebcentral");
    m_sourceCombo->addItem("ReadComicsOnline", "readcomicsonline");

    // Wire search signals
    for (auto* scraper : m_scrapers) {
        connect(scraper, &MangaScraper::searchFinished, this, [this](const QList<MangaResult>& results) {
            m_allResults.append(results);
            --m_pendingSearches;
            if (m_pendingSearches <= 0) {
                m_searchStatus->setText(QString("Done: %1 Results").arg(m_allResults.size()));
                m_cancelBtn->setVisible(false);
                m_searchBtn->setVisible(true);
            }
            renderResults();
        });
        connect(scraper, &MangaScraper::errorOccurred, this, [this](const QString& err) {
            --m_pendingSearches;
            if (m_pendingSearches <= 0) {
                m_searchStatus->setText(m_allResults.isEmpty() ? "Search Failed: " + err
                                                                : QString("Done: %1 Results").arg(m_allResults.size()));
                m_cancelBtn->setVisible(false);
                m_searchBtn->setVisible(true);
            }
        });
    }

    // Double-click on result → open chapter picker
    connect(m_resultsTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        onResultDoubleClicked(row);
    });

    // Results context menu
    connect(m_resultsTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int row = m_resultsTable->rowAt(pos.y());
        if (row < 0 || row >= m_displayedResults.size()) return;
        const auto& result = m_displayedResults[row];

        QMenu* menu = ContextMenuHelper::createMenu(this);
        menu->addAction("Download...", this, [this, row]() {
            onResultDoubleClicked(row);
        });
        menu->addSeparator();
        menu->addAction("Copy Title", this, [result]() {
            ContextMenuHelper::copyToClipboard(result.title);
        });
        menu->exec(m_resultsTable->viewport()->mapToGlobal(pos));
        delete menu;
    });

    // Transfers context menu
    connect(m_transfersTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int row = m_transfersTable->rowAt(pos.y());
        if (row < 0) return;
        auto* item = m_transfersTable->item(row, 0);
        if (!item) return;
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;

        QMenu* menu = ContextMenuHelper::createMenu(this);
        menu->addAction("Cancel", this, [this, id]() { m_downloader->cancelDownload(id); });
        menu->addSeparator();

        auto* removeAction = ContextMenuHelper::addDangerAction(menu, "Remove");
        connect(removeAction, &QAction::triggered, this, [this, id]() {
            m_downloader->removeDownload(id);
        });

        auto* removeWithFiles = ContextMenuHelper::addDangerAction(menu, "Remove + Delete Files");
        connect(removeWithFiles, &QAction::triggered, this, [this, id]() {
            if (ContextMenuHelper::confirmRemove(this, "Delete Files",
                    "Remove download and delete all files?"))
                m_downloader->removeWithData(id);
        });

        menu->exec(m_transfersTable->viewport()->mapToGlobal(pos));
        delete menu;
    });

    // Auto-refresh transfers
    m_transferTimer = new QTimer(this);
    connect(m_transferTimer, &QTimer::timeout, this, &TankoyomiPage::refreshTransfers);
    m_transferTimer->start(1000);
}

// ── UI ──────────────────────────────────────────────────────────────────────
void TankoyomiPage::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 0);
    root->setSpacing(6);

    buildSearchControls(root);
    buildStatusRow(root);
    buildMainTabs(root);
}

void TankoyomiPage::buildSearchControls(QVBoxLayout* parent)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    m_queryEdit = new QLineEdit;
    m_queryEdit->setPlaceholderText("Search manga & comics...");
    m_queryEdit->setFixedHeight(30);
    connect(m_queryEdit, &QLineEdit::returnPressed, this, &TankoyomiPage::startSearch);
    row->addWidget(m_queryEdit, 3);

    m_sourceCombo = new QComboBox;
    m_sourceCombo->setFixedHeight(30);
    m_sourceCombo->setMinimumWidth(160);
    m_sourceCombo->addItem("All Sources", "all");
    row->addWidget(m_sourceCombo, 1);

    m_searchBtn = new QPushButton("Search");
    m_searchBtn->setFixedHeight(30);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, &TankoyomiPage::startSearch);
    row->addWidget(m_searchBtn);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedHeight(30);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TankoyomiPage::cancelSearch);
    row->addWidget(m_cancelBtn);

    m_refreshBtn = new QPushButton("Refresh");
    m_refreshBtn->setFixedHeight(30);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &TankoyomiPage::refreshTransfers);
    row->addWidget(m_refreshBtn);

    parent->addLayout(row);
}

void TankoyomiPage::buildStatusRow(QVBoxLayout* parent)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    m_searchStatus = new QLabel("Ready");
    m_searchStatus->setStyleSheet("color: #a1a1aa; font-size: 11px;");
    row->addWidget(m_searchStatus, 2);

    m_downloadStatus = new QLabel("Active: 0 | History: 0");
    m_downloadStatus->setStyleSheet("color: #a1a1aa; font-size: 11px;");
    row->addWidget(m_downloadStatus, 1);

    parent->addLayout(row);
}

void TankoyomiPage::buildMainTabs(QVBoxLayout* parent)
{
    m_tabWidget = new QTabWidget;

    m_resultsTable = createResultsTable();
    m_tabWidget->addTab(m_resultsTable, "Search Results");

    m_transfersTable = createTransfersTable();
    m_tabWidget->addTab(m_transfersTable, "Transfers");

    parent->addWidget(m_tabWidget, 1);
}

QTableWidget* TankoyomiPage::createResultsTable()
{
    auto *table = new QTableWidget(0, 5);
    table->setObjectName("MangaResultsTable");
    table->setMinimumHeight(280);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->setContextMenuPolicy(Qt::CustomContextMenu);

    QStringList headers = { "Title", "Author", "Source", "Status", "Type" };
    table->setHorizontalHeaderLabels(headers);

    auto *hdr = table->horizontalHeader();
    hdr->setMinimumSectionSize(80);
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 5; ++i)
        hdr->setSectionResizeMode(i, QHeaderView::Interactive);
    hdr->resizeSection(1, 160);
    hdr->resizeSection(2, 140);
    hdr->resizeSection(3, 120);
    hdr->resizeSection(4, 100);

    table->setStyle(QStyleFactory::create("Fusion"));
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setFocusPolicy(Qt::NoFocus);

    QPalette pal = table->palette();
    pal.setColor(QPalette::Base,            QColor(0x11, 0x11, 0x11));
    pal.setColor(QPalette::AlternateBase,   QColor(0x18, 0x18, 0x18));
    pal.setColor(QPalette::Text,            QColor(0xee, 0xee, 0xee));
    pal.setColor(QPalette::Highlight,       QColor(192, 200, 212, 36));
    pal.setColor(QPalette::HighlightedText, QColor(0xee, 0xee, 0xee));
    table->setPalette(pal);

    table->setStyleSheet(QStringLiteral(
        "#MangaResultsTable { border: none; outline: none; font-size: 12px; }"
        "#MangaResultsTable::item { padding: 0 8px; }"
        "#MangaResultsTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
        "#MangaResultsTable QHeaderView::section {"
        "  background: #1a1a1a; color: #888; border: none;"
        "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
        "  padding: 4px 8px; font-size: 11px; }"
    ));

    return table;
}

QTableWidget* TankoyomiPage::createTransfersTable()
{
    auto *table = new QTableWidget(0, 4);
    table->setObjectName("MangaTransfersTable");
    table->setMinimumHeight(220);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(26);
    table->setContextMenuPolicy(Qt::CustomContextMenu);

    QStringList headers = { "Series", "Progress", "Status", "Chapters" };
    table->setHorizontalHeaderLabels(headers);

    auto *hdr = table->horizontalHeader();
    hdr->setMinimumSectionSize(80);
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 4; ++i)
        hdr->setSectionResizeMode(i, QHeaderView::Interactive);
    hdr->resizeSection(1, 100);
    hdr->resizeSection(2, 120);
    hdr->resizeSection(3, 120);

    table->setStyle(QStyleFactory::create("Fusion"));
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setFocusPolicy(Qt::NoFocus);

    QPalette pal = table->palette();
    pal.setColor(QPalette::Base,            QColor(0x11, 0x11, 0x11));
    pal.setColor(QPalette::AlternateBase,   QColor(0x18, 0x18, 0x18));
    pal.setColor(QPalette::Text,            QColor(0xee, 0xee, 0xee));
    pal.setColor(QPalette::Highlight,       QColor(192, 200, 212, 36));
    pal.setColor(QPalette::HighlightedText, QColor(0xee, 0xee, 0xee));
    table->setPalette(pal);

    table->setStyleSheet(QStringLiteral(
        "#MangaTransfersTable { border: none; outline: none; font-size: 12px; }"
        "#MangaTransfersTable::item { padding: 0 8px; }"
        "#MangaTransfersTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
        "#MangaTransfersTable QHeaderView::section {"
        "  background: #1a1a1a; color: #888; border: none;"
        "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
        "  padding: 4px 8px; font-size: 11px; }"
    ));

    return table;
}

// ── Search ──────────────────────────────────────────────────────────────────
void TankoyomiPage::startSearch()
{
    QString query = m_queryEdit->text().trimmed();
    if (query.isEmpty()) return;

    m_allResults.clear();
    m_searchStatus->setText("Searching...");
    m_searchBtn->setVisible(false);
    m_cancelBtn->setVisible(true);

    QString selectedSource = m_sourceCombo->currentData().toString();

    m_pendingSearches = 0;
    for (auto* scraper : m_scrapers) {
        if (selectedSource == "all" || scraper->sourceId() == selectedSource) {
            scraper->search(query);
            ++m_pendingSearches;
        }
    }
}

void TankoyomiPage::cancelSearch()
{
    for (auto* scraper : m_scrapers)
        scraper->disconnect(this);

    // Reconnect search signals (they were disconnected above)
    for (auto* scraper : m_scrapers) {
        connect(scraper, &MangaScraper::searchFinished, this, [this](const QList<MangaResult>& results) {
            m_allResults.append(results);
            --m_pendingSearches;
            if (m_pendingSearches <= 0) {
                m_searchStatus->setText(QString("Done: %1 Results").arg(m_allResults.size()));
                m_cancelBtn->setVisible(false);
                m_searchBtn->setVisible(true);
            }
            renderResults();
        });
        connect(scraper, &MangaScraper::errorOccurred, this, [this](const QString& err) {
            --m_pendingSearches;
            if (m_pendingSearches <= 0) {
                m_searchStatus->setText(m_allResults.isEmpty() ? "Search Failed: " + err
                                                                : QString("Done: %1 Results").arg(m_allResults.size()));
                m_cancelBtn->setVisible(false);
                m_searchBtn->setVisible(true);
            }
        });
    }

    m_pendingSearches = 0;
    m_searchBtn->setVisible(true);
    m_cancelBtn->setVisible(false);
    m_searchStatus->setText("Search Cancelled");
}

void TankoyomiPage::renderResults()
{
    // Dedup by normalized title
    QSet<QString> seen;
    m_displayedResults.clear();
    for (const auto& r : m_allResults) {
        QString key = r.title.toLower().trimmed();
        if (seen.contains(key)) continue;
        seen.insert(key);
        m_displayedResults.append(r);
    }

    m_resultsTable->setRowCount(m_displayedResults.size());

    for (int i = 0; i < m_displayedResults.size(); ++i) {
        const auto& r = m_displayedResults[i];

        auto* titleItem = new QTableWidgetItem(r.title);
        titleItem->setData(Qt::UserRole, r.source);       // source ID
        titleItem->setData(Qt::UserRole + 1, r.id);       // series ID
        m_resultsTable->setItem(i, 0, titleItem);

        m_resultsTable->setItem(i, 1, new QTableWidgetItem(r.author));
        m_resultsTable->setItem(i, 2, new QTableWidgetItem(r.source));
        m_resultsTable->setItem(i, 3, new QTableWidgetItem(r.status));
        m_resultsTable->setItem(i, 4, new QTableWidgetItem(r.type));
    }

    m_tabWidget->setCurrentIndex(0);
}

// ── Result double-click → chapter picker ────────────────────────────────────
void TankoyomiPage::onResultDoubleClicked(int row)
{
    if (row < 0 || row >= m_displayedResults.size()) return;
    const auto& result = m_displayedResults[row];

    // Get default comics path
    QStringList comicRoots = m_bridge->rootFolders("comics");
    QString defaultDest = comicRoots.isEmpty() ? QString() : comicRoots.first();

    AddMangaDialog dlg(result.title, result.source, defaultDest, this);

    // Find the right scraper
    MangaScraper* scraper = nullptr;
    for (auto* s : m_scrapers) {
        if (s->sourceId() == result.source) { scraper = s; break; }
    }
    if (!scraper) return;

    // Fetch chapters and populate dialog
    auto conn = std::make_shared<QMetaObject::Connection>();
    auto errConn = std::make_shared<QMetaObject::Connection>();

    *conn = connect(scraper, &MangaScraper::chaptersReady, &dlg,
        [&dlg, conn, errConn](const QList<ChapterInfo>& chapters) {
            disconnect(*conn);
            disconnect(*errConn);
            dlg.populateChapters(chapters);
        });

    *errConn = connect(scraper, &MangaScraper::errorOccurred, &dlg,
        [&dlg, conn, errConn](const QString& msg) {
            disconnect(*conn);
            disconnect(*errConn);
            dlg.showError(msg);
        });

    scraper->fetchChapters(result.id);

    if (dlg.exec() == QDialog::Accepted) {
        auto chapters = dlg.selectedChapters();
        if (chapters.isEmpty()) return;

        m_downloader->startDownload(result.title, result.source,
                                     chapters, dlg.destinationPath(), dlg.format());
        m_tabWidget->setCurrentIndex(1);  // Switch to Transfers
        m_searchStatus->setText("Download Started");
    }
}

// ── Transfers refresh ───────────────────────────────────────────────────────
void TankoyomiPage::refreshTransfers()
{
    auto active = m_downloader->listActive();

    m_transfersTable->setRowCount(active.size());
    int activeCount = 0;

    for (int i = 0; i < active.size(); ++i) {
        const auto& r = active[i];

        auto* nameItem = m_transfersTable->item(i, 0);
        if (!nameItem) { nameItem = new QTableWidgetItem; m_transfersTable->setItem(i, 0, nameItem); }
        nameItem->setText(r.seriesTitle);
        nameItem->setData(Qt::UserRole, r.id);

        auto* progItem = m_transfersTable->item(i, 1);
        if (!progItem) { progItem = new QTableWidgetItem; m_transfersTable->setItem(i, 1, progItem); }
        progItem->setText(QString::number(r.progress * 100, 'f', 0) + "%");
        progItem->setTextAlignment(Qt::AlignCenter);

        auto* stateItem = m_transfersTable->item(i, 2);
        if (!stateItem) { stateItem = new QTableWidgetItem; m_transfersTable->setItem(i, 2, stateItem); }
        stateItem->setText(r.status);

        auto* chapItem = m_transfersTable->item(i, 3);
        if (!chapItem) { chapItem = new QTableWidgetItem; m_transfersTable->setItem(i, 3, chapItem); }
        chapItem->setText(QString("%1/%2").arg(r.completedChapters).arg(r.totalChapters));
        chapItem->setTextAlignment(Qt::AlignCenter);

        if (r.status == "downloading") ++activeCount;
    }

    auto history = m_downloader->listHistory();
    m_downloadStatus->setText(QString("Active: %1 | History: %2").arg(activeCount).arg(history.size()));

    // Tab badge
    m_tabWidget->setTabText(1, active.isEmpty() ? "Transfers" : QString("Transfers (%1)").arg(active.size()));
}
