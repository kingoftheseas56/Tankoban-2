#include "ShowView.h"
#include "core/ScannerUtils.h"
#include "core/CoreBridge.h"
#include "ui/ContextMenuHelper.h"
#include <QJsonArray>

#include <QPushButton>
#include <QPixmap>
#include <QPainter>
#include <QPolygon>
#include <QHeaderView>
#include <QStyleFactory>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QCollator>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QFont>
#include <QColor>
#include <QPalette>
#include <QIcon>
#include <QShortcut>
#include <QMessageBox>
#include <algorithm>

static const QStringList VIDEO_EXTS = {
    "*.mp4", "*.mkv", "*.avi", "*.webm", "*.mov", "*.wmv", "*.flv",
    "*.m4v", "*.ts", "*.mpg", "*.mpeg", "*.ogv"
};

// Column indices
enum Col { ColNum = 0, ColEpisode, ColSize, ColDuration, ColProgress, ColModified, ColCount };

// ─── Progress Icon Delegate ─────────────────────────────────────────

void ShowProgressIconDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    int state = index.data(Qt::UserRole).toInt(); // 0=none, 1=in-progress, 2=finished
    if (state == 0) {
        // No progress — draw "-"
        painter->setPen(QColor(238, 238, 238, 140));
        painter->drawText(option.rect, Qt::AlignCenter, "-");
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QRect cell = option.rect;

    if (state == 2) {
        // Finished: green circle #4CAF50 + white checkmark
        int iconSize = 12;
        QRect iconRect(cell.center().x() - iconSize / 2, cell.center().y() - iconSize / 2,
                       iconSize, iconSize);
        painter->setBrush(QColor("#4CAF50"));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(iconRect.adjusted(1, 1, -1, -1)); // 10x10
        QFont checkFont;
        checkFont.setPixelSize(9);
        checkFont.setBold(true);
        painter->setFont(checkFont);
        painter->setPen(Qt::white);
        painter->drawText(iconRect, Qt::AlignCenter, QString::fromUtf8("\u2713"));
    } else if (state == 1) {
        // In-progress: slate circle + percentage text
        int iconSize = 12;
        QRect iconRect(cell.left() + (cell.width() / 2) - iconSize - 2,
                       cell.center().y() - iconSize / 2,
                       iconSize, iconSize);
        painter->setBrush(QColor("#94a3b8"));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(iconRect.adjusted(1, 1, -1, -1)); // 10x10

        // Percentage text to the right of the icon
        QFont pctFont;
        pctFont.setPixelSize(11);
        painter->setFont(pctFont);
        painter->setPen(QColor(238, 238, 238, 200));
        QString pctText = index.data(Qt::UserRole + 1).toString();
        QRect textRect(iconRect.right() + 4, cell.top(), cell.right() - iconRect.right() - 4, cell.height());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, pctText);
    }

    painter->restore();
}

// ─── Constructor ─────────────────────────────────────────────────────

ShowView::ShowView(CoreBridge* bridge, QWidget* parent)
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

    // Back button
    auto* backBtn = new QPushButton(QString::fromUtf8("\u2190  Videos"), topBar);
    backBtn->setObjectName("SidebarAction");
    backBtn->setFixedHeight(30);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setStyleSheet(
        "QPushButton { text-align: left; color: rgba(255,255,255,0.72);"
        "  background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 8px; padding: 4px 12px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.08);"
        "  border-color: rgba(255,255,255,0.14); color: rgba(255,255,255,0.9); }");
    connect(backBtn, &QPushButton::clicked, this, [this]() { navigateBack(); });
    topLayout->addWidget(backBtn);

    // Forward button (28x28, disabled when no forward history)
    m_forwardBtn = new QPushButton(QString::fromUtf8("\u2192"), topBar);
    m_forwardBtn->setFixedSize(28, 28);
    m_forwardBtn->setCursor(Qt::PointingHandCursor);
    m_forwardBtn->setEnabled(false);
    m_forwardBtn->setStyleSheet(
        "QPushButton { color: rgba(255,255,255,0.72);"
        "  background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 8px; font-size: 14px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.08);"
        "  border-color: rgba(255,255,255,0.14); color: rgba(255,255,255,0.9); }"
        "QPushButton:disabled { color: rgba(255,255,255,0.2); background: transparent;"
        "  border-color: rgba(255,255,255,0.04); }");
    connect(m_forwardBtn, &QPushButton::clicked, this, [this]() { navigateForward(); });
    topLayout->addWidget(m_forwardBtn);

    // Keyboard shortcuts: Alt+Left (back), Alt+Right (forward), Backspace (back)
    auto* backShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
    connect(backShortcut, &QShortcut::activated, this, [this]() { navigateBack(); });
    auto* fwdShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
    connect(fwdShortcut, &QShortcut::activated, this, [this]() { navigateForward(); });
    auto* bsShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    connect(bsShortcut, &QShortcut::activated, this, [this]() { navigateBack(); });

    // Breadcrumb
    m_breadcrumbWidget = new QWidget(topBar);
    m_breadcrumbLayout = new QHBoxLayout(m_breadcrumbWidget);
    m_breadcrumbLayout->setContentsMargins(0, 0, 0, 0);
    m_breadcrumbLayout->setSpacing(4);
    m_breadcrumbLayout->addStretch();
    topLayout->addWidget(m_breadcrumbWidget, 1);

    // Search bar
    m_searchBar = new QLineEdit(topBar);
    m_searchBar->setObjectName("DetailSearch");
    m_searchBar->setPlaceholderText(QString::fromUtf8("Search episodes\u2026"));
    m_searchBar->setClearButtonEnabled(true);
    m_searchBar->setFixedWidth(180);
    m_searchBar->setFixedHeight(28);
    m_searchBar->setStyleSheet(
        "QLineEdit { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px; color: #eee; padding: 2px 8px; font-size: 12px; }"
        "QLineEdit:focus { border: 1px solid rgba(255,255,255,0.3); }");
    connect(m_searchBar, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_searchText = text.trimmed().toLower();
        populateTable(m_showRootPath + (m_currentRel.isEmpty() ? "" : "/" + m_currentRel));
    });
    topLayout->addWidget(m_searchBar);

    // Sort combo
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
    // Ignore wheel events to prevent scroll interference
    m_sortCombo->installEventFilter(this);
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        m_sortKey = m_sortCombo->itemData(idx).toString();
        populateTable(m_showRootPath + (m_currentRel.isEmpty() ? "" : "/" + m_currentRel));
    });
    topLayout->addWidget(m_sortCombo);

    layout->addWidget(topBar);

    // ── Continue watching bar (40px, groundwork spec) ──
    m_continueBar = new QWidget(this);
    m_continueBar->setFixedHeight(40);
    auto* contBarLayout = new QHBoxLayout(m_continueBar);
    contBarLayout->setContentsMargins(16, 4, 16, 4);
    contBarLayout->setSpacing(10);

    m_continueTitle = new QLabel("Continue watching", m_continueBar);
    m_continueTitle->setObjectName("ContinueBarTitle");
    m_continueTitle->setStyleSheet("#ContinueBarTitle { color: rgba(255,255,255,0.72); font-weight: bold; font-size: 13px; }");
    contBarLayout->addWidget(m_continueTitle);

    m_continueItemLabel = new QLabel(m_continueBar);
    m_continueItemLabel->setObjectName("ContinueBarItem");
    m_continueItemLabel->setMinimumWidth(60);
    m_continueItemLabel->setStyleSheet("#ContinueBarItem { color: rgba(255,255,255,0.58); font-size: 12px; }");
    contBarLayout->addWidget(m_continueItemLabel, 1);

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
    m_continuePctLabel->setStyleSheet("#Subtle { color: rgba(255,255,255,0.45); font-size: 11px; }");
    contBarLayout->addWidget(m_continuePctLabel);

    m_continueBtn = new QPushButton("Watch", m_continueBar);
    m_continueBtn->setObjectName("ContinueBarBtn");
    m_continueBtn->setFixedSize(60, 26);
    m_continueBtn->setCursor(Qt::PointingHandCursor);
    m_continueBtn->setStyleSheet(
        "#ContinueBarBtn { color: #eee; background: rgba(255,255,255,0.07);"
        "  border: 1px solid rgba(255,255,255,0.12); border-radius: 6px;"
        "  font-size: 12px; }"
        "#ContinueBarBtn:hover { background: rgba(255,255,255,0.12); }");
    connect(m_continueBtn, &QPushButton::clicked, this, [this]() {
        if (!m_continueFilePath.isEmpty())
            emit episodeSelected(m_continueFilePath);
    });
    contBarLayout->addWidget(m_continueBtn);

    // Right-click context menu on continue bar
    m_continueBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_continueBar, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (m_continueFilePath.isEmpty()) return;
        auto* menu = ContextMenuHelper::createMenu(this);
        auto* watchAct = menu->addAction("Watch");
        menu->addSeparator();
        auto* resetAct = menu->addAction("Reset progress");
        menu->addSeparator();
        auto* revealAct = menu->addAction("Reveal in File Explorer");
        auto* copyAct = menu->addAction("Copy path");

        auto* chosen = menu->exec(m_continueBar->mapToGlobal(pos));
        if (chosen == watchAct) {
            emit episodeSelected(m_continueFilePath);
        } else if (chosen == resetAct) {
            if (!m_continueVideoId.isEmpty()) {
                QJsonObject prog = m_bridge->progress("videos", m_continueVideoId);
                prog.remove("positionSec");
                prog.remove("finished");
                m_bridge->saveProgress("videos", m_continueVideoId, prog);
                buildContinueBar();
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
    m_coverLabel->hide();
    contentLayout->addWidget(m_coverLabel);

    // Table
    m_table = new QTableWidget(contentRow);
    m_table->setObjectName("FolderDetailTable");
    // Force Fusion style to override Windows 11 system accent selection colors
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

    // Columns
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels({"#", "EPISODE", "SIZE", "DURATION", "PROGRESS", "MODIFIED"});

    auto* hdr = m_table->horizontalHeader();
    hdr->setSectionResizeMode(ColNum,      QHeaderView::Fixed);
    hdr->setSectionResizeMode(ColEpisode,  QHeaderView::Stretch);
    hdr->setSectionResizeMode(ColSize,     QHeaderView::Fixed);
    hdr->setSectionResizeMode(ColDuration, QHeaderView::Fixed);
    hdr->setSectionResizeMode(ColProgress, QHeaderView::Fixed);
    hdr->setSectionResizeMode(ColModified, QHeaderView::Fixed);
    m_table->setColumnWidth(ColNum,      42);
    m_table->setColumnWidth(ColSize,     90);
    m_table->setColumnWidth(ColDuration, 110);
    m_table->setColumnWidth(ColProgress, 92);
    m_table->setColumnWidth(ColModified, 132);
    hdr->setMinimumSectionSize(42);

    // Progress icon delegate on PROGRESS column
    m_table->setItemDelegateForColumn(ColProgress, new ShowProgressIconDelegate(m_table));

    // Selection palette
    QPalette pal = m_table->palette();
    pal.setColor(QPalette::Highlight, QColor(192, 200, 212, 36));
    pal.setColor(QPalette::HighlightedText, QColor("#eeeeee"));
    m_table->setPalette(pal);

    // Styling
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
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int /*col*/) {
        auto* item = m_table->item(row, ColEpisode);
        if (!item) return;

        bool isFolder = item->data(FolderRowRole).toBool();
        if (isFolder) {
            QString rel = item->data(FolderRelRole).toString();
            if (rel.isEmpty() && !m_currentRel.isEmpty()) {
                // ".." up-row
                int sep = m_currentRel.lastIndexOf('/');
                navigateTo(sep > 0 ? m_currentRel.left(sep) : QString());
            } else if (!rel.isEmpty()) {
                navigateTo(rel);
            }
        } else {
            QString filePath = item->data(FilePathRole).toString();
            if (!filePath.isEmpty())
                emit episodeSelected(filePath);
        }
    });

    // Context menu on table rows
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int row = m_table->rowAt(pos.y());
        if (row < 0) return;

        auto* titleItem = m_table->item(row, ColEpisode);
        if (!titleItem) return;

        bool isFolder = titleItem->data(FolderRowRole).toBool();
        QString filePath = titleItem->data(FilePathRole).toString();
        QString relPath = titleItem->data(FolderRelRole).toString();

        auto* menu = ContextMenuHelper::createMenu(this);

        if (isFolder) {
            // Folder row (not ".." row)
            if (relPath.isEmpty() && !m_currentRel.isEmpty()) {
                // ".." row — no menu
                menu->deleteLater();
                return;
            }
            QString folderAbsPath = m_showRootPath + "/" + relPath;
            auto* openAct = menu->addAction("Open folder");
            menu->addSeparator();
            auto* revealAct = menu->addAction("Reveal in File Explorer");
            auto* copyAct = menu->addAction("Copy path");

            auto* chosen = menu->exec(m_table->viewport()->mapToGlobal(pos));
            if (chosen == openAct) {
                navigateTo(relPath);
            } else if (chosen == revealAct) {
                ContextMenuHelper::revealInExplorer(folderAbsPath);
            } else if (chosen == copyAct) {
                ContextMenuHelper::copyToClipboard(folderAbsPath);
            }
        } else {
            // File row — full groundwork spec (Section 4F)
            QFileInfo fi(filePath);
            QString vid = videoId(fi.absoluteFilePath(), fi.size(), fi.lastModified().toMSecsSinceEpoch());
            QJsonObject prog = m_bridge ? m_bridge->progress("videos", vid) : QJsonObject();
            bool finished = prog.value("finished").toBool(false);
            bool hasProgress = !prog.isEmpty() && (prog.contains("positionSec") || prog.contains("finished"));

            auto* playAct = menu->addAction("Play");
            auto* playBeginAct = menu->addAction("Play from beginning");
            menu->addSeparator();
            auto* revealAct = menu->addAction("Reveal in File Explorer");
            revealAct->setEnabled(!filePath.isEmpty());
            auto* copyAct = menu->addAction("Copy file path");
            copyAct->setEnabled(!filePath.isEmpty());
            menu->addSeparator();
            auto* markAct = menu->addAction(finished ? "Mark as in progress" : "Mark as finished");
            auto* resetAct = menu->addAction("Reset progress");
            resetAct->setEnabled(hasProgress);
            menu->addSeparator();
            auto* removeAct = menu->addAction("Remove from library...");

            auto* chosen = menu->exec(m_table->viewport()->mapToGlobal(pos));
            if (chosen == playAct) {
                emit episodeSelected(filePath);
            } else if (chosen == playBeginAct) {
                // Reset position then play
                prog.remove("positionSec");
                if (m_bridge) m_bridge->saveProgress("videos", vid, prog);
                emit episodeSelected(filePath);
            } else if (chosen == revealAct) {
                ContextMenuHelper::revealInExplorer(filePath);
            } else if (chosen == copyAct) {
                ContextMenuHelper::copyToClipboard(filePath);
            } else if (chosen == markAct) {
                prog["finished"] = !finished;
                if (m_bridge) m_bridge->saveProgress("videos", vid, prog);
                // Refresh table to update progress icons
                QString absPath = m_showRootPath;
                if (!m_currentRel.isEmpty()) absPath += "/" + m_currentRel;
                populateTable(absPath);
                buildContinueBar();
            } else if (chosen == resetAct) {
                prog.remove("positionSec");
                prog.remove("finished");
                if (m_bridge) m_bridge->saveProgress("videos", vid, prog);
                QString absPath = m_showRootPath;
                if (!m_currentRel.isEmpty()) absPath += "/" + m_currentRel;
                populateTable(absPath);
                buildContinueBar();
            } else if (chosen == removeAct) {
                if (QMessageBox::question(this, "Remove from library",
                        "Remove this episode from the library?\n" + filePath +
                        "\nFiles will not be deleted from disk.",
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
                    // Reset progress for this file
                    if (m_bridge && hasProgress) {
                        prog.remove("positionSec");
                        prog.remove("finished");
                        m_bridge->saveProgress("videos", vid, prog);
                    }
                    QString absPath = m_showRootPath;
                    if (!m_currentRel.isEmpty()) absPath += "/" + m_currentRel;
                    populateTable(absPath);
                    buildContinueBar();
                }
            }
        }

        menu->deleteLater();
    });

    contentLayout->addWidget(m_table, 1);
    layout->addWidget(contentRow, 1);
}

// ─── Public API ──────────────────────────────────────────────────────

void ShowView::setFileDurations(const QMap<QString, double>& durations)
{
    m_fileDurations = durations;
}

void ShowView::showFolder(const QString& folderPath, const QString& showName,
                          const QString& coverThumbPath, bool isLoose)
{
    m_showRootPath = folderPath;
    m_showRootName = showName;
    m_isLoose = isLoose;
    m_currentRel.clear();
    m_searchBar->clear();

    // Reset navigation history
    m_navHistory.clear();
    m_navHistory.append(QString()); // root entry
    m_navIndex = 0;
    m_forwardBtn->setEnabled(false);

    // Set cover thumbnail or placeholder
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
        // Placeholder: dark box with play triangle + show name
        QPixmap ph(208, 320);
        ph.fill(QColor(30, 30, 30));
        QPainter p(&ph);
        // Play triangle icon (centered)
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(80, 80, 80));
        QPolygon tri;
        tri << QPoint(84, 120) << QPoint(84, 200) << QPoint(134, 160);
        p.drawPolygon(tri);
        // Border circle around play icon
        p.setPen(QPen(QColor(80, 80, 80), 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPoint(104, 160), 50, 50);
        // Show name below
        p.setPen(QColor(160, 160, 160));
        QFont font;
        font.setPixelSize(14);
        font.setBold(true);
        p.setFont(font);
        p.drawText(QRect(8, 240, 192, 60), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, showName);
        p.end();
        m_coverLabel->setPixmap(ph);
    }
    m_coverLabel->show();

    buildBreadcrumb();
    buildContinueBar();
    populateTable(folderPath);
}

// ─── Navigation ──────────────────────────────────────────────────────

void ShowView::navigateTo(const QString& relPath)
{
    // Push to history: truncate any forward entries beyond current index
    if (m_navIndex >= 0 && m_navIndex < m_navHistory.size() - 1)
        m_navHistory = m_navHistory.mid(0, m_navIndex + 1);
    m_navHistory.append(relPath);
    m_navIndex = m_navHistory.size() - 1;

    m_currentRel = relPath;
    buildBreadcrumb();
    buildContinueBar();
    m_forwardBtn->setEnabled(m_navIndex < m_navHistory.size() - 1);

    QString absPath = m_showRootPath;
    if (!relPath.isEmpty())
        absPath += "/" + relPath;
    populateTable(absPath);
}

void ShowView::navigateBack()
{
    if (m_navIndex > 0) {
        // Go back in history
        m_navIndex--;
        m_currentRel = m_navHistory.at(m_navIndex);
        buildBreadcrumb();
        buildContinueBar();
        m_forwardBtn->setEnabled(m_navIndex < m_navHistory.size() - 1);

        QString absPath = m_showRootPath;
        if (!m_currentRel.isEmpty())
            absPath += "/" + m_currentRel;
        populateTable(absPath);
    } else {
        // At root — go back to grid
        emit backRequested();
    }
}

void ShowView::navigateForward()
{
    if (m_navIndex < m_navHistory.size() - 1) {
        m_navIndex++;
        m_currentRel = m_navHistory.at(m_navIndex);
        buildBreadcrumb();
        buildContinueBar();
        m_forwardBtn->setEnabled(m_navIndex < m_navHistory.size() - 1);

        QString absPath = m_showRootPath;
        if (!m_currentRel.isEmpty())
            absPath += "/" + m_currentRel;
        populateTable(absPath);
    }
}

// ─── Breadcrumb ──────────────────────────────────────────────────────

void ShowView::buildBreadcrumb()
{
    while (m_breadcrumbLayout->count()) {
        auto* item = m_breadcrumbLayout->takeAt(0);
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }

    QStringList parts;
    parts << m_showRootName;
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

void ShowView::populateTable(const QString& folderPath)
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

    // Load progress data
    QJsonObject progressMap;
    if (m_bridge)
        progressMap = m_bridge->allProgress("videos");

    int row = 0;

    // ── ".." up-row ──
    if (!m_currentRel.isEmpty()) {
        m_table->insertRow(row);
        m_table->setRowHeight(row, 38);

        for (int c = 0; c < ColCount; ++c) {
            auto* item = new QTableWidgetItem();
            item->setFont(boldFont);
            item->setBackground(folderBg);
            m_table->setItem(row, c, item);
        }
        auto* titleItem = m_table->item(row, ColEpisode);
        titleItem->setText(QString::fromUtf8("\u2190  .."));
        titleItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        titleItem->setData(FolderRowRole, true);
        titleItem->setData(FolderRelRole, QString());
        m_table->item(row, ColNum)->setTextAlignment(Qt::AlignCenter);
        ++row;
    }

    // ── Folder rows (skipped for loose files — they have no subfolders) ──
    QStringList subdirs;
    if (!m_isLoose)
        subdirs = ScannerUtils::listImmediateSubdirs(folderPath);
    std::sort(subdirs.begin(), subdirs.end(), [&collator](const QString& a, const QString& b) {
        return collator.compare(QDir(a).dirName(), QDir(b).dirName()) < 0;
    });

    for (const auto& subdir : subdirs) {
        QStringList files = ScannerUtils::walkFiles(subdir, VIDEO_EXTS);
        if (files.isEmpty()) continue;

        QString dirName = QDir(subdir).dirName();
        QString relPath = m_currentRel.isEmpty() ? dirName : m_currentRel + "/" + dirName;
        QString countText = QString("    (%1 %2)")
            .arg(files.size())
            .arg(files.size() == 1 ? "episode" : "episodes");

        m_table->insertRow(row);
        m_table->setRowHeight(row, 38);

        for (int c = 0; c < ColCount; ++c) {
            auto* item = new QTableWidgetItem();
            item->setFont(boldFont);
            item->setBackground(folderBg);
            m_table->setItem(row, c, item);
        }

        auto* titleItem = m_table->item(row, ColEpisode);
        titleItem->setIcon(folderIcon);
        titleItem->setText(dirName + countText);
        titleItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        titleItem->setData(FolderRowRole, true);
        titleItem->setData(FolderRelRole, relPath);
        m_table->item(row, ColNum)->setTextAlignment(Qt::AlignCenter);
        ++row;
    }

    // ── File rows ──
    QDir dir(folderPath);
    auto fileInfos = dir.entryInfoList(VIDEO_EXTS, QDir::Files);

    // Search filter
    if (!m_searchText.isEmpty()) {
        QList<QFileInfo> filtered;
        for (const auto& fi : fileInfos) {
            QString title = ScannerUtils::cleanMediaFolderTitle(fi.completeBaseName()).toLower();
            QString basename = fi.fileName().toLower();
            if (title.contains(m_searchText) || basename.contains(m_searchText))
                filtered.append(fi);
        }
        fileInfos = filtered;
    }

    // Sort
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

        QString vid = videoId(fi.absoluteFilePath(), fi.size(), fi.lastModified().toMSecsSinceEpoch());
        QJsonObject prog = progressMap.value(vid).toObject();

        // #
        auto* numItem = new QTableWidgetItem(QString::number(fileNum));
        numItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, ColNum, numItem);

        // Episode
        QString displayName = ScannerUtils::cleanMediaFolderTitle(fi.completeBaseName());
        auto* titleItem = new QTableWidgetItem(displayName);
        titleItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        titleItem->setData(FolderRowRole, false);
        titleItem->setData(FilePathRole, fi.absoluteFilePath());
        m_table->setItem(row, ColEpisode, titleItem);

        // Size
        auto* sizeItem = new QTableWidgetItem(formatSize(fi.size()));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, ColSize, sizeItem);

        // Duration: prefer scan-time (ffprobe), fall back to saved progress
        double durSec = m_fileDurations.value(fi.absoluteFilePath(), 0.0);
        if (durSec <= 0.0)
            durSec = prog.value("durationSec").toDouble(0);
        auto* durItem = new QTableWidgetItem(formatDuration(durSec));
        durItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, ColDuration, durItem);

        // Progress — state for ProgressIconDelegate: 0=none, 1=in-progress, 2=finished
        bool finished = prog.value("finished").toBool(false);
        double posSec = prog.value("positionSec").toDouble(0);
        auto* progItem = new QTableWidgetItem();
        progItem->setTextAlignment(Qt::AlignCenter);
        if (finished) {
            progItem->setData(Qt::UserRole, 2);
        } else if (posSec > 0 && durSec > 0) {
            int pct = qBound(0, static_cast<int>(posSec / durSec * 100), 100);
            progItem->setData(Qt::UserRole, 1);
            progItem->setData(Qt::UserRole + 1, QString::number(pct) + "%");
        } else {
            progItem->setData(Qt::UserRole, 0);
        }
        m_table->setItem(row, ColProgress, progItem);

        // Modified
        auto* dateItem = new QTableWidgetItem(formatDate(fi.lastModified()));
        dateItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, ColModified, dateItem);

        ++row;
        ++fileNum;
    }
}

// ─── Helpers ─────────────────────────────────────────────────────────

QString ShowView::videoId(const QString& path, qint64 size, qint64 mtimeMs)
{
    QString raw = path + "::" + QString::number(size) + "::" + QString::number(mtimeMs);
    return QString(QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha1).toHex());
}

void ShowView::buildContinueBar()
{
    m_continueBar->hide();
    m_continueFilePath.clear();
    m_continueVideoId.clear();

    if (!m_bridge) return;

    QJsonObject allProg = m_bridge->allProgress("videos");
    if (allProg.isEmpty()) return;

    // Scope to current subfolder (not entire show root) — groundwork behavior:
    // continue watching shows the last-viewed file FROM THIS FOLDER and its descendants
    QString scopePath = m_showRootPath;
    if (!m_currentRel.isEmpty())
        scopePath = m_showRootPath + "/" + m_currentRel;
    QStringList allFiles = ScannerUtils::walkFiles(scopePath, VIDEO_EXTS);
    qint64 bestAt = -1;
    QString bestPath, bestTitle, bestId;
    double bestPosSec = 0, bestDurSec = 0;

    for (const auto& f : allFiles) {
        QFileInfo fi(f);
        QString vid = videoId(fi.absoluteFilePath(), fi.size(), fi.lastModified().toMSecsSinceEpoch());
        QJsonObject prog = allProg.value(vid).toObject();
        if (prog.isEmpty()) continue;
        if (prog.value("finished").toBool()) continue;
        double posSec = prog.value("positionSec").toDouble(0);
        if (posSec <= 0) continue;

        qint64 updatedAt = prog.value("updatedAt").toVariant().toLongLong();
        if (updatedAt > bestAt) {
            bestAt = updatedAt;
            bestPath = fi.absoluteFilePath();
            bestTitle = ScannerUtils::cleanMediaFolderTitle(fi.completeBaseName());
            bestId = vid;
            bestPosSec = posSec;
            bestDurSec = prog.value("durationSec").toDouble(0);
        }
    }

    if (bestPath.isEmpty()) return;

    m_continueFilePath = bestPath;
    m_continueVideoId = bestId;
    m_continueItemLabel->setText(bestTitle);

    int pct = (bestDurSec > 0) ? qBound(0, static_cast<int>(bestPosSec / bestDurSec * 100), 100) : 0;
    m_continueProgress->setValue(pct);
    m_continuePctLabel->setText(QString::number(pct) + "%");

    m_continueBar->show();
}

QString ShowView::formatSize(qint64 bytes)
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

QString ShowView::formatDate(const QDateTime& dt)
{
    if (!dt.isValid()) return "-";
    return dt.toString("MM/dd/yyyy");
}

QString ShowView::formatDuration(double seconds)
{
    if (seconds <= 0) return "-";
    int sec = static_cast<int>(seconds);
    int h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

// ── REPO_HYGIENE Phase 3 — dev-control bridge snapshot ──────────────────────

QJsonObject ShowView::devSnapshot() const
{
    QJsonObject snap;
    snap["showName"]     = m_showRootName;
    snap["showRootPath"] = m_showRootPath;
    snap["currentRel"]   = m_currentRel;
    snap["isLoose"]      = m_isLoose;
    snap["searchText"]   = m_searchText;
    snap["sortKey"]      = m_sortKey;
    snap["continueFile"] = m_continueFilePath;

    QJsonArray episodes;
    if (m_table) {
        const int rows = m_table->rowCount();
        snap["rowCount"] = rows;
        for (int r = 0; r < rows; ++r) {
            QJsonObject e;
            QTableWidgetItem* titleItem = m_table->item(r, 0);
            if (titleItem) {
                e["title"]    = titleItem->text();
                e["filePath"] = titleItem->data(FilePathRole).toString();
            }
            episodes.append(e);
        }
    } else {
        snap["rowCount"] = 0;
    }
    snap["episodes"] = episodes;

    return snap;
}

