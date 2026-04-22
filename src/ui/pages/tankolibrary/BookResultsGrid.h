#pragma once

#include <QWidget>
#include <QList>

#include "core/book/BookResult.h"

class QTableWidget;

// M1 shape: a QTableWidget wrapper that displays BookResult rows with columns
// Title / Author / Format / Year / Size / Language / Source. Named "Grid"
// per TODO §5 contract even though the M1 implementation is a list/table —
// tile-grid refactor is a Track B (polish) task.
class BookResultsGrid : public QWidget
{
    Q_OBJECT

public:
    explicit BookResultsGrid(QWidget* parent = nullptr);

    void setResults(const QList<BookResult>& results);
    void clearResults();
    int  resultCount() const;

signals:
    void resultActivated(int row);        // double-click / Enter
    void resultRightClicked(int row, const QPoint& globalPos);

private:
    void buildUI();
    void onCellDoubleClicked(int row, int col);

    QTableWidget* m_table = nullptr;
    QList<BookResult> m_rows;
};
