#include "TorrentioClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

static const QString TORRENTIO_BASE = QStringLiteral("https://torrentio.strem.fun");
static constexpr int REQUEST_TIMEOUT_MS = 10000;
static constexpr int MAX_TRACKERS = 16;

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

// Emoji code points used by Torrentio title format
static const QChar BUST_EMOJI_HI = QChar(0xD83D);  // U+1F464 high surrogate
static const QChar BUST_EMOJI_LO = QChar(0xDC64);  // U+1F464 low surrogate
static const QChar DISK_EMOJI_HI = QChar(0xD83D);  // U+1F4BE high surrogate
static const QChar DISK_EMOJI_LO = QChar(0xDCBE);  // U+1F4BE low surrogate
static const QChar GEAR_EMOJI    = QChar(0x2699);   // U+2699

// Flag emoji range (regional indicator symbols)
static const QChar FLAG_HI = QChar(0xD83C);         // U+1F1E0-1F1FF high surrogate
static const uint  FLAG_LO_MIN = 0xDDE0;            // low surrogate for U+1F1E0
static const uint  FLAG_LO_MAX = 0xDDFF;            // low surrogate for U+1F1FF

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
            uint lo = s[i + 1].unicode();
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
    int slash = path.lastIndexOf('/');
    int bslash = path.lastIndexOf('\\');
    int pos = qMax(slash, bslash);
    return pos >= 0 ? path.mid(pos + 1) : path;
}

// ─── InfoHash validation ────────────────────────────────────────────────────

static const QRegularExpression HASH_RE(QStringLiteral("^[a-f0-9]{40}$"));

// ─── Quality regex patterns ─────────────────────────────────────────────────

static const QRegularExpression RES_RE(
    QStringLiteral("\\b(2160p|1080p|720p|480p|4[Kk])\\b"));
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

// ─── Seeders extraction regex ───────────────────────────────────────────────
// Match digits after the bust emoji (👤)
static const QRegularExpression SEEDERS_RE(
    QString("\\x{1F464}\\s*(\\d+)"));

// ─── Size extraction regex ──────────────────────────────────────────────────
// Match size string after the disk emoji (💾)
static const QRegularExpression SIZE_RE(
    QString("\\x{1F4BE}\\s*([\\d.,]+\\s*[KMGT]?i?B)"),
    QRegularExpression::CaseInsensitiveOption);

// ─── Tracker extraction regex ───────────────────────────────────────────────
// Match text after gear emoji (⚙)
static const QRegularExpression TRACKER_RE(
    QString("\\x{2699}\\x{FE0F}?\\s*(.+)$"),
    QRegularExpression::MultilineOption);

// ═══════════════════════════════════════════════════════════════════════════

TorrentioClient::TorrentioClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void TorrentioClient::fetchStreams(const QString& imdbId, const QString& mediaType,
                                   int season, int episode)
{
    if (!imdbId.startsWith("tt")) {
        emit streamsError(QStringLiteral("Invalid IMDB ID"));
        return;
    }

    QString urlStr;
    if (mediaType == "movie") {
        urlStr = TORRENTIO_BASE + "/stream/movie/" + imdbId + ".json";
    } else {
        urlStr = TORRENTIO_BASE + "/stream/series/" + imdbId
                 + ":" + QString::number(qMax(1, season))
                 + ":" + QString::number(qMax(1, episode)) + ".json";
    }

    QUrl url(urlStr);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)"));
    req.setRawHeader("Accept", "application/json,*/*");
    req.setTransferTimeout(REQUEST_TIMEOUT_MS);

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReply(reply);
    });
}

void TorrentioClient::onReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit streamsError(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        emit streamsError(QStringLiteral("JSON parse error: ") + err.errorString());
        return;
    }

    QJsonArray streams = doc.object().value("streams").toArray();
    QList<TorrentioStream> results;

    for (const auto& val : streams) {
        TorrentioStream s = parseStream(val.toObject());
        if (!s.infoHash.isEmpty())
            results.append(s);
    }

    emit streamsReady(results);
}

TorrentioStream TorrentioClient::parseStream(const QJsonObject& streamObj)
{
    TorrentioStream s;

    // ── Info hash ────────────────────────────────────────────────────────
    s.infoHash = streamObj.value("infoHash").toString().trimmed().toLower();
    if (!HASH_RE.match(s.infoHash).hasMatch()) {
        s.infoHash.clear();
        return s;
    }

    // ── Raw title ────────────────────────────────────────────────────────
    QString rawTitle = streamObj.value("title").toString();
    s.title = rawTitle;

    // ── Parse title lines ────────────────────────────────────────────────
    QStringList lines = rawTitle.split('\n');
    QString fileHintFromTitle;

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();
        if (line.isEmpty())
            continue;

        if (i == 0) {
            s.trackerSource = line;
            continue;
        }

        // Seeders line (👤)
        if (containsBust(line)) {
            auto m = SEEDERS_RE.match(line);
            if (m.hasMatch())
                s.seeders = m.captured(1).toInt();
        }

        // Size line (💾)
        if (containsDisk(line)) {
            auto m = SIZE_RE.match(line);
            if (m.hasMatch())
                s.sizeBytes = parseSize(m.captured(1));
        }

        // Tracker line (⚙)
        if (containsGear(line)) {
            auto m = TRACKER_RE.match(line);
            if (m.hasMatch())
                s.tracker = m.captured(1).trimmed();
        }

        // Language flags
        if (containsFlag(line))
            s.languages = line;

        // Filename hint candidate
        if (fileHintFromTitle.isEmpty() && looksLikeFilename(line))
            fileHintFromTitle = extractFilename(line);
    }

    // ── Quality ──────────────────────────────────────────────────────────
    s.quality = parseQuality(rawTitle);

    // ── File index ───────────────────────────────────────────────────────
    for (const QString& key : {"fileIdx", "fileIndex"}) {
        if (streamObj.contains(key)) {
            int idx = streamObj.value(key).toInt(-1);
            if (idx >= 0) {
                s.fileIndex = idx;
                break;
            }
        }
    }

    // ── File name hint ───────────────────────────────────────────────────
    QJsonObject hints = streamObj.value("behaviorHints").toObject();
    QString bhFilename = hints.value("filename").toString().trimmed();
    s.fileNameHint = bhFilename.isEmpty() ? fileHintFromTitle : bhFilename;

    // ── Trackers from sources array ──────────────────────────────────────
    QStringList trackers;
    QJsonArray sources = streamObj.value("sources").toArray();
    for (const auto& src : sources) {
        QString val = src.toString();
        if (val.startsWith("tracker:")) {
            QString tracker = val.mid(8).trimmed();
            if (!tracker.isEmpty() && trackers.size() < MAX_TRACKERS)
                trackers.append(tracker);
        }
    }
    if (trackers.isEmpty())
        trackers = FALLBACK_TRACKERS.mid(0, MAX_TRACKERS);

    // ── Magnet URI ───────────────────────────────────────────────────────
    s.magnetUri = buildMagnet(s.infoHash, s.trackerSource, trackers);

    return s;
}

QString TorrentioClient::parseQuality(const QString& rawTitle)
{
    QStringList tags;

    auto tryMatch = [&](const QRegularExpression& re) {
        auto m = re.match(rawTitle);
        if (m.hasMatch())
            tags.append(m.captured(1));
    };

    tryMatch(RES_RE);
    tryMatch(SRC_RE);
    tryMatch(HDR_RE);
    tryMatch(CODEC_RE);
    tryMatch(AUDIO_RE);

    return tags.join(" / ");
}

QString TorrentioClient::buildMagnet(const QString& infoHash, const QString& title,
                                      const QStringList& trackers)
{
    QString magnet = QStringLiteral("magnet:?xt=urn:btih:")
                     + infoHash.toLower()
                     + QStringLiteral("&dn=")
                     + QUrl::toPercentEncoding(title);

    for (const QString& tr : trackers)
        magnet += QStringLiteral("&tr=") + QUrl::toPercentEncoding(tr);

    return magnet;
}

qint64 TorrentioClient::parseSize(const QString& sizeStr)
{
    QString s = sizeStr.trimmed().toUpper().replace(',', '.');

    static const QRegularExpression numRe(QStringLiteral("^([\\d.]+)\\s*([KMGT]?I?B?)$"));
    auto m = numRe.match(s);
    if (!m.hasMatch())
        return 0;

    bool ok = false;
    double val = m.captured(1).toDouble(&ok);
    if (!ok || val < 0)
        return 0;

    QString unit = m.captured(2);
    // Normalize: treat KB same as KIB (binary), matching groundwork behavior
    if (unit.startsWith('T'))      val *= 1099511627776.0;
    else if (unit.startsWith('G')) val *= 1073741824.0;
    else if (unit.startsWith('M')) val *= 1048576.0;
    else if (unit.startsWith('K')) val *= 1024.0;

    return static_cast<qint64>(val);
}
