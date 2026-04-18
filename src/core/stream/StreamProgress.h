#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QSettings>
#include <QString>

class CoreBridge;

// ── Progress key format ──────────────────────────────────────────────────────
// Movie:   "stream:tt1234567"
// Episode: "stream:tt1234567:s1:e3"

namespace StreamProgress {

inline QString movieKey(const QString& imdbId)
{
    return "stream:" + imdbId;
}

inline QString episodeKey(const QString& imdbId, int season, int episode)
{
    return "stream:" + imdbId + ":s" + QString::number(season)
           + ":e" + QString::number(episode);
}

// ── Watch state payload ──────────────────────────────────────────────────
// {
//   "schema_version": int   (STREAM_ENGINE_REBUILD P0 hardening — additive-only
//                            versioning so future rebuild phases can evolve the
//                            payload without losing Continue Watching entries)
//   "positionSec":  double,
//   "durationSec":  double,
//   "finished":     bool,
//   "completedAtMs": qint64,
//   "updatedAt":    qint64   (auto-set by CoreBridge::saveProgress)
// }

inline QJsonObject makeWatchState(double positionSec, double durationSec, bool finished)
{
    QJsonObject obj;
    obj["schema_version"] = 1;
    obj["positionSec"]  = positionSec;
    obj["durationSec"]  = durationSec;
    obj["finished"]     = finished;
    if (finished)
        obj["completedAtMs"] = QDateTime::currentMSecsSinceEpoch();
    return obj;
}

inline double percent(const QJsonObject& state)
{
    double dur = state.value("durationSec").toDouble(0.0);
    if (dur <= 0.0) return 0.0;
    return (state.value("positionSec").toDouble(0.0) / dur) * 100.0;
}

inline bool isFinished(const QJsonObject& state)
{
    if (state.value("finished").toBool(false))
        return true;
    return percent(state) >= 90.0;
}

// ── Next-unwatched-episode helper (STREAM_UX_PARITY Phase 2 Batch 2.1) ───────
//
// Returns the first (season, episode) pair whose StreamProgress record is
// either absent OR not finished. If every pair in the input is finished,
// returns {0, 0} signaling "no next episode" — callers treat this as "series
// fully watched, drop from Continue Watching."
//
// Caller pre-flattens `MetaAggregator::seriesMetaReady`'s QMap<int,
// QList<StreamEpisode>> into a `QList<QPair<int,int>>` sorted by (season asc,
// episode asc). This keeps StreamProgress free of a reverse dependency on
// MetaAggregator.
//
// Result is cached per-imdbId with a 5-minute TTL. Callers that observe a
// `CoreBridge::progressUpdated("stream", key)` for this series must call
// `invalidateNextUnwatchedCache(imdbId)` so the next lookup returns fresh.

struct NextUnwatchedCacheEntry {
    qint64 computedAtMs = 0;
    int    season       = 0;
    int    episode      = 0;
};

// inline = one storage location across all translation units (C++17 rule).
inline QHash<QString, NextUnwatchedCacheEntry> s_nextUnwatchedCache;

constexpr qint64 kNextUnwatchedTtlMs = 5LL * 60LL * 1000LL;

inline void invalidateNextUnwatchedCache(const QString& imdbId)
{
    s_nextUnwatchedCache.remove(imdbId);
}

inline void clearNextUnwatchedCache()
{
    s_nextUnwatchedCache.clear();
}

inline QPair<int, int> nextUnwatchedEpisode(
    const QString&                      imdbId,
    const QList<QPair<int, int>>&       episodesInOrder,
    const QJsonObject&                  allStreamProgress)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    const auto cacheIt = s_nextUnwatchedCache.constFind(imdbId);
    if (cacheIt != s_nextUnwatchedCache.constEnd()
        && nowMs - cacheIt->computedAtMs < kNextUnwatchedTtlMs) {
        return {cacheIt->season, cacheIt->episode};
    }

    for (const QPair<int, int>& se : episodesInOrder) {
        const QString key = episodeKey(imdbId, se.first, se.second);
        const QJsonObject state = allStreamProgress.value(key).toObject();
        if (state.isEmpty() || !isFinished(state)) {
            s_nextUnwatchedCache.insert(imdbId,
                NextUnwatchedCacheEntry{nowMs, se.first, se.second});
            return se;
        }
    }

    // Every episode in the list is finished.
    s_nextUnwatchedCache.insert(imdbId,
        NextUnwatchedCacheEntry{nowMs, 0, 0});
    return {0, 0};
}

} // namespace StreamProgress

// ── Torrent choice persistence ───────────────────────────────────────────────
// Remembers which torrent the user picked for a given episode so re-opening
// the same episode auto-selects the same source.
//
// Stored in QSettings under key "stream_choices/{episodeKey}"
// Value: QJsonObject with magnetUri, infoHash, fileIndex, quality, trackerSource

namespace StreamChoices {

inline void saveChoice(const QString& episodeKey, const QJsonObject& choice)
{
    QSettings s;
    s.setValue("stream_choices/" + episodeKey,
              QString::fromUtf8(QJsonDocument(choice).toJson(QJsonDocument::Compact)));
}

inline QJsonObject loadChoice(const QString& episodeKey)
{
    QSettings s;
    QString raw = s.value("stream_choices/" + episodeKey).toString();
    if (raw.isEmpty()) return {};
    return QJsonDocument::fromJson(raw.toUtf8()).object();
}

inline void clearChoice(const QString& episodeKey)
{
    QSettings s;
    s.remove("stream_choices/" + episodeKey);
}

// ── Series-level source memory (STREAM_UX_PARITY Phase 2 Batch 2.3) ──────
//
// Parallel to the per-episode `saveChoice/loadChoice` above. A per-series
// entry is written when the user picks a source for S1E1 AND that source's
// Stream payload carries a `behaviorHints.bingeGroup` (the addon's signal
// that "this release will work for other episodes of the series too"). On
// S1E2 open, if there's no per-episode saved choice, the per-series entry's
// bingeGroup is used to highlight a matching card in the incoming stream
// list. Movies don't use this layer — per-episode `saveChoice(movieKey)` is
// sufficient for single-title state.
//
// Stored in QSettings under "stream_series_choices/{imdbId}". The payload
// mirrors per-episode `saveChoice` + an explicit `bingeGroup` field so the
// caller doesn't have to re-read the Stream struct at resume time.

inline void saveSeriesChoice(const QString& imdbId, const QJsonObject& choice)
{
    QSettings s;
    s.setValue("stream_series_choices/" + imdbId,
              QString::fromUtf8(QJsonDocument(choice).toJson(QJsonDocument::Compact)));
}

inline QJsonObject loadSeriesChoice(const QString& imdbId)
{
    QSettings s;
    const QString raw = s.value("stream_series_choices/" + imdbId).toString();
    if (raw.isEmpty()) return {};
    return QJsonDocument::fromJson(raw.toUtf8()).object();
}

inline void clearSeriesChoice(const QString& imdbId)
{
    QSettings s;
    s.remove("stream_series_choices/" + imdbId);
}

} // namespace StreamChoices
