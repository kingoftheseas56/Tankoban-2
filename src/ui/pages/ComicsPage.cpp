#include "ComicsPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "SeriesView.h"
#include "core/CoreBridge.h"
#include "core/LibraryScanner.h"
#include "core/ScannerUtils.h"

#include "ui/ContextMenuHelper.h"
#include "ui/widgets/FadingStackedWidget.h"
#include "ui/widgets/LibraryListView.h"
#include <QPushButton>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QMetaObject>
#include <QSettings>
#include <QInputDialog>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QJsonObject>
#include <QFileInfo>
#include <QCollator>
#include <QShortcut>
#include <QMessageBox>

// P4-3: COMIC_EXTS covers all reader-supported archive formats. Engine
// (ArchiveReader) and library scanner both handle CBZ + CBR + RAR.
static const QStringList COMIC_EXTS = {"*.cbz", "*.cbr", "*.rar"};

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

    m_stack = new FadingStackedWidget(this);

    // ── Grid view (index 0) — wrapped in scroll area ──
    auto* gridScroll = new QScrollArea();
    gridScroll->setObjectName("ComicsGridScroll");
    gridScroll->setFrameShape(QFrame::NoFrame);
    gridScroll->setWidgetResizable(true);
    gridScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    gridScroll->setStyleSheet("QScrollArea#ComicsGridScroll { background: transparent; border: none; }");

    auto* gridPage = new QWidget();
    gridPage->setObjectName("ComicsGridPage");
    gridPage->setStyleSheet("QWidget#ComicsGridPage { background: transparent; }");
    auto* gridLayout = new QVBoxLayout(gridPage);
    gridLayout->setContentsMargins(20, 0, 20, 20);
    gridLayout->setSpacing(24);

    // ── 1. Search bar (full width, top) ──
    m_searchBar = new QLineEdit(gridPage);
    m_searchBar->setPlaceholderText("Search series and volumes\u2026");
    m_searchBar->setClearButtonEnabled(true);
    m_searchBar->setObjectName("LibrarySearch");
    m_searchBar->setFixedHeight(36);
    m_searchBar->setStyleSheet(
        "QLineEdit#LibrarySearch { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px; color: #eee; padding: 4px 10px; font-size: 13px; }"
        "QLineEdit#LibrarySearch:focus { border: 1px solid rgba(255,255,255,0.3); }");
    auto* searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0, 12, 0, 0);
    searchLayout->addWidget(m_searchBar);
    gridLayout->addLayout(searchLayout);

    m_searchBar->setToolTip("Separate words to match all\n"
                            "(e.g. 'one piece' matches series or volumes containing both words)");

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(250);
    connect(m_searchBar, &QLineEdit::textChanged, this, [this]() {
        m_searchTimer->start();
        m_searchBar->setProperty("activeSearch", !m_searchBar->text().trimmed().isEmpty());
        m_searchBar->style()->unpolish(m_searchBar);
        m_searchBar->style()->polish(m_searchBar);
    });
    connect(m_searchTimer, &QTimer::timeout, this, &ComicsPage::applySearch);

    // Ctrl+F focuses search bar
    auto* searchShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        m_searchBar->setFocus();
        m_searchBar->selectAll();
    });

    // Escape: clear search if active, else navigate back to grid
    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (!m_searchBar->text().trimmed().isEmpty()) {
            m_searchBar->clear();
        } else if (m_stack->currentIndex() != 0) {
            showGrid();
        }
    });

    // F5: trigger rescan
    auto* f5Shortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(f5Shortcut, &QShortcut::activated, this, &ComicsPage::triggerScan);

    // ── 2. Continue Reading section ──
    m_continueSection = new QWidget(gridPage);
    auto* continueLayout = new QVBoxLayout(m_continueSection);
    continueLayout->setContentsMargins(0, 0, 0, 0);
    continueLayout->setSpacing(4);
    auto* continueLabel = new QLabel("CONTINUE READING", m_continueSection);
    continueLabel->setObjectName("LibraryHeading");
    continueLayout->addWidget(continueLabel);
    m_continueStrip = new TileStrip(m_continueSection);
    m_continueStrip->setMode("continue");
    continueLayout->addWidget(m_continueStrip);
    m_continueSection->hide();
    gridLayout->addWidget(m_continueSection);

    // Continue strip context menu
    m_continueStrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_continueStrip, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* card = m_continueStrip->tileAt(pos);
        if (!card) return;

        QString filePath    = card->property("filePath").toString();
        QString seriesPath  = card->property("seriesPath").toString();
        QString seriesName  = card->property("seriesName").toString();

        // Compute progress key for this file
        QString progKey = QString(QCryptographicHash::hash(
            filePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));

        // Check finished state for toggle label
        bool isFinished = false;
        if (m_bridge) {
            QJsonObject prog = m_bridge->progress("comics", progKey);
            isFinished = prog.value("finished").toBool();
        }

        auto* menu = ContextMenuHelper::createMenu(this);

        // 1. Continue reading
        auto* continueAct = menu->addAction("Continue reading");

        // 2. Open series (visible only if seriesPath exists)
        QAction* openSeriesAct = nullptr;
        if (!seriesPath.isEmpty()) {
            openSeriesAct = menu->addAction("Open series");
        }

        menu->addSeparator();

        // 3. Mark as unread / Mark as read
        auto* markAct = menu->addAction(isFinished ? "Mark as unread" : "Mark as read");

        // 4. Clear from Continue Reading
        auto* clearAct = menu->addAction("Clear from Continue Reading");

        menu->addSeparator();

        // 5. Reveal in File Explorer
        auto* revealAct = menu->addAction("Reveal in File Explorer");
        revealAct->setEnabled(!filePath.isEmpty());

        // 6. Copy path
        auto* copyAct = menu->addAction("Copy path");
        copyAct->setEnabled(!filePath.isEmpty());

        menu->addSeparator();

        // 7. Remove from library... (DANGER)
        auto* removeAct = ContextMenuHelper::addDangerAction(menu, "Remove from library...");
        removeAct->setEnabled(!seriesPath.isEmpty());

        auto* chosen = menu->exec(m_continueStrip->mapToGlobal(pos));
        if (chosen == continueAct) {
            // Open the comic file directly
            QDir dir(seriesPath);
            QStringList files = dir.entryList(COMIC_EXTS, QDir::Files);
            QCollator col;
            col.setNumericMode(true);
            std::sort(files.begin(), files.end(), [&col](const QString& a, const QString& b) {
                return col.compare(a, b) < 0;
            });
            QStringList cbzList;
            for (const auto& f : files)
                cbzList.append(dir.absoluteFilePath(f));
            emit openComic(filePath, cbzList, seriesName);
        } else if (openSeriesAct && chosen == openSeriesAct) {
            m_seriesView->showSeries(seriesPath, seriesName,
                                     card->property("coverPath").toString());
            m_stack->setCurrentIndex(1);
        } else if (chosen == markAct && m_bridge) {
            QJsonObject prog = m_bridge->progress("comics", progKey);
            prog["finished"] = !isFinished;
            m_bridge->saveProgress("comics", progKey, prog);
            refreshContinueStrip();
        } else if (chosen == clearAct && m_bridge) {
            m_bridge->clearProgress("comics", progKey);
            refreshContinueStrip();
        } else if (chosen == revealAct) {
            ContextMenuHelper::revealInExplorer(filePath);
        } else if (chosen == copyAct) {
            ContextMenuHelper::copyToClipboard(filePath);
        } else if (chosen == removeAct) {
            if (ContextMenuHelper::confirmRemove(this, "Remove from library",
                    "Remove this series from the library?\n" + seriesPath +
                    "\nFiles will not be deleted from disk.")) {
                triggerScan();
            }
        }
        menu->deleteLater();
    });

    // ── 3. "SERIES" header row: label + sort + density ──
    auto* seriesRow = new QWidget(gridPage);
    auto* seriesLayout = new QHBoxLayout(seriesRow);
    seriesLayout->setContentsMargins(0, 0, 0, 0);
    seriesLayout->setSpacing(8);

    auto* seriesLabel = new QLabel("SERIES", seriesRow);
    seriesLabel->setObjectName("LibraryHeading");
    seriesLayout->addWidget(seriesLabel);
    seriesLayout->addStretch();

    m_sortCombo = new QComboBox(seriesRow);
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
    seriesLayout->addWidget(m_sortCombo);

    auto* densitySmall = new QLabel("A", seriesRow);
    densitySmall->setObjectName("DensityLabelSmall");
    seriesLayout->addWidget(densitySmall);

    m_densitySlider = new QSlider(Qt::Horizontal, seriesRow);
    m_densitySlider->setRange(0, 2);
    m_densitySlider->setFixedWidth(100);
    m_densitySlider->setFixedHeight(20);
    int savedDensity = QSettings("Tankoban", "Tankoban").value("grid_cover_size", 1).toInt();
    m_densitySlider->setValue(qBound(0, savedDensity, 2));
    connect(m_densitySlider, &QSlider::valueChanged, this, [this](int val) {
        QSettings("Tankoban", "Tankoban").setValue("grid_cover_size", val);
        m_tileStrip->setDensity(val);
        if (m_continueStrip) m_continueStrip->setDensity(val);
    });
    seriesLayout->addWidget(m_densitySlider);

    auto* densityLarge = new QLabel("A", seriesRow);
    densityLarge->setObjectName("DensityLabelLarge");
    seriesLayout->addWidget(densityLarge);

    // View toggle button (grid/list)
    m_viewToggle = new QPushButton(seriesRow);
    m_viewToggle->setObjectName("ViewToggle");
    m_viewToggle->setFixedSize(28, 28);
    m_viewToggle->setText("\u2630"); // hamburger icon
    m_viewToggle->setCursor(Qt::PointingHandCursor);
    // Styling lives in Theme.cpp QSS template under QPushButton#ViewToggle —
    // theme-bound (Light/Dark parity).
    connect(m_viewToggle, &QPushButton::clicked, this, &ComicsPage::toggleViewMode);
    seriesLayout->addWidget(m_viewToggle);

    gridLayout->addWidget(seriesRow);

    m_statusLabel = new QLabel("Add a comics folder to get started", gridPage);
    m_statusLabel->setObjectName("TileSubtitle");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 60px;");
    gridLayout->addWidget(m_statusLabel);

    m_tileStrip = new TileStrip(gridPage);
    m_tileStrip->hide();
    m_tileStrip->setDensity(savedDensity);
    if (m_continueStrip) m_continueStrip->setDensity(savedDensity);
    gridLayout->addWidget(m_tileStrip);

    // List view (hidden by default — V-key toggles)
    m_listView = new LibraryListView(gridPage);
    m_listView->hide();
    connect(m_listView, &LibraryListView::itemActivated, this, [this](const QString& path) {
        // Find series name from path
        QDir dir(path);
        QString name = dir.dirName();
        m_seriesView->showSeries(path, name);
        m_stack->setCurrentIndexAnimated(1);
    });
    gridLayout->addWidget(m_listView, 1);

    // Right-click on tiles (selection-aware via TileStrip signal)
    connect(m_tileStrip, &TileStrip::tileRightClicked, this, [this](TileCard* card, const QPoint& globalPos) {
        QList<TileCard*> sel = m_tileStrip->selectedTiles();
        if (sel.size() > 1) {
            // ── Multi-select context menu ──
            onMultiSelectContextMenu(sel, globalPos);
        } else {
            // ── Single-select context menu (existing logic) ──
            QPoint localPos = m_tileStrip->mapFromGlobal(globalPos);
            onTileContextMenu(localPos);
        }
    });

    // Double-click opens SeriesView
    connect(m_tileStrip, &TileStrip::tileDoubleClicked, this, [this](TileCard* card) {
        if (!card) return;
        m_seriesView->showSeries(card->property("seriesPath").toString(),
                                 card->property("seriesName").toString(),
                                 card->property("coverPath").toString());
        m_stack->setCurrentIndexAnimated(1);
    });

    // Ctrl+A select all tiles
    auto* selectAllShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_A), this);
    connect(selectAllShortcut, &QShortcut::activated, this, [this]() {
        if (m_stack->currentIndex() == 0)
            m_tileStrip->selectAll();
    });

    // V-key: toggle grid/list view
    auto* viewToggleShortcut = new QShortcut(QKeySequence(Qt::Key_V), this);
    connect(viewToggleShortcut, &QShortcut::activated, this, [this]() {
        if (m_stack->currentIndex() == 0 && !m_searchBar->hasFocus())
            toggleViewMode();
    });

    // Restore persisted view mode
    m_gridMode = QSettings("Tankoban", "Tankoban").value("library_view_mode_comics", "grid").toString() == "grid";
    if (!m_gridMode) toggleViewMode();

    gridLayout->addStretch();
    gridScroll->setWidget(gridPage);
    m_stack->addWidget(gridScroll);

    // ── Series view (index 1) ──
    m_seriesView = new SeriesView(m_bridge);
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
        m_listView->clear();
        m_progressKeyMap.clear();
        m_tileStrip->hide();
        m_statusLabel->setText("Add a comics folder to get started");
        m_statusLabel->show();
        m_hasScanned = true;
        m_scanning = false;
        return;
    }

    if (!m_hasScanned) {
        // First scan: clear tiles, show scanning label for progressive loading
        m_tileStrip->clear();
        m_listView->clear();
        m_progressKeyMap.clear();
        m_statusLabel->setText("Scanning...");
        m_statusLabel->show();
        m_tileStrip->hide();
    }
    // Rescan: keep old tiles visible — atomic swap happens in onScanFinished

    QMetaObject::invokeMethod(m_scanner, "scan", Qt::QueuedConnection,
                              Q_ARG(QStringList, roots));
}

void ComicsPage::addSeriesTile(const SeriesInfo& series)
{
    // Build progress key map for continue strip (with per-file cover paths)
    QString thumbsDir = m_bridge->dataDir() + "/thumbs";
    QDir dir(series.seriesPath);
    for (const auto& f : dir.entryList(COMIC_EXTS, QDir::Files)) {
        QString fullPath = dir.absoluteFilePath(f);
        QString progressKey = QString(QCryptographicHash::hash(
            fullPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        QFileInfo fi(fullPath);
        QString fileKey = fullPath + "::" + QString::number(fi.size())
                        + "::" + QString::number(fi.lastModified().toMSecsSinceEpoch());
        QString fileHash = QString(QCryptographicHash::hash(
            fileKey.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        QString fileCover = thumbsDir + "/" + fileHash + ".jpg";
        QString coverPath = QFile::exists(fileCover) ? fileCover : series.coverThumbPath;
        m_progressKeyMap[progressKey] = {fullPath, series.seriesPath, coverPath};
    }

    QString subtitle = QString::number(series.fileCount)
                     + (series.fileCount == 1 ? " issue" : " issues");

    auto* card = new TileCard(series.coverThumbPath, series.seriesName, subtitle);

    card->setProperty("seriesPath", series.seriesPath);
    card->setProperty("seriesName", series.seriesName);
    card->setProperty("coverPath", series.coverThumbPath);
    card->setProperty("fileCount", series.fileCount);
    card->setProperty("newestMtime", series.newestMtimeMs);
    connect(card, &TileCard::clicked, this, &ComicsPage::onCardClicked);

    card->setIsFolder(true);
    {
        QJsonObject allProg = m_bridge->allProgress("comics");
        int totalPages = 0, readPages = 0;
        bool anyInProgress = false;
        bool allFinished = !series.files.isEmpty();
        bool anyNew = false;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 sevenDaysMs = 7LL * 24 * 60 * 60 * 1000;

        for (const auto& fe : series.files) {
            QString pk = QString(QCryptographicHash::hash(
                fe.path.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            QJsonObject prog = allProg.value(pk).toObject();
            bool finished = prog.value("finished").toBool();
            int page = prog.value("page").toInt(-1);
            int pc = fe.pageCount > 0 ? fe.pageCount : prog.value("pageCount").toInt(0);
            totalPages += pc;
            if (finished) {
                readPages += pc;
            } else if (page >= 0 && pc > 0) {
                readPages += page + 1;
                anyInProgress = true;
                allFinished = false;
            } else {
                allFinished = false;
            }
            if (fe.mtimeMs > 0 && (now - fe.mtimeMs) < sevenDaysMs)
                anyNew = true;
        }

        double fraction = totalPages > 0 ? static_cast<double>(readPages) / totalPages : 0.0;
        QString status = allFinished ? "finished" : (anyInProgress ? "reading" : "");
        // Dropped countBadge — the "N issues" subtitle already conveys the count
        // and the pill rendered as text "bleeding into" the thumbnail (2026-04-15 Hemanth).
        card->setBadges(fraction, QString(), QString(), status);
        card->setIsNew(anyNew);
    }

    m_tileStrip->addTile(card);

    LibraryListView::ItemData listItem;
    listItem.name = series.seriesName;
    listItem.path = series.seriesPath;
    listItem.itemCount = series.fileCount;
    listItem.lastModifiedMs = series.newestMtimeMs;
    m_listView->addItem(listItem);
}

void ComicsPage::onSeriesFound(const SeriesInfo& series)
{
    // On rescan: skip incremental tiles — atomic rebuild in onScanFinished
    if (m_hasScanned) return;

    // First scan: progressive loading
    if (m_statusLabel->isVisible()) {
        m_statusLabel->hide();
        m_tileStrip->show();
    }
    addSeriesTile(series);
}

void ComicsPage::onScanFinished(const QList<SeriesInfo>& allSeries)
{
    bool wasRescan = m_hasScanned;
    m_hasScanned = true;
    m_scanning = false;

    if (wasRescan) {
        // Atomic swap: clear old tiles, rebuild from complete list
        m_tileStrip->clear();
        m_listView->clear();
        m_progressKeyMap.clear();

        for (const auto& series : allSeries)
            addSeriesTile(series);
    }

    if (allSeries.isEmpty()) {
        m_tileStrip->hide();
        m_statusLabel->setObjectName("LibraryEmptyLabel");
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setText("No comics found\nAdd a root folder via the + button or browse Sources for content");
        m_statusLabel->show();
    } else {
        m_statusLabel->hide();
        m_tileStrip->show();
        m_tileStrip->sortTiles(m_sortCombo->currentData().toString());
    }

    refreshContinueStrip();
}

void ComicsPage::onTileClicked(const QString& seriesPath, const QString& seriesName)
{
    m_seriesView->showSeries(seriesPath, seriesName);
    m_stack->setCurrentIndexAnimated(1);
}

void ComicsPage::showGrid()
{
    m_stack->setCurrentIndexAnimated(0);
}

void ComicsPage::toggleViewMode()
{
    m_gridMode = !m_gridMode;
    QSettings("Tankoban", "Tankoban").setValue("library_view_mode_comics",
                                                m_gridMode ? "grid" : "list");
    if (m_gridMode) {
        m_listView->hide();
        m_tileStrip->show();
        m_densitySlider->show();
        m_viewToggle->setText("\u2630"); // hamburger
    } else {
        m_tileStrip->hide();
        m_listView->show();
        m_densitySlider->hide();
        m_viewToggle->setText("\u2637"); // dotted square
    }
}

void ComicsPage::applySearch()
{
    QString query = m_searchBar->text();
    m_tileStrip->filterTiles(query);
    m_listView->setTextFilter(query);

    if (m_tileStrip->visibleCount() == 0 && !query.trimmed().isEmpty()) {
        m_statusLabel->setObjectName("LibraryEmptyLabel");
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setText(
            QString("No results for \"%1\"").arg(query.trimmed()));
        m_statusLabel->show();
        m_tileStrip->hide();
    } else if (m_tileStrip->visibleCount() > 0) {
        m_statusLabel->hide();
        m_tileStrip->show();
    }
}

void ComicsPage::refreshContinueStrip()
{
    m_continueStrip->clear();

    QJsonObject allProg = m_bridge->allProgress("comics");
    if (allProg.isEmpty()) {
        m_continueSection->hide();
        return;
    }

    struct ContinueItem {
        qint64 updatedAt;
        QString filePath;
        QString seriesPath;
        QString title;
        QString subtitle;
        QString coverPath;
    };
    QList<ContinueItem> items;

    for (auto it = allProg.begin(); it != allProg.end(); ++it) {
        QJsonObject prog = it.value().toObject();
        if (prog.value("finished").toBool())
            continue;
        int page = prog.value("page").toInt(0);
        if (page < 0)
            continue;

        // Look up file from our scan-built map
        auto ref = m_progressKeyMap.find(it.key());
        if (ref == m_progressKeyMap.end())
            continue;

        qint64 updatedAt = prog.value("updatedAt").toVariant().toLongLong();
        int pageCount = prog.value("pageCount").toInt(0);

        QString title = ScannerUtils::cleanMediaFolderTitle(QFileInfo(ref->filePath).completeBaseName());
        QString subtitle = pageCount > 0
            ? QString("Page %1/%2").arg(page + 1).arg(pageCount)
            : QString("Page %1").arg(page + 1);

        items.append({updatedAt, ref->filePath, ref->seriesPath, title, subtitle, ref->coverPath});
    }

    if (items.isEmpty()) {
        m_continueSection->hide();
        return;
    }

    // Per-series dedup: keep only the most recently updated volume per series
    QMap<QString, int> bestPerSeries;  // seriesPath → index in items
    for (int i = 0; i < items.size(); ++i) {
        auto it = bestPerSeries.find(items[i].seriesPath);
        if (it == bestPerSeries.end() || items[i].updatedAt > items[it.value()].updatedAt)
            bestPerSeries[items[i].seriesPath] = i;
    }

    QList<ContinueItem> deduped;
    for (int idx : bestPerSeries)
        deduped.append(items[idx]);

    std::sort(deduped.begin(), deduped.end(), [](const ContinueItem& a, const ContinueItem& b) {
        return a.updatedAt > b.updatedAt;
    });

    // Groundwork limit: 40 tiles max
    if (deduped.size() > 40)
        deduped = deduped.mid(0, 40);

    for (const auto& item : deduped) {
        auto* card = new TileCard(item.coverPath, item.title, item.subtitle);
        card->setProperty("filePath", item.filePath);
        card->setProperty("seriesPath", item.seriesPath);
        card->setProperty("seriesName", ScannerUtils::cleanMediaFolderTitle(
            QDir(item.seriesPath).dirName()));
        connect(card, &TileCard::clicked, this, [this, card]() {
            QString path = card->property("filePath").toString();
            QString seriesPath = card->property("seriesPath").toString();
            QString seriesName = card->property("seriesName").toString();
            QDir dir(seriesPath);
            QStringList files = dir.entryList(COMIC_EXTS, QDir::Files);
            QCollator col;
            col.setNumericMode(true);
            std::sort(files.begin(), files.end(), [&col](const QString& a, const QString& b) {
                return col.compare(a, b) < 0;
            });
            QStringList cbzList;
            for (const auto& f : files)
                cbzList.append(dir.absoluteFilePath(f));
            emit openComic(path, cbzList, seriesName);
        });
        m_continueStrip->addTile(card);
    }

    m_continueSection->show();
}

void ComicsPage::onCardClicked()
{
    auto* card = qobject_cast<TileCard*>(sender());
    if (!card) return;
    m_seriesView->showSeries(card->property("seriesPath").toString(),
                             card->property("seriesName").toString(),
                             card->property("coverPath").toString());
    m_stack->setCurrentIndexAnimated(1);
}

void ComicsPage::onTileContextMenu(const QPoint& pos)
{
    auto* card = m_tileStrip->tileAt(pos);
    if (!card) return;

    QString seriesPath = card->property("seriesPath").toString();
    QString seriesName = card->property("seriesName").toString();
    QString coverPath = card->property("coverPath").toString();

    // Check if all volumes in series are finished (for toggle label)
    QDir dir(seriesPath);
    QStringList cbzFiles = dir.entryList(COMIC_EXTS, QDir::Files);
    QJsonObject allProg = m_bridge->allProgress("comics");
    bool allFinished = !cbzFiles.isEmpty();
    for (const auto& f : cbzFiles) {
        QString id = QString(QCryptographicHash::hash(
            dir.absoluteFilePath(f).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        if (!allProg.value(id).toObject().value("finished").toBool()) {
            allFinished = false;
            break;
        }
    }

    auto* menu = ContextMenuHelper::createMenu(this);
    auto* openAct = menu->addAction("Open");
    menu->addSeparator();
    auto* markAct = menu->addAction(allFinished ? "Mark all as unread" : "Mark all as read");
    menu->addSeparator();
    auto* renameAct = menu->addAction("Rename series...");
    auto* hideAct = menu->addAction("Hide series");
    auto* revealAct = menu->addAction("Reveal in File Explorer");
    revealAct->setEnabled(!seriesPath.isEmpty());
    auto* copyAct = menu->addAction("Copy path");
    copyAct->setEnabled(!seriesPath.isEmpty());
    menu->addSeparator();
    auto* removeAct = ContextMenuHelper::addDangerAction(menu, "Remove from library...");
    removeAct->setEnabled(!seriesPath.isEmpty());

    auto* chosen = menu->exec(m_tileStrip->mapToGlobal(pos));
    if (chosen == openAct) {
        m_seriesView->showSeries(seriesPath, seriesName, coverPath);
        m_stack->setCurrentIndexAnimated(1);
    } else if (chosen == markAct) {
        bool setFinished = !allFinished;
        for (const auto& f : cbzFiles) {
            QString id = QString(QCryptographicHash::hash(
                dir.absoluteFilePath(f).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            QJsonObject prog = m_bridge->progress("comics", id);
            prog["finished"] = setFinished;
            m_bridge->saveProgress("comics", id, prog);
        }
    } else if (chosen == renameAct) {
        QString dirName = QDir(seriesPath).dirName();
        QString newName = QInputDialog::getText(this, "Rename series", "New name:", QLineEdit::Normal, dirName);
        if (!newName.isEmpty() && newName != dirName) {
            QString parentPath = QFileInfo(seriesPath).absolutePath();
            QString oldPath = parentPath + "/" + dirName;
            QString newPath = parentPath + "/" + newName.trimmed();
            if (QFile::rename(oldPath, newPath)) {
                triggerScan();
            } else {
                QMessageBox::warning(this, "Rename failed",
                    "Could not rename \"" + dirName + "\".\n"
                    "The folder may be in use by another program.");
            }
        }
    } else if (chosen == hideAct) {
        QSettings settings("Tankoban", "Tankoban");
        QStringList hidden = settings.value("comics_hidden_series").toStringList();
        if (!hidden.contains(seriesPath)) {
            hidden.append(seriesPath);
            settings.setValue("comics_hidden_series", hidden);
        }
        card->hide();
        m_tileStrip->filterTiles(m_searchBar->text());
    } else if (chosen == revealAct) {
        ContextMenuHelper::revealInExplorer(seriesPath);
    } else if (chosen == copyAct) {
        ContextMenuHelper::copyToClipboard(seriesPath);
    } else if (chosen == removeAct) {
        if (ContextMenuHelper::confirmRemove(this, "Remove from library",
                "Remove this series from the library?\n" + seriesPath +
                "\nFiles will not be deleted from disk.")) {
            triggerScan();
        }
    }
    menu->deleteLater();
}

void ComicsPage::onMultiSelectContextMenu(const QList<TileCard*>& selected, const QPoint& globalPos)
{
    int count = selected.size();
    if (count < 2) return;

    auto* menu = ContextMenuHelper::createMenu(this);

    auto* openFirstAct = menu->addAction("Open first selected");
    menu->addSeparator();
    auto* markReadAct   = menu->addAction("Mark all as read");
    auto* markUnreadAct = menu->addAction("Mark all as unread");
    menu->addSeparator();
    auto* removeAct = ContextMenuHelper::addDangerAction(menu,
        QString("Remove %1 items").arg(count));

    auto* chosen = menu->exec(globalPos);
    if (chosen == openFirstAct) {
        auto* first = selected.first();
        m_seriesView->showSeries(first->property("seriesPath").toString(),
                                 first->property("seriesName").toString(),
                                 first->property("coverPath").toString());
        m_stack->setCurrentIndexAnimated(1);
    } else if (chosen == markReadAct || chosen == markUnreadAct) {
        bool setFinished = (chosen == markReadAct);
        QJsonObject allProg = m_bridge->allProgress("comics");
        for (auto* card : selected) {
            QString seriesPath = card->property("seriesPath").toString();
            QDir dir(seriesPath);
            for (const auto& f : dir.entryList(COMIC_EXTS, QDir::Files)) {
                QString id = QString(QCryptographicHash::hash(
                    dir.absoluteFilePath(f).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                QJsonObject prog = m_bridge->progress("comics", id);
                prog["finished"] = setFinished;
                m_bridge->saveProgress("comics", id, prog);
            }
        }
        refreshContinueStrip();
    } else if (chosen == removeAct) {
        if (ContextMenuHelper::confirmRemove(this, "Remove from library",
                QString("Remove %1 items from library?\nFiles will not be deleted from disk.").arg(count))) {
            triggerScan();
        }
    }
    menu->deleteLater();
}
