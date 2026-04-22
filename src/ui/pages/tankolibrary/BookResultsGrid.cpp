#include "BookResultsGrid.h"

#include <QHeaderView>
#include <QIcon>
#include <QSize>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
// Cover column dims — 48px wide column, 40x60 icon (KeepAspectRatio letterbox
// inside). Row height 60px aligns with icon height to prevent clipping.
constexpr int kCoverColWidth = 48;
constexpr int kCoverIconW    = 40;
constexpr int kCoverIconH    = 60;
constexpr int kRowHeight     = 60;
}

BookResultsGrid::BookResultsGrid(QWidget* parent)
    : QWidget(parent)
{
    buildUI();
}

void BookResultsGrid::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels(
        { "", "Title", "Author", "Format", "Year", "Size", "Language", "Source" });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);          // Cover
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);        // Title
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);        // Author
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    m_table->setColumnWidth(0, kCoverColWidth);
    m_table->setIconSize(QSize(kCoverIconW, kCoverIconH));
    m_table->verticalHeader()->setDefaultSectionSize(kRowHeight);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, &BookResultsGrid::onCellDoubleClicked);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
                const int row = m_table->rowAt(pos.y());
                if (row < 0) return;
                emit resultRightClicked(row, m_table->viewport()->mapToGlobal(pos));
            });

    root->addWidget(m_table);
}

void BookResultsGrid::setResults(const QList<BookResult>& results)
{
    m_rows = results;
    m_table->setRowCount(m_rows.size());
    for (int i = 0; i < m_rows.size(); ++i) {
        const BookResult& r = m_rows[i];
        // Column 0 — Cover placeholder (empty QTableWidgetItem so row-click
        // still targets the right row; setCoverPixmap fills the icon later).
        m_table->setItem(i, 0, new QTableWidgetItem());
        m_table->setItem(i, 1, new QTableWidgetItem(r.title));
        m_table->setItem(i, 2, new QTableWidgetItem(r.author));
        m_table->setItem(i, 3, new QTableWidgetItem(r.format.toUpper()));
        m_table->setItem(i, 4, new QTableWidgetItem(r.year));
        m_table->setItem(i, 5, new QTableWidgetItem(r.fileSize));
        m_table->setItem(i, 6, new QTableWidgetItem(r.language));
        m_table->setItem(i, 7, new QTableWidgetItem(bookSourceDisplayName(r.source)));
    }
}

void BookResultsGrid::clearResults()
{
    m_rows.clear();
    m_table->setRowCount(0);
}

int BookResultsGrid::resultCount() const
{
    return m_rows.size();
}

void BookResultsGrid::setCoverPixmap(int row, const QPixmap& pixmap)
{
    if (!m_table || row < 0 || row >= m_table->rowCount()) return;
    if (pixmap.isNull()) return;
    QTableWidgetItem* item = m_table->item(row, 0);
    if (!item) {
        item = new QTableWidgetItem();
        m_table->setItem(row, 0, item);
    }
    // Scale once to the icon size so the QTableWidget doesn't rescale on paint.
    const QPixmap scaled = pixmap.scaled(
        QSize(kCoverIconW, kCoverIconH),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    item->setIcon(QIcon(scaled));
}

void BookResultsGrid::onCellDoubleClicked(int row, int /*col*/)
{
    if (row < 0 || row >= m_rows.size()) return;
    emit resultActivated(row);
}
