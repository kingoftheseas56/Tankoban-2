#include "SubtitlesAggregator.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <memory>

#include "addon/AddonRegistry.h"
#include "addon/AddonTransport.h"
#include "addon/Descriptor.h"
#include "addon/ResourcePath.h"

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::AddonTransport;
using tankostream::addon::ResourceRequest;
using tankostream::addon::Stream;
using tankostream::addon::SubtitleTrack;

namespace tankostream::stream {

namespace {

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
        // Dual-spelling: addons in the wild accept both snake and camel.
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
        if (t.label.isEmpty()) {
            t.label = obj.value(QStringLiteral("title")).toString().trimmed();
        }

        if (t.id.isEmpty()) {
            t.id = t.url.toString(QUrl::FullyEncoded);
        }
        if (t.lang.isEmpty()) {
            t.lang = QStringLiteral("und");
        }

        out.append(t);
    }

    return out;
}

}

SubtitlesAggregator::SubtitlesAggregator(AddonRegistry* registry, QObject* parent)
    : QObject(parent)
    , m_registry(registry)
{
}

void SubtitlesAggregator::load(const SubtitleLoadRequest& request)
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

QString SubtitlesAggregator::makeCacheKey(const SubtitleLoadRequest& request)
{
    return request.type + QLatin1Char('|')
        + request.id + QLatin1Char('|')
        + request.selectedStream.behaviorHints.videoHash + QLatin1Char('|')
        + QString::number(request.selectedStream.behaviorHints.videoSize) + QLatin1Char('|')
        + request.selectedStream.behaviorHints.filename + QLatin1Char('|')
        + request.selectedStream.source.fileNameHint + QLatin1Char('|')
        + request.selectedStream.name;
}

void SubtitlesAggregator::dispatch()
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
        const QString addonId = addon.addonId;
        auto handled = std::make_shared<bool>(false);
        auto readyConn = std::make_shared<QMetaObject::Connection>();
        auto failConn = std::make_shared<QMetaObject::Connection>();

        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
            [this, req, addonId, handled, readyConn, failConn, worker](
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
            [this, req, addonId, handled, readyConn, failConn, worker](
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

void SubtitlesAggregator::onAddonReady(const QString& addonId, const QJsonObject& payload)
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
        m_tracks.append(track);
    }

    completeOne();
}

void SubtitlesAggregator::onAddonFailed(const QString& addonId, const QString& message)
{
    auto pendingIt = m_pendingByAddon.find(addonId);
    if (pendingIt != m_pendingByAddon.end()) {
        pendingIt->inFlight = false;
    }

    emit subtitlesError(addonId, message);
    completeOne();
}

void SubtitlesAggregator::completeOne()
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

void SubtitlesAggregator::resetTransientState()
{
    m_request = {};
    m_requestExtra.clear();
    m_pendingByAddon.clear();
    m_pendingResponses = 0;

    m_tracks.clear();
    m_seenTrackKeys.clear();
    m_originByTrackKey.clear();
}

}
