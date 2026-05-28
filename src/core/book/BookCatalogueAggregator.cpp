#include "BookCatalogueAggregator.h"

#include <algorithm>

#include <QHash>

#include "FictionDbClient.h"

namespace {
// catalogueId is "fictiondb:<slug>"; recover the slug for fetchBook().
QString slugOf(const QString& catalogueId)
{
    const QString prefix = QStringLiteral("fictiondb:");
    return catalogueId.startsWith(prefix) ? catalogueId.mid(prefix.size()) : QString();
}
}  // namespace

BookCatalogueAggregator::BookCatalogueAggregator(FictionDbClient* fictiondb, QObject* parent)
    : QObject(parent), m_fictiondb(fictiondb)
{
    if (m_fictiondb) {
        connect(m_fictiondb, &FictionDbClient::searchResults,
                this, &BookCatalogueAggregator::onFictionResults);
        connect(m_fictiondb, &FictionDbClient::searchFailed,
                this, &BookCatalogueAggregator::onFictionFailed);
        connect(m_fictiondb, &FictionDbClient::bookReady,
                this, &BookCatalogueAggregator::onBookResolved);
        connect(m_fictiondb, &FictionDbClient::bookFailed,
                this, &BookCatalogueAggregator::onBookResolveFailed);
    }
}

void BookCatalogueAggregator::query(const QString& q)
{
    ++m_generation;
    m_currentQuery = q;
    m_books.clear();
    m_pendingBookIds.clear();
    m_resolved.clear();

    if (m_fictiondb)
        m_fictiondb->search(q);
    else
        emit aggregateReady(q, {}, {});
}

void BookCatalogueAggregator::onFictionResults(const QString& query,
                                               const QList<BookCatalogueResult>& books)
{
    if (query != m_currentQuery) return;   // superseded query

    m_books = rerankBooks(query, books);

    // Single clean paint: we do NOT emit the flat book list first (that caused a
    // jarring "random results, then reload" flicker). We resolve the top-N series
    // first, then emit once with series + folded standalones. The storefront
    // shows its "Searching…" state until then.

    // Top-N resolution for the Series track.
    m_pendingBookIds.clear();
    m_resolved.clear();
    const int n = std::min(kTopN, static_cast<int>(m_books.size()));
    for (int i = 0; i < n; ++i) {
        const QString slug = slugOf(m_books[i].catalogueId);
        if (!slug.isEmpty()) m_pendingBookIds.insert(slug);
    }
    if (m_pendingBookIds.isEmpty()) {
        emitGrouped();   // nothing to resolve → final emit (series empty)
        return;
    }
    // Snapshot the set (fetchBook replies mutate m_pendingBookIds as they land).
    const QList<QString> slugs = m_pendingBookIds.values();
    for (const QString& slug : slugs)
        m_fictiondb->fetchBook(slug);
}

void BookCatalogueAggregator::onFictionFailed(const QString& query, const QString& error)
{
    Q_UNUSED(error);
    if (query != m_currentQuery) return;
    emit aggregateReady(query, {}, {});   // surface empty; series track has nothing
}

void BookCatalogueAggregator::onBookResolved(const QString& bookId, const BookCatalogueResult& book)
{
    if (!m_pendingBookIds.contains(bookId)) return;   // not part of the current resolution
    m_pendingBookIds.remove(bookId);
    m_resolved.append(book);
    if (m_pendingBookIds.isEmpty()) emitGrouped();
}

void BookCatalogueAggregator::onBookResolveFailed(const QString& bookId, const QString& error)
{
    Q_UNUSED(error);
    if (!m_pendingBookIds.contains(bookId)) return;
    m_pendingBookIds.remove(bookId);
    if (m_pendingBookIds.isEmpty()) emitGrouped();
}

void BookCatalogueAggregator::emitGrouped()
{
    // Group resolved books by their FictionDB series slug.
    QList<SeriesDetector::SeriesGroup> groups;
    QHash<QString, int> indexBySeries;
    QSet<QString> seriesMemberIds;   // catalogueIds folded into a series tile

    for (const BookCatalogueResult& b : m_resolved) {
        if (b.seriesId.isEmpty() || b.seriesName.isEmpty()) continue;
        if (!indexBySeries.contains(b.seriesId)) {
            SeriesDetector::SeriesGroup g;
            g.seriesName = b.seriesName;
            g.author     = b.author;
            indexBySeries.insert(b.seriesId, groups.size());
            groups.append(g);
        }
        groups[indexBySeries.value(b.seriesId)].books.append(b);
        seriesMemberIds.insert(b.catalogueId);
    }

    // Books track = re-ranked results minus the ones now shown as series tiles.
    QList<BookCatalogueResult> standalones;
    standalones.reserve(m_books.size());
    for (const BookCatalogueResult& b : m_books)
        if (!seriesMemberIds.contains(b.catalogueId))
            standalones.append(b);

    emit aggregateReady(m_currentQuery, groups, standalones);
}

QList<BookCatalogueResult> BookCatalogueAggregator::rerankBooks(
        const QString& query, QList<BookCatalogueResult> books)
{
    const QString q = query.trimmed().toLower();
    auto score = [&q](const BookCatalogueResult& b) {
        const QString t = b.title.toLower();
        int s = 0;
        if (t == q)               s = 300;
        else if (t.startsWith(q)) s = 200;
        else if (t.contains(q))   s = 100;
        if (!b.author.isEmpty() && b.author.toLower().contains(q)) s += 25;
        return s;
    };
    std::stable_sort(books.begin(), books.end(),
        [&score](const BookCatalogueResult& a, const BookCatalogueResult& b) {
            return score(a) > score(b);
        });
    return books;
}
