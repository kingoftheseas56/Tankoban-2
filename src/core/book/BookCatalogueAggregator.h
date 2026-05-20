#pragma once

#include <QObject>
#include <QList>
#include <QString>

#include "BookCatalogueResult.h"
#include "SeriesDetector.h"

class QNetworkAccessManager;
class OpenLibraryClient;
class GoogleBooksClient;

// Top-of-the-catalogue layer for BOOKS_STREMIO_PIVOT.
//
// Fires OpenLibraryClient + GoogleBooksClient queries in parallel, waits for
// BOTH to return (or timeout/fail), dedupes with CatalogueDeduper, runs
// SeriesDetector, and emits TWO ordered lists for the search-takeover view:
//   - seriesGroups (multi-book series tiles)
//   - standalones (movie-shape book tiles)
//
// Per-source failures are non-fatal — if Google Books fails, results from
// OpenLibrary still flow through. Both-failure cases emit aggregateFailed.
//
// Lifecycle: one Aggregator instance owned by BooksPage; new query()
// supersedes any pending in-flight query (cheaper than tracking generation IDs
// for a 2-user app — last-fire-wins is acceptable).
class BookCatalogueAggregator : public QObject
{
    Q_OBJECT

public:
    explicit BookCatalogueAggregator(QNetworkAccessManager* nam,
                                     const QString& googleBooksApiKey,
                                     QObject* parent = nullptr);

    void query(const QString& q);

    // For the "Other books by author" scroller on detail pages.
    void fetchAuthorWorks(const QString& openLibraryAuthorKey, const QString& authorName);

signals:
    // Fires once both sources have replied (or one replied + the other failed).
    // seriesGroups + standalones are partitioned via SeriesDetector.
    void aggregateReady(const QString& query,
                        const QList<SeriesDetector::SeriesGroup>& seriesGroups,
                        const QList<BookCatalogueResult>& standalones);

    void aggregateFailed(const QString& query, const QString& error);

    // Per-author works for the scroller.
    void authorWorksReady(const QString& authorKey,
                          const QList<BookCatalogueResult>& works);

private:
    void tryEmitAggregate();

    QNetworkAccessManager* m_nam;
    OpenLibraryClient*     m_openlib;
    GoogleBooksClient*     m_googlebooks;

    QString m_currentQuery;

    // Pending state for the current query — both must complete (success or
    // failure) before tryEmitAggregate() fires.
    bool m_openlibPending = false;
    bool m_googlebooksPending = false;
    QList<BookCatalogueResult> m_openlibResults;
    QList<BookCatalogueResult> m_googlebooksResults;
    bool m_openlibSucceeded = false;
    bool m_googlebooksSucceeded = false;
    QString m_lastOpenlibError;
    QString m_lastGooglebooksError;
};
