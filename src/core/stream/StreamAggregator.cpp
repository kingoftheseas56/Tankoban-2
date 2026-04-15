#include "StreamAggregator.h"

#include <QChar>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

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
using tankostream::addon::StreamBehaviorHints;
using tankostream::addon::StreamSource;
using tankostream::addon::SubtitleTrack;

namespace tankostream::stream {

namespace {

constexpr int kMaxTrackers = 16;

const QStringList kFallbackTrackers = {
    QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
    QStringLiteral("udp://open.stealth.si:80/announce"),
    QStringLiteral("udp://tracker.torrent.eu.org:451/announce"),
    QStringLiteral("udp://tracker.openbittorrent.com:6969/announce"),
    QStringLiteral("udp://open.demonii.com:1337/announce"),
    QStringLiteral("udp://tracker.internetwarriors.net:1337/announce"),
    QStringLiteral("udp://tracker.cyberia.is:6969/announce"),
    QStringLiteral("udp://tracker.moeking.me:6969/announce"),
    QStringLiteral("udp://explodie.org:6969/announce"),
    QStringLiteral("udp://tracker-udp.gbitt.info:80/announce"),
    QStringLiteral("udp://tracker.uw0.xyz:6969/announce"),
    QStringLiteral("udp://tracker.bittor.pw:1337/announce"),
};

const QRegularExpression kHashRe(QStringLiteral("^[a-f0-9]{40}$"));
const QRegularExpression kResRe(QStringLiteral("\\b(2160p|1080p|720p|480p|4[Kk])\\b"));
const QRegularExpression kSrcRe(
    QStringLiteral("\\b(WEB[-\\s]?DL|WEBRip|BluRay|BDRip|BRRip|HDRip|HDTV|DVDRip|PDTV|AMZN|NF)\\b"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kHdrRe(
    QStringLiteral("\\b(Dolby\\s*Vision|DV|HDR10\\+?|HDR|SDR|10[Bb]it)\\b"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kCodecRe(
    QStringLiteral("\\b(H\\.?265|x265|HEVC|H\\.?264|x264|AV1|VP9)\\b"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kAudioRe(
    QStringLiteral("\\b(Atmos|DDP?\\s*5\\.1|TrueHD|DTS[-\\s]?HD|AAC|FLAC|AC3)\\b"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kSeedersRe(QStringLiteral("\\x{1F464}\\s*(\\d+)"));
const QRegularExpression kSizeRe(
    QStringLiteral("\\x{1F4BE}\\s*([\\d.,]+\\s*[KMGT]?i?B)"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kTrackerRe(
    QStringLiteral("\\x{2699}\\x{FE0F}?\\s*(.+)$"),
    QRegularExpression::MultilineOption);
const QRegularExpression kMagnetTrackerRe(
    QStringLiteral("^(tracker|dht):"),
    QRegularExpression::CaseInsensitiveOption);

const QChar kBustHi = QChar(0xD83D);
const QChar kBustLo = QChar(0xDC64);
const QChar kDiskHi = QChar(0xD83D);
const QChar kDiskLo = QChar(0xDCBE);
const QChar kGear = QChar(0x2699);
const QChar kFlagHi = QChar(0xD83C);
constexpr uint kFlagLoMin = 0xDDE0;
constexpr uint kFlagLoMax = 0xDDFF;

bool sameRequest(const ResourceRequest& a, const ResourceRequest& b)
{
    return a.resource == b.resource
        && a.type == b.type
        && a.id == b.id
        && a.extra == b.extra;
}

bool containsBust(const QString& s)
{
    for (int i = 0; i < s.size() - 1; ++i) {
        if (s[i] == kBustHi && s[i + 1] == kBustLo) {
            return true;
        }
    }
    return false;
}

bool containsDisk(const QString& s)
{
    for (int i = 0; i < s.size() - 1; ++i) {
        if (s[i] == kDiskHi && s[i + 1] == kDiskLo) {
            return true;
        }
    }
    return false;
}

bool containsGear(const QString& s)
{
    return s.contains(kGear);
}

bool containsFlag(const QString& s)
{
    for (int i = 0; i < s.size() - 1; ++i) {
        if (s[i] == kFlagHi) {
            const uint lo = s[i + 1].unicode();
            if (lo >= kFlagLoMin && lo <= kFlagLoMax) {
                return true;
            }
        }
    }
    return false;
}

bool looksLikeFilename(const QString& s)
{
    return s.contains('.') && s.length() <= 300 && !s.startsWith(QStringLiteral("http"));
}

QString extractFilename(const QString& path)
{
    const int slash = path.lastIndexOf('/');
    const int backslash = path.lastIndexOf('\\');
    const int pos = qMax(slash, backslash);
    return pos >= 0 ? path.mid(pos + 1) : path;
}

qint64 parseSize(const QString& sizeStr)
{
    const QString normalized = sizeStr.trimmed().toUpper().replace(',', '.');
    static const QRegularExpression kNumRe(QStringLiteral("^([\\d.]+)\\s*([KMGT]?I?B?)$"));
    const auto m = kNumRe.match(normalized);
    if (!m.hasMatch()) {
        return 0;
    }
    bool ok = false;
    double value = m.captured(1).toDouble(&ok);
    if (!ok || value < 0) {
        return 0;
    }
    const QString unit = m.captured(2);
    if (unit.startsWith('T')) {
        value *= 1099511627776.0;
    } else if (unit.startsWith('G')) {
        value *= 1073741824.0;
    } else if (unit.startsWith('M')) {
        value *= 1048576.0;
    } else if (unit.startsWith('K')) {
        value *= 1024.0;
    }
    return static_cast<qint64>(value);
}

QString parseQuality(const QString& rawTitle)
{
    QStringList tags;
    auto tryMatch = [&](const QRegularExpression& re) {
        const auto m = re.match(rawTitle);
        if (m.hasMatch()) {
            tags.append(m.captured(1));
        }
    };
    tryMatch(kResRe);
    tryMatch(kSrcRe);
    tryMatch(kHdrRe);
    tryMatch(kCodecRe);
    tryMatch(kAudioRe);
    return tags.join(QStringLiteral(" / "));
}

QString stripTrackerPrefix(QString source)
{
    source = source.trimmed();
    source.remove(kMagnetTrackerRe);
    return source.trimmed();
}

void parseBehaviorHints(const QJsonObject& obj, StreamBehaviorHints& out)
{
    out.notWebReady = obj.value(QStringLiteral("notWebReady")).toBool(false);
    out.bingeGroup = obj.value(QStringLiteral("bingeGroup")).toString().trimmed();
    out.filename = obj.value(QStringLiteral("filename")).toString().trimmed();
    out.videoHash = obj.value(QStringLiteral("videoHash")).toString().trimmed();
    if (obj.contains(QStringLiteral("videoSize"))) {
        out.videoSize = static_cast<qint64>(
            obj.value(QStringLiteral("videoSize")).toDouble(0.0));
    }

    for (const QJsonValue& value : obj.value(QStringLiteral("countryWhitelist")).toArray()) {
        const QString country = value.toString().trimmed();
        if (!country.isEmpty()) {
            out.countryWhitelist.append(country);
        }
    }

    const QJsonObject proxyHeaders = obj.value(QStringLiteral("proxyHeaders")).toObject();
    const QJsonObject requestHeaders = proxyHeaders.value(QStringLiteral("request")).toObject();
    for (auto it = requestHeaders.constBegin(); it != requestHeaders.constEnd(); ++it) {
        out.proxyRequestHeaders.insert(it.key(), it.value().toString());
    }
    const QJsonObject responseHeaders = proxyHeaders.value(QStringLiteral("response")).toObject();
    for (auto it = responseHeaders.constBegin(); it != responseHeaders.constEnd(); ++it) {
        out.proxyResponseHeaders.insert(it.key(), it.value().toString());
    }

    static const QSet<QString> kKnown {
        QStringLiteral("notWebReady"),
        QStringLiteral("bingeGroup"),
        QStringLiteral("countryWhitelist"),
        QStringLiteral("proxyHeaders"),
        QStringLiteral("filename"),
        QStringLiteral("videoHash"),
        QStringLiteral("videoSize"),
    };
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (!kKnown.contains(it.key())) {
            out.other.insert(it.key(), it.value().toVariant());
        }
    }
}

QList<SubtitleTrack> parseSubtitles(const QJsonArray& subtitlesArray)
{
    QList<SubtitleTrack> tracks;
    for (const QJsonValue& value : subtitlesArray) {
        const QJsonObject subtitleObj = value.toObject();
        const QUrl subtitleUrl(subtitleObj.value(QStringLiteral("url")).toString().trimmed());
        if (!subtitleUrl.isValid()) {
            continue;
        }
        SubtitleTrack track;
        track.url = subtitleUrl;
        track.id = subtitleObj.value(QStringLiteral("id")).toString().trimmed();
        if (track.id.isEmpty()) {
            track.id = subtitleUrl.toString();
        }
        track.lang = subtitleObj.value(QStringLiteral("lang")).toString().trimmed();
        if (track.lang.isEmpty()) {
            track.lang = subtitleObj.value(QStringLiteral("language")).toString().trimmed();
        }
        track.label = subtitleObj.value(QStringLiteral("label")).toString().trimmed();
        if (track.label.isEmpty()) {
            track.label = subtitleObj.value(QStringLiteral("title")).toString().trimmed();
        }
        tracks.append(track);
    }
    return tracks;
}

QStringList parseTrackerSources(const QJsonArray& sourcesArray)
{
    QStringList trackers;
    for (const QJsonValue& value : sourcesArray) {
        const QString source = value.toString().trimmed();
        if (source.isEmpty()) {
            continue;
        }
        if (!source.startsWith(QStringLiteral("tracker:"), Qt::CaseInsensitive)
            && !source.startsWith(QStringLiteral("dht:"), Qt::CaseInsensitive)) {
            continue;
        }
        const QString tracker = stripTrackerPrefix(source);
        if (tracker.isEmpty()) {
            continue;
        }
        if (trackers.contains(tracker, Qt::CaseInsensitive)) {
            continue;
        }
        trackers.append(tracker);
        if (trackers.size() >= kMaxTrackers) {
            break;
        }
    }
    return trackers;
}

bool parseStreamSource(const QJsonObject& streamObj, StreamSource& sourceOut)
{
    const QString infoHash = streamObj.value(QStringLiteral("infoHash"))
                                 .toString().trimmed().toLower();
    if (!infoHash.isEmpty() && kHashRe.match(infoHash).hasMatch()) {
        int fileIdx = -1;
        if (streamObj.contains(QStringLiteral("fileIdx"))) {
            fileIdx = streamObj.value(QStringLiteral("fileIdx")).toInt(-1);
        } else if (streamObj.contains(QStringLiteral("fileIndex"))) {
            fileIdx = streamObj.value(QStringLiteral("fileIndex")).toInt(-1);
        }
        sourceOut = StreamSource::magnetSource(
            infoHash,
            parseTrackerSources(streamObj.value(QStringLiteral("sources")).toArray()),
            fileIdx);
        return true;
    }

    const QString ytId = streamObj.value(QStringLiteral("ytId")).toString().trimmed();
    if (!ytId.isEmpty()) {
        sourceOut = StreamSource::youtubeSource(ytId);
        return true;
    }

    QString urlValue = streamObj.value(QStringLiteral("url")).toString().trimmed();
    if (urlValue.isEmpty()) {
        urlValue = streamObj.value(QStringLiteral("externalUrl")).toString().trimmed();
    }
    if (urlValue.isEmpty()) {
        urlValue = streamObj.value(QStringLiteral("playerFrameUrl")).toString().trimmed();
    }
    const QUrl url(urlValue);
    if (!url.isValid() || url.scheme().isEmpty()) {
        return false;
    }

    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) {
        sourceOut = StreamSource::httpSource(url);
    } else {
        sourceOut = StreamSource::urlSource(url);
    }
    return true;
}

void enrichTorrentioLikeFields(Stream& stream, const QJsonObject& streamObj)
{
    if (stream.source.kind != StreamSource::Kind::Magnet) {
        return;
    }
    if (stream.behaviorHints.bingeGroup.isEmpty()) {
        return;
    }

    const QString rawTitle = streamObj.value(QStringLiteral("title")).toString();
    if (rawTitle.isEmpty()) {
        return;
    }

    const QStringList lines = rawTitle.split('\n');
    QString trackerSource;
    QString tracker;
    QString languages;
    QString fileHintFromTitle;
    int seeders = 0;
    qint64 sizeBytes = 0;

    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (i == 0) {
            trackerSource = line;
            continue;
        }
        if (containsBust(line)) {
            const auto m = kSeedersRe.match(line);
            if (m.hasMatch()) {
                seeders = m.captured(1).toInt();
            }
        }
        if (containsDisk(line)) {
            const auto m = kSizeRe.match(line);
            if (m.hasMatch()) {
                sizeBytes = parseSize(m.captured(1));
            }
        }
        if (containsGear(line)) {
            const auto m = kTrackerRe.match(line);
            if (m.hasMatch()) {
                tracker = m.captured(1).trimmed();
            }
        }
        if (containsFlag(line)) {
            languages = line;
        }
        if (fileHintFromTitle.isEmpty() && looksLikeFilename(line)) {
            fileHintFromTitle = extractFilename(line);
        }
    }

    const QString quality = parseQuality(rawTitle);
    if (!quality.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("qualityLabel"), quality);
    }
    if (!trackerSource.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("trackerSource"), trackerSource);
    }
    if (!tracker.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("tracker"), tracker);
    }
    if (!languages.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("languages"), languages);
    }
    if (seeders > 0) {
        stream.behaviorHints.other.insert(QStringLiteral("seeders"), seeders);
    }
    if (sizeBytes > 0) {
        stream.behaviorHints.other.insert(QStringLiteral("sizeBytes"),
                                          QVariant::fromValue<qint64>(sizeBytes));
    }

    if (!fileHintFromTitle.isEmpty() && stream.source.fileNameHint.isEmpty()) {
        stream.source.fileNameHint = fileHintFromTitle;
    }
    if (!stream.behaviorHints.filename.isEmpty()) {
        stream.source.fileNameHint = stream.behaviorHints.filename;
    }

    // Stream-picker UX rework — preserve the parsed filename as a UI-readable
    // field. Stremio-style source cards render this as the primary line under
    // the addon name; without it the table showed "Torrentio..." for every
    // Torrentio row because neither stream.name nor stream.description carry
    // anything useful per Torrentio payload. Mirror into behaviorHints.filename
    // when empty so Stremio-compliant addons that populate `filename` directly
    // and Torrentio-style addons that embed it in `title` look identical to
    // the card layer.
    if (!fileHintFromTitle.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("parsedFilename"),
                                          fileHintFromTitle);
        if (stream.behaviorHints.filename.isEmpty()) {
            stream.behaviorHints.filename = fileHintFromTitle;
        }
    }

    if (stream.source.trackers.isEmpty()) {
        stream.source.trackers = kFallbackTrackers.mid(0, kMaxTrackers);
    }
}

QString streamIdentityKey(const Stream& stream)
{
    switch (stream.source.kind) {
    case StreamSource::Kind::Magnet:
        return QStringLiteral("magnet|%1|%2|%3")
            .arg(stream.source.infoHash.toLower())
            .arg(stream.source.fileIndex)
            .arg(stream.source.fileNameHint);
    case StreamSource::Kind::Http:
        return QStringLiteral("http|%1").arg(stream.source.url.toString(QUrl::FullyEncoded));
    case StreamSource::Kind::Url:
        return QStringLiteral("url|%1").arg(stream.source.url.toString(QUrl::FullyEncoded));
    case StreamSource::Kind::YouTube:
        return QStringLiteral("yt|%1").arg(stream.source.youtubeId);
    }
    return {};
}

bool parseStreamRow(const QJsonObject& streamObj, Stream& out)
{
    StreamSource source;
    if (!parseStreamSource(streamObj, source)) {
        return false;
    }
    out.source = source;
    out.name = streamObj.value(QStringLiteral("name")).toString().trimmed();
    out.description = streamObj.value(QStringLiteral("description")).toString().trimmed();
    if (out.description.isEmpty()) {
        out.description = streamObj.value(QStringLiteral("title")).toString().trimmed();
    }
    out.thumbnail = QUrl(streamObj.value(QStringLiteral("thumbnail")).toString().trimmed());
    out.subtitles = parseSubtitles(streamObj.value(QStringLiteral("subtitles")).toArray());
    parseBehaviorHints(streamObj.value(QStringLiteral("behaviorHints")).toObject(),
                       out.behaviorHints);

    if (out.name.isEmpty()) {
        out.name = out.description;
    }
    return true;
}

}

StreamAggregator::StreamAggregator(AddonRegistry* registry, QObject* parent)
    : QObject(parent)
    , m_registry(registry)
{
}

void StreamAggregator::load(const StreamLoadRequest& request)
{
    reset();
    m_request = request;

    if (!m_registry || request.type.isEmpty() || request.id.isEmpty()) {
        emit streamsReady({}, {});
        return;
    }

    const QList<AddonDescriptor> addons =
        m_registry->findByResourceType(QStringLiteral("stream"), request.type);

    if (addons.isEmpty()) {
        emit streamsReady({}, {});
        return;
    }

    for (const AddonDescriptor& addon : addons) {
        PendingAddon pending;
        pending.addonId = addon.manifest.id;
        pending.addonName = addon.manifest.name;
        pending.transportUrl = addon.transportUrl;
        m_pendingByAddon.insert(pending.addonId, pending);
        m_addonsById.insert(pending.addonId, pending.addonName);
    }

    dispatchRequests();
}

void StreamAggregator::dispatchRequests()
{
    for (auto it = m_pendingByAddon.begin(); it != m_pendingByAddon.end(); ++it) {
        PendingAddon& addon = it.value();
        if (addon.inFlight) {
            continue;
        }
        addon.inFlight = true;
        ++m_pendingResponses;

        ResourceRequest req;
        req.resource = QStringLiteral("stream");
        req.type = m_request.type;
        req.id = m_request.id;
        req.extra = m_request.extra;

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

        worker->fetchResource(addon.transportUrl, req);
    }
}

void StreamAggregator::onAddonReady(const QString& addonId, const QJsonObject& payload)
{
    auto addonIt = m_pendingByAddon.find(addonId);
    if (addonIt == m_pendingByAddon.end()) {
        completeOne();
        return;
    }
    addonIt->inFlight = false;

    const QString addonName = addonIt->addonName;
    const QJsonArray streamsArray = payload.value(QStringLiteral("streams")).toArray();
    for (const QJsonValue& streamValue : streamsArray) {
        const QJsonObject streamObj = streamValue.toObject();
        Stream parsed;
        if (!parseStreamRow(streamObj, parsed)) {
            continue;
        }

        enrichTorrentioLikeFields(parsed, streamObj);
        parsed.behaviorHints.other.insert(QStringLiteral("originAddonId"), addonId);
        parsed.behaviorHints.other.insert(QStringLiteral("originAddonName"), addonName);

        const QString identity = streamIdentityKey(parsed);
        if (identity.isEmpty()) {
            continue;
        }
        if (m_seenIdentityKeys.contains(identity)) {
            continue;
        }
        m_seenIdentityKeys.insert(identity);
        m_streams.append(parsed);
    }

    completeOne();
}

void StreamAggregator::onAddonFailed(const QString& addonId, const QString& message)
{
    auto addonIt = m_pendingByAddon.find(addonId);
    if (addonIt != m_pendingByAddon.end()) {
        addonIt->inFlight = false;
    }
    emit streamError(addonId, message);
    completeOne();
}

void StreamAggregator::completeOne()
{
    --m_pendingResponses;
    if (m_pendingResponses > 0) {
        return;
    }
    emit streamsReady(m_streams, m_addonsById);
}

void StreamAggregator::reset()
{
    m_request = {};
    m_pendingByAddon.clear();
    m_addonsById.clear();
    m_seenIdentityKeys.clear();
    m_streams.clear();
    m_pendingResponses = 0;
}

}
