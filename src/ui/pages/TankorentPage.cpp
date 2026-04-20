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
#include "ui/dialogs/HistoryDialog.h"
#include "ui/dialogs/AddFromUrlDialog.h"
#include "ui/pages/IndexerStatusPanel.h"
#include "ui/pages/tankorent/TorrentPropertiesWidget.h"
#include "ui/widgets/Toast.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QSettings>
#include <QToolButton>
#include <QClipboard>
#include <QGuiApplication>
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
#include <QHash>
#include <QSet>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QTextEdit>
#include <QFileInfo>
#include <algorithm>

#include "ui/ContextMenuHelper.h"

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
    setAcceptDrops(true);
    buildUI();
    populateSourceCombo();

    // A5/C: restore results sort state from QSettings. Validate the column
    // against the post-Track-C sortable set (0 Title, 1 Category, 2 Size,
    // 4 Seeders, 5 Leechers); fall back to default (Seeders desc, col 4) on
    // missing, out-of-range, or stale-from-pre-C values (e.g. saved 6 was
    // Leechers pre-C, now Link — invalid).
    {
        QSettings s;
        const int   savedCol   = s.value("tankorent/sortCol",   m_resultsSortCol).toInt();
        const int   savedOrder = s.value("tankorent/sortOrder", static_cast<int>(m_resultsSortOrder)).toInt();
        const bool  validCol   = (savedCol == 0 || savedCol == 1 || savedCol == 2 ||
                                  savedCol == 4 || savedCol == 5);
        if (validCol) m_resultsSortCol = savedCol;
        m_resultsSortOrder = (savedOrder == Qt::AscendingOrder)
                                 ? Qt::AscendingOrder : Qt::DescendingOrder;
        if (m_resultsTable && m_resultsTable->horizontalHeader())
            m_resultsTable->horizontalHeader()->setSortIndicator(
                m_resultsSortCol, m_resultsSortOrder);
    }

    // C1: row-level Link buttons handle their own clicks via cellWidget;
    // double-click anywhere on the row still opens the AddTorrentDialog.
    connect(m_resultsTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int /*col*/) {
        onAddTorrentClicked(row);
    });

    // Transfers table context menu
    connect(m_transfersTable, &QTableWidget::customContextMenuRequested,
            this, &TankorentPage::showTransfersContextMenu);

    // Double-click or Info column click opens TorrentPropertiesWidget.
    auto openPropertiesFor = [this](int row) {
        auto* item = m_transfersTable->item(row, 0);
        if (!item) return;
        const QString hash = item->data(Qt::UserRole).toString();
        if (hash.isEmpty()) return;
        auto* dlg = new TorrentPropertiesWidget(m_client, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->showTorrent(hash);
        dlg->show();
    };
    connect(m_transfersTable, &QTableWidget::cellDoubleClicked, this, [openPropertiesFor](int row, int) {
        openPropertiesFor(row);
    });
    connect(m_transfersTable, &QTableWidget::cellClicked, this, [openPropertiesFor](int row, int col) {
        if (col == 11) openPropertiesFor(row);  // Info column
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
    // Row 1 — query + its immediate actions. Query bar gets a sensible minimum
    // so it stays prominent at any window width; Search/Cancel sit right next
    // to it so "type query, click Search" is one hand-motion (per Hemanth
    // 2026-04-20 UX ask).
    auto *queryRow = new QHBoxLayout;
    queryRow->setContentsMargins(0, 0, 0, 0);
    queryRow->setSpacing(8);

    m_queryEdit = new QLineEdit;
    m_queryEdit->setPlaceholderText("Search torrents...");
    m_queryEdit->setFixedHeight(30);
    m_queryEdit->setMinimumWidth(320);
    connect(m_queryEdit, &QLineEdit::returnPressed, this, &TankorentPage::startSearch);
    queryRow->addWidget(m_queryEdit, 1);

    m_searchBtn = new QPushButton("Search");
    m_searchBtn->setFixedHeight(30);
    m_searchBtn->setMinimumWidth(90);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, &TankorentPage::startSearch);
    queryRow->addWidget(m_searchBtn);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedHeight(30);
    m_cancelBtn->setMinimumWidth(90);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TankorentPage::cancelSearch);
    queryRow->addWidget(m_cancelBtn);

    parent->addLayout(queryRow);

    // Row 2 — filter combos + global actions.
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    m_searchTypeCombo = new QComboBox;
    m_searchTypeCombo->setFixedHeight(30);
    m_searchTypeCombo->setMinimumWidth(130);
    m_searchTypeCombo->addItem("Videos",     "videos");
    m_searchTypeCombo->addItem("Books",      "books");
    m_searchTypeCombo->addItem("Audiobooks", "audiobooks");
    m_searchTypeCombo->addItem("Comics",     "comics");
    row->addWidget(m_searchTypeCombo, 1);

    m_sourceCombo = new QComboBox;
    m_sourceCombo->setFixedHeight(30);
    m_sourceCombo->setMinimumWidth(140);
    connect(m_sourceCombo, &QComboBox::currentIndexChanged, this, [this]() {
        reloadCategoryOptions();
    });
    row->addWidget(m_sourceCombo, 1);

    m_categoryCombo = new QComboBox;
    m_categoryCombo->setFixedHeight(30);
    m_categoryCombo->setMinimumWidth(180);
    m_categoryCombo->addItem("All categories", "");
    m_categoryCombo->setEnabled(false);
    row->addWidget(m_categoryCombo, 1);

    // E1: client-side seeder filter. Applied in renderResults between dedup
    // and the soft cap. Persisted to QSettings tankorent/filter.
    m_filterCombo = new QComboBox;
    m_filterCombo->setFixedHeight(30);
    m_filterCombo->setMinimumWidth(130);
    m_filterCombo->setToolTip("Filter results by seeder count");
    m_filterCombo->addItem("All",            "all");
    m_filterCombo->addItem("Hide dead",      "active");   // seeders >= 1
    m_filterCombo->addItem("High seed only", "high");     // seeders >= 20
    {
        const QString saved = QSettings().value("tankorent/filter", "all").toString();
        const int idx = m_filterCombo->findData(saved);
        m_filterCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    connect(m_filterCombo, &QComboBox::currentIndexChanged, this, [this]() {
        QSettings().setValue("tankorent/filter", m_filterCombo->currentData().toString());
        if (!m_allResults.isEmpty()) renderResults();
    });
    row->addWidget(m_filterCombo, 1);

    // A4: Sort combo removed — sort is now driven by clicking column headers
    // (see onResultsHeaderClicked + compareResults). Default = Seeders desc (A3).

    row->addStretch(1);

    m_refreshBtn = new QPushButton("Refresh");
    m_refreshBtn->setFixedHeight(30);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &TankorentPage::refreshTransfers);
    row->addWidget(m_refreshBtn);

    m_sourcesBtn = new QPushButton("Sources");
    m_sourcesBtn->setFixedHeight(30);
    m_sourcesBtn->setCursor(Qt::PointingHandCursor);
    connect(m_sourcesBtn, &QPushButton::clicked, this, &TankorentPage::onSourcesClicked);
    row->addWidget(m_sourcesBtn);

    m_addUrlBtn = new QPushButton("Add URL");
    m_addUrlBtn->setFixedHeight(30);
    m_addUrlBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addUrlBtn, &QPushButton::clicked, this, &TankorentPage::onAddFromUrlClicked);
    row->addWidget(m_addUrlBtn);

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
    // D1/D2: result count line. Sits above the tab widget so it's visible from
    // both Search Results and Transfers (cleaner than embedding inside the tab
    // and then juggling visibility per tab). Hidden when no results.
    m_resultsCountLabel = new QLabel;
    m_resultsCountLabel->setStyleSheet("color: #a1a1aa; font-size: 11px; padding: 4px 0;");
    m_resultsCountLabel->setOpenExternalLinks(false);
    m_resultsCountLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_resultsCountLabel->hide();
    connect(m_resultsCountLabel, &QLabel::linkActivated, this, [this](const QString&) {
        m_showAll = true;
        renderResults();
    });
    parent->addWidget(m_resultsCountLabel);

    m_tabWidget = new QTabWidget;

    m_resultsTable = createResultsTable();
    m_tabWidget->addTab(m_resultsTable, "Search Results");

    m_transfersTable = createTransfersTable();
    m_tabWidget->addTab(m_transfersTable, "Transfers");

    parent->addWidget(m_tabWidget, 1);
}

QTableWidget* TankorentPage::createResultsTable()
{
    auto *table = new QTableWidget(0, 7);
    table->setObjectName("SearchResultsTable");
    table->setMinimumHeight(280);

    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QTableWidget::customContextMenuRequested, this, &TankorentPage::showResultsContextMenu);

    // C2: Source column is gone — the source is now a "[name]" badge prefixed
    // to the Title cell. C1: Action column ("+") replaced by a Link column
    // hosting download + magnet QToolButtons.
    QStringList headers = { "Title", "Category", "Size", "Files", "Seeders", "Leechers", "Link" };
    table->setHorizontalHeaderLabels(headers);

    auto *hdr = table->horizontalHeader();
    hdr->setMinimumSectionSize(60);
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 7; ++i)
        hdr->setSectionResizeMode(i, QHeaderView::Interactive);

    hdr->resizeSection(1, 130);   // Category
    hdr->resizeSection(2, 110);   // Size
    hdr->resizeSection(3, 90);    // Files
    hdr->resizeSection(4, 90);    // Seeders
    hdr->resizeSection(5, 90);    // Leechers
    hdr->resizeSection(6, 80);    // Link (two icon buttons)

    // A1: click-to-sort. Manual — we sort m_displayedResults ourselves so the
    // model stays the source of truth. setSortingEnabled(true) would let Qt
    // sort by display strings, which mis-sorts sizes ("1.3 GiB" < "520 MiB").
    hdr->setSectionsClickable(true);
    hdr->setSortIndicatorShown(true);
    hdr->setSortIndicator(m_resultsSortCol, m_resultsSortOrder);
    connect(hdr, &QHeaderView::sectionClicked,
            this, &TankorentPage::onResultsHeaderClicked);

    // F3: right-click the header → menu of checkable column-visibility entries.
    // Always lists every column (incl. currently-hidden ones) so the user
    // can recover from accidentally hiding the column they were aiming at.
    // Persisted to QSettings tankorent/hiddenColumns as a CSV of indices.
    {
        const QStringList saved = QSettings()
            .value("tankorent/hiddenColumns").toString()
            .split(',', Qt::SkipEmptyParts);
        for (const QString& s : saved) {
            bool ok = false; const int c = s.toInt(&ok);
            if (ok && c >= 0 && c < table->columnCount())
                table->setColumnHidden(c, true);
        }
    }
    hdr->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(hdr, &QHeaderView::customContextMenuRequested,
            this, [this, table, headers](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction("Visible columns")->setEnabled(false);
        menu.addSeparator();
        for (int c = 0; c < headers.size(); ++c) {
            const QString label = headers[c].isEmpty()
                ? QStringLiteral("(column %1)").arg(c) : headers[c];
            QAction* act = menu.addAction(label);
            act->setCheckable(true);
            act->setChecked(!table->isColumnHidden(c));
            connect(act, &QAction::toggled, this, [this, table, c](bool visible) {
                table->setColumnHidden(c, !visible);
                QStringList hiddenCsv;
                for (int k = 0; k < table->columnCount(); ++k)
                    if (table->isColumnHidden(k)) hiddenCsv << QString::number(k);
                QSettings().setValue("tankorent/hiddenColumns", hiddenCsv.join(','));
            });
        }
        menu.exec(table->horizontalHeader()->mapToGlobal(pos));
    });

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
    m_sourceCombo->addItem("1337x",           "1337x");
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

// Single-purpose trackers (YTS = movies, EZTV = TV, Nyaa = anime/manga) ride
// only with their matching media types; general-purpose trackers ride all.
// 1337x appears in every set but is gated behind its own un-comment in the
// instantiation block below — listing it here makes the flip a one-liner
// once its Cloudflare path lands.
static const QHash<QString, QSet<QString>> kMediaTypeIndexers = {
    { "videos",     { "yts", "eztv", "piratebay", "1337x", "exttorrents" } },
    { "books",      { "piratebay", "exttorrents", "torrentscsv", "1337x" } },
    { "audiobooks", { "piratebay", "exttorrents", "torrentscsv", "1337x" } },
    { "comics",     { "nyaa", "piratebay", "1337x" } },
};

int TankorentPage::dispatchIndexers(const QString& mediaType,
                                    const QString& sourceFilter,
                                    const QString& query,
                                    int limit,
                                    const QString& categoryId)
{
    const QSet<QString> allowed = kMediaTypeIndexers.value(mediaType);
    const bool hasAllowlist = !allowed.isEmpty();
    const bool explicitSource = (sourceFilter != "all");

    QSettings settings;
    auto wanted = [&](const QString& id) -> bool {
        // When the user picks a specific source, honor the explicit intent and
        // skip the media-type allowlist. The allowlist exists to keep the
        // "All Sources" path from spamming irrelevant trackers (e.g. YTS on a
        // comics search); it should not second-guess an explicit pick like
        // Nyaa+Videos for anime (Hemanth 2026-04-20).
        if (explicitSource) {
            if (sourceFilter != id)
                return false;
        } else if (hasAllowlist && !allowed.contains(id)) {
            return false;
        }
        return settings.value(
            QStringLiteral("tankorent/indexers/%1/enabled").arg(id), true).toBool();
    };

    auto addIf = [&](const QString& id, TorrentIndexer* indexer) {
        if (wanted(id))
            m_activeIndexers.append(indexer);
        else
            delete indexer;
    };

    addIf("nyaa",         new NyaaIndexer(m_nam, this));
    addIf("piratebay",    new PirateBayIndexer(m_nam, this));
    addIf("1337x",        new X1337xIndexer(m_nam, this));
    addIf("yts",          new YtsIndexer(m_nam, this));
    addIf("eztv",         new EztvIndexer(m_nam, this));
    addIf("exttorrents",  new ExtTorrentsIndexer(m_nam, this));
    addIf("torrentscsv",  new TorrentsCsvIndexer(m_nam, this));

    m_pendingSearches = m_activeIndexers.size();

    for (auto* idx : m_activeIndexers) {
        connect(idx, &TorrentIndexer::searchFinished, this, &TankorentPage::onSearchFinished);
        connect(idx, &TorrentIndexer::searchError,    this, &TankorentPage::onSearchError);
        idx->search(query, limit, categoryId);
    }

    return m_activeIndexers.size();
}

void TankorentPage::startSearch()
{
    QString query = m_queryEdit->text().trimmed();
    if (query.isEmpty())
        return;

    cancelSearch();

    m_allResults.clear();
    m_resultsTable->setRowCount(0);
    m_showAll = false;                          // D2: re-arm soft cap on each search
    if (m_resultsCountLabel) m_resultsCountLabel->hide();
    m_pendingSearches = 0;

    QString mediaType  = m_searchTypeCombo->currentData().toString();
    QString sourceId   = m_sourceCombo->currentData().toString();
    QString categoryId = m_categoryCombo->currentData().toString();

    const int dispatched = dispatchIndexers(mediaType, sourceId, query, 80, categoryId);

    if (dispatched == 0) {
        m_searchStatus->setText(
            QStringLiteral("No sources available for %1 search")
                .arg(mediaType.isEmpty() ? QStringLiteral("this") : mediaType));
        return;
    }

    m_searchBtn->setVisible(false);
    m_cancelBtn->setVisible(true);
    m_searchStatus->setText("Searching...");
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
    // A1: apply sort via the column-driven comparator. Both initial render and
    // the click-to-sort handler funnel through here. m_resultsSortCol /
    // m_resultsSortOrder are mutated by onResultsHeaderClicked.
    auto sorted = m_allResults;
    const int   sortCol   = m_resultsSortCol;
    const auto  sortOrder = m_resultsSortOrder;
    std::stable_sort(sorted.begin(), sorted.end(),
        [sortCol, sortOrder](const TorrentResult& a, const TorrentResult& b) {
            return compareResults(sortCol, sortOrder, a, b);
        });

    // Three-tier dedup key: canonical infoHash (populated by the indexer at
    // parse time) → btih-regex on magnet (catches v2 64-hex + base32 magnets
    // that canonicalizeInfoHash intentionally rejects) → whole magnet string
    // as last resort so even unparseable magnets still collapse duplicates
    // against themselves.
    QSet<QString> seen;
    QList<TorrentResult> deduped;
    static const QRegularExpression btihRe(
        "btih:([a-fA-F0-9]{40}(?:[a-fA-F0-9]{24})?|[A-Z2-7]{32})",
        QRegularExpression::CaseInsensitiveOption);
    for (const auto& r : sorted) {
        QString key;
        if (!r.infoHash.isEmpty()) {
            key = r.infoHash;
        } else {
            auto m = btihRe.match(r.magnetUri);
            key = m.hasMatch() ? m.captured(1).toLower() : r.magnetUri.toLower();
        }
        if (seen.contains(key)) continue;
        seen.insert(key);
        deduped.append(r);
    }

    // E1: client-side seeder filter. Applied between dedup and the soft cap so
    // the count line and the cap both operate on the visible-relevant set —
    // a "Hide dead" filter doesn't cap to "100 of 1000" if only 30 survive.
    if (m_filterCombo) {
        const QString filterKey = m_filterCombo->currentData().toString();
        if (filterKey == QLatin1String("active")) {
            QList<TorrentResult> kept;
            kept.reserve(deduped.size());
            for (const auto& r : deduped)
                if (r.seeders >= 1) kept.append(r);
            deduped = std::move(kept);
        } else if (filterKey == QLatin1String("high")) {
            QList<TorrentResult> kept;
            kept.reserve(deduped.size());
            for (const auto& r : deduped)
                if (r.seeders >= 20) kept.append(r);
            deduped = std::move(kept);
        }
    }

    // D2: soft cap. Keep the full deduped count for the label; truncate the
    // visible subset to the first kSoftCapRows when the user hasn't asked to
    // see the rest. m_displayedResults must stay row-index-aligned with the
    // table — clip both at the same point.
    static constexpr int kSoftCapRows = 100;
    const int totalDeduped = deduped.size();
    if (!m_showAll && totalDeduped > kSoftCapRows)
        deduped = deduped.mid(0, kSoftCapRows);

    m_displayedResults = deduped;  // row N in table == m_displayedResults[N] — single source of truth

    // D1: result count line. Source count from m_allResults (pre-dedup) so we
    // attribute correctly even if dedup collapsed cross-source duplicates.
    if (m_resultsCountLabel) {
        if (m_allResults.isEmpty()) {
            m_resultsCountLabel->hide();
        } else {
            QSet<QString> sources;
            for (const auto& r : m_allResults)
                if (!r.sourceKey.isEmpty()) sources.insert(r.sourceKey);

            const int srcCount = sources.size();
            QString text;
            if (!m_showAll && totalDeduped > kSoftCapRows) {
                text = QStringLiteral(
                    "Showing %1 of %2 results from %3 source%4 \u2014 "
                    "<a href=\"show\" style=\"color:#60a5fa;text-decoration:none;\">Show all</a>")
                    .arg(kSoftCapRows).arg(totalDeduped).arg(srcCount)
                    .arg(srcCount == 1 ? "" : "s");
            } else {
                text = QStringLiteral("Showing %1 result%2 from %3 source%4")
                    .arg(totalDeduped).arg(totalDeduped == 1 ? "" : "s")
                    .arg(srcCount).arg(srcCount == 1 ? "" : "s");
            }
            m_resultsCountLabel->setText(text);
            m_resultsCountLabel->show();
        }
    }

    m_resultsTable->setRowCount(deduped.size());

    for (int i = 0; i < deduped.size(); ++i) {
        const auto& r = deduped[i];

        // C2: Title with [source] badge prefix + quality tags suffix.
        // C3: full title in the tooltip so elision can't hide information.
        const QString tags    = qualityTagSuffix(r.title);
        const QString badged  = r.sourceName.isEmpty()
            ? r.title
            : QStringLiteral("[%1]  %2").arg(r.sourceName, r.title);
        const QString display = tags.isEmpty() ? badged : badged + "  " + tags;
        auto *titleItem = new QTableWidgetItem(display);
        titleItem->setToolTip(r.title);   // C3
        m_resultsTable->setItem(i, 0, titleItem);

        // Category
        m_resultsTable->setItem(i, 1, new QTableWidgetItem(
            r.category.isEmpty() ? r.categoryId : r.category));

        // Size
        m_resultsTable->setItem(i, 2, new QTableWidgetItem(humanSize(r.sizeBytes)));

        // Files
        auto *filesItem = new QTableWidgetItem("-");
        filesItem->setTextAlignment(Qt::AlignCenter);
        m_resultsTable->setItem(i, 3, filesItem);

        // Seeders — plain integer; row tint (B2) carries the trust signal.
        auto *seedItem = new QTableWidgetItem(QString::number(r.seeders));
        seedItem->setTextAlignment(Qt::AlignCenter);
        m_resultsTable->setItem(i, 4, seedItem);

        // Leechers
        auto *leechItem = new QTableWidgetItem(QString::number(r.leechers));
        leechItem->setTextAlignment(Qt::AlignCenter);
        m_resultsTable->setItem(i, 5, leechItem);

        // C1: Link column with download + magnet QToolButtons.
        // The download button funnels through onAddTorrentClicked (same path
        // the old "+" Action column used). Magnet copies the URI to clipboard.
        // Backing item is empty so row-tint stamping below still applies.
        m_resultsTable->setItem(i, 6, new QTableWidgetItem(QString()));
        auto *linkCell = new QWidget;
        auto *linkLay  = new QHBoxLayout(linkCell);
        linkLay->setContentsMargins(2, 0, 2, 0);
        linkLay->setSpacing(4);
        linkLay->setAlignment(Qt::AlignCenter);

        auto *dlBtn = new QToolButton(linkCell);
        dlBtn->setText(QStringLiteral("\u2193"));   // ↓
        dlBtn->setToolTip("Add torrent");
        dlBtn->setCursor(Qt::PointingHandCursor);
        dlBtn->setAutoRaise(true);
        connect(dlBtn, &QToolButton::clicked, this, [this, i]() {
            onAddTorrentClicked(i);
        });
        linkLay->addWidget(dlBtn);

        auto *magBtn = new QToolButton(linkCell);
        magBtn->setText(QStringLiteral("M"));
        magBtn->setToolTip("Copy magnet link");
        magBtn->setCursor(Qt::PointingHandCursor);
        magBtn->setAutoRaise(true);
        const QString magnet = r.magnetUri;
        connect(magBtn, &QToolButton::clicked, this, [magnet]() {
            QGuiApplication::clipboard()->setText(magnet);
        });
        linkLay->addWidget(magBtn);

        m_resultsTable->setCellWidget(i, 6, linkCell);

        // B2 + F1: Nyaa-style row tint, alternating alpha by row parity so the
        // table's striping survives under the tint (otherwise a band of all-
        // healthy or all-poor rows reads as one solid block). Two brushes per
        // tier: the lower-alpha for odd rows lines up visually with the
        // table's regular alternating Base color.
        static const QBrush kHealthyOdd (QColor(76, 175, 80, 26));   // ~0.10 alpha
        static const QBrush kHealthyEven(QColor(76, 175, 80, 44));   // ~0.17 alpha
        static const QBrush kPoorOdd    (QColor(239, 68, 68, 26));
        static const QBrush kPoorEven   (QColor(239, 68, 68, 44));
        const QString cls = trustClass(r);
        if (cls != QLatin1String("normal")) {
            const bool even = (i % 2 == 0);
            const QBrush& tint = (cls == QLatin1String("healthy"))
                                   ? (even ? kHealthyEven : kHealthyOdd)
                                   : (even ? kPoorEven    : kPoorOdd);
            for (int c = 0; c < 7; ++c) {
                if (auto* cell = m_resultsTable->item(i, c))
                    cell->setBackground(tint);
            }
        }
    }

    m_tabWidget->setCurrentIndex(0);
}

// ══════════════════════════════════════════════════════════════════════════════
// A1: click-to-sort comparator + header handler
// ══════════════════════════════════════════════════════════════════════════════

bool TankorentPage::compareResults(int col, Qt::SortOrder order,
                                    const TorrentResult& a, const TorrentResult& b)
{
    auto cmpThen = [order](bool less) {
        return order == Qt::AscendingOrder ? less : !less;
    };
    // Post-Track-C layout: 0 Title, 1 Category, 2 Size, 3 Files,
    // 4 Seeders, 5 Leechers, 6 Link.
    switch (col) {
    case 0: // Title
        return cmpThen(a.title.compare(b.title, Qt::CaseInsensitive) < 0);
    case 1: // Category
        return cmpThen(a.category.compare(b.category, Qt::CaseInsensitive) < 0);
    case 2: // Size
        return cmpThen(a.sizeBytes < b.sizeBytes);
    case 4: // Seeders
        return cmpThen(a.seeders < b.seeders);
    case 5: // Leechers
        return cmpThen(a.leechers < b.leechers);
    default:
        // Cols 3 (Files) and 6 (Link) have no sortable backing field.
        return false;   // stable_sort keeps original order
    }
}

void TankorentPage::onResultsHeaderClicked(int col)
{
    // Non-sortable columns: ignore. Header indicator stays where it was.
    // Post-Track-C: 3 Files, 6 Link.
    if (col == 3 || col == 6) return;

    if (col == m_resultsSortCol) {
        // Same column: flip direction.
        m_resultsSortOrder = (m_resultsSortOrder == Qt::AscendingOrder)
                                 ? Qt::DescendingOrder : Qt::AscendingOrder;
    } else {
        // New column: pick the column-default direction. Numeric cols default
        // descending (high seeders / large sizes first); strings ascending.
        m_resultsSortCol = col;
        const bool numeric = (col == 2 || col == 4 || col == 5);
        m_resultsSortOrder = numeric ? Qt::DescendingOrder : Qt::AscendingOrder;
    }

    if (m_resultsTable && m_resultsTable->horizontalHeader())
        m_resultsTable->horizontalHeader()->setSortIndicator(
            m_resultsSortCol, m_resultsSortOrder);

    // A5: persist for the next session.
    {
        QSettings s;
        s.setValue("tankorent/sortCol",   m_resultsSortCol);
        s.setValue("tankorent/sortOrder", static_cast<int>(m_resultsSortOrder));
    }

    renderResults();
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
// B1: trust class for Nyaa-style row tint (replaces the per-cell health dot)
// ══════════════════════════════════════════════════════════════════════════════

QString TankorentPage::trustClass(const TorrentResult& r)
{
    if (r.seeders >= 50) return QStringLiteral("healthy");
    if (r.seeders >= 5)  return QStringLiteral("normal");
    return QStringLiteral("poor");
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

    menu->addAction("View Files...", this, [this, firstHash]() {
        auto* dlg = new TorrentPropertiesWidget(m_client, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->showTorrent(firstHash, TorrentPropertiesWidget::TabFiles);
        dlg->show();
    });

    menu->addAction("Properties...", this, [this, firstHash]() {
        auto* dlg = new TorrentPropertiesWidget(m_client, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->showTorrent(firstHash);
        dlg->show();
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

void TankorentPage::onSourcesClicked()
{
    IndexerStatusPanel dlg(m_nam, this);
    dlg.exec();
    // configurationChanged fires on enable/credential changes; no live handling
    // needed — the next startSearch re-reads QSettings via dispatchIndexers.
}

QPair<int, int> TankorentPage::addMagnetBatch(const QStringList& magnets,
                                              const QString& category,
                                              bool startImmediately)
{
    const auto defaults = m_client->defaultPaths();
    const QString destPath = defaults.value(category);

    int added = 0;
    int skipped = 0;
    for (const QString& magnet : magnets) {
        if (m_client->isDuplicate(magnet)) {
            ++skipped;
            continue;
        }
        const QString hash = m_client->resolveMetadata(magnet);
        if (hash.isEmpty()) {
            ++skipped;
            continue;
        }

        AddTorrentConfig config;
        config.category        = category;
        config.destinationPath = destPath;
        config.contentLayout   = QStringLiteral("original");
        config.sequential      = false;
        config.startPaused     = !startImmediately;
        m_client->startDownload(hash, config);
        ++added;
    }
    return { added, skipped };
}

void TankorentPage::onAddFromUrlClicked()
{
    AddFromUrlDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const auto [added, skipped] = addMagnetBatch(
        dlg.magnets(), dlg.category(), dlg.startImmediately());

    if (added > 0) {
        m_tabWidget->setCurrentIndex(1); // Transfers tab
        m_searchStatus->setText(skipped > 0
            ? QStringLiteral("Added %1, skipped %2").arg(added).arg(skipped)
            : QStringLiteral("Added %1 torrent(s)").arg(added));
    } else if (skipped > 0) {
        m_searchStatus->setText(QStringLiteral("All %1 skipped (duplicates or invalid)").arg(skipped));
    }
}

// ── Drag-drop ───────────────────────────────────────────────────────────────

void TankorentPage::dragEnterEvent(QDragEnterEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!mime) return;

    if (mime->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    if (mime->hasText()) {
        const QString text = mime->text().trimmed();
        if (text.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive))
            event->acceptProposedAction();
    }
}

void TankorentPage::dropEvent(QDropEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!mime) return;

    QStringList magnets;
    int torrentFileCount = 0;  // local .torrent files + .torrent URLs — unsupported

    auto classify = [&](const QString& raw) {
        const QString item = raw.trimmed();
        if (item.isEmpty()) return;
        if (item.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)) {
            magnets.append(item);
            return;
        }
        // Local file URL or HTTP URL ending in .torrent — both need the
        // add-torrent-file engine path (see Batch 5.1 scope note).
        const QString lower = item.toLower();
        if (lower.endsWith(QLatin1String(".torrent")) ||
            QFileInfo(item).suffix().toLower() == QLatin1String("torrent")) {
            ++torrentFileCount;
        }
    };

    if (mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            if (url.scheme() == QLatin1String("magnet"))
                magnets.append(url.toString());
            else if (url.isLocalFile())
                classify(url.toLocalFile());
            else
                classify(url.toString());
        }
    }
    if (mime->hasText()) {
        for (const QString& line : mime->text().split('\n', Qt::SkipEmptyParts))
            classify(line);
    }

    magnets.removeDuplicates();

    if (magnets.isEmpty()) {
        if (torrentFileCount > 0) {
            Toast::show(this, QStringLiteral(
                ".torrent files aren't supported yet — use magnet links."));
        } else {
            Toast::show(this, QStringLiteral("No magnet links found in drop."));
        }
        event->acceptProposedAction();
        return;
    }

    // Surface the magnets through AddFromUrlDialog so the user picks a
    // category + start-immediately flag for the batch. Matches the TODO's
    // "Multiple items → open AddFromUrlDialog pre-filled" contract.
    AddFromUrlDialog dlg(this, magnets.join('\n'));
    if (dlg.exec() == QDialog::Accepted) {
        const auto [added, skipped] = addMagnetBatch(
            dlg.magnets(), dlg.category(), dlg.startImmediately());

        if (added > 0) {
            m_tabWidget->setCurrentIndex(1);
            QString msg = QStringLiteral("Added %1 torrent(s)").arg(added);
            if (skipped > 0)
                msg += QStringLiteral(", skipped %1").arg(skipped);
            if (torrentFileCount > 0)
                msg += QStringLiteral(" (%1 .torrent file%2 ignored)")
                           .arg(torrentFileCount)
                           .arg(torrentFileCount == 1 ? "" : "s");
            Toast::show(this, msg);
        }
    }

    event->acceptProposedAction();
}

void TankorentPage::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Paste)) {
        QWidget* focused = QApplication::focusWidget();
        const bool inTextInput = qobject_cast<QLineEdit*>(focused)
                              || qobject_cast<QTextEdit*>(focused);
        if (!inTextInput) {
            onAddFromUrlClicked();
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}
