#pragma once

#include <QJsonObject>
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
//   "positionSec":  double,
//   "durationSec":  double,
//   "finished":     bool,
//   "completedAtMs": qint64,
//   "updatedAt":    qint64   (auto-set by CoreBridge::saveProgress)
// }

inline QJsonObject makeWatchState(double positionSec, double durationSec, bool finished)
{
    QJsonObject obj;
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

} // namespace StreamChoices
