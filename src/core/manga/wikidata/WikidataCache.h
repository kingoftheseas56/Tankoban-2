// src/core/manga/wikidata/WikidataCache.h
//
// 30-day disk cache for Wikidata Q-ID → FandomReference resolutions. The
// SPARQL query layer (WikidataClient) consults this cache before hitting
// the network; a hit short-circuits the async resolveQid path.
//
// Cache shape (single JSON file at <AppDataLocation>/cache/wikidata_fandom_refs.json):
//   {
//     "version": 1,
//     "entries": {
//       "Q14559":  { "subdomain": "deathnote", "fetchedAt": "2026-05-20T..." },
//       "Q633292": { "subdomain": "berserk",   "fetchedAt": "..." }
//     }
//   }
//
// One-file-many-entries vs FandomCatalogCache's one-file-per-entry choice:
// WikidataReferences are tiny (~50 bytes each); a 5K-series corpus fits in
// a single ~250 KB file with negligible read cost. FandomCatalog records
// are ~100× larger and benefit from per-file isolation.
//
// Thread-safety contract: static methods, called from the main thread
// (same contract as BookWalkerCache + FandomCatalogCache).
//
// Plan: docs/superpowers/plans/2026-05-19-fandom-catalog-redesign.md Task 13

#pragma once

#include "core/manga/fandom/FandomTypes.h"

#include <QString>
#include <optional>

namespace tankoban::manga::wikidata {

class WikidataCache
{
public:
    static constexpr qint64 kDefaultTtlSeconds = 30 * 24 * 60 * 60; // 30 days

    // Resolved path to the single cache file.
    static QString cacheFilePath();

    // Load + validate. Returns nullopt when:
    //   - file missing / unreadable / malformed
    //   - entry for `qid` missing
    //   - now - entry.fetchedAt > ttlSeconds
    static std::optional<tankoban::manga::fandom::FandomReference>
        loadByQid(const QString& qid,
                  qint64 ttlSeconds = kDefaultTtlSeconds);

    // Upsert `qid → ref` in the single-file map. Atomic write (QSaveFile).
    // Stamps fetchedAt to current UTC if not already set on the in-memory
    // record being persisted alongside ref (we don't mutate `ref` itself).
    static bool storeByQid(const QString& qid,
                           const tankoban::manga::fandom::FandomReference& ref);
};

} // namespace tankoban::manga::wikidata
