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
    QJsonObject allEpisodePayloadsForStreamDomain() const;

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
    static QString streamDomainKeyForEntry(const Entry& entry);

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
