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
};

} // namespace tankoban::manga::bookwalker
