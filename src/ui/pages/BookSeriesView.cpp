#include "BookSeriesView.h"
#include "core/ScannerUtils.h"
#include "core/CoreBridge.h"
#include "ui/ContextMenuHelper.h"

#include <QPushButton>
#include <QPixmap>
#include <QPainter>
#include <QHeaderView>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QStyleFactory>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QCollator>
#include <QFont>
#include <QColor>
#include <QPalette>
#include <QIcon>
#include <QInputDialog>
#include <QMessageBox>
#include <QFile>
#include <QShortcut>
#include <algorithm>

static const QStringList BOOK_EXTS = {
    "*.epub", "*.pdf", "*.mobi", "*.fb2", "*.azw3", "*.djvu", "*.txt"
};

enum Col { ColNum = 0, ColTitle, ColSize, ColRead, ColModified, ColCount };

// ─── Constructor ─────────────────────────────────────────────────────

BookSeriesView::BookSeriesView(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Top bar ──
    auto* topBar = new QWidget(this);
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(16, 8, 16, 0);
    topLayout->setSpacing(8);

    auto* backBtn = new QPushButton(QString::fromUtf8("\u2190  Books"), topBar);
    backBtn->setObjectName("SidebarAction");
    backBtn->setFixedHeight(30);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setStyleSheet(
        "QPushButton { text-align: left; color: rgba(255,255,255,0.72);"
        "  background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 8px; padding: 4px 12px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.08);"
        "  border-color: rgba(255,255,255,0.14); color: rgba(255,255,255,0.9); }");
    connect(backBtn, &QPushButton::clicked, this, [this]() { goBack(); });
    topLayout->addWidget(backBtn);

    m_forwardBtn = new QPushButton(QString::fromUtf8("\u2192"), topBar);
    m_forwardBtn->setFixedSize(28, 28);
    m_forwardBtn->setCursor(Qt::PointingHandCursor);
    m_forwardBtn->setEnabled(false);
    m_forwardBtn->setStyleSheet(
        "QPushButton { color: rgba(255,255,255,0.72);"
        "  background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 8px; font-size: 14px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.08); color: rgba(255,255,255,0.9); }"
        "QPushButton:disabled { color: rgba(255,255,255,0.2); background: transparent; border-color: rgba(255,255,255,0.04); }");
    connect(m_forwardBtn, &QPushButton::clicked, this, [this]() { goForward(); });
    topLayout->addWidget(m_forwardBtn);

    m_breadcrumbWidget = new QWidget(topBar);
    m_breadcrumbLayout = new QHBoxLayout(m_breadcrumbWidget);
    m_breadcrumbLayout->setContentsMargins(0, 0, 0, 0);
    m_breadcrumbLayout->setSpacing(4);
    m_breadcrumbLayout->addStretch();
    topLayout->addWidget(m_breadcrumbWidget, 1);

    m_searchBar = new QLineEdit(topBar);
    m_searchBar->setObjectName("DetailSearch");
    m_searchBar->setPlaceholderText(QString::fromUtf8("Search books\u2026"));
    m_searchBar->setClearButtonEnabled(true);
    m_searchBar->setFixedWidth(180);
    m_searchBar->setFixedHeight(28);
    m_searchBar->setStyleSheet(
        "QLineEdit { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px; color: #eee; padding: 2px 8px; font-size: 12px; }"
        "QLineEdit:focus { border: 1px solid rgba(255,255,255,0.3); }");
    connect(m_searchBar, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_searchText = text.trimmed().toLower();
        populateTable(m_seriesRootPath + (m_currentRel.isEmpty() ? "" : "/" + m_currentRel));
    });
    topLayout->addWidget(m_searchBar);

    m_sortCombo = new QComboBox(topBar);
    m_sortCombo->setObjectName("DetailSortCombo");
    m_sortCombo->setFixedWidth(140);
    m_sortCombo->setFixedHeight(28);
    m_sortCombo->addItem("Title A\u2192Z",   "title_asc");
    m_sortCombo->addItem("Title Z\u2192A",   "title_desc");
    m_sortCombo->addItem("Size \u2191",       "size_asc");
    m_sortCombo->addItem("Size \u2193",       "size_desc");
    m_sortCombo->addItem("Modified \u2191",   "modified_asc");
    m_sortCombo->addItem("Modified \u2193",   "modified_desc");
    m_sortCombo->addItem("Number \u2191",     "number_asc");
    m_sortCombo->addItem("Number \u2193",     "number_desc");
    m_sortCombo->setStyleSheet(
        "QComboBox#DetailSortCombo {"
        "  background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 6px; color: #ccc; padding: 2px 8px; font-size: 12px; }"
        "QComboBox#DetailSortCombo:hover { border-color: rgba(255,255,255,0.2); }"
        "QComboBox#DetailSortCombo::drop-down { border: none; }"
        "QComboBox#DetailSortCombo QAbstractItemView {"
        "  background: #1e1e1e; color: #ccc; selection-background-color: rgba(255,255,255,0.1);"
        "  border: 1px solid rgba(255,255,255,0.12); }");
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        m_sortKey = m_sortCombo->itemData(idx).toString();
        populateTable(m_seriesRootPath + (m_currentRel.isEmpty() ? "" : "/" + m_currentRel));
    });
    topLayout->addWidget(m_sortCombo);

    layout->addWidget(topBar);

    // ── Continue reading bar (40px, groundwork spec) ──
    m_continueBar = new QWidget(this);
    m_continueBar->setFixedHeight(40);
    m_continueBar->setContextMenuPolicy(Qt::CustomContextMenu);
    auto* contBarLayout = new QHBoxLayout(m_continueBar);
    contBarLayout->setContentsMargins(16, 4, 16, 4);
    contBarLayout->setSpacing(10);

    auto* contLabel = new QLabel("Continue reading", m_continueBar);
    contLabel->setObjectName("ContinueBarTitle");
    contLabel->setStyleSheet("color: #c7a76b; font-weight: bold; font-size: 13px;");
    contBarLayout->addWidget(contLabel);

    m_continueTitle = new QLabel(m_continueBar);
    m_continueTitle->setObjectName("ContinueBarItem");
    m_continueTitle->setMinimumWidth(60);
    m_continueTitle->setStyleSheet("color: rgba(255,255,255,0.72); font-size: 13px;");
    contBarLayout->addWidget(m_continueTitle, 1);

    m_continueProgress = new QProgressBar(m_continueBar);
    m_continueProgress->setFixedSize(100, 6);
    m_continueProgress->setTextVisible(false);
    m_continueProgress->setRange(0, 100);
    m_continueProgress->setStyleSheet(
        "QProgressBar { background: rgba(255,255,255,0.08); border: none; border-radius: 3px; }"
        "QProgressBar::chunk { background: #94a3b8; border-radius: 3px; }");
    contBarLayout->addWidget(m_continueProgress);

    m_continuePctLabel = new QLabel(m_continueBar);
    m_continuePctLabel->setObjectName("Subtle");
    m_continuePctLabel->setFixedWidth(36);
    m_continuePctLabel->setAlignment(Qt::AlignCenter);
    m_continuePctLabel->setStyleSheet("color: rgba(255,255,255,0.5); font-size: 12px;");
    contBarLayout->addWidget(m_continuePctLabel);

    m_continueBtn = new QPushButton("Read", m_continueBar);
    m_continueBtn->setObjectName("ContinueBarBtn");
    m_continueBtn->setFixedSize(60, 26);
    m_continueBtn->setCursor(Qt::PointingHandCursor);
    m_continueBtn->setStyleSheet(
        "QPushButton { color: #eee; background: rgba(255,255,255,0.07);"
        "  border: 1px solid rgba(255,255,255,0.12); border-radius: 6px;"
        "  padding: 4px 8px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.12); }");
    connect(m_continueBtn, &QPushButton::clicked, this, [this]() {
        if (!m_continueFilePath.isEmpty())
            emit bookSelected(m_continueFilePath);
    });
    contBarLayout->addWidget(m_continueBtn);

    // Right-click context menu on continue bar
    connect(m_continueBar, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (m_continueFilePath.isEmpty()) return;
        auto* menu = ContextMenuHelper::createMenu(this);
        auto* readAct = menu->addAction("Read");
        menu->addSeparator();
        auto* resetAct = menu->addAction("Reset progress");
        menu->addSeparator();
        auto* revealAct = menu->addAction("Reveal in File Explorer");
        auto* copyAct = menu->addAction("Copy path");
        auto* chosen = menu->exec(m_continueBar->mapToGlobal(pos));
        if (chosen == readAct) {
            emit bookSelected(m_continueFilePath);
        } else if (chosen == resetAct) {
            if (m_bridge) {
                QString key = QString(QCryptographicHash::hash(
                    m_continueFilePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                m_bridge->clearProgress("books", key);
                buildContinueBar();
                populateTable(m_seriesRootPath + (m_currentRel.isEmpty() ? "" : "/" + m_currentRel));
            }
        } else if (chosen == revealAct) {
            ContextMenuHelper::revealInExplorer(m_continueFilePath);
        } else if (chosen == copyAct) {
            ContextMenuHelper::copyToClipboard(m_continueFilePath);
        }
        menu->deleteLater();
    });

    m_continueBar->hide();
    layout->addWidget(m_continueBar);

    // ── Content row: cover panel + table ──
    auto* contentRow = new QWidget(this);
    auto* contentLayout = new QHBoxLayout(contentRow);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_coverLabel = new QLabel(contentRow);
    m_coverLabel->setFixedWidth(240);
    m_coverLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_coverLabel->setStyleSheet("background: transparent; padding: 16px;");
    m_coverLabel->setMinimumHeight(200);
    contentLayout->addWidget(m_coverLabel);

    // Table
    m_table = new QTableWidget(contentRow);
    m_table->setObjectName("FolderDetailTable");
    m_table->setStyle(QStyleFactory::create("Fusion"));
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setMouseTracking(true);
    m_table->setMinimumHeight(200);

    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels({"#", "BOOK", "SIZE", "READ", "MODIFIED"});

    auto* hdr = m_table->horizontalHeader();
    hdr->setSectionResizeMode(ColNum,      QHeaderView::Fixed);
    hdr->setSectionResizeMode(ColTitle,    QHeaderView::Stretch);
    hdr->setSectionResizeMode(ColSize,     QHeaderView::Fixed);
    hdr->setSectionResizeMode(ColRead,     QHeaderView::Fixed);
    hdr->setSectionResizeMode(ColModified, QHeaderView::Fixed);
    m_table->setColumnWidth(ColNum,      42);
    m_table->setColumnWidth(ColSize,     90);
    m_table->setColumnWidth(ColRead,     80);
    m_table->setColumnWidth(ColModified, 132);
    hdr->setMinimumSectionSize(42);

    QPalette pal = m_table->palette();
    pal.setColor(QPalette::Highlight, QColor(192, 200, 212, 36));
    pal.setColor(QPalette::HighlightedText, QColor("#eeeeee"));
    m_table->setPalette(pal);

    m_table->setStyleSheet(
        "QTableWidget#FolderDetailTable {"
        "  background: transparent;"
        "  alternate-background-color: rgba(255,255,255,0.02);"
        "  border: none; outline: none;"
        "  color: rgba(238,238,238,0.86);"
        "  font-size: 12px;"
        "}"
        "QTableWidget#FolderDetailTable::item {"
        "  padding: 0 8px;"
        "}"
        "QTableWidget#FolderDetailTable::item:selected {"
        "  background: rgba(192,200,212,36);"
        "  color: #eeeeee;"
        "}"
        "QHeaderView::section {"
        "  background: transparent;"
        "  color: rgba(255,255,255,0.45);"
        "  font-size: 11px; font-weight: bold;"
        "  border: none;"
        "  border-bottom: 1px solid rgba(255,255,255,0.08);"
        "  padding: 6px 8px;"
        "}");

    // Double-click to open
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto* item = m_table->item(row, ColTitle);
        if (!item) return;

        bool isFolder = item->data(FolderRowRole).toBool();
        if (isFolder) {
            QString rel = item->data(FolderRelRole).toString();
            if (rel.isEmpty() && !m_currentRel.isEmpty()) {
                int sep = m_currentRel.lastIndexOf('/');
                navigateTo(sep > 0 ? m_currentRel.left(sep) : QString());
            } else if (!rel.isEmpty()) {
                navigateTo(rel);
            }
        } else {
            QString filePath = item->data(FilePathRole).toString();
            if (!filePath.isEmpty())
                emit bookSelected(filePath);
        }
    });

    // Context menu — file rows and folder rows
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int row = m_table->rowAt(pos.y());
        if (row < 0) return;

        auto* titleItem = m_table->item(row, ColTitle);
        if (!titleItem) return;

        bool isFolder = titleItem->data(FolderRowRole).toBool();
        auto* menu = ContextMenuHelper::createMenu(this);

        if (isFolder) {
            // ── Folder row context menu (Batch 6) ──
            QString relPath = titleItem->data(FolderRelRole).toString();
            QString folderPath = m_seriesRootPath;
            if (!relPath.isEmpty())
                folderPath += "/" + relPath;

            auto* openAct = menu->addAction("Open folder");
            auto* revealAct = menu->addAction("Reveal in File Explorer");
            auto* copyAct = menu->addAction("Copy folder path");

            auto* chosen = menu->exec(m_table->viewport()->mapToGlobal(pos));
            if (chosen == openAct) {
                navigateTo(relPath);
            } else if (chosen == revealAct) {
                ContextMenuHelper::revealInExplorer(folderPath);
            } else if (chosen == copyAct) {
                ContextMenuHelper::copyToClipboard(folderPath);
            }
        } else {
            // ── File row context menu (Batch 5) ──
            QString filePath = titleItem->data(FilePathRole).toString();

            auto* openAct = menu->addAction("Open");
            menu->addSeparator();
            auto* revealAct = menu->addAction("Reveal in File Explorer");
            revealAct->setEnabled(!filePath.isEmpty());
            auto* copyAct = menu->addAction("Copy path");
            copyAct->setEnabled(!filePath.isEmpty());
            auto* setCoverAct = menu->addAction("Set as series cover");
            setCoverAct->setEnabled(!filePath.isEmpty());
            menu->addSeparator();
            auto* renameAct = menu->addAction("Rename...");

            // Mark read/unread toggle
            bool isFinished = false;
            QString progKey;
            if (m_bridge && !filePath.isEmpty()) {
                progKey = QString(QCryptographicHash::hash(
                    filePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                QJsonObject prog = m_bridge->progress("books", progKey);
                isFinished = prog.value("finished").toBool();
            }
            auto* markAct = menu->addAction(isFinished ? "Mark as unread" : "Mark as read");

            auto* resetAct = menu->addAction("Reset progress");
            resetAct->setEnabled(!progKey.isEmpty());
            menu->addSeparator();
            auto* removeAct = menu->addAction("Remove from library...");

            auto* chosen = menu->exec(m_table->viewport()->mapToGlobal(pos));
            if (chosen == openAct) {
                emit bookSelected(filePath);
            } else if (chosen == revealAct) {
                ContextMenuHelper::revealInExplorer(filePath);
            } else if (chosen == copyAct) {
                ContextMenuHelper::copyToClipboard(filePath);
            } else if (chosen == setCoverAct) {
                // Extract cover from this book's per-file thumbnail and overwrite series thumbnail
                if (m_bridge && !filePath.isEmpty()) {
                    QString thumbsDir = m_bridge->dataDir() + "/thumbs";
                    // Per-file cover key
                    QFileInfo cfi(filePath);
                    QString fileKey = QString(QCryptographicHash::hash(
                        (filePath + "::" + QString::number(cfi.size()) + "::" +
                         QString::number(cfi.lastModified().toMSecsSinceEpoch())).toUtf8(),
                        QCryptographicHash::Sha1).toHex().left(20));
                    QString fileCover = thumbsDir + "/" + fileKey + ".jpg";
                    // Series cover key
                    QString seriesHash = QString(QCryptographicHash::hash(
                        m_seriesRootPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                    QString seriesThumb = thumbsDir + "/" + seriesHash + ".jpg";
                    if (QFile::exists(fileCover)) {
                        QFile::remove(seriesThumb);
                        QFile::copy(fileCover, seriesThumb);
                        // Refresh cover label
                        QPixmap pix(seriesThumb);
                        if (!pix.isNull())
                            m_coverLabel->setPixmap(pix.scaled(208, 320,
                                Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    }
                }
            } else if (chosen == renameAct) {
                QFileInfo fi(filePath);
                QString newName = QInputDialog::getText(this, "Rename",
                    "New name:", QLineEdit::Normal, fi.completeBaseName());
                if (!newName.isEmpty() && newName != fi.completeBaseName()) {
                    QString newPath = fi.absolutePath() + "/" + newName + "." + fi.suffix();
                    if (QFile::rename(filePath, newPath)) {
                        populateTable(m_seriesRootPath +
                            (m_currentRel.isEmpty() ? "" : "/" + m_currentRel));
                    } else {
                        QMessageBox::warning(this, "Rename failed",
                            "Could not rename \"" + fi.fileName() + "\".\n"
                            "The file may be in use by another program.");
                    }
                }
            } else if (chosen == markAct) {
                if (m_bridge && !progKey.isEmpty()) {
                    QJsonObject prog = m_bridge->progress("books", progKey);
                    prog["finished"] = !isFinished;
                    m_bridge->saveProgress("books", progKey, prog);
                    populateTable(m_seriesRootPath +
                        (m_currentRel.isEmpty() ? "" : "/" + m_currentRel));
                    buildContinueBar();
                }
            } else if (chosen == resetAct) {
                if (m_bridge && !progKey.isEmpty()) {
                    m_bridge->clearProgress("books", progKey);
                    populateTable(m_seriesRootPath +
                        (m_currentRel.isEmpty() ? "" : "/" + m_currentRel));
                    buildContinueBar();
                }
            } else if (chosen == removeAct) {
                auto reply = QMessageBox::question(this, "Remove from library",
                    QString("Remove this book from the library?\n%1\nThe file will not be deleted from disk.").arg(filePath),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (reply == QMessageBox::Yes) {
                    // Remove progress, refresh table
                    if (m_bridge && !progKey.isEmpty())
                        m_bridge->clearProgress("books", progKey);
                    populateTable(m_seriesRootPath +
                        (m_currentRel.isEmpty() ? "" : "/" + m_currentRel));
                    buildContinueBar();
                }
            }
        }
        menu->deleteLater();
    });

    contentLayout->addWidget(m_table, 1);
    layout->addWidget(contentRow, 1);

    // ── Navigation shortcuts ──
    auto* backShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
    connect(backShortcut, &QShortcut::activated, this, [this]() { goBack(); });
    auto* fwdShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
    connect(fwdShortcut, &QShortcut::activated, this, [this]() { goForward(); });
    auto* bsShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    connect(bsShortcut, &QShortcut::activated, this, [this]() { goBack(); });
}

// ─── Public API ──────────────────────────────────────────────────────

void BookSeriesView::showSeries(const QString& seriesPath, const QString& seriesName,
                                const QString& coverThumbPath)
{
    m_seriesRootPath = seriesPath;
    m_seriesRootName = seriesName;
    m_currentRel.clear();
    m_searchBar->clear();
    m_navHistory.clear();
    m_navHistory.append(QString()); // root is first entry
    m_navIndex = 0;
    m_forwardBtn->setEnabled(false);

    // Set cover thumbnail
    bool hasCover = false;
    if (!coverThumbPath.isEmpty()) {
        QPixmap pix(coverThumbPath);
        if (!pix.isNull()) {
            m_coverLabel->setPixmap(pix.scaled(208, 320,
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
            hasCover = true;
        }
    }
    if (!hasCover) {
        QPixmap ph(208, 320);
        ph.fill(QColor(30, 30, 30));
        QPainter p(&ph);
        p.setPen(QColor(80, 80, 80));
        QFont font;
        font.setPixelSize(64);
        font.setBold(true);
        p.setFont(font);
        QString initial = seriesName.isEmpty() ? "?" : seriesName.left(1).toUpper();
        p.drawText(QRect(0, 60, 208, 120), Qt::AlignCenter, initial);
        p.setPen(QColor(160, 160, 160));
        font.setPixelSize(14);
        p.setFont(font);
        p.drawText(QRect(8, 220, 192, 60), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, seriesName);
        p.end();
        m_coverLabel->setPixmap(ph);
    }
    m_coverLabel->show();

    buildBreadcrumb();
    buildContinueBar();
    populateTable(seriesPath);
}

// ─── Navigation ──────────────────────────────────────────────────────

void BookSeriesView::navigateTo(const QString& relPath)
{
    // Push to history — truncate any forward history beyond current index
    if (m_navIndex >= 0 && m_navIndex < m_navHistory.size() - 1)
        m_navHistory = m_navHistory.mid(0, m_navIndex + 1);
    m_navHistory.append(relPath);
    m_navIndex = m_navHistory.size() - 1;
    m_forwardBtn->setEnabled(false);

    m_currentRel = relPath;
    buildBreadcrumb();
    buildContinueBar();
    QString absPath = m_seriesRootPath;
    if (!relPath.isEmpty())
        absPath += "/" + relPath;
    populateTable(absPath);
}

void BookSeriesView::goBack()
{
    if (m_navIndex > 0) {
        m_navIndex--;
        m_currentRel = m_navHistory.at(m_navIndex);
        m_forwardBtn->setEnabled(m_navIndex < m_navHistory.size() - 1);
        buildBreadcrumb();
        buildContinueBar();
        QString absPath = m_seriesRootPath;
        if (!m_currentRel.isEmpty())
            absPath += "/" + m_currentRel;
        populateTable(absPath);
    } else {
        emit backRequested();
    }
}

void BookSeriesView::goForward()
{
    if (m_navIndex < m_navHistory.size() - 1) {
        m_navIndex++;
        m_currentRel = m_navHistory.at(m_navIndex);
        m_forwardBtn->setEnabled(m_navIndex < m_navHistory.size() - 1);
        buildBreadcrumb();
        buildContinueBar();
        QString absPath = m_seriesRootPath;
        if (!m_currentRel.isEmpty())
            absPath += "/" + m_currentRel;
        populateTable(absPath);
    }
}

// ─── Breadcrumb ──────────────────────────────────────────────────────

void BookSeriesView::buildBreadcrumb()
{
    while (m_breadcrumbLayout->count()) {
        auto* item = m_breadcrumbLayout->takeAt(0);
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }

    QStringList parts;
    parts << m_seriesRootName;
    if (!m_currentRel.isEmpty()) {
        for (const auto& p : m_currentRel.split('/', Qt::SkipEmptyParts))
            parts << p;
    }

    for (int i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            auto* sep = new QLabel(">", m_breadcrumbWidget);
            sep->setStyleSheet("color: #666; font-size: 13px;");
            m_breadcrumbLayout->addWidget(sep);
        }

        bool isLast = (i == parts.size() - 1);
        if (isLast) {
            auto* lbl = new QLabel(parts[i], m_breadcrumbWidget);
            lbl->setStyleSheet("color: #ccc; font-weight: bold; font-size: 13px;");
            m_breadcrumbLayout->addWidget(lbl);
        } else {
            auto* btn = new QPushButton(parts[i], m_breadcrumbWidget);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFlat(true);
            btn->setStyleSheet(
                "QPushButton { color: #999; font-size: 13px; padding: 2px 4px; border: none; }"
                "QPushButton:hover { color: #ddd; text-decoration: underline; }");
            QString targetRel;
            if (i > 0) {
                QStringList relParts;
                for (int j = 1; j <= i; ++j)
                    relParts << parts[j];
                targetRel = relParts.join('/');
            }
            connect(btn, &QPushButton::clicked, this, [this, targetRel]() {
                navigateTo(targetRel);
            });
            m_breadcrumbLayout->addWidget(btn);
        }
    }
    m_breadcrumbLayout->addStretch();
}

// ─── Table Population ────────────────────────────────────────────────

void BookSeriesView::populateTable(const QString& folderPath)
{
    m_table->setRowCount(0);

    QCollator collator;
    collator.setNumericMode(true);

    QFont boldFont = m_table->font();
    boldFont.setBold(true);

    QColor baseBg = m_table->palette().color(QPalette::Base);
    QColor folderBg(
        qMin(baseBg.red()   + 12, 255),
        qMin(baseBg.green() + 12, 255),
        qMin(baseBg.blue()  + 14, 255));

    QIcon folderIcon(":/icons/folder.svg");
    int row = 0;

    // ".." up-row
    if (!m_currentRel.isEmpty()) {
        m_table->insertRow(row);
        m_table->setRowHeight(row, 38);
        for (int c = 0; c < ColCount; ++c) {
            auto* item = new QTableWidgetItem();
            item->setFont(boldFont);
            item->setBackground(folderBg);
            m_table->setItem(row, c, item);
        }
        auto* titleItem = m_table->item(row, ColTitle);
        titleItem->setText(QString::fromUtf8("\u2190  .."));
        titleItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        titleItem->setData(FolderRowRole, true);
        titleItem->setData(FolderRelRole, QString());
        m_table->item(row, ColNum)->setTextAlignment(Qt::AlignCenter);
        ++row;
    }

    // Folder rows
    QStringList subdirs = ScannerUtils::listImmediateSubdirs(folderPath);
    std::sort(subdirs.begin(), subdirs.end(), [&collator](const QString& a, const QString& b) {
        return collator.compare(QDir(a).dirName(), QDir(b).dirName()) < 0;
    });

    for (const auto& subdir : subdirs) {
        QStringList files = ScannerUtils::walkFiles(subdir, BOOK_EXTS);
        if (files.isEmpty()) continue;

        QString dirName = QDir(subdir).dirName();
        QString relPath = m_currentRel.isEmpty() ? dirName : m_currentRel + "/" + dirName;
        QString countText = QString("    (%1 %2)")
            .arg(files.size())
            .arg(files.size() == 1 ? "book" : "books");

        m_table->insertRow(row);
        m_table->setRowHeight(row, 38);
        for (int c = 0; c < ColCount; ++c) {
            auto* item = new QTableWidgetItem();
            item->setFont(boldFont);
            item->setBackground(folderBg);
            m_table->setItem(row, c, item);
        }
        auto* titleItem = m_table->item(row, ColTitle);
        titleItem->setIcon(folderIcon);
        titleItem->setText(dirName + countText);
        titleItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        titleItem->setData(FolderRowRole, true);
        titleItem->setData(FolderRelRole, relPath);
        m_table->item(row, ColNum)->setTextAlignment(Qt::AlignCenter);
        ++row;
    }

    // File rows
    QDir dir(folderPath);
    auto fileInfos = dir.entryInfoList(BOOK_EXTS, QDir::Files);

    if (!m_searchText.isEmpty()) {
        QList<QFileInfo> filtered;
        for (const auto& fi : fileInfos) {
            if (fi.completeBaseName().toLower().contains(m_searchText))
                filtered.append(fi);
        }
        fileInfos = filtered;
    }

    QString sortBase = m_sortKey.section('_', 0, 0);
    bool desc = m_sortKey.endsWith("_desc");
    if (sortBase == "title" || sortBase == "number") {
        std::sort(fileInfos.begin(), fileInfos.end(),
                  [&collator, desc](const QFileInfo& a, const QFileInfo& b) {
            int cmp = collator.compare(a.fileName(), b.fileName());
            return desc ? cmp > 0 : cmp < 0;
        });
    } else if (sortBase == "size") {
        std::sort(fileInfos.begin(), fileInfos.end(),
                  [desc](const QFileInfo& a, const QFileInfo& b) {
            return desc ? a.size() > b.size() : a.size() < b.size();
        });
    } else if (sortBase == "modified") {
        std::sort(fileInfos.begin(), fileInfos.end(),
                  [desc](const QFileInfo& a, const QFileInfo& b) {
            return desc ? a.lastModified() > b.lastModified() : a.lastModified() < b.lastModified();
        });
    }

    int fileNum = 1;
    for (const auto& fi : fileInfos) {
        m_table->insertRow(row);
        m_table->setRowHeight(row, 34);

        auto* numItem = new QTableWidgetItem(QString::number(fileNum));
        numItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, ColNum, numItem);

        QString displayName = ScannerUtils::cleanMediaFolderTitle(fi.completeBaseName());
        auto* titleItem = new QTableWidgetItem(displayName);
        titleItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        titleItem->setData(FolderRowRole, false);
        titleItem->setData(FilePathRole, fi.absoluteFilePath());
        m_table->setItem(row, ColTitle, titleItem);

        auto* sizeItem = new QTableWidgetItem(formatSize(fi.size()));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, ColSize, sizeItem);

        // READ column — 12x12 progress icons per groundwork spec
        auto* readItem = new QTableWidgetItem();
        readItem->setTextAlignment(Qt::AlignCenter);
        if (m_bridge) {
            QString progKey = QString(QCryptographicHash::hash(
                fi.absoluteFilePath().toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            QJsonObject prog = m_bridge->progress("books", progKey);
            bool finished = prog.value("finished").toBool();
            int page = prog.value("page").toInt(-1);
            int pageCount = prog.value("pageCount").toInt(0);
            if (finished) {
                // Green circle with white checkmark
                QPixmap pm(12, 12);
                pm.fill(Qt::transparent);
                QPainter p(&pm);
                p.setRenderHint(QPainter::Antialiasing);
                p.setBrush(QColor("#4CAF50"));
                p.setPen(Qt::NoPen);
                p.drawEllipse(1, 1, 10, 10);
                p.setPen(QPen(Qt::white, 1.4));
                QFont ckFont;
                ckFont.setPixelSize(9);
                ckFont.setBold(true);
                p.setFont(ckFont);
                p.drawText(QRect(0, 0, 12, 12), Qt::AlignCenter, QString::fromUtf8("\u2713"));
                p.end();
                readItem->setIcon(QIcon(pm));
                readItem->setText("");
            } else if (page >= 0 && pageCount > 0) {
                // Slate circle, percentage text
                QPixmap pm(12, 12);
                pm.fill(Qt::transparent);
                QPainter p(&pm);
                p.setRenderHint(QPainter::Antialiasing);
                p.setBrush(QColor("#94a3b8"));
                p.setPen(Qt::NoPen);
                p.drawEllipse(1, 1, 10, 10);
                p.end();
                int pct = qBound(0, static_cast<int>((page + 1) * 100.0 / pageCount), 100);
                readItem->setIcon(QIcon(pm));
                readItem->setText(QString::number(pct) + "%");
            } else {
                readItem->setText("-");
            }
        } else {
            readItem->setText("-");
        }
        m_table->setItem(row, ColRead, readItem);

        auto* dateItem = new QTableWidgetItem(
            fi.lastModified().isValid() ? fi.lastModified().toString("MM/dd/yyyy") : "-");
        dateItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, ColModified, dateItem);

        ++row;
        ++fileNum;
    }
}

void BookSeriesView::buildContinueBar()
{
    m_continueBar->hide();
    m_continueFilePath.clear();

    if (!m_bridge) return;

    QJsonObject allProg = m_bridge->allProgress("books");
    if (allProg.isEmpty()) return;

    static const QStringList bookExts = {"*.epub","*.pdf","*.mobi","*.fb2","*.azw3","*.djvu","*.txt"};
    qint64 bestAt = -1;
    QString bestPath;
    QString bestTitle;

    // Scope to current subfolder and its descendants (not entire series root) —
    // groundwork: continue reading shows the last-read file FROM THIS FOLDER
    QString scopePath = m_seriesRootPath;
    if (!m_currentRel.isEmpty())
        scopePath = m_seriesRootPath + "/" + m_currentRel;
    QStringList allFiles = ScannerUtils::walkFiles(scopePath, bookExts);
    for (const auto& fullPath : allFiles) {
        QString key = QString(QCryptographicHash::hash(
            fullPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        QJsonObject prog = allProg.value(key).toObject();
        if (prog.isEmpty()) continue;
        if (prog.value("finished").toBool()) continue;
        int page = prog.value("page").toInt(-1);
        if (page < 0) continue;

        qint64 updatedAt = prog.value("updatedAt").toVariant().toLongLong();
        if (updatedAt > bestAt) {
            bestAt = updatedAt;
            bestPath = fullPath;
            bestTitle = ScannerUtils::cleanMediaFolderTitle(QFileInfo(fullPath).completeBaseName());
        }
    }

    if (bestPath.isEmpty()) return;

    m_continueFilePath = bestPath;
    m_continueTitle->setText(bestTitle);

    // Populate progress bar and percentage from best candidate's progress
    QString bestKey = QString(QCryptographicHash::hash(
        bestPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
    QJsonObject bestProg = allProg.value(bestKey).toObject();
    int page = bestProg.value("page").toInt(0);
    int pageCount = bestProg.value("pageCount").toInt(0);
    int pct = (pageCount > 0) ? qBound(0, static_cast<int>((page + 1) * 100.0 / pageCount), 100) : 0;
    m_continueProgress->setValue(pct);
    m_continuePctLabel->setText(QString::number(pct) + "%");

    m_continueBar->show();
}

QString BookSeriesView::formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024) + " KB";
    if (bytes < 1024LL * 1024 * 1024) {
        double mb = bytes / (1024.0 * 1024.0);
        return QString::number(mb, 'f', 1) + " MB";
    }
    double gb = bytes / (1024.0 * 1024.0 * 1024.0);
    return QString::number(gb, 'f', 2) + " GB";
}
