#include "core/stream/UnifiedProgressStore.h"

#include "core/JsonStore.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QMutexLocker>

UnifiedProgressStore::UnifiedProgressStore(JsonStore* store, QObject* parent)
    : QObject(parent)
    , m_store(store)
{
    load();
}

QString UnifiedProgressStore::episodeKey(const QString& imdbId, int season, int episode)
{
    return QStringLiteral("%1:%2:%3").arg(imdbId).arg(season).arg(episode);
}

QString UnifiedProgressStore::pathKey(const QString& canonicalPath)
{
    return QDir::toNativeSeparators(QFileInfo(canonicalPath).absoluteFilePath()).toLower();
}

void UnifiedProgressStore::setProgress(const QString& imdbId, int season, int episode,
                                       double positionSec, double durationSec)
{
    QJsonObject payload;
    payload[QStringLiteral("positionSec")] = positionSec;
    payload[QStringLiteral("durationSec")] = durationSec;
    setEpisodePayload(imdbId, season, episode, payload);
}

double UnifiedProgressStore::resumePositionFor(const QString& imdbId,
                                               int season, int episode) const
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_byEpisode.constFind(episodeKey(imdbId, season, episode));
    if (it == m_byEpisode.constEnd())
        return 0.0;
    return it->positionSec;
}

void UnifiedProgressStore::setProgressByPath(const QString& canonicalPath,
                                             double positionSec, double durationSec)
{
    QJsonObject payload;
    payload[QStringLiteral("positionSec")] = positionSec;
    payload[QStringLiteral("durationSec")] = durationSec;
    setPathPayload(canonicalPath, payload);
}

double UnifiedProgressStore::resumePositionForPath(const QString& canonicalPath) const
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_byPath.constFind(pathKey(canonicalPath));
    if (it == m_byPath.constEnd())
        return 0.0;
    return it->positionSec;
}

void UnifiedProgressStore::setEpisodePayload(const QString& imdbId, int season, int episode,
                                             const QJsonObject& payload)
{
    {
        QMutexLocker lock(&m_mutex);
        setEpisodePayloadLocked(imdbId, season, episode, payload);
    }
    save();
}

QJsonObject UnifiedProgressStore::episodePayload(const QString& imdbId,
                                                 int season, int episode) const
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_byEpisode.constFind(episodeKey(imdbId, season, episode));
    if (it == m_byEpisode.constEnd())
        return {};
    return it->payload;
}

QJsonObject UnifiedProgressStore::allEpisodePayloadsForStreamDomain(
    const QString& domainPrefix) const
{
    QMutexLocker lock(&m_mutex);
    QJsonObject result;
    for (auto it = m_byEpisode.constBegin(); it != m_byEpisode.constEnd(); ++it) {
        const QString streamKey = streamDomainKeyForEntry(it.value(), domainPrefix);
        if (!streamKey.isEmpty())
            result[streamKey] = it->payload;
    }
    return result;
}

void UnifiedProgressStore::setPathPayload(const QString& canonicalPath,
                                          const QJsonObject& payload,
                                          const QString& legacyVideoId)
{
    {
        QMutexLocker lock(&m_mutex);
        setPathPayloadLocked(canonicalPath, payload, legacyVideoId);
    }
    save();
}

QJsonObject UnifiedProgressStore::pathPayload(const QString& canonicalPath) const
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_byPath.constFind(pathKey(canonicalPath));
    if (it == m_byPath.constEnd())
        return {};
    return it->payload;
}

QJsonObject UnifiedProgressStore::payloadForLegacyVideoId(const QString& legacyVideoId) const
{
    QMutexLocker lock(&m_mutex);
    const auto keyIt = m_legacyVideoIdToPathKey.constFind(legacyVideoId);
    if (keyIt == m_legacyVideoIdToPathKey.constEnd())
        return {};
    const auto entryIt = m_byPath.constFind(keyIt.value());
    if (entryIt == m_byPath.constEnd())
        return {};
    return entryIt->payload;
}

QJsonObject UnifiedProgressStore::allPathPayloadsForVideosDomain() const
{
    QMutexLocker lock(&m_mutex);
    QJsonObject result;
    for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it) {
        const QString key = it->legacyVideoId.isEmpty() ? it.key() : it->legacyVideoId;
        result[key] = it->payload;
    }
    return result;
}

void UnifiedProgressStore::clearEpisode(const QString& imdbId, int season, int episode)
{
    {
        QMutexLocker lock(&m_mutex);
        m_byEpisode.remove(episodeKey(imdbId, season, episode));
    }
    save();
}

void UnifiedProgressStore::clearLegacyVideoId(const QString& legacyVideoId)
{
    {
        QMutexLocker lock(&m_mutex);
        const auto keyIt = m_legacyVideoIdToPathKey.find(legacyVideoId);
        if (keyIt == m_legacyVideoIdToPathKey.end())
            return;
        m_byPath.remove(keyIt.value());
        m_legacyVideoIdToPathKey.erase(keyIt);
    }
    save();
}

void UnifiedProgressStore::load()
{
    if (!m_store)
        return;

    const QJsonObject root = m_store->read(FILENAME);
    if (root.value(QStringLiteral("version")).toInt(0) != kSchemaVersion)
        return;

    const QJsonObject byEpisode = root.value(QStringLiteral("byEpisode")).toObject();
    const QJsonObject byPath = root.value(QStringLiteral("byPath")).toObject();

    QMutexLocker lock(&m_mutex);
    for (auto it = byEpisode.constBegin(); it != byEpisode.constEnd(); ++it) {
        m_byEpisode.insert(it.key(), entryFromObject(it.value().toObject()));
    }
    for (auto it = byPath.constBegin(); it != byPath.constEnd(); ++it) {
        Entry entry = entryFromObject(it.value().toObject());
        if (entry.canonicalPath.isEmpty())
            entry.canonicalPath = it.key();
        m_byPath.insert(it.key(), entry);
        if (!entry.legacyVideoId.isEmpty())
            m_legacyVideoIdToPathKey.insert(entry.legacyVideoId, it.key());
    }
}

void UnifiedProgressStore::save()
{
    if (!m_store)
        return;

    QJsonObject byEpisode;
    QJsonObject byPath;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_byEpisode.constBegin(); it != m_byEpisode.constEnd(); ++it)
            byEpisode[it.key()] = entryToObject(it.value());
        for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it)
            byPath[it.key()] = entryToObject(it.value());
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kSchemaVersion;
    root[QStringLiteral("byEpisode")] = byEpisode;
    root[QStringLiteral("byPath")] = byPath;
    m_store->write(FILENAME, root);
}

UnifiedProgressStore::Entry UnifiedProgressStore::entryFromObject(const QJsonObject& obj)
{
    Entry entry;
    entry.positionSec = obj.value(QStringLiteral("positionSec")).toDouble(0.0);
    entry.durationSec = obj.value(QStringLiteral("durationSec")).toDouble(0.0);
    entry.payload = obj.value(QStringLiteral("payload")).toObject();
    entry.imdbId = obj.value(QStringLiteral("imdbId")).toString();
    entry.season = obj.value(QStringLiteral("season")).toInt(-1);
    entry.episode = obj.value(QStringLiteral("episode")).toInt(-1);
    entry.canonicalPath = obj.value(QStringLiteral("canonicalPath")).toString();
    entry.legacyVideoId = obj.value(QStringLiteral("legacyVideoId")).toString();
    if (entry.payload.isEmpty()) {
        entry.payload[QStringLiteral("positionSec")] = entry.positionSec;
        entry.payload[QStringLiteral("durationSec")] = entry.durationSec;
    }
    return entry;
}

QJsonObject UnifiedProgressStore::entryToObject(const Entry& entry)
{
    QJsonObject obj;
    obj[QStringLiteral("positionSec")] = entry.positionSec;
    obj[QStringLiteral("durationSec")] = entry.durationSec;
    obj[QStringLiteral("payload")] = entry.payload;
    if (!entry.imdbId.isEmpty())
        obj[QStringLiteral("imdbId")] = entry.imdbId;
    if (entry.season >= 0)
        obj[QStringLiteral("season")] = entry.season;
    if (entry.episode >= 0)
        obj[QStringLiteral("episode")] = entry.episode;
    if (!entry.canonicalPath.isEmpty())
        obj[QStringLiteral("canonicalPath")] = entry.canonicalPath;
    if (!entry.legacyVideoId.isEmpty())
        obj[QStringLiteral("legacyVideoId")] = entry.legacyVideoId;
    return obj;
}

QJsonObject UnifiedProgressStore::normalizedPayload(const QJsonObject& payload)
{
    QJsonObject normalized = payload;
    if (!normalized.contains(QStringLiteral("positionSec")))
        normalized[QStringLiteral("positionSec")] = 0.0;
    if (!normalized.contains(QStringLiteral("durationSec")))
        normalized[QStringLiteral("durationSec")] = 0.0;
    return normalized;
}

QString UnifiedProgressStore::streamDomainKeyForEntry(const Entry& entry,
                                                      const QString& domainPrefix)
{
    if (entry.imdbId.isEmpty())
        return {};
    if (entry.season <= 0 && entry.episode <= 0)
        return QStringLiteral("%1:%2").arg(domainPrefix, entry.imdbId);
    return QStringLiteral("%1:%2:s%3:e%4")
        .arg(domainPrefix, entry.imdbId)
        .arg(entry.season)
        .arg(entry.episode);
}

bool UnifiedProgressStore::parseDomainKey(const QString& key, const QString& domainPrefix,
                                          QString& outImdb, int& outSeason, int& outEpisode)
{
    const QStringList parts = key.split(QLatin1Char(':'));
    // 2-part imdb-only form: "<prefix>:<imdb>" — movies + season-less series.
    if (parts.size() == 2 && parts[0] == domainPrefix && !parts[1].isEmpty()) {
        outImdb = parts[1];
        outSeason = 0;
        outEpisode = 0;
        return true;
    }
    // 4-part episode form: "<prefix>:<imdb>:s<season>:e<episode>".
    if (parts.size() >= 4 && parts[0] == domainPrefix
        && parts[2].startsWith(QLatin1Char('s'))
        && parts[3].startsWith(QLatin1Char('e'))) {
        const int season = parts[2].mid(1).toInt();
        const int episode = parts[3].mid(1).toInt();
        if (!parts[1].isEmpty() && season > 0 && episode > 0) {
            outImdb = parts[1];
            outSeason = season;
            outEpisode = episode;
            return true;
        }
    }
    return false;
}

void UnifiedProgressStore::setEpisodePayloadLocked(const QString& imdbId,
                                                   int season, int episode,
                                                   const QJsonObject& payload)
{
    if (imdbId.isEmpty() || season < 0 || episode < 0)
        return;

    const QString key = episodeKey(imdbId, season, episode);
    Entry entry = m_byEpisode.value(key);
    entry.imdbId = imdbId;
    entry.season = season;
    entry.episode = episode;
    entry.payload = normalizedPayload(payload);
    entry.positionSec = entry.payload.value(QStringLiteral("positionSec")).toDouble(0.0);
    entry.durationSec = entry.payload.value(QStringLiteral("durationSec")).toDouble(0.0);
    entry.legacyVideoId = entry.payload.value(QStringLiteral("legacyVideoId")).toString(entry.legacyVideoId);

    const QString path = entry.payload.value(QStringLiteral("path")).toString();
    if (!path.isEmpty()) {
        entry.canonicalPath = path;
        setPathPayloadLocked(path, entry.payload,
                             entry.payload.value(QStringLiteral("legacyVideoId")).toString());
    }

    m_byEpisode.insert(key, entry);
}

void UnifiedProgressStore::setPathPayloadLocked(const QString& canonicalPath,
                                                const QJsonObject& payload,
                                                const QString& legacyVideoId)
{
    if (canonicalPath.isEmpty())
        return;

    const QString key = pathKey(canonicalPath);
    Entry entry = m_byPath.value(key);
    entry.canonicalPath = canonicalPath;
    entry.payload = normalizedPayload(payload);
    entry.positionSec = entry.payload.value(QStringLiteral("positionSec")).toDouble(0.0);
    entry.durationSec = entry.payload.value(QStringLiteral("durationSec")).toDouble(0.0);
    if (!legacyVideoId.isEmpty())
        entry.legacyVideoId = legacyVideoId;
    if (!entry.legacyVideoId.isEmpty())
        m_legacyVideoIdToPathKey.insert(entry.legacyVideoId, key);

    const QString imdbId = entry.payload.value(QStringLiteral("imdbId")).toString(entry.imdbId);
    const int season = entry.payload.value(QStringLiteral("season")).toInt(entry.season);
    const int episode = entry.payload.value(QStringLiteral("episode")).toInt(entry.episode);
    if (!imdbId.isEmpty() && season >= 0 && episode >= 0) {
        entry.imdbId = imdbId;
        entry.season = season;
        entry.episode = episode;
        Entry epEntry = entry;
        m_byEpisode.insert(episodeKey(imdbId, season, episode), epEntry);
    }

    m_byPath.insert(key, entry);
}
