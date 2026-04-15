#include "SeriesView.h"
#include "core/ScannerUtils.h"
#include "core/CoreBridge.h"
#include "core/ArchiveReader.h"
#include "ui/ContextMenuHelper.h"

#include <QPushButton>
#include <QPixmap>
#include <QPainter>
#include <QHeaderView>
#include <QStyleFactory>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QCollator>
#include <QFont>
#include <QColor>
#include <QPalette>
#include <QIcon>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QSettings>
#include <algorithm>

// P4-3: COMIC_EXTS covers all reader-supported archive formats. Engine
// (ArchiveReader) and library scanner both handle CBZ + CBR + RAR.
static const QStringList COMIC_EXTS = {"*.cbz", "*.cbr", "*.rar"};

enum Col { ColNum = 0, ColVolume, ColPages, ColSize, ColRead, ColModified, ColCount };

// ─── ProgressIconDelegate ────────────────────────────────────────────

void ProgressIconDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
    // Draw selection/hover background
    QStyledItemDelegate::paint(painter, option, QModelIndex());

    int state = index.data(ProgressStateRole).toInt(); // 0=none, 1=in-progress, 2=finished
    int pct   = index.data(ProgressPctRole).toInt();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QRect cell = option.rect;

    if (state == 2) {
        // Finished: green circle + white checkmark
        int iconSize = 12;
        QRect iconRect(cell.center().x() - iconSize / 2, cell.center().y() - iconSize / 2,
                       iconSize, iconSize);

        painter->setBrush(QColor("#4CAF50"));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(iconRect.adjusted(1, 1, -1, -1)); // 10x10 ellipse

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
        painter->drawEllipse(iconRect.adjusted(1, 1, -1, -1)); // 10x10 ellipse

        // Percentage text to the right of the icon
        QFont pctFont;
        pctFont.setPixelSize(11);
        painter->setFont(pctFont);
        painter->setPen(QColor(238, 238, 238, 219)); // rgba(238,238,238,0.86)
        QRect textRect(iconRect.right() + 4, cell.top(), cell.right() - iconRect.right() - 4, cell.height());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, QString::number(pct) + "%");

    } else {
        // No progress: "-" text
        QFont font;
        font.setPixelSize(12);
        painter->setFont(font);
        painter->setPen(QColor(238, 238, 238, 110));
        painter->drawText(cell, Qt::AlignCenter, "-");
    }

    painter->restore();
}

QSize ProgressIconDelegate::sizeHint(const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const
{
    Q_UNUSED(index);
    return QSize(80, option.rect.height());
}

// ─── Constructor ─────────────────────────────────────────────────────

SeriesView::SeriesView(CoreBridge* bridge, QWidget* parent)
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

    auto* backBtn = new QPushButton(QString::fromUtf8("\u2190  Comics"), topBar);
    backBtn->setObjectName("SidebarAction");
    backBtn->setFixedHeight(30);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setStyleSheet(
        "QPushButton { text-align: left; color: rgba(255,255,255,0.72);"
        "  background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 8px; padding: 4px 12px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.08);"
        "  border-color: rgba(255,255,255,0.14); color: rgba(255,255,255,0.9); }");
    connect(backBtn, &QPushButton::clicked, this, &SeriesView::goBack);
    topLayout->addWidget(backBtn);

    // Forward button
    m_fwdBtn = new QPushButton(QString::fromUtf8("\u2192"), topBar);
    m_fwdBtn->setObjectName("SidebarAction");
    m_fwdBtn->setFixedSize(28, 28);
    m_fwdBtn->setCursor(Qt::PointingHandCursor);
    m_fwdBtn->setStyleSheet(
        "QPushButton { color: rgba(255,255,255,0.72);"
        "  background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 8px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.08);"
        "  border-color: rgba(255,255,255,0.14); color: rgba(255,255,255,0.9); }"
        "QPushButton:disabled { color: rgba(255,255,255,0.2);"
        "  background: rgba(255,255,255,0.02); border-color: rgba(255,255,255,0.04); }");
    m_fwdBtn->setEnabled(false);
    connect(m_fwdBtn, &QPushButton::clicked, this, &SeriesView::goForward);
    topLayout->addWidget(m_fwdBtn);

    m_breadcrumbWidget = new QWidget(topBar);
    m_breadcrumbLayout = new QHBoxLayout(m_breadcrumbWidget);
    m_breadcrumbLayout->setContentsMargins(0, 0, 0, 0);
    m_breadcrumbLayout->setSpacing(4);
    m_breadcrumbLayout->addStretch();
    topLayout->addWidget(m_breadcrumbWidget, 1);

    m_searchBar = new QLineEdit(topBar);
    m_searchBar->setObjectName("DetailSearch");
    m_searchBar->setPlaceholderText(QString::fromUtf8("Search volumes\u2026"));
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

    // Naming toggle (Volumes / Chapters)
    m_namingCombo = new QComboBox(topBar);
    m_namingCombo->setObjectName("DetailNamingCombo");
    m_namingCombo->setFixedWidth(110);
    m_namingCombo->setFixedHeight(28);
    m_namingCombo->addItem("Volumes",  "volumes");
    m_namingCombo->addItem("Chapters", "chapters");
    m_namingCombo->setStyleSheet(
        "QComboBox#DetailNamingCombo {"
        "  background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 6px; color: #ccc; padding: 2px 8px; font-size: 12px; }"
        "QComboBox#DetailNamingCombo:hover { border-color: rgba(255,255,255,0.2); }"
        "QComboBox#DetailNamingCombo::drop-down { border: none; }"
        "QComboBox#DetailNamingCombo QAbstractItemView {"
        "  background: #1e1e1e; color: #ccc; selection-background-color: rgba(255,255,255,0.1);"
        "  border: 1px solid rgba(255,255,255,0.12); }");
    // Restore persisted naming mode
    {
        QSettings settings;
        QString saved = settings.value("comics_naming_mode", "volumes").toString();
        m_namingMode = saved;
        int idx = m_namingCombo->findData(saved);
        if (idx >= 0) m_namingCombo->setCurrentIndex(idx);
    }
    connect(m_namingCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        m_namingMode = m_namingCombo->itemData(idx).toString();
        QSettings settings;
        settings.setValue("comics_naming_mode", m_namingMode);
        // Update column header
        QString label = (m_namingMode == "chapters") ? "CHAPTER" : "VOLUME";
        m_table->horizontalHeaderItem(ColVolume)->setText(label);
        // Update search placeholder
        QString placeholder = (m_namingMode == "chapters")
            ? QString::fromUtf8("Search chapters\u2026")
            : QString::fromUtf8("Search volumes\u2026");
        m_searchBar->setPlaceholderText(placeholder);
        // Repopulate to update folder count text
        populateTable(m_seriesRootPath + (m_currentRel.isEmpty() ? "" : "/" + m_currentRel));
    });
    topLayout->addWidget(m_namingCombo);

    layout->addWidget(topBar);

    // ── Keyboard shortcuts ──
    auto* scBack = new QShortcut(QKeySequence("Alt+Left"), this);
    connect(scBack, &QShortcut::activated, this, &SeriesView::goBack);
    auto* scFwd = new QShortcut(QKeySequence("Alt+Right"), this);
    connect(scFwd, &QShortcut::activated, this, &SeriesView::goForward);
    auto* scBackspace = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    connect(scBackspace, &QShortcut::activated, this, &SeriesView::goBack);

    // ── Continue reading bar ──
    m_continueBar = new QWidget(this);
    auto* contBarLayout = new QHBoxLayout(m_continueBar);
    contBarLayout->setContentsMargins(24, 8, 24, 8);
    contBarLayout->setSpacing(12);
    auto* contLabel = new QLabel("Continue reading", m_continueBar);
    contLabel->setStyleSheet("color: #c7a76b; font-weight: bold; font-size: 13px;");
    contBarLayout->addWidget(contLabel);
    m_continueTitle = new QLabel(m_continueBar);
    m_continueTitle->setStyleSheet("color: rgba(255,255,255,0.72); font-size: 13px;");
    contBarLayout->addWidget(m_continueTitle, 1);
    m_continueBtn = new QPushButton("Read", m_continueBar);
    m_continueBtn->setObjectName("SidebarAction");
    m_continueBtn->setFixedHeight(28);
    m_continueBtn->setCursor(Qt::PointingHandCursor);
    m_continueBtn->setStyleSheet(
        "QPushButton { color: #eee; background: rgba(255,255,255,0.07);"
        "  border: 1px solid rgba(255,255,255,0.12); border-radius: 6px;"
        "  padding: 4px 16px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.12); }");
    connect(m_continueBtn, &QPushButton::clicked, this, [this]() {
        if (!m_continueFilePath.isEmpty())
            emit issueSelected(m_continueFilePath, m_allCbzFiles, m_seriesRootName);
    });
    contBarLayout->addWidget(m_continueBtn);

    // Continue bar right-click context menu
    m_continueBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_continueBar, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (m_continueFilePath.isEmpty()) return;

        auto* menu = ContextMenuHelper::createMenu(this);
        auto* readAct    = menu->addAction("Read");
        menu->addSeparator();
        auto* resetAct   = menu->addAction("Reset progress");
        menu->addSeparator();
        auto* revealAct  = menu->addAction("Reveal in File Explorer");
        auto* copyAct    = menu->addAction("Copy path");

        auto* chosen = menu->exec(m_continueBar->mapToGlobal(pos));
        if (chosen == readAct) {
            emit issueSelected(m_continueFilePath, m_allCbzFiles, m_seriesRootName);
        } else if (chosen == resetAct && m_bridge) {
            QString progKey = QString(QCryptographicHash::hash(
                m_continueFilePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            m_bridge->clearProgress("comics", progKey);
            buildContinueBar();
            // Refresh table to update READ column
            populateTable(m_seriesRootPath + (m_currentRel.isEmpty() ? "" : "/" + m_currentRel));
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

    // Cover panel (240px fixed width, matching groundwork)
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
    m_table->setHorizontalHeaderLabels({"#", "VOLUME", "PAGES", "SIZE", "READ", "MODIFIED"});

    auto* hdr = m_table->horizontalHeader();
    hdr->setSectionResizeMode(ColNum,      QHeaderView::Fixed);
    hdr->setSectionResizeMode(ColVolume,   QHeaderView::Stretch);
    hdr->setSectionResizeMode(ColPages,    QHeaderView::Fixed);
    hdr->setSectionResizeMode(ColSize,     QHeaderView::Fixed);
    hdr->setSectionResizeMode(ColRead,     QHeaderView::Fixed);
    hdr->setSectionResizeMode(ColModified, QHeaderView::Fixed);
    m_table->setColumnWidth(ColNum,      42);
    m_table->setColumnWidth(ColPages,    70);
    m_table->setColumnWidth(ColSize,     90);
    m_table->setColumnWidth(ColRead,     84);
    m_table->setColumnWidth(ColModified, 132);
    hdr->setMinimumSectionSize(42);

    // Set progress icon delegate on READ column
    m_table->setItemDelegateForColumn(ColRead, new ProgressIconDelegate(m_table));

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
        auto* item = m_table->item(row, ColVolume);
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
                emit issueSelected(filePath, m_allCbzFiles, m_seriesRootName);
        }
    });

    // Context menu
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int row = m_table->rowAt(pos.y());
        if (row < 0) return;

        auto* titleItem = m_table->item(row, ColVolume);
        if (!titleItem) return;

        bool isFolder = titleItem->data(FolderRowRole).toBool();

        if (isFolder) {
            // ── Folder row context menu ──
            QString relPath = titleItem->data(FolderRelRole).toString();
            QString absPath = m_seriesRootPath;
            if (!relPath.isEmpty())
                absPath += "/" + relPath;

            auto* menu = ContextMenuHelper::createMenu(this);
            auto* openAct   = menu->addAction("Open folder");
            auto* revealAct = menu->addAction("Reveal in File Explorer");
            auto* copyAct   = menu->addAction("Copy folder path");

            auto* chosen = menu->exec(m_table->viewport()->mapToGlobal(pos));
            if (chosen == openAct) {
                navigateTo(relPath);
            } else if (chosen == revealAct) {
                ContextMenuHelper::revealInExplorer(absPath);
            } else if (chosen == copyAct) {
                ContextMenuHelper::copyToClipboard(absPath);
            }
            menu->deleteLater();
            return;
        }

        // ── File row context menu ──
        QString filePath = titleItem->data(FilePathRole).toString();
        QString progKey = QString(QCryptographicHash::hash(
            filePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));

        auto* menu = ContextMenuHelper::createMenu(this);
        auto* openAct = menu->addAction("Open");
        menu->addSeparator();

        // Set as series cover
        auto* setCoverAct = menu->addAction("Set as series cover");
        setCoverAct->setEnabled(!filePath.isEmpty());
        menu->addSeparator();

        // Mark as read / unread (toggle based on current state)
        bool isFinished = false;
        if (m_bridge) {
            QJsonObject prog = m_bridge->progress("comics", progKey);
            isFinished = prog.value("finished").toBool();
        }
        auto* markAct = menu->addAction(isFinished ? "Mark as unread" : "Mark as read");

        // Reset progress
        auto* resetAct = menu->addAction("Reset progress");
        resetAct->setEnabled(m_bridge != nullptr);
        menu->addSeparator();

        auto* revealAct = menu->addAction("Reveal in File Explorer");
        revealAct->setEnabled(!filePath.isEmpty());
        auto* copyAct = menu->addAction("Copy path");
        copyAct->setEnabled(!filePath.isEmpty());

        auto* chosen = menu->exec(m_table->viewport()->mapToGlobal(pos));
        if (chosen == openAct) {
            emit issueSelected(filePath, m_allCbzFiles, m_seriesRootName);
        } else if (chosen == setCoverAct && m_bridge) {
            // Save cover override to QSettings
            QString seriesHash = QString(QCryptographicHash::hash(
                m_seriesRootPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            QSettings settings;
            settings.setValue("comics_series_cover_override/" + seriesHash, filePath);
            // Re-render cover panel from the selected CBZ's first page
            QStringList pages = ArchiveReader::pageList(filePath);
            if (!pages.isEmpty()) {
                QByteArray imgData = ArchiveReader::pageData(filePath, pages.first());
                QPixmap pix;
                if (pix.loadFromData(imgData)) {
                    m_coverLabel->setPixmap(pix.scaled(208, 320,
                        Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                        .copy(0, 0, 208, 320));
                }
            }
        } else if (chosen == markAct && m_bridge) {
            QJsonObject prog = m_bridge->progress("comics", progKey);
            prog["finished"] = !isFinished;
            m_bridge->saveProgress("comics", progKey, prog);
            // Refresh this row's READ cell
            auto* readItem = m_table->item(row, ColRead);
            if (readItem) {
                if (!isFinished) {
                    readItem->setData(ProgressIconDelegate::ProgressStateRole, 2);
                    readItem->setData(ProgressIconDelegate::ProgressPctRole, 100);
                } else {
                    int page = prog.value("page").toInt(-1);
                    int pageCount = prog.value("pageCount").toInt(0);
                    if (page >= 0 && pageCount > 0) {
                        int pct = qBound(0, static_cast<int>((page + 1) * 100.0 / pageCount), 100);
                        readItem->setData(ProgressIconDelegate::ProgressStateRole, 1);
                        readItem->setData(ProgressIconDelegate::ProgressPctRole, pct);
                    } else {
                        readItem->setData(ProgressIconDelegate::ProgressStateRole, 0);
                    }
                }
            }
            buildContinueBar();
        } else if (chosen == resetAct && m_bridge) {
            m_bridge->clearProgress("comics", progKey);
            auto* readItem = m_table->item(row, ColRead);
            if (readItem)
                readItem->setData(ProgressIconDelegate::ProgressStateRole, 0);
            buildContinueBar();
        } else if (chosen == revealAct) {
            ContextMenuHelper::revealInExplorer(filePath);
        } else if (chosen == copyAct) {
            ContextMenuHelper::copyToClipboard(filePath);
        }
        menu->deleteLater();
    });

    contentLayout->addWidget(m_table, 1);
    layout->addWidget(contentRow, 1);
}

// ─── Public API ──────────────────────────────────────────────────────

void SeriesView::showSeries(const QString& seriesPath, const QString& seriesName,
                            const QString& coverThumbPath)
{
    m_seriesRootPath = seriesPath;
    m_seriesRootName = seriesName;
    m_currentRel.clear();
    m_searchBar->clear();

    // Reset navigation history
    m_navHistory.clear();
    m_navHistory.append(QString()); // root = empty relPath
    m_navIndex = 0;
    updateFwdBtn();

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
        // Placeholder with first letter + series name
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
        // Series name below
        p.setPen(QColor(160, 160, 160));
        font.setPixelSize(14);
        p.setFont(font);
        p.drawText(QRect(8, 220, 192, 60), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, seriesName);
        p.end();
        m_coverLabel->setPixmap(ph);
    }
    m_coverLabel->show();

    // Build flat list of all CBZ files for reader context
    m_allCbzFiles.clear();
    QCollator collator;
    collator.setNumericMode(true);
    QStringList allFiles = ScannerUtils::walkFiles(seriesPath, COMIC_EXTS);
    std::sort(allFiles.begin(), allFiles.end(), [&collator](const QString& a, const QString& b) {
        return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
    });
    m_allCbzFiles = allFiles;

    buildBreadcrumb();
    buildContinueBar();
    populateTable(seriesPath);
}

// ─── Navigation ──────────────────────────────────────────────────────

void SeriesView::navigateTo(const QString& relPath)
{
    // Push to history: truncate any forward entries beyond current position
    if (m_navIndex >= 0 && m_navIndex < m_navHistory.size() - 1)
        m_navHistory = m_navHistory.mid(0, m_navIndex + 1);
    m_navHistory.append(relPath);
    m_navIndex = m_navHistory.size() - 1;
    updateFwdBtn();

    m_currentRel = relPath;
    buildBreadcrumb();
    buildContinueBar();
    QString absPath = m_seriesRootPath;
    if (!relPath.isEmpty())
        absPath += "/" + relPath;
    populateTable(absPath);
}

void SeriesView::goBack()
{
    if (m_navIndex > 0) {
        --m_navIndex;
        QString relPath = m_navHistory.at(m_navIndex);
        m_currentRel = relPath;
        updateFwdBtn();
        buildBreadcrumb();
        buildContinueBar();
        QString absPath = m_seriesRootPath;
        if (!relPath.isEmpty())
            absPath += "/" + relPath;
        populateTable(absPath);
    } else {
        emit backRequested();
    }
}

void SeriesView::goForward()
{
    if (m_navIndex < m_navHistory.size() - 1) {
        ++m_navIndex;
        QString relPath = m_navHistory.at(m_navIndex);
        m_currentRel = relPath;
        updateFwdBtn();
        buildBreadcrumb();
        buildContinueBar();
        QString absPath = m_seriesRootPath;
        if (!relPath.isEmpty())
            absPath += "/" + relPath;
        populateTable(absPath);
    }
}

void SeriesView::updateFwdBtn()
{
    if (m_fwdBtn)
        m_fwdBtn->setEnabled(m_navIndex < m_navHistory.size() - 1);
}

// ─── Breadcrumb ──────────────────────────────────────────────────────

void SeriesView::buildBreadcrumb()
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

void SeriesView::populateTable(const QString& folderPath)
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
        auto* titleItem = m_table->item(row, ColVolume);
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
        QStringList files = ScannerUtils::walkFiles(subdir, COMIC_EXTS);
        if (files.isEmpty()) continue;

        QString dirName = QDir(subdir).dirName();
        QString relPath = m_currentRel.isEmpty() ? dirName : m_currentRel + "/" + dirName;
        QString unitSingular = (m_namingMode == "chapters") ? "chapter" : "volume";
        QString unitPlural   = (m_namingMode == "chapters") ? "chapters" : "volumes";
        QString countText = QString("    (%1 %2)")
            .arg(files.size())
            .arg(files.size() == 1 ? unitSingular : unitPlural);

        m_table->insertRow(row);
        m_table->setRowHeight(row, 38);
        for (int c = 0; c < ColCount; ++c) {
            auto* item = new QTableWidgetItem();
            item->setFont(boldFont);
            item->setBackground(folderBg);
            m_table->setItem(row, c, item);
        }
        auto* titleItem = m_table->item(row, ColVolume);
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
    auto fileInfos = dir.entryInfoList(COMIC_EXTS, QDir::Files);

    // Search filter
    if (!m_searchText.isEmpty()) {
        QList<QFileInfo> filtered;
        for (const auto& fi : fileInfos) {
            if (fi.completeBaseName().toLower().contains(m_searchText))
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

        auto* numItem = new QTableWidgetItem(QString::number(fileNum));
        numItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, ColNum, numItem);

        QString displayName = ScannerUtils::cleanMediaFolderTitle(fi.completeBaseName());
        auto* titleItem = new QTableWidgetItem(displayName);
        titleItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        titleItem->setData(FolderRowRole, false);
        titleItem->setData(FilePathRole, fi.absoluteFilePath());
        m_table->setItem(row, ColVolume, titleItem);

        // PAGES column — count images in CBZ archive
        auto* pagesItem = new QTableWidgetItem();
        pagesItem->setTextAlignment(Qt::AlignCenter);
        int archivePageCount = ArchiveReader::pageList(fi.absoluteFilePath()).size();
        pagesItem->setText(archivePageCount > 0 ? QString::number(archivePageCount) : "-");
        m_table->setItem(row, ColPages, pagesItem);

        auto* sizeItem = new QTableWidgetItem(formatSize(fi.size()));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, ColSize, sizeItem);

        // READ column — progress icons via ProgressIconDelegate
        auto* readItem = new QTableWidgetItem();
        readItem->setTextAlignment(Qt::AlignCenter);
        if (m_bridge) {
            QString progKey = QString(QCryptographicHash::hash(
                fi.absoluteFilePath().toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            QJsonObject prog = m_bridge->progress("comics", progKey);
            bool finished = prog.value("finished").toBool();
            int page = prog.value("page").toInt(-1);
            int pageCount = prog.value("pageCount").toInt(0);
            if (finished) {
                readItem->setData(ProgressIconDelegate::ProgressStateRole, 2);
                readItem->setData(ProgressIconDelegate::ProgressPctRole, 100);
            } else if (page >= 0 && pageCount > 0) {
                int pct = qBound(0, static_cast<int>((page + 1) * 100.0 / pageCount), 100);
                readItem->setData(ProgressIconDelegate::ProgressStateRole, 1);
                readItem->setData(ProgressIconDelegate::ProgressPctRole, pct);
            } else {
                readItem->setData(ProgressIconDelegate::ProgressStateRole, 0);
            }
        } else {
            readItem->setData(ProgressIconDelegate::ProgressStateRole, 0);
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

void SeriesView::buildContinueBar()
{
    m_continueBar->hide();
    m_continueFilePath.clear();

    if (!m_bridge) return;

    QJsonObject allProg = m_bridge->allProgress("comics");
    if (allProg.isEmpty()) return;

    qint64 bestAt = -1;
    QString bestPath;
    QString bestTitle;

    // Scope to current subfolder and its descendants (not entire series root) —
    // groundwork: continue reading shows the last-read file FROM THIS FOLDER
    QString scopePath = m_seriesRootPath;
    if (!m_currentRel.isEmpty())
        scopePath = m_seriesRootPath + "/" + m_currentRel;
    QStringList allFiles = ScannerUtils::walkFiles(scopePath, COMIC_EXTS);
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
    m_continueBar->show();
}

QString SeriesView::formatSize(qint64 bytes)
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
