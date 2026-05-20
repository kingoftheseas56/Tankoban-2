// src/core/manga/fandom/FandomCatalogCache.h
//
// 7-day disk cache for FandomCatalog records, keyed by Wikidata Q-ID.
// JSON files at <AppDataLocation>/cache/fandom_catalogs/<qid>.json.
//
// Plan: docs/superpowers/plans/2026-05-19-fandom-catalog-redesign.md Task 12
//
// JSON serialization for FandomCatalog + FandomVolume + FandomReference lives
// in this translation unit (per the comment in FandomTypes.cpp — keeps the
// value types' header dependency-free of QJsonDocument).

#pragma once

#include "FandomTypes.h"

#include <QJsonObject>
#include <QString>
#include <optional>

namespace tankoban::manga::fandom {

class FandomCatalogCache
{
public:
    static constexpr qint64 kDefaultTtlSeconds = 7 * 24 * 60 * 60; // 7 days

    // Storage path: <AppDataLocation>/cache/fandom_catalogs/<qid>.json
    // Q-IDs are filename-safe by construction (Q + digits) — no sanitization.
    static QString cacheFilePath(const QString& qid);

    // Load + validate. Returns nullopt when:
    //   - file missing / unreadable / malformed JSON
    //   - now - fetchedAt > ttlSeconds
    //   - schemaVersion != kFandomCatalogSchemaVersion (treat as miss; UI
    //     will refetch and overwrite with current shape)
    static std::optional<FandomCatalog> loadByQid(
        const QString& qid,
        qint64 ttlSeconds = kDefaultTtlSeconds);

    // Atomic write (QSaveFile). Returns true on success. Creates parent
    // directories if missing.
    static bool storeByQid(const QString& qid, const FandomCatalog& catalog);

    // Fandom catalog redesign Task 19 (Phase 7, 2026-05-20). Delete the
    // cache entry for the given Q-ID. No-op if the file is absent. Returns
    // true on success (delete + absent both count as success). Used by
    // FallbackChainResolver::forceRefreshSeries to invalidate before the
    // re-resolve fan-out — guarantees the next resolveForSeries hits the
    // network instead of returning the stale cached catalog.
    static bool invalidateByQid(const QString& qid);

    // JSON serialization helpers — exposed for testing + future consumers.
    static QJsonObject toJson(const FandomCatalog& c);
    static FandomCatalog fromJson(const QJsonObject& obj);
};

} // namespace tankoban::manga::fandom
