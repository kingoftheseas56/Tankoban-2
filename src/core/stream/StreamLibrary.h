#pragma once

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QString>

class JsonStore;
class StreamDownloadIndex;

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

    // STREAM_DOWNLOADED_LIBRARY Phase 3 (2026-05-10) — optional wire to the
    // download index so remove() can evict per-episode rows for the show
    // being removed from library. See spec §6.5 Data Flow E.
    void setStreamDownloadIndex(StreamDownloadIndex* idx) { m_downloadIndex = idx; }

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

    static constexpr const char* FILENAME = "stream_library.json";
};
