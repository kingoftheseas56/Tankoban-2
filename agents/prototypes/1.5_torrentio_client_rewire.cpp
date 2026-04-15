// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 1.5 (Addon Protocol Foundation)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:91
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/TorrentioClient.h:10
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/TorrentioClient.cpp:135
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:250
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/1.2_addon_transport.cpp:149
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/1.3_addon_registry.cpp:131
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/resource/stream.rs:68
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/request.rs:64
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 1.5.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <memory>

namespace tankostream::addon {

// -----------------------------------------------------------------
// Minimal addon types for standalone prototype wiring.
// -----------------------------------------------------------------

struct ResourceRequest {
    QString resource;
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;
};

struct AddonManifest {
    QString id;
    QString name;
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

class AddonTransport : public QObject {
    Q_OBJECT
public:
    explicit AddonTransport(QObject* parent = nullptr) : QObject(parent) {}
    void fetchResource(const QUrl& base, const ResourceRequest& request);

signals:
    void resourceReady(const ResourceRequest& request, const QJsonObject& payload);
    void resourceFailed(const ResourceRequest& request, const QString& message);
};

class AddonRegistry : public QObject {
    Q_OBJECT
public:
    explicit AddonRegistry(AddonTransport* transport, QObject* parent = nullptr)
        : QObject(parent)
    {
        Q_UNUSED(transport);
    }
    QList<AddonDescriptor> list() const;
};

} // namespace tankostream::addon

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::AddonTransport;
using tankostream::addon::ResourceRequest;

// -----------------------------------------------------------------
// Existing public data contract retained (unchanged).
// -----------------------------------------------------------------

struct TorrentioStream {
    QString title;          // raw multi-line title from Torrentio
    QString magnetUri;      // built from infoHash + trackers
    QString infoHash;       // 40-char hex lowercase
    qint64  sizeBytes = 0;  // parsed from emoji marker
    int     seeders = 0;    // parsed from emoji marker
    QString quality;        // "1080p / BluRay / HEVC / DDP 5.1"
    QString trackerSource;  // release name (first line of title)
    QString tracker;        // tracker name from title
    QString languages;      // flag emojis line
    QString fileNameHint;   // from behaviorHints or parsed
    int     fileIndex = -1; // pre-selected video file index, -1 if unknown
};

// Emoji code points used by Torrentio title format.
static const QChar BUST_EMOJI_HI = QChar(0xD83D);  // U+1F464 high surrogate
static const QChar BUST_EMOJI_LO = QChar(0xDC64);  // U+1F464 low surrogate
static const QChar DISK_EMOJI_HI = QChar(0xD83D);  // U+1F4BE high surrogate
static const QChar DISK_EMOJI_LO = QChar(0xDCBE);  // U+1F4BE low surrogate
static const QChar GEAR_EMOJI    = QChar(0x2699);  // U+2699

// Flag emoji range (regional indicator symbols).
static const QChar FLAG_HI = QChar(0xD83C);  // U+1F1E0-1F1FF high surrogate
static const uint FLAG_LO_MIN = 0xDDE0;
static const uint FLAG_LO_MAX = 0xDDFF;

static bool containsBust(const QString& s)
{
    for (int i = 0; i < s.size() - 1; ++i)
        if (s[i] == BUST_EMOJI_HI && s[i + 1] == BUST_EMOJI_LO)
            return true;
    return false;
}

static bool containsDisk(const QString& s)
{
    for (int i = 0; i < s.size() - 1; ++i)
        if (s[i] == DISK_EMOJI_HI && s[i + 1] == DISK_EMOJI_LO)
            return true;
    return false;
}

static bool containsGear(const QString& s)
{
    return s.contains(GEAR_EMOJI);
}

static bool containsFlag(const QString& s)
{
    for (int i = 0; i < s.size() - 1; ++i) {
        if (s[i] == FLAG_HI) {
            const uint lo = s[i + 1].unicode();
            if (lo >= FLAG_LO_MIN && lo <= FLAG_LO_MAX)
                return true;
        }
    }
    return false;
}

static bool looksLikeFilename(const QString& s)
{
    return s.contains('.') && s.length() <= 300 && !s.startsWith("http");
}

static QString extractFilename(const QString& path)
{
    const int slash = path.lastIndexOf('/');
    const int bslash = path.lastIndexOf('\\');
    const int pos = qMax(slash, bslash);
    return pos >= 0 ? path.mid(pos + 1) : path;
}

static const QRegularExpression HASH_RE(QStringLiteral("^[a-f0-9]{40}$"));
static const QRegularExpression RES_RE(QStringLiteral("\\b(2160p|1080p|720p|480p|4[Kk])\\b"));
static const QRegularExpression SRC_RE(
    QStringLiteral("\\b(WEB[-\\s]?DL|WEBRip|BluRay|BDRip|BRRip|HDRip|HDTV|DVDRip|PDTV|AMZN|NF)\\b"),
    QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression HDR_RE(
    QStringLiteral("\\b(Dolby\\s*Vision|DV|HDR10\\+?|HDR|SDR|10[Bb]it)\\b"),
    QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression CODEC_RE(
    QStringLiteral("\\b(H\\.?265|x265|HEVC|H\\.?264|x264|AV1|VP9)\\b"),
    QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression AUDIO_RE(
    QStringLiteral("\\b(Atmos|DDP?\\s*5\\.1|TrueHD|DTS[-\\s]?HD|AAC|FLAC|AC3)\\b"),
    QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression SEEDERS_RE(QString("\\x{1F464}\\s*(\\d+)"));
static const QRegularExpression SIZE_RE(
    QString("\\x{1F4BE}\\s*([\\d.,]+\\s*[KMGT]?i?B)"),
    QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression TRACKER_RE(
    QString("\\x{2699}\\x{FE0F}?\\s*(.+)$"),
    QRegularExpression::MultilineOption);

static const QStringList FALLBACK_TRACKERS = {
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

class TorrentioClient : public QObject
{
    Q_OBJECT

public:
    // Public signature unchanged from current src/.
    explicit TorrentioClient(QObject* parent = nullptr)
        : QObject(parent)
        , m_transport(new AddonTransport(this))
        , m_registry(new AddonRegistry(m_transport, this))
    {
    }

    // Public signature unchanged from current src/.
    void fetchStreams(const QString& imdbId, const QString& mediaType,
                      int season = 1, int episode = 1)
    {
        if (!imdbId.startsWith("tt")) {
            emit streamsError("Invalid IMDB ID");
            return;
        }

        const QUrl torrentioUrl = findTorrentioTransportUrl();
        if (!torrentioUrl.isValid()) {
            emit streamsError("Torrentio addon is not installed or disabled");
            return;
        }

        ResourceRequest request;
        request.resource = "stream";
        request.type = (mediaType == "movie") ? "movie" : "series";
        request.id = (mediaType == "movie")
            ? imdbId
            : imdbId + ":" + QString::number(qMax(1, season)) + ":" + QString::number(qMax(1, episode));

        auto handled = std::make_shared<bool>(false);
        auto readyConn = std::make_shared<QMetaObject::Connection>();
        auto failConn = std::make_shared<QMetaObject::Connection>();

        *readyConn = connect(m_transport, &AddonTransport::resourceReady, this,
            [this, request, handled, readyConn, failConn](
                const ResourceRequest& incoming,
                const QJsonObject& payload) {
                if (*handled || !sameRequest(request, incoming))
                    return;
                *handled = true;
                disconnect(*readyConn);
                disconnect(*failConn);

                const QJsonArray streamsJson = payload.value("streams").toArray();
                QList<TorrentioStream> parsed;
                for (const QJsonValue& value : streamsJson) {
                    TorrentioStream stream = parseStream(value.toObject());
                    if (!stream.infoHash.isEmpty())
                        parsed.push_back(stream);
                }
                emit streamsReady(parsed);
            });

        *failConn = connect(m_transport, &AddonTransport::resourceFailed, this,
            [this, request, handled, readyConn, failConn](
                const ResourceRequest& incoming,
                const QString& message) {
                if (*handled || !sameRequest(request, incoming))
                    return;
                *handled = true;
                disconnect(*readyConn);
                disconnect(*failConn);
                emit streamsError(message);
            });

        m_transport->fetchResource(torrentioUrl, request);
    }

signals:
    void streamsReady(const QList<TorrentioStream>& streams);
    void streamsError(const QString& message);

private:
    static constexpr int MAX_TRACKERS = 16;

    static bool sameRequest(const ResourceRequest& a, const ResourceRequest& b)
    {
        return a.resource == b.resource
            && a.type == b.type
            && a.id == b.id
            && a.extra == b.extra;
    }

    QUrl findTorrentioTransportUrl() const
    {
        // No hardcoded Torrentio URL in this client. Registry is source of truth.
        const QList<AddonDescriptor> addons = m_registry->list();
        for (const AddonDescriptor& addon : addons) {
            if (!addon.flags.enabled)
                continue;
            if (addon.manifest.id == "com.stremio.torrentio.addon")
                return addon.transportUrl;
        }

        // Fallback for custom fork naming.
        for (const AddonDescriptor& addon : addons) {
            if (!addon.flags.enabled)
                continue;
            if (addon.manifest.name.compare("Torrentio", Qt::CaseInsensitive) == 0)
                return addon.transportUrl;
        }

        return {};
    }

    // Parsing logic intentionally retained from current TorrentioClient.
    static TorrentioStream parseStream(const QJsonObject& streamObj)
    {
        TorrentioStream s;

        s.infoHash = streamObj.value("infoHash").toString().trimmed().toLower();
        if (!HASH_RE.match(s.infoHash).hasMatch()) {
            s.infoHash.clear();
            return s;
        }

        const QString rawTitle = streamObj.value("title").toString();
        s.title = rawTitle;

        const QStringList lines = rawTitle.split('\n');
        QString fileHintFromTitle;

        for (int i = 0; i < lines.size(); ++i) {
            const QString line = lines[i].trimmed();
            if (line.isEmpty())
                continue;

            if (i == 0) {
                s.trackerSource = line;
                continue;
            }

            if (containsBust(line)) {
                const auto m = SEEDERS_RE.match(line);
                if (m.hasMatch())
                    s.seeders = m.captured(1).toInt();
            }

            if (containsDisk(line)) {
                const auto m = SIZE_RE.match(line);
                if (m.hasMatch())
                    s.sizeBytes = parseSize(m.captured(1));
            }

            if (containsGear(line)) {
                const auto m = TRACKER_RE.match(line);
                if (m.hasMatch())
                    s.tracker = m.captured(1).trimmed();
            }

            if (containsFlag(line))
                s.languages = line;

            if (fileHintFromTitle.isEmpty() && looksLikeFilename(line))
                fileHintFromTitle = extractFilename(line);
        }

        s.quality = parseQuality(rawTitle);

        for (const QString& key : {"fileIdx", "fileIndex"}) {
            if (streamObj.contains(key)) {
                const int idx = streamObj.value(key).toInt(-1);
                if (idx >= 0) {
                    s.fileIndex = idx;
                    break;
                }
            }
        }

        const QJsonObject hints = streamObj.value("behaviorHints").toObject();
        const QString hintedFilename = hints.value("filename").toString().trimmed();
        s.fileNameHint = hintedFilename.isEmpty() ? fileHintFromTitle : hintedFilename;

        QStringList trackers;
        for (const QJsonValue& source : streamObj.value("sources").toArray()) {
            const QString value = source.toString();
            if (!value.startsWith("tracker:"))
                continue;
            const QString tracker = value.mid(8).trimmed();
            if (!tracker.isEmpty() && trackers.size() < MAX_TRACKERS)
                trackers.push_back(tracker);
        }
        if (trackers.isEmpty())
            trackers = FALLBACK_TRACKERS.mid(0, MAX_TRACKERS);

        s.magnetUri = buildMagnet(s.infoHash, s.trackerSource, trackers);
        return s;
    }

    static QString parseQuality(const QString& rawTitle)
    {
        QStringList tags;
        auto tryMatch = [&](const QRegularExpression& re) {
            const auto m = re.match(rawTitle);
            if (m.hasMatch())
                tags.push_back(m.captured(1));
        };

        tryMatch(RES_RE);
        tryMatch(SRC_RE);
        tryMatch(HDR_RE);
        tryMatch(CODEC_RE);
        tryMatch(AUDIO_RE);

        return tags.join(" / ");
    }

    static QString buildMagnet(
        const QString& infoHash,
        const QString& title,
        const QStringList& trackers)
    {
        QString magnet = QStringLiteral("magnet:?xt=urn:btih:")
            + infoHash.toLower()
            + QStringLiteral("&dn=")
            + QUrl::toPercentEncoding(title);

        for (const QString& tracker : trackers)
            magnet += QStringLiteral("&tr=") + QUrl::toPercentEncoding(tracker);
        return magnet;
    }

    static qint64 parseSize(const QString& sizeStr)
    {
        const QString normalized = sizeStr.trimmed().toUpper().replace(',', '.');
        static const QRegularExpression NUM_RE(QStringLiteral("^([\\d.]+)\\s*([KMGT]?I?B?)$"));
        const auto m = NUM_RE.match(normalized);
        if (!m.hasMatch())
            return 0;

        bool ok = false;
        double value = m.captured(1).toDouble(&ok);
        if (!ok || value < 0)
            return 0;

        const QString unit = m.captured(2);
        if (unit.startsWith('T')) value *= 1099511627776.0;
        else if (unit.startsWith('G')) value *= 1073741824.0;
        else if (unit.startsWith('M')) value *= 1048576.0;
        else if (unit.startsWith('K')) value *= 1024.0;

        return static_cast<qint64>(value);
    }

    AddonTransport* m_transport = nullptr;
    AddonRegistry* m_registry = nullptr;
};

