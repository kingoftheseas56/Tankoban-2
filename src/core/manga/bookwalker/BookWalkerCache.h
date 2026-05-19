#pragma once

#include "BookWalkerCacheTypes.h"

#include <QString>
#include <optional>

namespace tankoban::manga::bookwalker {

class BookWalkerCache
{
public:
    static constexpr qint64 kDefaultTtlSeconds = 7 * 24 * 60 * 60;

    // Storage path: <AppDataLocation>/cache/bookwalker_covers/<anilistId>.json
    static QString cacheFilePath(int anilistId);

    // Load + validate. Returns nullopt if:
    //  - file missing / unreadable / malformed
    //  - now - fetchedAt > ttlSeconds
    //  - record.canonicalCount != currentCanonicalCount (drift)
    // Pass currentCanonicalCount == 0 to skip the drift check (degraded MangaUpdates path).
    static std::optional<BookWalkerCacheRecord> load(int anilistId,
                                                     int currentCanonicalCount,
                                                     qint64 ttlSeconds = kDefaultTtlSeconds);

    // Atomic write (QSaveFile). Returns true on success.
    static bool store(int anilistId, const BookWalkerCacheRecord& record);

    // --- seriesKey-keyed API (WEEBCENTRAL_IDENTITY_PIVOT Tasks 6+7) ---
    // seriesKey is the WeebCentral sourceId:seriesId composite, e.g.
    // "weebcentral:01J76XYAVE3FZ3YMHMTKEZGXM4". Colons are sanitized to
    // underscores for the filename (Windows compat).

    // Compute the filename-safe path for a seriesKey.
    static QString cacheFilePathByKey(const QString& seriesKey);

    // Load + validate. TTL + drift checks identical to the anilistId variant.
    // Pass currentCanonicalCount == 0 to skip the drift check (degraded path).
    static std::optional<BookWalkerCacheRecord> loadByKey(const QString& seriesKey,
                                                          int currentCanonicalCount,
                                                          qint64 ttlSeconds = kDefaultTtlSeconds);

    // Atomic write keyed by seriesKey.
    static bool storeByKey(const QString& seriesKey, const BookWalkerCacheRecord& record);
};

} // namespace tankoban::manga::bookwalker
