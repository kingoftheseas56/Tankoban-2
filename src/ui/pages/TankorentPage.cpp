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

#include "core/torrent/TorrentClient.h"
#include "core/torrent/TorrentEngine.h"
#include "ui/dialogs/AddTorrentDialog.h"
#include "ui/dialogs/SpeedLimitDialog.h"
#include "ui/dialogs/SeedingRulesDialog.h"
#include "ui/dialogs/QueueLimitsDialog.h"
#include "ui/dialogs/TorrentFilesDialog.h"
#include "ui/dialogs/HistoryDialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPalette>
#include <QStyleFactory>
#include <QNetworkAccessManager>
#include <QApplication>
#include <QClipboard>
#include <QRegularExpression>
#include <QTimer>
#include <QIcon>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QProgressBar>
#include <algorithm>

#include "ui/ContextMenuHelper.h"

// ══════════════════════════════════════════════════════════════════════════════
// Progress bar delegate — custom painting for the Progress column
// ══════════════════════════════════════════════════════════════════════════════

void ProgressBarDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    // Draw selection highlight first
    if (option.state & QStyle::State_Selected)
        painter->fillRect(option.rect, option.palette.highlight());

    float progress = index.data(Qt::UserRole).toFloat();
    QRect cell = option.rect.adjusted(6, 0, -6, 0);

    // Track: 6px tall, centered vertically, rounded
    int barH = 6, barR = 3;
    int barY = cell.top() + (cell.height() - barH) / 2;
    QRect track(cell.left(), barY, cell.width(), barH);

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);

    // Track background
    painter->setBrush(QColor(0x33, 0x33, 0x33));
    painter->drawRoundedRect(track, barR, barR);

    // Fill
    if (progress > 0.f) {
        int fillW = qMax(barH, static_cast<int>(track.width() * qMin(progress, 1.f)));
        QRect fill(track.left(), track.top(), fillW, barH);
        painter->setBrush(QColor(0xcc, 0xcc, 0xcc));
        painter->drawRoundedRect(fill, barR, barR);
    }

    // Percentage text centered
    painter->setPen(QColor(0xee, 0xee, 0xee));
    QFont f = option.font;
    f.setPixelSize(10);
    painter->setFont(f);
    QString text = QString::number(progress * 100, 'f', 1) + "%";
    painter->drawText(cell, Qt::AlignCenter, text);
}

QSize ProgressBarDelegate::sizeHint(const QStyleOptionViewItem& /*option*/,
                                    const QModelIndex& /*index*/) const
{
    return QSize(110, 26);
}

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

TankorentPage::TankorentPage(CoreBridge* bridge, TorrentClient* client, QWidget* parent)
    : QWidget(parent), m_bridge(bridge), m_client(client)
{
    qRegisterMetaType<TorrentResult>();
    qRegisterMetaType<QList<TorrentResult>>();

    m_nam = new QNetworkAccessManager(this);
    buildUI();
    populateSourceCombo();

    // Wire "+" button clicks and double-clicks on results table
    connect(m_resultsTable, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (col == 7) onAddTorrentClicked(row);  // "+" column
    });
    connect(m_resultsTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int /*col*/) {
        onAddTorrentClicked(row);
    });

    // Transfers table context menu
    connect(m_transfersTable, &QTableWidget::customContextMenuRequested,
            this, &TankorentPage::showTransfersContextMenu);

    // Double-click or Info column click opens TorrentFilesDialog
    auto openFilesFor = [this](int row) {
        auto* item = m_transfersTable->item(row, 0);
        if (!item) return;
        QString hash = item->data(Qt::UserRole).toString();
        for (const auto& t : m_cachedActive) {
            if (t.infoHash == hash) {
                TorrentFilesDialog dlg(t.name, hash, m_client, this);
                dlg.exec();
                break;
            }
        }
    };
    connect(m_transfersTable, &QTableWidget::cellDoubleClicked, this, [openFilesFor](int row, int) {
        openFilesFor(row);
    });
    connect(m_transfersTable, &QTableWidget::cellClicked, this, [openFilesFor](int row, int col) {
        if (col == 11) openFilesFor(row);  // Info column
    });

    // Auto-refresh transfers every 1 second
    m_transferTimer = new QTimer(this);
    connect(m_transferTimer, &QTimer::timeout, this, &TankorentPage::refreshTransfers);
    m_transferTimer->start(1000);
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
    connect(m_refreshBtn, &QPushButton::clicked, this, &TankorentPage::refreshTransfers);
    row->addWidget(m_refreshBtn);

    m_moreBtn = new QPushButton("More");
    m_moreBtn->setFixedHeight(30);
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    connect(m_moreBtn, &QPushButton::clicked, this, [this]() {
        QMenu* menu = ContextMenuHelper::createMenu(this);
        menu->addAction("Global Speed Limits...", this, [this]() {
            SpeedLimitDialog dlg("Speed Limits (Global)", 0, 0, this);
            if (dlg.exec() == QDialog::Accepted)
                m_client->setGlobalSpeedLimits(dlg.dlLimitBps(), dlg.ulLimitBps());
        });
        menu->addAction("Global Seeding Rules...", this, [this]() {
            SeedingRulesDialog dlg("Seeding Rules (Global)", 0.f, 0, this);
            if (dlg.exec() == QDialog::Accepted)
                m_client->setGlobalSeedingRules(dlg.ratioLimit(), dlg.seedTimeSecs());
        });
        menu->addAction("Queue Limits...", this, [this]() {
            QueueLimitsDialog dlg(this);
            if (dlg.exec() == QDialog::Accepted)
                m_client->setQueueLimits(dlg.maxDownloads(), dlg.maxUploads(), dlg.maxActive());
        });
        menu->addSeparator();
        menu->addAction("Pause All", this, [this]() {
            for (const auto& t : m_cachedActive) m_client->pauseTorrent(t.infoHash);
        });
        menu->addAction("Resume All", this, [this]() {
            for (const auto& t : m_cachedActive) m_client->resumeTorrent(t.infoHash);
        });
        menu->addSeparator();
        menu->addAction("View History...", this, [this]() {
            HistoryDialog dlg(m_client, this);
            dlg.exec();
        });
        menu->exec(m_moreBtn->mapToGlobal(QPoint(0, m_moreBtn->height())));
        delete menu;
    });
    row->addWidget(m_moreBtn);

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

    table->setStyle(QStyleFactory::create("Fusion"));
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setFocusPolicy(Qt::NoFocus);

    QPalette palR = table->palette();
    palR.setColor(QPalette::Base,            QColor(0x11, 0x11, 0x11));
    palR.setColor(QPalette::AlternateBase,   QColor(0x18, 0x18, 0x18));
    palR.setColor(QPalette::Text,            QColor(0xee, 0xee, 0xee));
    palR.setColor(QPalette::Highlight,       QColor(192, 200, 212, 36));
    palR.setColor(QPalette::HighlightedText, QColor(0xee, 0xee, 0xee));
    table->setPalette(palR);

    table->setStyleSheet(QStringLiteral(
        "#SearchResultsTable { border: none; outline: none; font-size: 12px; }"
        "#SearchResultsTable::item { padding: 0 8px; }"
        "#SearchResultsTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
        "#SearchResultsTable QHeaderView::section {"
        "  background: #1a1a1a; color: #888; border: none;"
        "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
        "  padding: 4px 8px; font-size: 11px; }"
    ));

    return table;
}

QTableWidget* TankorentPage::createTransfersTable()
{
    auto *table = new QTableWidget(0, 12);
    table->setObjectName("TransfersTable");

    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(26);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    table->setSortingEnabled(true);

    QStringList headers = { "Name", "Size", "Progress", "Status", "Seeds", "Peers",
                            "Down Speed", "Up Speed", "ETA", "Category", "Queue", "Info" };
    table->setHorizontalHeaderLabels(headers);

    auto *hdr = table->horizontalHeader();
    hdr->setMinimumSectionSize(40);
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 12; ++i)
        hdr->setSectionResizeMode(i, QHeaderView::Interactive);

    hdr->resizeSection(1, 100);   // Size
    hdr->resizeSection(2, 110);   // Progress
    hdr->resizeSection(3, 120);   // Status
    hdr->resizeSection(4, 70);    // Seeds
    hdr->resizeSection(5, 70);    // Peers
    hdr->resizeSection(6, 110);   // Down Speed
    hdr->resizeSection(7, 110);   // Up Speed
    hdr->resizeSection(8, 80);    // ETA
    hdr->resizeSection(9, 100);   // Category
    hdr->resizeSection(10, 60);   // Queue
    hdr->resizeSection(11, 40);   // Info

    // Progress bar delegate for column 2
    table->setItemDelegateForColumn(2, new ProgressBarDelegate(table));

    table->setStyle(QStyleFactory::create("Fusion"));
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setFocusPolicy(Qt::NoFocus);

    QPalette palT = table->palette();
    palT.setColor(QPalette::Base,            QColor(0x11, 0x11, 0x11));
    palT.setColor(QPalette::AlternateBase,   QColor(0x18, 0x18, 0x18));
    palT.setColor(QPalette::Text,            QColor(0xee, 0xee, 0xee));
    palT.setColor(QPalette::Highlight,       QColor(192, 200, 212, 36));
    palT.setColor(QPalette::HighlightedText, QColor(0xee, 0xee, 0xee));
    table->setPalette(palT);

    table->setStyleSheet(QStringLiteral(
        "#TransfersTable { border: none; outline: none; font-size: 12px; }"
        "#TransfersTable::item { padding: 0 8px; }"
        "#TransfersTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
        "#TransfersTable QHeaderView::section {"
        "  background: #1a1a1a; color: #888; border: none;"
        "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
        "  padding: 4px 8px; font-size: 11px; }"
    ));

    return table;
}

// ══════════════════════════════════════════════════════════════════════════════
// Source combo + per-site category system
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::populateSourceCombo()
{
    m_sourceCombo->blockSignals(true);
    m_sourceCombo->clear();
    m_sourceCombo->addItem("All Sources",     "all");
    m_sourceCombo->addItem("Nyaa",            "nyaa");
    m_sourceCombo->addItem("PirateBay",       "piratebay");
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
            ? QStringLiteral("Searching... %1 Results").arg(m_allResults.size())
            : QStringLiteral("Done: %1 Results").arg(m_allResults.size()));

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
            m_searchStatus->setText("Search Failed: " + error);
        else {
            renderResults();
            m_searchStatus->setText(QStringLiteral("%1 Results (Some Sources Failed)").arg(m_allResults.size()));
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
    static const QRegularExpression btihRe(
        "btih:([a-fA-F0-9]{40}(?:[a-fA-F0-9]{24})?|[A-Z2-7]{32})",
        QRegularExpression::CaseInsensitiveOption);
    for (const auto& r : sorted) {
        auto m = btihRe.match(r.magnetUri);
        QString key = m.hasMatch() ? m.captured(1).toLower() : r.magnetUri.toLower();
        if (seen.contains(key)) continue;
        seen.insert(key);
        deduped.append(r);
    }

    m_displayedResults = deduped;  // row N in table == m_displayedResults[N] — single source of truth

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

        // Seeders with health dot (monochrome)
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
// Speed formatting — 2 decimal places, "X.XX MB/s"
// ══════════════════════════════════════════════════════════════════════════════

QString TankorentPage::humanSpeed(int bytesPerSec)
{
    if (bytesPerSec <= 0) return QString();
    const double kb = 1024.0, mb = kb * 1024, gb = mb * 1024;
    double v = bytesPerSec;
    if (v >= gb) return QString::number(v / gb, 'f', 2) + " GB/s";
    if (v >= mb) return QString::number(v / mb, 'f', 2) + " MB/s";
    if (v >= kb) return QString::number(v / kb, 'f', 1) + " KB/s";
    return QString::number(bytesPerSec) + " B/s";
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
    if (seeders >= 10) return QColor(0xee, 0xee, 0xee);  // bright — healthy
    if (seeders >= 1)  return QColor(0x88, 0x88, 0x88);  // mid gray — weak
    return QColor(0x44, 0x44, 0x44);                       // dark gray — dead
}

// ══════════════════════════════════════════════════════════════════════════════
// Context menu
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::showResultsContextMenu(const QPoint& pos)
{
    int row = m_resultsTable->rowAt(pos.y());
    if (row < 0 || row >= m_displayedResults.size()) return;

    const auto& result = m_displayedResults[row];

    QMenu* menu = ContextMenuHelper::createMenu(this);

    menu->addAction("Download...", this, [this, row]() {
        onAddTorrentClicked(row);
    });

    menu->addSeparator();

    auto *copyMagnet = menu->addAction("Copy Magnet URI", this, [&result]() {
        ContextMenuHelper::copyToClipboard(result.magnetUri);
    });
    copyMagnet->setEnabled(!result.magnetUri.isEmpty());

    auto *copyTitle = menu->addAction("Copy Title", this, [&result]() {
        ContextMenuHelper::copyToClipboard(result.title);
    });
    copyTitle->setEnabled(!result.title.isEmpty());

    menu->exec(m_resultsTable->viewport()->mapToGlobal(pos));
    delete menu;
}

// ══════════════════════════════════════════════════════════════════════════════
// Add Torrent flow — "+" button or double-click
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::onAddTorrentClicked(int row)
{
    if (!m_client || row < 0 || row >= m_displayedResults.size()) return;

    const auto& result = m_displayedResults[row];
    if (result.magnetUri.isEmpty()) return;

    // Dedup check
    if (m_client->isDuplicate(result.magnetUri)) {
        m_searchStatus->setText("Torrent Already Added");
        return;
    }

    // Open the Add Torrent dialog
    auto defaultPaths = m_client->defaultPaths();
    AddTorrentDialog dlg(result.title, QString(), defaultPaths, this);

    // Start metadata resolution
    QString hash = m_client->resolveMetadata(result.magnetUri);
    if (hash.isEmpty()) {
        m_searchStatus->setText("Failed to Add Magnet");
        return;
    }

    // Connect engine's metadataReady to populate the dialog
    auto conn = connect(m_client->engine(), &TorrentEngine::metadataReady,
        &dlg, [&dlg, hash](const QString& h, const QString& name, qint64 size, const QJsonArray& files) {
            if (h == hash)
                dlg.populateFiles(name, size, files);
        });

    // Timeout after 30 seconds
    QTimer::singleShot(30000, &dlg, [&dlg, hash]() {
        if (!dlg.isVisible()) return;
        dlg.showMetadataError("Metadata resolution timed out — no peers found");
    });

    if (dlg.exec() == QDialog::Accepted) {
        auto config = dlg.config();
        m_client->startDownload(hash, config);
        m_tabWidget->setCurrentIndex(1); // Switch to Transfers tab
        m_searchStatus->setText("Download Started");
    } else {
        // User cancelled — clean up the draft torrent
        m_client->deleteTorrent(hash, false);
    }

    disconnect(conn);
}

// ══════════════════════════════════════════════════════════════════════════════
// Transfers tab — auto-refresh
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::refreshTransfers()
{
    if (!m_client) return;

    m_cachedActive = m_client->listActive();
    const auto& active = m_cachedActive;

    // Preserve selection (multi-select)
    QSet<QString> selectedHashes;
    for (auto* item : m_transfersTable->selectedItems()) {
        if (item->column() == 0)
            selectedHashes.insert(item->data(Qt::UserRole).toString());
    }

    // Save sort state, disable during population
    if (m_sortCol < 0 && m_transfersTable->horizontalHeader()->sortIndicatorSection() >= 0) {
        m_sortCol = m_transfersTable->horizontalHeader()->sortIndicatorSection();
        m_sortOrder = m_transfersTable->horizontalHeader()->sortIndicatorOrder();
    }
    m_transfersTable->setSortingEnabled(false);
    m_transfersTable->setRowCount(active.size());

    int totalDl = 0, totalUl = 0;
    int activeCount = 0, seedingCount = 0;

    auto ensureItem = [this](int row, int col) -> QTableWidgetItem* {
        auto* item = m_transfersTable->item(row, col);
        if (!item) { item = new QTableWidgetItem; m_transfersTable->setItem(row, col, item); }
        return item;
    };

    for (int i = 0; i < active.size(); ++i) {
        const auto& t = active[i];

        // Col 0: Name — store infoHash in UserRole
        auto* nameItem = ensureItem(i, 0);
        QString displayName = t.name.isEmpty() ? t.infoHash.left(8) + "..." : t.name;
        if (t.forceStarted) displayName = "[F] " + displayName;
        nameItem->setText(displayName);
        nameItem->setData(Qt::UserRole, t.infoHash);

        // Col 1: Size
        auto* sizeItem = ensureItem(i, 1);
        sizeItem->setText(t.totalWanted > 0 ? humanSize(t.totalWanted) : "-");
        sizeItem->setData(Qt::UserRole, static_cast<qlonglong>(t.totalWanted));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // Col 2: Progress — UserRole for delegate + sorting
        auto* progItem = ensureItem(i, 2);
        progItem->setData(Qt::UserRole, t.progress);
        progItem->setText(QString::number(t.progress * 100, 'f', 1) + "%");
        progItem->setTextAlignment(Qt::AlignCenter);

        // Col 3: Status
        auto* stateItem = ensureItem(i, 3);
        QString stateIcon;
        QString stateText;
        if (t.stateString == "downloading")       { stateIcon = ":/icons/download.svg"; stateText = "Downloading"; }
        else if (t.stateString == "paused")       { stateIcon = ":/icons/pause.svg";    stateText = "Paused"; }
        else if (t.stateString == "seeding")      { stateIcon = ":/icons/seed.svg";     stateText = "Seeding"; }
        else if (t.stateString == "error")        { stateIcon = ":/icons/error.svg";    stateText = "Error"; }
        else if (t.stateString == "metadata")     { stateIcon = ":/icons/waiting.svg";  stateText = "Resolving"; }
        else if (t.stateString == "completed")    { stateIcon = ":/icons/check.svg";    stateText = "Completed"; }
        else if (t.stateString == "checking")     { stateIcon = ":/icons/waiting.svg";  stateText = "Checking"; }
        else                                      { stateIcon = ":/icons/stalled.svg";  stateText = "Stalled"; }
        if (!stateIcon.isEmpty())
            stateItem->setIcon(QIcon(stateIcon));
        stateItem->setToolTip(QString());
        if (t.stateString == "error" && !t.errorMessage.isEmpty()) {
            stateItem->setToolTip(t.errorMessage);
            stateText = "Error: " + t.errorMessage.left(40);
        }
        stateItem->setText(stateText);

        // Col 4: Seeds
        auto* seedItem = ensureItem(i, 4);
        seedItem->setText(QString::number(t.seeds));
        seedItem->setData(Qt::UserRole, t.seeds);
        seedItem->setTextAlignment(Qt::AlignCenter);

        // Col 5: Peers
        auto* peerItem = ensureItem(i, 5);
        peerItem->setText(QString::number(t.peers));
        peerItem->setData(Qt::UserRole, t.peers);
        peerItem->setTextAlignment(Qt::AlignCenter);

        // Col 6: Down Speed
        auto* dlItem = ensureItem(i, 6);
        QString dlText = humanSpeed(t.dlSpeed);
        if (t.dlLimit > 0 && !dlText.isEmpty()) {
            dlText += " [L]";
            dlItem->setToolTip("Throttled: " + humanSpeed(t.dlLimit) + " max");
        } else {
            dlItem->setToolTip(QString());
        }
        dlItem->setText(dlText);
        dlItem->setData(Qt::UserRole, t.dlSpeed);
        dlItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // Col 7: Up Speed
        auto* ulItem = ensureItem(i, 7);
        ulItem->setText(humanSpeed(t.ulSpeed));
        ulItem->setData(Qt::UserRole, t.ulSpeed);
        ulItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // Col 8: ETA
        auto* etaItem = ensureItem(i, 8);
        int etaSecs = INT_MAX;
        if (t.dlSpeed > 0 && t.totalWanted > t.totalDone) {
            etaSecs = static_cast<int>((t.totalWanted - t.totalDone) / t.dlSpeed);
            int h = etaSecs / 3600, m = (etaSecs % 3600) / 60;
            etaItem->setText(h > 0 ? QString("%1h %2m").arg(h).arg(m)
                                   : QString("%1m %2s").arg(m).arg(etaSecs % 60));
        } else {
            etaItem->setText(t.stateString == "seeding" || t.stateString == "completed" ? "-" : "...");
        }
        etaItem->setData(Qt::UserRole, etaSecs);
        etaItem->setTextAlignment(Qt::AlignCenter);

        // Col 9: Category
        auto* catItem = ensureItem(i, 9);
        catItem->setText(t.category.isEmpty() ? "-" : t.category);

        // Col 10: Queue
        auto* queueItem = ensureItem(i, 10);
        queueItem->setText(t.queuePosition >= 0 ? QString::number(t.queuePosition + 1) : "-");
        queueItem->setData(Qt::UserRole, t.queuePosition);
        queueItem->setTextAlignment(Qt::AlignCenter);

        // Col 11: Info
        auto* infoItem = ensureItem(i, 11);
        infoItem->setText(QString());
        infoItem->setIcon(QIcon(":/icons/file.svg"));
        infoItem->setToolTip("View Files");
        infoItem->setTextAlignment(Qt::AlignCenter);

        // Stats
        totalDl += t.dlSpeed;
        totalUl += t.ulSpeed;
        if (t.stateString == "downloading") ++activeCount;
        else if (t.stateString == "seeding") ++seedingCount;
    }

    // Re-enable sorting and restore sort state
    m_transfersTable->setSortingEnabled(true);
    if (m_sortCol >= 0)
        m_transfersTable->sortByColumn(m_sortCol, m_sortOrder);

    // Restore selection
    m_transfersTable->clearSelection();
    for (int i = 0; i < m_transfersTable->rowCount(); ++i) {
        auto* item = m_transfersTable->item(i, 0);
        if (item && selectedHashes.contains(item->data(Qt::UserRole).toString()))
            m_transfersTable->selectRow(i);
    }

    // Update status labels
    int historyCount = m_client->listHistory().size();
    m_downloadStatus->setText(QString("Active: %1 | Seeding: %2 | History: %3")
                                  .arg(activeCount).arg(seedingCount).arg(historyCount));

    // Tab badge
    m_tabWidget->setTabText(1, active.isEmpty() ? "Transfers" : QString("Transfers (%1)").arg(active.size()));
    if (totalDl > 0 || totalUl > 0)
        m_backendStatus->setText(QString("DL %1  UL %2")
                                  .arg(humanSpeed(totalDl), humanSpeed(totalUl)));
    else
        m_backendStatus->setText("");
}

// ══════════════════════════════════════════════════════════════════════════════
// Transfers context menu
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::showTransfersContextMenu(const QPoint& pos)
{
    if (!m_client) return;

    // Collect all selected hashes
    QStringList selectedHashes;
    for (auto* item : m_transfersTable->selectedItems()) {
        if (item->column() == 0) {
            QString h = item->data(Qt::UserRole).toString();
            if (!h.isEmpty()) selectedHashes.append(h);
        }
    }
    if (selectedHashes.isEmpty()) return;

    // Find first selected torrent info
    QString firstHash = selectedHashes.first();
    TorrentInfo firstInfo;
    for (const auto& t : m_cachedActive) {
        if (t.infoHash == firstHash) { firstInfo = t; break; }
    }

    bool anyPaused = false, anyForced = false;
    for (const auto& h : selectedHashes) {
        for (const auto& t : m_cachedActive) {
            if (t.infoHash == h) {
                if (t.stateString == "paused") anyPaused = true;
                if (t.forceStarted) anyForced = true;
                break;
            }
        }
    }

    QMenu* menu = ContextMenuHelper::createMenu(this);

    // Resume / Pause
    if (anyPaused) {
        menu->addAction("Resume", this, [this, selectedHashes]() {
            for (const auto& h : selectedHashes) m_client->resumeTorrent(h);
        });
    } else {
        menu->addAction("Pause", this, [this, selectedHashes]() {
            for (const auto& h : selectedHashes) m_client->pauseTorrent(h);
        });
    }

    // Force Start
    if (anyForced) {
        menu->addAction("Cancel Force Start", this, [this, selectedHashes]() {
            for (const auto& h : selectedHashes) m_client->clearForceStart(h);
        });
    } else {
        menu->addAction("Force Start", this, [this, selectedHashes]() {
            for (const auto& h : selectedHashes) m_client->forceStart(h);
        });
    }

    menu->addSeparator();

    // Queue submenu
    auto* queueMenu = menu->addMenu("Queue");
    queueMenu->addAction("Move Up", this, [this, firstHash]() {
        m_client->queuePositionUp(firstHash);
    });
    queueMenu->addAction("Move Down", this, [this, firstHash]() {
        m_client->queuePositionDown(firstHash);
    });

    // Limits submenu
    auto* limitsMenu = menu->addMenu("Limits");
    limitsMenu->addAction("Speed Limits...", this, [this, firstHash, firstInfo]() {
        int curDl = firstInfo.dlLimit / 1024;
        int curUl = firstInfo.ulLimit / 1024;
        SpeedLimitDialog dlg("Speed Limit: " + firstInfo.name, curDl, curUl, this);
        if (dlg.exec() == QDialog::Accepted)
            m_client->setSpeedLimits(firstHash, dlg.dlLimitBps(), dlg.ulLimitBps());
    });
    limitsMenu->addAction("Seeding Rules...", this, [this, firstHash, firstInfo]() {
        SeedingRulesDialog dlg("Seeding Rules: " + firstInfo.name, 0.f, 0, this);
        if (dlg.exec() == QDialog::Accepted)
            m_client->setSeedingRules(firstHash, dlg.ratioLimit(), dlg.seedTimeSecs());
    });

    // Advanced submenu
    auto* advMenu = menu->addMenu("Advanced");
    advMenu->addAction("Force Recheck", this, [this, firstHash]() {
        m_client->forceRecheck(firstHash);
    });
    advMenu->addAction("Force Reannounce", this, [this, firstHash]() {
        m_client->forceReannounce(firstHash);
    });
    auto* seqAction = advMenu->addAction("Sequential Download");
    seqAction->setCheckable(true);
    seqAction->setChecked(firstInfo.sequential);
    connect(seqAction, &QAction::toggled, this, [this, firstHash](bool on) {
        m_client->engine()->setSequentialDownload(firstHash, on);
    });

    menu->addSeparator();

    // Open Folder
    auto* openFolder = menu->addAction("Open Folder", this, [firstInfo]() {
        if (!firstInfo.savePath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(firstInfo.savePath));
    });
    openFolder->setEnabled(!firstInfo.savePath.isEmpty());

    menu->addAction("View Files...", this, [this, firstHash, firstInfo]() {
        TorrentFilesDialog dlg(firstInfo.name, firstHash, m_client, this);
        dlg.exec();
    });

    menu->addSeparator();

    // Copy submenu
    auto* copyMenu = menu->addMenu("Copy");
    copyMenu->addAction("Copy Name", this, [firstInfo]() {
        ContextMenuHelper::copyToClipboard(firstInfo.name);
    });
    copyMenu->addAction("Copy Info Hash", this, [firstHash]() {
        ContextMenuHelper::copyToClipboard(firstHash);
    });

    menu->addSeparator();

    // Remove (danger actions)
    auto* removeAction = ContextMenuHelper::addDangerAction(menu, "Remove");
    connect(removeAction, &QAction::triggered, this, [this, selectedHashes]() {
        for (const auto& h : selectedHashes) m_client->deleteTorrent(h, false);
    });

    auto* removeWithFiles = ContextMenuHelper::addDangerAction(menu, "Remove + Delete Files");
    connect(removeWithFiles, &QAction::triggered, this, [this, selectedHashes]() {
        if (ContextMenuHelper::confirmRemove(this, "Delete Files",
                QString("Remove %1 torrent(s) and delete all downloaded files?").arg(selectedHashes.size()))) {
            for (const auto& h : selectedHashes) m_client->deleteTorrent(h, true);
        }
    });

    menu->exec(m_transfersTable->viewport()->mapToGlobal(pos));
    delete menu;
}
