#pragma once

#include <QPixmap>
#include <QWidget>
#include <QList>

#include "core/book/BookResult.h"

class QTableWidget;

// Columns: Cover / Title / Author / Format / Year / Size / Language / Source.
// Track B closeout: leftmost Cover column added as a 48px icon slot; lazily
// populated by TankoLibraryPage via setCoverPixmap(). Rows use fixed 60px
// height so the 40x60 cover letterbox reads cleanly.
class BookResultsGrid : public QWidget
{
    Q_OBJECT

public:
    explicit BookResultsGrid(QWidget* parent = nullptr);

    void setResults(const QList<BookResult>& results);
    void clearResults();
    int  resultCount() const;

    // Track B closeout — paint a thumbnail into column 0 for the given row.
    // No-op on invalid row / null pixmap.
    void setCoverPixmap(int row, const QPixmap& pixmap);

signals:
    void resultActivated(int row);        // double-click / Enter
    void resultRightClicked(int row, const QPoint& globalPos);

private:
    void buildUI();
    void onCellDoubleClicked(int row, int col);

    QTableWidget* m_table = nullptr;
    QList<BookResult> m_rows;
};
