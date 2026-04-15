#include "TankoyomiPage.h"
#include "core/CoreBridge.h"
#include "core/JsonStore.h"
#include "core/manga/MangaScraper.h"
#include "core/manga/WeebCentralScraper.h"
#include "core/manga/ReadComicsScraper.h"
#include "core/manga/MangaDownloader.h"
#include "ui/dialogs/AddMangaDialog.h"
#include "ui/dialogs/MangaTransferDialog.h"
#include "ui/pages/tankoyomi/MangaResultsGrid.h"
#include "ui/widgets/Toast.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QProgressBar>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QPalette>
#include <QStyleFactory>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

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

    // B1: manga poster cache directory
    m_posterCacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                       + "/Tankoban/data/manga_posters";
    QDir().mkpath(m_posterCacheDir);

    // Create scrapers
    auto* weeb = new WeebCentralScraper(m_nam, this);
    auto* rco  = new ReadComicsScraper(m_nam, this);
    m_scrapers = { weeb, rco };

    // Create downloader
    m_downloader = new MangaDownloader(&m_bridge->store(), this);
    m_downloader->setScraper("weebcentral", weeb);
    m_downloader->setScraper("readcomicsonline", rco);

    buildUI();

    // B3: initial view-toggle label reflects the restored mode. Button label
    // shows the *other* view so clicking it reads like an action.
    m_viewToggleBtn->setText(m_preferredDataView == 0 ? "Grid" : "List");

    // B4: set the initial empty-state copy.
    updateResultsView();

    // B3: grid consumes prefetched covers and drives result activation.
    connect(this, &TankoyomiPage::coverReady,
            m_resultsGrid, &MangaResultsGrid::onCoverReady);
    connect(m_resultsGrid, &MangaResultsGrid::resultActivated,
            this, &TankoyomiPage::onResultDoubleClicked);

    // A2: keep pause button label synced with engine state
    connect(m_downloader, &MangaDownloader::pausedChanged, this, [this](bool paused) {
        m_pauseBtn->setText(paused ? "Resume Downloads" : "Pause Downloads");
    });

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
                const bool allFailed = m_allResults.isEmpty();
                m_searchStatus->setText(allFailed
                    ? "Search failed"
                    : QString("Done: %1 Results").arg(m_allResults.size()));
                m_cancelBtn->setVisible(false);
                m_searchBtn->setVisible(true);
                // C1: surface scraper errors via a transient Toast with a
                // Retry action instead of wedging the error string into the
                // small status label. Parent to the top-level window so the
                // toast anchors to the main window, not just our tab.
                QWidget* anchor = window() ? window() : this;
                if (allFailed) {
                    Toast::show(anchor,
                                QStringLiteral("Search failed — %1").arg(err),
                                QStringLiteral("Retry"),
                                [this]() { startSearch(); });
                } else {
                    Toast::show(anchor,
                                QStringLiteral("One source failed: %1").arg(err));
                }
                // B5: drop out of the loading page now that nothing is in flight.
                updateResultsView();
            }
        });
    }

    // Double-click on result → open chapter picker
    connect(m_resultsTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        onResultDoubleClicked(row);
    });

    // Results context menu — shared between table (B) and grid (E2).
    connect(m_resultsTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int row = m_resultsTable->rowAt(pos.y());
        if (row < 0) return;
        showResultContextMenu(row, m_resultsTable->viewport()->mapToGlobal(pos));
    });
    // E2: same menu when right-clicking a grid tile.
    connect(m_resultsGrid, &MangaResultsGrid::resultRightClicked, this,
            [this](int row, const QPoint& globalPos) {
        showResultContextMenu(row, globalPos);
    });

    // Double-click a transfer → open details dialog
    connect(m_transfersTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto* item = m_transfersTable->item(row, 0);
        if (!item) return;
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        MangaTransferDialog dlg(id, m_downloader, this);
        dlg.exec();
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

        // R5: queue reorder actions — disabled when the row is already at the
        // target end. Total-row count is rowCount(); the engine treats order
        // as contiguous so the UI index matches the internal order 1:1.
        const int totalRows = m_transfersTable->rowCount();
        auto* moveTopAct = menu->addAction("Move to top", this, [this, id]() {
            m_downloader->moveSeriesToTop(id);
            refreshTransfers();
        });
        moveTopAct->setEnabled(row > 0);
        auto* moveBottomAct = menu->addAction("Move to bottom", this, [this, id]() {
            m_downloader->moveSeriesToBottom(id);
            refreshTransfers();
        });
        moveBottomAct->setEnabled(row < totalRows - 1);

        // R6: sort the queued chapters of this series. Doesn't touch in-flight
        // or completed chapters.
        auto* sortMenu = menu->addMenu("Sort chapters");
        sortMenu->addAction("By chapter number (ascending)", this, [this, id]() {
            m_downloader->reorderChapters(id, "chapter_number", true);
        });
        sortMenu->addAction("By chapter number (descending)", this, [this, id]() {
            m_downloader->reorderChapters(id, "chapter_number", false);
        });
        sortMenu->addAction("By upload date (newest first)", this, [this, id]() {
            m_downloader->reorderChapters(id, "date", false);
        });
        sortMenu->addAction("By upload date (oldest first)", this, [this, id]() {
            m_downloader->reorderChapters(id, "date", true);
        });
        menu->addSeparator();

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

    // C2: client-side sort combo. "As returned" keeps the scraper order (also
    // used as default for fresh installs); other keys re-sort m_displayedResults
    // in renderResults() before pushing to either view.
    // Q3: label renamed from "Relevance" — with multi-source aggregation the
    // order is arrival-interleave, not quality-ranked, so "Relevance" misleads.
    // Key stays "relevance" so previously-saved QSettings still restore.
    m_sortCombo = new QComboBox;
    m_sortCombo->setFixedHeight(30);
    m_sortCombo->setMinimumWidth(120);
    m_sortCombo->setCursor(Qt::PointingHandCursor);
    m_sortCombo->setToolTip("Sort search results");
    m_sortCombo->addItem("As returned", "relevance");
    m_sortCombo->addItem("Title A–Z",   "title_asc");
    m_sortCombo->addItem("Title Z–A",   "title_desc");
    m_sortCombo->addItem("Source",      "source");
    {
        const QString saved = QSettings().value("tankoyomi/sortKey", "relevance").toString();
        const int idx = m_sortCombo->findData(saved);
        m_sortCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        QSettings().setValue("tankoyomi/sortKey", m_sortCombo->currentData().toString());
        renderResults();   // re-sorts + re-renders in place
    });
    row->addWidget(m_sortCombo);

    // B3: grid/list view toggle. Label reflects the *other* view (so click
    // reads as an action). B4: flipping only swaps which data page would be
    // shown — if results are empty, the empty state stays visible.
    m_viewToggleBtn = new QPushButton;
    m_viewToggleBtn->setFixedHeight(30);
    m_viewToggleBtn->setCursor(Qt::PointingHandCursor);
    m_viewToggleBtn->setToolTip("Toggle between list and grid view");
    connect(m_viewToggleBtn, &QPushButton::clicked, this, [this]() {
        m_preferredDataView = (m_preferredDataView == 1) ? 0 : 1;
        QSettings().setValue("tankoyomi/resultsView",
                             m_preferredDataView == 0 ? "list" : "grid");
        m_viewToggleBtn->setText(m_preferredDataView == 0 ? "Grid" : "List");
        updateResultsView();
    });
    row->addWidget(m_viewToggleBtn);

    // A2: Pause/Resume the download engine. Hidden when there are no active
    // downloads; label flips on MangaDownloader::pausedChanged.
    m_pauseBtn = new QPushButton("Pause Downloads");
    m_pauseBtn->setFixedHeight(30);
    m_pauseBtn->setCursor(Qt::PointingHandCursor);
    m_pauseBtn->setVisible(false);
    connect(m_pauseBtn, &QPushButton::clicked, this, [this]() {
        if (m_downloader->isPaused())
            m_downloader->resumeAll();
        else
            m_downloader->pauseAll();
    });
    row->addWidget(m_pauseBtn);

    // A3: overflow menu (Cancel All, future global actions). Hidden in lockstep
    // with the Pause button.
    m_moreBtn = new QPushButton(QStringLiteral("\u22EE"));   // vertical ellipsis
    m_moreBtn->setFixedSize(30, 30);
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setToolTip("More download actions");
    m_moreBtn->setVisible(false);
    connect(m_moreBtn, &QPushButton::clicked, this, [this]() {
        QMenu menu(this);
        auto* cancelAllAct = menu.addAction("Cancel All Downloads");
        connect(cancelAllAct, &QAction::triggered, this, [this]() {
            auto btn = QMessageBox::question(this, "Cancel All Downloads",
                "Cancel every active download? This will stop in-progress series; "
                "already-downloaded files on disk are kept.",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (btn == QMessageBox::Yes) {
                // If we're paused, unpause first so cancel can proceed cleanly.
                if (m_downloader->isPaused())
                    m_downloader->resumeAll();
                m_downloader->cancelAll();
                refreshTransfers();
            }
        });
        menu.exec(m_moreBtn->mapToGlobal(QPoint(0, m_moreBtn->height())));
    });
    row->addWidget(m_moreBtn);

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

    // B3/B4: Search Results tab holds a stacked widget:
    //   index 0 = list (table), index 1 = grid, index 2 = empty state.
    // m_preferredDataView tracks the user's list-vs-grid choice independent of
    // which page is actually shown — so we can flip back to their preference
    // the moment results arrive.
    m_resultsTable = createResultsTable();
    m_resultsGrid  = new MangaResultsGrid;

    // B4/E3: empty-state page with a label + action buttons. Buttons are
    // hidden in the pre-search state (label-only) and shown after a search
    // returns zero results so the user can Retry or Clear.
    m_emptyPage = new QWidget;
    {
        auto* v = new QVBoxLayout(m_emptyPage);
        v->setAlignment(Qt::AlignCenter);
        v->setSpacing(16);

        m_emptyLabel = new QLabel;
        m_emptyLabel->setObjectName("TankoyomiEmptyState");
        m_emptyLabel->setAlignment(Qt::AlignCenter);
        m_emptyLabel->setWordWrap(true);
        m_emptyLabel->setStyleSheet(
            "#TankoyomiEmptyState { color: #a1a1aa; font-size: 14px; }");
        v->addWidget(m_emptyLabel);

        auto* btnRow = new QHBoxLayout;
        btnRow->setSpacing(8);
        btnRow->setAlignment(Qt::AlignCenter);

        m_emptyRetryBtn = new QPushButton("Retry");
        m_emptyRetryBtn->setFixedHeight(28);
        m_emptyRetryBtn->setCursor(Qt::PointingHandCursor);
        m_emptyRetryBtn->hide();
        connect(m_emptyRetryBtn, &QPushButton::clicked, this, [this]() {
            if (!m_lastQuery.isEmpty()) {
                m_queryEdit->setText(m_lastQuery);
                startSearch();
            }
        });
        btnRow->addWidget(m_emptyRetryBtn);

        m_emptyClearBtn = new QPushButton("Clear search");
        m_emptyClearBtn->setFixedHeight(28);
        m_emptyClearBtn->setCursor(Qt::PointingHandCursor);
        m_emptyClearBtn->hide();
        connect(m_emptyClearBtn, &QPushButton::clicked, this, [this]() {
            m_queryEdit->clear();
            m_lastQuery.clear();
            m_queryEdit->setFocus();
            updateResultsView();
        });
        btnRow->addWidget(m_emptyClearBtn);

        v->addLayout(btnRow);
    }

    // B5: loading page — indeterminate progress bar + status line. Used while
    // any scraper's search is in flight.
    m_loadingPage = new QWidget;
    {
        auto* v = new QVBoxLayout(m_loadingPage);
        v->setAlignment(Qt::AlignCenter);
        v->setSpacing(16);

        m_loadingLabel = new QLabel("Searching...");
        m_loadingLabel->setAlignment(Qt::AlignCenter);
        m_loadingLabel->setStyleSheet("color: #cbd5e1; font-size: 14px;");
        v->addWidget(m_loadingLabel);

        auto* bar = new QProgressBar;
        bar->setRange(0, 0);                  // indeterminate
        bar->setTextVisible(false);
        bar->setFixedWidth(220);
        bar->setFixedHeight(4);
        bar->setStyleSheet(
            "QProgressBar { background: rgba(255,255,255,0.08); border: none; "
            "  border-radius: 2px; }"
            "QProgressBar::chunk { background: #60a5fa; border-radius: 2px; }");
        v->addWidget(bar, 0, Qt::AlignCenter);
    }

    m_resultsStack = new QStackedWidget;
    m_resultsStack->addWidget(m_resultsTable);  // index 0: list
    m_resultsStack->addWidget(m_resultsGrid);   // index 1: grid
    m_resultsStack->addWidget(m_emptyPage);     // index 2: empty state
    m_resultsStack->addWidget(m_loadingPage);   // index 3: loading

    const QString savedMode = QSettings().value("tankoyomi/resultsView", "grid").toString();
    m_preferredDataView = (savedMode == "list") ? 0 : 1;
    // Start on empty state until a search runs.
    m_resultsStack->setCurrentIndex(2);

    m_tabWidget->addTab(m_resultsStack, "Search Results");

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
    m_lastQuery = query;   // B4: drives the zero-results empty-state copy
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

    // B5: flip to the loading page now that at least one scraper is in flight.
    updateResultsView();
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
                const bool allFailed = m_allResults.isEmpty();
                m_searchStatus->setText(allFailed
                    ? "Search failed"
                    : QString("Done: %1 Results").arg(m_allResults.size()));
                m_cancelBtn->setVisible(false);
                m_searchBtn->setVisible(true);
                // C1: surface scraper errors via a transient Toast with a
                // Retry action instead of wedging the error string into the
                // small status label. Parent to the top-level window so the
                // toast anchors to the main window, not just our tab.
                QWidget* anchor = window() ? window() : this;
                if (allFailed) {
                    Toast::show(anchor,
                                QStringLiteral("Search failed — %1").arg(err),
                                QStringLiteral("Retry"),
                                [this]() { startSearch(); });
                } else {
                    Toast::show(anchor,
                                QStringLiteral("One source failed: %1").arg(err));
                }
                // B5: drop out of the loading page now that nothing is in flight.
                updateResultsView();
            }
        });
    }

    m_pendingSearches = 0;
    m_searchBtn->setVisible(true);
    m_cancelBtn->setVisible(false);
    m_searchStatus->setText("Search Cancelled");
    // B5: drop out of the loading page on cancel.
    updateResultsView();
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

    // C2: apply client-side sort. "relevance" preserves scraper order so the
    // first entry stays what WeebCentral/ReadComics considered the best match.
    const QString sortKey = m_sortCombo ? m_sortCombo->currentData().toString()
                                         : QStringLiteral("relevance");
    if (sortKey == QLatin1String("title_asc")) {
        std::stable_sort(m_displayedResults.begin(), m_displayedResults.end(),
            [](const MangaResult& a, const MangaResult& b) {
                return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
            });
    } else if (sortKey == QLatin1String("title_desc")) {
        std::stable_sort(m_displayedResults.begin(), m_displayedResults.end(),
            [](const MangaResult& a, const MangaResult& b) {
                return a.title.compare(b.title, Qt::CaseInsensitive) > 0;
            });
    } else if (sortKey == QLatin1String("source")) {
        std::stable_sort(m_displayedResults.begin(), m_displayedResults.end(),
            [](const MangaResult& a, const MangaResult& b) {
                const int cmp = a.source.compare(b.source, Qt::CaseInsensitive);
                if (cmp != 0) return cmp < 0;
                return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
            });
    }
    // "relevance" falls through — order untouched.

    m_resultsTable->setRowCount(m_displayedResults.size());

    for (int i = 0; i < m_displayedResults.size(); ++i) {
        const auto& r = m_displayedResults[i];

        auto* titleItem = new QTableWidgetItem(r.title);
        titleItem->setData(Qt::UserRole, r.source);       // source ID
        titleItem->setData(Qt::UserRole + 1, r.id);       // series ID
        m_resultsTable->setItem(i, 0, titleItem);

        m_resultsTable->setItem(i, 1, new QTableWidgetItem(r.author));
        m_resultsTable->setItem(i, 2, new QTableWidgetItem(mangaSourceDisplayName(r.source)));
        m_resultsTable->setItem(i, 3, new QTableWidgetItem(r.status));
        m_resultsTable->setItem(i, 4, new QTableWidgetItem(r.type));

        // B1: prefetch the cover into the on-disk cache. The grid view (B2) and
        // the detail panel (C3) consume coverReady; for the current table view
        // the file just sits warm for the next consumer.
        if (!r.thumbnailUrl.isEmpty())
            ensureCover(r.source, r.id, r.thumbnailUrl);
    }

    // B3: mirror results into the grid view so the user sees the same data
    // regardless of which mode is active.
    if (m_resultsGrid) {
        m_resultsGrid->setResults(m_displayedResults);

        // E1: compute which results are already in the user's library so the
        // grid can render an "IN LIBRARY" badge + dim. Criterion: either the
        // downloader has an active/history record with the matching title, or
        // a directory of that name exists under any comics root.
        QSet<QString> inLibraryKeys;
        QSet<QString> libraryTitles;
        for (const auto& rec : m_downloader->listActive())
            libraryTitles.insert(rec.seriesTitle.toLower().trimmed());
        const auto history = m_downloader->listHistory();
        for (const auto& v : history) {
            const auto o = v.toObject();
            libraryTitles.insert(o.value("seriesTitle").toString().toLower().trimmed());
        }
        QStringList comicRoots = m_bridge->rootFolders("comics");
        QSet<QString> diskTitles;
        for (const auto& root : comicRoots) {
            QDir d(root);
            for (const auto& name : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                diskTitles.insert(name.toLower().trimmed());
        }
        for (const auto& r : m_displayedResults) {
            const QString t = r.title.toLower().trimmed();
            if (libraryTitles.contains(t) || diskTitles.contains(t))
                inLibraryKeys.insert(r.source + QStringLiteral("_") + r.id);
        }
        m_resultsGrid->setInLibraryKeys(inLibraryKeys);
    }

    // B4: pick between data-view and empty-state based on result count.
    updateResultsView();

    m_tabWidget->setCurrentIndex(0);
}

// ── B4/B5: pick data view vs empty state vs loading ─────────────────────────
void TankoyomiPage::updateResultsView()
{
    if (!m_resultsStack) return;

    // B5: a search is in flight — show loading page regardless of stale results.
    if (m_pendingSearches > 0) {
        m_loadingLabel->setText(m_lastQuery.isEmpty()
            ? "Searching..."
            : QString("Searching for \"%1\"...").arg(m_lastQuery));
        m_resultsStack->setCurrentIndex(3);
        return;
    }

    if (m_displayedResults.isEmpty()) {
        const bool postSearch = !m_lastQuery.isEmpty();
        m_emptyLabel->setText(postSearch
            ? QString("No results for \"%1\"").arg(m_lastQuery)
            : QStringLiteral("Search manga & comics above"));
        // E3: Retry + Clear only make sense once the user has run a search.
        if (m_emptyRetryBtn) m_emptyRetryBtn->setVisible(postSearch);
        if (m_emptyClearBtn) m_emptyClearBtn->setVisible(postSearch);
        m_resultsStack->setCurrentIndex(2);
    } else {
        m_resultsStack->setCurrentIndex(m_preferredDataView);
    }
}

// ── B1: cover cache ─────────────────────────────────────────────────────────
QString TankoyomiPage::ensureCover(const QString& source, const QString& id,
                                    const QString& thumbUrl)
{
    // Sanitize the id so slugs with awkward chars don't break the filesystem.
    QString safeId = id;
    safeId.replace(QRegularExpression(R"([<>:"/\\|?*\s])"), "_");
    const QString key  = source + "_" + safeId;
    const QString path = m_posterCacheDir + "/" + key + ".jpg";

    // Cache hit → emit immediately and return. Connectors formed after this
    // point are fine; queued emit via singleShot keeps signal delivery async
    // so callers don't have to worry about re-entrancy during renderResults.
    if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
        QTimer::singleShot(0, this, [this, source, id, path]() {
            emit coverReady(source, id, path);
        });
        return path;
    }

    // De-dupe in-flight requests for the same key.
    if (m_coversInFlight.contains(key)) return path;
    if (thumbUrl.isEmpty()) return path;

    m_coversInFlight.insert(key);

    QNetworkRequest req{QUrl(thumbUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    // Referer — some CDNs reject hotlinks without it.
    if (source == "weebcentral")
        req.setRawHeader("Referer", "https://weebcentral.com/");
    else if (source == "readcomicsonline")
        req.setRawHeader("Referer", "https://readcomicsonline.ru/");
    req.setTransferTimeout(10000);

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, source, id, key, path]() {
            reply->deleteLater();
            m_coversInFlight.remove(key);

            if (reply->error() != QNetworkReply::NoError) return;
            const QByteArray data = reply->readAll();
            if (data.isEmpty()) return;

            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) return;
            f.write(data);
            f.close();

            emit coverReady(source, id, path);
        });

    return path;
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

    // C3: push metadata + cover (path comes from B1's cache; file may appear
    // asynchronously, so also re-push on coverReady while the dialog is open).
    dlg.setMangaMetadata(result);
    if (!result.thumbnailUrl.isEmpty()) {
        const QString coverPath = ensureCover(result.source, result.id, result.thumbnailUrl);
        dlg.setCoverPath(coverPath);
        auto coverConn = connect(this, &TankoyomiPage::coverReady, &dlg,
            [&dlg, rid = result.id, rsrc = result.source]
            (const QString& src, const QString& id, const QString& path) {
                if (src == rsrc && id == rid) dlg.setCoverPath(path);
            });
        connect(&dlg, &QDialog::destroyed, this, [this, coverConn]() {
            disconnect(coverConn);
        });
    }

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

// ── E2: shared results context menu (table + grid) ──────────────────────────
void TankoyomiPage::showResultContextMenu(int row, const QPoint& globalPos)
{
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
    menu->exec(globalPos);
    delete menu;
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

    // A5: count pending chapters and the series that still have any pending work.
    // Used for both the tab badge and the Pause/Overflow visibility check.
    int  pendingChapters = 0;
    int  pendingSeries   = 0;
    for (const auto& rec : active) {
        int chaptersHere = 0;
        for (const auto& ch : rec.chapters) {
            if (ch.status == "queued" || ch.status == "downloading")
                ++chaptersHere;
        }
        if (chaptersHere > 0) {
            pendingChapters += chaptersHere;
            ++pendingSeries;
        }
    }
    const bool hasPendingWork = pendingChapters > 0;

    // Tab badge — show total active records (including finished/errored ones the
    // user hasn't dismissed), plus a pending-chapter count when work remains.
    QString tabLabel;
    if (active.isEmpty()) {
        tabLabel = "Transfers";
    } else if (pendingChapters > 0) {
        tabLabel = QString("Transfers · %1 series · %2 chapters pending")
                       .arg(pendingSeries)
                       .arg(pendingChapters);
    } else {
        tabLabel = QString("Transfers (%1)").arg(active.size());
    }
    m_tabWidget->setTabText(1, tabLabel);

    // A2/A3: Pause and overflow visibility piggyback on pending work.
    m_pauseBtn->setVisible(hasPendingWork);
    m_pauseBtn->setText(m_downloader->isPaused() ? "Resume Downloads" : "Pause Downloads");
    m_moreBtn->setVisible(hasPendingWork);
}
