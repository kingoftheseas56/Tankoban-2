#include "StreamDownloadIndex.h"

#include "core/JsonStore.h"
#include "core/DebugLogBuffer.h"
#include "core/stream/QualityScorer.h"

#include <QDateTime>
#include <QDir>
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
    if (storedVersion != kSchemaVersion) {
        DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
            QStringLiteral("schema mismatch on load — starting empty"),
            QJsonObject{{QStringLiteral("storedVersion"), storedVersion},
                        {QStringLiteral("expected"), kSchemaVersion}});
        return;
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
