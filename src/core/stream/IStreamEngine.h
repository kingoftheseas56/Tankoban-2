#pragma once

// STREAM_SERVER_PIVOT Phase 1 (2026-04-24) — abstract interface for the
// stream engine. Captures the method + signal surface StreamPage and
// StreamPlayerController consume so we can swap the backing implementation
// between the legacy libtorrent-based `StreamEngine` (src/core/stream/
// StreamEngine.h) and the new Stremio-stream-server-subprocess-based
// `StreamServerEngine` (src/core/stream/stremio/StreamServerEngine.h) behind
// a single env-gated branch at `StreamPage.cpp:93`.
//
// Signals can't live on a non-Q_OBJECT interface (moc can't generate them on
// a pure virtual), so both concrete implementations declare the 4 signals
// independently with identical signatures. Callers use the SIGNAL-macro
// string form `connect(engine->asQObject(), SIGNAL(stallDetected(...)), ...)`
// which does runtime string matching and works regardless of concrete type.
//
// Phase 2 (separate wake, post-pivot-smoke-green) will delete StreamEngine +
// StreamHttpServer + StreamPieceWaiter + StreamPrioritizer + StreamSeekClassifier
// wholesale, remove the env flag, and collapse StreamServerEngine inline to
// replace IStreamEngine. This file goes away with that collapse.

#include <QList>
#include <QPair>
#include <QString>
#include <atomic>
#include <memory>

#include "addon/StreamInfo.h"

struct StreamFileResult;       // defined in StreamEngine.h
struct StreamTorrentStatus;    // defined in StreamEngine.h
struct StreamEngineStats;      // defined in StreamEngine.h
class QObject;

class IStreamEngine {
public:
    virtual ~IStreamEngine() = default;

    // ── Engine lifecycle ────────────────────────────────────────────────
    // Legacy StreamEngine: starts localhost HTTP server for serving stream
    // bytes. StreamServerEngine: spawns stremio-runtime.exe + server.js
    // subprocess, waits for "EngineFS server started at …" stdout signal.
    virtual bool start() = 0;
    virtual void stop()  = 0;

    // Cache hygiene. Legacy: cleans up orphaned .part files + infohashes
    // whose .tankotorrent records were lost. StreamServerEngine: may
    // delete stream-server's cache dir on ungraceful prior shutdown.
    virtual void cleanupOrphans() = 0;
    virtual void startPeriodicCleanup() = 0;

    // ── Streaming API (polled by StreamPlayerController) ───────────────
    virtual StreamFileResult streamFile(const QString& magnetUri,
                                         int fileIndex = -1,
                                         const QString& fileNameHint = {}) = 0;

    virtual StreamFileResult streamFile(
        const tankostream::addon::Stream& stream) = 0;

    virtual void stopStream(const QString& infoHash) = 0;
    virtual void stopAll() = 0;

    virtual StreamTorrentStatus torrentStatus(const QString& infoHash) const = 0;
    virtual StreamEngineStats statsSnapshot(const QString& infoHash) const = 0;

    virtual QList<QPair<qint64, qint64>> contiguousHaveRanges(
        const QString& infoHash) const = 0;

    // Playback-window hinting. Legacy: sets libtorrent deadlines on the
    // next windowBytes of pieces. StreamServerEngine: no-op (stream-server
    // auto-manages prefetch via Range GETs + its own priority picker).
    virtual void updatePlaybackWindow(const QString& infoHash,
                                       double positionSec,
                                       double durationSec,
                                       qint64 windowBytes = 20LL * 1024 * 1024) = 0;
    virtual void clearPlaybackWindow(const QString& infoHash) = 0;

    // Seek pre-gate. Legacy: sets urgent deadlines on seek-target pieces,
    // returns true iff contiguous bytes already available. StreamServerEngine:
    // returns true unconditionally (Range GETs are always allowed).
    virtual bool prepareSeekTarget(const QString& infoHash,
                                    double positionSec,
                                    double durationSec,
                                    qint64 prefetchBytes = 3LL * 1024 * 1024) = 0;

    // Per-stream cancellation token. Flipped true by stopStream so workers
    // that captured the shared_ptr before the session record was erased
    // still observe cancellation. Empty shared_ptr on unknown infoHash.
    virtual std::shared_ptr<std::atomic<bool>> cancellationToken(
        const QString& infoHash) const = 0;

    // ── Signal routing helper ──────────────────────────────────────────
    // Both concrete implementations are Q_OBJECT subclasses and emit
    // streamReady / streamError / stallDetected / stallRecovered with
    // identical signatures. Callers use string-macro connects via this
    // helper: connect(engine->asQObject(), SIGNAL(streamReady(QString,QString)),
    //                  this, SLOT(onStreamReady(QString,QString))).
    virtual QObject* asQObject() = 0;
};
