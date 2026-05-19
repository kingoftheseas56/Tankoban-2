#include "VideosPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "ShowView.h"
#include <QJsonArray>
#include "core/CoreBridge.h"
#include "core/VideosScanner.h"
#include "core/ScannerUtils.h"
#include "core/DebugLogBuffer.h"
#include "core/library/VideoCategoryStore.h"
#include "core/PosterCache.h"
#include "core/PosterFetcher.h"
#include "core/stream/MetaAggregator.h"
#include "core/stream/StreamDownloadIndex.h"
#include "core/stream/addon/MetaItem.h"
#include "core/torrent/TorrentClient.h"
#include "PosterPickerPopover.h"
#include "ui/ContextMenuHelper.h"
#include "ui/widgets/FadingStackedWidget.h"
#include "ui/widgets/LibraryListView.h"
#include <QPushButton>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QAction>
#include <QMenu>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QMetaObject>
#include <QSettings>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QFileInfo>
#include <QEvent>
#include <QShortcut>
#include <QFileDialog>
#include <QClipboard>
#include <QMimeData>
#include <QApplication>
#include <QStandardPaths>
#include <QImageReader>
#include <QMessageBox>
#include <QPixmap>

namespace {
const QStringList kVideoExts = {
    "*.mp4", "*.mkv", "*.avi", "*.webm", "*.mov", "*.wmv",
    "*.flv", "*.m4v", "*.ts",  "*.mpg", "*.mpeg", "*.ogv",
};

QString videoPosterCacheKey(const QString& posterPath)
{
    return QStringLiteral("video:") + posterPath;
}

void applyPosterPathToCard(TileCard* card, const QString& posterPath, QObject* context)
{
    if (!card || posterPath.isEmpty() || !QFile::exists(posterPath)) {
        return;
    }

    const QString cacheKey = videoPosterCacheKey(posterPath);
    const QPixmap cached = PosterCache::instance().get(cacheKey);
    if (!cached.isNull()) {
        card->setThumbPixmap(cached);
        return;
    }

    QPointer<TileCard> guard(card);
    PosterCache::instance().decodeFileAsync(cacheKey, posterPath, context,
        [guard, posterPath](const QPixmap& pix) {
            if (!guard) {
                return;
            }
            if (!pix.isNull()) {
                guard->setThumbPixmap(pix);
            } else {
                QFile::remove(posterPath);
            }
        });
}
}

QString VideosPage::posterPathFor(const QString& showPath)
{
    const QString hash = QString(QCryptographicHash::hash(
        showPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString dir = base + "/Tankoban/data/posters";
    QDir().mkpath(dir);
    return dir + "/" + hash + ".jpg";
}

VideosPage::VideosPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setObjectName("videos");
    qRegisterMetaType<ShowInfo>("ShowInfo");
    qRegisterMetaType<QList<ShowInfo>>("QList<ShowInfo>");

    buildUI();

    m_scanThread = new QThread(this);
    m_scanner = new VideosScanner();
    m_scanner->setCacheDir(m_bridge->dataDir());
    m_scanner->moveToThread(m_scanThread);

    connect(m_scanner, &VideosScanner::showFound,
            this, &VideosPage::onShowFound, Qt::QueuedConnection);
    connect(m_scanner, &VideosScanner::scanFinished,
            this, &VideosPage::onScanFinished, Qt::QueuedConnection);
    connect(m_scanner, &VideosScanner::durationsUpdated,
            this, [this](const QMap<QString, double>& updates) {
                for (auto it = updates.begin(); it != updates.end(); ++it) {
                    QString showRoot = m_fileToShowRoot.value(it.key());
                    if (!showRoot.isEmpty())
                        m_showDurations[showRoot].insert(it.key(), it.value());
                }
            }, Qt::QueuedConnection);

    // REPO_HYGIENE Phase 4 P4.2 (2026-04-26) — scanner ownership.
    // Pre-fix dtor did `m_scanThread->quit(); m_scanThread->wait(); delete m_scanner;`
    // which races with any scanner method still running on the thread between
    // quit() and wait()'s actual return. Connect deleteLater on thread::finished
    // instead so Qt's event loop guarantees the scanner is destroyed AFTER all
    // pending events have drained.
    connect(m_scanThread, &QThread::finished, m_scanner, &QObject::deleteLater);

    m_scanThread->start();

    connect(m_bridge, &CoreBridge::rootFoldersChanged, this, [this](const QString& domain) {
        if (domain == "videos")
            triggerScan();
    });
}

VideosPage::~VideosPage()
{
    m_scanThread->quit();
    m_scanThread->wait();
    // REPO_HYGIENE Phase 4 P4.2 (2026-04-26): m_scanner is auto-deleted via
    // the deleteLater connect on thread::finished above. No manual delete.
}

bool VideosPage::dispatchDevCommand(const QString& cmd,
                                    const QJsonObject& payload,
                                    QJsonObject& reply)
{
    Q_UNUSED(cmd);
    Q_UNUSED(payload);
    Q_UNUSED(reply);
    return false;
}

void VideosPage::buildUI()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_stack = new FadingStackedWidget(this);

    // ── Grid view (index 0) — wrapped in scroll area ──
    auto* gridScroll = new QScrollArea();
    m_gridScroll = gridScroll;  // GLOBAL_NAV_HISTORY Task 10
    gridScroll->setObjectName("VideosGridScroll");
    gridScroll->setFrameShape(QFrame::NoFrame);
    gridScroll->setWidgetResizable(true);
    gridScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    gridScroll->setStyleSheet("QScrollArea#VideosGridScroll { background: transparent; border: none; }");

    auto* gridPage = new QWidget();
    gridPage->setObjectName("VideosGridPage");
    gridPage->setStyleSheet("QWidget#VideosGridPage { background: transparent; }");
    auto* gridLayout = new QVBoxLayout(gridPage);
    gridLayout->setContentsMargins(20, 0, 20, 20);
    gridLayout->setSpacing(24);

    // ── 1. Search bar (full width, top) ──
    gridLayout->addSpacing(12);
    m_searchBar = new QLineEdit(gridPage);
    m_searchBar->setPlaceholderText("Search shows and episodes\u2026");
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
    connect(m_searchTimer, &QTimer::timeout, this, &VideosPage::applySearch);

    auto* searchShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        m_searchBar->setFocus();
        m_searchBar->selectAll();
    });

    // Throttle continue strip refresh during active playback (max once per 5s)
    m_continueRefreshThrottle = new QTimer(this);
    m_continueRefreshThrottle->setSingleShot(true);
    m_continueRefreshThrottle->setInterval(5000);
    connect(m_continueRefreshThrottle, &QTimer::timeout, this, &VideosPage::refreshContinueStrip);

    // 250ms single-click delay — double-click cancels and executes immediately
    m_clickTimer = new QTimer(this);
    m_clickTimer->setSingleShot(true);
    m_clickTimer->setInterval(250);
    connect(m_clickTimer, &QTimer::timeout, this, &VideosPage::executePendingClick);

    // ── 2. Continue Watching section (flat-only — per-category mode removed
    //      2026-05-05 per Hemanth "remove the continue watching per category
    //      feature altogether it's a mess looks like a mess") ──
    m_continueSection = new QWidget(gridPage);
    auto* continueLayout = new QVBoxLayout(m_continueSection);
    continueLayout->setContentsMargins(0, 0, 0, 0);
    continueLayout->setSpacing(4);
    auto* continueLabel = new QLabel("CONTINUE WATCHING", m_continueSection);
    continueLabel->setObjectName("LibraryHeading");
    continueLayout->addWidget(continueLabel);
    m_continueStrip = new TileStrip(m_continueSection);
    m_continueStrip->setMode("continue");
    continueLayout->addWidget(m_continueStrip);
    m_continueSection->hide();
    gridLayout->addWidget(m_continueSection);

    // ── 3. "SHOWS" header row: label + sort + density ──
    auto* showsRow = new QWidget(gridPage);
    auto* showsLayout = new QHBoxLayout(showsRow);
    showsLayout->setContentsMargins(0, 0, 0, 0);
    showsLayout->setSpacing(8);

    auto* showsLabel = new QLabel("LIBRARY", showsRow);
    showsLabel->setObjectName("LibraryHeading");
    showsLayout->addWidget(showsLabel);
    showsLayout->addStretch();

    m_sortCombo = new QComboBox(showsRow);
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
    QString savedSort = QSettings("Tankoban", "Tankoban").value("library_sort_videos", "name_asc").toString();
    for (int i = 0; i < m_sortCombo->count(); ++i) {
        if (m_sortCombo->itemData(i).toString() == savedSort) {
            m_sortCombo->setCurrentIndex(i);
            break;
        }
    }
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        QString key = m_sortCombo->itemData(idx).toString();
        QSettings("Tankoban", "Tankoban").setValue("library_sort_videos", key);
        sortCategoryRows();
    });
    showsLayout->addWidget(m_sortCombo);

    auto* densitySmall = new QLabel("A", showsRow);
    densitySmall->setObjectName("DensityLabelSmall");
    showsLayout->addWidget(densitySmall);

    m_densitySlider = new QSlider(Qt::Horizontal, showsRow);
    m_densitySlider->setRange(0, 2);
    m_densitySlider->setFixedWidth(100);
    m_densitySlider->setFixedHeight(20);
    int savedDensity = QSettings("Tankoban", "Tankoban").value("grid_cover_size", 1).toInt();
    m_densitySlider->setValue(qBound(0, savedDensity, 2));
    connect(m_densitySlider, &QSlider::valueChanged, this, [this](int val) {
        QSettings("Tankoban", "Tankoban").setValue("grid_cover_size", val);
        applyDensityToAllStrips(val);
    });
    showsLayout->addWidget(m_densitySlider);

    auto* densityLarge = new QLabel("A", showsRow);
    densityLarge->setObjectName("DensityLabelLarge");
    showsLayout->addWidget(densityLarge);

    // View toggle button (grid/list)
    m_viewToggle = new QPushButton(showsRow);
    m_viewToggle->setObjectName("ViewToggle");
    m_viewToggle->setFixedSize(28, 28);
    m_viewToggle->setText("\u2630");
    m_viewToggle->setCursor(Qt::PointingHandCursor);
    m_viewToggle->setStyleSheet(
        "#ViewToggle { background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.1); "
        "border-radius: 4px; color: rgba(255,255,255,0.5); font-size: 14px; }"
        "#ViewToggle:hover { background: rgba(255,255,255,0.1); color: rgba(255,255,255,0.7); }");
    connect(m_viewToggle, &QPushButton::clicked, this, &VideosPage::toggleViewMode);
    showsLayout->addWidget(m_viewToggle);

    gridLayout->addWidget(showsRow);

    m_statusLabel = new QLabel("Add a videos folder to get started", gridPage);
    m_statusLabel->setObjectName("TileSubtitle");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 60px;");
    gridLayout->addWidget(m_statusLabel);

    m_categoriesContainer = new QWidget(gridPage);
    m_categoriesLayout = new QVBoxLayout(m_categoriesContainer);
    m_categoriesLayout->setContentsMargins(0, 0, 0, 0);
    m_categoriesLayout->setSpacing(22);
    for (const auto& info : videoCategoryInfos()) {
        const VideoCategory category = info.category;

        auto* section = new QWidget(m_categoriesContainer);
        auto* sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(4);
        auto* label = new QLabel(videoCategoryHeading(category), section);
        label->setObjectName("LibraryHeading");
        sectionLayout->addWidget(label);
        auto* strip = new TileStrip(section);
        strip->setDensity(savedDensity);
        sectionLayout->addWidget(strip);
        section->hide();
        m_categorySections.insert(category, section);
        m_categoryStrips.insert(category, strip);
        m_categoriesLayout->addWidget(section);
    }
    if (m_continueStrip) m_continueStrip->setDensity(savedDensity);
    m_categoriesContainer->hide();
    gridLayout->addWidget(m_categoriesContainer);

    // List view (hidden by default — V-key toggles)
    m_listView = new LibraryListView(gridPage);
    m_listView->hide();
    connect(m_listView, &LibraryListView::itemActivated, this, [this](const QString& path) {
        m_showView->setFileDurations(m_showDurations.value(path));
        m_showView->showFolder(path, QFileInfo(path).fileName(), posterPathFor(path));
        m_stack->setCurrentIndexAnimated(1);
    });
    gridLayout->addWidget(m_listView, 1);

    // ── Helper: compute video ID for a file ──
    auto computeVideoId = [](const QString& filePath) -> QString {
        QFileInfo fi(filePath);
        QString raw = fi.absoluteFilePath() + "::" + QString::number(fi.size())
                    + "::" + QString::number(fi.lastModified().toMSecsSinceEpoch());
        return QString(QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha1).toHex());
    };

    // ── Helper: poster path for a show folder ──
    auto posterPath = [](const QString& showPath) -> QString {
        QString hash = QString(QCryptographicHash::hash(showPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        QString dir = base + "/Tankoban/data/posters";
        QDir().mkpath(dir);
        return dir + "/" + hash + ".jpg";
    };

    // ── Helper: mark all episodes watched/unwatched ──
    auto markAllEpisodes = [this, computeVideoId](const QString& showPath, bool setFinished) {
        QStringList allFiles = QFileInfo(showPath).isFile()
            ? QStringList{showPath}
            : ScannerUtils::walkFiles(showPath, kVideoExts);
        for (const auto& f : allFiles) {
            QString id = computeVideoId(f);
            QJsonObject prog = m_bridge->progress("videos", id);
            prog["finished"] = setFinished;
            m_bridge->saveProgress("videos", id, prog);
        }
        refreshContinueStrip();
    };

    // ── Helper: rename a show folder, migrating per-file progress + poster ──
    // Progress IDs hash the absolute file path, so a folder rename orphans every
    // record unless we re-key them. Poster files are hashed on showPath too.
    auto renameShowFolder = [this, computeVideoId, posterPath]
        (const QString& oldPath, const QString& newPath) -> bool {
        struct Migration { QString relPath; QString oldId; QJsonObject data; };
        QList<Migration> migrations;
        const QStringList oldFiles = ScannerUtils::walkFiles(oldPath, kVideoExts);
        for (const auto& oldFile : oldFiles) {
            const QString oldId = computeVideoId(oldFile);
            QJsonObject prog = m_bridge->progress("videos", oldId);
            if (!prog.isEmpty())
                migrations.append({QDir(oldPath).relativeFilePath(oldFile), oldId, prog});
        }
        const QString oldPoster = posterPath(oldPath);
        const bool hadPoster = QFile::exists(oldPoster);

        // Release any active libtorrent record pointing at oldPath BEFORE
        // QFile::rename so libtorrent doesn't resurrect the original folder
        // on its next periodic resume-data save (every 30s) or on next boot
        // — that resurrection is what produced the "multiplying folders"
        // symptom (Hemanth's Vinland Saga case 2026-04-16).
        if (m_torrentClient)
            m_torrentClient->releaseFolder(oldPath);

        // Retry rename on transient sharing violations. lt::session::remove_torrent
        // (called from releaseFolder above) is asynchronous — file handles can
        // stay open for hundreds of ms while libtorrent's disk thread flushes
        // + closes per-file mmaps. Other holders (PosterCache, AV scanners,
        // Explorer preview pane) can also momentarily lock the parent on
        // Windows. Retry every 100ms for ~1s; the rename succeeds the instant
        // any holder releases, regardless of which one it was.
        bool renamed = false;
        for (int attempt = 0; attempt < 10; ++attempt) {
            if (QFile::rename(oldPath, newPath)) {
                renamed = true;
                break;
            }
            QThread::msleep(100);
        }
        if (!renamed) {
            qWarning() << "renameShowFolder: rename failed after 10 retries —"
                       << oldPath << "→" << newPath;
            return false;
        }

        for (const auto& m : migrations) {
            const QString newFile = QDir(newPath).absoluteFilePath(m.relPath);
            const QString newId = computeVideoId(newFile);
            QJsonObject data = m.data;
            data["path"] = newFile;
            m_bridge->saveProgress("videos", newId, data);
            m_bridge->clearProgress("videos", m.oldId);
        }
        if (hadPoster)
            QFile::rename(oldPoster, posterPath(newPath));
        return true;
    };

    // ── Grid context menus: full folder-tile menu (Play / Continue, Mark
    //    watched, Clear from CW, Rename, Auto-rename, Reveal, Copy path,
    //    Move to..., Set/Remove/Paste/Fetch poster, Remove) on EVERY
    //    category strip — restored 2026-05-06 after Codex's 2026-05-05
    //    multi-category ship narrowed it to Misc-only via the m_tileStrip
    //    alias. See VideosPage::installFolderTileContextMenu. ──
    for (TileStrip* strip : m_categoryStrips) {
        if (!strip) continue;
        installFolderTileContextMenu(strip, computeVideoId, markAllEpisodes,
                                     posterPath, renameShowFolder);
    }

    // ── Continue-tile context menu ──
    m_continueStrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_continueStrip, &QWidget::customContextMenuRequested, this, [this, computeVideoId](const QPoint& pos) {
        auto* card = m_continueStrip->tileAt(pos);
        if (!card) return;

        QString filePath = card->property("filePath").toString();
        if (filePath.isEmpty()) return;

        QString videoId = computeVideoId(filePath);
        QJsonObject prog = m_bridge->progress("videos", videoId);
        bool finished = prog.value("finished").toBool();

        // Find the show identity this episode belongs to. Scanner data is
        // authoritative here because loose-file tiles use the file path itself.
        QFileInfo fi(filePath);
        QString showPath = m_fileToShowRoot.value(filePath, fi.absolutePath());

        auto* menu = ContextMenuHelper::createMenu(this);
        auto* playAct = menu->addAction("Play / Continue");
        auto* playBeginAct = menu->addAction("Play from beginning");
        auto* openShowAct = menu->addAction("Open show");
        openShowAct->setVisible(!showPath.isEmpty());
        menu->addSeparator();
        auto* markAct = menu->addAction(finished ? "Mark as unwatched" : "Mark as watched");
        auto* clearAct = menu->addAction("Clear from Continue Watching");
        menu->addSeparator();
        auto* autoRenameAct = menu->addAction("Auto-rename show");
        autoRenameAct->setEnabled(!showPath.isEmpty());
        auto* revealAct = menu->addAction("Reveal in File Explorer");
        revealAct->setEnabled(!filePath.isEmpty());
        auto* copyAct = menu->addAction("Copy path");
        copyAct->setEnabled(!filePath.isEmpty());

        menu->addSeparator();
        auto* moveMenu = menu->addMenu("Move to");
        QMap<QAction*, VideoCategory> moveActions;
        for (const auto& info : videoCategoryInfos()) {
            auto* act = moveMenu->addAction(videoCategoryLabel(info.category));
            moveActions.insert(act, info.category);
        }

        menu->addSeparator();
        auto* removeAct = ContextMenuHelper::addDangerAction(menu, "Remove from library...");
        removeAct->setEnabled(!showPath.isEmpty());

        auto* chosen = menu->exec(m_continueStrip->mapToGlobal(pos));
        if (chosen == playAct) {
            emit playVideo(filePath);
        } else if (chosen == playBeginAct) {
            // Reset position then play
            prog.remove("positionSec");
            m_bridge->saveProgress("videos", videoId, prog);
            emit playVideo(filePath);
        } else if (chosen == openShowAct) {
            QString showName = ScannerUtils::cleanMediaFolderTitle(QDir(showPath).dirName());
            m_showView->setFileDurations(m_showDurations.value(showPath));
            m_showView->showFolder(showPath, showName, posterPathFor(showPath));
            m_stack->setCurrentIndexAnimated(1);
        } else if (chosen == markAct) {
            prog["finished"] = !finished;
            m_bridge->saveProgress("videos", videoId, prog);
            refreshContinueStrip();
        } else if (chosen == clearAct) {
            prog.remove("positionSec");
            prog.remove("finished");
            m_bridge->saveProgress("videos", videoId, prog);
            refreshContinueStrip();
        } else if (chosen == autoRenameAct) {
            QString dirName = QDir(showPath).dirName();
            QString cleaned = ScannerUtils::cleanMediaFolderTitle(dirName);
            if (cleaned.isEmpty() || cleaned == dirName) {
                QMessageBox::information(this, "Auto-rename",
                    "Auto-rename not needed — \"" + dirName + "\" is already clean.");
            } else {
                QString parentPath = QFileInfo(showPath).absolutePath();
                QString oldPath = parentPath + "/" + dirName;
                QString newPath = parentPath + "/" + cleaned;
                if (QFileInfo::exists(newPath)) {
                    QMessageBox::warning(this, "Auto-rename failed",
                        "A folder named \"" + cleaned + "\" already exists in this location.");
                } else if (QFile::rename(oldPath, newPath)) {
                    triggerScan();
                } else {
                    QMessageBox::warning(this, "Auto-rename failed",
                        "Could not rename \"" + dirName + "\" to \"" + cleaned + "\".\n"
                        "The folder may be in use by another program.");
                }
            }
        } else if (chosen == revealAct) {
            ContextMenuHelper::revealInExplorer(filePath);
        } else if (chosen == copyAct) {
            ContextMenuHelper::copyToClipboard(filePath);
        } else if (moveActions.contains(chosen)) {
            moveShowToCategory(showPath, moveActions.value(chosen));
        } else if (chosen == removeAct) {
            if (ContextMenuHelper::confirmRemove(this, "Remove from library",
                    "Remove this show from the library?\n" + showPath +
                    "\nFiles will not be deleted from disk.")) {
                triggerScan();
            }
        }
        menu->deleteLater();
    });

    // V-key: toggle grid/list view
    auto* viewToggleShortcut = new QShortcut(QKeySequence(Qt::Key_V), this);
    connect(viewToggleShortcut, &QShortcut::activated, this, [this]() {
        if (m_stack->currentIndex() == 0 && !m_searchBar->hasFocus())
            toggleViewMode();
    });

    // Escape: clear search if active, else navigate back from ShowView.
    // MAKE_MPV_SOLO Task 7 (2026-05-01) — scope to WidgetWithChildren so
    // the shortcut only fires when this page (or its descendants) has
    // focus. Default Qt::WindowShortcut fired on any Esc press in the
    // MainWindow even when this page was hidden in the QStackedWidget,
    // eating Esc before VideoPlayer's back_to_library handler at
    // VideoPlayer.cpp:3294 could close the player.
    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (!m_searchBar->text().isEmpty()) {
            m_searchBar->clear();
        } else if (m_stack->currentIndex() == 1) {
            showGrid();
        } else {
            for (TileStrip* strip : m_categoryStrips)
                if (strip) strip->clearSelection();
        }
    });

    // F5: trigger rescan
    auto* f5Shortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(f5Shortcut, &QShortcut::activated, this, [this]() { triggerScan(); });

    // Ctrl+R: refresh state (rescan)
    auto* ctrlRShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), this);
    connect(ctrlRShortcut, &QShortcut::activated, this, [this]() { triggerScan(); });

    // Ctrl+A: select all tiles
    auto* ctrlAShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_A), this);
    connect(ctrlAShortcut, &QShortcut::activated, this, [this]() {
        if (m_stack->currentIndex() == 0 && !m_searchBar->hasFocus()) {
            for (TileStrip* strip : m_categoryStrips)
                if (strip && strip->isVisible()) strip->selectAll();
        }
    });

    QSettings settings("Tankoban", "Tankoban");
    m_gridMode = settings.value("library_view_mode_videos", "grid").toString() == "grid";
    if (!m_gridMode) toggleViewMode();

    gridLayout->addStretch();
    gridScroll->setWidget(gridPage);
    m_stack->addWidget(gridScroll);

    // ── Show view (index 1) ──
    m_showView = new ShowView(m_bridge);
    connect(m_showView, &ShowView::backRequested, this, &VideosPage::showGrid);
    connect(m_showView, &ShowView::episodeSelected, this, [this](const QString& filePath) {
        emit playVideo(filePath);
    });
    connect(m_showView, &ShowView::categoryMoveRequested,
            this, &VideosPage::moveShowToCategory);
    m_stack->addWidget(m_showView);

    outerLayout->addWidget(m_stack, 1);
}

// VIDEOS_LIBRARY_FULL_CONTEXT_MENU 2026-05-06 — restored full folder-tile
// menu on every category strip after Codex's 2026-05-05 multi-category ship
// narrowed it to Misc-only. Body is lifted verbatim from the prior
// m_tileStrip-bound lambda; the only deltas are (a) parametrized on
// `strip` instead of m_tileStrip, (b) closures take std::function args
// instead of inline lambda captures, (c) videoExts is now file-scope
// kVideoExts. Multi-select branch fires when strip->selectedTiles().size() > 1
// — TileStrip already supports multi-select uniformly, so per-category
// strips inherit it for free.
void VideosPage::installFolderTileContextMenu(
    TileStrip* strip,
    std::function<QString(const QString&)> computeVideoId,
    std::function<void(const QString&, bool)> markAllEpisodes,
    std::function<QString(const QString&)> posterPath,
    std::function<bool(const QString&, const QString&)> renameShowFolder)
{
    if (!strip)
        return;
    strip->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(strip, &QWidget::customContextMenuRequested, this,
        [this, strip, computeVideoId, markAllEpisodes, posterPath, renameShowFolder]
        (const QPoint& pos) {
        // Check for multi-selection first
        auto selected = strip->selectedTiles();
        if (selected.size() > 1) {
            // ── Multi-select context menu ──
            auto* menu = ContextMenuHelper::createMenu(this);
            auto* playFirstAct = menu->addAction("Play first selected");
            menu->addSeparator();
            auto* markWatchedAct = menu->addAction("Mark all as watched");
            auto* markUnwatchedAct = menu->addAction("Mark all as unwatched");
            menu->addSeparator();
            auto* removeAct = ContextMenuHelper::addDangerAction(menu,
                QString("Remove %1 items").arg(selected.size()));

            auto* chosen = menu->exec(strip->mapToGlobal(pos));
            if (chosen == playFirstAct) {
                QString path = selected.first()->property("seriesPath").toString();
                QStringList files = ScannerUtils::walkFiles(path, kVideoExts);
                if (!files.isEmpty())
                    emit playVideo(files.first());
            } else if (chosen == markWatchedAct || chosen == markUnwatchedAct) {
                bool setFinished = (chosen == markWatchedAct);
                for (auto* card : selected)
                    markAllEpisodes(card->property("seriesPath").toString(), setFinished);
            } else if (chosen == removeAct) {
                if (ContextMenuHelper::confirmRemove(this, "Remove from library",
                        QString("Remove %1 items from library?\nFiles will not be deleted from disk.")
                            .arg(selected.size()))) {
                    triggerScan();
                }
            }
            menu->deleteLater();
            return;
        }

        // ── Single-select context menu ──
        auto* card = strip->tileAt(pos);
        if (!card) return;

        QString showPath = card->property("seriesPath").toString();
        QString showName = card->property("seriesName").toString();

        QStringList allFiles = card->property("isLoose").toBool()
            ? QStringList{card->property("primaryFilePath").toString()}
            : ScannerUtils::walkFiles(showPath, kVideoExts);
        QJsonObject allProg = m_bridge->allProgress("videos");
        bool hasEpisodes = !allFiles.isEmpty();

        bool allWatched = hasEpisodes;
        bool hasProgress = false;
        for (const auto& f : allFiles) {
            QString id = computeVideoId(f);
            QJsonObject prog = allProg.value(id).toObject();
            if (!prog.value("finished").toBool())
                allWatched = false;
            if (!prog.isEmpty())
                hasProgress = true;
        }

        auto* menu = ContextMenuHelper::createMenu(this);
        auto* playAct = menu->addAction("Play / Continue");
        playAct->setEnabled(hasEpisodes);
        auto* playBeginAct = menu->addAction("Play from beginning");
        playBeginAct->setEnabled(hasEpisodes);
        menu->addSeparator();
        auto* markAct = menu->addAction(allWatched ? "Mark all as unwatched" : "Mark all as watched");
        auto* clearContAct = menu->addAction("Clear from Continue Watching");
        clearContAct->setEnabled(hasProgress);
        menu->addSeparator();
        auto* renameAct = menu->addAction("Rename");
        auto* autoRenameAct = menu->addAction("Auto-rename");
        auto* revealAct = menu->addAction("Reveal in File Explorer");
        revealAct->setEnabled(!showPath.isEmpty());
        auto* copyAct = menu->addAction("Copy path");
        copyAct->setEnabled(!showPath.isEmpty());

        menu->addSeparator();
        auto* moveMenu = menu->addMenu("Move to");
        QMap<QAction*, VideoCategory> moveActions;
        for (const auto& info : videoCategoryInfos()) {
            auto* act = moveMenu->addAction(videoCategoryLabel(info.category));
            moveActions.insert(act, info.category);
        }

        menu->addSeparator();
        auto* setPosterAct = menu->addAction("Set poster...");
        QString existingPoster = posterPath(showPath);
        auto* removePosterAct = menu->addAction("Remove poster");
        removePosterAct->setEnabled(QFile::exists(existingPoster));
        auto* pastePosterAct = menu->addAction("Paste image as poster");
        pastePosterAct->setEnabled(QApplication::clipboard()->mimeData()->hasImage());
        auto* fetchPosterAct = menu->addAction("Fetch poster from internet");
        fetchPosterAct->setEnabled(m_meta != nullptr && !showPath.isEmpty());
        menu->addSeparator();
        auto* removeAct = ContextMenuHelper::addDangerAction(menu, "Remove from library...");
        removeAct->setEnabled(!showPath.isEmpty());

        auto* chosen = menu->exec(strip->mapToGlobal(pos));
        if (chosen == playAct) {
            QString resumeFile;
            qint64 bestAt = -1;
            for (const auto& f : allFiles) {
                QString id = computeVideoId(f);
                QJsonObject prog = allProg.value(id).toObject();
                if (prog.value("finished").toBool()) continue;
                double posSec = prog.value("positionSec").toDouble(0);
                if (posSec > 0) {
                    qint64 upd = prog.value("updatedAt").toVariant().toLongLong();
                    if (upd > bestAt) { bestAt = upd; resumeFile = f; }
                }
            }
            if (resumeFile.isEmpty() && !allFiles.isEmpty())
                resumeFile = allFiles.first();
            if (!resumeFile.isEmpty())
                emit playVideo(resumeFile);
        } else if (chosen == playBeginAct) {
            if (!allFiles.isEmpty())
                emit playVideo(allFiles.first());
        } else if (chosen == markAct) {
            markAllEpisodes(showPath, !allWatched);
        } else if (chosen == clearContAct) {
            QJsonObject allProg = m_bridge->allProgress("videos");
            bool changed = false;
            for (auto it = allProg.begin(); it != allProg.end(); ++it) {
                QJsonObject prog = it.value().toObject();
                const QString entryPath = prog.value("path").toString();
                if (entryPath.isEmpty())
                    continue;
                const bool match = (entryPath == showPath)
                    || entryPath.startsWith(showPath + QLatin1Char('/'))
                    || entryPath.startsWith(showPath + QLatin1Char('\\'));
                if (!match)
                    continue;
                prog.remove("positionSec");
                prog.remove("finished");
                m_bridge->saveProgress("videos", it.key(), prog);
                changed = true;
            }
            if (changed)
                refreshContinueStrip();
        } else if (chosen == renameAct) {
            QObject::connect(card, &TileCard::renameCompleted, this,
                [this, showPath, renameShowFolder](bool commit, const QString& newName) {
                    if (!commit) return;
                    const QString dirName = QDir(showPath).dirName();
                    const QString parentPath = QFileInfo(showPath).absolutePath();
                    const QString oldPath = parentPath + "/" + dirName;
                    const QString newPath = parentPath + "/" + newName;
                    if (QFileInfo::exists(newPath)) {
                        QMessageBox::warning(this, "Rename failed",
                            "A folder named \"" + newName + "\" already exists in this location.");
                    } else if (renameShowFolder(oldPath, newPath)) {
                        triggerScan();
                    } else {
                        QMessageBox::warning(this, "Rename failed",
                            "Could not rename \"" + dirName + "\".\n"
                            "The folder may be in use by another program.");
                    }
                }, Qt::SingleShotConnection);
            card->beginRename();
        } else if (chosen == autoRenameAct) {
            QString dirName = QDir(showPath).dirName();
            QString cleaned = ScannerUtils::cleanMediaFolderTitle(dirName);
            if (cleaned.isEmpty() || cleaned == dirName) {
                QMessageBox::information(this, "Auto-rename",
                    "Auto-rename not needed — \"" + dirName + "\" is already clean.");
            } else {
                QString parentPath = QFileInfo(showPath).absolutePath();
                QString oldPath = parentPath + "/" + dirName;
                QString newPath = parentPath + "/" + cleaned;
                if (QFileInfo::exists(newPath)) {
                    QMessageBox::warning(this, "Auto-rename failed",
                        "A folder named \"" + cleaned + "\" already exists in this location.");
                } else if (renameShowFolder(oldPath, newPath)) {
                    triggerScan();
                } else {
                    QMessageBox::warning(this, "Auto-rename failed",
                        "Could not rename \"" + dirName + "\" to \"" + cleaned + "\".\n"
                        "The folder may be in use by another program.");
                }
            }
        } else if (chosen == revealAct) {
            ContextMenuHelper::revealInExplorer(showPath);
        } else if (chosen == copyAct) {
            ContextMenuHelper::copyToClipboard(showPath);
        } else if (moveActions.contains(chosen)) {
            moveShowToCategory(showPath, moveActions.value(chosen));
        } else if (chosen == setPosterAct) {
            QString file = QFileDialog::getOpenFileName(this, "Set poster",
                QString(), "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
            if (!file.isEmpty()) {
                QImage img(file);
                if (!img.isNull()) {
                    img.save(existingPoster, "JPEG", 92);
                    const QPixmap pix = QPixmap::fromImage(img);
                    if (!pix.isNull()) {
                        PosterCache::instance().put(videoPosterCacheKey(existingPoster), pix);
                        card->setThumbPixmap(pix);
                    }
                    triggerScan();
                }
            }
        } else if (chosen == removePosterAct) {
            QFile::remove(existingPoster);
            PosterCache::instance().remove(videoPosterCacheKey(existingPoster));
            triggerScan();
        } else if (chosen == fetchPosterAct) {
            const QString dirName = QDir(showPath).dirName();
            QString query = ScannerUtils::cleanMediaFolderTitle(dirName);
            // VIDEOS_FETCH_POSTER_FIX Phase 2C 2026-05-06 — strip trailing
            // "Season N" / "Series N" / "S0N" tokens from the search query.
            // cleanMediaFolderTitle re-appends season info because folder
            // IDENTITY needs the season suffix (catalog labels distinguish
            // "Sopranos Season 1" from "Sopranos Season 6"). But Cinemeta's
            // searchByTitle gets confused by the season suffix and returns
            // alphabetical/weak matches (Hemanth: "click on Community Season 1
            // returns 1 Litre of Tears, 1 vs. 100, 1-800-Missing — wrong shows").
            // The poster search wants the bare show title; strip-after-clean
            // preserves identity-correctness for every other caller of
            // cleanMediaFolderTitle while fixing the search-query case.
            static const QRegularExpression seasonSuffix(
                QStringLiteral("\\s*(?:Season|Series|Vol\\.?|Volume)\\s+\\d+\\s*$|\\s*S\\d{1,2}\\s*$"),
                QRegularExpression::CaseInsensitiveOption);
            query.replace(seasonSuffix, QString());
            query = query.trimmed();
            if (query.isEmpty()) query = dirName;
            const QString destPath = posterPath(showPath);
            const QPoint globalPos = strip->mapToGlobal(pos);

            if (!m_nam) m_nam = new QNetworkAccessManager(this);
            QPointer<TileCard> cardGuard(card);
            QPointer<VideosPage> selfGuard(this);
            QNetworkAccessManager* nam = m_nam;

            auto applyPoster = [cardGuard, destPath, nam, selfGuard](const QUrl& poster) {
                if (!selfGuard || !poster.isValid()) return;
                PosterFetcher::download(nam, poster, destPath, selfGuard,
                    [cardGuard, destPath, selfGuard](bool ok) {
                        if (!selfGuard) return;
                        if (ok) {
                            PosterCache::instance().decodeFileAsync(
                                videoPosterCacheKey(destPath), destPath, selfGuard,
                                [cardGuard, destPath](const QPixmap& pix) {
                                    if (!cardGuard) {
                                        return;
                                    }
                                    if (!pix.isNull()) {
                                        cardGuard->setThumbPixmap(pix);
                                    } else {
                                        cardGuard->setThumbPath(destPath);
                                    }
                                });
                            return;
                        }
                        QMessageBox::information(selfGuard, "Poster download failed",
                            "Could not download the poster image. Try again or pick "
                            "a different match.");
                    });
            };

            auto handleResults = [selfGuard, applyPoster, query, globalPos, nam](
                const QList<tankostream::addon::MetaItemPreview>& results) {
                if (!selfGuard) return;
                QList<tankostream::addon::MetaItemPreview> usable;
                usable.reserve(results.size());
                int withName = 0;
                int withPoster = 0;
                for (const auto& r : results) {
                    if (!r.name.isEmpty()) ++withName;
                    if (r.poster.isValid()) ++withPoster;
                    if (r.name.isEmpty()) continue;
                    if (!r.poster.isValid()) continue;
                    usable.append(r);
                }
                DebugLogBuffer::instance().info("videospage",
                    QStringLiteral("fetchPoster query='%1' results=%2 withName=%3 withPoster=%4 usable=%5")
                        .arg(query)
                        .arg(results.size())
                        .arg(withName)
                        .arg(withPoster)
                        .arg(usable.size()));
                if (usable.isEmpty()) {
                    QMessageBox::information(selfGuard, "No match found",
                        QStringLiteral("No matching poster found for \"%1\".").arg(query));
                    return;
                }
                if (usable.size() == 1) {
                    applyPoster(usable.first().poster);
                    return;
                }
                auto* picker = new PosterPickerPopover(selfGuard);
                QObject::connect(picker, &PosterPickerPopover::posterChosen,
                                 selfGuard,
                                 [applyPoster](const QUrl& url, const QString& /*name*/) {
                                     applyPoster(url);
                                 });
                picker->showAtGlobal(usable, globalPos, nam);
            };

            m_meta->searchByTitle(query, QStringLiteral("series"),
                [selfGuard, handleResults, query](
                    const QList<tankostream::addon::MetaItemPreview>& results,
                    const QString& /*error*/) {
                    if (!selfGuard) return;
                    if (results.isEmpty()) {
                        selfGuard->m_meta->searchByTitle(query, QStringLiteral("movie"),
                            [selfGuard, handleResults](
                                const QList<tankostream::addon::MetaItemPreview>& r2,
                                const QString& /*error*/) {
                                if (!selfGuard) return;
                                handleResults(r2);
                            });
                        return;
                    }
                    handleResults(results);
                });
        } else if (chosen == pastePosterAct) {
            QImage img = QApplication::clipboard()->image();
            if (!img.isNull()) {
                img.save(existingPoster, "JPEG", 92);
                const QPixmap pix = QPixmap::fromImage(img);
                if (!pix.isNull()) {
                    PosterCache::instance().put(videoPosterCacheKey(existingPoster), pix);
                    card->setThumbPixmap(pix);
                }
                triggerScan();
            }
        } else if (chosen == removeAct) {
            if (ContextMenuHelper::confirmRemove(this, "Remove from library",
                    "Remove this show from the library?\n" + showPath +
                    "\nFiles will not be deleted from disk.")) {
                triggerScan();
            }
        }
        menu->deleteLater();
    });
}

void VideosPage::setMetaAggregator(tankostream::stream::MetaAggregator* meta)
{
    m_meta = meta;
}

void VideosPage::setStreamDownloadIndex(StreamDownloadIndex* idx)
{
    m_downloadIndex = idx;

    // Forward to the scanner so it filters Stream-owned files at scan time.
    if (m_scanner)
        m_scanner->setStreamDownloadIndex(idx);

    if (!m_downloadIndex) return;

    // STREAM_DOWNLOADED_LIBRARY Phase 5 (2026-05-10) — debounced rescan when
    // the download-index changes (bulk completion landing N episodes;
    // Remove-from-Library evicting many entries). 500ms window collapses
    // batches into a single triggerScan call. Spec §8.3.
    if (!m_streamDownloadDebounce) {
        m_streamDownloadDebounce = new QTimer(this);
        m_streamDownloadDebounce->setSingleShot(true);
        m_streamDownloadDebounce->setInterval(500);
        connect(m_streamDownloadDebounce, &QTimer::timeout, this, [this]() {
            triggerScan();
        });
    }
    connect(m_downloadIndex, &StreamDownloadIndex::entriesChanged,
            this, [this]() {
                if (m_streamDownloadDebounce)
                    m_streamDownloadDebounce->start();  // restart the window
            }, Qt::QueuedConnection);
}

void VideosPage::activate()
{
    if (!m_hasScanned)
        triggerScan();
}

void VideosPage::triggerScan()
{
    // REPO_HYGIENE Phase 4 P4.3 (2026-04-26) — buffer rather than drop.
    if (m_scanning) {
        m_rescanPending = true;
        return;
    }
    m_scanning = true;
    m_rescanPending = false;

    QStringList roots = m_bridge->rootFolders("videos");
    if (roots.isEmpty()) {
        clearCategoryRows();
        clearContinueRows();
        m_listView->clear();
        m_showDurations.clear();
        m_fileToShowRoot.clear();
        m_showPathToName.clear();
        m_showsById.clear();
        m_allShows.clear();
        setGridRowsVisible(false);
        m_statusLabel->setText("Add a videos folder to get started");
        m_statusLabel->show();
        m_hasScanned = true;
        m_scanning = false;
        return;
    }

    if (!m_hasScanned) {
        // First scan: clear tiles, show scanning label for progressive loading
        clearCategoryRows();
        clearContinueRows();
        m_listView->clear();
        m_showDurations.clear();
        m_fileToShowRoot.clear();
        m_showPathToName.clear();
        m_showsById.clear();
        m_allShows.clear();
        m_statusLabel->setText("Scanning...");
        m_statusLabel->show();
        setGridRowsVisible(false);
    }
    // Rescan: keep old tiles visible — atomic swap happens in onScanFinished

    QMetaObject::invokeMethod(m_scanner, "scan", Qt::QueuedConnection,
                              Q_ARG(QStringList, roots));
}

void VideosPage::addShowTile(const ShowInfo& show)
{
    VideoCategoryStore store(m_bridge->store());
    const VideoCategory category = store.categoryFor(VideoCategoryStore::itemIdForShow(show));
    TileStrip* strip = m_categoryStrips.value(category, m_categoryStrips.value(VideoCategory::Miscellaneous));
    addShowTileToStrip(show, strip);
}

void VideosPage::addShowTileToStrip(const ShowInfo& show, TileStrip* strip)
{
    QString subtitle;
    if (show.episodeCount == 1)
        subtitle = formatSize(show.totalSizeBytes);
    else
        subtitle = QString::number(show.episodeCount) + " episodes \u00B7 " + formatSize(show.totalSizeBytes);

    // Check for user-set poster
    QString hash = QString(QCryptographicHash::hash(show.showPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QString poster = base + "/Tankoban/data/posters/" + hash + ".jpg";

    auto* card = new TileCard(QString(), show.showName, subtitle);
    applyPosterPathToCard(card, poster, this);
    card->setProperty("seriesPath", show.showPath);
    card->setProperty("seriesName", show.showName);
    card->setProperty("fileCount", show.episodeCount);
    card->setProperty("newestMtime", show.newestMtimeMs);
    card->setProperty("isLoose", show.isLoose);
    if (!show.files.isEmpty())
        card->setProperty("primaryFilePath", show.files.first().path);

    // Store scan-time durations for this show
    QMap<QString, double> durations;
    for (const auto& fe : show.files) {
        durations.insert(fe.path, fe.durationSec);
        m_fileToShowRoot.insert(fe.path, show.showPath);
    }
    m_showDurations.insert(show.showPath, durations);
    m_showPathToName.insert(show.showPath, show.showName);

    connect(card, &TileCard::clicked, this, [this, card]() {
        m_pendingClickPath = card->property("seriesPath").toString();
        m_pendingClickName = card->property("seriesName").toString();
        m_pendingIsPlay = false;
        m_pendingIsLoose = card->property("isLoose").toBool();
        m_clickTimer->start();
    });
    card->installEventFilter(this);
    if (strip)
        strip->addTile(card);

    // Also add to list view
    LibraryListView::ItemData listItem;
    listItem.name = show.showName;
    listItem.path = show.showPath;
    listItem.itemCount = show.episodeCount;
    listItem.lastModifiedMs = show.newestMtimeMs;
    m_listView->addItem(listItem);
}

void VideosPage::onShowFound(const ShowInfo& show)
{
    // On rescan: skip incremental tiles — atomic rebuild in onScanFinished
    if (m_hasScanned) return;

    // First scan: progressive loading
    if (m_statusLabel->isVisible()) {
        m_statusLabel->hide();
        setGridRowsVisible(true);
    }
    addShowTile(show);
}

void VideosPage::onScanFinished(const QList<ShowInfo>& allShows)
{
    m_hasScanned = true;
    m_scanning = false;
    m_allShows = allShows;
    m_showsById.clear();
    for (const auto& show : m_allShows)
        m_showsById.insert(VideoCategoryStore::itemIdForShow(show), show);

    VideoCategoryStore(m_bridge->store()).ensureAssignments(m_allShows);
    // REPO_HYGIENE Phase 4 P4.3 (2026-04-26) — fire pending rescan (set if
    // triggerScan was called mid-scan). Defer via single-shot timer to let
    // the rest of onScanFinished's UI updates settle before the next scan
    // queues to the scanner thread.
    if (m_rescanPending) {
        m_rescanPending = false;
        QTimer::singleShot(0, this, [this]() { triggerScan(); });
    }

    rebuildLibraryRows();

    if (allShows.isEmpty()) {
        setGridRowsVisible(false);
        m_statusLabel->setObjectName("LibraryEmptyLabel");
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setText("No videos found\nAdd a root folder via the + button or browse Sources for content");
        m_statusLabel->show();
    } else {
        m_statusLabel->hide();
        setGridRowsVisible(m_gridMode);
        sortCategoryRows();
    }

    refreshContinueStrip();
}

void VideosPage::onTileClicked(const QString& showPath, const QString& showName)
{
    m_showView->setFileDurations(m_showDurations.value(showPath));
    m_showView->showFolder(showPath, showName, posterPathFor(showPath));
    m_stack->setCurrentIndexAnimated(1);
}

void VideosPage::showGrid()
{
    m_stack->setCurrentIndexAnimated(0);
}

void VideosPage::clearCategoryRows()
{
    for (TileStrip* strip : m_categoryStrips)
        if (strip) strip->clear();
    for (QWidget* section : m_categorySections)
        if (section) section->hide();
}

void VideosPage::clearContinueRows()
{
    if (m_continueStrip) m_continueStrip->clear();
    if (m_continueSection) m_continueSection->hide();
}

void VideosPage::rebuildLibraryRows()
{
    clearCategoryRows();
    m_listView->clear();
    m_showDurations.clear();
    m_fileToShowRoot.clear();
    m_showPathToName.clear();

    for (const auto& show : m_allShows)
        addShowTile(show);

    sortCategoryRows();
    applySearch();
}

void VideosPage::sortCategoryRows()
{
    const QString key = m_sortCombo ? m_sortCombo->currentData().toString() : QStringLiteral("name_asc");
    for (TileStrip* strip : m_categoryStrips)
        if (strip) strip->sortTiles(key);
}

void VideosPage::setGridRowsVisible(bool visible)
{
    if (m_categoriesContainer)
        m_categoriesContainer->setVisible(visible);
    for (auto it = m_categorySections.begin(); it != m_categorySections.end(); ++it) {
        TileStrip* strip = m_categoryStrips.value(it.key());
        it.value()->setVisible(visible && strip && strip->visibleCount() > 0);
    }
}

void VideosPage::applyDensityToAllStrips(int val)
{
    for (TileStrip* strip : m_categoryStrips)
        if (strip) strip->setDensity(val);
    if (m_continueStrip)
        m_continueStrip->setDensity(val);
}

void VideosPage::moveShowToCategory(const QString& showId, VideoCategory category)
{
    if (showId.isEmpty())
        return;
    VideoCategoryStore(m_bridge->store()).setCategory(showId, category);
    refreshFromCategoryStore();
    emit categoryAssignmentsChanged();
}

void VideosPage::refreshFromCategoryStore()
{
    rebuildLibraryRows();
    refreshContinueStrip();
}

void VideosPage::toggleViewMode()
{
    m_gridMode = !m_gridMode;
    QSettings("Tankoban", "Tankoban").setValue("library_view_mode_videos",
                                                m_gridMode ? "grid" : "list");
    if (m_gridMode) {
        m_listView->hide();
        setGridRowsVisible(true);
        m_densitySlider->show();
        m_viewToggle->setText("\u2630");
    } else {
        setGridRowsVisible(false);
        m_listView->show();
        m_densitySlider->hide();
        m_viewToggle->setText("\u2637");
    }
}

void VideosPage::applySearch()
{
    QString query = m_searchBar->text();
    int visible = 0;
    for (TileStrip* strip : m_categoryStrips) {
        if (!strip) continue;
        strip->filterTiles(query);
        visible += strip->visibleCount();
    }
    m_listView->setTextFilter(query);

    if (visible == 0 && !query.trimmed().isEmpty()) {
        m_statusLabel->setObjectName("LibraryEmptyLabel");
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setText(
            QString("No results for \"%1\"").arg(query.trimmed()));
        m_statusLabel->show();
        setGridRowsVisible(false);
    } else if (visible > 0) {
        m_statusLabel->hide();
        setGridRowsVisible(m_gridMode);
    }
}

void VideosPage::executePendingClick()
{
    if (m_pendingIsPlay) {
        emit playVideo(m_pendingClickPath);
    } else {
        m_showView->setFileDurations(m_showDurations.value(m_pendingClickPath));
        m_showView->showFolder(m_pendingClickPath, m_pendingClickName,
                               posterPathFor(m_pendingClickPath), m_pendingIsLoose);
        m_stack->setCurrentIndexAnimated(1);
    }
    m_pendingClickPath.clear();
    m_pendingClickName.clear();
    m_pendingIsLoose = false;
}

bool VideosPage::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        // Double-click: cancel the pending 250ms timer and execute immediately
        if (m_clickTimer->isActive()) {
            m_clickTimer->stop();
            executePendingClick();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

QString VideosPage::formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024) + " KB";
    if (bytes < 1024LL * 1024 * 1024)
        return QString::number(bytes / (1024 * 1024)) + " MB";
    double gb = bytes / (1024.0 * 1024.0 * 1024.0);
    return QString::number(gb, 'f', 1) + " GB";
}

QString VideosPage::resolveShowPath(const QString& filePath) const
{
    // Normalize to forward-slash form for comparison. video_progress.json
    // stores paths in mixed slash conventions across watch sessions (forward
    // slashes from QFileDialog, backslashes from drag-drop / Explorer entry
    // points), and the same logical file can land in the JSON twice with
    // different separators. Without normalization those become two distinct
    // showMap keys downstream → Continue Watching shows duplicate tiles for
    // the same show. Comparison-only normalization; original strings are
    // preserved for the returned showPath so downstream lookups against
    // m_showPathToName still hit (scanner-side keys retain native form).
    const QString p = QDir::fromNativeSeparators(filePath);

    // Hot path: exact lookup. Try BOTH normalized and original forms because
    // the scanner may have inserted under either depending on entry point.
    if (auto it = m_fileToShowRoot.constFind(p); it != m_fileToShowRoot.constEnd())
        return it.value();
    if (p != filePath) {
        if (auto it = m_fileToShowRoot.constFind(filePath); it != m_fileToShowRoot.constEnd())
            return it.value();
    }

    // Scanner may not enumerate nested files in show.files (e.g. Season N
    // episodes under a parent series folder). Find the show root whose path
    // is the longest prefix of filePath — that's the matching top-level show.
    // Normalize candidates for comparison; return the original key so
    // downstream m_showPathToName lookups still hit.
    QString best;
    for (auto sit = m_showPathToName.constBegin(); sit != m_showPathToName.constEnd(); ++sit) {
        const QString& candidate = sit.key();
        if (candidate.isEmpty())
            continue;
        const QString candidateNorm = QDir::fromNativeSeparators(candidate);
        if (p.startsWith(candidateNorm + QLatin1Char('/'))) {
            if (candidate.length() > best.length())
                best = candidate;
        }
    }
    if (!best.isEmpty())
        return best;

    // Last-resort fallback. Default Qt would return the immediate parent
    // directory, which makes Continue Watching show one tile per Season
    // folder rather than one per show (Sopranos/Season 5/EpX and
    // Sopranos/Season 6/EpY become two tiles instead of one). Climb up
    // past season-shaped folder names so multi-season shows collapse to a
    // single show-root key. Patterns matched (case-insensitive, exact
    // segment): "Season N", "S01"-"S999", "Disc N", "Volume N", "Vol N",
    // "Part N", "CD N". Folder names that EMBED these tokens but aren't
    // exactly equal to them (e.g. "Community Season 1 [1080p ...]") are
    // intentionally NOT climbed — that folder IS the show root.
    static const QRegularExpression kSeasonLike(
        QStringLiteral("^(season\\s*\\d+|s\\d{1,3}|disc\\s*\\d+|volume\\s*\\d+|vol\\s*\\d+|part\\s*\\d+|cd\\s*\\d+)$"),
        QRegularExpression::CaseInsensitiveOption);
    QDir parent = QFileInfo(p).absoluteDir();
    while (kSeasonLike.match(parent.dirName()).hasMatch()) {
        if (!parent.cdUp())
            break;
    }
    return parent.absolutePath();
}

QList<VideosPage::ContinueItem> VideosPage::collectContinueItems()
{
    QList<ContinueItem> items;
    QJsonObject allProg = m_bridge->allProgress("videos");
    if (allProg.isEmpty())
        return items;

    QMap<QString, ContinueItem> showMap;

    for (auto it = allProg.begin(); it != allProg.end(); ++it) {
        QJsonObject prog = it.value().toObject();
        if (prog.value("finished").toBool())
            continue;

        double posSec = prog.value("positionSec").toDouble(0);
        double durSec = prog.value("durationSec").toDouble(0);
        if (posSec <= 0)
            continue;

        QString filePath = prog.value("path").toString();
        if (filePath.isEmpty() || !QFile::exists(filePath))
            continue;

        qint64 updatedAt = prog.value("updatedAt").toVariant().toLongLong();
        QString showPath = resolveShowPath(filePath);
        QFileInfo showInfo(showPath);
        const QString fallbackName = showInfo.isFile()
            ? ScannerUtils::cleanMediaFolderTitle(showInfo.completeBaseName())
            : ScannerUtils::cleanMediaFolderTitle(QDir(showPath).dirName());

        auto existing = showMap.find(showPath);
        if (existing == showMap.end()) {
            ContinueItem item;
            item.updatedAt = updatedAt;
            item.showPath = showPath;
            item.showName = m_showPathToName.value(showPath, fallbackName);
            item.resumeFilePath = filePath;
            item.resumePosSec = posSec;
            item.resumeDurSec = durSec;
            showMap.insert(showPath, item);
        } else if (updatedAt > existing->updatedAt) {
            existing->updatedAt = updatedAt;
            existing->resumeFilePath = filePath;
            existing->resumePosSec = posSec;
            existing->resumeDurSec = durSec;
        }
    }

    items = showMap.values();
    std::sort(items.begin(), items.end(), [](const ContinueItem& a, const ContinueItem& b) {
        return a.updatedAt > b.updatedAt;
    });
    return items;
}

void VideosPage::addContinueTile(TileStrip* strip, const ContinueItem& item)
{
    if (!strip)
        return;

    const QString poster = posterPathFor(item.showPath);
    const int pct = (item.resumeDurSec > 0)
        ? qBound(0, static_cast<int>(item.resumePosSec / item.resumeDurSec * 100), 100)
        : 0;

    auto* card = new TileCard(QString(), item.showName, QString::number(pct) + "%");
    applyPosterPathToCard(card, poster, this);
    card->setProperty("filePath", item.resumeFilePath);
    card->setProperty("seriesPath", item.showPath);
    connect(card, &TileCard::clicked, this, [this, card]() {
        m_pendingClickPath = card->property("filePath").toString();
        m_pendingClickName.clear();
        m_pendingIsPlay = true;
        m_clickTimer->start();
    });
    card->installEventFilter(this);
    strip->addTile(card);
}

void VideosPage::refreshContinueStrip()
{
    // Compute new state first so we can decide whether to hide vs. just
    // re-populate without toggling the section's visibility — the prior
    // clearContinueRows() unconditionally hide()'d, then we'd show() again
    // after re-adding tiles, causing a double layout reflow that flashed
    // as a visible vertical "shake" when the user cleared a single show
    // from a multi-show continue strip (Hemanth report 2026-05-05 ~22:50pm).
    QList<ContinueItem> items = collectContinueItems();
    if (items.isEmpty()) {
        if (m_continueStrip) m_continueStrip->clear();
        if (m_continueSection) m_continueSection->hide();
        return;
    }

    if (items.size() > 40)
        items = items.mid(0, 40);

    if (m_continueStrip) m_continueStrip->clear();
    for (const ContinueItem& item : items)
        addContinueTile(m_continueStrip, item);
    if (m_continueSection && !m_continueSection->isVisible())
        m_continueSection->show();
}

void VideosPage::refreshContinueStripLegacy()
{
    m_continueStrip->clear();

    QJsonObject allProg = m_bridge->allProgress("videos");
    if (allProg.isEmpty()) {
        m_continueSection->hide();
        return;
    }

    // ── Per-show dedup: one tile per show folder, pick best resume episode ──
    struct ShowContinue {
        qint64 updatedAt;       // most recent updatedAt across episodes
        QString showPath;       // parent folder = show identity
        QString showName;
        QString resumeFilePath; // best episode to resume
        double resumePosSec;
        double resumeDurSec;
    };
    QMap<QString, ShowContinue> showMap; // keyed by showPath

    for (auto it = allProg.begin(); it != allProg.end(); ++it) {
        QJsonObject prog = it.value().toObject();
        if (prog.value("finished").toBool())
            continue;
        double posSec = prog.value("positionSec").toDouble(0);
        double durSec = prog.value("durationSec").toDouble(0);
        if (posSec <= 0)
            continue;

        QString filePath = prog.value("path").toString();
        if (filePath.isEmpty() || !QFile::exists(filePath))
            continue;

        qint64 updatedAt = prog.value("updatedAt").toVariant().toLongLong();
        // Use scanner's show root; falls back to longest-prefix-match across
        // known shows for nested files (e.g. Sopranos/Season 6/episode.mkv).
        QString showPath = resolveShowPath(filePath);

        auto existing = showMap.find(showPath);
        if (existing == showMap.end()) {
            ShowContinue sc;
            sc.updatedAt = updatedAt;
            sc.showPath = showPath;
            sc.showName = m_showPathToName.value(showPath,
                ScannerUtils::cleanMediaFolderTitle(QDir(showPath).dirName()));
            sc.resumeFilePath = filePath;
            sc.resumePosSec = posSec;
            sc.resumeDurSec = durSec;
            showMap.insert(showPath, sc);
        } else {
            // Keep the most recently updated episode as resume target
            if (updatedAt > existing->updatedAt) {
                existing->updatedAt = updatedAt;
                existing->resumeFilePath = filePath;
                existing->resumePosSec = posSec;
                existing->resumeDurSec = durSec;
            }
        }
    }

    if (showMap.isEmpty()) {
        m_continueSection->hide();
        return;
    }

    // Sort by most recently updated, limit to 40 tiles
    QList<ShowContinue> items = showMap.values();
    std::sort(items.begin(), items.end(), [](const ShowContinue& a, const ShowContinue& b) {
        return a.updatedAt > b.updatedAt;
    });
    if (items.size() > 40)
        items = items.mid(0, 40);

    // Check for poster thumbnails
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);

    for (const auto& item : items) {
        // Use show poster if available
        QString hash = QString(QCryptographicHash::hash(item.showPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        QString poster = base + "/Tankoban/data/posters/" + hash + ".jpg";

        int pct = (item.resumeDurSec > 0)
            ? qBound(0, static_cast<int>(item.resumePosSec / item.resumeDurSec * 100), 100) : 0;
        QString subtitle = QString::number(pct) + "%";

        auto* card = new TileCard(QString(), item.showName, subtitle);
        applyPosterPathToCard(card, poster, this);
        card->setProperty("filePath", item.resumeFilePath);
        connect(card, &TileCard::clicked, this, [this, card]() {
            m_pendingClickPath = card->property("filePath").toString();
            m_pendingClickName.clear();
            m_pendingIsPlay = true;
            m_clickTimer->start();
        });
        card->installEventFilter(this);
        m_continueStrip->addTile(card);
    }

    m_continueSection->show();
}

void VideosPage::refreshContinueOnly()
{
    // During active playback, throttle to once per 5 seconds
    if (!m_continueRefreshThrottle->isActive()) {
        refreshContinueStrip();
        m_continueRefreshThrottle->start();
    }
}

// ── REPO_HYGIENE Phase 3 — dev-control bridge snapshot ──────────────────────

QJsonObject VideosPage::devSnapshot(int limit) const
{
    QJsonObject snap;
    snap["gridMode"]       = m_gridMode;
    snap["scanInProgress"] = m_scanning;
    snap["hasScanned"]     = m_hasScanned;
    snap["totalShowCount"] = static_cast<int>(m_showPathToName.size());

    QJsonArray tiles;
    int n = 0;
    for (auto it = m_showPathToName.cbegin();
         it != m_showPathToName.cend() && n < limit; ++it, ++n) {
        QJsonObject t;
        t["showPath"] = it.key();
        t["showName"] = it.value();
        tiles.append(t);
    }
    snap["tiles"] = tiles;

    if (m_showView && m_showView->isVisible())
        snap["activeShow"] = m_showView->devSnapshot();
    else
        snap["activeShow"] = QJsonValue::Null;

    return snap;
}

void VideosPage::restoreLayer(const tankoban::ui::LayerEntry& target)
{
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- no-op restore. VideosPage
    // has no deep state participating in the per-mode back stack today; its
    // ShowView sub-view is reached via cross-mode pill activation, not a
    // layer push. Phase 1+ may extend.
    Q_UNUSED(target);
}
