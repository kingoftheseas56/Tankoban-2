#pragma once

// STREAM_DOWNLOADED_LIBRARY 2026-05-10 — persistent index of bulk-downloaded
// episodes. Spec: docs/superpowers/specs/2026-05-10-stream-downloaded-library-design.md
//
// Owns three in-memory lookup maps derived from a single sibling JSON file
// (<dataDir>/stream_downloads.json) keyed by canonicalKey (lowercased
// native-form absolute path). Threadsafe — VideosScanner reads from a worker
// thread via mutex-guarded const APIs.

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <optional>

class JsonStore;

class StreamDownloadIndex : public QObject
{
    Q_OBJECT

public:
    struct Entry {
        QString imdbId;        // "tt6741278"
        QString type;          // "series" or "movie" (movies use season=0, episode=0)
        int     season = 0;
        int     episode = 0;
        QString canonicalPath; // display-form absolute path
        qint64  addedAt = 0;
        QString sourceGroupId; // empty for migration-rescued; non-empty for bulk-completion
        qint64  fileSizeBytes = 0;
    };

    explicit StreamDownloadIndex(JsonStore* store, QObject* parent = nullptr);

    // ── Thread safety contract ──────────────────────────────────────────────
    // All mutating methods (registerEpisode/registerMovie/evictByImdb/evictByPath/validateAll)
    // execute synchronously on the calling thread. They acquire m_mutex around
    // the in-memory map mutations, then call save() and emit entriesChanged()
    // OFF the lock. JsonStore::write is internally thread-safe (its writer
    // loop accepts cross-thread requests). DebugLogBuffer::instance() is
    // thread-safe (its own QMutex). entriesChanged() will deliver to slots
    // per their connection type — subscribers MUST use Qt::QueuedConnection
    // if they want delivery on the GUI thread regardless of which thread
    // called the mutating method.
    //
    // validateAll is the only method that may legitimately run on a worker
    // thread (StreamLibraryHomeView dispatches it via QtConcurrent::run on
    // home-open per spec §10.4). registerEpisode and the eviction methods
    // are conventionally called on the GUI thread.
    //
    // All read methods (isStreamOwned/filePathFor/hasAnyForImdb/entriesForImdb/
    // all) are const and acquire m_mutex via QMutexLocker; safe from any
    // thread including a VideosScanner worker.
    void registerEpisode(const QString& imdbId, int season, int episode,
                         const QString& canonicalPath, const QString& sourceGroupId,
                         qint64 fileSizeBytes);
    void registerMovie(const QString& imdbId, const QString& canonicalPath,
                       const QString& sourceGroupId, qint64 fileSizeBytes);
    void evictByImdb(const QString& imdbId);
    void evictByPath(const QString& canonicalKey);
    void validateAll();

    // Read API — mutex-guarded. Safe from any thread.
    bool isStreamOwned(const QString& canonicalKey) const;
    std::optional<QString> filePathFor(const QString& imdbId, int season, int episode) const;
    std::optional<QString> filePathForMovie(const QString& imdbId) const;
    bool hasAnyForImdb(const QString& imdbId) const;
    QList<Entry> entriesForImdb(const QString& imdbId) const;
    QList<Entry> all() const;

    // Static helper: convert any path to the canonical lookup key.
    // Public so callers building lookups in a hot loop can compute once.
    static QString computeCanonicalKey(const QString& anyPath);

    // Static helper: encode (imdbId, season, episode) as the by-episode map key.
    static QString computeEpisodeKey(const QString& imdbId, int season, int episode);

signals:
    void entriesChanged();

private:
    void load();
    void save();

    // Recompute m_imdbHasAny for a single imdbId after an entry was evicted.
    // MUST be called with m_mutex already held by the caller.
    void recomputeImdbHasAnyLocked(const QString& imdbId);

    JsonStore* m_store;
    mutable QMutex m_mutex;

    // Three derived maps; all updated atomically under m_mutex.
    QHash<QString, Entry>   m_byPath;       // canonicalKey -> Entry
    QHash<QString, QString> m_byEpisode;    // "imdb:NN:NN" -> canonicalKey
    QSet<QString>           m_imdbHasAny;   // imdb if at least one entry exists

    static constexpr const char* FILENAME = "stream_downloads.json";
    static constexpr int kSchemaVersion = 1;
};
