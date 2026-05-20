#include "StreamDownloadIndex.h"

#include "core/DebugLogBuffer.h"
#include "core/JsonlEventLog.h"
#include "core/stream/QualityScorer.h"
#include "core/torrent/TorrentRepository.h"  // Phase 3.4 — durable backing store
#include "core/torrent/TorrentRow.h"          // StreamDownloadRow

#include <QDateTime>
#include <QDir>
#include <algorithm>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPair>
#include <QStringList>

// ── Phase 3.4 enum <-> string helpers (TorrentRepository stores state as
// TEXT; StreamDownloadIndex::Entry::State is the in-app enum) ──────────────
namespace {

using ::StreamDownloadIndex;

QString entryStateToRepoStr(StreamDownloadIndex::Entry::State s) {
    switch (s) {
        case StreamDownloadIndex::Entry::Complete:    return QStringLiteral("complete");
        case StreamDownloadIndex::Entry::Pending:     return QStringLiteral("pending");
        case StreamDownloadIndex::Entry::Downloading: return QStringLiteral("downloading");
        case StreamDownloadIndex::Entry::Failed:      return QStringLiteral("failed");
    }
    return QStringLiteral("complete");
}

StreamDownloadIndex::Entry::State entryStateFromRepoStr(const QString& s) {
    if (s == QLatin1String("complete"))    return StreamDownloadIndex::Entry::Complete;
    if (s == QLatin1String("pending"))     return StreamDownloadIndex::Entry::Pending;
    if (s == QLatin1String("downloading")) return StreamDownloadIndex::Entry::Downloading;
    if (s == QLatin1String("failed"))      return StreamDownloadIndex::Entry::Failed;
    return StreamDownloadIndex::Entry::Complete;  // defensive default
}

tankoban::torrent::StreamDownloadRow rowFromEntry(const StreamDownloadIndex::Entry& e) {
    tankoban::torrent::StreamDownloadRow r;
    r.canonicalPath = e.canonicalPath;
    r.imdbId        = e.imdbId;
    r.season        = e.season;
    r.episode       = e.episode;
    r.state         = entryStateToRepoStr(e.state);
    r.infoHash      = QString();  // not tracked on Entry
    r.addedAt       = QDateTime::fromMSecsSinceEpoch(e.addedAt);
    r.sourceGroupId = e.sourceGroupId;
    r.progressPct   = e.progressPct;
    return r;
}

}  // namespace

// ── Static helpers ──────────────────────────────────────────────────────────

QString StreamDownloadIndex::computeCanonicalKey(const QString& anyPath)
{
    // Per spec §4.1 — lowercased native-form absolute path. Handles
    // Windows case-insensitivity + slash normalization in one pass.
    return QDir::toNativeSeparators(QFileInfo(anyPath).absoluteFilePath()).toLower();
}

QString StreamDownloadIndex::computeEpisodeKey(const QString& imdbId, int season, int episode)
{
    return QStringLiteral("%1:%2:%3")
        .arg(imdbId)
        .arg(season, 2, 10, QLatin1Char('0'))
        .arg(episode, 2, 10, QLatin1Char('0'));
}

// ── ctor + setRepository + load ─────────────────────────────────────────────

StreamDownloadIndex::StreamDownloadIndex(QObject* parent)
    : QObject(parent)
{
    // Phase 3.4 — no I/O at construction. Repo is injected via
    // setRepository(), which triggers load() to rebuild the in-memory maps
    // from the SQLite stream_downloads_index table.
}

void StreamDownloadIndex::setRepository(tankoban::torrent::TorrentRepository* repo)
{
    if (m_repo == repo) return;
    m_repo = repo;
    load();
    emit entriesChanged();  // late subscribers (StreamPage tiles, etc.) need to repaint
}

void StreamDownloadIndex::load()
{
    if (!m_repo) {
        return;  // not yet wired; entries stay empty
    }

    const auto rows = m_repo->listStreamDownloads();

    QMutexLocker lock(&m_mutex);
    m_byPath.clear();
    m_byEpisode.clear();
    m_imdbHasAny.clear();

    for (const auto& r : rows) {
        if (r.imdbId.isEmpty() || r.canonicalPath.isEmpty())
            continue;
        Entry e;
        e.imdbId        = r.imdbId;
        e.type          = (r.season == 0 && r.episode == 0)
                              ? QStringLiteral("movie")
                              : QStringLiteral("series");
        e.season        = r.season;
        e.episode       = r.episode;
        e.canonicalPath = r.canonicalPath;
        e.addedAt       = r.addedAt.isValid() ? r.addedAt.toMSecsSinceEpoch() : 0;
        e.sourceGroupId = r.sourceGroupId;
        e.fileSizeBytes = 0;  // not tracked in repo schema; cosmetic-only field
        e.state         = entryStateFromRepoStr(r.state);
        e.progressPct   = r.progressPct;

        const QString key = computeCanonicalKey(r.canonicalPath);
        m_byPath.insert(key, e);
        m_byEpisode.insert(computeEpisodeKey(e.imdbId, e.season, e.episode), key);
        m_imdbHasAny.insert(e.imdbId);
    }

    DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
        QStringLiteral("loaded from repo"),
        QJsonObject{{QStringLiteral("count"), m_byPath.size()}});
}

// ── Mutating API ────────────────────────────────────────────────────────────

void StreamDownloadIndex::recomputeImdbHasAnyLocked(const QString& imdbId)
{
    bool stillHasAny = false;
    for (const Entry& other : m_byPath) {
        if (other.imdbId == imdbId) { stillHasAny = true; break; }
    }
    if (stillHasAny)
        m_imdbHasAny.insert(imdbId);
    else
        m_imdbHasAny.remove(imdbId);
}

void StreamDownloadIndex::registerEpisode(const QString& imdbId, int season, int episode,
                                          const QString& canonicalPath,
                                          const QString& sourceGroupId,
                                          qint64 fileSizeBytes)
{
    if (imdbId.isEmpty() || canonicalPath.isEmpty() || season < 0 || episode < 0)
        return;

    Entry e;
    e.imdbId        = imdbId;
    e.type          = QStringLiteral("series");  // v1 series-only per spec §3 P5
    e.season        = season;
    e.episode       = episode;
    e.canonicalPath = canonicalPath;
    e.addedAt       = QDateTime::currentMSecsSinceEpoch();
    e.sourceGroupId = sourceGroupId;
    e.fileSizeBytes = fileSizeBytes;

    const QString key   = computeCanonicalKey(canonicalPath);
    const QString epKey = computeEpisodeKey(imdbId, season, episode);

    {
        QMutexLocker lock(&m_mutex);

        // Highest-quality-wins dedup for duplicate episode bindings. Ties
        // keep the first registered path so equal-quality redownloads do not
        // churn the show-view binding.
        QString displacedImdbId;
        auto epIt = m_byEpisode.constFind(epKey);
        if (epIt != m_byEpisode.constEnd()) {
            auto displacedIt = m_byPath.constFind(epIt.value());
            if (displacedIt != m_byPath.constEnd()) {
                const int existingScore = tankostream::stream::QualityScorer::qualityScore(
                    QFileInfo(displacedIt.value().canonicalPath).fileName());
                const int newScore = tankostream::stream::QualityScorer::qualityScore(
                    QFileInfo(canonicalPath).fileName());
                if (newScore <= existingScore)
                    return;
                displacedImdbId = displacedIt.value().imdbId;
            }
            m_byPath.remove(epIt.value());
        }

        m_byPath.insert(key, e);
        m_byEpisode.insert(epKey, key);
        m_imdbHasAny.insert(imdbId);

        // If the displaced entry belonged to a different imdb, recompute
        // its has-any flag (the displaced may have been the last entry for
        // that imdb).
        if (!displacedImdbId.isEmpty() && displacedImdbId != imdbId)
            recomputeImdbHasAnyLocked(displacedImdbId);
    }

    DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
        QStringLiteral("registerEpisode"),
        QJsonObject{{QStringLiteral("imdb"), imdbId},
                    {QStringLiteral("season"), season},
                    {QStringLiteral("episode"), episode},
                    {QStringLiteral("path"), canonicalPath},
                    {QStringLiteral("groupId"), sourceGroupId}});
    JsonlEventLog::instance().emitEvent(
        QStringLiteral("download.indexed"),
        QStringLiteral("register_episode"),
        QJsonObject{{QStringLiteral("imdb"), imdbId},
                    {QStringLiteral("season"), season},
                    {QStringLiteral("episode"), episode},
                    {QStringLiteral("canonicalPath"), canonicalPath},
                    {QStringLiteral("sourceGroupId"), sourceGroupId},
                    {QStringLiteral("fileSizeBytes"), static_cast<double>(fileSizeBytes)}});

    // Phase 3.4 — durable write to repo. Off-mutex per the threading contract
    // documented in the header: mutating methods drop the lock before save().
    if (m_repo)
        m_repo->upsertStreamDownload(rowFromEntry(e));
    else
        qWarning() << "[StreamDownloadIndex] registerEpisode dropped — repo not yet wired";
    emit entriesChanged();
}

void StreamDownloadIndex::registerMovie(const QString& imdbId,
                                        const QString& canonicalPath,
                                        const QString& sourceGroupId,
                                        qint64 fileSizeBytes)
{
    registerEpisode(imdbId, 0, 0, canonicalPath, sourceGroupId, fileSizeBytes);

    bool changed = false;
    {
        QMutexLocker lock(&m_mutex);
        const QString key = computeCanonicalKey(canonicalPath);
        auto it = m_byPath.find(key);
        if (it != m_byPath.end() && it->type != QStringLiteral("movie")) {
            it->type = QStringLiteral("movie");
            changed = true;
        }
    }

    // Phase 3.4 — `type` field is derived from (season==0 && episode==0) at
    // load time in the post-cutover world, so persistence isn't needed here;
    // registerEpisode's own repo write already captured the row at season=0,
    // episode=0 which the next load() will classify as "movie". The emit
    // stays so live UI consumers see the in-memory `type` flip immediately.
    if (changed)
        emit entriesChanged();
}

void StreamDownloadIndex::registerPendingEpisode(const QString& imdbId, int season,
                                                 int episode,
                                                 const QString& canonicalPath,
                                                 const QString& sourceGroupId,
                                                 qint64 fileSizeBytes)
{
    QString epKey = computeEpisodeKey(imdbId, season, episode);
    QString canonKey = computeCanonicalKey(canonicalPath);
    Entry persisted;
    {
        QMutexLocker locker(&m_mutex);
        Entry e;
        e.imdbId = imdbId;
        e.type = QStringLiteral("series");
        e.season = season;
        e.episode = episode;
        e.canonicalPath = canonicalPath;
        e.addedAt = QDateTime::currentMSecsSinceEpoch();  // Phase 3.4 — ms-since-epoch matches registerEpisode for repo round-trip
        e.sourceGroupId = sourceGroupId;
        e.fileSizeBytes = fileSizeBytes;
        e.state = Entry::Pending;
        e.progressPct = 0;
        m_byPath.insert(canonKey, e);
        m_byEpisode.insert(epKey, canonKey);
        m_imdbHasAny.insert(imdbId);
        persisted = e;
    }
    if (m_repo)
        m_repo->upsertStreamDownload(rowFromEntry(persisted));
    else
        qWarning() << "[StreamDownloadIndex] registerPendingEpisode dropped — repo not yet wired";
    emit entriesChanged();
    emit entryStateChanged(imdbId, season, episode);
}

void StreamDownloadIndex::registerPendingMovie(const QString& imdbId,
                                               const QString& canonicalPath,
                                               const QString& sourceGroupId,
                                               qint64 fileSizeBytes)
{
    QString canonKey = computeCanonicalKey(canonicalPath);
    QString epKey = computeEpisodeKey(imdbId, 0, 0);
    Entry persisted;
    {
        QMutexLocker locker(&m_mutex);
        Entry e;
        e.imdbId = imdbId;
        e.type = QStringLiteral("movie");
        e.season = 0;
        e.episode = 0;
        e.canonicalPath = canonicalPath;
        e.addedAt = QDateTime::currentMSecsSinceEpoch();
        e.sourceGroupId = sourceGroupId;
        e.fileSizeBytes = fileSizeBytes;
        e.state = Entry::Pending;
        e.progressPct = 0;
        m_byPath.insert(canonKey, e);
        m_byEpisode.insert(epKey, canonKey);
        m_imdbHasAny.insert(imdbId);
        persisted = e;
    }
    if (m_repo)
        m_repo->upsertStreamDownload(rowFromEntry(persisted));
    else
        qWarning() << "[StreamDownloadIndex] registerPendingMovie dropped — repo not yet wired";
    emit entriesChanged();
    emit entryStateChanged(imdbId, 0, 0);
}

void StreamDownloadIndex::updateEpisodeProgress(const QString& imdbId, int season,
                                                int episode, int progressPct)
{
    QString epKey = computeEpisodeKey(imdbId, season, episode);
    bool changed = false;
    Entry persisted;
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_byEpisode.find(epKey);
        if (it == m_byEpisode.end())
            return;  // unknown entry, no-op
        const QString canonKey = it.value();
        auto pathIt = m_byPath.find(canonKey);
        if (pathIt == m_byPath.end())
            return;
        Entry& e = pathIt.value();
        const int clamped = std::clamp(progressPct, 0, 100);
        Entry::State newState = e.state;
        if (clamped >= 100) {
            newState = Entry::Complete;
        } else if (clamped > 0) {
            newState = Entry::Downloading;
        }
        if (clamped != e.progressPct || newState != e.state) {
            e.progressPct = clamped;
            e.state = newState;
            changed = true;
            persisted = e;
        }
    }
    if (changed) {
        if (m_repo)
            m_repo->upsertStreamDownload(rowFromEntry(persisted));
        emit entryStateChanged(imdbId, season, episode);
    }
}

void StreamDownloadIndex::evictBySourceGroup(const QString& sourceGroupId)
{
    if (sourceGroupId.isEmpty())
        return;
    QList<QString> evictedImdbs;
    QList<QString> evictedPaths;  // display-form paths for repo delete
    {
        QMutexLocker locker(&m_mutex);
        QList<QString> keysToEvict;
        QList<QString> epKeysToEvict;
        for (auto it = m_byPath.begin(); it != m_byPath.end(); ++it) {
            if (it.value().sourceGroupId == sourceGroupId) {
                keysToEvict.append(it.key());
                epKeysToEvict.append(
                    computeEpisodeKey(it.value().imdbId,
                                      it.value().season,
                                      it.value().episode));
                evictedPaths.append(it.value().canonicalPath);
                if (!evictedImdbs.contains(it.value().imdbId))
                    evictedImdbs.append(it.value().imdbId);
            }
        }
        for (const QString& k : keysToEvict)
            m_byPath.remove(k);
        for (const QString& k : epKeysToEvict)
            m_byEpisode.remove(k);
        for (const QString& imdb : evictedImdbs)
            recomputeImdbHasAnyLocked(imdb);
    }
    if (m_repo) {
        for (const QString& p : evictedPaths)
            m_repo->removeStreamDownload(p);
    }
    emit entriesChanged();
}

void StreamDownloadIndex::evictByImdb(const QString& imdbId)
{
    if (imdbId.isEmpty())
        return;

    int removed = 0;
    QList<QString> evictedPaths;
    {
        QMutexLocker lock(&m_mutex);
        QList<QPair<QString, QString>> toRemove;  // (canonicalKey, episodeKey)
        for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it) {
            if (it.value().imdbId == imdbId) {
                toRemove.append({it.key(),
                                 computeEpisodeKey(imdbId, it.value().season,
                                                   it.value().episode)});
                evictedPaths.append(it.value().canonicalPath);
            }
        }
        for (const auto& pr : toRemove) {
            m_byPath.remove(pr.first);
            m_byEpisode.remove(pr.second);
        }
        m_imdbHasAny.remove(imdbId);
        removed = toRemove.size();
    }

    if (removed > 0) {
        DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
            QStringLiteral("evictByImdb"),
            QJsonObject{{QStringLiteral("imdb"), imdbId},
                        {QStringLiteral("removed"), removed}});
        if (m_repo) {
            for (const QString& p : evictedPaths)
                m_repo->removeStreamDownload(p);
        }
        emit entriesChanged();
    }
}

void StreamDownloadIndex::evictByPath(const QString& canonicalKey)
{
    if (canonicalKey.isEmpty())
        return;

    bool changed = false;
    bool removeImdbFlag = false;
    QString affectedImdb;
    QString evictedDisplayPath;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_byPath.constFind(canonicalKey);
        if (it == m_byPath.constEnd())
            return;
        const Entry e = it.value();
        evictedDisplayPath = e.canonicalPath;
        m_byPath.remove(canonicalKey);
        m_byEpisode.remove(computeEpisodeKey(e.imdbId, e.season, e.episode));
        affectedImdb = e.imdbId;
        // Recompute m_imdbHasAny membership for this imdb.
        recomputeImdbHasAnyLocked(e.imdbId);
        // Re-read the flag for the log entry below.
        removeImdbFlag = !m_imdbHasAny.contains(e.imdbId);
        changed = true;
    }

    if (changed) {
        DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
            QStringLiteral("evictByPath"),
            QJsonObject{{QStringLiteral("path"), canonicalKey},
                        {QStringLiteral("imdb"), affectedImdb},
                        {QStringLiteral("imdbStillHasAny"), !removeImdbFlag}});
        if (m_repo)
            m_repo->removeStreamDownload(evictedDisplayPath);
        emit entriesChanged();
    }
}

void StreamDownloadIndex::validateAll()
{
    // Snapshot the keys+paths under lock; stat off-lock; collect missing;
    // re-acquire lock to evict.
    QList<QPair<QString, QString>> snapshot;  // canonicalKey -> displayPath
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it)
            snapshot.append({it.key(), it.value().canonicalPath});
    }

    QStringList missing;
    for (const auto& pr : snapshot) {
        if (!QFileInfo::exists(pr.second))
            missing.append(pr.first);
    }

    if (missing.isEmpty())
        return;

    DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
        QStringLiteral("validateAll evicting missing entries"),
        QJsonObject{{QStringLiteral("count"), missing.size()}});

    for (const QString& key : missing)
        evictByPath(key);
}

void StreamDownloadIndex::validateInFlightEntries(const QSet<QString>& activeInfoHashes)
{
    QList<QString> keysToEvict;
    QList<QString> epKeysToEvict;
    QList<QString> evictedImdbs;
    QList<QString> evictedDisplayPaths;
    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_byPath.begin(); it != m_byPath.end(); ++it) {
            const Entry& e = it.value();
            if (e.state != Entry::Pending && e.state != Entry::Downloading)
                continue;
            // sourceGroupId for Tankorent path is "tankorent:<infoHash>"
            const QString prefix = QStringLiteral("tankorent:");
            if (!e.sourceGroupId.startsWith(prefix))
                continue;  // not a Tankorent in-flight entry
            const QString infoHash = e.sourceGroupId.mid(prefix.size());
            if (activeInfoHashes.contains(infoHash))
                continue;  // libtorrent knows about it — leave alone
            keysToEvict.append(it.key());
            epKeysToEvict.append(
                computeEpisodeKey(e.imdbId, e.season, e.episode));
            evictedDisplayPaths.append(e.canonicalPath);
            if (!evictedImdbs.contains(e.imdbId))
                evictedImdbs.append(e.imdbId);
        }
        for (const QString& k : keysToEvict)
            m_byPath.remove(k);
        for (const QString& k : epKeysToEvict)
            m_byEpisode.remove(k);
        for (const QString& imdb : evictedImdbs)
            recomputeImdbHasAnyLocked(imdb);
    }
    if (!keysToEvict.isEmpty()) {
        if (m_repo) {
            for (const QString& p : evictedDisplayPaths)
                m_repo->removeStreamDownload(p);
        }
        emit entriesChanged();
        qDebug() << "validateInFlightEntries: evicted" << keysToEvict.size()
                 << "stale Pending/Downloading entries";
    }
}

// ── Read API ────────────────────────────────────────────────────────────────

bool StreamDownloadIndex::isStreamOwned(const QString& canonicalKey) const
{
    QMutexLocker lock(&m_mutex);
    return m_byPath.contains(canonicalKey);
}

std::optional<QString> StreamDownloadIndex::filePathFor(const QString& imdbId,
                                                        int season, int episode) const
{
    const QString epKey = computeEpisodeKey(imdbId, season, episode);
    QMutexLocker lock(&m_mutex);
    auto it = m_byEpisode.constFind(epKey);
    if (it == m_byEpisode.constEnd())
        return std::nullopt;
    auto pIt = m_byPath.constFind(it.value());
    if (pIt == m_byPath.constEnd())
        return std::nullopt;
    return pIt.value().canonicalPath;
}

std::optional<QString> StreamDownloadIndex::filePathForMovie(const QString& imdbId) const
{
    return filePathFor(imdbId, 0, 0);
}

bool StreamDownloadIndex::hasAnyForImdb(const QString& imdbId) const
{
    QMutexLocker lock(&m_mutex);
    return m_imdbHasAny.contains(imdbId);
}

QList<StreamDownloadIndex::Entry> StreamDownloadIndex::entriesForImdb(const QString& imdbId) const
{
    QList<Entry> out;
    QMutexLocker lock(&m_mutex);
    for (const Entry& e : m_byPath) {
        if (e.imdbId == imdbId)
            out.append(e);
    }
    return out;
}

QList<StreamDownloadIndex::Entry> StreamDownloadIndex::all() const
{
    QMutexLocker lock(&m_mutex);
    return m_byPath.values();
}
