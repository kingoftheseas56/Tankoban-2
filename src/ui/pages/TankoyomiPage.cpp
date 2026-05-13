#include "TankoyomiPage.h"
#include "core/CoreBridge.h"
#include "core/JsonStore.h"
#include "core/manga/MangaScraper.h"
#include "core/manga/WeebCentralScraper.h"
#include "core/manga/ReadComicsScraper.h"
#include "core/manga/MangaDownloader.h"
#include "ui/dialogs/MangaTransferDialog.h"
#include "ui/pages/tankoyomi/MangaDetailView.h"
#include "ui/pages/tankoyomi/MangaResultsGrid.h"
#include "ui/pages/tankoyomi/TransferGroupCard.h"
#include "ui/widgets/Toast.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QProgressBar>
#include <QScrollArea>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QColor>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QPalette>
#include <QStyleFactory>
#include <QIcon>
#include <QSize>
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

namespace {
inline QColor fgMutedColor()
{
    QColor c = QApplication::palette().color(QPalette::Text);
    c.setAlpha(140);
    return c;
}

// T17 — map raw MangaDownloader chapter-state enum strings to Title Case
// display strings. Applied at transfer-row render boundary. Audited
// MangaDownloader source 2026-05-13 to enumerate vocabulary.
// Confirmed states: queued, downloading, completed, error, cancelled.
// Defensive coverage added for paused/complete/waiting/resolving aliases.
inline QString chapterStatusText(const QString& rawState)
{
    if (rawState == QLatin1String("queued"))      return QStringLiteral("Queued");
    if (rawState == QLatin1String("downloading")) return QStringLiteral("Downloading");
    if (rawState == QLatin1String("paused"))      return QStringLiteral("Paused");
    if (rawState == QLatin1String("complete"))    return QStringLiteral("Complete");
    if (rawState == QLatin1String("completed"))   return QStringLiteral("Complete");
    if (rawState == QLatin1String("failed"))      return QStringLiteral("Failed");
    if (rawState == QLatin1String("cancelled"))   return QStringLiteral("Cancelled");
    if (rawState == QLatin1String("error"))       return QStringLiteral("Error");
    if (rawState == QLatin1String("waiting"))     return QStringLiteral("Waiting");
    if (rawState == QLatin1String("resolving"))   return QStringLiteral("Resolving");
    // Fallback: capitalize first letter for any state missed in the audit.
    if (rawState.isEmpty()) return rawState;
    QString out = rawState;
    out[0] = out[0].toUpper();
    return out;
}
}



// ── Constructor ─────────────────────────────────────────────────────────────
TankoyomiPage::TankoyomiPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent), m_bridge(bridge)
{
    qRegisterMetaType<MangaResult>();
    qRegisterMetaType<QList<MangaResult>>();
    qRegisterMetaType<ChapterInfo>();
    qRegisterMetaType<QList<ChapterInfo>>();

    m_nam = new QNetworkAccessManager(this);
    setObjectName(QStringLiteral("TankoyomiPage"));

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

    // Mihon-overhaul E.5 — transfers tab is now a card list; per-card
    // double-click + context-menu wiring lives on each TransferGroupCard, set up
    // in refreshTransfers() when the card is created. The detail-dialog open and
    // context-menu vocabulary moves to showTransferCardContextMenu (F.1 fills).

    // Auto-refresh transfers
    m_transferTimer = new QTimer(this);
    connect(m_transferTimer, &QTimer::timeout, this, &TankoyomiPage::refreshTransfers);
    m_transferTimer->start(1000);
}

// ── UI ──────────────────────────────────────────────────────────────────────
void TankoyomiPage::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    buildSearchControls(root);
    buildStatusRow(root);
    buildMainTabs(root);
}

void TankoyomiPage::buildSearchControls(QVBoxLayout* parent)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    m_queryEdit = new QLineEdit;
    m_queryEdit->setPlaceholderText("Search manga & comics...");
    m_queryEdit->setFixedHeight(36);
    connect(m_queryEdit, &QLineEdit::returnPressed, this, &TankoyomiPage::startSearch);
    row->addWidget(m_queryEdit, 3);

    m_sourceCombo = new QComboBox;
    m_sourceCombo->setFixedHeight(36);
    m_sourceCombo->setMinimumWidth(160);
    m_sourceCombo->addItem("All Sources", "all");
    row->addWidget(m_sourceCombo, 1);

    m_searchBtn = new QPushButton("Search");
    m_searchBtn->setFixedHeight(36);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, &TankoyomiPage::startSearch);
    row->addWidget(m_searchBtn);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedHeight(36);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TankoyomiPage::cancelSearch);
    row->addWidget(m_cancelBtn);

    m_refreshBtn = new QPushButton("Refresh");
    m_refreshBtn->setFixedHeight(36);
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
    m_sortCombo->setFixedHeight(36);
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
    m_viewToggleBtn->setFixedHeight(36);
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
    m_pauseBtn->setFixedHeight(36);
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
    m_moreBtn = new QPushButton;
    m_moreBtn->setIcon(QIcon(QStringLiteral(":/icons/kebab-menu.svg")));
    m_moreBtn->setIconSize(QSize(16, 16));
    m_moreBtn->setFixedSize(36, 36);
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
    row->setSpacing(10);

    m_searchStatus = new QLabel("Ready");
    m_searchStatus->setStyleSheet("color: #a1a1aa; font-size: 13px;");
    row->addWidget(m_searchStatus, 2);

    m_downloadStatus = new QLabel("Active: 0 | History: 0");
    m_downloadStatus->setStyleSheet("color: #a1a1aa; font-size: 13px;");
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
            "#TankoyomiEmptyState { color: #a1a1aa; font-size: 15px; }");
        v->addWidget(m_emptyLabel);

        auto* btnRow = new QHBoxLayout;
        btnRow->setSpacing(8);
        btnRow->setAlignment(Qt::AlignCenter);

        m_emptyRetryBtn = new QPushButton("Retry");
        m_emptyRetryBtn->setFixedHeight(32);
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
        m_emptyClearBtn->setFixedHeight(32);
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
        m_loadingLabel->setStyleSheet("color: #cbd5e1; font-size: 15px;");
        v->addWidget(m_loadingLabel);

        auto* bar = new QProgressBar;
        bar->setRange(0, 0);                  // indeterminate
        bar->setTextVisible(false);
        bar->setFixedWidth(220);
        bar->setFixedHeight(4);
        {
            const QColor accent = QApplication::palette().color(QPalette::Highlight);
            bar->setStyleSheet(QStringLiteral(
                "QProgressBar { background: rgba(255,255,255,0.08); border: none; "
                "  border-radius: 2px; }"
                "QProgressBar::chunk { background: %1; border-radius: 2px; }")
                .arg(accent.name()));
        }
        v->addWidget(bar, 0, Qt::AlignCenter);
    }

    m_searchResultsStack = new QStackedWidget;
    m_searchResultsStack->addWidget(m_resultsTable);  // index 0: list
    m_searchResultsStack->addWidget(m_resultsGrid);   // index 1: grid
    m_searchResultsStack->addWidget(m_emptyPage);     // index 2: empty state
    m_searchResultsStack->addWidget(m_loadingPage);   // index 3: loading

    const QString savedMode = QSettings().value("tankoyomi/resultsView", "grid").toString();
    m_preferredDataView = (savedMode == "list") ? 0 : 1;
    // Start on empty state until a search runs.
    m_searchResultsStack->setCurrentIndex(2);

    // Mihon-overhaul C.5 — inner stack wraps search results + detail view.
    //   index 0 = m_searchResultsStack (list/grid/empty/loading)
    //   index 1 = m_detailView (MangaDetailView)
    m_resultsInnerStack = new QStackedWidget;
    m_resultsInnerStack->addWidget(m_searchResultsStack);

    m_detailView = new MangaDetailView(this);
    m_detailView->setDownloader(m_downloader);
    m_detailView->setBridgeDestinationProvider([this]() {
        const QStringList roots = m_bridge->rootFolders("comics");
        return roots.isEmpty() ? QString() : roots.first();
    });
    connect(m_detailView, &MangaDetailView::backRequested,
            this, [this]() { m_resultsInnerStack->setCurrentIndex(0); });
    m_resultsInnerStack->addWidget(m_detailView);

    m_tabWidget->addTab(m_resultsInnerStack, "Search Results");

    // Mihon-overhaul E.4 — Transfers tab body: top status row above the
    // existing table. A container widget is needed because addTab() takes a
    // single widget; we host the status row + table in a QVBoxLayout.
    auto* transfersTabBody = new QWidget;
    transfersTabBody->setObjectName("TransfersTabBody");
    auto* transfersTabLayout = new QVBoxLayout(transfersTabBody);
    transfersTabLayout->setContentsMargins(0, 0, 0, 0);
    transfersTabLayout->setSpacing(8);

    // Mihon-overhaul E.4 — global Pause-all / Resume-all / Cancel-all
    // controls. Status-line dropped 2026-05-13 per Hemanth smoke feedback
    // ("the 0 downloaded 0 queded etc is very poorly placed and not
    // necessary") — redundant with search-row Active|History counter +
    // per-card "Completed · X of Y chapters" labels.
    auto* btnRow = new QHBoxLayout();
    m_transfersPauseAll = new QPushButton(this);
    m_transfersPauseAll->setObjectName("TransfersPauseAll");
    m_transfersPauseAll->setIcon(QIcon(QStringLiteral(":/icons/pause-circle.svg")));
    m_transfersPauseAll->setIconSize(QSize(18, 18));
    m_transfersPauseAll->setFlat(true);
    m_transfersPauseAll->setToolTip(tr("Pause all"));

    m_transfersResumeAll = new QPushButton(this);
    m_transfersResumeAll->setObjectName("TransfersResumeAll");
    m_transfersResumeAll->setIcon(QIcon(QStringLiteral(":/icons/play-circle.svg")));
    m_transfersResumeAll->setIconSize(QSize(18, 18));
    m_transfersResumeAll->setFlat(true);
    m_transfersResumeAll->setToolTip(tr("Resume all"));

    m_transfersCancelAll = new QPushButton(this);
    m_transfersCancelAll->setObjectName("TransfersCancelAll");
    m_transfersCancelAll->setIcon(QIcon(QStringLiteral(":/icons/close-x.svg")));
    m_transfersCancelAll->setIconSize(QSize(18, 18));
    m_transfersCancelAll->setFlat(true);
    m_transfersCancelAll->setToolTip(tr("Cancel all"));

    btnRow->addWidget(m_transfersPauseAll);
    btnRow->addWidget(m_transfersResumeAll);
    btnRow->addWidget(m_transfersCancelAll);
    btnRow->addStretch();

    connect(m_transfersPauseAll, &QPushButton::clicked, this, [this]() {
        if (m_downloader) m_downloader->pauseAll();
    });
    connect(m_transfersResumeAll, &QPushButton::clicked, this, [this]() {
        if (m_downloader) m_downloader->resumeAll();
    });
    connect(m_transfersCancelAll, &QPushButton::clicked, this, [this]() {
        const auto ans = QMessageBox::question(this, tr("Cancel all?"),
            tr("Cancel all queued and downloading chapters across every series?"),
            QMessageBox::Yes | QMessageBox::No);
        if (ans == QMessageBox::Yes && m_downloader) m_downloader->cancelAll();
    });

    transfersTabLayout->addLayout(btnRow);
    // Mihon-overhaul E.5 — flat QTableWidget replaced with vertical card list.
    transfersTabLayout->addWidget(createTransfersList(), 1);

    m_tabWidget->addTab(transfersTabBody, "Transfers");

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
    table->verticalHeader()->setDefaultSectionSize(32);
    table->setContextMenuPolicy(Qt::CustomContextMenu);

    QStringList headers = { "Title", "Author", "Source", "Status", "Type" };
    table->setHorizontalHeaderLabels(headers);

    // T18 — match header alignment to cell alignment per spec CR.9.
    // All 5 result columns are text fields; default left+vcenter on both.
    auto *hdr = table->horizontalHeader();
    hdr->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
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
        "#MangaResultsTable { border: none; outline: none; font-size: 13px; }"
        "#MangaResultsTable::item { padding: 0 8px; }"
        "#MangaResultsTable::item:hover { background: rgba(255,255,255,0.04); }"
        "#MangaResultsTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
        "#MangaResultsTable QHeaderView::section {"
        "  background: #1a1a1a; color: #888; border: none;"
        "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
        "  padding: 6px 8px; font-size: 11px; font-weight: 600; }"
    ));

    return table;
}

// Mihon-overhaul E.5 — Transfers tab vertical scroll list of TransferGroupCard
// widgets. Replaces the flat 4-column QTableWidget. Each card is one active
// series; refreshTransfers() inserts / updates / removes cards in place.
QWidget* TankoyomiPage::createTransfersList()
{
    m_transfersScroll = new QScrollArea(this);
    m_transfersScroll->setObjectName("TransfersScroll");
    m_transfersScroll->setWidgetResizable(true);

    m_transfersContainer = new QWidget(m_transfersScroll);
    m_transfersContainer->setObjectName("TransfersContainer");
    m_transfersCardList = new QVBoxLayout(m_transfersContainer);
    m_transfersCardList->setContentsMargins(8, 8, 8, 8);
    m_transfersCardList->setSpacing(8);
    m_transfersCardList->addStretch();  // trailing spacer so cards stack from top

    m_transfersScroll->setWidget(m_transfersContainer);
    return m_transfersScroll;
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
        // T18 — explicit alignment on Title column per spec CR.9.
        titleItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
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
    if (!m_searchResultsStack) return;

    // B5: a search is in flight — show loading page regardless of stale results.
    if (m_pendingSearches > 0) {
        m_loadingLabel->setText(m_lastQuery.isEmpty()
            ? "Searching..."
            : QString("Searching for \"%1\"...").arg(m_lastQuery));
        m_searchResultsStack->setCurrentIndex(3);
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
        m_searchResultsStack->setCurrentIndex(2);
    } else {
        m_searchResultsStack->setCurrentIndex(m_preferredDataView);
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

// ── Result double-click → embedded detail screen ────────────────────────────
void TankoyomiPage::onResultDoubleClicked(int row)
{
    if (row < 0 || row >= m_displayedResults.size()) return;
    const auto& result = m_displayedResults[row];

    // Find the right scraper for this source
    MangaScraper* scraper = nullptr;
    for (auto* s : m_scrapers) {
        if (s->sourceId() == result.source) { scraper = s; break; }
    }
    if (!scraper) return;

    // Mihon-overhaul C.5 — embedded detail screen replaces AddMangaDialog
    m_detailView->setScraper(scraper);
    const QString coverPath = result.thumbnailUrl.isEmpty()
        ? QString()
        : ensureCover(result.source, result.id, result.thumbnailUrl);
    m_detailView->show(result, coverPath);
    m_resultsInnerStack->setCurrentIndex(1);
}

// ── E2: shared results context menu (table + grid) ──────────────────────────
void TankoyomiPage::showResultContextMenu(int row, const QPoint& globalPos)
{
    if (row < 0 || row >= m_displayedResults.size()) return;
    const auto& result = m_displayedResults[row];

    QMenu menu(this);

    menu.addAction(tr("Open detail screen"), this, [this, row]() {
        onResultDoubleClicked(row);
    });

    menu.addAction(tr("Quick add all chapters"), this, [this, result]() {
        // Find the scraper
        MangaScraper* scraper = nullptr;
        for (auto* s : m_scrapers) {
            if (s->sourceId() == result.source) { scraper = s; break; }
        }
        if (!scraper || !m_downloader) return;

        // One-shot connect to chaptersReady
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(scraper, &MangaScraper::chaptersReady, this,
            [this, conn, result](const QList<ChapterInfo>& chapters) {
                disconnect(*conn);
                if (chapters.isEmpty()) return;
                const QStringList roots = m_bridge->rootFolders("comics");
                const QString dest = roots.isEmpty() ? QString() : roots.first();
                if (dest.isEmpty()) return;
                m_downloader->startDownload(result.title, result.source,
                    chapters, dest, "cbz");
                m_tabWidget->setCurrentIndex(1);   // jump to Transfers
            });
        scraper->fetchChapters(result.id);
    });

    menu.addSeparator();

    menu.addAction(tr("Open source page in browser"), this, [result]() {
        if (!result.url.isEmpty())
            QDesktopServices::openUrl(QUrl(result.url));
    });

    // Show in library folder — only enabled if any chapter of this series is on disk
    const int downloaded = m_downloader
        ? m_downloader->countDownloadedForSeries(result.title, result.source)
        : 0;
    auto* showFolderAct = menu.addAction(tr("Show in library folder"),
        this, [this, result]() {
            const auto records = m_downloader->listActive();
            for (const auto& rec : records) {
                if (rec.seriesTitle == result.title &&
                    rec.source == result.source &&
                    !rec.destinationPath.isEmpty()) {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(
                        rec.destinationPath + "/" + rec.seriesTitle));
                    return;
                }
            }
        });
    showFolderAct->setEnabled(downloaded > 0);

    menu.addSeparator();

    menu.addAction(tr("Copy title"), this, [result]() {
        QGuiApplication::clipboard()->setText(result.title);
    });
    menu.addAction(tr("Copy source URL"), this, [result]() {
        QGuiApplication::clipboard()->setText(result.url);
    });

    menu.exec(globalPos);
}

// ── Transfers refresh ───────────────────────────────────────────────────────
// Mihon-overhaul E.5 — drives the vertical card list. Walks the downloader's
// listActive() and reconciles m_transfersCardsById: remove cards whose series
// vanished, create+wire new cards for new series, otherwise just push the
// fresh record into the existing card so it refreshes its inner state.
void TankoyomiPage::refreshTransfers()
{
    if (!m_downloader || !m_transfersCardList) return;

    const auto records = m_downloader->listActive();
    QSet<QString> liveIds;
    for (const auto& rec : records) liveIds.insert(rec.id);

    // Remove cards whose series no longer exists
    for (auto it = m_transfersCardsById.begin();
         it != m_transfersCardsById.end(); ) {
        if (!liveIds.contains(it.key())) {
            it.value()->deleteLater();
            it = m_transfersCardsById.erase(it);
        } else {
            ++it;
        }
    }

    // Insert / refresh cards for live records
    int insertPos = 0;
    int activeCount = 0;
    int pendingChapters = 0;
    int pendingSeries   = 0;
    for (const auto& rec : records) {
        TransferGroupCard* card = m_transfersCardsById.value(rec.id, nullptr);
        if (!card) {
            card = new TransferGroupCard(m_downloader, m_transfersContainer);
            connect(card, &TransferGroupCard::cancelSeriesRequested,
                    this, [this](const QString& id) {
                        const auto ans = QMessageBox::question(this,
                            tr("Cancel series?"),
                            tr("Cancel this series's queued / downloading chapters?"),
                            QMessageBox::Yes | QMessageBox::No);
                        if (ans == QMessageBox::Yes)
                            m_downloader->cancelDownload(id);
                    });
            connect(card, &TransferGroupCard::contextMenuRequested,
                    this, &TankoyomiPage::showTransferCardContextMenu);
            m_transfersCardList->insertWidget(insertPos, card);
            m_transfersCardsById.insert(rec.id, card);
        }
        card->setRecord(rec);
        ++insertPos;

        if (rec.status == "downloading") ++activeCount;

        // A5: tally pending chapters / series for badge + global-controls toggles.
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

    const auto history = m_downloader->listHistory();
    m_downloadStatus->setText(QString("Active: %1 | History: %2")
                                  .arg(activeCount).arg(history.size()));

    const bool hasPendingWork = pendingChapters > 0;

    // Tab badge — show total active records (including finished/errored ones the
    // user hasn't dismissed), plus a pending-chapter count when work remains.
    QString tabLabel;
    if (records.isEmpty()) {
        tabLabel = "Transfers";
    } else if (pendingChapters > 0) {
        tabLabel = QString("Transfers · %1 series · %2 chapters pending")
                       .arg(pendingSeries)
                       .arg(pendingChapters);
    } else {
        tabLabel = QString("Transfers (%1)").arg(records.size());
    }
    m_tabWidget->setTabText(1, tabLabel);

    // A2/A3: Pause and overflow visibility piggyback on pending work.
    m_pauseBtn->setVisible(hasPendingWork);
    m_pauseBtn->setText(m_downloader->isPaused() ? "Resume Downloads" : "Pause Downloads");
    m_moreBtn->setVisible(hasPendingWork);

    // E.4 top status line removed 2026-05-13 (Hemanth smoke feedback —
    // redundant with the search-row Active|History counter + per-card
    // "Completed · X of Y chapters" labels). countByState() remains in
    // the MangaDownloader API for any future caller.
}

// Mihon-overhaul F.1 — full Tankorent-parity menu vocabulary on the
// TransferGroupCard right-click. Replaces E.5's empty stub.
void TankoyomiPage::showTransferCardContextMenu(const QPoint& globalPos,
                                                 const QString& seriesId)
{
    if (!m_downloader || seriesId.isEmpty()) return;

    QMenu menu(this);

    const bool paused = m_downloader->isSeriesPaused(seriesId);
    if (paused) {
        menu.addAction(tr("Resume series"), this, [this, seriesId]() {
            m_downloader->resumeSeries(seriesId);
        });
    } else {
        menu.addAction(tr("Pause series"), this, [this, seriesId]() {
            m_downloader->pauseSeries(seriesId);
        });
    }

    menu.addAction(tr("Restart series"), this, [this, seriesId]() {
        m_downloader->restartSeries(seriesId);
    });

    menu.addSeparator();

    menu.addAction(tr("Show in folder"), this, [this, seriesId]() {
        // Find the record's destinationPath
        const auto records = m_downloader->listActive();
        for (const auto& rec : records) {
            if (rec.id != seriesId) continue;
            if (!rec.destinationPath.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(rec.destinationPath));
            }
            break;
        }
    });

    // Retry failed — only if any chapter is "error"
    const auto records = m_downloader->listActive();
    int errorCount = 0;
    for (const auto& rec : records) {
        if (rec.id != seriesId) continue;
        for (const auto& ch : rec.chapters) {
            if (ch.status == "error") ++errorCount;
        }
        break;
    }
    if (errorCount > 0) {
        menu.addAction(tr("Retry failed chapters (%1)").arg(errorCount),
            this, [this, seriesId]() {
                m_downloader->retryFailedChapters(seriesId);
            });
    }

    menu.addSeparator();

    menu.addAction(tr("Move to top"), this, [this, seriesId]() {
        m_downloader->moveSeriesToTop(seriesId);
        refreshTransfers();
    });
    menu.addAction(tr("Move to bottom"), this, [this, seriesId]() {
        m_downloader->moveSeriesToBottom(seriesId);
        refreshTransfers();
    });

    auto* sortMenu = menu.addMenu(tr("Sort chapters by"));
    sortMenu->addAction(tr("Chapter number ascending"), this, [this, seriesId]() {
        m_downloader->reorderChapters(seriesId, "chapter_number", true);
    });
    sortMenu->addAction(tr("Chapter number descending"), this, [this, seriesId]() {
        m_downloader->reorderChapters(seriesId, "chapter_number", false);
    });
    sortMenu->addAction(tr("Date ascending"), this, [this, seriesId]() {
        m_downloader->reorderChapters(seriesId, "date", true);
    });
    sortMenu->addAction(tr("Date descending"), this, [this, seriesId]() {
        m_downloader->reorderChapters(seriesId, "date", false);
    });

    menu.addSeparator();

    QString seriesTitle;
    for (const auto& rec : records) {
        if (rec.id == seriesId) { seriesTitle = rec.seriesTitle; break; }
    }
    menu.addAction(tr("Copy series title"), this, [seriesTitle]() {
        QGuiApplication::clipboard()->setText(seriesTitle);
    });

    menu.addSeparator();

    menu.addAction(tr("Cancel series"), this, [this, seriesId]() {
        const auto ans = QMessageBox::question(this, tr("Cancel series?"),
            tr("Cancel this series's queued / downloading chapters?\n"
               "Already-downloaded files stay on disk."),
            QMessageBox::Yes | QMessageBox::No);
        if (ans == QMessageBox::Yes) {
            m_downloader->cancelDownload(seriesId);
            refreshTransfers();
        }
    });

    menu.addAction(tr("Cancel + Delete files"), this, [this, seriesId]() {
        const auto ans = QMessageBox::question(this, tr("Delete files?"),
            tr("Cancel this series AND delete all of its downloaded files from disk?"),
            QMessageBox::Yes | QMessageBox::No);
        if (ans == QMessageBox::Yes) {
            m_downloader->removeWithData(seriesId);
            refreshTransfers();
        }
    });

    menu.exec(globalPos);
}
