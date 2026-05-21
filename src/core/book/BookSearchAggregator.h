#pragma once

#include <QObject>
#include <QList>
#include <QSet>
#include <QString>

#include "BookResult.h"
#include "BookCatalogueResult.h"

class BookScraper;

// Picker-side aggregator. Given a `BookCatalogueResult` (the user's pick from
// the catalogue layer), fans out parallel queries to every registered
// `BookScraper`. Each source's results stream into the picker independently;
// the picker UI renders one vertical section per source with its own spinner
// that stops as that source returns (or fails).
//
// Source list is provided at construction — `BookSearchAggregator` is
// source-agnostic. v1 callers pass [LibGenScraper, TankorentBookScraper];
// when AA flips back on at v1.1, callers pass three.
//
// Query strategy: ISBN-first (when present on the catalogue record); fall back
// to "title author" concatenation. Per-source scrapers handle their own
// escaping / encoding.
class BookSearchAggregator : public QObject
{
    Q_OBJECT
public:
    explicit BookSearchAggregator(const QList<BookScraper*>& scrapers,
                                  QObject* parent = nullptr);

    // Fire queries against every registered scraper. Each scraper's results
    // flow back as `sourceResultsReady` or `sourceFailed`.
    void searchFor(const BookCatalogueResult& target);

signals:
    // Per-source results — picker handles streaming UI per section.
    void sourceResultsReady(const QString& sourceId,
                            const QList<BookResult>& results);
    void sourceFailed(const QString& sourceId, const QString& error);

    // Fires once every source has completed (success or fail). Picker uses
    // this to render an empty-state banner if no section has any rows.
    void allSourcesCompleted();

private:
    void onScraperSearchFinished(const QString& sourceId,
                                 const QList<BookResult>& results);
    void onScraperError(const QString& sourceId, const QString& error);
    void checkAllCompleted();

    QList<BookScraper*> m_scrapers;
    QSet<QString>       m_pending;  // sourceIds with in-flight queries
};
