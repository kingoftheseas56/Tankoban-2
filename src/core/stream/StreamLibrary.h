#pragma once

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QString>

class JsonStore;

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

    static constexpr const char* FILENAME = "stream_library.json";
};
