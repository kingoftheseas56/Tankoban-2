#pragma once

#include <QList>
#include <QString>
#include <QWidget>

#include "core/book/BookCatalogueResult.h"

class BooksCatalogueLibraryStore;
class QLabel;
class QVBoxLayout;

// Series-shape detail view (BOOKS_FICTIONDB_CATALOGUE §4.4).
//
// Hero (series cover + name + author + book count) + the member books in
// reading order. Each row carries a per-book action:
//   - on disk → [Read] (emits bookReadRequested → opens the reader)
//   - not yet → [Get]  (emits bookOpenRequested → BooksPage routes to the
//                        movie-shape BookCatalogueDetailView for that book,
//                        which runs the existing §5.2 source-search + download)
// Lazy per D3: no source search fires until a row's button is clicked. Rows
// re-derive Read/Get on the store's recordsChanged.
class BookSeriesDetailView : public QWidget
{
    Q_OBJECT

public:
    explicit BookSeriesDetailView(QWidget* parent = nullptr);

    void setCatalogueStore(BooksCatalogueLibraryStore* store);
    void showSeries(const QString& seriesName, const QString& author,
                    const QString& coverPath, const QList<BookCatalogueResult>& books);

signals:
    void backRequested();
    void bookOpenRequested(const BookCatalogueResult& book);
    void bookReadRequested(const QString& catalogueId, const QString& filePath);

private:
    void buildUi();
    void rebuildRows();

    BooksCatalogueLibraryStore* m_store = nullptr;
    QString m_seriesName;
    QString m_author;
    QString m_coverPath;
    QList<BookCatalogueResult> m_books;   // ordered by seriesPosition

    QLabel* m_coverLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QWidget* m_rowsContainer = nullptr;
    QVBoxLayout* m_rowsLayout = nullptr;
};
