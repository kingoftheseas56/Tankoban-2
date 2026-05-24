// src/core/manga/mangafire/MangaFireCatalogClient.h
//
// On-demand single-series fetch against mangafire.to.
//
// Context (2026-05-23, post-Phase-F):
//   - Phase A wiped data/mangafire_catalog/ and the Fandom scraper.
//   - Phase F removed the catalog browser; Comics is now search-first.
//   - Consequence: WeebCentral-search-opened series have no catalog data on disk
//     and dispatchCatalogResolve silently no-ops, leaving volume rows empty.
//
// This client closes that gap: when dispatchCatalogResolve finds no local match,
// it fires fetchByTitle() here, which scrapes MangaFire for one series and
// writes the canonical JSON at data/mangafire_catalog/<slug>.json. On success
// the index refreshes and dispatchCatalogResolve picks it up — every subsequent
// open of the same series is instant.
//
// HTTP pipeline mirrors scripts/mangafire_scraper/mangafire_ingest.py:
//   1. GET /filter?keyword=<title>       -> first /manga/<slug>.<hash> link
//   2. GET /ajax/manga/<hash>/volume/en  -> volume number + cover URL per row
//   3. GET /ajax/manga/<hash>/chapter/en -> vol -> chapter range mapping
//
// Output JSON shape matches the Python scraper bit-for-bit so the existing
// LocalMangaCatalogLoader deserializer consumes it unchanged.
//
// Threading: pure QNetworkAccessManager + QObject::connect lambdas. All work
// runs on the main thread; the pipeline is asynchronous via QNetworkReply::finished.
// Each fetch carries its own PendingFetch state through the lambdas via shared_ptr.

#pragma once

#include "core/manga/MangaCatalogTypes.h"

#include <QObject>
#include <QString>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace tankoban::manga::mangafire {

class MangaFireCatalogClient : public QObject
{
    Q_OBJECT
public:
    explicit MangaFireCatalogClient(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~MangaFireCatalogClient() override;

    // Fire the full pipeline for a single series.
    // Emits catalogReady(catalog, writtenPath) on success exactly once,
    // or catalogFailed(title, reason) on any step failure.
    // Concurrent calls are allowed; each carries its own internal state.
    void fetchByTitle(const QString& title);

    // COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1). Atomic JSON-patch
    // helper: reads data/mangafire_catalog/<seriesId>.json, mutates ONLY the
    // "weebCentral" key, writes back via QSaveFile. Does NOT re-serialize
    // from MangaCatalog; that would risk dropping MangaFire-only fields not
    // mirrored to the C++ struct.
    static QString patchWeebCentralBlock(
        const QString& seriesId,
        const tankoban::manga::WeebCentralCacheBlock& block);

signals:
    void catalogReady(const tankoban::manga::MangaCatalog& catalog,
                      const QString& writtenPath);
    void catalogFailed(const QString& title, const QString& reason);

private:
    struct PendingFetch;
    using PendingFetchPtr = std::shared_ptr<PendingFetch>;

    void stepFilter(PendingFetchPtr pending);
    void stepSitemapIndex(PendingFetchPtr pending, const QString& fallbackReason);
    void stepSitemapList(PendingFetchPtr pending,
                         const QStringList& listUrls,
                         int index);
    void stepVolume(PendingFetchPtr pending);
    void stepChapter(PendingFetchPtr pending);
    void finish(PendingFetchPtr pending);

    void emitFailure(PendingFetchPtr pending, const QString& reason);

    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace tankoban::manga::mangafire
