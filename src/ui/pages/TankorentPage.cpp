#include "TankorentPage.h"
#include "core/CoreBridge.h"
#include "core/TorrentIndexer.h"
#include "core/indexers/TorrentsCsvIndexer.h"
#include "core/indexers/NyaaIndexer.h"
#include "core/indexers/PirateBayIndexer.h"
#include "core/indexers/YtsIndexer.h"
#include "core/indexers/EztvIndexer.h"
#include "core/indexers/X1337xIndexer.h"
#include "core/indexers/ExtTorrentsIndexer.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QNetworkAccessManager>
#include <QApplication>
#include <QClipboard>
#include <QRegularExpression>
#include <algorithm>

// ══════════════════════════════════════════════════════════════════════════════
// Per-site category options — faithfully ported from _SITE_CATEGORY_OPTIONS
// Each entry: { display_text, value }
// ══════════════════════════════════════════════════════════════════════════════

struct CategoryOption { const char* label; const char* value; };

static const CategoryOption NYAA_CATEGORIES[] = {
    {"All categories",             "0_0"},
    {"Anime - Music Video",        "1_1"},
    {"Anime - English-translated", "1_2"},
    {"Anime - Non-English",        "1_3"},
    {"Anime - Raw",                "1_4"},
    {"Audio - Lossless",           "2_1"},
    {"Audio - Lossy",              "2_2"},
    {"Literature - English",       "3_1"},
    {"Literature - Non-English",   "3_2"},
    {"Literature - Raw",           "3_3"},
    {"Live Action - English",      "4_1"},
    {"Live Action - Idol/PV",      "4_2"},
    {"Live Action - Non-English",  "4_3"},
    {"Live Action - Raw",          "4_4"},
    {"Pictures - Graphics",        "5_1"},
    {"Pictures - Photos",          "5_2"},
    {"Software - Applications",    "6_1"},
    {"Software - Games",           "6_2"},
    {nullptr, nullptr}
};

static const CategoryOption PIRATEBAY_CATEGORIES[] = {
    {"All categories", ""},
    {"Audio",          "100"},
    {"Video",          "200"},
    {"Applications",   "300"},
    {"Games",          "400"},
    {"Porn",           "500"},
    {"Other",          "600"},
    {nullptr, nullptr}
};

static const CategoryOption EXTTORRENTS_CATEGORIES[] = {
    {"All categories", ""},
    {"Movies",         "Movies"},
    {"TV",             "TV"},
    {"Games",          "Games"},
    {"Music",          "Music"},
    {"Apps",           "Apps"},
    {"Documentaries",  "Documentaries"},
    {"Anime",          "Anime"},
    {"Books",          "Books"},
    {"Other",          "Other"},
    {"XXX",            "XXX"},
    {nullptr, nullptr}
};

static const CategoryOption YTS_CATEGORIES[] = {
    {"All genres",   ""},
    {"Action",       "action"},
    {"Adventure",    "adventure"},
    {"Animation",    "animation"},
    {"Biography",    "biography"},
    {"Comedy",       "comedy"},
    {"Crime",        "crime"},
    {"Documentary",  "documentary"},
    {"Drama",        "drama"},
    {"Family",       "family"},
    {"Fantasy",      "fantasy"},
    {"Film-Noir",    "film-noir"},
    {"History",      "history"},
    {"Horror",       "horror"},
    {"Music",        "music"},
    {"Musical",      "musical"},
    {"Mystery",      "mystery"},
    {"Romance",      "romance"},
    {"Sci-Fi",       "sci-fi"},
    {"Sport",        "sport"},
    {"Thriller",     "thriller"},
    {"War",          "war"},
    {"Western",      "western"},
    {nullptr, nullptr}
};

static const CategoryOption EZTV_CATEGORIES[] = {
    {"All TV", ""},
    {nullptr, nullptr}
};

static const CategoryOption TORRENTSCSV_CATEGORIES[] = {
    {"All categories", ""},
    {nullptr, nullptr}
};

// 1337x has 80+ categories — abbreviated to major groups
static const CategoryOption X1337X_CATEGORIES[] = {
    {"All categories",      ""},
    {"Movies",              "Movies"},
    {"Movies/HD",           "42"},
    {"Movies/Cam/TS",       "43"},
    {"Movies/DVD",          "66"},
    {"Movies/Bollywood",    "68"},
    {"TV",                  "TV"},
    {"TV/HD",               "41"},
    {"TV/SD",               "71"},
    {"TV/Foreign",          "76"},
    {"Anime",               "28"},
    {"Games",               "Games"},
    {"Games/PC",            "10"},
    {"Games/PS",            "43"},
    {"Games/Xbox",          "44"},
    {"Games/Nintendo",      "45"},
    {"Music",               "Music"},
    {"Music/MP3",           "22"},
    {"Music/Lossless",      "23"},
    {"Music/Video",         "47"},
    {"Apps",                "Apps"},
    {"Apps/PC",             "18"},
    {"Apps/Mac",            "19"},
    {"Apps/Mobile",         "21"},
    {"Other",               "Other"},
    {"Books",               "36"},
    {"Comics",              "39"},
    {"Audiobooks",          "52"},
    {"XXX",                 "48"},
    {nullptr, nullptr}
};

static const CategoryOption* categoryOptionsForSite(const QString& siteKey)
{
    if (siteKey == "nyaa")         return NYAA_CATEGORIES;
    if (siteKey == "piratebay")    return PIRATEBAY_CATEGORIES;
    if (siteKey == "exttorrents")  return EXTTORRENTS_CATEGORIES;
    if (siteKey == "yts")          return YTS_CATEGORIES;
    if (siteKey == "eztv")         return EZTV_CATEGORIES;
    if (siteKey == "1337x")        return X1337X_CATEGORIES;
    if (siteKey == "torrentscsv")  return TORRENTSCSV_CATEGORIES;
    return nullptr;
}

// ══════════════════════════════════════════════════════════════════════════════
// Constructor
// ══════════════════════════════════════════════════════════════════════════════

TankorentPage::TankorentPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent), m_bridge(bridge)
{
    qRegisterMetaType<TorrentResult>();
    qRegisterMetaType<QList<TorrentResult>>();

    m_nam = new QNetworkAccessManager(this);
    buildUI();
    populateSourceCombo();
}

// ══════════════════════════════════════════════════════════════════════════════
// UI
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 0);
    root->setSpacing(6);

    buildSearchControls(root);
    buildStatusRow(root);
    buildMainTabs(root);
}

void TankorentPage::buildSearchControls(QVBoxLayout* parent)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    m_queryEdit = new QLineEdit;
    m_queryEdit->setPlaceholderText("Search torrents...");
    m_queryEdit->setFixedHeight(30);
    connect(m_queryEdit, &QLineEdit::returnPressed, this, &TankorentPage::startSearch);
    row->addWidget(m_queryEdit, 3);

    m_searchTypeCombo = new QComboBox;
    m_searchTypeCombo->setFixedHeight(30);
    m_searchTypeCombo->setMinimumWidth(150);
    m_searchTypeCombo->addItem("Videos",     "videos");
    m_searchTypeCombo->addItem("Books",      "books");
    m_searchTypeCombo->addItem("Audiobooks", "audiobooks");
    m_searchTypeCombo->addItem("Comics",     "comics");
    row->addWidget(m_searchTypeCombo, 1);

    m_sourceCombo = new QComboBox;
    m_sourceCombo->setFixedHeight(30);
    m_sourceCombo->setMinimumWidth(160);
    connect(m_sourceCombo, &QComboBox::currentIndexChanged, this, [this]() {
        reloadCategoryOptions();
    });
    row->addWidget(m_sourceCombo, 1);

    m_categoryCombo = new QComboBox;
    m_categoryCombo->setFixedHeight(30);
    m_categoryCombo->setMinimumWidth(220);
    m_categoryCombo->addItem("All categories", "");
    m_categoryCombo->setEnabled(false);
    row->addWidget(m_categoryCombo, 1);

    m_sortCombo = new QComboBox;
    m_sortCombo->setFixedHeight(30);
    m_sortCombo->setMinimumWidth(160);
    m_sortCombo->addItem("Sort: Relevance", "relevance");
    m_sortCombo->addItem("Sort: Seeders",   "seeders_desc");
    m_sortCombo->addItem("Sort: Size",      "size_desc");
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this]() {
        if (!m_allResults.isEmpty()) renderResults();
    });
    row->addWidget(m_sortCombo, 1);

    m_searchBtn = new QPushButton("Search");
    m_searchBtn->setFixedHeight(30);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, &TankorentPage::startSearch);
    row->addWidget(m_searchBtn);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedHeight(30);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TankorentPage::cancelSearch);
    row->addWidget(m_cancelBtn);

    m_refreshBtn = new QPushButton("Refresh");
    m_refreshBtn->setFixedHeight(30);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    row->addWidget(m_refreshBtn);

    parent->addLayout(row);
}

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

void TankorentPage::buildMainTabs(QVBoxLayout* parent)
{
    m_tabWidget = new QTabWidget;

    m_resultsTable = createResultsTable();
    m_tabWidget->addTab(m_resultsTable, "Search Results");

    m_transfersTable = createTransfersTable();
    m_tabWidget->addTab(m_transfersTable, "Transfers");

    parent->addWidget(m_tabWidget, 1);
}

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
    connect(table, &QTableWidget::customContextMenuRequested, this, &TankorentPage::showResultsContextMenu);

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

// ══════════════════════════════════════════════════════════════════════════════
// Source combo + per-site category system
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::populateSourceCombo()
{
    m_sourceCombo->blockSignals(true);
    m_sourceCombo->clear();
    m_sourceCombo->addItem("All Sources (7)", "all");
    m_sourceCombo->addItem("Nyaa",            "nyaa");
    m_sourceCombo->addItem("PirateBay",       "piratebay");
    m_sourceCombo->addItem("1337x (disabled)","1337x");
    m_sourceCombo->addItem("YTS",             "yts");
    m_sourceCombo->addItem("EZTV",            "eztv");
    m_sourceCombo->addItem("ExtraTorrents",   "exttorrents");
    m_sourceCombo->addItem("Torrents-CSV",    "torrentscsv");
    m_sourceCombo->blockSignals(false);
    reloadCategoryOptions();
}

void TankorentPage::reloadCategoryOptions()
{
    QString prevValue = m_categoryCombo->currentData().toString();
    QString siteKey = m_sourceCombo->currentData().toString();

    m_categoryCombo->blockSignals(true);
    m_categoryCombo->clear();

    if (siteKey == "all" || siteKey.isEmpty()) {
        m_categoryCombo->addItem("All categories", "");
        m_categoryCombo->setEnabled(false);
    } else {
        const CategoryOption* opts = categoryOptionsForSite(siteKey);
        if (opts) {
            for (int i = 0; opts[i].label != nullptr; ++i)
                m_categoryCombo->addItem(opts[i].label, opts[i].value);
            m_categoryCombo->setEnabled(true);
        } else {
            m_categoryCombo->addItem("All categories", "");
            m_categoryCombo->setEnabled(false);
        }

        // Try to restore previous selection
        int idx = m_categoryCombo->findData(prevValue);
        if (idx >= 0)
            m_categoryCombo->setCurrentIndex(idx);
    }
    m_categoryCombo->blockSignals(false);
}

// ══════════════════════════════════════════════════════════════════════════════
// Search logic
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::startSearch()
{
    QString query = m_queryEdit->text().trimmed();
    if (query.isEmpty())
        return;

    cancelSearch();

    m_allResults.clear();
    m_resultsTable->setRowCount(0);
    m_pendingSearches = 0;

    QString sourceId = m_sourceCombo->currentData().toString();
    QString categoryId = m_categoryCombo->currentData().toString();

    QList<TorrentIndexer*> indexers;

    auto addIf = [&](const QString& id, TorrentIndexer* indexer) {
        if (sourceId == "all" || sourceId == id)
            indexers.append(indexer);
        else
            delete indexer;
    };

    addIf("nyaa",         new NyaaIndexer(m_nam, this));
    addIf("piratebay",    new PirateBayIndexer(m_nam, this));
    // 1337x disabled — Cloudflare JS challenge
    addIf("yts",          new YtsIndexer(m_nam, this));
    addIf("eztv",         new EztvIndexer(m_nam, this));
    addIf("exttorrents",  new ExtTorrentsIndexer(m_nam, this));
    addIf("torrentscsv",  new TorrentsCsvIndexer(m_nam, this));

    if (indexers.isEmpty())
        return;

    m_activeIndexers = indexers;
    m_pendingSearches = indexers.size();

    m_searchBtn->setVisible(false);
    m_cancelBtn->setVisible(true);
    m_searchStatus->setText("Searching...");

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
    // Partial results: append and render immediately
    m_allResults.append(results);
    --m_pendingSearches;

    // Render what we have so far
    renderResults();
    m_searchStatus->setText(
        m_pendingSearches > 0
            ? QStringLiteral("Searching... %1 results").arg(m_allResults.size())
            : QStringLiteral("Done: %1 results").arg(m_allResults.size()));

    if (m_pendingSearches <= 0) {
        for (auto *idx : m_activeIndexers)
            idx->deleteLater();
        m_activeIndexers.clear();
        m_searchBtn->setVisible(true);
        m_cancelBtn->setVisible(false);
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

        if (m_allResults.isEmpty())
            m_searchStatus->setText("Search failed: " + error);
        else {
            renderResults();
            m_searchStatus->setText(QStringLiteral("%1 results (some sources failed)").arg(m_allResults.size()));
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// Result rendering with quality tags + health indicators
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::renderResults()
{
    // Apply sort
    auto sorted = m_allResults;
    QString sortMode = m_sortCombo->currentData().toString();
    if (sortMode == "seeders_desc") {
        std::sort(sorted.begin(), sorted.end(),
                  [](const TorrentResult& a, const TorrentResult& b) { return a.seeders > b.seeders; });
    } else if (sortMode == "size_desc") {
        std::sort(sorted.begin(), sorted.end(),
                  [](const TorrentResult& a, const TorrentResult& b) { return a.sizeBytes > b.sizeBytes; });
    }

    // Dedup by infohash
    QSet<QString> seen;
    QList<TorrentResult> deduped;
    static const QRegularExpression btihRe("btih:([a-fA-F0-9]{40})", QRegularExpression::CaseInsensitiveOption);
    for (const auto& r : sorted) {
        auto m = btihRe.match(r.magnetUri);
        QString key = m.hasMatch() ? m.captured(1).toLower() : r.magnetUri.toLower();
        if (seen.contains(key)) continue;
        seen.insert(key);
        deduped.append(r);
    }

    m_resultsTable->setRowCount(deduped.size());

    for (int i = 0; i < deduped.size(); ++i) {
        const auto& r = deduped[i];

        // Title with quality tags
        QString tags = qualityTagSuffix(r.title);
        QString displayTitle = tags.isEmpty() ? r.title : r.title + "  " + tags;
        m_resultsTable->setItem(i, 0, new QTableWidgetItem(displayTitle));

        // Category
        m_resultsTable->setItem(i, 1, new QTableWidgetItem(
            r.category.isEmpty() ? r.categoryId : r.category));

        // Source
        m_resultsTable->setItem(i, 2, new QTableWidgetItem(r.sourceName));

        // Size
        m_resultsTable->setItem(i, 3, new QTableWidgetItem(humanSize(r.sizeBytes)));

        // Files
        auto *filesItem = new QTableWidgetItem("-");
        filesItem->setTextAlignment(Qt::AlignCenter);
        m_resultsTable->setItem(i, 4, filesItem);

        // Seeders with health dot
        auto *seedItem = new QTableWidgetItem(healthDot(r.seeders) + " " + QString::number(r.seeders));
        seedItem->setTextAlignment(Qt::AlignCenter);
        seedItem->setForeground(healthColor(r.seeders));
        m_resultsTable->setItem(i, 5, seedItem);

        // Leechers
        auto *leechItem = new QTableWidgetItem(QString::number(r.leechers));
        leechItem->setTextAlignment(Qt::AlignCenter);
        m_resultsTable->setItem(i, 6, leechItem);

        // Action button
        auto *actionItem = new QTableWidgetItem("+");
        actionItem->setTextAlignment(Qt::AlignCenter);
        m_resultsTable->setItem(i, 7, actionItem);
    }

    m_tabWidget->setCurrentIndex(0);
}

// ══════════════════════════════════════════════════════════════════════════════
// Quality tag parsing (ported from sources_ui_helpers.py)
// ══════════════════════════════════════════════════════════════════════════════

QString TankorentPage::qualityTagSuffix(const QString& title)
{
    QStringList tags;

    // Resolution
    static const QRegularExpression resRe("\\b(2160p|4K|UHD|1080p|720p|480p|576p|360p)\\b",
        QRegularExpression::CaseInsensitiveOption);
    auto rm = resRe.match(title);
    if (rm.hasMatch()) {
        QString r = rm.captured(1).toLower();
        if (r == "4k" || r == "uhd" || r == "2160p") tags << "[4K]";
        else tags << "[" + rm.captured(1) + "]";
    }

    // Codec
    static const QRegularExpression codecRe("\\b(x265|HEVC|H\\.?264|x264|AV1|VP9)\\b",
        QRegularExpression::CaseInsensitiveOption);
    auto cm = codecRe.match(title);
    if (cm.hasMatch()) {
        QString c = cm.captured(1).toLower();
        if (c == "x265" || c == "hevc") tags << "[HEVC]";
        else if (c.startsWith("h") || c == "x264") tags << "[H.264]";
        else tags << "[" + cm.captured(1) + "]";
    }

    // Source
    static const QRegularExpression srcRe("\\b(Blu-?Ray|BDRip|BDMV|WEB-DL|WEBRip|HDTV|DVDRip|REMUX|CAMRip)\\b",
        QRegularExpression::CaseInsensitiveOption);
    auto sm = srcRe.match(title);
    if (sm.hasMatch()) {
        QString s = sm.captured(1).toLower();
        if (s.startsWith("blu")) tags << "[BluRay]";
        else tags << "[" + sm.captured(1) + "]";
    }

    return tags.join("  ");
}

// ══════════════════════════════════════════════════════════════════════════════
// Health indicators (ported from source_health())
// ══════════════════════════════════════════════════════════════════════════════

QString TankorentPage::healthDot(int seeders)
{
    // Filled circle for healthy/weak, empty circle for dead
    return seeders >= 1 ? QStringLiteral("\u25CF") : QStringLiteral("\u25CB");
}

QColor TankorentPage::healthColor(int seeders)
{
    if (seeders >= 10) return QColor(34, 197, 94);    // green — healthy
    if (seeders >= 1)  return QColor(234, 179, 8);    // yellow — weak
    return QColor(220, 50, 50);                        // red — dead
}

// ══════════════════════════════════════════════════════════════════════════════
// Context menu
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::showResultsContextMenu(const QPoint& pos)
{
    int row = m_resultsTable->rowAt(pos.y());
    if (row < 0) return;

    // Find the matching result
    if (row >= m_allResults.size()) return;

    // Re-derive the deduped list to get the correct result for this row
    QSet<QString> seen;
    QList<TorrentResult> deduped;
    static const QRegularExpression btihRe("btih:([a-fA-F0-9]{40})", QRegularExpression::CaseInsensitiveOption);
    for (const auto& r : m_allResults) {
        auto m = btihRe.match(r.magnetUri);
        QString key = m.hasMatch() ? m.captured(1).toLower() : r.magnetUri.toLower();
        if (seen.contains(key)) continue;
        seen.insert(key);
        deduped.append(r);
    }

    if (row >= deduped.size()) return;
    const auto& result = deduped[row];

    QMenu menu(this);
    auto *copyMagnet = menu.addAction("Copy Magnet URI");
    copyMagnet->setEnabled(!result.magnetUri.isEmpty());

    auto *copyTitle = menu.addAction("Copy Title");
    copyTitle->setEnabled(!result.title.isEmpty());

    auto *chosen = menu.exec(m_resultsTable->viewport()->mapToGlobal(pos));
    if (chosen == copyMagnet)
        QApplication::clipboard()->setText(result.magnetUri);
    else if (chosen == copyTitle)
        QApplication::clipboard()->setText(result.title);
}
