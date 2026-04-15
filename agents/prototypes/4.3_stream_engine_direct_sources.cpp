// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 4.3 (Multi-Source Stream Aggregation)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:197
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:201
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:202
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:203
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:204
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamEngine.h:12
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamEngine.h:47
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamEngine.cpp:58
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamEngine.cpp:195
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamPlayerController.h:20
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamPlayerController.cpp:19
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamPlayerController.cpp:77
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/StreamSource.h:10
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/StreamSource.h:66
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/StreamInfo.h:30
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/4.2_stream_picker_dialog.cpp:470
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/resource/stream.rs:769
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/resource/stream.rs:994
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 4.3.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>

class TorrentEngine;
class StreamHttpServer;
class CoreBridge;

namespace tankostream::addon {

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

    QString toMagnetUri() const
    {
        if (kind != Kind::Magnet || infoHash.isEmpty()) {
            return {};
        }
        QString uri = QStringLiteral("magnet:?xt=urn:btih:") + infoHash.toLower();
        for (const QString& tracker : trackers) {
            uri += QStringLiteral("&tr=")
                   + QString::fromUtf8(QUrl::toPercentEncoding(tracker));
        }
        return uri;
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
};

} // namespace tankostream::addon

namespace tankostream::stream {

using tankostream::addon::Stream;
using tankostream::addon::StreamSource;

enum class PlaybackMode {
    LocalHttp, // torrent-backed local path/http path
    DirectUrl, // addon url/http source
};

struct StreamFileResult {
    bool ok = false;
    PlaybackMode playbackMode = PlaybackMode::LocalHttp;

    // For both modes this is what StreamPage forwards to VideoPlayer::openFile.
    QString url;

    // Populated for magnet path only.
    QString infoHash;
    bool readyToStart = false;
    bool queued = false;
    double fileProgress = 0.0;
    qint64 downloadedBytes = 0;
    qint64 fileSize = 0;
    int selectedFileIndex = -1;
    QString selectedFileName;

    QString errorCode;     // METADATA_NOT_READY, FILE_NOT_READY, ENGINE_ERROR, UNSUPPORTED_SOURCE
    QString errorMessage;
};

struct StreamTorrentStatus {
    int peers = 0;
    int dlSpeed = 0;
};

class StreamEngine : public QObject
{
    Q_OBJECT

public:
    explicit StreamEngine(TorrentEngine* engine, const QString& cacheDir, QObject* parent = nullptr)
        : QObject(parent)
        , m_torrentEngine(engine)
        , m_cacheDir(cacheDir)
    {
    }

    // Batch 4.3 API change: Stream replaces raw magnet tuple.
    StreamFileResult streamFile(const Stream& stream)
    {
        switch (stream.source.kind) {
        case StreamSource::Kind::Magnet:
            return streamMagnet(stream);
        case StreamSource::Kind::Http:
        case StreamSource::Kind::Url:
            return streamDirect(stream);
        case StreamSource::Kind::YouTube:
            return streamYouTube(stream);
        }
        return {};
    }

    void stopStream(const QString& infoHash)
    {
        if (infoHash.isEmpty()) {
            return;
        }
        // Existing magnet cleanup behavior from current StreamEngine:
        // unregister http file + remove torrent + delete cache files.
    }

    StreamTorrentStatus torrentStatus(const QString& infoHash) const
    {
        StreamTorrentStatus out;
        QMutexLocker lock(&m_mutex);
        const auto it = m_activeByInfoHash.find(infoHash);
        if (it != m_activeByInfoHash.end()) {
            out.peers = it->peers;
            out.dlSpeed = it->dlSpeed;
        }
        return out;
    }

private:
    struct ActiveMagnetStream {
        QString infoHash;
        QString magnetUri;
        QString savePath;
        int requestedFileIndex = -1;
        QString fileNameHint;
        int selectedFileIndex = -1;
        QString selectedFileName;
        qint64 selectedFileSize = 0;
        bool metadataReady = false;
        bool registered = false;
        int peers = 0;
        int dlSpeed = 0;
    };

    StreamFileResult streamDirect(const Stream& stream)
    {
        StreamFileResult result;
        result.playbackMode = PlaybackMode::DirectUrl;

        if (!stream.source.url.isValid() || stream.source.url.scheme().isEmpty()) {
            result.errorCode = QStringLiteral("ENGINE_ERROR");
            result.errorMessage = QStringLiteral("Direct stream URL is invalid");
            return result;
        }

        // For 4.3, Url/Http skip torrent path entirely.
        result.ok = true;
        result.readyToStart = true;
        result.url = stream.source.url.toString(QUrl::FullyEncoded);
        return result;
    }

    StreamFileResult streamYouTube(const Stream& stream)
    {
        Q_UNUSED(stream);
        StreamFileResult result;
        result.playbackMode = PlaybackMode::DirectUrl;
        result.errorCode = QStringLiteral("UNSUPPORTED_SOURCE");
        result.errorMessage = QStringLiteral("YouTube playback not yet supported");
        return result;
    }

    StreamFileResult streamMagnet(const Stream& stream)
    {
        StreamFileResult result;
        result.playbackMode = PlaybackMode::LocalHttp;

        if (!m_torrentEngine || !m_torrentEngine->isRunning()) {
            result.errorCode = QStringLiteral("ENGINE_ERROR");
            result.errorMessage = QStringLiteral("Torrent engine not running");
            return result;
        }

        const QString magnetUri = stream.source.toMagnetUri();
        if (magnetUri.isEmpty()) {
            result.errorCode = QStringLiteral("ENGINE_ERROR");
            result.errorMessage = QStringLiteral("Magnet stream missing infoHash");
            return result;
        }

        const int fileIndex = stream.source.fileIndex;
        const QString fileNameHint = !stream.source.fileNameHint.isEmpty()
            ? stream.source.fileNameHint
            : stream.behaviorHints.filename;

        // Keep existing lifecycle from current StreamEngine:
        // 1) addMagnet once (by URI)
        // 2) wait for metadata
        // 3) select target file
        // 4) wait for contiguous header bytes
        // 5) flush cache + return local file path (or local http url if preferred)
        QMutexLocker lock(&m_mutex);
        QString infoHash;
        for (auto it = m_activeByInfoHash.begin(); it != m_activeByInfoHash.end(); ++it) {
            if (it->magnetUri == magnetUri) {
                infoHash = it.key();
                break;
            }
        }

        if (infoHash.isEmpty()) {
            infoHash = m_torrentEngine->addMagnet(magnetUri, m_cacheDir, false);
            if (infoHash.isEmpty()) {
                result.errorCode = QStringLiteral("ENGINE_ERROR");
                result.errorMessage = QStringLiteral("Failed to add magnet");
                return result;
            }

            ActiveMagnetStream rec;
            rec.infoHash = infoHash;
            rec.magnetUri = magnetUri;
            rec.savePath = m_cacheDir;
            rec.requestedFileIndex = fileIndex;
            rec.fileNameHint = fileNameHint;
            m_activeByInfoHash.insert(infoHash, rec);

            m_torrentEngine->setSequentialDownload(infoHash, true);

            result.infoHash = infoHash;
            result.queued = true;
            result.errorCode = QStringLiteral("METADATA_NOT_READY");
            result.errorMessage = QStringLiteral("Metadata not ready");
            return result;
        }

        ActiveMagnetStream& rec = m_activeByInfoHash[infoHash];
        result.infoHash = rec.infoHash;

        if (!rec.metadataReady) {
            result.queued = true;
            result.errorCode = QStringLiteral("METADATA_NOT_READY");
            result.errorMessage = QStringLiteral("Metadata not ready");
            return result;
        }
        if (!rec.registered) {
            result.queued = true;
            result.errorCode = QStringLiteral("FILE_NOT_READY");
            result.errorMessage = QStringLiteral("File not ready");
            return result;
        }

        // Existing progress/build-url logic remains here.
        // Omitted in prototype for brevity - keep current implementation body.
        //
        // result.fileProgress = ...
        // result.downloadedBytes = ...
        // result.fileSize = ...
        // result.selectedFileIndex = rec.selectedFileIndex;
        // result.selectedFileName = rec.selectedFileName;

        // Ready branch:
        result.ok = true;
        result.readyToStart = true;
        result.url = rec.savePath + QLatin1Char('/') + rec.selectedFileName;
        return result;
    }

    TorrentEngine* m_torrentEngine = nullptr;
    StreamHttpServer* m_httpServer = nullptr;
    QString m_cacheDir;

    mutable QMutex m_mutex;
    QHash<QString, ActiveMagnetStream> m_activeByInfoHash;
};

class StreamPlayerController : public QObject
{
    Q_OBJECT

public:
    explicit StreamPlayerController(CoreBridge* bridge, StreamEngine* engine, QObject* parent = nullptr)
        : QObject(parent)
        , m_bridge(bridge)
        , m_engine(engine)
    {
        m_pollTimer.setSingleShot(false);
        connect(&m_pollTimer, &QTimer::timeout, this, &StreamPlayerController::pollStreamStatus);
    }

    // Batch 4.3 API change: selectedStream replaces raw magnet tuple.
    void startStream(const QString& imdbId,
                     const QString& mediaType,
                     int season,
                     int episode,
                     const Stream& selectedStream)
    {
        stopStream();

        m_active = true;
        m_imdbId = imdbId;
        m_mediaType = mediaType;
        m_season = season;
        m_episode = episode;
        m_selectedStream = selectedStream;
        m_pollCount = 0;
        m_startTimeMs = QDateTime::currentMSecsSinceEpoch();
        m_lastMetadataChangeMs = m_startTimeMs;
        m_lastErrorCode.clear();

        // Direct URL/HTTP does not need polling loop.
        if (selectedStream.source.kind == StreamSource::Kind::Http
            || selectedStream.source.kind == StreamSource::Kind::Url) {
            StreamFileResult oneShot = m_engine->streamFile(m_selectedStream);
            if (oneShot.ok && oneShot.readyToStart) {
                onStreamReady(oneShot.url);
                return;
            }
            m_active = false;
            emit streamFailed(oneShot.errorMessage.isEmpty()
                ? QStringLiteral("Failed to start direct stream")
                : oneShot.errorMessage);
            return;
        }

        // YouTube still unsupported for now.
        if (selectedStream.source.kind == StreamSource::Kind::YouTube) {
            m_active = false;
            emit streamFailed(QStringLiteral("YouTube playback not yet supported"));
            return;
        }

        // Magnet path keeps current polling behavior.
        pollStreamStatus();
        m_pollTimer.start(POLL_FAST_MS);
    }

    void stopStream()
    {
        if (!m_active) {
            return;
        }

        m_pollTimer.stop();
        m_active = false;

        // Only magnet streams have an engine-side torrent to stop.
        if (m_selectedStream.source.kind == StreamSource::Kind::Magnet && !m_infoHash.isEmpty()) {
            m_engine->stopStream(m_infoHash);
        }

        m_infoHash.clear();
        m_selectedStream = {};
        emit streamStopped();
    }

signals:
    void bufferUpdate(const QString& statusText, double percent);
    void readyToPlay(const QString& url);
    void streamFailed(const QString& message);
    void streamStopped();

private slots:
    void pollStreamStatus()
    {
        if (!m_active) {
            return;
        }

        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startTimeMs;
        if (elapsed > HARD_TIMEOUT_MS) {
            m_pollTimer.stop();
            m_active = false;
            emit streamFailed(QStringLiteral("Stream timed out after ")
                             + QString::number(HARD_TIMEOUT_MS / 1000) + QStringLiteral("s"));
            return;
        }

        StreamFileResult result = m_engine->streamFile(m_selectedStream);

        if (m_infoHash.isEmpty() && !result.infoHash.isEmpty()) {
            m_infoHash = result.infoHash;
        }

        // Core 4.3 branch: direct mode exits immediately, no polling/overlay loop.
        if (result.playbackMode == PlaybackMode::DirectUrl) {
            m_pollTimer.stop();
            if (result.ok && result.readyToStart) {
                onStreamReady(result.url);
            } else {
                m_active = false;
                emit streamFailed(result.errorMessage);
            }
            return;
        }

        if (result.ok && result.readyToStart) {
            m_pollTimer.stop();
            onStreamReady(result.url);
            return;
        }

        QString statusText;
        StreamTorrentStatus torrentStatus = m_engine->torrentStatus(m_infoHash);
        if (result.errorCode == QStringLiteral("METADATA_NOT_READY")) {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_lastErrorCode != QStringLiteral("METADATA_NOT_READY")) {
                m_lastMetadataChangeMs = now;
            } else if (now - m_lastMetadataChangeMs > METADATA_STALL_MS) {
                statusText = QStringLiteral("Metadata stalled. Torrent may be dead.");
                emit bufferUpdate(statusText, 0.0);
                m_lastErrorCode = result.errorCode;
                ++m_pollCount;
                return;
            }
            statusText = QStringLiteral("Resolving metadata...");
        } else if (result.errorCode == QStringLiteral("FILE_NOT_READY")) {
            const int pct = static_cast<int>(result.fileProgress * 100.0);
            const double mb = result.downloadedBytes / (1024.0 * 1024.0);
            const double speed = torrentStatus.dlSpeed / (1024.0 * 1024.0);
            statusText = QStringLiteral("Buffering... %1% (%2 MB)")
                .arg(pct).arg(mb, 0, 'f', 1);
            if (torrentStatus.peers > 0 || speed > 0.01) {
                statusText += QStringLiteral(" - %1 peers, %2 MB/s")
                    .arg(torrentStatus.peers).arg(speed, 0, 'f', 1);
            }
        } else if (result.errorCode == QStringLiteral("ENGINE_ERROR")) {
            m_pollTimer.stop();
            m_active = false;
            emit streamFailed(result.errorMessage);
            return;
        } else {
            statusText = QStringLiteral("Connecting...");
        }

        m_lastErrorCode = result.errorCode;
        emit bufferUpdate(statusText, result.fileProgress * 100.0);

        ++m_pollCount;
        if (m_pollCount == POLL_SLOW_AFTER && m_pollTimer.interval() != POLL_SLOW_MS) {
            m_pollTimer.setInterval(POLL_SLOW_MS);
        }
    }

private:
    void onStreamReady(const QString& url)
    {
        emit readyToPlay(url);
    }

    CoreBridge* m_bridge = nullptr;
    StreamEngine* m_engine = nullptr;

    QTimer m_pollTimer;
    bool m_active = false;

    QString m_infoHash;
    QString m_imdbId;
    QString m_mediaType;
    int m_season = 0;
    int m_episode = 0;
    Stream m_selectedStream;

    int m_pollCount = 0;
    qint64 m_startTimeMs = 0;
    qint64 m_lastMetadataChangeMs = 0;
    QString m_lastErrorCode;

    static constexpr int POLL_FAST_MS = 300;
    static constexpr int POLL_SLOW_MS = 1000;
    static constexpr int POLL_SLOW_AFTER = 100;
    static constexpr int HARD_TIMEOUT_MS = 120000;
    static constexpr int METADATA_STALL_MS = 60000;
};

} // namespace tankostream::stream

// -----------------------------------------------------------------
// StreamPage integration sketch (Batch 4.3 scope only)
// -----------------------------------------------------------------
//
// 1) StreamPicker (from 4.2) now returns StreamPickerChoice::stream.
//    Keep choice persistence keys from 4.2:
//      sourceKind, addonId, directUrl, youtubeId, magnetUri, infoHash, fileIndex, fileNameHint
//
// 2) Replace current player handoff call:
//      m_playerController->startStream(
//          imdbId, mediaType, season, episode, selected.stream);
//
// 3) Behavior:
//    - Magnet -> unchanged buffering flow via StreamEngine poll loop.
//    - Url/Http -> immediate readyToPlay(url), no buffer polling.
//    - YouTube -> immediate streamFailed("YouTube playback not yet supported").
//
// 4) No changes required in VideoPlayer open path: readyToPlay still emits a QString URL/path.
//
