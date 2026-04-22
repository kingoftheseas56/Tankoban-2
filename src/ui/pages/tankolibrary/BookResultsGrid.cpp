#include "BookResultsGrid.h"

#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>

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
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels(
        { "Title", "Author", "Format", "Year", "Size", "Language", "Source" });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);       // Title
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);       // Author
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
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
        m_table->setItem(i, 0, new QTableWidgetItem(r.title));
        m_table->setItem(i, 1, new QTableWidgetItem(r.author));
        m_table->setItem(i, 2, new QTableWidgetItem(r.format.toUpper()));
        m_table->setItem(i, 3, new QTableWidgetItem(r.year));
        m_table->setItem(i, 4, new QTableWidgetItem(r.fileSize));
        m_table->setItem(i, 5, new QTableWidgetItem(r.language));
        m_table->setItem(i, 6, new QTableWidgetItem(bookSourceDisplayName(r.source)));
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

void BookResultsGrid::onCellDoubleClicked(int row, int /*col*/)
{
    if (row < 0 || row >= m_rows.size()) return;
    emit resultActivated(row);
}
