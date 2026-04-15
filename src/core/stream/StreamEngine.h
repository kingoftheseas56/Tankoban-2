#pragma once

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QTimer>

#include "addon/StreamInfo.h"

class TorrentEngine;
class StreamHttpServer;

enum class StreamPlaybackMode {
    LocalHttp, // torrent-backed (magnet) path served via local HTTP
    DirectUrl, // addon-provided direct URL (http/https/url kinds)
};

struct StreamFileResult {
    bool ok = false;
    StreamPlaybackMode playbackMode = StreamPlaybackMode::LocalHttp;
    QString url;                // http://127.0.0.1:{port}/stream/{hash}/{fileIndex} OR direct URL
    QString infoHash;           // canonical 40-char hex hash from libtorrent (magnet only)
    QString errorCode;          // METADATA_NOT_READY, FILE_NOT_READY, UNKNOWN_TORRENT, ENGINE_ERROR, UNSUPPORTED_SOURCE
    QString errorMessage;
    bool readyToStart = false;
    bool queued = false;
    double fileProgress = 0.0;  // 0.0 - 1.0
    qint64 downloadedBytes = 0;
    qint64 fileSize = 0;
    int selectedFileIndex = -1;
    QString selectedFileName;
};

struct StreamTorrentStatus {
    int peers = 0;
    int dlSpeed = 0;            // bytes/sec
};

class StreamEngine : public QObject
{
    Q_OBJECT

public:
    explicit StreamEngine(TorrentEngine* engine, const QString& cacheDir,
                          QObject* parent = nullptr);
    ~StreamEngine() override;

    // Start/stop the HTTP server
    bool start();
    void stop();
    int httpPort() const;

    // Main streaming API — caller polls this until ok==true.
    // Magnet path: caller polls until ok==true and URL is a local HTTP URL.
    // Direct/HTTP path: returns immediately with ok==true.
    StreamFileResult streamFile(const QString& magnetUri,
                                int fileIndex = -1,
                                const QString& fileNameHint = {});

    // Phase 4.3 addon-aware overload: dispatches by source kind.
    // Magnet → delegates to the magnet streamFile above.
    // Url/Http → immediate DirectUrl result (skips torrent path).
    // YouTube → UNSUPPORTED_SOURCE error.
    StreamFileResult streamFile(const tankostream::addon::Stream& stream);

    // Stop and clean up a stream
    void stopStream(const QString& infoHash);

    // Stop all active streams
    void stopAll();

    // Query torrent status for UI
    StreamTorrentStatus torrentStatus(const QString& infoHash) const;

    // STREAM_PLAYBACK_FIX Phase 2 Batch 2.3 — sliding-window deadline
    // retargeting. Called from the StreamPage progressUpdated lambda,
    // rate-limited by the caller (currently once per 2s). Looks up the
    // stream's selected file size + index, converts positionSec to a
    // byte offset, and asks TorrentEngine for deadlines across the next
    // ~windowBytes of pieces. Cheap + idempotent: re-calling with the
    // same position is a no-op on libtorrent's side (same piece, same
    // deadline → update in place).
    void updatePlaybackWindow(const QString& infoHash,
                              double positionSec, double durationSec,
                              qint64 windowBytes = 20LL * 1024 * 1024);

    // Pair with updatePlaybackWindow. Called on player close / back to
    // browse / source switch so libtorrent isn't pre-fetching ahead of a
    // playback position that no longer exists. Safe to call on an unknown
    // infoHash.
    void clearPlaybackWindow(const QString& infoHash);

    // STREAM_PLAYBACK_FIX Phase 2 Batch 2.4 — seek/resume target pre-gate.
    // Called by StreamPage before handing a resume offset to the player.
    // Translates (positionSec / durationSec) to a byte offset, sets urgent
    // (200ms→500ms) deadlines on the first `prefetchBytes` worth of pieces
    // around that offset, and returns whether those pieces are already
    // contiguous. If true, caller opens the player immediately; if false,
    // caller shows a "Seeking..." overlay and retries (either via
    // singleShot or by re-calling this function). Fires the deadline set
    // on every call so the caller's retry cadence keeps libtorrent's
    // urgency up to date without saturating it.
    bool prepareSeekTarget(const QString& infoHash,
                           double positionSec, double durationSec,
                           qint64 prefetchBytes = 3LL * 1024 * 1024);

    // Clean up orphaned cache data from previous sessions
    void cleanupOrphans();

    // Start periodic cleanup timer (call once after start)
    void startPeriodicCleanup();

signals:
    void streamReady(const QString& infoHash, const QString& url);
    void streamError(const QString& infoHash, const QString& message);

private slots:
    void onMetadataReady(const QString& infoHash, const QString& name,
                         qint64 totalSize, const QJsonArray& files);
    void onTorrentProgress(const QString& infoHash, float progress,
                           int dlSpeed, int ulSpeed, int peers, int seeds);
    void onTorrentError(const QString& infoHash, const QString& message);

private:
    struct StreamRecord {
        QString infoHash;
        QString magnetUri;
        QString savePath;
        int requestedFileIndex = -1;    // from caller, -1 = auto-select
        QString fileNameHint;
        int selectedFileIndex = -1;     // resolved after metadata
        QString selectedFileName;
        qint64 selectedFileSize = 0;
        bool metadataReady = false;
        bool registered = false;        // registered with HTTP server
        int peers = 0;
        int dlSpeed = 0;
    };

    int autoSelectVideoFile(const QJsonArray& files, const QString& hint) const;
    QString buildStreamUrl(const QString& infoHash, int fileIndex) const;
    void applyStreamPriorities(const QString& infoHash, int fileIndex, int totalFiles);

    static bool isVideoExtension(const QString& path);

    TorrentEngine* m_torrentEngine;
    StreamHttpServer* m_httpServer;
    QString m_cacheDir;
    QTimer* m_cleanupTimer = nullptr;

    mutable QMutex m_mutex;
    QHash<QString, StreamRecord> m_streams;
};
