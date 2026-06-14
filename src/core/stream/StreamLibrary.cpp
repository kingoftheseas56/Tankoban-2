#include "StreamLibrary.h"
#include "StreamLibraryCodec.h"
#include "core/JsonStore.h"
#include "core/JsonlEventLog.h"
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
    JsonlEventLog::instance().emitEvent(
        QStringLiteral("library.entry_added"),
        QStringLiteral("add"),
        QJsonObject{{QStringLiteral("imdb"), e.imdb},
                    {QStringLiteral("name"), e.name},
                    {QStringLiteral("type"), e.type},
                    {QStringLiteral("year"), e.year},
                    {QStringLiteral("addedAt"), static_cast<double>(e.addedAt)}});
    emit libraryChanged();
}

bool StreamLibrary::remove(const QString& imdbId)
{
    QMutexLocker lock(&m_mutex);
    if (!m_entries.remove(imdbId))
        return false;
    lock.unlock();

    save();
    JsonlEventLog::instance().emitEvent(
        QStringLiteral("library.entry_removed"),
        QStringLiteral("remove"),
        QJsonObject{{QStringLiteral("imdb"), imdbId}});
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

int StreamLibrary::clear()
{
    // THEATRE_CLEANUP F2 (2026-05-22) — bulk variant of remove(). Mirrors
    // the existing remove() cascade exactly: same evictByImdb + same cohort
    // cancellation with deleteFiles=true. The only structural difference is
    // (a) we capture the imdb list before clearing so we know what to
    // cascade-clean even after m_entries is empty, and (b) we emit
    // libraryChanged once for the bulk op, not N times.
    QMutexLocker lock(&m_mutex);
    const QStringList clearedImdbs = m_entries.keys();
    const int count = clearedImdbs.size();
    if (count == 0) {
        return 0;  // nothing to do — caller can still surface a "library was already empty" toast.
    }
    m_entries.clear();
    lock.unlock();

    save();
    JsonlEventLog::instance().emitEvent(
        QStringLiteral("library.cleared"),
        QStringLiteral("clear"),
        QJsonObject{{QStringLiteral("count"), count}});
    emit libraryChanged();

    // STREAM_DOWNLOADED_LIBRARY Phase 3 (2026-05-10) parity — evict per-
    // episode download-index rows for every cleared imdb.
    if (m_downloadIndex) {
        for (const QString& imdb : clearedImdbs)
            m_downloadIndex->evictByImdb(imdb);
    }

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL 2026-05-12 Phase 7 parity — cancel
    // every active stream-bulk cohort with deleteFiles=true. Snapshot once,
    // walk all "stream:*:*" keys (vs remove()'s "stream:<imdb>:*" filter)
    // since we're clearing the entire library and every cohort is in scope.
    // This is the engine-level guarantee that "Clear Library" leaves no
    // orphaned cohorts and drops every downloaded byte from disk.
    if (m_torrentClient) {
        const QJsonObject snap = m_torrentClient->streamBulkGroupsSnapshot();
        for (auto it = snap.constBegin(); it != snap.constEnd(); ++it) {
            if (!it.key().startsWith(QStringLiteral("stream:"))) continue;
            m_torrentClient->cancelStreamBulkGroup(it.key(), /*deleteFiles=*/true);
        }
    }

    return count;
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

        StreamLibraryEntry entry = streamLibraryEntryFromJson(it->toObject());
        if (!entry.imdb.isEmpty())
            m_entries[entry.imdb] = entry;
    }
}

void StreamLibrary::save()
{
    QJsonObject root;
    QMutexLocker lock(&m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        root[it.key()] = streamLibraryEntryToJson(it.value());
    lock.unlock();

    m_store->write(FILENAME, root);
}
