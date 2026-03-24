#include "TankorentPage.h"
#include "core/CoreBridge.h"
#include "core/TorrentIndexer.h"
#include "core/indexers/TorrentsCsvIndexer.h"
#include "core/indexers/NyaaIndexer.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QNetworkAccessManager>
#include <algorithm>

// ── Constructor ─────────────────────────────────────────────────────────────
TankorentPage::TankorentPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent), m_bridge(bridge)
{
    qRegisterMetaType<TorrentResult>();
    qRegisterMetaType<QList<TorrentResult>>();

    m_nam = new QNetworkAccessManager(this);
    buildUI();
    populateSourceCombo();
}

// ── UI ──────────────────────────────────────────────────────────────────────
void TankorentPage::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 0);
    root->setSpacing(6);

    buildSearchControls(root);
    buildStatusRow(root);
    buildMainTabs(root);
}

// ── Search controls bar ─────────────────────────────────────────────────────
void TankorentPage::buildSearchControls(QVBoxLayout* parent)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    // Query input
    m_queryEdit = new QLineEdit;
    m_queryEdit->setPlaceholderText("Search torrents...");
    m_queryEdit->setFixedHeight(30);
    connect(m_queryEdit, &QLineEdit::returnPressed, this, &TankorentPage::startSearch);
    row->addWidget(m_queryEdit, 3);

    // Search type combo
    m_searchTypeCombo = new QComboBox;
    m_searchTypeCombo->setFixedHeight(30);
    m_searchTypeCombo->setMinimumWidth(150);
    m_searchTypeCombo->addItem("Videos",     "videos");
    m_searchTypeCombo->addItem("Books",      "books");
    m_searchTypeCombo->addItem("Audiobooks", "audiobooks");
    m_searchTypeCombo->addItem("Comics",     "comics");
    row->addWidget(m_searchTypeCombo, 1);

    // Source filter combo
    m_sourceCombo = new QComboBox;
    m_sourceCombo->setFixedHeight(30);
    m_sourceCombo->setMinimumWidth(160);
    row->addWidget(m_sourceCombo, 1);

    // Site category combo
    m_categoryCombo = new QComboBox;
    m_categoryCombo->setFixedHeight(30);
    m_categoryCombo->setMinimumWidth(220);
    m_categoryCombo->addItem("All categories", "");
    m_categoryCombo->setEnabled(false);
    row->addWidget(m_categoryCombo, 1);

    // Sort combo
    m_sortCombo = new QComboBox;
    m_sortCombo->setFixedHeight(30);
    m_sortCombo->setMinimumWidth(160);
    m_sortCombo->addItem("Sort: Relevance", "relevance");
    m_sortCombo->addItem("Sort: Seeders",   "seeders_desc");
    m_sortCombo->addItem("Sort: Size",      "size_desc");
    row->addWidget(m_sortCombo, 1);

    // Search button
    m_searchBtn = new QPushButton("Search");
    m_searchBtn->setFixedHeight(30);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, &TankorentPage::startSearch);
    row->addWidget(m_searchBtn);

    // Cancel button (hidden initially)
    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedHeight(30);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TankorentPage::cancelSearch);
    row->addWidget(m_cancelBtn);

    // Refresh button
    m_refreshBtn = new QPushButton("Refresh");
    m_refreshBtn->setFixedHeight(30);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    row->addWidget(m_refreshBtn);

    parent->addLayout(row);
}

// ── Status row ──────────────────────────────────────────────────────────────
void TankorentPage::buildStatusRow(QVBoxLayout* parent)
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

    m_backendStatus = new QLabel;
    m_backendStatus->setStyleSheet("color: #a1a1aa; font-size: 11px;");
    row->addWidget(m_backendStatus);

    parent->addLayout(row);
}

// ── Main tabs ───────────────────────────────────────────────────────────────
void TankorentPage::buildMainTabs(QVBoxLayout* parent)
{
    m_tabWidget = new QTabWidget;

    m_resultsTable = createResultsTable();
    m_tabWidget->addTab(m_resultsTable, "Search Results");

    m_transfersTable = createTransfersTable();
    m_tabWidget->addTab(m_transfersTable, "Transfers");

    parent->addWidget(m_tabWidget, 1);
}

// ── Results table ───────────────────────────────────────────────────────────
QTableWidget* TankorentPage::createResultsTable()
{
    auto *table = new QTableWidget(0, 8);
    table->setObjectName("SearchResultsTable");
    table->setMinimumHeight(280);

    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->setContextMenuPolicy(Qt::CustomContextMenu);

    QStringList headers = { "Title", "Category", "Source", "Size", "Files", "Seeders", "Leechers", "" };
    table->setHorizontalHeaderLabels(headers);

    auto *hdr = table->horizontalHeader();
    hdr->setMinimumSectionSize(60);
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 8; ++i)
        hdr->setSectionResizeMode(i, QHeaderView::Interactive);

    hdr->resizeSection(1, 130);
    hdr->resizeSection(2, 210);
    hdr->resizeSection(3, 110);
    hdr->resizeSection(4, 90);
    hdr->resizeSection(5, 90);
    hdr->resizeSection(6, 90);
    hdr->resizeSection(7, 60);

    return table;
}

// ── Transfers table ─────────────────────────────────────────────────────────
QTableWidget* TankorentPage::createTransfersTable()
{
    auto *table = new QTableWidget(0, 8);
    table->setObjectName("TransfersTable");

    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->setContextMenuPolicy(Qt::CustomContextMenu);

    QStringList headers = { "Name", "Size", "Progress", "Status", "Seeds", "Peers", "Down Speed", "ETA" };
    table->setHorizontalHeaderLabels(headers);

    auto *hdr = table->horizontalHeader();
    hdr->setMinimumSectionSize(60);
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 8; ++i)
        hdr->setSectionResizeMode(i, QHeaderView::Interactive);

    hdr->resizeSection(1, 100);
    hdr->resizeSection(2, 110);
    hdr->resizeSection(3, 100);
    hdr->resizeSection(4, 80);
    hdr->resizeSection(5, 80);
    hdr->resizeSection(6, 110);
    hdr->resizeSection(7, 80);

    return table;
}

// ── Source combo population ─────────────────────────────────────────────────
void TankorentPage::populateSourceCombo()
{
    m_sourceCombo->clear();
    m_sourceCombo->addItem("All Sources (2)", "all");
    m_sourceCombo->addItem("Nyaa",            "nyaa");
    m_sourceCombo->addItem("Torrents-CSV",    "torrentscsv");
}

// ── Search logic ────────────────────────────────────────────────────────────
void TankorentPage::startSearch()
{
    QString query = m_queryEdit->text().trimmed();
    if (query.isEmpty())
        return;

    // Cancel any running search
    cancelSearch();

    m_allResults.clear();
    m_resultsTable->setRowCount(0);
    m_pendingSearches = 0;

    QString sourceId = m_sourceCombo->currentData().toString();
    QString categoryId = m_categoryCombo->currentData().toString();

    // Determine which indexers to run
    QList<TorrentIndexer*> indexers;

    if (sourceId == "all" || sourceId == "nyaa") {
        auto *nyaa = new NyaaIndexer(m_nam, this);
        indexers.append(nyaa);
    }
    if (sourceId == "all" || sourceId == "torrentscsv") {
        auto *csv = new TorrentsCsvIndexer(m_nam, this);
        indexers.append(csv);
    }

    if (indexers.isEmpty())
        return;

    m_activeIndexers = indexers;
    m_pendingSearches = indexers.size();

    // UI state
    m_searchBtn->setVisible(false);
    m_cancelBtn->setVisible(true);
    m_searchStatus->setText("Searching...");

    // Launch all indexers
    for (auto *idx : indexers) {
        connect(idx, &TorrentIndexer::searchFinished, this, &TankorentPage::onSearchFinished);
        connect(idx, &TorrentIndexer::searchError, this, &TankorentPage::onSearchError);
        idx->search(query, 80, categoryId);
    }
}

void TankorentPage::cancelSearch()
{
    for (auto *idx : m_activeIndexers) {
        idx->disconnect(this);
        idx->deleteLater();
    }
    m_activeIndexers.clear();
    m_pendingSearches = 0;

    m_searchBtn->setVisible(true);
    m_cancelBtn->setVisible(false);
}

void TankorentPage::onSearchFinished(const QList<TorrentResult>& results)
{
    m_allResults.append(results);
    --m_pendingSearches;

    if (m_pendingSearches <= 0) {
        // All indexers done — clean up and render
        for (auto *idx : m_activeIndexers)
            idx->deleteLater();
        m_activeIndexers.clear();

        m_searchBtn->setVisible(true);
        m_cancelBtn->setVisible(false);

        // Apply sort
        QString sortMode = m_sortCombo->currentData().toString();
        if (sortMode == "seeders_desc") {
            std::sort(m_allResults.begin(), m_allResults.end(),
                      [](const TorrentResult& a, const TorrentResult& b) { return a.seeders > b.seeders; });
        } else if (sortMode == "size_desc") {
            std::sort(m_allResults.begin(), m_allResults.end(),
                      [](const TorrentResult& a, const TorrentResult& b) { return a.sizeBytes > b.sizeBytes; });
        }

        populateResults(m_allResults);
        m_searchStatus->setText(QString("%1 results").arg(m_allResults.size()));
    } else {
        m_searchStatus->setText(QString("Searching... %1 results").arg(m_allResults.size()));
    }
}

void TankorentPage::onSearchError(const QString& error)
{
    --m_pendingSearches;

    if (m_pendingSearches <= 0) {
        for (auto *idx : m_activeIndexers)
            idx->deleteLater();
        m_activeIndexers.clear();

        m_searchBtn->setVisible(true);
        m_cancelBtn->setVisible(false);

        if (m_allResults.isEmpty()) {
            m_searchStatus->setText("Search failed: " + error);
        } else {
            populateResults(m_allResults);
            m_searchStatus->setText(QString("%1 results (some sources failed)").arg(m_allResults.size()));
        }
    }
}

void TankorentPage::populateResults(const QList<TorrentResult>& results)
{
    m_resultsTable->setRowCount(0);
    m_resultsTable->setRowCount(results.size());

    for (int i = 0; i < results.size(); ++i) {
        const auto& r = results[i];

        m_resultsTable->setItem(i, 0, new QTableWidgetItem(r.title));
        m_resultsTable->setItem(i, 1, new QTableWidgetItem(r.category.isEmpty() ? r.categoryId : r.category));
        m_resultsTable->setItem(i, 2, new QTableWidgetItem(r.sourceName));
        m_resultsTable->setItem(i, 3, new QTableWidgetItem(humanSize(r.sizeBytes)));

        auto *filesItem = new QTableWidgetItem("-");
        filesItem->setTextAlignment(Qt::AlignCenter);
        m_resultsTable->setItem(i, 4, filesItem);

        auto *seedItem = new QTableWidgetItem(QString::number(r.seeders));
        seedItem->setTextAlignment(Qt::AlignCenter);
        m_resultsTable->setItem(i, 5, seedItem);

        auto *leechItem = new QTableWidgetItem(QString::number(r.leechers));
        leechItem->setTextAlignment(Qt::AlignCenter);
        m_resultsTable->setItem(i, 6, leechItem);

        auto *actionItem = new QTableWidgetItem("+");
        actionItem->setTextAlignment(Qt::AlignCenter);
        m_resultsTable->setItem(i, 7, actionItem);
    }

    m_tabWidget->setCurrentIndex(0);
}
