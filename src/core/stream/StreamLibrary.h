#pragma once

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QString>

class JsonStore;
class StreamDownloadIndex;
class TorrentClient;

struct StreamLibraryEntry {
    QString imdb;           // "tt1234567"
    QString type;           // "movie" or "series"
    QString name;
    QString year;
    QString poster;         // URL
    QString description;
    QString imdbRating;
    qint64  addedAt = 0;    // ms since epoch
};

class StreamLibrary : public QObject
{
    Q_OBJECT

public:
    explicit StreamLibrary(JsonStore* store, QObject* parent = nullptr);

    void add(const StreamLibraryEntry& entry);
    bool remove(const QString& imdbId);
    bool has(const QString& imdbId) const;
    StreamLibraryEntry get(const QString& imdbId) const;
    QList<StreamLibraryEntry> getAll() const;

    // THEATRE_CLEANUP F2 (2026-05-22) — Hemanth-driven "Clear Library" flow.
    // Wipes every entry + cascades the same destructive cleanup remove()
    // does for a single entry: evicts every download-index row (per
    // m_downloadIndex), cancels every active stream-bulk cohort with
    // deleteFiles=true (per m_torrentClient → drops downloaded bytes from
    // disk), persists the empty state, and emits libraryChanged exactly
    // once. Returns the count of entries cleared (useful for UI toasts /
    // confirmation echo). Caller (UI layer) MUST gate this behind explicit
    // user confirmation — engine-level there is no second guard.
    int clear();

    // STREAM_DOWNLOADED_LIBRARY Phase 3 (2026-05-10) — optional wire to the
    // download index so remove() can evict per-episode rows for the show
    // being removed from library. See spec §6.5 Data Flow E.
    void setStreamDownloadIndex(StreamDownloadIndex* idx) { m_downloadIndex = idx; }

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL 2026-05-12 Phase 7 — optional wire
    // to the torrent client so remove() can cancel any active stream-bulk
    // cohort for the show being removed (cancelStreamBulkGroup with
    // deleteFiles=true). Closes spec §9.3 — engine-level guarantee that
    // Remove-from-Library leaves no orphaned cohorts. Independent of any
    // UI-level confirmation; this is the belt-and-suspenders cleanup.
    void setTorrentClient(TorrentClient* client) { m_torrentClient = client; }

signals:
    void libraryChanged();

private:
    void load();
    void save();

    static StreamLibraryEntry fromJson(const QJsonObject& obj);
    static QJsonObject toJson(const StreamLibraryEntry& entry);

    JsonStore* m_store;
    mutable QMutex m_mutex;
    QHash<QString, StreamLibraryEntry> m_entries;
    StreamDownloadIndex* m_downloadIndex = nullptr;
    TorrentClient* m_torrentClient = nullptr;

    static constexpr const char* FILENAME = "stream_library.json";
};
