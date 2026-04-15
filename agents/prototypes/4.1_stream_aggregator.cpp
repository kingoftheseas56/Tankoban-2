// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 4.1 (Multi-Source Stream Aggregation)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:178
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:182
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:183
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonRegistry.h:24
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonTransport.h:22
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonTransport.h:27
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/StreamSource.h:10
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/StreamSource.h:44
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/StreamInfo.h:15
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/StreamInfo.h:30
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/TorrentioClient.cpp:171
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/TorrentioClient.cpp:203
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/resource/stream.rs:994
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/resource/stream.rs:1076
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/addon_transport/http_transport/http_transport.rs:57
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/query_params_encode.rs:5
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 4.1.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QRegularExpression>
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

    static StreamSource urlSource(const QUrl& value)
    {
        StreamSource out;
        out.kind = Kind::Url;
        out.url = value;
        return out;
    }

    static StreamSource httpSource(const QUrl& value)
    {
        StreamSource out;
        out.kind = Kind::Http;
        out.url = value;
        return out;
    }

    static StreamSource magnetSource(const QString& hash,
                                     const QStringList& trackerList = {},
                                     int selectedFileIndex = -1,
                                     const QString& hint = {})
    {
        StreamSource out;
        out.kind = Kind::Magnet;
        out.infoHash = hash;
        out.trackers = trackerList;
        out.fileIndex = selectedFileIndex;
        out.fileNameHint = hint;
        return out;
    }

    static StreamSource youtubeSource(const QString& ytId)
    {
        StreamSource out;
        out.kind = Kind::YouTube;
        out.youtubeId = ytId;
        return out;
    }
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

    QList<AddonDescriptor> list() const;
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
using tankostream::addon::StreamBehaviorHints;
using tankostream::addon::StreamSource;
using tankostream::addon::SubtitleTrack;

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
            tags.push_back(m.captured(1));
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
        out.videoSize = static_cast<qint64>(obj.value(QStringLiteral("videoSize")).toDouble(0.0));
    }

    for (const QJsonValue& value : obj.value(QStringLiteral("countryWhitelist")).toArray()) {
        const QString country = value.toString().trimmed();
        if (!country.isEmpty()) {
            out.countryWhitelist.push_back(country);
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
        tracks.push_back(track);
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
        trackers.push_back(tracker);
        if (trackers.size() >= kMaxTrackers) {
            break;
        }
    }
    return trackers;
}

bool parseStreamSource(const QJsonObject& streamObj, StreamSource& sourceOut)
{
    const QString infoHash = streamObj.value(QStringLiteral("infoHash")).toString().trimmed().toLower();
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
        stream.behaviorHints.other.insert(QStringLiteral("sizeBytes"), sizeBytes);
    }

    if (!fileHintFromTitle.isEmpty() && stream.source.fileNameHint.isEmpty()) {
        stream.source.fileNameHint = fileHintFromTitle;
    }
    if (!stream.behaviorHints.filename.isEmpty()) {
        stream.source.fileNameHint = stream.behaviorHints.filename;
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

} // namespace

struct StreamLoadRequest {
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;
};

class StreamAggregator : public QObject {
    Q_OBJECT

public:
    explicit StreamAggregator(AddonRegistry* registry, QObject* parent = nullptr)
        : QObject(parent)
        , m_registry(registry)
    {
    }

    void load(const StreamLoadRequest& request)
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

signals:
    // addonsById maps addonId -> human-readable addon name.
    // Each Stream also carries originAddonId/originAddonName in behaviorHints.other.
    void streamsReady(const QList<Stream>& streams,
                      const QHash<QString, QString>& addonsById);
    void streamError(const QString& addonId, const QString& message);

private:
    struct PendingAddon {
        QString addonId;
        QString addonName;
        QUrl transportUrl;
        bool inFlight = false;
    };

    void dispatchRequests()
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
                    disconnect(*readyConn);
                    disconnect(*failConn);
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
                    disconnect(*readyConn);
                    disconnect(*failConn);
                    worker->deleteLater();
                    onAddonFailed(addonId, message);
                });

            worker->fetchResource(addon.transportUrl, req);
        }
    }

    void onAddonReady(const QString& addonId, const QJsonObject& payload)
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
            m_streams.push_back(parsed);
        }

        completeOne();
    }

    void onAddonFailed(const QString& addonId, const QString& message)
    {
        auto addonIt = m_pendingByAddon.find(addonId);
        if (addonIt != m_pendingByAddon.end()) {
            addonIt->inFlight = false;
        }
        emit streamError(addonId, message);
        completeOne();
    }

    void completeOne()
    {
        --m_pendingResponses;
        if (m_pendingResponses > 0) {
            return;
        }
        emit streamsReady(m_streams, m_addonsById);
    }

    void reset()
    {
        m_request = {};
        m_pendingByAddon.clear();
        m_addonsById.clear();
        m_seenIdentityKeys.clear();
        m_streams.clear();
        m_pendingResponses = 0;
    }

    AddonRegistry* m_registry = nullptr;

    StreamLoadRequest m_request;
    QMap<QString, PendingAddon> m_pendingByAddon;
    QHash<QString, QString> m_addonsById;
    QSet<QString> m_seenIdentityKeys;
    QList<Stream> m_streams;
    int m_pendingResponses = 0;
};

} // namespace tankostream::stream

// -----------------------------------------------------------------
// StreamPage integration sketch (Batch 4.1 scope only)
// -----------------------------------------------------------------
//
// 1) Add core member:
//      StreamAggregator* m_streamAggregator = nullptr;
//
// 2) Construct in StreamPage ctor with the shared Phase-2 registry:
//      m_streamAggregator = new StreamAggregator(m_addonRegistry, this);
//
// 3) Replace TorrentioClient call in onPlayRequested:
//      StreamLoadRequest req;
//      req.type = mediaType == "movie" ? "movie" : "series";
//      req.id = mediaType == "movie"
//          ? imdbId
//          : imdbId + ":" + QString::number(season) + ":" + QString::number(episode);
//      m_streamAggregator->load(req);
//
// 4) Adapt picker-input mapping:
//      Stream rows carry originAddonId/originAddonName in behaviorHints.other.
//      Torrentio-like rows also carry quality/seeders/size metadata in other.
//
