#include "TankorentBookScraper.h"

#include <QDebug>

// Note: TankorentSearchService.h is intentionally NOT included here today.
// Agent 4 ships the service as a Phase 5 follow-on commit (~one wake from
// 2026-05-21). When that lands, this file gains:
//   #include "core/torrent/TankorentSearchService.h"
// plus the search()/fetchDetail()/resolveDownload() bodies wire into
// m_service->startSearch(...) and friends, plus signal-forwarded result
// mapping (TorrentResult -> BookResult: format inferred from filename, size
// from torrent payload, seeders/leechers stashed into fileSize as
// "X seeders / Y MB" until BookResult grows dedicated fields).
//
// Skeleton-only stubs below preserve the BookScraper contract: every entry
// point emits a deterministic empty/failed signal so callers don't dangle.

TankorentBookScraper::TankorentBookScraper(TankorentSearchService* service,
                                           QObject* parent)
    : BookScraper(nullptr, parent)
    , m_service(service)
{
}

void TankorentBookScraper::search(const QString& query, int /*limit*/)
{
    if (!m_service) {
        qWarning() << "[TankorentBookScraper] search() called but TankorentSearchService"
                   << "not yet wired (Agent 4 Phase 5 follow-on pending). query=" << query;
        emit searchFinished({});
        return;
    }
    // TODO(Task 4.4 implementer): bridge to m_service->startSearch("books",
    // "all", query, limit). Connect to resultsReady/searchFinished/indexerError
    // and map TorrentResult -> BookResult; emit searchFinished on completion.
    emit searchFinished({});
}

void TankorentBookScraper::fetchDetail(const QString& /*torrentId*/)
{
    // Tankorent rows carry enough detail in the search result that a fetchDetail
    // round-trip isn't required for the picker UI. If a future caller asks for
    // detail (e.g., a Tankorent-specific deep-detail view), this can be wired
    // through `TankorentSearchService` or a sibling detail surface. For now,
    // emit detailReady with the cached search-row data via the BookScraper
    // contract — current callers don't invoke this path for Tankorent rows.
    emit detailReady({});
}

void TankorentBookScraper::resolveDownload(const QString& torrentId)
{
    if (!m_service) {
        emit downloadFailed(torrentId,
                            QStringLiteral("Tankorent search service not yet wired"));
        return;
    }
    // TODO(Task 4.4 implementer): resolve torrentId to a magnet URI through
    // m_service (or via a dedicated magnet-lookup path Agent 4 exposes), then
    // emit downloadResolved(torrentId, { magnetUri }) for BookDownloader to
    // pick up via startMagnetDownload(...).
    emit downloadFailed(torrentId,
                        QStringLiteral("Tankorent resolveDownload not yet implemented"));
}
