#include "StreamLibrary.h"
#include "core/JsonStore.h"

#include <QDateTime>

StreamLibrary::StreamLibrary(JsonStore* store, QObject* parent)
    : QObject(parent)
    , m_store(store)
{
    load();
}

void StreamLibrary::add(const StreamLibraryEntry& entry)
{
    if (!entry.imdb.startsWith("tt") || entry.imdb.isEmpty())
        return;

    QMutexLocker lock(&m_mutex);
    StreamLibraryEntry e = entry;
    if (e.addedAt <= 0)
        e.addedAt = QDateTime::currentMSecsSinceEpoch();
    m_entries[e.imdb] = e;
    lock.unlock();

    save();
    emit libraryChanged();
}

bool StreamLibrary::remove(const QString& imdbId)
{
    QMutexLocker lock(&m_mutex);
    if (!m_entries.remove(imdbId))
        return false;
    lock.unlock();

    save();
    emit libraryChanged();
    return true;
}

bool StreamLibrary::has(const QString& imdbId) const
{
    QMutexLocker lock(&m_mutex);
    return m_entries.contains(imdbId);
}

StreamLibraryEntry StreamLibrary::get(const QString& imdbId) const
{
    QMutexLocker lock(&m_mutex);
    return m_entries.value(imdbId);
}

QList<StreamLibraryEntry> StreamLibrary::getAll() const
{
    QMutexLocker lock(&m_mutex);
    return m_entries.values();
}

// ─── Persistence ─────────────────────────────────────────────────────────────

void StreamLibrary::load()
{
    QJsonObject root = m_store->read(FILENAME);
    QMutexLocker lock(&m_mutex);
    m_entries.clear();

    for (auto it = root.begin(); it != root.end(); ++it) {
        QString key = it.key();
        if (!key.startsWith("tt"))
            continue;

        StreamLibraryEntry entry = fromJson(it->toObject());
        if (!entry.imdb.isEmpty())
            m_entries[entry.imdb] = entry;
    }
}

void StreamLibrary::save()
{
    QJsonObject root;
    QMutexLocker lock(&m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        root[it.key()] = toJson(it.value());
    lock.unlock();

    m_store->write(FILENAME, root);
}

StreamLibraryEntry StreamLibrary::fromJson(const QJsonObject& obj)
{
    StreamLibraryEntry e;
    e.imdb        = obj.value("imdb").toString().trimmed();
    e.type        = obj.value("type").toString().trimmed();
    e.name        = obj.value("name").toString().trimmed();
    e.year        = obj.value("year").toString().trimmed();
    e.poster      = obj.value("poster").toString().trimmed();
    e.description = obj.value("description").toString().trimmed();
    e.imdbRating  = obj.value("imdbRating").toString().trimmed();
    e.addedAt     = obj.value("addedAt").toInteger(0);
    return e;
}

QJsonObject StreamLibrary::toJson(const StreamLibraryEntry& entry)
{
    QJsonObject obj;
    obj["imdb"]        = entry.imdb;
    obj["type"]        = entry.type;
    obj["name"]        = entry.name;
    obj["year"]        = entry.year;
    obj["poster"]      = entry.poster;
    obj["description"] = entry.description;
    obj["imdbRating"]  = entry.imdbRating;
    obj["addedAt"]     = entry.addedAt;
    return obj;
}
