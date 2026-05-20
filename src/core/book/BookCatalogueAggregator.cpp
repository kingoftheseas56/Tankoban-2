#include "BookCatalogueAggregator.h"

#include "OpenLibraryClient.h"
#include "GoogleBooksClient.h"
#include "CatalogueDeduper.h"

BookCatalogueAggregator::BookCatalogueAggregator(QNetworkAccessManager* nam,
                                                 const QString& googleBooksApiKey,
                                                 QObject* parent)
    : QObject(parent),
      m_nam(nam),
      m_openlib(new OpenLibraryClient(nam, this)),
      m_googlebooks(new GoogleBooksClient(nam, googleBooksApiKey, this))
{
    connect(m_openlib, &OpenLibraryClient::searchResults,
            this, [this](const QList<BookCatalogueResult>& results) {
                m_openlibResults = results;
                m_openlibSucceeded = true;
                m_openlibPending = false;
                tryEmitAggregate();
            });
    connect(m_openlib, &OpenLibraryClient::searchFailed,
            this, [this](const QString& err) {
                m_lastOpenlibError = err;
                m_openlibSucceeded = false;
                m_openlibPending = false;
                tryEmitAggregate();
            });

    connect(m_googlebooks, &GoogleBooksClient::searchResults,
            this, [this](const QList<BookCatalogueResult>& results) {
                m_googlebooksResults = results;
                m_googlebooksSucceeded = true;
                m_googlebooksPending = false;
                tryEmitAggregate();
            });
    connect(m_googlebooks, &GoogleBooksClient::searchFailed,
            this, [this](const QString& err) {
                m_lastGooglebooksError = err;
                m_googlebooksSucceeded = false;
                m_googlebooksPending = false;
                tryEmitAggregate();
            });

    connect(m_openlib, &OpenLibraryClient::authorWorksResults,
            this, [this](const QString& authorKey,
                         const QList<BookCatalogueResult>& results) {
                emit authorWorksReady(authorKey, results);
            });
}

void BookCatalogueAggregator::query(const QString& q)
{
    m_currentQuery = q;
    m_openlibResults.clear();
    m_googlebooksResults.clear();
    m_openlibSucceeded = false;
    m_googlebooksSucceeded = false;
    m_openlibPending = true;
    m_googlebooksPending = true;

    m_openlib->search(q);
    m_googlebooks->search(q);
}

void BookCatalogueAggregator::fetchAuthorWorks(const QString& authorKey,
                                               const QString& authorName)
{
    m_openlib->fetchAuthorWorks(authorKey, authorName);
}

void BookCatalogueAggregator::tryEmitAggregate()
{
    if (m_openlibPending || m_googlebooksPending) return;

    if (!m_openlibSucceeded && !m_googlebooksSucceeded) {
        emit aggregateFailed(m_currentQuery,
            QStringLiteral("OpenLibrary: %1; GoogleBooks: %2")
                .arg(m_lastOpenlibError, m_lastGooglebooksError));
        return;
    }

    auto merged = CatalogueDeduper::merge(m_openlibResults, m_googlebooksResults);
    auto detection = SeriesDetector::detect(merged);

    // ─────────────────────────────────────────────────────────────────────
    // seriesId assignment — fix for P3.1 code-quality reviewer Important #1
    // ─────────────────────────────────────────────────────────────────────
    // SeriesDetector stamps isSeries / seriesName / seriesPosition / seriesTotal
    // on grouped books, but does NOT populate seriesId. The library store's
    // m_bySeries index uses seriesId as the foreign key, so leaving it empty
    // would break series-tile aggregation in Phase 9 BooksPage.
    //
    // Canonical seriesId is derived from the group's first book's catalogueId
    // with a ":series" suffix (matches the pattern that
    // BooksTankoLibrarySearchWidget::addSeriesCard will use in Phase 5).
    for (auto& group : detection.seriesGroups) {
        if (group.books.isEmpty()) continue;
        const QString seriesId = group.books.first().catalogueId
                               + QStringLiteral(":series");
        for (auto& book : group.books) {
            book.seriesId = seriesId;
        }
    }

    emit aggregateReady(m_currentQuery, detection.seriesGroups, detection.standalones);
}
