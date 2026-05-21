#include "TankorentBookScraper.h"

#include "core/TankorentSearchService.h"
#include "core/TorrentResult.h"

#include <QDebug>
#include <QHash>
#include <QRegularExpression>

namespace {

// Inferred book format from the torrent title. Returns the matched extension
// lowercase (e.g. "epub") or empty if none of the recognized formats appears.
// Order is preference: epub > pdf > mobi when multiple appear in the title.
QString inferFormatFromTitle(const QString& title)
{
    static const QStringList kFormats = {
        QStringLiteral("epub"),
        QStringLiteral("pdf"),
        QStringLiteral("mobi"),
        QStringLiteral("azw3"),
        QStringLiteral("azw"),
        QStringLiteral("djvu"),
        QStringLiteral("cbz"),
        QStringLiteral("cbr"),
    };
    const QString lower = title.toLower();
    for (const QString& ext : kFormats) {
        if (lower.contains(QStringLiteral(".") + ext)) return ext;
        // Some scene/scanlation tags use "[EPUB]" or "(EPUB)" instead of a
        // dotted extension — pick those up too.
        if (lower.contains(QStringLiteral("[") + ext + QStringLiteral("]"))) return ext;
        if (lower.contains(QStringLiteral("(") + ext + QStringLiteral(")"))) return ext;
    }
    return {};
}

// Map a TorrentResult into a BookResult row. BookResult fields that don't
// have a torrent analog (md5, isbn, pages, publisher, description, language,
// coverUrl, author) stay empty — the picker UI compensates via the catalogue
// record's pre-known author/year/isbn (the Tankorent row's role is "did
// someone publish a torrent of this book; if so, here's the magnet").
//
// fileSize composition: "<human-size> · <seeders> seeders" mirrors Hemanth's
// 2026-05-20 mockup language. The seeders count is the load-bearing quality
// signal for torrent rows — without it the user can't dodge dead torrents.
BookResult torrentToBook(const TorrentResult& t)
{
    BookResult r;
    r.source     = QStringLiteral("tankorent");
    // Row identity: prefer infoHash, fall back to magnetUri. infoHash is the
    // canonical cross-indexer dedup key when available; magnet alone works
    // for callers that just want to start a download.
    r.sourceId   = t.infoHash.isEmpty() ? t.magnetUri : t.infoHash;
    r.md5        = {};                              // not a torrent concept
    r.title      = t.title;
    r.author     = {};                              // catalogue record carries it
    r.publisher  = {};
    r.year       = t.publishDate.isValid() ? QString::number(t.publishDate.date().year()) : QString{};
    r.format     = inferFormatFromTitle(t.title);
    r.fileSize   = QStringLiteral("%1 · %2 seeders")
                      .arg(humanSize(t.sizeBytes))
                      .arg(t.seeders);
    r.detailUrl  = t.detailsUrl;
    r.downloadUrl = t.magnetUri;                    // BookDownloader::startMagnetDownload reads this
    return r;
}

} // namespace

TankorentBookScraper::TankorentBookScraper(TankorentSearchService* service,
                                           QObject* parent)
    : BookScraper(nullptr, parent)
    , m_service(service)
{
    if (m_service) {
        connect(m_service, &TankorentSearchService::resultsReady,
                this, &TankorentBookScraper::onServiceResultsReady);
        connect(m_service, &TankorentSearchService::indexerError,
                this, &TankorentBookScraper::onServiceIndexerError);
        connect(m_service, &TankorentSearchService::searchFinished,
                this, &TankorentBookScraper::onServiceSearchFinished);
    }
}

void TankorentBookScraper::search(const QString& query, int limit)
{
    if (!m_service) {
        qWarning() << "[TankorentBookScraper] search() called but TankorentSearchService"
                   << "not wired (nullptr ctor injection). query=" << query;
        emit searchFinished({});
        return;
    }

    // Single-flight: cancel any in-flight handle before starting a new one.
    // Picker UI won't normally re-fire mid-search, but the scraper stays honest
    // about which handle owns the slot.
    if (!m_currentHandle.isEmpty()) {
        m_service->cancelSearch(m_currentHandle);
    }

    m_accumulator.clear();
    m_magnetByRowId.clear();
    m_currentLimit = limit;
    m_currentHandle = m_service->startSearch(QStringLiteral("books"),
                                             QStringLiteral("all"),
                                             query, limit);

    if (m_currentHandle.isEmpty()) {
        // No indexers matched the "books" allowlist or all were disabled in
        // QSettings — emit empty result immediately.
        qInfo() << "[TankorentBookScraper] no eligible indexers for query=" << query;
        emit searchFinished({});
        return;
    }
}

void TankorentBookScraper::fetchDetail(const QString& torrentId)
{
    // Tankorent rows carry enough detail in the search result (title, size,
    // seeders, magnet, year) that a separate fetchDetail round-trip isn't
    // required for the picker UI. Emit the cached row back so callers that
    // do invoke fetchDetail still receive a deterministic detailReady.
    BookResult cached;
    cached.source = QStringLiteral("tankorent");
    cached.sourceId = torrentId;
    // If we have a magnet cached for this row, populate downloadUrl so the
    // caller can pipe directly into BookDownloader::startMagnetDownload.
    if (m_magnetByRowId.contains(torrentId)) {
        cached.downloadUrl = m_magnetByRowId.value(torrentId).magnetUri;
        cached.detailUrl   = m_magnetByRowId.value(torrentId).detailsUrl;
    }
    emit detailReady(cached);
}

void TankorentBookScraper::resolveDownload(const QString& torrentId)
{
    // For Tankorent rows the magnet URI IS the resolution — it's already
    // baked into the BookResult.downloadUrl at search-mapping time. The
    // BookDownloader picks it up via startMagnetDownload(magnetUri, ...).
    if (!m_magnetByRowId.contains(torrentId)) {
        emit downloadFailed(torrentId,
            QStringLiteral("No cached magnet for row %1 (search may have rotated)").arg(torrentId));
        return;
    }
    const QString magnetUri = m_magnetByRowId.value(torrentId).magnetUri;
    emit downloadResolved(torrentId, QStringList{ magnetUri });
}

// ── Service slots ───────────────────────────────────────────────────────────

void TankorentBookScraper::onServiceResultsReady(const QString& handle,
                                                  const QList<TorrentResult>& results)
{
    if (handle != m_currentHandle) return;  // not our search
    for (const TorrentResult& t : results) {
        const BookResult row = torrentToBook(t);
        m_accumulator.append(row);
        CachedRow cr;
        cr.magnetUri  = t.magnetUri;
        cr.detailsUrl = t.detailsUrl;
        m_magnetByRowId.insert(row.sourceId, cr);
    }
}

void TankorentBookScraper::onServiceIndexerError(const QString& handle,
                                                  const QString& indexerId,
                                                  const QString& error)
{
    if (handle != m_currentHandle) return;
    // Partial-failure on a single indexer is non-fatal — the service still emits
    // searchFinished once all indexers settle. Log + continue; UI surfaces the
    // partial result set when searchFinished arrives.
    qInfo() << "[TankorentBookScraper] indexer" << indexerId << "errored:" << error
            << "(handle=" << handle << ", partial-success path)";
}

void TankorentBookScraper::onServiceSearchFinished(const QString& handle)
{
    if (handle != m_currentHandle) return;
    QList<BookResult> finalResults;
    finalResults.swap(m_accumulator);
    m_currentHandle.clear();
    emit searchFinished(finalResults);
}
