#include "StreamDownloadIndex.h"

#include "core/JsonStore.h"
#include "core/DebugLogBuffer.h"
#include "core/JsonlEventLog.h"
#include "core/stream/QualityScorer.h"

#include <QDateTime>
#include <QDir>
#include <algorithm>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPair>
#include <QStringList>

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

// ── ctor + load/save ────────────────────────────────────────────────────────

StreamDownloadIndex::StreamDownloadIndex(JsonStore* store, QObject* parent)
    : QObject(parent), m_store(store)
{
    load();
}

void StreamDownloadIndex::load()
{
    if (!m_store)
        return;

    const QJsonObject data = m_store->read(FILENAME);
    if (data.isEmpty())
        return;  // first-launch: file doesn't exist yet, nothing to load

    const int storedVersion = data.value(QStringLiteral("version")).toInt(0);
    // Accept v1 (migrate) and v2 (read natively). Reject anything else.
    if (storedVersion < 1 || storedVersion > kSchemaVersion) {
        DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
            QStringLiteral("schema mismatch on load — starting empty"),
            QJsonObject{{QStringLiteral("storedVersion"), storedVersion},
                        {QStringLiteral("expected"), kSchemaVersion}});
        return;
    }
    if (storedVersion < kSchemaVersion) {
        DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
            QStringLiteral("migrating schema"),
            QJsonObject{{QStringLiteral("from"), storedVersion},
                        {QStringLiteral("to"), kSchemaVersion}});
    }

    const QJsonObject byPath = data.value(QStringLiteral("byPath")).toObject();

    QMutexLocker lock(&m_mutex);
    for (auto it = byPath.constBegin(); it != byPath.constEnd(); ++it) {
        const QJsonObject obj = it.value().toObject();
        Entry e;
        e.imdbId        = obj.value(QStringLiteral("imdbId")).toString();
        e.type          = obj.value(QStringLiteral("type")).toString();
        e.season        = obj.value(QStringLiteral("season")).toInt();
        e.episode       = obj.value(QStringLiteral("episode")).toInt();
        e.canonicalPath = obj.value(QStringLiteral("canonicalPath")).toString();
        e.addedAt       = static_cast<qint64>(obj.value(QStringLiteral("addedAt")).toDouble());
        e.sourceGroupId = obj.value(QStringLiteral("sourceGroupId")).toString();
        e.fileSizeBytes = static_cast<qint64>(obj.value(QStringLiteral("fileSizeBytes")).toDouble());
        // v2 fields — default Complete/100 for v1 migration.
        const int rawState = obj.value(QStringLiteral("state")).toInt(
            static_cast<int>(Entry::Complete));
        e.state = (rawState >= 0 && rawState <= static_cast<int>(Entry::Failed))
            ? static_cast<Entry::State>(rawState)
            : Entry::Complete;
        e.progressPct = obj.value(QStringLiteral("progressPct")).toInt(100);

        if (e.imdbId.isEmpty() || e.canonicalPath.isEmpty())
            continue;

        const QString key = it.key();
        m_byPath.insert(key, e);
        m_byEpisode.insert(computeEpisodeKey(e.imdbId, e.season, e.episode), key);
        m_imdbHasAny.insert(e.imdbId);
    }

    DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
        QStringLiteral("loaded entries"),
        QJsonObject{{QStringLiteral("count"), m_byPath.size()}});
}

void StreamDownloadIndex::save()
{
    if (!m_store)
        return;

    QJsonObject byPath;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it) {
            const Entry& e = it.value();
            QJsonObject obj;
            obj[QStringLiteral("imdbId")]        = e.imdbId;
            obj[QStringLiteral("type")]          = e.type;
            obj[QStringLiteral("season")]        = e.season;
            obj[QStringLiteral("episode")]       = e.episode;
            obj[QStringLiteral("canonicalPath")] = e.canonicalPath;
            obj[QStringLiteral("addedAt")]       = static_cast<double>(e.addedAt);
            obj[QStringLiteral("sourceGroupId")] = e.sourceGroupId;
            obj[QStringLiteral("fileSizeBytes")] = static_cast<double>(e.fileSizeBytes);
            obj[QStringLiteral("state")]         = static_cast<int>(e.state);
            obj[QStringLiteral("progressPct")]   = e.progressPct;
            byPath[it.key()] = obj;
        }
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kSchemaVersion;
    root[QStringLiteral("byPath")]  = byPath;
    m_store->write(FILENAME, root);
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

    save();
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

    if (changed) {
        save();
        emit entriesChanged();
    }
}

void StreamDownloadIndex::registerPendingEpisode(const QString& imdbId, int season,
                                                 int episode,
                                                 const QString& canonicalPath,
                                                 const QString& sourceGroupId,
                                                 qint64 fileSizeBytes)
{
    QString epKey = computeEpisodeKey(imdbId, season, episode);
    QString canonKey = computeCanonicalKey(canonicalPath);
    {
        QMutexLocker locker(&m_mutex);
        Entry e;
        e.imdbId = imdbId;
        e.type = QStringLiteral("series");
        e.season = season;
        e.episode = episode;
        e.canonicalPath = canonicalPath;
        e.addedAt = QDateTime::currentSecsSinceEpoch();
        e.sourceGroupId = sourceGroupId;
        e.fileSizeBytes = fileSizeBytes;
        e.state = Entry::Pending;
        e.progressPct = 0;
        m_byPath.insert(canonKey, e);
        m_byEpisode.insert(epKey, canonKey);
        m_imdbHasAny.insert(imdbId);
    }
    save();
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
    {
        QMutexLocker locker(&m_mutex);
        Entry e;
        e.imdbId = imdbId;
        e.type = QStringLiteral("movie");
        e.season = 0;
        e.episode = 0;
        e.canonicalPath = canonicalPath;
        e.addedAt = QDateTime::currentSecsSinceEpoch();
        e.sourceGroupId = sourceGroupId;
        e.fileSizeBytes = fileSizeBytes;
        e.state = Entry::Pending;
        e.progressPct = 0;
        m_byPath.insert(canonKey, e);
        m_byEpisode.insert(epKey, canonKey);
        m_imdbHasAny.insert(imdbId);
    }
    save();
    emit entriesChanged();
    emit entryStateChanged(imdbId, 0, 0);
}

void StreamDownloadIndex::updateEpisodeProgress(const QString& imdbId, int season,
                                                int episode, int progressPct)
{
    QString epKey = computeEpisodeKey(imdbId, season, episode);
    bool changed = false;
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
        }
    }
    if (changed) {
        save();
        emit entryStateChanged(imdbId, season, episode);
    }
}

void StreamDownloadIndex::evictBySourceGroup(const QString& sourceGroupId)
{
    if (sourceGroupId.isEmpty())
        return;
    QList<QString> evictedImdbs;
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
    save();
    emit entriesChanged();
}

void StreamDownloadIndex::evictByImdb(const QString& imdbId)
{
    if (imdbId.isEmpty())
        return;

    int removed = 0;
    {
        QMutexLocker lock(&m_mutex);
        QList<QPair<QString, QString>> toRemove;  // (canonicalKey, episodeKey)
        for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it) {
            if (it.value().imdbId == imdbId) {
                toRemove.append({it.key(),
                                 computeEpisodeKey(imdbId, it.value().season,
                                                   it.value().episode)});
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
        save();
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
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_byPath.constFind(canonicalKey);
        if (it == m_byPath.constEnd())
            return;
        const Entry e = it.value();
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
        save();
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
        save();
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
