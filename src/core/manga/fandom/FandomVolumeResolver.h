// src/core/manga/fandom/FandomVolumeResolver.h
//
// Network-orchestration layer for the Fandom catalog redesign. Composes
// WikidataClient + WikiManifestRegistry + FandomClient + extractors +
// FandomCatalogCache into a single resolveForSeries() entry point that
// emits resolved(seriesId, catalog) or unresolved(seriesId, reason).
//
// Resolution flow (happy path, manifest-present):
//   1. Cache lookup by manifest.wikidataQid → hit short-circuits.
//   2. If manifest carries subdomain + page-path → go straight to fetch.
//   3. Otherwise resolve subdomain via WikidataClient first (cache-aware
//      under the hood from Task 13).
//   4. FandomClient::fetchPage(subdomain, pagePath).
//   5. Route to TableExtractor (Table-style monoliths) by manifest hint.
//   6. Store resulting FandomCatalog in the 7d disk cache.
//   7. Emit resolved.
//
// Scope deferrals (deliberate, documented in unresolved reason strings):
//   - Infobox / hierarchy multi-page crawls: out of Task 14 v1 scope —
//     they require iterating /wiki/Vol.1 .. /wiki/Vol.N and aggregating
//     per-volume FandomVolume records into a catalog. Plan Task 19
//     (ComicsPage wiring) is the consumer that'll exercise this; the
//     iteration loop lands then.
//   - Title-only / Q-ID-less resolution: also out of v1 scope. Caller
//     must supply either a registry-resident manifest or a Q-ID hint.
//
// Plan: docs/superpowers/plans/2026-05-19-fandom-catalog-redesign.md Task 14

#pragma once

#include "FandomTypes.h"
#include "WikiManifest.h"

#include <QHash>
#include <QObject>
#include <QString>

namespace tankoban::manga {
namespace fandom { class FandomClient; class WikiManifestRegistry; struct ParsedPage; }
namespace wikidata { class WikidataClient; }
}

namespace tankoban::manga::fandom {

class FandomVolumeResolver : public QObject
{
    Q_OBJECT
public:
    FandomVolumeResolver(wikidata::WikidataClient* wd,
                         FandomClient* fc,
                         WikiManifestRegistry* registry,
                         QObject* parent = nullptr);

    // Drive the chain. wikidataQidHint is consulted when the manifest
    // registry has no entry for seriesId (and the manifest's own Q-ID is
    // therefore unavailable). englishTitleHint is reserved for the future
    // heuristic-discovery path and currently unused.
    void resolveForSeries(const QString& seriesId,
                          const QString& wikidataQidHint = {},
                          const QString& englishTitleHint = {});

signals:
    void resolved(const QString& seriesId,
                  const tankoban::manga::fandom::FandomCatalog& catalog);
    void unresolved(const QString& seriesId, const QString& reason);

private slots:
    void onWikidataResolved(int requestId,
                            const tankoban::manga::fandom::FandomReference& ref);
    void onWikidataFailed(int requestId, const QString& reason);
    void onPageFetched(int requestId,
                       const tankoban::manga::fandom::ParsedPage& page);
    void onPageFetchFailed(int requestId, const QString& reason);

private:
    struct Pending {
        QString      seriesId;
        WikiManifest manifest;     // captured at resolveForSeries time
        QString      qid;          // resolved or hinted; used for cache key
        QString      subdomain;    // populated after Wikidata step (or from manifest)
    };

    int issueRequestId() { return ++m_nextRequestId; }
    void fetchPageFor(int requestId, Pending pending);
    void completeWithCatalog(const Pending& p, QList<FandomVolume> volumes);

    wikidata::WikidataClient* m_wd       = nullptr; // non-owning
    FandomClient*             m_fc       = nullptr; // non-owning
    WikiManifestRegistry*     m_registry = nullptr; // non-owning

    QHash<int, Pending> m_pending;
    int                 m_nextRequestId = 0;
};

} // namespace tankoban::manga::fandom
