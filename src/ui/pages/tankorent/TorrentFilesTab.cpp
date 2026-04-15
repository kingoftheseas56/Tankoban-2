#include "TorrentFilesTab.h"

#include "core/torrent/TorrentClient.h"
#include "core/torrent/TorrentEngine.h"
#include "core/TorrentResult.h"   // humanSize()

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QComboBox>
#include <QPushButton>
#include <QMenu>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QInputDialog>
#include <QMessageBox>

namespace {

// libtorrent priority scale: 0 = skip, 1-3 = low, 4-5 = normal, 6 = high, 7 = maximum.
// Combo indices 0..4 map to a canonical value in each bucket.
constexpr int kPrioSkip   = 0;
constexpr int kPrioLow    = 1;
constexpr int kPrioNormal = 4;
constexpr int kPrioHigh   = 6;
constexpr int kPrioMax    = 7;

const int ROLE_FILE_INDEX = Qt::UserRole + 1;

} // namespace

// ── Combo mapping ───────────────────────────────────────────────────────────

int TorrentFilesTab::priorityComboIndex(int libtorrentPriority)
{
    if (libtorrentPriority <= 0)      return 0; // Skip
    if (libtorrentPriority <= 3)      return 1; // Low
    if (libtorrentPriority <= 5)      return 2; // Normal
    if (libtorrentPriority == 6)      return 3; // High
    return 4;                                   // Maximum (7+)
}

int TorrentFilesTab::libtorrentPriorityForComboIndex(int idx)
{
    switch (idx) {
    case 0: return kPrioSkip;
    case 1: return kPrioLow;
    case 2: return kPrioNormal;
    case 3: return kPrioHigh;
    case 4: return kPrioMax;
    }
    return kPrioNormal;
}

QString TorrentFilesTab::priorityLabel(int libtorrentPriority)
{
    switch (priorityComboIndex(libtorrentPriority)) {
    case 0: return QStringLiteral("Skip");
    case 1: return QStringLiteral("Low");
    case 2: return QStringLiteral("Normal");
    case 3: return QStringLiteral("High");
    case 4: return QStringLiteral("Maximum");
    }
    return QStringLiteral("Normal");
}

// ── Construction ────────────────────────────────────────────────────────────

TorrentFilesTab::TorrentFilesTab(TorrentClient* client, QWidget* parent)
    : QWidget(parent), m_client(client)
{
    buildUI();
}

void TorrentFilesTab::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(8);

    m_tree = new QTreeWidget;
    m_tree->setObjectName("FilesTree");
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({ "Name", "Size", "Progress", "Priority" });
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->resizeSection(1, 110);
    m_tree->header()->resizeSection(2, 100);
    m_tree->header()->resizeSection(3, 130);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setStyleSheet(QStringLiteral(
        "#FilesTree { background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.06); "
        "border-radius: 6px; color: #eee; font-size: 12px; }"
        "#FilesTree::item { padding: 2px 4px; }"
        "#FilesTree QHeaderView::section { background: #1a1a1a; color: #888; border: none; "
        "border-right: 1px solid #222; border-bottom: 1px solid #222; padding: 4px 8px; font-size: 11px; }"
    ));
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &TorrentFilesTab::onTreeContextMenu);
    root->addWidget(m_tree, 1);

    auto* buttonRow = new QHBoxLayout;

    auto* selectAll = new QPushButton("Select All");
    selectAll->setCursor(Qt::PointingHandCursor);
    selectAll->setFixedHeight(26);
    connect(selectAll, &QPushButton::clicked, this, [this]() { applyBulkPriority(kPrioNormal); });
    buttonRow->addWidget(selectAll);

    auto* deselectAll = new QPushButton("Deselect All");
    deselectAll->setCursor(Qt::PointingHandCursor);
    deselectAll->setFixedHeight(26);
    connect(deselectAll, &QPushButton::clicked, this, [this]() { applyBulkPriority(kPrioSkip); });
    buttonRow->addWidget(deselectAll);

    buttonRow->addStretch();

    root->addLayout(buttonRow);
}

// ── Population ──────────────────────────────────────────────────────────────

void TorrentFilesTab::setInfoHash(const QString& infoHash)
{
    m_infoHash = infoHash;
    m_tree->clear();
    m_rows.clear();

    if (!m_client || infoHash.isEmpty())
        return;

    QString rootName;
    for (const auto& t : m_client->listActive()) {
        if (t.infoHash == infoHash) { rootName = t.name; break; }
    }
    populateTree(rootName);
}

void TorrentFilesTab::populateTree(const QString& /*rootName*/)
{
    const QJsonArray files = m_client->engine()->torrentFiles(m_infoHash);

    // Build directory tree. Keys are directory paths; values are parent items.
    QMap<QString, QTreeWidgetItem*> dirItems;

    for (const auto& v : files) {
        const QJsonObject obj = v.toObject();
        const int     idx      = obj.value("index").toInt();
        const QString fullPath = obj.value("name").toString();
        const qint64  sz       = obj.value("size").toVariant().toLongLong();
        const double  progress = obj.value("progress").toDouble();
        const int     prio     = obj.value("priority").toInt(kPrioNormal);

        const int sep = fullPath.lastIndexOf('/');
        const QString dir  = sep > 0 ? fullPath.left(sep) : QString();
        const QString name = sep > 0 ? fullPath.mid(sep + 1) : fullPath;

        QTreeWidgetItem* parent = nullptr;
        if (!dir.isEmpty()) {
            if (!dirItems.contains(dir)) {
                auto* d = new QTreeWidgetItem(m_tree);
                d->setText(0, dir);
                d->setData(0, ROLE_FILE_INDEX, -1);
                d->setExpanded(true);
                dirItems[dir] = d;
            }
            parent = dirItems[dir];
        }

        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, name);
        item->setData(0, ROLE_FILE_INDEX, idx);
        item->setText(1, humanSize(sz));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(2, QString::number(progress * 100.0, 'f', 1) + "%");
        item->setTextAlignment(2, Qt::AlignCenter);

        auto* combo = new QComboBox;
        combo->addItems({ "Skip", "Low", "Normal", "High", "Maximum" });
        combo->setCurrentIndex(priorityComboIndex(prio));
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, idx](int comboIdx) {
                    onPriorityCombo(idx, libtorrentPriorityForComboIndex(comboIdx));
                });
        m_tree->setItemWidget(item, 3, combo);

        FileRow row;
        row.index    = idx;
        row.fullPath = fullPath;
        row.size     = sz;
        row.item     = item;
        row.combo    = combo;
        m_rows.insert(idx, row);
    }
}

// ── Live refresh ────────────────────────────────────────────────────────────

void TorrentFilesTab::refresh()
{
    if (m_infoHash.isEmpty() || !m_client) return;

    const QJsonArray files = m_client->engine()->torrentFiles(m_infoHash);
    for (const auto& v : files) {
        const QJsonObject obj = v.toObject();
        const int idx = obj.value("index").toInt();
        auto it = m_rows.find(idx);
        if (it == m_rows.end()) continue;
        const double progress = obj.value("progress").toDouble();
        it->item->setText(2, QString::number(progress * 100.0, 'f', 1) + "%");
    }
}

// ── Priority edits ──────────────────────────────────────────────────────────

void TorrentFilesTab::onPriorityCombo(int /*fileIndex*/, int /*libtorrentPriority*/)
{
    // Push the entire current priority vector through to the engine. Pulling
    // per-file one at a time would still require building the full array on
    // each change, so do it in one shot from the rows map.
    writePrioritiesToEngine();
}

void TorrentFilesTab::writePrioritiesToEngine()
{
    if (m_infoHash.isEmpty() || !m_client) return;

    int maxIdx = -1;
    for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it)
        maxIdx = qMax(maxIdx, it.key());
    if (maxIdx < 0) return;

    QVector<int> priorities(maxIdx + 1, kPrioNormal);
    for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it) {
        const int comboIdx = it->combo ? it->combo->currentIndex() : 2;
        priorities[it.key()] = libtorrentPriorityForComboIndex(comboIdx);
    }
    m_client->engine()->setFilePriorities(m_infoHash, priorities);
}

void TorrentFilesTab::applyBulkPriority(int libtorrentPriority)
{
    const int comboIdx = priorityComboIndex(libtorrentPriority);
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        if (it->combo)
            it->combo->setCurrentIndex(comboIdx);   // triggers writePrioritiesToEngine once per
    }
    // Explicit write in case the bulk change didn't trigger the slot
    // (setCurrentIndex is a no-op when the index is unchanged).
    writePrioritiesToEngine();
}

// ── Context menu ────────────────────────────────────────────────────────────

void TorrentFilesTab::onTreeContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item) return;
    const int idx = item->data(0, ROLE_FILE_INDEX).toInt();
    const bool isFolder = (idx < 0);

    QMenu menu(this);

    auto setPriority = [this, item, idx, isFolder](int libtorrentPriority) {
        const int comboIdx = priorityComboIndex(libtorrentPriority);
        if (!isFolder) {
            auto rowIt = m_rows.find(idx);
            if (rowIt != m_rows.end() && rowIt->combo)
                rowIt->combo->setCurrentIndex(comboIdx);
        } else {
            // Apply to every descendant leaf
            for (int i = 0; i < item->childCount(); ++i) {
                QTreeWidgetItem* child = item->child(i);
                const int childIdx = child->data(0, ROLE_FILE_INDEX).toInt();
                if (childIdx < 0) continue;
                auto rowIt = m_rows.find(childIdx);
                if (rowIt != m_rows.end() && rowIt->combo)
                    rowIt->combo->setCurrentIndex(comboIdx);
            }
        }
        writePrioritiesToEngine();
    };

    menu.addAction("Skip",    this, [setPriority]() { setPriority(kPrioSkip); });
    menu.addAction("Low",     this, [setPriority]() { setPriority(kPrioLow); });
    menu.addAction("Normal",  this, [setPriority]() { setPriority(kPrioNormal); });
    menu.addAction("High",    this, [setPriority]() { setPriority(kPrioHigh); });
    menu.addAction("Maximum", this, [setPriority]() { setPriority(kPrioMax); });

    menu.addSeparator();

    if (!isFolder) {
        // Resolve on-disk path via torrent savePath + relative path in the row
        QString savePath;
        for (const auto& t : m_client->listActive()) {
            if (t.infoHash == m_infoHash) { savePath = t.savePath; break; }
        }
        const QString relPath = m_rows.value(idx).fullPath;
        const QString absPath = QDir(savePath).filePath(relPath);

        menu.addAction("Open File", this, [absPath]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(absPath));
        });
        menu.addAction("Open Containing Folder", this, [absPath]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(absPath).absolutePath()));
        });
        menu.addSeparator();
        menu.addAction("Rename...", this, [this, idx, item]() {
            const QString oldName = item->text(0);
            bool ok = false;
            const QString newName = QInputDialog::getText(this, "Rename File",
                "New name:", QLineEdit::Normal, oldName, &ok);
            if (!ok || newName.isEmpty() || newName == oldName) return;

            // libtorrent updates internally; we just need to rebuild the
            // target relative path (keep the same directory portion).
            const QString oldFullPath = m_rows.value(idx).fullPath;
            const int sep = oldFullPath.lastIndexOf('/');
            const QString newFullPath = sep > 0
                ? oldFullPath.left(sep + 1) + newName
                : newName;

            m_client->engine()->renameFile(m_infoHash, idx, newFullPath);
            item->setText(0, newName);
            auto rowIt = m_rows.find(idx);
            if (rowIt != m_rows.end()) rowIt->fullPath = newFullPath;
        });
    }

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}
