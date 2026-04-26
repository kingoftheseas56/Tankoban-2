#include "BooksPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "BookSeriesView.h"
#include "AudiobookDetailView.h"
#include "core/CoreBridge.h"
#include "core/BooksScanner.h"
#include "core/ScannerUtils.h"

#include "ui/ContextMenuHelper.h"
#include "ui/widgets/FadingStackedWidget.h"
#include "ui/widgets/LibraryListView.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QMetaObject>
#include <QSettings>
#include <QInputDialog>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonObject>
#include <QFileInfo>
#include <QShortcut>
#include <QPushButton>
#include <QRegularExpression>
#include <QFile>
#include <QMessageBox>

BooksPage::BooksPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setObjectName("books");
    qRegisterMetaType<BookSeriesInfo>("BookSeriesInfo");
    qRegisterMetaType<AudiobookInfo>("AudiobookInfo");
    qRegisterMetaType<QList<BookSeriesInfo>>("QList<BookSeriesInfo>");
    qRegisterMetaType<QList<AudiobookInfo>>("QList<AudiobookInfo>");

    buildUI();

    m_scanThread = new QThread(this);
    m_scanner = new BooksScanner(m_bridge->dataDir() + "/thumbs");
    m_scanner->moveToThread(m_scanThread);

    connect(m_scanner, &BooksScanner::bookSeriesFound,
            this, &BooksPage::onBookSeriesFound, Qt::QueuedConnection);
    connect(m_scanner, &BooksScanner::audiobookFound,
            this, &BooksPage::onAudiobookFound, Qt::QueuedConnection);
    connect(m_scanner, &BooksScanner::scanFinished,
            this, &BooksPage::onScanFinished, Qt::QueuedConnection);

    // REPO_HYGIENE Phase 4 P4.2 (2026-04-26) — race-safe scanner ownership.
    connect(m_scanThread, &QThread::finished, m_scanner, &QObject::deleteLater);

    m_scanThread->start();

    connect(m_bridge, &CoreBridge::rootFoldersChanged, this, [this](const QString& domain) {
        if (domain == "books" || domain == "audiobooks")
            triggerScan();
    });
}

BooksPage::~BooksPage()
{
    m_scanThread->quit();
    m_scanThread->wait();
    // REPO_HYGIENE Phase 4 P4.2: m_scanner auto-deleted via deleteLater on
    // thread::finished. No manual delete.
}

void BooksPage::buildUI()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_stack = new FadingStackedWidget(this);

    // ── Grid view (index 0) ──
    auto* gridPage = new QWidget();
    auto* gridLayout = new QVBoxLayout(gridPage);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(0);

    // Scrollable content area
    auto* scroll = new QScrollArea(gridPage);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("background: transparent;");

    auto* content = new QWidget();
    content->setStyleSheet("background: transparent;");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(20, 0, 20, 20);
    layout->setSpacing(24);

    // ── 1. Search bar (full width, top) ──
    m_searchBar = new QLineEdit(content);
    m_searchBar->setPlaceholderText("Search books and series\u2026");
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
    layout->addLayout(searchLayout);

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
    connect(m_searchTimer, &QTimer::timeout, this, &BooksPage::applySearch);

    auto* searchShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        m_searchBar->setFocus();
        m_searchBar->selectAll();
    });

    // ── 2. Continue Reading section ──
    m_continueSection = new QWidget(content);
    auto* continueLayout = new QVBoxLayout(m_continueSection);
    continueLayout->setContentsMargins(0, 0, 0, 0);
    continueLayout->setSpacing(4);
    auto* continueLabel = new QLabel("CONTINUE READING", m_continueSection);
    continueLabel->setObjectName("LibraryHeading");
    continueLayout->addWidget(continueLabel);
    m_continueStrip = new TileStrip(m_continueSection);
    m_continueStrip->setMode("continue");
    continueLayout->addWidget(m_continueStrip);

    // Continue-tile context menu
    m_continueStrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_continueStrip, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* card = m_continueStrip->tileAt(pos);
        if (!card) return;

        QString filePath = card->property("filePath").toString();
        QString seriesPath = card->property("seriesPath").toString();
        QString seriesName = card->property("seriesName").toString();
        QString coverPath = card->property("coverPath").toString();
        QString progKey = card->property("progressKey").toString();

        bool isFinished = false;
        if (m_bridge && !progKey.isEmpty()) {
            QJsonObject prog = m_bridge->progress("books", progKey);
            isFinished = prog.value("finished").toBool();
        }

        auto* menu = ContextMenuHelper::createMenu(this);
        auto* continueAct = menu->addAction("Continue reading");
        auto* openSeriesAct = menu->addAction("Open series");
        openSeriesAct->setEnabled(!seriesPath.isEmpty());
        menu->addSeparator();
        auto* markAct = menu->addAction(isFinished ? "Mark as unread" : "Mark as read");
        auto* clearAct = menu->addAction("Clear from Continue Reading");
        menu->addSeparator();
        auto* renameAct = menu->addAction("Rename...");
        auto* revealAct = menu->addAction("Reveal in File Explorer");
        revealAct->setEnabled(!filePath.isEmpty());
        auto* copyAct = menu->addAction("Copy path");
        copyAct->setEnabled(!filePath.isEmpty());
        menu->addSeparator();
        auto* removeAct = ContextMenuHelper::addDangerAction(menu, "Remove from library...");
        removeAct->setEnabled(!seriesPath.isEmpty());

        auto* chosen = menu->exec(m_continueStrip->mapToGlobal(pos));
        if (chosen == continueAct) {
            if (!filePath.isEmpty()) emit openBook(filePath);
        } else if (chosen == openSeriesAct) {
            m_seriesView->showSeries(seriesPath, seriesName, coverPath);
            m_stack->setCurrentIndexAnimated(1);
        } else if (chosen == markAct) {
            if (m_bridge && !progKey.isEmpty()) {
                QJsonObject prog = m_bridge->progress("books", progKey);
                prog["finished"] = !isFinished;
                m_bridge->saveProgress("books", progKey, prog);
                refreshContinueStrip();
            }
        } else if (chosen == clearAct) {
            if (m_bridge && !progKey.isEmpty()) {
                m_bridge->clearProgress("books", progKey);
                refreshContinueStrip();
            }
        } else if (chosen == renameAct) {
            if (!filePath.isEmpty()) {
                QFileInfo fi(filePath);
                QString newName = QInputDialog::getText(this, "Rename",
                    "New name:", QLineEdit::Normal, fi.completeBaseName());
                if (!newName.isEmpty() && newName != fi.completeBaseName()) {
                    QString newPath = fi.absolutePath() + "/" + newName + "." + fi.suffix();
                    if (QFile::rename(filePath, newPath)) {
                        refreshContinueStrip();
                    } else {
                        QMessageBox::warning(this, "Rename failed",
                            "Could not rename \"" + fi.fileName() + "\".\n"
                            "The file may be in use by another program.");
                    }
                }
            }
        } else if (chosen == revealAct) {
            ContextMenuHelper::revealInExplorer(filePath);
        } else if (chosen == copyAct) {
            ContextMenuHelper::copyToClipboard(filePath);
        } else if (chosen == removeAct) {
            if (ContextMenuHelper::confirmRemove(this, "Remove from library",
                    "Remove this series from the library?\n" + seriesPath +
                    "\n\nFiles will remain on disk.")) {
                triggerScan();
            }
        }
        menu->deleteLater();
    });

    m_continueSection->hide();
    layout->addWidget(m_continueSection);

    // ── 3. "BOOKS" header row: label + sort + density ──
    auto* booksRow = new QWidget(content);
    auto* booksRowLayout = new QHBoxLayout(booksRow);
    booksRowLayout->setContentsMargins(0, 0, 0, 0);
    booksRowLayout->setSpacing(8);

    auto* booksLabel = new QLabel("BOOKS", booksRow);
    booksLabel->setObjectName("LibraryHeading");
    booksRowLayout->addWidget(booksLabel);
    booksRowLayout->addStretch();

    m_sortCombo = new QComboBox(booksRow);
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
    QString savedSort = QSettings("Tankoban", "Tankoban").value("library_sort_books", "name_asc").toString();
    for (int i = 0; i < m_sortCombo->count(); ++i) {
        if (m_sortCombo->itemData(i).toString() == savedSort) {
            m_sortCombo->setCurrentIndex(i);
            break;
        }
    }
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        QString key = m_sortCombo->itemData(idx).toString();
        QSettings("Tankoban", "Tankoban").setValue("library_sort_books", key);
        m_bookStrip->sortTiles(key);
    });
    booksRowLayout->addWidget(m_sortCombo);

    auto* densitySmall = new QLabel("A", booksRow);
    densitySmall->setObjectName("DensityLabelSmall");
    booksRowLayout->addWidget(densitySmall);

    m_densitySlider = new QSlider(Qt::Horizontal, booksRow);
    m_densitySlider->setRange(0, 2);
    m_densitySlider->setFixedWidth(100);
    m_densitySlider->setFixedHeight(20);
    int savedDensity = QSettings("Tankoban", "Tankoban").value("grid_cover_size", 1).toInt();
    m_densitySlider->setValue(qBound(0, savedDensity, 2));
    connect(m_densitySlider, &QSlider::valueChanged, this, [this](int val) {
        QSettings("Tankoban", "Tankoban").setValue("grid_cover_size", val);
        m_bookStrip->setDensity(val);
        m_audiobookStrip->setDensity(val);
        if (m_continueStrip) m_continueStrip->setDensity(val);
    });
    booksRowLayout->addWidget(m_densitySlider);

    auto* densityLarge = new QLabel("A", booksRow);
    densityLarge->setObjectName("DensityLabelLarge");
    booksRowLayout->addWidget(densityLarge);

    m_viewToggle = new QPushButton(booksRow);
    m_viewToggle->setObjectName("ViewToggle");
    m_viewToggle->setFixedSize(28, 28);
    m_viewToggle->setCursor(Qt::PointingHandCursor);
    m_viewToggle->setToolTip("Toggle grid/list view (V)");
    m_viewToggle->setStyleSheet(
        "QPushButton#ViewToggle { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 6px; color: rgba(255,255,255,0.5); font-size: 14px; }"
        "QPushButton#ViewToggle:hover { background: rgba(255,255,255,0.1); color: rgba(255,255,255,0.8); }");
    m_viewToggle->setText(QString::fromUtf8("\u2630")); // hamburger icon for grid mode
    booksRowLayout->addWidget(m_viewToggle);

    layout->addWidget(booksRow);

    m_bookStatus = new QLabel("Add a books folder to get started", content);
    m_bookStatus->setObjectName("TileSubtitle");
    m_bookStatus->setAlignment(Qt::AlignCenter);
    m_bookStatus->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 40px;");
    layout->addWidget(m_bookStatus);

    m_bookStrip = new TileStrip(content);
    m_bookStrip->hide();
    m_bookStrip->setMinimumHeight(340);
    layout->addWidget(m_bookStrip);

    // ── List view (hidden by default, shown on V toggle) ──
    m_listView = new LibraryListView(content);
    m_listView->hide();
    layout->addWidget(m_listView);

    connect(m_listView, &LibraryListView::itemActivated, this, [this](const QString& path) {
        // Find the series for this path and open series view
        for (auto it = m_seriesFiles.begin(); it != m_seriesFiles.end(); ++it) {
            if (it.key() == path) {
                QString name = ScannerUtils::cleanMediaFolderTitle(QDir(path).dirName());
                QString hash = QString(QCryptographicHash::hash(
                    path.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                QString thumbsDir = m_bridge->dataDir() + "/thumbs";
                QString cover = thumbsDir + "/" + hash + ".jpg";
                m_seriesView->showSeries(path, name, QFile::exists(cover) ? cover : QString());
                m_stack->setCurrentIndexAnimated(1);
                return;
            }
        }
    });

    // View toggle logic
    m_gridMode = QSettings("Tankoban", "Tankoban").value("library_view_mode_books", "grid").toString() == "grid";
    connect(m_viewToggle, &QPushButton::clicked, this, [this]() {
        m_gridMode = !m_gridMode;
        QSettings("Tankoban", "Tankoban").setValue("library_view_mode_books", m_gridMode ? "grid" : "list");
        if (m_gridMode) {
            m_listView->hide();
            m_bookStrip->show();
            m_densitySlider->show();
            m_viewToggle->setText(QString::fromUtf8("\u2630")); // hamburger
        } else {
            m_bookStrip->hide();
            m_listView->show();
            m_densitySlider->hide();
            m_viewToggle->setText(QString::fromUtf8("\u2637")); // dotted square
        }
    });

    // Context menu on book tiles
    m_bookStrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_bookStrip, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* card = m_bookStrip->tileAt(pos);
        if (!card) return;

        QString seriesPath = card->property("seriesPath").toString();
        QString seriesName = card->property("seriesName").toString();
        QString coverPath = card->property("coverPath").toString();

        // Check if all books are finished
        static const QStringList bookExts = {"*.epub","*.pdf","*.mobi","*.fb2","*.azw3","*.djvu","*.txt"};
        QDir dir(seriesPath);
        QStringList bookFiles = dir.entryList(bookExts, QDir::Files);
        QJsonObject allProg = m_bridge->allProgress("books");
        bool allFinished = !bookFiles.isEmpty();
        for (const auto& f : bookFiles) {
            QString id = QString(QCryptographicHash::hash(
                dir.absoluteFilePath(f).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            if (!allProg.value(id).toObject().value("finished").toBool()) {
                allFinished = false;
                break;
            }
        }

        // Find most recent in-progress book for "Continue reading"
        bool hasInProgress = false;
        QString continueFilePath;
        qint64 bestAt = -1;
        for (const auto& f : bookFiles) {
            QString fullPath = dir.absoluteFilePath(f);
            QString id = QString(QCryptographicHash::hash(
                fullPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            QJsonObject prog = allProg.value(id).toObject();
            if (!prog.isEmpty() && !prog.value("finished").toBool() && prog.value("page").toInt(-1) >= 0) {
                hasInProgress = true;
                qint64 updAt = prog.value("updatedAt").toVariant().toLongLong();
                if (updAt > bestAt) {
                    bestAt = updAt;
                    continueFilePath = fullPath;
                }
            }
        }

        auto* menu = ContextMenuHelper::createMenu(this);
        auto* openAct = menu->addAction("Open series");
        auto* continueAct = menu->addAction("Continue reading");
        continueAct->setEnabled(hasInProgress);
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
        auto* removeAct = ContextMenuHelper::addDangerAction(menu, "Remove series folder...");
        removeAct->setEnabled(!seriesPath.isEmpty());

        auto* chosen = menu->exec(m_bookStrip->mapToGlobal(pos));
        if (chosen == openAct) {
            m_seriesView->showSeries(seriesPath, seriesName, coverPath);
            m_stack->setCurrentIndexAnimated(1);
        } else if (chosen == continueAct) {
            if (!continueFilePath.isEmpty())
                emit openBook(continueFilePath);
        } else if (chosen == markAct) {
            bool setFinished = !allFinished;
            for (const auto& f : bookFiles) {
                QString id = QString(QCryptographicHash::hash(
                    dir.absoluteFilePath(f).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                QJsonObject prog = m_bridge->progress("books", id);
                prog["finished"] = setFinished;
                m_bridge->saveProgress("books", id, prog);
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
            QStringList hidden = settings.value("books_hidden_series").toStringList();
            if (!hidden.contains(seriesPath)) {
                hidden.append(seriesPath);
                settings.setValue("books_hidden_series", hidden);
            }
            card->hide();
            m_bookStrip->filterTiles(m_searchBar->text());
        } else if (chosen == revealAct) {
            ContextMenuHelper::revealInExplorer(seriesPath);
        } else if (chosen == copyAct) {
            ContextMenuHelper::copyToClipboard(seriesPath);
        } else if (chosen == removeAct) {
            if (ContextMenuHelper::confirmRemove(this, "Remove from library",
                    "Remove this series from the library?\n" + seriesPath +
                    "\n\nFiles will remain on disk.")) {
                triggerScan();
            }
        }
        menu->deleteLater();
    });

    // ── Book Hits section (scored search — individual book matches) ──
    m_bookHitsSection = new QWidget(content);
    auto* bhLayout = new QVBoxLayout(m_bookHitsSection);
    bhLayout->setContentsMargins(0, 0, 0, 0);
    bhLayout->setSpacing(0);
    m_bookHitsStrip = new TileStrip(m_bookHitsSection);
    m_bookHitsStrip->setStripLabel("MATCHING BOOKS");
    bhLayout->addWidget(m_bookHitsStrip);
    m_bookHitsSection->hide();
    layout->addWidget(m_bookHitsSection);

    // ── Audiobooks section ──
    m_audiobookSection = new QWidget(content);
    auto* abLayout = new QVBoxLayout(m_audiobookSection);
    abLayout->setContentsMargins(0, 0, 0, 0);
    // 24px matches the parent `layout`'s spacing — gives AUDIOBOOKS-header→tile
    // gap parity with BOOKS-header-row→tile gap. (Continue Reading uses 4px
    // intentionally for its denser top-of-page rhythm; Audiobooks sits
    // mid-scroll and matches the BOOKS section convention.)
    abLayout->setSpacing(24);

    // Header matches the "CONTINUE READING" / "BOOKS" pattern at :130-139 +
    // :233-235 — same QLabel#LibraryHeading styling, same direct-add (no
    // wrapper widget for extra padding).
    m_audiobookTitle = new QLabel("AUDIOBOOKS", m_audiobookSection);
    m_audiobookTitle->setObjectName("LibraryHeading");
    abLayout->addWidget(m_audiobookTitle);

    m_audiobookStatus = new QLabel("No audiobooks found", m_audiobookSection);
    m_audiobookStatus->setObjectName("TileSubtitle");
    m_audiobookStatus->setAlignment(Qt::AlignCenter);
    m_audiobookStatus->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 40px;");
    abLayout->addWidget(m_audiobookStatus);

    m_audiobookStrip = new TileStrip(m_audiobookSection);
    m_audiobookStrip->hide();
    m_audiobookStrip->setMinimumHeight(340);
    abLayout->addWidget(m_audiobookStrip);

    // Apply saved density now that all strips exist (book + audiobook +
    // continue). Continue strip density gate was dropped in TileStrip.cpp
    // 2026-04-25 so it now responds to setDensity uniformly.
    m_bookStrip->setDensity(savedDensity);
    m_audiobookStrip->setDensity(savedDensity);
    if (m_continueStrip) m_continueStrip->setDensity(savedDensity);

    m_audiobookSection->hide();
    layout->addWidget(m_audiobookSection);

    layout->addStretch(1);
    scroll->setWidget(content);
    gridLayout->addWidget(scroll, 1);

    m_stack->addWidget(gridPage);

    // ── Series view (index 1) ──
    m_seriesView = new BookSeriesView(m_bridge);
    connect(m_seriesView, &BookSeriesView::backRequested, this, &BooksPage::showGrid);
    connect(m_seriesView, &BookSeriesView::bookSelected, this, &BooksPage::openBook);
    m_stack->addWidget(m_seriesView);

    // AUDIOBOOK_PAIRED_READING_FIX Phase 2.1 — chapter-list detail view on
    // audiobook tile click. Read-only info view; playback happens inside
    // BookReader's Audio sidebar tab (Phase 3), not here.
    m_audiobookDetailView = new AudiobookDetailView(this);
    connect(m_audiobookDetailView, &AudiobookDetailView::backRequested,
            this, &BooksPage::showGrid);
    m_stack->addWidget(m_audiobookDetailView);

    outerLayout->addWidget(m_stack, 1);

    // ── Keyboard shortcuts (Batch 7) ──
    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (!m_searchBar->text().trimmed().isEmpty()) {
            m_searchBar->clear();
        } else if (m_stack->currentIndex() == 1) {
            showGrid();
        }
    });

    auto* f5Shortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(f5Shortcut, &QShortcut::activated, this, [this]() { triggerScan(); });

    auto* refreshShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), this);
    connect(refreshShortcut, &QShortcut::activated, this, [this]() {
        refreshContinueStrip();
        m_bookStrip->sortTiles(m_sortCombo->currentData().toString());
    });

    auto* vShortcut = new QShortcut(QKeySequence(Qt::Key_V), this);
    connect(vShortcut, &QShortcut::activated, this, [this]() {
        if (m_searchBar->hasFocus()) return; // no-op if search focused
        m_viewToggle->click();
    });

    auto* selectAllShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_A), this);
    connect(selectAllShortcut, &QShortcut::activated, this, [this]() {
        if (m_gridMode)
            m_bookStrip->selectAll();
    });

    // Apply initial view mode
    if (!m_gridMode) {
        m_bookStrip->hide();
        m_listView->show();
        m_densitySlider->hide();
        m_viewToggle->setText(QString::fromUtf8("\u2637"));
    }
}

void BooksPage::activate()
{
    if (!m_hasScanned)
        triggerScan();
}

void BooksPage::triggerScan()
{
    // REPO_HYGIENE Phase 4 P4.3 (2026-04-26) — buffer rather than drop.
    if (m_scanning) {
        m_rescanPending = true;
        return;
    }
    m_scanning = true;
    m_rescanPending = false;

    QStringList bookRoots = m_bridge->rootFolders("books");
    QStringList audiobookRoots = m_bridge->rootFolders("audiobooks");

    if (bookRoots.isEmpty() && audiobookRoots.isEmpty()) {
        m_bookStrip->clear();
        m_bookStrip->hide();
        m_bookStatus->setText("Add a books folder to get started");
        m_bookStatus->show();
        m_audiobookSection->hide();
        m_hasScanned = true;
        m_scanning = false;
        return;
    }

    if (!m_hasScanned) {
        // First scan: clear tiles, show scanning label for progressive loading
        m_bookStrip->clear();
        m_bookHitsStrip->clear();
        m_bookHitsSection->hide();
        m_progressKeyMap.clear();
        m_seriesFiles.clear();
        m_bookStatus->setText("Scanning...");
        m_bookStatus->show();
        m_bookStrip->hide();

        m_audiobookStrip->clear();
        m_audiobookStatus->setText("Scanning...");
        m_audiobookStatus->show();
        m_audiobookStrip->hide();
        m_audiobookSection->show();
    }
    // Rescan: keep old tiles visible — atomic swap happens in onScanFinished

    QMetaObject::invokeMethod(m_scanner, "scan", Qt::QueuedConnection,
                              Q_ARG(QStringList, bookRoots),
                              Q_ARG(QStringList, audiobookRoots));
}

void BooksPage::addBookSeriesTile(const BookSeriesInfo& series)
{
    static const QStringList bookExts = {"*.epub","*.pdf","*.mobi","*.fb2","*.azw3","*.djvu","*.txt"};
    QString thumbsDir = m_bridge->dataDir() + "/thumbs";
    QDir dir(series.seriesPath);
    QList<BookFile> fileList;
    for (const auto& f : dir.entryList(bookExts, QDir::Files)) {
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
        fileList.append({fullPath, ScannerUtils::cleanMediaFolderTitle(fi.completeBaseName())});
    }
    m_seriesFiles[series.seriesPath] = fileList;

    QString subtitle = QString::number(series.fileCount)
                     + (series.fileCount == 1 ? " book" : " books");

    auto* card = new TileCard(series.coverThumbPath, series.seriesName, subtitle);

    card->setProperty("seriesPath", series.seriesPath);
    card->setProperty("seriesName", series.seriesName);
    card->setProperty("coverPath", series.coverThumbPath);
    card->setProperty("fileCount", series.fileCount);
    card->setProperty("newestMtime", series.newestMtimeMs);
    connect(card, &TileCard::clicked, this, [this, card]() {
        QString path = card->property("seriesPath").toString();
        QString name = card->property("seriesName").toString();
        QString cover = card->property("coverPath").toString();
        m_seriesView->showSeries(path, name, cover);
        m_stack->setCurrentIndexAnimated(1);
    });

    m_bookStrip->addTile(card);
}

void BooksPage::addAudiobookTile(const AudiobookInfo& audiobook)
{
    // Phase 1.3 tile subtitle format: "{N} chapter(s) · HH:MM:SS" when
    // AudiobookMetaCache has populated durations; fall back to "{N} track(s)"
    // on cold-cache / probe-fail. Matches Max's detail-view "N chapters ·
    // HH:MM:SS" shape (see the screenshot Hemanth shared 2026-04-22).
    const int n = audiobook.trackCount;
    QString subtitle;

    if (audiobook.totalDurationMs > 0) {
        const qint64 totalSec = audiobook.totalDurationMs / 1000;
        const qint64 hh = totalSec / 3600;
        const qint64 mm = (totalSec % 3600) / 60;
        const qint64 ss = totalSec % 60;
        const QString duration = (hh > 0)
            ? QString("%1:%2:%3")
                  .arg(hh)
                  .arg(mm, 2, 10, QChar('0'))
                  .arg(ss, 2, 10, QChar('0'))
            : QString("%1:%2")
                  .arg(mm)
                  .arg(ss, 2, 10, QChar('0'));
        subtitle = QString("%1 %2 · %3")
                       .arg(n)
                       .arg(n == 1 ? "chapter" : "chapters")
                       .arg(duration);
    } else {
        subtitle = QString("%1 %2")
                       .arg(n)
                       .arg(n == 1 ? "track" : "tracks");
    }

    auto* card = new TileCard(audiobook.coverPath, audiobook.name, subtitle);

    // AUDIOBOOK_PAIRED_READING_FIX Phase 2.1 — click → chapter-list detail
    // view. Captures the AudiobookInfo by value so the lambda stays valid
    // even if the underlying list is rebuilt on rescan.
    connect(card, &TileCard::clicked, this, [this, audiobook]() {
        m_audiobookDetailView->showAudiobook(audiobook);
        m_stack->setCurrentIndexAnimated(2);
    });

    m_audiobookStrip->addTile(card);
}

void BooksPage::onBookSeriesFound(const BookSeriesInfo& series)
{
    // On rescan: skip incremental tiles — atomic rebuild in onScanFinished
    if (m_hasScanned) return;

    // First scan: progressive loading
    if (m_bookStatus->isVisible()) {
        m_bookStatus->hide();
        m_bookStrip->show();
    }
    addBookSeriesTile(series);
}

void BooksPage::onAudiobookFound(const AudiobookInfo& audiobook)
{
    // On rescan: skip incremental tiles — atomic rebuild in onScanFinished
    if (m_hasScanned) return;

    // First scan: progressive loading
    if (m_audiobookStatus->isVisible()) {
        m_audiobookStatus->hide();
        m_audiobookStrip->show();
    }
    addAudiobookTile(audiobook);
}

void BooksPage::onScanFinished(const QList<BookSeriesInfo>& allBooks,
                                const QList<AudiobookInfo>& allAudiobooks)
{
    bool wasRescan = m_hasScanned;
    m_hasScanned = true;
    m_scanning = false;
    // REPO_HYGIENE Phase 4 P4.3 (2026-04-26) — fire pending rescan.
    if (m_rescanPending) {
        m_rescanPending = false;
        QTimer::singleShot(0, this, [this]() { triggerScan(); });
    }

    if (wasRescan) {
        // Atomic swap: clear old tiles, rebuild from complete list
        m_bookStrip->clear();
        m_bookHitsStrip->clear();
        m_bookHitsSection->hide();
        m_listView->clear();
        m_progressKeyMap.clear();
        m_seriesFiles.clear();

        for (const auto& series : allBooks)
            addBookSeriesTile(series);

        m_audiobookStrip->clear();
        for (const auto& ab : allAudiobooks)
            addAudiobookTile(ab);
    }

    // Populate list view
    m_listView->clear();
    for (const auto& series : allBooks) {
        LibraryListView::ItemData item;
        item.name = series.seriesName;
        item.path = series.seriesPath;
        item.itemCount = series.fileCount;
        item.lastModifiedMs = series.newestMtimeMs;
        m_listView->addItem(item);
    }

    if (allBooks.isEmpty()) {
        m_bookStrip->hide();
        m_listView->hide();
        m_bookStatus->setObjectName("LibraryEmptyLabel");
        m_bookStatus->setAlignment(Qt::AlignCenter);
        m_bookStatus->setText("No books found\nAdd a root folder via the + button or browse Sources for content");
        m_bookStatus->show();
    } else {
        m_bookStatus->hide();
        m_bookStrip->show();
        m_bookStrip->sortTiles(m_sortCombo->currentData().toString());
    }

    if (allAudiobooks.isEmpty()) {
        m_audiobookStrip->hide();
        if (m_bridge->rootFolders("audiobooks").isEmpty())
            m_audiobookSection->hide();
        else {
            m_audiobookStatus->setText("No audiobooks found in your library folders");
            m_audiobookStatus->show();
        }
    } else {
        m_audiobookStatus->hide();
        m_audiobookStrip->show();
    }

    refreshContinueStrip();
}

void BooksPage::onTileClicked(const QString& seriesPath, const QString& seriesName)
{
    m_seriesView->showSeries(seriesPath, seriesName);
    m_stack->setCurrentIndexAnimated(1);
}

void BooksPage::showGrid()
{
    m_stack->setCurrentIndexAnimated(0);
}

// ── Scored search helpers ──

static QStringList tokenize(const QString& text)
{
    static QRegularExpression re("[a-z0-9]+");
    QStringList tokens;
    auto it = re.globalMatch(text.toLower());
    while (it.hasNext())
        tokens.append(it.next().captured());
    return tokens;
}

static int scoreTokens(const QString& text, const QStringList& queryTokens, const QString& fullQuery)
{
    QString lower = text.toLower();
    QStringList textTokens = tokenize(text);
    int score = 0;

    for (const auto& qt : queryTokens) {
        bool found = false;
        // Check substring match in full text
        if (lower.contains(qt)) {
            score += 14;
            found = true;
        }
        // Check exact word match
        for (const auto& tt : textTokens) {
            if (tt == qt) { score += 12; found = true; break; }
            if (tt.startsWith(qt)) { score += 6; found = true; break; }
        }
        if (!found) return 0; // AND logic: all tokens must match
    }

    // Full phrase bonus
    if (!fullQuery.isEmpty() && lower.contains(fullQuery.toLower()))
        score += 140;

    return score;
}

void BooksPage::applySearch()
{
    QString rawQuery = m_searchBar->text().trimmed();
    bool searchActive = !rawQuery.isEmpty();

    // Clear book hits from previous search
    m_bookHitsStrip->clear();
    m_bookHitsSection->hide();

    if (!searchActive) {
        // No search — show all, delegate to simple filter
        m_bookStrip->filterTiles(QString());
        m_audiobookStrip->filterTiles(QString());

        if (m_bookStrip->totalCount() > 0) {
            m_bookStatus->hide();
            m_bookStrip->show();
        }
        if (m_audiobookStrip->totalCount() > 0) {
            m_audiobookSection->show();
            m_audiobookStatus->hide();
            m_audiobookStrip->show();
        }
        return;
    }

    QStringList queryTokens = tokenize(rawQuery);
    if (queryTokens.isEmpty()) {
        m_bookStrip->filterTiles(QString());
        m_audiobookStrip->filterTiles(rawQuery);
        return;
    }

    // ── Score each series tile ──
    // We need to manually show/hide + sort by score, rather than using filterTiles
    m_bookStrip->filterTiles(QString()); // first show all
    struct ScoredTile { TileCard* card; int score; };
    QList<ScoredTile> scoredTiles;
    QSet<QString> seriesWithBookHits; // seriesPath of series that got book-level hits

    // Score series names
    for (int i = 0; i < m_bookStrip->totalCount(); ++i) {
        // Access tiles through the strip — use tileAt with position isn't ideal
        // Instead, iterate children
    }

    // Since TileStrip doesn't expose tile iteration directly, use filterTiles
    // and then also do book-level search for the "Book Hits" strip
    m_bookStrip->filterTiles(rawQuery);

    // ── Book-level search: find individual books matching query ──
    int bookHitCount = 0;
    QString thumbsDir = m_bridge->dataDir() + "/thumbs";

    for (auto it = m_seriesFiles.begin(); it != m_seriesFiles.end() && bookHitCount < 24; ++it) {
        QString seriesPath = it.key();
        // Check if the series name already matches — skip book-level hits for matching series
        QString seriesName = QDir(seriesPath).dirName();
        int seriesScore = scoreTokens(seriesName, queryTokens, rawQuery);
        if (seriesScore > 0) continue; // series already visible, no need for book hits

        const auto& files = it.value();
        for (const auto& bf : files) {
            if (bookHitCount >= 24) break;
            int bookScore = scoreTokens(bf.title, queryTokens, rawQuery);
            if (bookScore > 0) {
                // Create a tile for this individual book
                QFileInfo fi(bf.filePath);
                QString fileKey = bf.filePath + "::" + QString::number(fi.size())
                                + "::" + QString::number(fi.lastModified().toMSecsSinceEpoch());
                QString fileHash = QString(QCryptographicHash::hash(
                    fileKey.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                QString fileCover = thumbsDir + "/" + fileHash + ".jpg";

                // Find series cover as fallback
                QString seriesHash = QString(QCryptographicHash::hash(
                    seriesPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                QString seriesCover = thumbsDir + "/" + seriesHash + ".jpg";
                QString coverPath = QFile::exists(fileCover) ? fileCover :
                                    (QFile::exists(seriesCover) ? seriesCover : QString());

                auto* card = new TileCard(coverPath, bf.title, seriesName);
                card->setProperty("seriesPath", seriesPath);
                card->setProperty("filePath", bf.filePath);
                connect(card, &TileCard::clicked, this, [this, bf]() {
                    emit openBook(bf.filePath);
                });
                m_bookHitsStrip->addTile(card);
                bookHitCount++;
            }
        }
    }

    if (bookHitCount > 0)
        m_bookHitsSection->show();

    // Audiobooks — simple filter
    m_audiobookStrip->filterTiles(rawQuery);

    // Books section empty state
    if (m_bookStrip->visibleCount() == 0 && bookHitCount == 0) {
        m_bookStatus->setObjectName("LibraryEmptyLabel");
        m_bookStatus->setAlignment(Qt::AlignCenter);
        m_bookStatus->setText(
            QString("No results for \"%1\"").arg(rawQuery));
        m_bookStatus->show();
        m_bookStrip->hide();
    } else if (m_bookStrip->visibleCount() > 0) {
        m_bookStatus->hide();
        m_bookStrip->show();
    }

    // Audiobooks section empty state
    if (m_audiobookStrip->totalCount() > 0) {
        if (m_audiobookStrip->visibleCount() == 0 && searchActive) {
            m_audiobookSection->hide();
        } else if (m_audiobookStrip->visibleCount() > 0) {
            m_audiobookSection->show();
            m_audiobookStatus->hide();
            m_audiobookStrip->show();
        }
    }
}

void BooksPage::refreshContinueStrip()
{
    m_continueStrip->clear();

    QJsonObject allProg = m_bridge->allProgress("books");
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

    // Per-series dedup: keep only the most recently updated book per series
    QMap<QString, int> bestPerSeries;
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

    for (const auto& item : deduped) {
        auto* card = new TileCard(item.coverPath, item.title, item.subtitle);
        card->setProperty("filePath", item.filePath);
        card->setProperty("seriesPath", item.seriesPath);
        card->setProperty("seriesName", ScannerUtils::cleanMediaFolderTitle(QDir(item.seriesPath).dirName()));
        card->setProperty("coverPath", item.coverPath);
        // Store progress key for context menu operations
        QString progKey = QString(QCryptographicHash::hash(
            item.filePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        card->setProperty("progressKey", progKey);
        connect(card, &TileCard::clicked, this, [this, card]() {
            emit openBook(card->property("filePath").toString());
        });
        m_continueStrip->addTile(card);
    }

    m_continueSection->show();
}
