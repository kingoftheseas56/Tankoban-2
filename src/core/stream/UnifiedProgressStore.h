#pragma once

// TANKORENT_STREAM_INTEGRATION 2026-05-15 - canonical playback progress.
//
// Stores one shared progress value for Cinemeta-bound playback keyed by
// (imdbId, season, episode), and for unbound local content keyed by canonical
// path. CoreBridge adapts the legacy "stream" and "videos" progress APIs onto
// this store so existing callers keep their payload fields while positions
// converge onto one source of truth.

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>

class JsonStore;

class UnifiedProgressStore : public QObject
{
public:
    explicit UnifiedProgressStore(JsonStore* store, QObject* parent = nullptr);

    void setProgress(const QString& imdbId, int season, int episode,
                     double positionSec, double durationSec);
    double resumePositionFor(const QString& imdbId, int season, int episode) const;

    void setProgressByPath(const QString& canonicalPath,
                           double positionSec, double durationSec);
    double resumePositionForPath(const QString& canonicalPath) const;

    void setEpisodePayload(const QString& imdbId, int season, int episode,
                           const QJsonObject& payload);
    QJsonObject episodePayload(const QString& imdbId, int season, int episode) const;
    // SIX_MODE_RESTRUCTURE Arc 2 (2026-06-07), Task 5 — the continue-watching
    // key namespace is parameterized by a domain prefix so each video mode
    // (anime/tv/movies) reads its own "<prefix>:<imdb>[:s..:e..]" keys off the
    // same store. Default "stream" keeps legacy callers byte-identical.
    QJsonObject allEpisodePayloadsForStreamDomain(
        const QString& domainPrefix = QStringLiteral("stream")) const;

    // Dep-free inverse of streamDomainKeyForEntry(): parse a continue-watching
    // key under a given domain prefix back into (imdb, season, episode).
    // Mirrors CoreBridge::parseStreamProgressKey — both the 2-part imdb-only
    // form ("<prefix>:<imdb>" → season/episode == 0) and the 4-part
    // "<prefix>:<imdb>:s<season>:e<episode>" form. Returns false (and leaves
    // out-params untouched) if the key does not match the prefix/shape.
    // Static + co-located with the builder so the two can't drift; callable
    // from the unit test without pulling TorrentClient/libtorrent.
    static bool parseDomainKey(const QString& key, const QString& domainPrefix,
                               QString& outImdb, int& outSeason, int& outEpisode);

    void setPathPayload(const QString& canonicalPath, const QJsonObject& payload,
                        const QString& legacyVideoId = QString());
    QJsonObject pathPayload(const QString& canonicalPath) const;
    QJsonObject payloadForLegacyVideoId(const QString& legacyVideoId) const;
    QJsonObject allPathPayloadsForVideosDomain() const;

    void clearEpisode(const QString& imdbId, int season, int episode);
    void clearLegacyVideoId(const QString& legacyVideoId);

    static QString episodeKey(const QString& imdbId, int season, int episode);
    static QString pathKey(const QString& canonicalPath);

private:
    struct Entry {
        double positionSec = 0.0;
        double durationSec = 0.0;
        QJsonObject payload;
        QString imdbId;
        int season = -1;
        int episode = -1;
        QString canonicalPath;
        QString legacyVideoId;
    };

    void load();
    void save();
    static Entry entryFromObject(const QJsonObject& obj);
    static QJsonObject entryToObject(const Entry& entry);
    static QJsonObject normalizedPayload(const QJsonObject& payload);
    static QString streamDomainKeyForEntry(
        const Entry& entry, const QString& domainPrefix = QStringLiteral("stream"));

    void setEpisodePayloadLocked(const QString& imdbId, int season, int episode,
                                 const QJsonObject& payload);
    void setPathPayloadLocked(const QString& canonicalPath,
                              const QJsonObject& payload,
                              const QString& legacyVideoId);

    JsonStore* m_store = nullptr;
    mutable QMutex m_mutex;

    QHash<QString, Entry> m_byEpisode;
    QHash<QString, Entry> m_byPath;
    QHash<QString, QString> m_legacyVideoIdToPathKey;

    static constexpr const char* FILENAME = "unified_progress.json";
    static constexpr int kSchemaVersion = 1;
};
