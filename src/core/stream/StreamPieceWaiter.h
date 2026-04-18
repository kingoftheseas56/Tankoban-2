#pragma once

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QString>
#include <QWaitCondition>

#include <atomic>
#include <memory>

class TorrentEngine;

// STREAM_ENGINE_REBUILD P2 — notification-driven piece-wait primitive.
//
// Replaces the 15 s poll floor at StreamHttpServer::waitForPieces with a
// QWaitCondition-driven wake on TorrentEngine::pieceFinished. Cold-start
// wait-floor drops from PIECE_WAIT_POLL_MS=200 ms × up to 75 iterations
// (15 s ceiling) to alert-pump latency (≤ 250 ms on the current AlertWorker
// cadence; → ≤ 5–25 ms once M2 tightens wait_for_alert at TorrentEngine.cpp:52
// in a separate post-audit commit per integration-memo §5).
//
// Fallback: STREAM_PIECE_WAITER_POLL=1 at process start forces polling mode —
// same 200 ms cadence as the pre-rebuild loop. Rollback safety valve during
// the P2 soak window; removed in P6 per STREAM_ENGINE_REBUILD_TODO.md §6.1.
//
// Per-piece wake registry: QHash<(hash, pieceIdx), QList<Waiter*>> guarded
// by m_mutex. The `pieceFinished` signal slot runs on the main thread (the
// waiter is a main-thread QObject; AutoConnection from AlertWorker's QThread
// resolves to QueuedConnection). Worker threads calling awaitRange block on
// a Waiter's QWaitCondition — that cond is woken either by the main-thread
// slot or by the wait's own timeout.
//
// M1 lock-ordering call (§5 of agents/audits/congress6_integration_2026-04-18.md):
// KEEP TorrentEngine::m_mutex as a single mutex. StreamPieceWaiter holds its
// own m_mutex ONLY while registering / deregistering / waking Waiters, never
// simultaneously with any TorrentEngine lock — every engine call
// (haveContiguousBytes / pieceRangeForFileOffset / havePiece) happens from
// awaitRange before we register a waiter or after a wake, outside the local
// m_mutex scope. Zero cross-domain nesting. No demonstrated contention
// motivates a read-write lock or per-stream split; defer that to post-P6
// polish if contention ever materializes under load.
class StreamPieceWaiter : public QObject
{
    Q_OBJECT

public:
    explicit StreamPieceWaiter(TorrentEngine* engine, QObject* parent = nullptr);
    ~StreamPieceWaiter() override;

    // Wait for the file-byte range [fileOffset, fileOffset+length) to be
    // fully downloaded within timeoutMs. Returns true once
    // haveContiguousBytes reports the range is complete; false on timeout
    // or cancellation.
    //
    // Loop: probe haveContiguousBytes → if true return; probe cancellation
    // token → if set return false; compute the piece range covering the
    // byte range (pieceRangeForFileOffset); scan for the first missing
    // piece (havePiece); block on that piece's QWaitCondition for up to
    // min(remaining-timeout, 1 s) so cancellation is re-checked on a
    // predictable cadence even if the piece never arrives; repeat. The
    // 1 s cap prevents a stale waiter from outliving a shutdown path that
    // torn down the torrent without emitting further piece-finished alerts.
    //
    // cancelled: if non-null and its stored bool flips to true, the wait
    // aborts and returns false — matches the existing contract that
    // StreamEngine::cancellationToken fed into the poll-sleep.
    bool awaitRange(const QString& infoHash, int fileIndex,
                    qint64 fileOffset, qint64 length, int timeoutMs,
                    const std::shared_ptr<std::atomic<bool>>& cancelled);

private slots:
    void onPieceFinished(const QString& infoHash, int pieceIndex);

private:
    struct Waiter {
        QWaitCondition cond;
        bool awakened = false;
    };

    using Key = QPair<QString, int>;  // (hash, pieceIdx)

    // Blocking single-piece wait. Caller must NOT hold m_mutex.
    void waitForPiece(const QString& infoHash, int pieceIdx, int timeoutMs);

    TorrentEngine* m_engine;
    mutable QMutex m_mutex;
    QHash<Key, QList<Waiter*>> m_waiters;

    // Cached at ctor so tests / user overrides stay consistent across every
    // awaitRange call, even if the env var flips mid-run.
    const bool m_pollFallback;
};
