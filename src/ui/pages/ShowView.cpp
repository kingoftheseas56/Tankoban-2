#include "ShowView.h"
#include "core/ScannerUtils.h"
#include "core/CoreBridge.h"

#include <QPushButton>
#include <QHeaderView>
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
#include <algorithm>

static const QStringList VIDEO_EXTS = {
    "*.mp4", "*.mkv", "*.avi", "*.webm", "*.mov", "*.wmv", "*.flv",
    "*.m4v", "*.ts", "*.mpg", "*.mpeg", "*.ogv"
};

// Column indices
enum Col { ColNum = 0, ColEpisode, ColSize, ColDuration, ColProgress, ColModified, ColCount };

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
    connect(backBtn, &QPushButton::clicked, this, &ShowView::backRequested);
    topLayout->addWidget(backBtn);

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

    // ── Table ──
    m_table = new QTableWidget(this);
    m_table->setObjectName("FolderDetailTable");
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

    layout->addWidget(m_table, 1);
}

// ─── Public API ──────────────────────────────────────────────────────

void ShowView::showFolder(const QString& folderPath, const QString& showName)
{
    m_showRootPath = folderPath;
    m_showRootName = showName;
    m_currentRel.clear();
    m_searchBar->clear();

    buildBreadcrumb();
    populateTable(folderPath);
}

// ─── Navigation ──────────────────────────────────────────────────────

void ShowView::navigateTo(const QString& relPath)
{
    m_currentRel = relPath;
    buildBreadcrumb();

    QString absPath = m_showRootPath;
    if (!relPath.isEmpty())
        absPath += "/" + relPath;
    populateTable(absPath);
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

    // ── Folder rows ──
    QStringList subdirs = ScannerUtils::listImmediateSubdirs(folderPath);
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

        // Duration (from progress data — only available for previously played videos)
        double durSec = prog.value("durationSec").toDouble(0);
        auto* durItem = new QTableWidgetItem(formatDuration(durSec));
        durItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, ColDuration, durItem);

        // Progress
        bool finished = prog.value("finished").toBool(false);
        double posSec = prog.value("positionSec").toDouble(0);
        auto* progItem = new QTableWidgetItem();
        progItem->setTextAlignment(Qt::AlignCenter);
        if (finished) {
            progItem->setText("Done");
            progItem->setForeground(QColor("#4CAF50"));
        } else if (posSec > 0 && durSec > 0) {
            int pct = qBound(0, static_cast<int>(posSec / durSec * 100), 100);
            progItem->setText(QString::number(pct) + "%");
        } else {
            progItem->setText("-");
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
