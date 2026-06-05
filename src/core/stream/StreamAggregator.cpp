#include "StreamAggregator.h"

#include <QChar>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QTimer>

#include <memory>

#include "addon/AddonRegistry.h"
#include "addon/AddonTransport.h"
#include "addon/Descriptor.h"
#include "addon/ResourcePath.h"
#include "core/stream/AnimeCatalogResolver.h"
#include "core/net/NetSeam.h"

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B1) - includes for the
// indexer fan-out wrapped by searchPacks. Mirrors the set used by
// TorrentPackPicker::launchSearches.
#include "core/TorrentIndexer.h"
#include "core/indexers/NyaaIndexer.h"
#include "core/indexers/EztvIndexer.h"
#include "core/indexers/ExtTorrentsIndexer.h"
#include "core/indexers/PirateBayIndexer.h"
#include "core/indexers/X1337xIndexer.h"
#include "core/indexers/YtsIndexer.h"

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

// Only query a stream addon for the id-prefixes it declares it handles (Stremio
// addon semantics). An addon that declares no stream idPrefixes handles every id
// (backward-compat). Without this gate, a type-only match queries the Amatsu
// anime gateway (kitsu/anilist) for non-anime tt ids, where its slow
// tt->AniList->Nyaa lookup (~17s) trips the 10s request timeout and stalls the
// whole "Resolving sources" step — Hemanth-reported 2026-06-05 (Star Wars: Maul).
bool addonHandlesStreamId(const AddonDescriptor& addon, const QString& id)
{
    for (const auto& res : addon.manifest.resources) {
        if (res.name != QStringLiteral("stream"))
            continue;
        if (!res.hasIdPrefixes || res.idPrefixes.isEmpty())
            return true;  // declares no prefixes -> handles all
        for (const QString& pfx : res.idPrefixes)
            if (!pfx.isEmpty() && id.startsWith(pfx))
                return true;
        return false;  // declares prefixes, none match this id
    }
    return true;  // no detailed stream resource entry -> do not exclude
}

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
// Text fallback for addons that write "<n> Seeds"/"<n> Seeders" (e.g. Amatsu)
// rather than Torrentio's 👤-prefixed count.
const QRegularExpression kSeedersTextRe(
    QStringLiteral("(\\d+)\\s*[Ss]eed"),
    QRegularExpression::CaseInsensitiveOption);
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

    // Torrentio packs everything (filename, 👤 seeders, 💾 size, ⚙ tracker) into a
    // multi-line `title`. Newer Stremio addons (e.g. Amatsu, the Nyaa anime gateway)
    // leave `title` empty and split the same data across `name` + `description`.
    // Fall back to name+description so their magnet streams get enriched too —
    // without this, seeders/size/quality stay unset and AutoSourcePicker rejects
    // every Amatsu candidate as "No 1080p source found".
    QString rawTitle = streamObj.value(QStringLiteral("title")).toString();
    if (rawTitle.isEmpty()) {
        const QString nm = streamObj.value(QStringLiteral("name")).toString();
        const QString desc = streamObj.value(QStringLiteral("description")).toString();
        rawTitle = nm.isEmpty() ? desc
                 : desc.isEmpty() ? nm
                 : nm + QLatin1Char('\n') + desc;
    }
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
        // Amatsu uses 👥 (two-bust, U+1F465) + the literal "Seeds" word instead of
        // Torrentio's 👤 (single-bust, U+1F464). Fall back to a text match so its
        // seeder counts are read. Guarded on seeders==0 so Torrentio's emoji path
        // (no "Seeds" word) keeps priority and never double-parses.
        if (seeders == 0) {
            const auto m = kSeedersTextRe.match(line);
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

quint64 StreamAggregator::load(const StreamLoadRequest& request)
{
    reset();
    // DOWNLOAD BUG 2026-06-02 — stamp the new generation immediately after the
    // reset() that zeroed mid-flight state. The returned token lets the caller
    // gate its one-shot streamsReady handler against currentLoadToken() so a
    // late emit from a SUPERSEDED load() (rapid re-clicks) is discarded.
    ++m_loadGeneration;
    m_request = request;

    if (!m_registry || request.type.isEmpty() || request.id.isEmpty()) {
        emitStreamsReadyDeferred(m_loadGeneration, {}, {});  // review fix — never emit synchronously
        return m_loadGeneration;
    }

    QList<AddonDescriptor> addons =
        m_registry->findByResourceType(QStringLiteral("stream"), request.type);

    // Gate by declared id-prefix so e.g. the Amatsu anime gateway is only queried
    // for kitsu/anilist ids, never for non-anime tt ids (where its slow lookup
    // tripped the timeout and stalled source resolution).
    addons.erase(std::remove_if(addons.begin(), addons.end(),
                     [&](const AddonDescriptor& a) {
                         return !addonHandlesStreamId(a, request.id);
                     }),
                 addons.end());

    if (addons.isEmpty()) {
        emitStreamsReadyDeferred(m_loadGeneration, {}, {});  // review fix — never emit synchronously
        return m_loadGeneration;
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
    return m_loadGeneration;
}

void StreamAggregator::emitStreamsReadyDeferred(quint64 generation,
                                                QList<Stream> streams,
                                                QHash<QString, QString> addonsById)
{
    // See the header for why this is always queued rather than emitted
    // synchronously. The queued lambda is bound to `this` as its context object,
    // so Qt discards it automatically if the aggregator is destroyed before it
    // runs (no use-after-free).
    QMetaObject::invokeMethod(this,
        [this, generation, streams = std::move(streams),
         addonsById = std::move(addonsById)]() {
            if (generation != m_loadGeneration)
                return;  // superseded by a newer load() — drop the stale emit
            emit streamsReady(streams, addonsById);
        },
        Qt::QueuedConnection);
}

void StreamAggregator::dispatchRequests()
{
    // DOWNLOAD BUG 2026-06-03 (review fix) — capture the generation this dispatch
    // belongs to. A reply from a worker launched by a SUPERSEDED load() (rapid
    // re-clicks) must be dropped at the source: without this, a stale reply with
    // the same request shape lands in the CURRENT generation's m_streams and
    // decrements m_pendingResponses, corrupting accumulation and firing an early
    // streamsReady that the handler-side token gate would then wrongly accept
    // (the current generation IS still active). Correlating here closes the gap
    // that gating only at the handler left open. Mirrors searchPacks()'s epoch
    // suppression already in this file.
    const quint64 gen = m_loadGeneration;

    // DOWNLOAD BUG 2026-06-03 (review fix) — count EVERY addon we are about to
    // dispatch BEFORE firing any request. AddonTransport::fetchResource() can
    // emit resourceFailed SYNCHRONOUSLY (invalid URL from a persisted addon), so
    // incrementing m_pendingResponses one-at-a-time inside the dispatch loop let
    // an early addon's synchronous failure drive the counter to 0 before later
    // addons were dispatched — firing a premature streamsReady (empty), and then
    // a SECOND one when the rest completed. Pre-counting makes the terminal emit
    // fire exactly once, after the last completion. Iterating a separate id list
    // (not the map) also avoids any mutate-during-iteration hazard if a
    // synchronous reply touches m_pendingByAddon.
    QList<QString> toDispatch;
    for (auto it = m_pendingByAddon.begin(); it != m_pendingByAddon.end(); ++it) {
        if (it.value().inFlight) continue;
        it.value().inFlight = true;
        toDispatch.append(it.key());
    }
    if (toDispatch.isEmpty()) return;
    m_pendingResponses += toDispatch.size();

    for (const QString& addonId : toDispatch) {
        auto addonIt = m_pendingByAddon.find(addonId);
        if (addonIt == m_pendingByAddon.end()) continue;  // defensive
        PendingAddon& addon = addonIt.value();

        ResourceRequest req;
        req.resource = QStringLiteral("stream");
        req.type = m_request.type;
        req.id = m_request.id;
        req.extra = m_request.extra;

        auto* worker = new AddonTransport(this);
        auto handled = std::make_shared<bool>(false);
        auto readyConn = std::make_shared<QMetaObject::Connection>();
        auto failConn = std::make_shared<QMetaObject::Connection>();

        // Drop a stale-generation reply (and reap its worker) before it can touch
        // current state; otherwise apply the existing same-request guard.
        auto dropIfStale = [this, gen, handled, readyConn, failConn, worker]() -> bool {
            if (gen == m_loadGeneration)
                return false;
            *handled = true;
            QObject::disconnect(*readyConn);
            QObject::disconnect(*failConn);
            worker->deleteLater();
            return true;
        };

        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
            [this, req, addonId, handled, readyConn, failConn, worker, dropIfStale](
                const ResourceRequest& incoming,
                const QJsonObject& payload) {
                if (*handled) return;
                if (dropIfStale()) return;
                if (!sameRequest(req, incoming)) return;
                *handled = true;
                QObject::disconnect(*readyConn);
                QObject::disconnect(*failConn);
                worker->deleteLater();
                onAddonReady(addonId, payload);
            });

        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
            [this, req, addonId, handled, readyConn, failConn, worker, dropIfStale](
                const ResourceRequest& incoming,
                const QString& message) {
                if (*handled) return;
                if (dropIfStale()) return;
                if (!sameRequest(req, incoming)) return;
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
    // DOWNLOAD BUG 2026-06-02 (defense-in-depth) — a stale late completion from
    // a superseded load() (reset() zeroed m_pendingResponses mid-flight) could
    // otherwise drive the counter negative and re-emit streamsReady against the
    // wrong request. Bail before decrementing if there is nothing outstanding.
    if (m_pendingResponses <= 0) {
        m_pendingResponses = 0;
        return;
    }
    --m_pendingResponses;
    if (m_pendingResponses > 0) {
        return;
    }
    // DOWNLOAD BUG 2026-06-03 (review fix) — deferred + generation-guarded so a
    // SYNCHRONOUS resourceFailed (invalid addon URL) during dispatchRequests()
    // can't fire streamsReady inside load() before the caller stores its token.
    emitStreamsReadyDeferred(m_loadGeneration, m_streams, m_addonsById);
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

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B1) - per-call pack-search
// state. Owned by std::shared_ptr so multiple concurrent searches don't
// collide and so a still-pending search can outlive the searchPacks()
// return. Post-review (C1) fix: shared_ptr ownership replaces raw new/delete
// so slow indexer callbacks arriving after the 30s timeout no longer
// UB-read freed memory; they keep ctx alive via captured shared_ptr and
// hit the ctx->emitted early-out instead.
struct StreamAggregator::PackSearchContext
{
    QString imdbId;
    int     season = 0;
    int     outstanding = 0;
    QList<TorrentResult> results;
    QSet<QString>        seenInfoHashes;
    QTimer* timeout = nullptr;
    bool    emitted = false;
    // TANKORENT audit DEFECT 2 (2026-05-28) — monotonic search epoch. Each
    // searchPacks() call bumps StreamAggregator::m_packEpoch and stamps it
    // here. finalizePackSearch suppresses any ctx whose epoch is no longer
    // current, so a superseded search (e.g. "All Sources" still in flight
    // when the user switches to "Nyaa") cannot emit stale results that would
    // bleed into the new search — its imdbId/season match the new search so
    // the downstream imdbId/season guards alone would NOT catch it.
    quint64 epoch = 0;
};

namespace {

constexpr int kPackSearchTimeoutMs = 30 * 1000;
constexpr int kPackSearchPerIndexerLimit = 25;
// THEATRE_ANIME_CATALOG — anime batch search needs a wider net (big batch
// torrents bury individual titles); lift the per-indexer cap for that path.
constexpr int kAnimeBatchPerIndexerLimit = 100;

bool packSearchIndexerEnabled(const QString& id)
{
    QSettings settings;
    return settings.value(
        QStringLiteral("tankorent/indexers/%1/enabled").arg(id), true).toBool();
}

}  // namespace

void StreamAggregator::searchPacks(const QString& imdbId,
                                   const QString& showName,
                                   int season,
                                   const QString& sourceFilter,
                                   bool anime)
{
    if (!m_packNam) {
        m_packNam = tankoban::net::NetSeam::instance()->createManager(this, QStringLiteral("stream-pack-search"));
    }

    // THEATRE_ANIME_CATALOG — anime is torrented as big multi-episode batches,
    // never "Season N", and needs a wider net than the 25-cap. Broaden both.
    const int perIndexerLimit =
        anime ? kAnimeBatchPerIndexerLimit : kPackSearchPerIndexerLimit;

    QStringList queries;
    if (anime) {
        queries = buildAnimePackQueries(showName);
    } else if (season > 0) {
        queries << QStringLiteral("%1 S%2")
                       .arg(showName)
                       .arg(season, 2, 10, QLatin1Char('0'));
        queries << QStringLiteral("%1 Season %2").arg(showName).arg(season);
    } else {
        queries << QStringLiteral("%1 Complete").arg(showName);
        queries << QStringLiteral("%1 Complete Series").arg(showName);
    }

    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B1 post-review fix) -
    // shared_ptr ownership so a slow indexer responding after the 30s timeout
    // can no-op cleanly on the ctx->emitted early-out instead of UB-reading
    // freed memory. Every lambda below captures ctx BY VALUE (shared_ptr copy
    // bumps refcount); the context dies when the last lambda is destroyed.
    auto ctx = std::make_shared<PackSearchContext>();
    ctx->imdbId = imdbId;
    ctx->season = season;
    ctx->epoch  = ++m_packEpoch;  // DEFECT 2 — supersede any prior in-flight pack search

    QPointer<StreamAggregator> self(this);

    // Explicit-only capture (post-review I1 fix): ctx is a shared_ptr by value,
    // self is a QPointer by value, this is needed for connect()'s receiver
    // argument and for indexer instantiation. No `&` capture - searchPacks's
    // stack frame is gone by the time async indexer callbacks fire.
    auto dispatch = [ctx, self, this, perIndexerLimit](const QString& id,
                                      TorrentIndexer* indexer, const QString& query) {
        if (!packSearchIndexerEnabled(id)) {
            indexer->deleteLater();
            return;
        }
        ++ctx->outstanding;
        QPointer<TorrentIndexer> idxPtr(indexer);
        connect(indexer, &TorrentIndexer::searchFinished, this,
                [self, ctx, idxPtr](const QList<TorrentResult>& results) {
                    if (!self || ctx->emitted) {
                        if (idxPtr) idxPtr->deleteLater();
                        return;
                    }
                    for (const TorrentResult& r : results) {
                        const QString key = r.infoHash.isEmpty()
                            ? QStringLiteral("magnet::") + r.magnetUri
                            : r.infoHash;
                        if (ctx->seenInfoHashes.contains(key))
                            continue;
                        ctx->seenInfoHashes.insert(key);
                        ctx->results.append(r);
                    }
                    if (idxPtr) idxPtr->deleteLater();
                    if (--ctx->outstanding <= 0) {
                        self->finalizePackSearch(ctx);
                    }
                });
        connect(indexer, &TorrentIndexer::searchError, this,
                [self, ctx, idxPtr](const QString&) {
                    if (!self || ctx->emitted) {
                        if (idxPtr) idxPtr->deleteLater();
                        return;
                    }
                    if (idxPtr) idxPtr->deleteLater();
                    if (--ctx->outstanding <= 0) {
                        self->finalizePackSearch(ctx);
                    }
                });
        indexer->search(query, perIndexerLimit);
    };

    // THEATRE_SOURCE_PICKER 2026-05-17: gate each indexer by sourceFilter.
    // "all" (default) preserves the existing fan-out shape. A specific id
    // skips siblings, letting the Theatre source-combo UI route to a single
    // indexer (e.g. "nyaa" for anime).
    auto wants = [&](const QString& id) {
        return sourceFilter == QStringLiteral("all") || sourceFilter == id;
    };
    for (const QString& query : queries) {
        if (wants(QStringLiteral("nyaa")))
            dispatch(QStringLiteral("nyaa"),
                     new NyaaIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("piratebay")))
            dispatch(QStringLiteral("piratebay"),
                     new PirateBayIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("1337x")))
            dispatch(QStringLiteral("1337x"),
                     new X1337xIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("yts")))
            dispatch(QStringLiteral("yts"),
                     new YtsIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("eztv")))
            dispatch(QStringLiteral("eztv"),
                     new EztvIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("exttorrents")))
            dispatch(QStringLiteral("exttorrents"),
                     new ExtTorrentsIndexer(m_packNam, this), query);
    }

    if (ctx->outstanding == 0) {
        // No enabled indexers; emit empty result on the next event-loop tick
        // so listeners that connect() right after this call still receive it.
        QTimer::singleShot(0, this, [self, ctx]() {
            if (self) self->finalizePackSearch(ctx);
            // shared_ptr drops automatically if !self; no manual delete needed.
        });
        return;
    }

    ctx->timeout = new QTimer(this);
    ctx->timeout->setSingleShot(true);
    connect(ctx->timeout, &QTimer::timeout, this, [self, ctx]() {
        if (self && !ctx->emitted) {
            self->finalizePackSearch(ctx);
        }
    });
    ctx->timeout->start(kPackSearchTimeoutMs);
}

void StreamAggregator::finalizePackSearch(std::shared_ptr<PackSearchContext> ctx)
{
    if (!ctx || ctx->emitted) {
        return;
    }
    // DEFECT 2 (2026-05-28) — suppress superseded searches. If a newer
    // searchPacks() has bumped m_packEpoch since this ctx was created, its
    // results are stale (e.g. the old "All Sources" fan-out finishing after
    // the user switched to "Nyaa"). Mark emitted so captured lambdas no-op,
    // but do NOT emit packsAvailable — that would bleed old-source rows into
    // the current search.
    if (ctx->epoch != m_packEpoch) {
        ctx->emitted = true;
        if (ctx->timeout) {
            ctx->timeout->stop();
            ctx->timeout->deleteLater();
            ctx->timeout = nullptr;
        }
        return;
    }
    ctx->emitted = true;
    if (ctx->timeout) {
        ctx->timeout->stop();
        ctx->timeout->deleteLater();
        ctx->timeout = nullptr;
    }
    emit packsAvailable(ctx->imdbId, ctx->season, ctx->results);
    // No manual delete: shared_ptr destroys the context when the last lambda
    // (and this local) goes out of scope. Stale post-timeout callbacks keep
    // ctx alive but hit the ctx->emitted early-out cleanly.
}

}
