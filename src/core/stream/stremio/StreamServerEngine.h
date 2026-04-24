#pragma once

// STREAM_SERVER_PIVOT Phase 1 (2026-04-24) — concrete implementation of
// IStreamEngine backed by a Stremio stream-server subprocess. Owns a
// `StreamServerProcess` (lifecycle) + `StreamServerClient` (REST). Per-hash
// state tracks what torrents we've asked stream-server to start, their
// selected file index, their last-known stats snapshot (for sync answers to
// StreamPage's statsSnapshot polls), and their cancellation token.
//
// Threading: main-thread only. All REST callbacks return to main thread
// (QNetworkAccessManager signal is queued-connected when needed). Method
// calls from StreamPlayerController's poll timer and StreamPage's signal
// lambdas are also main-thread.
//
// Phase 2B additions (2026-04-24):
//   - stream_telemetry.log parity — writeTelemetry() emits engine_started /
//     metadata_ready / file_selected / first_piece / cancelled / stopped
//     events in the same line format as legacy StreamEngine so the existing
//     runtime-health parser works unchanged.
//   - contiguousHaveRanges — populated from /:hash/:idx/stats.json's
//     streamProgress fraction. Head-contiguous approximation only (no
//     per-piece bitfield exposed by stream-server); under-paints during
//     mid-file-seek transients.
//
// What's still NOT implemented (deferred):
//   - stallDetected / stallRecovered signals — never emitted. Aligns with
//     the text-free indeterminate LoadingOverlay we shipped Phase 1.
//     Could be derived from dlSpeed=0 over N seconds if needed.
//   - updatePlaybackWindow / prepareSeekTarget — no-ops (stream-server
//     auto-manages prefetch via Range GET behavior).

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>

#include "core/stream/IStreamEngine.h"
#include "core/stream/StreamEngine.h"   // for StreamFileResult / StreamEngineStats / StreamTorrentStatus

class StreamServerProcess;
class StreamServerClient;

class StreamServerEngine : public QObject, public IStreamEngine {
    Q_OBJECT

public:
    explicit StreamServerEngine(const QString& cacheDir,
                                 QObject* parent = nullptr);
    ~StreamServerEngine() override;

    // IStreamEngine overrides ──────────────────────────────────────────
    bool start() override;
    void stop() override;
    void cleanupOrphans() override;
    void startPeriodicCleanup() override;

    StreamFileResult streamFile(const QString& magnetUri,
                                 int fileIndex = -1,
                                 const QString& fileNameHint = {}) override;
    StreamFileResult streamFile(
        const tankostream::addon::Stream& stream) override;

    void stopStream(const QString& infoHash) override;
    void stopAll() override;

    StreamTorrentStatus torrentStatus(const QString& infoHash) const override;
    StreamEngineStats statsSnapshot(const QString& infoHash) const override;

    QList<QPair<qint64, qint64>> contiguousHaveRanges(
        const QString& infoHash) const override;

    void updatePlaybackWindow(const QString& infoHash,
                               double positionSec,
                               double durationSec,
                               qint64 windowBytes = 20LL * 1024 * 1024) override;
    void clearPlaybackWindow(const QString& infoHash) override;

    bool prepareSeekTarget(const QString& infoHash,
                            double positionSec,
                            double durationSec,
                            qint64 prefetchBytes = 3LL * 1024 * 1024) override;

    std::shared_ptr<std::atomic<bool>> cancellationToken(
        const QString& infoHash) const override;

    QObject* asQObject() override { return this; }

signals:
    // Identical shape to StreamEngine's signals (StreamEngine.h:252-271) so
    // StreamPage's SIGNAL-macro connect-strings match both types.
    void streamReady(const QString& infoHash, const QString& url);
    void streamError(const QString& infoHash, const QString& message);
    void stallDetected(const QString& infoHash, int piece,
                        qint64 waitMs, int peerHaveCount);
    void stallRecovered(const QString& infoHash, int piece,
                         qint64 elapsedMs, const QString& via);

private slots:
    void onProcessReady(int port);
    void onProcessCrashed(int exitCode);
    void onProcessError(const QString& message);

private:
    // Per-stream in-flight state. One instance per infoHash we've asked
    // stream-server to start. Populated lazily on first streamFile() call.
    struct Context {
        QString      magnetUri;
        QString      fileNameHint;
        int          requestedFileIndex = -1;   // caller's hint; -1 = auto-select
        int          selectedFileIndex  = -1;   // resolved from /stats.json .files
        qint64       selectedFileSize   = 0;
        QString      selectedFileName;
        qint64       downloadedBytes    = 0;
        double       streamProgress     = 0.0;    // [0.0, 1.0] fraction from /:hash/:idx/stats.json
        bool         createSent         = false;
        bool         createCompleted    = false;  // /create callback fired
        bool         readyEmitted       = false;  // streamReady already emitted
        bool         streamReadyAnnounced = false; // readyToStart flipped in last streamFile return
        // Phase 2B telemetry one-shot latches — avoid re-emitting events
        // every poll tick.
        bool         telemMetadataReady    = false;
        bool         telemFileSelected     = false;
        bool         telemFirstPiece       = false;
        bool         telemCancelledOrStopped = false;
        QString      lastError;
        QJsonObject  lastStats;
        std::shared_ptr<std::atomic<bool>> cancelToken;
    };

    // Select the "best" video file from a stream-server /create or /stats.json
    // .files array. Honors caller hint (by filename substring match) + falls
    // back to largest-video-extension heuristic. -1 if none suitable.
    int pickFileIndex(const QJsonObject& stats,
                      const QString& hint,
                      int callerOverride) const;

    // Populate selectedFileIndex/Size/Name from stats.json into ctx.
    // Returns true if fields are now non-sentinel.
    bool resolveSelectedFile(Context& ctx, const QJsonObject& stats) const;

    // Kick off a fresh /stats.json fetch for this hash; results land in
    // ctx.lastStats + may flip ctx.selectedFileIndex + ctx.readyEmitted.
    // Also chains a /:hash/:idx/stats.json fetch once selectedFileIndex is
    // resolved, to populate ctx.streamProgress + ctx.downloadedBytes from
    // the per-file bitfield fraction (Phase 2B; the generic /stats.json
    // response has no per-file downloaded bytes).
    void refreshStats(const QString& infoHash);

    // Phase 2B — write a legacy-telemetry-format line to
    // stream_telemetry.log. Same file + env-gate + line shape as
    // StreamEngine::writeTelemetry (StreamEngine.cpp:50-59) so existing
    // log parsers work unchanged. Cheap when env is off.
    void writeTelemetry(const QString& event, const QString& body) const;

    QString m_cacheDir;
    StreamServerProcess* m_process = nullptr;
    StreamServerClient*  m_client  = nullptr;

    mutable QHash<QString, Context> m_contexts;

    bool m_started = false;
    bool m_processReady = false;
    QString m_startupError;

    // 1MB threshold at which we flip readyToStart=true + emit streamReady
    // once stream-server has streamed enough bytes of the selected file
    // that ffmpeg Tier-1 probe (probesize=524288) can succeed.
    static constexpr qint64 kReadyByteThreshold = 1LL * 1024 * 1024;
};
