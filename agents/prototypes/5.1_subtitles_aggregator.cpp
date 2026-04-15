// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 5.1 (Subtitles)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:230
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:234
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:316
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/chat.md:9025
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/chat.md:9052
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/review_archive/2026-04-14_tankostream_phase4.md:79
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/SubtitleInfo.h:8
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/StreamInfo.h:15
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/StreamInfo.h:23
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/StreamInfo.h:24
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/StreamInfo.h:25
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonRegistry.h:24
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamAggregator.h:37
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamAggregator.cpp:190
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamAggregator.cpp:194
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamAggregator.cpp:195
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamAggregator.cpp:234
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamAggregator.cpp:455
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/MetaAggregator.cpp:36
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:309
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:328
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:359
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/resource/subtitles.rs:9
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/resource/subtitles.rs:10
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/resource/subtitles.rs:16
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 5.1.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <memory>

namespace tankostream::addon {

struct ManifestResource {
    QString name;
    QStringList types;
    bool hasTypes = false;
};

struct AddonManifest {
    QString id;
    QString name;
    QStringList types;
    QList<ManifestResource> resources;
};

struct AddonDescriptorFlags {
    bool official = false;
    bool enabled = true;
    bool protectedAddon = false;
};

struct AddonDescriptor {
    AddonManifest manifest;
    QUrl transportUrl;
    AddonDescriptorFlags flags;
};

struct ResourceRequest {
    QString resource;
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;
};

struct SubtitleTrack {
    QString id;
    QString lang;
    QUrl url;
    QString label;
};

struct StreamBehaviorHints {
    bool notWebReady = false;
    QString bingeGroup;
    QStringList countryWhitelist;
    QHash<QString, QString> proxyRequestHeaders;
    QHash<QString, QString> proxyResponseHeaders;
    QString filename;
    QString videoHash;
    qint64 videoSize = 0;
    QVariantMap other;
};

struct StreamSource {
    enum class Kind {
        Url,
        Magnet,
        YouTube,
        Http,
    };

    Kind kind = Kind::Url;
    QUrl url;
    QString infoHash;
    int fileIndex = -1;
    QString fileNameHint;
    QStringList trackers;
    QString youtubeId;
};

struct Stream {
    StreamSource source;
    QString name;
    QString description;
    QUrl thumbnail;
    StreamBehaviorHints behaviorHints;
    QList<SubtitleTrack> subtitles;
};

class AddonRegistry : public QObject {
    Q_OBJECT
public:
    explicit AddonRegistry(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    QList<AddonDescriptor> findByResourceType(const QString& resource,
                                              const QString& type) const;
};

class AddonTransport : public QObject {
    Q_OBJECT
public:
    explicit AddonTransport(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    void fetchResource(const QUrl& base, const ResourceRequest& request);

signals:
    void resourceReady(const ResourceRequest& request, const QJsonObject& payload);
    void resourceFailed(const ResourceRequest& request, const QString& message);
};

} // namespace tankostream::addon

namespace tankostream::stream {

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::AddonTransport;
using tankostream::addon::ResourceRequest;
using tankostream::addon::Stream;
using tankostream::addon::SubtitleTrack;

namespace {

constexpr qint64 kSubtitleCacheTtlMs = 5LL * 60LL * 1000LL;

bool sameRequest(const ResourceRequest& a, const ResourceRequest& b)
{
    return a.resource == b.resource
        && a.type == b.type
        && a.id == b.id
        && a.extra == b.extra;
}

QString canonicalTrackKey(const SubtitleTrack& track)
{
    return track.id.trimmed().toLower()
        + QLatin1Char('|')
        + track.lang.trimmed().toLower()
        + QLatin1Char('|')
        + track.url.toString(QUrl::FullyEncoded).toLower();
}

QList<QPair<QString, QString>> buildSubtitleExtra(const Stream& stream)
{
    QList<QPair<QString, QString>> extra;

    const QString videoHash = stream.behaviorHints.videoHash.trimmed();
    if (!videoHash.isEmpty()) {
        // Keep both key spellings. Addons in the wild use both forms.
        extra.append(qMakePair(QStringLiteral("videoHash"), videoHash));
        extra.append(qMakePair(QStringLiteral("video_hash"), videoHash));
    }

    if (stream.behaviorHints.videoSize > 0) {
        const QString sz = QString::number(stream.behaviorHints.videoSize);
        extra.append(qMakePair(QStringLiteral("videoSize"), sz));
        extra.append(qMakePair(QStringLiteral("video_size"), sz));
    }

    QString fileName = stream.behaviorHints.filename.trimmed();
    if (fileName.isEmpty()) {
        fileName = stream.source.fileNameHint.trimmed();
    }
    if (fileName.isEmpty()) {
        fileName = stream.name.trimmed();
    }
    if (!fileName.isEmpty()) {
        extra.append(qMakePair(QStringLiteral("filename"), fileName));
    }

    return extra;
}

QList<SubtitleTrack> parseSubtitleArray(const QJsonArray& subtitles)
{
    QList<SubtitleTrack> out;

    for (const QJsonValue& value : subtitles) {
        const QJsonObject obj = value.toObject();

        const QUrl url(obj.value(QStringLiteral("url")).toString().trimmed());
        if (!url.isValid() || url.scheme().isEmpty()) {
            continue;
        }

        SubtitleTrack t;
        t.url = url;
        t.id = obj.value(QStringLiteral("id")).toString().trimmed();
        t.lang = obj.value(QStringLiteral("lang")).toString().trimmed();
        if (t.lang.isEmpty()) {
            t.lang = obj.value(QStringLiteral("language")).toString().trimmed();
        }
        t.label = obj.value(QStringLiteral("label")).toString().trimmed();

        if (t.id.isEmpty()) {
            t.id = t.url.toString(QUrl::FullyEncoded);
        }
        if (t.lang.isEmpty()) {
            t.lang = QStringLiteral("und");
        }

        out.push_back(t);
    }

    return out;
}

} // namespace

struct SubtitleLoadRequest {
    QString type;
    QString id;
    Stream selectedStream;
};

class SubtitlesAggregator : public QObject {
    Q_OBJECT

public:
    explicit SubtitlesAggregator(AddonRegistry* registry, QObject* parent = nullptr)
        : QObject(parent)
        , m_registry(registry)
    {
    }

    void load(const SubtitleLoadRequest& request)
    {
        resetTransientState();
        m_request = request;
        m_requestExtra = buildSubtitleExtra(request.selectedStream);

        if (!m_registry || m_request.type.isEmpty() || m_request.id.isEmpty()) {
            emit subtitlesReady({}, {});
            return;
        }

        const QString cacheKey = makeCacheKey(m_request);
        auto cacheIt = m_cache.find(cacheKey);
        if (cacheIt != m_cache.end()) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - cacheIt->timestampMs < kSubtitleCacheTtlMs) {
                emit subtitlesReady(cacheIt->tracks, cacheIt->originByTrackKey);
                return;
            }
            m_cache.erase(cacheIt);
        }

        const QList<AddonDescriptor> addons =
            m_registry->findByResourceType(QStringLiteral("subtitles"), m_request.type);

        if (addons.isEmpty()) {
            emit subtitlesReady({}, {});
            return;
        }

        for (const AddonDescriptor& addon : addons) {
            PendingAddon pending;
            pending.addonId = addon.manifest.id;
            pending.addonName = addon.manifest.name;
            pending.baseUrl = addon.transportUrl;
            m_pendingByAddon.insert(pending.addonId, pending);
        }

        dispatch();
    }

signals:
    // originByTrackKey maps canonicalTrackKey(track) -> addonId.
    void subtitlesReady(const QList<SubtitleTrack>& tracks,
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
        QList<SubtitleTrack> tracks;
        QHash<QString, QString> originByTrackKey;
    };

    static QString makeCacheKey(const SubtitleLoadRequest& request)
    {
        return request.type + QLatin1Char('|')
            + request.id + QLatin1Char('|')
            + request.selectedStream.behaviorHints.videoHash + QLatin1Char('|')
            + QString::number(request.selectedStream.behaviorHints.videoSize) + QLatin1Char('|')
            + request.selectedStream.behaviorHints.filename + QLatin1Char('|')
            + request.selectedStream.source.fileNameHint + QLatin1Char('|')
            + request.selectedStream.name;
    }

    void dispatch()
    {
        for (auto it = m_pendingByAddon.begin(); it != m_pendingByAddon.end(); ++it) {
            PendingAddon& addon = it.value();
            if (addon.inFlight) {
                continue;
            }

            addon.inFlight = true;
            ++m_pendingResponses;

            ResourceRequest req;
            req.resource = QStringLiteral("subtitles");
            req.type = m_request.type;
            req.id = m_request.id;
            req.extra = m_requestExtra;

            auto* worker = new AddonTransport(this);
            auto handled = std::make_shared<bool>(false);
            auto readyConn = std::make_shared<QMetaObject::Connection>();
            auto failConn = std::make_shared<QMetaObject::Connection>();

            *readyConn = connect(worker, &AddonTransport::resourceReady, this,
                [this, req, addonId = addon.addonId, handled, readyConn, failConn, worker](
                    const ResourceRequest& incoming,
                    const QJsonObject& payload) {
                    if (*handled || !sameRequest(req, incoming)) {
                        return;
                    }
                    *handled = true;
                    QObject::disconnect(*readyConn);
                    QObject::disconnect(*failConn);
                    worker->deleteLater();
                    onAddonReady(addonId, payload);
                });

            *failConn = connect(worker, &AddonTransport::resourceFailed, this,
                [this, req, addonId = addon.addonId, handled, readyConn, failConn, worker](
                    const ResourceRequest& incoming,
                    const QString& message) {
                    if (*handled || !sameRequest(req, incoming)) {
                        return;
                    }
                    *handled = true;
                    QObject::disconnect(*readyConn);
                    QObject::disconnect(*failConn);
                    worker->deleteLater();
                    onAddonFailed(addonId, message);
                });

            worker->fetchResource(addon.baseUrl, req);
        }
    }

    void onAddonReady(const QString& addonId, const QJsonObject& payload)
    {
        auto pendingIt = m_pendingByAddon.find(addonId);
        if (pendingIt != m_pendingByAddon.end()) {
            pendingIt->inFlight = false;
        }

        const QJsonArray raw = payload.value(QStringLiteral("subtitles")).toArray();
        const QList<SubtitleTrack> parsed = parseSubtitleArray(raw);

        for (const SubtitleTrack& track : parsed) {
            const QString key = canonicalTrackKey(track);
            if (key.isEmpty() || m_seenTrackKeys.contains(key)) {
                continue;
            }
            m_seenTrackKeys.insert(key);
            m_originByTrackKey.insert(key, addonId);
            m_tracks.push_back(track);
        }

        completeOne();
    }

    void onAddonFailed(const QString& addonId, const QString& message)
    {
        auto pendingIt = m_pendingByAddon.find(addonId);
        if (pendingIt != m_pendingByAddon.end()) {
            pendingIt->inFlight = false;
        }

        emit subtitlesError(addonId, message);
        completeOne();
    }

    void completeOne()
    {
        --m_pendingResponses;
        if (m_pendingResponses > 0) {
            return;
        }

        CacheEntry entry;
        entry.timestampMs = QDateTime::currentMSecsSinceEpoch();
        entry.tracks = m_tracks;
        entry.originByTrackKey = m_originByTrackKey;
        m_cache.insert(makeCacheKey(m_request), entry);

        emit subtitlesReady(m_tracks, m_originByTrackKey);
    }

    void resetTransientState()
    {
        m_request = {};
        m_requestExtra.clear();
        m_pendingByAddon.clear();
        m_pendingResponses = 0;

        m_tracks.clear();
        m_seenTrackKeys.clear();
        m_originByTrackKey.clear();
    }

    AddonRegistry* m_registry = nullptr;

    SubtitleLoadRequest m_request;
    QList<QPair<QString, QString>> m_requestExtra;
    QMap<QString, PendingAddon> m_pendingByAddon;
    int m_pendingResponses = 0;

    QList<SubtitleTrack> m_tracks;
    QSet<QString> m_seenTrackKeys;
    QHash<QString, QString> m_originByTrackKey;

    QHash<QString, CacheEntry> m_cache;
};

} // namespace tankostream::stream

// -----------------------------------------------------------------
// StreamPage wiring sketch (Batch 5.1 scope only)
// -----------------------------------------------------------------
//
// New members in StreamPage:
//   tankostream::stream::SubtitlesAggregator* m_subtitlesAggregator = nullptr;
//   QList<tankostream::addon::SubtitleTrack>  m_externalSubtitleTracks;
//   QHash<QString, QString>                   m_externalSubtitleOrigins;
//
// Construct once in StreamPage ctor, reusing the Phase 2 AddonRegistry:
//   m_subtitlesAggregator =
//       new tankostream::stream::SubtitlesAggregator(m_addonRegistry, this);
//
// In onPlayRequested, after StreamPickerDialog selection is accepted:
//   tankostream::stream::SubtitleLoadRequest subReq;
//   subReq.type = (mediaType == "movie") ? "movie" : "series";
//   subReq.id = req.id; // same stream resource id used for StreamAggregator
//   subReq.selectedStream = selected.stream;
//   m_subtitlesAggregator->load(subReq);
//
// Connect once:
//   connect(m_subtitlesAggregator, &SubtitlesAggregator::subtitlesReady, this,
//       [this](const QList<SubtitleTrack>& tracks,
//              const QHash<QString, QString>& origins) {
//           m_externalSubtitleTracks = tracks;
//           m_externalSubtitleOrigins = origins;
//           // Batch 5.3 subtitle menu merges this with embedded tracks from 5.2.
//       });
//
//   connect(m_subtitlesAggregator, &SubtitlesAggregator::subtitlesError, this,
//       [this](const QString& addonId, const QString& message) {
//           Q_UNUSED(addonId);
//           Q_UNUSED(message);
//           // Non-blocking: subtitle lookup should not block playback start.
//       });
//
// Notes for 5.1 -> 5.2 handoff:
// - Keep SubtitlesAggregator independent of SidecarProcess.
// - Proxy headers are parsed into StreamBehaviorHints by Phase 4 but sidecar
//   forwarding remains a Phase 5.2/5.3 concern.
