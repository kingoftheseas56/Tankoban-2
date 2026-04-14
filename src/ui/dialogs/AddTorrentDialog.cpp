#include "AddTorrentDialog.h"
#include "core/TorrentResult.h"  // for humanSize()

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMenu>
#include <QDir>
#include <QJsonObject>
#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <QIcon>

// ── Styling constants ───────────────────────────────────────────────────────
static const QString GOLD     = QStringLiteral("#c7a76b");
static const QString GLASS_BG = QStringLiteral("rgba(12, 12, 12, 0.95)");
static const QString BORDER   = QStringLiteral("rgba(255,255,255,0.08)");

static const QColor COLOR_SKIP(150, 150, 150);
static const QColor COLOR_NORMAL(238, 238, 238);
static const QColor COLOR_HIGH(234, 179, 8);
static const QColor COLOR_MAX(34, 197, 94);

static QString priorityText(int prio)
{
    switch (prio) {
    case 0:  return QStringLiteral("Skip");
    case 1:  return QStringLiteral("Normal");
    case 6:  return QStringLiteral("High");
    case 7:  return QStringLiteral("Maximum");
    case -1: return QStringLiteral("Mixed");
    default: return QStringLiteral("Normal");
    }
}

// ── Constructor ─────────────────────────────────────────────────────────────
AddTorrentDialog::AddTorrentDialog(const QString& torrentName,
                                   const QString& infoHash,
                                   const QMap<QString, QString>& defaultPaths,
                                   QWidget* parent)
    : QDialog(parent)
    , m_infoHash(infoHash)
    , m_defaultPaths(defaultPaths)
{
    setWindowTitle(QStringLiteral("Add Torrent"));
    setMinimumSize(820, 560);
    resize(1020, 680);
    setStyleSheet(QStringLiteral(
        "AddTorrentDialog { background: %1; border: 1px solid %2; border-radius: 12px; }"
    ).arg(GLASS_BG, BORDER));

    buildUI();

    m_nameEdit->setText(torrentName);
    m_hashLabel->setText(infoHash.isEmpty() ? QStringLiteral("—") : infoHash);
    m_statusLabel->setText(QStringLiteral("Resolving metadata..."));
    m_downloadBtn->setEnabled(false);
}

// ── Build UI ────────────────────────────────────────────────────────────────
void AddTorrentDialog::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    // ── 1. Torrent Info Header ──────────────────────────────────────────
    m_nameEdit = new QLineEdit;
    m_nameEdit->setStyleSheet(QStringLiteral(
        "font-size: 14px; font-weight: bold; background: transparent; "
        "border: 1px solid rgba(255,255,255,0.06); border-radius: 6px; padding: 6px 8px;"));
    root->addWidget(m_nameEdit);

    auto* infoRow = new QHBoxLayout;
    infoRow->setSpacing(16);
    m_hashLabel = new QLabel;
    m_hashLabel->setStyleSheet(QStringLiteral(
        "font-family: 'Consolas', 'Courier New', monospace; font-size: 11px; color: #888; cursor: pointer;"));
    m_hashLabel->setCursor(Qt::PointingHandCursor);
    m_hashLabel->setToolTip(QStringLiteral("Click to copy info hash"));
    connect(m_hashLabel, &QLabel::linkActivated, this, [](const QString&){});
    m_hashLabel->installEventFilter(this);
    infoRow->addWidget(m_hashLabel);
    infoRow->addStretch();
    m_sizeLabel = new QLabel;
    m_sizeLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));
    infoRow->addWidget(m_sizeLabel);
    root->addLayout(infoRow);

    // ── 2. Quick Presets ────────────────────────────────────────────────
    auto* presetRow = new QHBoxLayout;
    presetRow->setSpacing(8);
    auto* presetLabel = new QLabel(QStringLiteral("Quick presets"));
    presetLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));
    presetRow->addWidget(presetLabel);

    const QStringList presets = {
        QStringLiteral("Comics"), QStringLiteral("Books"),
        QStringLiteral("Audiobooks"), QStringLiteral("Videos")
    };
    for (const auto& p : presets) {
        auto* btn = new QPushButton(p);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(28);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.1); "
            "border-radius: 14px; padding: 2px 16px; font-size: 12px; color: #ddd; }"
            "QPushButton:hover { border-color: %1; color: %1; }").arg(GOLD));
        connect(btn, &QPushButton::clicked, this, [this, p]() { onPresetClicked(p.toLower()); });
        presetRow->addWidget(btn);
    }
    presetRow->addStretch();
    root->addLayout(presetRow);

    // ── 3. Options Grid ─────────────────────────────────────────────────
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);

    // Save to
    grid->addWidget(new QLabel(QStringLiteral("Save to")), 0, 0);
    auto* destRow = new QHBoxLayout;
    m_destEdit = new QLineEdit;
    m_destEdit->setPlaceholderText(QStringLiteral("Select download path..."));
    connect(m_destEdit, &QLineEdit::textEdited, this, [this]() {
        m_userChangedDest = true;
        updateStatusLabel();
    });
    destRow->addWidget(m_destEdit, 1);
    auto* browseBtn = new QPushButton(QStringLiteral("..."));
    browseBtn->setFixedSize(32, 28);
    browseBtn->setCursor(Qt::PointingHandCursor);
    connect(browseBtn, &QPushButton::clicked, this, &AddTorrentDialog::onDestinationBrowse);
    destRow->addWidget(browseBtn);
    grid->addLayout(destRow, 0, 1);

    m_destPreview = new QLabel;
    m_destPreview->setStyleSheet(QStringLiteral("font-size: 10px; color: #666;"));
    grid->addWidget(m_destPreview, 1, 1);

    // Category
    grid->addWidget(new QLabel(QStringLiteral("Category")), 2, 0);
    m_categoryCombo = new QComboBox;
    m_categoryCombo->addItem(QStringLiteral("Comics"),     QStringLiteral("comics"));
    m_categoryCombo->addItem(QStringLiteral("Books"),      QStringLiteral("books"));
    m_categoryCombo->addItem(QStringLiteral("Audiobooks"), QStringLiteral("audiobooks"));
    m_categoryCombo->addItem(QStringLiteral("Videos"),     QStringLiteral("videos"));
    m_categoryCombo->setCurrentIndex(3); // default Videos
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AddTorrentDialog::onCategoryChanged);
    grid->addWidget(m_categoryCombo, 2, 1);

    // Content layout
    grid->addWidget(new QLabel(QStringLiteral("Content layout")), 3, 0);
    m_layoutCombo = new QComboBox;
    m_layoutCombo->addItem(QStringLiteral("Original"),          QStringLiteral("original"));
    m_layoutCombo->addItem(QStringLiteral("Create subfolder"),  QStringLiteral("subfolder"));
    m_layoutCombo->addItem(QStringLiteral("No subfolder"),      QStringLiteral("no_subfolder"));
    grid->addWidget(m_layoutCombo, 3, 1);

    // Checkboxes
    auto* cbRow = new QHBoxLayout;
    m_sequentialCb = new QCheckBox(QStringLiteral("Sequential download"));
    cbRow->addWidget(m_sequentialCb);
    cbRow->addStretch();
    m_startCb = new QCheckBox(QStringLiteral("Start torrent"));
    m_startCb->setChecked(true);
    cbRow->addWidget(m_startCb);
    grid->addLayout(cbRow, 4, 0, 1, 2);

    root->addLayout(grid);

    // ── 4. File Tree ────────────────────────────────────────────────────
    // Controls above tree
    auto* treeControls = new QHBoxLayout;
    auto* selectAllBtn = new QPushButton(QStringLiteral("Select All"));
    selectAllBtn->setFixedHeight(26);
    selectAllBtn->setCursor(Qt::PointingHandCursor);
    connect(selectAllBtn, &QPushButton::clicked, this, &AddTorrentDialog::onSelectAll);
    treeControls->addWidget(selectAllBtn);

    auto* deselectAllBtn = new QPushButton(QStringLiteral("Deselect All"));
    deselectAllBtn->setFixedHeight(26);
    deselectAllBtn->setCursor(Qt::PointingHandCursor);
    connect(deselectAllBtn, &QPushButton::clicked, this, &AddTorrentDialog::onDeselectAll);
    treeControls->addWidget(deselectAllBtn);
    treeControls->addStretch();
    root->addLayout(treeControls);

    m_fileTree = new QTreeWidget;
    m_fileTree->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Size"), QStringLiteral("Priority")});
    m_fileTree->setColumnCount(3);
    m_fileTree->header()->setStretchLastSection(false);
    m_fileTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_fileTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_fileTree->header()->setMinimumSectionSize(80);
    m_fileTree->setAlternatingRowColors(true);
    m_fileTree->setRootIsDecorated(true);
    m_fileTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileTree->setMinimumHeight(200);
    m_fileTree->setStyleSheet(QStringLiteral(
        "QTreeWidget { background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.06); border-radius: 6px; }"
        "QTreeWidget::item { padding: 2px 0; }"
        "QTreeWidget::item:alternate { background: rgba(255,255,255,0.02); }"
        "QTreeWidget::item:selected { background: rgba(199,167,107,0.15); }"
        "QHeaderView::section { background: rgba(255,255,255,0.04); border: none; "
        "border-bottom: 1px solid rgba(255,255,255,0.08); padding: 4px 8px; font-size: 11px; color: #aaa; }"));

    connect(m_fileTree, &QTreeWidget::itemChanged,
            this, &AddTorrentDialog::onTreeItemChanged);
    connect(m_fileTree, &QTreeWidget::customContextMenuRequested,
            this, &AddTorrentDialog::showTreeContextMenu);
    root->addWidget(m_fileTree, 1);

    // ── 5. Status + Buttons ─────────────────────────────────────────────
    auto* bottomRow = new QHBoxLayout;
    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));
    bottomRow->addWidget(m_statusLabel, 1);

    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"));
    cancelBtn->setFixedHeight(32);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    bottomRow->addWidget(cancelBtn);

    m_downloadBtn = new QPushButton(QStringLiteral("Download"));
    m_downloadBtn->setFixedHeight(32);
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    m_downloadBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: #111; font-weight: bold; border-radius: 6px; "
        "padding: 4px 24px; border: none; }"
        "QPushButton:hover { background: #d4b87a; }"
        "QPushButton:disabled { background: rgba(255,255,255,0.08); color: #555; }").arg(GOLD));
    connect(m_downloadBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomRow->addWidget(m_downloadBtn);
    root->addLayout(bottomRow);
}

// ── Populate files from metadata ────────────────────────────────────────────
void AddTorrentDialog::populateFiles(const QString& name, qint64 totalSize, const QJsonArray& files)
{
    m_metadataReady = true;
    m_totalSize = totalSize;
    if (!name.isEmpty())
        m_nameEdit->setText(name);
    m_sizeLabel->setText(humanSize(totalSize));

    buildFileTree(files);

    m_downloadBtn->setEnabled(true);
    updateStatusLabel();
}

void AddTorrentDialog::showMetadataError(const QString& message)
{
    m_statusLabel->setText(QStringLiteral("Error: ") + message);
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: rgb(220,50,50);"));
}

void AddTorrentDialog::buildFileTree(const QJsonArray& files)
{
    m_treeSyncing = true;
    m_fileTree->clear();
    m_fileIndices.clear();

    // Build folder hierarchy
    QMap<QString, QTreeWidgetItem*> folderItems;

    for (const auto& val : files) {
        auto obj = val.toObject();
        int index = obj["index"].toInt();
        QString path = obj["name"].toString();
        qint64 size = obj["size"].toVariant().toLongLong();

        QStringList parts = path.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;

        // Create folder hierarchy
        QTreeWidgetItem* parentItem = nullptr;
        QString folderPath;
        for (int i = 0; i < parts.size() - 1; ++i) {
            folderPath += (i > 0 ? "/" : "") + parts[i];
            if (!folderItems.contains(folderPath)) {
                auto* folderItem = new QTreeWidgetItem;
                folderItem->setIcon(0, QIcon(QStringLiteral(":/icons/folder.svg")));
                folderItem->setText(0, parts[i]);
                folderItem->setCheckState(0, Qt::Checked);
                folderItem->setData(0, ROLE_IS_FOLDER, true);
                folderItem->setData(0, ROLE_PRIORITY, PRIORITY_NORMAL);
                folderItem->setFlags(folderItem->flags() | Qt::ItemIsAutoTristate | Qt::ItemIsUserCheckable);
                QFont f = folderItem->font(0);
                f.setBold(true);
                folderItem->setFont(0, f);

                if (parentItem)
                    parentItem->addChild(folderItem);
                else
                    m_fileTree->addTopLevelItem(folderItem);

                folderItems[folderPath] = folderItem;
            }
            parentItem = folderItems[folderPath];
        }

        // Create file item
        auto* fileItem = new QTreeWidgetItem;
        fileItem->setIcon(0, QIcon(QStringLiteral(":/icons/file.svg")));
        fileItem->setText(0, parts.last());
        fileItem->setText(1, humanSize(size));
        fileItem->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        fileItem->setText(2, QStringLiteral("Normal"));
        fileItem->setCheckState(0, Qt::Checked);
        fileItem->setData(0, ROLE_FILE_INDEX, index);
        fileItem->setData(0, ROLE_IS_FOLDER, false);
        fileItem->setData(0, ROLE_PRIORITY, PRIORITY_NORMAL);
        fileItem->setFlags(fileItem->flags() | Qt::ItemIsUserCheckable);

        if (parentItem)
            parentItem->addChild(fileItem);
        else
            m_fileTree->addTopLevelItem(fileItem);

        m_fileIndices[fileItem] = index;
    }

    m_fileTree->expandAll();
    m_treeSyncing = false;

    // Apply initial colors
    for (int i = 0; i < m_fileTree->topLevelItemCount(); ++i)
        updateItemColors(m_fileTree->topLevelItem(i));
}

// ── Presets ─────────────────────────────────────────────────────────────────
void AddTorrentDialog::onPresetClicked(const QString& preset)
{
    // Find category index
    for (int i = 0; i < m_categoryCombo->count(); ++i) {
        if (m_categoryCombo->itemData(i).toString() == preset) {
            m_categoryCombo->setCurrentIndex(i);
            break;
        }
    }

    // Set destination
    QString dest = defaultDestForCategory(preset);
    if (!dest.isEmpty()) {
        m_destEdit->setText(dest);
        m_userChangedDest = false;
    }

    // Sequential for comics/books
    m_sequentialCb->setChecked(preset == "comics" || preset == "books");

    updateStatusLabel();
}

void AddTorrentDialog::onCategoryChanged(int /*index*/)
{
    if (m_userChangedDest) return;
    QString cat = m_categoryCombo->currentData().toString();
    QString dest = defaultDestForCategory(cat);
    if (!dest.isEmpty())
        m_destEdit->setText(dest);
    updateStatusLabel();
}

void AddTorrentDialog::onDestinationBrowse()
{
    QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Download Folder"),
                                                     m_destEdit->text());
    if (!dir.isEmpty()) {
        m_destEdit->setText(dir);
        m_userChangedDest = true;
        updateStatusLabel();
    }
}

QString AddTorrentDialog::defaultDestForCategory(const QString& category) const
{
    return m_defaultPaths.value(category);
}

// ── Select All / Deselect All ───────────────────────────────────────────────
void AddTorrentDialog::onSelectAll()
{
    m_treeSyncing = true;
    for (int i = 0; i < m_fileTree->topLevelItemCount(); ++i) {
        auto* item = m_fileTree->topLevelItem(i);
        item->setCheckState(0, Qt::Checked);
        setItemPriority(item, PRIORITY_NORMAL, true);
    }
    m_treeSyncing = false;
    updateStatusLabel();
}

void AddTorrentDialog::onDeselectAll()
{
    m_treeSyncing = true;
    for (int i = 0; i < m_fileTree->topLevelItemCount(); ++i) {
        auto* item = m_fileTree->topLevelItem(i);
        item->setCheckState(0, Qt::Unchecked);
        setItemPriority(item, PRIORITY_SKIP, true);
    }
    m_treeSyncing = false;
    updateStatusLabel();
}

// ── Tree checkbox change → bidirectional sync ───────────────────────────────
void AddTorrentDialog::onTreeItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_treeSyncing || column != 0) return;
    m_treeSyncing = true;

    bool checked = (item->checkState(0) == Qt::Checked);
    int newPrio = checked ? PRIORITY_NORMAL : PRIORITY_SKIP;

    // If this item has children (folder), cascade
    if (item->childCount() > 0) {
        setItemPriority(item, newPrio, true);
        syncChildrenFromParent(item);
    } else {
        setItemPriority(item, newPrio, false);
    }

    // Update parent state
    if (item->parent())
        syncParentFromChildren(item->parent());

    m_treeSyncing = false;
    updateStatusLabel();
}

void AddTorrentDialog::syncChildrenFromParent(QTreeWidgetItem* parent)
{
    Qt::CheckState state = parent->checkState(0);
    int prio = (state == Qt::Checked) ? PRIORITY_NORMAL : PRIORITY_SKIP;

    for (int i = 0; i < parent->childCount(); ++i) {
        auto* child = parent->child(i);
        child->setCheckState(0, state);
        setItemPriority(child, prio, false);
        updateItemColors(child);
        if (child->childCount() > 0)
            syncChildrenFromParent(child);
    }
    updateItemColors(parent);
}

void AddTorrentDialog::syncParentFromChildren(QTreeWidgetItem* parent)
{
    if (!parent) return;

    int checked = 0, unchecked = 0, total = parent->childCount();
    for (int i = 0; i < total; ++i) {
        if (parent->child(i)->checkState(0) == Qt::Checked)
            ++checked;
        else if (parent->child(i)->checkState(0) == Qt::Unchecked)
            ++unchecked;
    }

    Qt::CheckState newState;
    if (checked == total)
        newState = Qt::Checked;
    else if (unchecked == total)
        newState = Qt::Unchecked;
    else
        newState = Qt::PartiallyChecked;

    parent->setCheckState(0, newState);

    // Folder priority reflects children
    if (checked == total) {
        // Check if all same priority
        int firstPrio = itemPriority(parent->child(0));
        bool allSame = true;
        for (int i = 1; i < total; ++i) {
            if (itemPriority(parent->child(i)) != firstPrio) { allSame = false; break; }
        }
        parent->setData(0, ROLE_PRIORITY, allSame ? firstPrio : PRIORITY_MIXED);
        parent->setText(2, allSame ? priorityText(firstPrio) : QStringLiteral("Mixed"));
    } else if (unchecked == total) {
        parent->setData(0, ROLE_PRIORITY, PRIORITY_SKIP);
        parent->setText(2, QStringLiteral("Skip"));
    } else {
        parent->setData(0, ROLE_PRIORITY, PRIORITY_MIXED);
        parent->setText(2, QStringLiteral("Mixed"));
    }

    updateItemColors(parent);

    if (parent->parent())
        syncParentFromChildren(parent->parent());
}

// ── Priority helpers ────────────────────────────────────────────────────────
void AddTorrentDialog::setItemPriority(QTreeWidgetItem* item, int priority, bool propagate)
{
    item->setData(0, ROLE_PRIORITY, priority);
    item->setText(2, priorityText(priority));

    // Sync checkbox with priority
    if (priority == PRIORITY_SKIP)
        item->setCheckState(0, Qt::Unchecked);
    else if (item->checkState(0) == Qt::Unchecked && priority > 0)
        item->setCheckState(0, Qt::Checked);

    updateItemColors(item);

    if (propagate && item->childCount() > 0) {
        for (int i = 0; i < item->childCount(); ++i) {
            setItemPriority(item->child(i), priority, true);
        }
    }
}

int AddTorrentDialog::itemPriority(QTreeWidgetItem* item) const
{
    return item->data(0, ROLE_PRIORITY).toInt();
}

void AddTorrentDialog::updateItemColors(QTreeWidgetItem* item)
{
    int prio = itemPriority(item);
    QColor color;
    switch (prio) {
    case PRIORITY_SKIP:  color = COLOR_SKIP; break;
    case PRIORITY_HIGH:  color = COLOR_HIGH; break;
    case PRIORITY_MAX:   color = COLOR_MAX;  break;
    default:             color = COLOR_NORMAL; break;
    }
    item->setForeground(0, color);
    item->setForeground(1, color);
    item->setForeground(2, color);

    for (int i = 0; i < item->childCount(); ++i)
        updateItemColors(item->child(i));
}

// ── Context menu ────────────────────────────────────────────────────────────
void AddTorrentDialog::showTreeContextMenu(const QPoint& pos)
{
    auto* item = m_fileTree->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    menu.addAction(QStringLiteral("Select for Download"), this, [this, item]() {
        m_treeSyncing = true;
        setItemPriority(item, PRIORITY_NORMAL, true);
        if (item->parent()) syncParentFromChildren(item->parent());
        m_treeSyncing = false;
        updateStatusLabel();
    });
    menu.addAction(QStringLiteral("Skip"), this, [this, item]() {
        m_treeSyncing = true;
        setItemPriority(item, PRIORITY_SKIP, true);
        if (item->parent()) syncParentFromChildren(item->parent());
        m_treeSyncing = false;
        updateStatusLabel();
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Priority: Normal"), this, [this, item]() {
        m_treeSyncing = true;
        setItemPriority(item, PRIORITY_NORMAL, true);
        if (item->parent()) syncParentFromChildren(item->parent());
        m_treeSyncing = false;
    });
    menu.addAction(QStringLiteral("Priority: High"), this, [this, item]() {
        m_treeSyncing = true;
        setItemPriority(item, PRIORITY_HIGH, true);
        if (item->parent()) syncParentFromChildren(item->parent());
        m_treeSyncing = false;
    });
    menu.addAction(QStringLiteral("Priority: Maximum"), this, [this, item]() {
        m_treeSyncing = true;
        setItemPriority(item, PRIORITY_MAX, true);
        if (item->parent()) syncParentFromChildren(item->parent());
        m_treeSyncing = false;
    });

    menu.exec(m_fileTree->viewport()->mapToGlobal(pos));
}

void AddTorrentDialog::onPriorityChanged(QTreeWidgetItem* item, int newPriority)
{
    if (m_treeSyncing) return;
    m_treeSyncing = true;
    setItemPriority(item, newPriority, true);
    if (item->parent()) syncParentFromChildren(item->parent());
    m_treeSyncing = false;
    updateStatusLabel();
}

// ── Status label ────────────────────────────────────────────────────────────
void AddTorrentDialog::updateStatusLabel()
{
    if (!m_metadataReady) {
        m_statusLabel->setText(QStringLiteral("Resolving metadata..."));
        return;
    }

    int selectedCount = 0;
    qint64 selectedSize = 0;
    QMapIterator<QTreeWidgetItem*, int> it(m_fileIndices);
    while (it.hasNext()) {
        it.next();
        if (it.key()->checkState(0) == Qt::Checked) {
            ++selectedCount;
            // Parse size from display text
            // Better: store actual size in item data
        }
    }

    QString dest = m_destEdit->text().trimmed();
    QString resolved = dest.isEmpty() ? QStringLiteral("(not set)")
                                       : QDir(dest).absolutePath();
    m_destPreview->setText(QStringLiteral("→ ") + resolved);

    m_statusLabel->setText(QStringLiteral("Ready — %1 files selected, %2 total")
                           .arg(selectedCount).arg(humanSize(m_totalSize)));
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));
}

// ── Config extraction ───────────────────────────────────────────────────────
AddTorrentConfig AddTorrentDialog::config() const
{
    AddTorrentConfig cfg;
    cfg.category        = m_categoryCombo->currentData().toString();
    cfg.destinationPath = m_destEdit->text().trimmed();
    cfg.contentLayout   = m_layoutCombo->currentData().toString();
    cfg.sequential      = m_sequentialCb->isChecked();
    cfg.startPaused     = !m_startCb->isChecked();

    QMapIterator<QTreeWidgetItem*, int> it(m_fileIndices);
    while (it.hasNext()) {
        it.next();
        int fileIdx = it.value();
        int prio = itemPriority(it.key());

        cfg.filePriorities[fileIdx] = prio;
        if (prio > 0)
            cfg.selectedIndices.append(fileIdx);
    }
    std::sort(cfg.selectedIndices.begin(), cfg.selectedIndices.end());

    return cfg;
}

// ── Event filter for hash copy ──────────────────────────────────────────────
bool AddTorrentDialog::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_hashLabel && event->type() == QEvent::MouseButtonPress) {
        QApplication::clipboard()->setText(m_infoHash);
        m_hashLabel->setText(m_infoHash + QStringLiteral("  ✓ copied"));
        QTimer::singleShot(1500, this, [this]() {
            m_hashLabel->setText(m_infoHash);
        });
        return true;
    }
    return QDialog::eventFilter(obj, event);
}
