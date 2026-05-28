#pragma once

#include "core/stream/AnimeCatalogResolver.h"

#include <QByteArray>
#include <QMutex>
#include <QString>

#include <optional>

namespace tankostream::stream {

// File-backed cache of the Fribb IMDb->Kitsu map at:
//   <cacheDir>/anime-id-map.json   (raw Fribb anime-list JSON array)
//
// Pure file IO + an in-memory AnimeIdMap. The NETWORK refresh is intentionally
// NOT here -- the caller (MetaAggregator wiring) fetches the Fribb URL with the
// existing network stack and hands the bytes to saveJson(). Keeps this unit
// QtNetwork-free and trivially testable. Thread-safe via m_mutex.
class AnimeIdMapCache {
public:
    explicit AnimeIdMapCache(const QString& cacheDir);

    std::optional<int> kitsuIdForImdb(const QString& imdbId) const;

    // true if the cache file is missing OR older than maxAgeMs -- the caller
    // uses this to decide whether to fire a background refresh.
    bool isStale(qint64 maxAgeMs) const;

    // Persists raw Fribb JSON to disk and reloads the in-memory map.
    void saveJson(const QByteArray& json);

    int size() const;

private:
    QString filePath() const;
    void loadFromDisk();  // assumes caller holds m_mutex

    const QString m_cacheDir;
    mutable QMutex m_mutex;
    AnimeIdMap m_map;
};

}  // namespace tankostream::stream
