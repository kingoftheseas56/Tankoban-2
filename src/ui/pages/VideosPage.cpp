#include "VideosPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "ShowView.h"
#include "core/CoreBridge.h"
#include "core/VideosScanner.h"
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
    delete m_scanner;
}

void VideosPage::buildUI()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_stack = new FadingStackedWidget(this);

    // ── Grid view (index 0) — wrapped in scroll area ──
    auto* gridScroll = new QScrollArea();
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

    // ── 2. Continue Watching section ──
    m_continueSection = new QWidget(gridPage);
    auto* continueLayout = new QVBoxLayout(m_continueSection);
    continueLayout->setContentsMargins(0, 0, 0, 0);
    continueLayout->setSpacing(4);
    auto* continueLabel = new QLabel("CONTINUE WATCHING", m_continueSection);
    continueLabel->setStyleSheet("color: rgba(255,255,255,0.55); font-size: 12px; font-weight: bold; letter-spacing: 1px;");
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

    auto* showsLabel = new QLabel("SHOWS", showsRow);
    showsLabel->setStyleSheet("color: rgba(255,255,255,0.55); font-size: 12px; font-weight: bold; letter-spacing: 1px;");
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
        m_tileStrip->sortTiles(key);
    });
    showsLayout->addWidget(m_sortCombo);

    auto* densitySmall = new QLabel("A", showsRow);
    densitySmall->setStyleSheet("color: rgba(255,255,255,0.4); font-size: 10px;");
    showsLayout->addWidget(densitySmall);

    m_densitySlider = new QSlider(Qt::Horizontal, showsRow);
    m_densitySlider->setRange(0, 2);
    m_densitySlider->setFixedWidth(100);
    m_densitySlider->setFixedHeight(20);
    int savedDensity = QSettings("Tankoban", "Tankoban").value("grid_cover_size", 1).toInt();
    m_densitySlider->setValue(qBound(0, savedDensity, 2));
    connect(m_densitySlider, &QSlider::valueChanged, this, [this](int val) {
        QSettings("Tankoban", "Tankoban").setValue("grid_cover_size", val);
        m_tileStrip->setDensity(val);
    });
    showsLayout->addWidget(m_densitySlider);

    auto* densityLarge = new QLabel("A", showsRow);
    densityLarge->setStyleSheet("color: rgba(255,255,255,0.4); font-size: 16px;");
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

    m_tileStrip = new TileStrip(gridPage);
    m_tileStrip->hide();
    m_tileStrip->setDensity(savedDensity);
    gridLayout->addWidget(m_tileStrip);

    // List view (hidden by default — V-key toggles)
    m_listView = new LibraryListView(gridPage);
    m_listView->hide();
    connect(m_listView, &LibraryListView::itemActivated, this, [this](const QString& path) {
        m_showView->setFileDurations(m_showDurations.value(path));
        m_showView->showFolder(path, QFileInfo(path).fileName());
        m_stack->setCurrentIndexAnimated(1);
    });
    gridLayout->addWidget(m_listView, 1);

    // ── Helper: compute video ID for a file ──
    static const QStringList videoExts = {"*.mp4","*.mkv","*.avi","*.webm","*.mov","*.wmv","*.flv","*.m4v","*.ts","*.mpg","*.mpeg","*.ogv"};
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
        QStringList allFiles = ScannerUtils::walkFiles(showPath, videoExts);
        for (const auto& f : allFiles) {
            QString id = computeVideoId(f);
            QJsonObject prog = m_bridge->progress("videos", id);
            prog["finished"] = setFinished;
            m_bridge->saveProgress("videos", id, prog);
        }
        refreshContinueStrip();
    };

    // ── Grid context menu — single selection ──
    m_tileStrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tileStrip, &QWidget::customContextMenuRequested, this, [this, computeVideoId, markAllEpisodes, posterPath](const QPoint& pos) {
        // Check for multi-selection first
        auto selected = m_tileStrip->selectedTiles();
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

            auto* chosen = menu->exec(m_tileStrip->mapToGlobal(pos));
            if (chosen == playFirstAct) {
                QString path = selected.first()->property("seriesPath").toString();
                QStringList files = ScannerUtils::walkFiles(path, videoExts);
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
        auto* card = m_tileStrip->tileAt(pos);
        if (!card) return;

        QString showPath = card->property("seriesPath").toString();
        QString showName = card->property("seriesName").toString();

        QStringList allFiles = ScannerUtils::walkFiles(showPath, videoExts);
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
        auto* renameAct = menu->addAction("Rename...");
        auto* autoRenameAct = menu->addAction("Auto-rename");
        auto* revealAct = menu->addAction("Reveal in File Explorer");
        revealAct->setEnabled(!showPath.isEmpty());
        auto* copyAct = menu->addAction("Copy path");
        copyAct->setEnabled(!showPath.isEmpty());
        menu->addSeparator();
        auto* setPosterAct = menu->addAction("Set poster...");
        QString existingPoster = posterPath(showPath);
        auto* removePosterAct = menu->addAction("Remove poster");
        removePosterAct->setEnabled(QFile::exists(existingPoster));
        auto* pastePosterAct = menu->addAction("Paste image as poster");
        pastePosterAct->setEnabled(QApplication::clipboard()->mimeData()->hasImage());
        menu->addSeparator();
        auto* removeAct = ContextMenuHelper::addDangerAction(menu, "Remove from library...");
        removeAct->setEnabled(!showPath.isEmpty());

        auto* chosen = menu->exec(m_tileStrip->mapToGlobal(pos));
        if (chosen == playAct) {
            // Find best resume episode or first episode
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
            for (const auto& f : allFiles) {
                QString id = computeVideoId(f);
                QJsonObject prog = m_bridge->progress("videos", id);
                if (!prog.isEmpty()) {
                    prog.remove("positionSec");
                    prog.remove("finished");
                    m_bridge->saveProgress("videos", id, prog);
                }
            }
            refreshContinueStrip();
        } else if (chosen == renameAct) {
            QString dirName = QDir(showPath).dirName();
            QString newName = QInputDialog::getText(this, "Rename show", "New name:", QLineEdit::Normal, dirName);
            if (!newName.isEmpty() && newName != dirName) {
                QString parentPath = QFileInfo(showPath).absolutePath();
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
            ContextMenuHelper::revealInExplorer(showPath);
        } else if (chosen == copyAct) {
            ContextMenuHelper::copyToClipboard(showPath);
        } else if (chosen == setPosterAct) {
            QString file = QFileDialog::getOpenFileName(this, "Set poster",
                QString(), "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
            if (!file.isEmpty()) {
                QImage img(file);
                if (!img.isNull()) {
                    img.save(existingPoster, "JPEG", 92);
                    triggerScan();
                }
            }
        } else if (chosen == removePosterAct) {
            QFile::remove(existingPoster);
            triggerScan();
        } else if (chosen == pastePosterAct) {
            QImage img = QApplication::clipboard()->image();
            if (!img.isNull()) {
                img.save(existingPoster, "JPEG", 92);
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

        // Find the show folder this episode belongs to
        QFileInfo fi(filePath);
        QString showPath = fi.absolutePath();

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
            m_showView->showFolder(showPath, showName);
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

    // Escape: clear search if active, else navigate back from ShowView
    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (!m_searchBar->text().isEmpty()) {
            m_searchBar->clear();
        } else if (m_stack->currentIndex() == 1) {
            showGrid();
        } else {
            m_tileStrip->clearSelection();
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
        if (m_stack->currentIndex() == 0 && !m_searchBar->hasFocus())
            m_tileStrip->selectAll();
    });

    m_gridMode = QSettings("Tankoban", "Tankoban").value("library_view_mode_videos", "grid").toString() == "grid";
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
    m_stack->addWidget(m_showView);

    outerLayout->addWidget(m_stack, 1);
}

void VideosPage::activate()
{
    if (!m_hasScanned)
        triggerScan();
}

void VideosPage::triggerScan()
{
    if (m_scanning) return;
    m_scanning = true;

    QStringList roots = m_bridge->rootFolders("videos");
    if (roots.isEmpty()) {
        m_tileStrip->clear();
        m_listView->clear();
        m_showDurations.clear();
        m_fileToShowRoot.clear();
        m_showPathToName.clear();
        m_tileStrip->hide();
        m_statusLabel->setText("Add a videos folder to get started");
        m_statusLabel->show();
        m_hasScanned = true;
        m_scanning = false;
        return;
    }

    if (!m_hasScanned) {
        // First scan: clear tiles, show scanning label for progressive loading
        m_tileStrip->clear();
        m_listView->clear();
        m_showDurations.clear();
        m_fileToShowRoot.clear();
        m_showPathToName.clear();
        m_statusLabel->setText("Scanning...");
        m_statusLabel->show();
        m_tileStrip->hide();
    }
    // Rescan: keep old tiles visible — atomic swap happens in onScanFinished

    QMetaObject::invokeMethod(m_scanner, "scan", Qt::QueuedConnection,
                              Q_ARG(QStringList, roots));
}

void VideosPage::addShowTile(const ShowInfo& show)
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
    QString thumbPath = QFile::exists(poster) ? poster : QString();

    auto* card = new TileCard(thumbPath, show.showName, subtitle);
    card->setProperty("seriesPath", show.showPath);
    card->setProperty("seriesName", show.showName);
    card->setProperty("fileCount", show.episodeCount);
    card->setProperty("newestMtime", show.newestMtimeMs);
    card->setProperty("isLoose", show.isLoose);

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
    m_tileStrip->addTile(card);

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
        m_tileStrip->show();
    }
    addShowTile(show);
}

void VideosPage::onScanFinished(const QList<ShowInfo>& allShows)
{
    bool wasRescan = m_hasScanned;
    m_hasScanned = true;
    m_scanning = false;

    if (wasRescan) {
        // Atomic swap: clear old tiles, rebuild from complete list
        m_tileStrip->clear();
        m_listView->clear();
        m_showDurations.clear();
        m_fileToShowRoot.clear();
        m_showPathToName.clear();

        for (const auto& show : allShows)
            addShowTile(show);
    }

    if (allShows.isEmpty()) {
        m_tileStrip->hide();
        m_statusLabel->setObjectName("LibraryEmptyLabel");
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setText("No videos found\nAdd a root folder via the + button or browse Sources for content");
        m_statusLabel->show();
    } else {
        m_statusLabel->hide();
        m_tileStrip->show();
        m_tileStrip->sortTiles(m_sortCombo->currentData().toString());
    }

    refreshContinueStrip();
}

void VideosPage::onTileClicked(const QString& showPath, const QString& showName)
{
    m_showView->setFileDurations(m_showDurations.value(showPath));
    m_showView->showFolder(showPath, showName);
    m_stack->setCurrentIndexAnimated(1);
}

void VideosPage::showGrid()
{
    m_stack->setCurrentIndexAnimated(0);
}

void VideosPage::toggleViewMode()
{
    m_gridMode = !m_gridMode;
    QSettings("Tankoban", "Tankoban").setValue("library_view_mode_videos",
                                                m_gridMode ? "grid" : "list");
    if (m_gridMode) {
        m_listView->hide();
        m_tileStrip->show();
        m_densitySlider->show();
        m_viewToggle->setText("\u2630");
    } else {
        m_tileStrip->hide();
        m_listView->show();
        m_densitySlider->hide();
        m_viewToggle->setText("\u2637");
    }
}

void VideosPage::applySearch()
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

void VideosPage::executePendingClick()
{
    if (m_pendingIsPlay) {
        emit playVideo(m_pendingClickPath);
    } else {
        m_showView->setFileDurations(m_showDurations.value(m_pendingClickPath));
        m_showView->showFolder(m_pendingClickPath, m_pendingClickName,
                               QString(), m_pendingIsLoose);
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

void VideosPage::refreshContinueStrip()
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
        // Use scanner's show root, not episode's immediate parent dir
        QString showPath = m_fileToShowRoot.value(filePath,
                                                   QFileInfo(filePath).absolutePath());

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
        QString thumbPath = QFile::exists(poster) ? poster : QString();

        int pct = (item.resumeDurSec > 0)
            ? qBound(0, static_cast<int>(item.resumePosSec / item.resumeDurSec * 100), 100) : 0;
        QString subtitle = QString::number(pct) + "%";

        auto* card = new TileCard(thumbPath, item.showName, subtitle);
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
