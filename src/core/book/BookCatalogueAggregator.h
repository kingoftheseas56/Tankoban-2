#pragma once

#include <QObject>
#include <QList>
#include <QSet>
#include <QString>

#include "BookCatalogueResult.h"
#include "SeriesDetector.h"   // SeriesGroup wire-type

class FictionDbClient;

// Top-of-the-catalogue layer for Books mode (BOOKS_FICTIONDB_CATALOGUE).
//
// Two-track search, both fed from FictionDB's comprehensive free-text book
// search (FictionDB has no enumerable series directory, but every book page
// self-declares its series — so series are reached *through* the books):
//
//   1. BOOKS track  — the search results, re-ranked locally so the obvious
//      title floats up (FictionDB's native order isn't relevance-ranked).
//      Emitted immediately.
//   2. SERIES track — "Top-N resolution": fetch the top-N result book pages,
//      read each book's series link, group same-series books into one tile.
//      Series-member books then fold out of the BOOKS track. Emitted once the
//      resolutions land (a beat after the books).
//
// query() therefore emits aggregateReady twice: books-only first, then
// series + folded-standalones. A superseded query is dropped via a generation
// guard + the current-query check.
class BookCatalogueAggregator : public QObject
{
    Q_OBJECT

public:
    explicit BookCatalogueAggregator(FictionDbClient* fictiondb, QObject* parent = nullptr);

    void query(const QString& q);

    // Pure: re-rank live FictionDB book results so exact/prefix title (+author)
    // matches float to the top. Exposed for unit tests.
    static QList<BookCatalogueResult> rerankBooks(const QString& query,
                                                  QList<BookCatalogueResult> books);

signals:
    void aggregateReady(const QString& query,
                        const QList<SeriesDetector::SeriesGroup>& seriesGroups,
                        const QList<BookCatalogueResult>& standalones);
    void aggregateFailed(const QString& query, const QString& error);

private:
    void onFictionResults(const QString& query, const QList<BookCatalogueResult>& books);
    void onFictionFailed(const QString& query, const QString& error);
    void onBookResolved(const QString& bookId, const BookCatalogueResult& book);
    void onBookResolveFailed(const QString& bookId, const QString& error);
    void emitGrouped();

    FictionDbClient* m_fictiondb = nullptr;
    QString m_currentQuery;
    int m_generation = 0;

    QList<BookCatalogueResult> m_books;       // re-ranked search results (Books track)
    QSet<QString> m_pendingBookIds;           // top-N slugs awaiting fetchBook
    QList<BookCatalogueResult> m_resolved;    // resolved top-N books (carry series info)

    static constexpr int kTopN = 8;           // book pages peeked per search for series grouping (kept low for snappy single-paint)
};
