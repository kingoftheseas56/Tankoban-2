#include "BookSearchAggregator.h"

#include "BookScraper.h"

namespace {
constexpr int kPerSourceLimit = 10;
}

BookSearchAggregator::BookSearchAggregator(const QList<BookScraper*>& scrapers,
                                           QObject* parent)
    : QObject(parent)
    , m_scrapers(scrapers)
{
    for (BookScraper* s : m_scrapers) {
        connect(s, &BookScraper::searchFinished,
                this, [this, s](const QList<BookResult>& results) {
                    onScraperSearchFinished(s->sourceId(), results);
                });
        connect(s, &BookScraper::errorOccurred,
                this, [this, s](const QString& err) {
                    onScraperError(s->sourceId(), err);
                });
    }
}

void BookSearchAggregator::searchFor(const BookCatalogueResult& target)
{
    m_pending.clear();

    QString query;
    if (!target.isbn.isEmpty()) {
        // Multi-ISBN is comma-joined in BookCatalogueResult; pick the first.
        query = target.isbn.section(QLatin1Char(','), 0, 0).trimmed();
    } else {
        query = (target.title + QLatin1Char(' ') + target.author).trimmed();
    }

    for (BookScraper* s : m_scrapers) {
        m_pending.insert(s->sourceId());
        s->search(query, kPerSourceLimit);
    }
}

void BookSearchAggregator::onScraperSearchFinished(const QString& sourceId,
                                                   const QList<BookResult>& results)
{
    m_pending.remove(sourceId);
    emit sourceResultsReady(sourceId, results);
    checkAllCompleted();
}

void BookSearchAggregator::onScraperError(const QString& sourceId, const QString& error)
{
    m_pending.remove(sourceId);
    emit sourceFailed(sourceId, error);
    checkAllCompleted();
}

void BookSearchAggregator::checkAllCompleted()
{
    if (m_pending.isEmpty()) emit allSourcesCompleted();
}
