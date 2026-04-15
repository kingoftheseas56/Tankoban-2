#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QUrl>

#include "addon/StreamInfo.h"
#include "addon/SubtitleInfo.h"

namespace tankostream::addon {
class AddonRegistry;
}

namespace tankostream::stream {

struct SubtitleLoadRequest {
    QString type;     // "movie" | "series"
    QString id;       // imdbId for movie; imdbId:S:E for series episode
    tankostream::addon::Stream selectedStream;
};

class SubtitlesAggregator : public QObject
{
    Q_OBJECT

public:
    explicit SubtitlesAggregator(tankostream::addon::AddonRegistry* registry,
                                 QObject* parent = nullptr);

    void load(const SubtitleLoadRequest& request);

signals:
    // originByTrackKey maps canonicalTrackKey(track) → addonId (the addon that supplied the track).
    void subtitlesReady(const QList<tankostream::addon::SubtitleTrack>& tracks,
                        const QHash<QString, QString>& originByTrackKey);
    void subtitlesError(const QString& addonId, const QString& message);

private:
    struct PendingAddon {
        QString addonId;
        QString addonName;
        QUrl baseUrl;
        bool inFlight = false;
    };

    struct CacheEntry {
        qint64 timestampMs = 0;
        QList<tankostream::addon::SubtitleTrack> tracks;
        QHash<QString, QString> originByTrackKey;
    };

    void dispatch();
    void onAddonReady(const QString& addonId, const QJsonObject& payload);
    void onAddonFailed(const QString& addonId, const QString& message);
    void completeOne();
    void resetTransientState();

    static QString makeCacheKey(const SubtitleLoadRequest& request);

    tankostream::addon::AddonRegistry* m_registry = nullptr;

    SubtitleLoadRequest m_request;
    QList<QPair<QString, QString>> m_requestExtra;
    QMap<QString, PendingAddon> m_pendingByAddon;
    int m_pendingResponses = 0;

    QList<tankostream::addon::SubtitleTrack> m_tracks;
    QSet<QString> m_seenTrackKeys;
    QHash<QString, QString> m_originByTrackKey;

    QHash<QString, CacheEntry> m_cache;

    static constexpr qint64 kSubtitleCacheTtlMs = 30LL * 60LL * 1000LL; // 30 min
};

}
