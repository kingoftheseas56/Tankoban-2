#include "LibraryListView.h"

#include <QHeaderView>
#include <QMouseEvent>
#include <QDateTime>
#include <QMenu>

LibraryListView::LibraryListView(QWidget* parent)
    : QTreeWidget(parent)
{
    setObjectName("LibraryListView");
    setRootIsDecorated(false);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setAlternatingRowColors(true);
    setIndentation(0);
    setSortingEnabled(true);

    setColumnCount(3);
    setHeaderLabels({"Name", "Items", "Last Modified"});

    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::Fixed);
    header()->setSectionResizeMode(2, QHeaderView::Fixed);
    header()->resizeSection(1, 80);
    header()->resizeSection(2, 140);

    setStyleSheet(
        "QTreeWidget#LibraryListView { background: transparent; border: none; "
        "color: #ddd; font-size: 13px; }"
        "QTreeWidget#LibraryListView::item { padding: 4px 8px; }"
        "QTreeWidget#LibraryListView::item:selected { background: rgba(255,255,255,0.08); }"
        "QTreeWidget#LibraryListView::item:hover { background: rgba(255,255,255,0.04); }"
        "QHeaderView::section { background: rgba(255,255,255,0.04); color: rgba(255,255,255,0.5); "
        "border: none; border-bottom: 1px solid rgba(255,255,255,0.08); "
        "padding: 6px 8px; font-size: 11px; font-weight: bold; }");

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = itemAt(pos);
        if (!item) return;
        QString path = item->data(0, Qt::UserRole).toString();
        emit itemRightClicked(path, viewport()->mapToGlobal(pos));
    });
}

void LibraryListView::clear()
{
    QTreeWidget::clear();
    m_rows.clear();
}

void LibraryListView::addItem(const ItemData& data)
{
    auto* item = new QTreeWidgetItem();
    item->setText(0, data.name);
    item->setData(0, Qt::UserRole, data.path);
    item->setText(1, QString::number(data.itemCount));
    item->setTextAlignment(1, Qt::AlignCenter);

    if (data.lastModifiedMs > 0) {
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(data.lastModifiedMs);
        item->setText(2, dt.toString("MM/dd/yyyy"));
    } else {
        item->setText(2, "-");
    }
    item->setTextAlignment(2, Qt::AlignCenter);

    // Sort data
    item->setData(1, Qt::UserRole, data.itemCount);
    item->setData(2, Qt::UserRole, data.lastModifiedMs);

    Row row{data, item};
    m_rows.append(row);

    // Check filters before adding
    bool passText = m_textFilter.isEmpty() || data.name.toLower().contains(m_textFilter);
    bool passRoot = m_rootFilter.isEmpty() || data.path.startsWith(m_rootFilter);

    if (passText && passRoot)
        addTopLevelItem(item);
}

void LibraryListView::setRootFilter(const QString& rootPath)
{
    m_rootFilter = rootPath;
    applyFilters();
}

void LibraryListView::setTextFilter(const QString& query)
{
    m_textFilter = query.trimmed().toLower();
    applyFilters();
}

void LibraryListView::applyFilters()
{
    // Remove all items without deleting them
    while (topLevelItemCount() > 0)
        takeTopLevelItem(0);

    for (auto& row : m_rows) {
        bool passText = m_textFilter.isEmpty() ||
                        row.data.name.toLower().contains(m_textFilter);
        bool passRoot = m_rootFilter.isEmpty() ||
                        row.data.path.startsWith(m_rootFilter);
        if (passText && passRoot)
            addTopLevelItem(row.item);
    }
}

void LibraryListView::mouseDoubleClickEvent(QMouseEvent* event)
{
    auto* item = itemAt(event->pos());
    if (item) {
        QString path = item->data(0, Qt::UserRole).toString();
        emit itemActivated(path);
    }
    QTreeWidget::mouseDoubleClickEvent(event);
}
