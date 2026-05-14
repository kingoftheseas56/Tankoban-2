#include "StreamLibrary.h"
#include "core/JsonStore.h"
#include "StreamDownloadIndex.h"
#include "core/torrent/TorrentClient.h"

#include <QDateTime>
#include <QJsonObject>

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

    // STREAM_DOWNLOADED_LIBRARY Phase 3 (2026-05-10) — Remove from library
    // also evicts any per-episode download entries for this show.
    if (m_downloadIndex)
        m_downloadIndex->evictByImdb(imdbId);

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL 2026-05-12 Phase 7 — engine-level
    // cancel-on-Remove. Walks every active "stream:<imdb>:*" cohort and
    // cancels with deleteFiles=true so the show's removal also drops any
    // in-flight downloaded bytes from disk. Closes spec §9.3. The UI layer
    // is responsible for the user-facing confirmation dialog BEFORE
    // calling remove(); this is the engine-level guarantee that any
    // caller (UI, scripted, future API) gets the file-delete behavior
    // consistently.
    if (m_torrentClient && m_torrentClient->imdbHasActiveCohort(imdbId)) {
        const QString prefix = QStringLiteral("stream:") + imdbId + QLatin1Char(':');
        const QJsonObject snap = m_torrentClient->streamBulkGroupsSnapshot();
        for (auto it = snap.constBegin(); it != snap.constEnd(); ++it) {
            if (!it.key().startsWith(prefix)) continue;
            m_torrentClient->cancelStreamBulkGroup(it.key(), /*deleteFiles=*/true);
        }
    }

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
