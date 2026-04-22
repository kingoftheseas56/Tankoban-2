#include "StreamPieceWaiter.h"
#include "StreamTelemetryWriter.h"

#include "core/torrent/TorrentEngine.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QThread>

namespace {

constexpr int kWakeWaitCapMs  = 1000;  // cap one QWaitCondition::wait call
                                       // so cancellation + overall timeout
                                       // progress on a bounded cadence even
                                       // if pieceFinished never fires.

// Mirror StreamEngine.cpp's telemetry facility locally so piece_wait events
// share the same background append writer. Cadence-gated by env var; zero
// cost when disabled.
void emitPieceWait(const QString& hash, int pieceIdx, qint64 elapsedMs,
                   bool ok, bool cancelled)
{
    if (!streamTelemetryEnabled()) return;
    const QString line = QStringLiteral("[")
        + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + QStringLiteral("] event=piece_wait hash=") + hash.left(8)
        + QStringLiteral(" piece=") + QString::number(pieceIdx)
        + QStringLiteral(" elapsedMs=") + QString::number(elapsedMs)
        + QStringLiteral(" ok=") + (ok ? QStringLiteral("1") : QStringLiteral("0"))
        + QStringLiteral(" cancelled=") + (cancelled ? QStringLiteral("1") : QStringLiteral("0"))
        + QStringLiteral("\n");
    appendStreamTelemetryLine(line);
}

}  // namespace

StreamPieceWaiter::StreamPieceWaiter(TorrentEngine* engine, QObject* parent)
    : QObject(parent)
    , m_engine(engine)
{
    m_clock.start();
    if (m_engine) {
        // AutoConnection from AlertWorker (QThread) to this main-thread
        // QObject resolves to QueuedConnection. onPieceFinished runs on the
        // main thread, so touching m_waiters + waking conditions is safe
        // against worker threads blocked in waitForPiece.
        connect(m_engine, &TorrentEngine::pieceFinished,
                this, &StreamPieceWaiter::onPieceFinished);
    }
}

StreamPieceWaiter::~StreamPieceWaiter()
{
    // Wake any in-flight waiters so the worker threads exit their
    // QWaitCondition::wait before destruction proceeds. Workers re-probe
    // the engine on wake; on StreamEngine teardown haveContiguousBytes
    // either returns false (torrent gone) or the cancellation token fires,
    // so workers fall to timeout / return false cleanly.
    QMutexLocker lock(&m_mutex);
    for (auto& waiters : m_waiters) {
        for (auto* w : waiters) {
            w->awakened = true;
            w->cond.wakeAll();
        }
    }
    m_waiters.clear();
    m_firstSeenMs.clear();
}

bool StreamPieceWaiter::awaitRange(const QString& infoHash, int fileIndex,
                                    qint64 fileOffset, qint64 length,
                                    int timeoutMs,
                                    const std::shared_ptr<std::atomic<bool>>& cancelled)
{
    if (!m_engine || timeoutMs <= 0) return false;

    QElapsedTimer t;
    t.start();
    int firstWaitedPiece = -1;

    while (t.elapsed() < timeoutMs) {
        const bool wasCancelled =
            cancelled && cancelled->load(std::memory_order_acquire);
        if (wasCancelled) {
            emitPieceWait(infoHash, firstWaitedPiece, t.elapsed(), false, true);
            return false;
        }

        if (m_engine->haveContiguousBytes(infoHash, fileIndex,
                                          fileOffset, length)) {
            emitPieceWait(infoHash, firstWaitedPiece, t.elapsed(), true, false);
            return true;
        }

        // Resolve the piece range covering [fileOffset, fileOffset+length).
        // pieceRangeForFileOffset returns {-1,-1} on unknown torrent / file
        // / bad offset; treat that as "no piece to bind to" → short sleep,
        // loop, and let cancellation or havebytes drive the exit.
        const QPair<int, int> range = m_engine->pieceRangeForFileOffset(
            infoHash, fileIndex, fileOffset, length);
        if (range.first < 0 || range.second < range.first) {
            QThread::msleep(200);  // bad-offset retry — rare, small sleep
            continue;
        }

        int missingPiece = -1;
        for (int p = range.first; p <= range.second; ++p) {
            if (!m_engine->havePiece(infoHash, p)) {
                missingPiece = p;
                break;
            }
        }

        // Race: the piece became available between haveContiguousBytes
        // returning false and this scan. Loop — the next haveContiguousBytes
        // call picks it up.
        if (missingPiece < 0) continue;

        if (firstWaitedPiece < 0) firstWaitedPiece = missingPiece;

        // STREAM_ENGINE_REBUILD 2026-04-19 — on-demand time-critical
        // marking. Any piece an HTTP worker is actually blocked on becomes
        // time-critical in libtorrent on the spot — priority=7 + 40 ms
        // deadline. Closes the gap diagnosed via piece_diag where sidecar
        // reads ahead of playback position (buffer-read, moov probe, or
        // untracked seek) to a piece that is NOT yet in the Prioritizer's
        // output window, so has no time-critical signal set. Without this,
        // the piece gets scheduled as a regular sequential-download piece
        // and trickles in at 30+ s wait, even though a simple "peer has
        // it, priority=7, deadline=40 ms" signal would resolve it in
        // 1-3 s. Idempotent: setPiecePriority(same value) + setPieceDeadlines
        // (updates in place) are both safe on repeat calls. Fires at most
        // once per wake-wait cycle (~1 s) per blocked (hash, piece). Small
        // per-call cost (two cross-thread API hops); acceptable for the
        // rare "waiter registered" path.
        m_engine->setPiecePriority(infoHash, missingPiece, 7);
        m_engine->setPieceDeadlines(infoHash, {{ missingPiece, 40 }});

        const qint64 remaining =
            static_cast<qint64>(timeoutMs) - t.elapsed();
        if (remaining <= 0) break;
        const int waitMs = static_cast<int>(
            qMin<qint64>(kWakeWaitCapMs, remaining));

        waitForPiece(infoHash, missingPiece, waitMs);
    }

    emitPieceWait(infoHash, firstWaitedPiece, t.elapsed(), false, false);
    return false;
}

void StreamPieceWaiter::waitForPiece(const QString& infoHash, int pieceIdx,
                                      int timeoutMs)
{
    const Key key{infoHash, pieceIdx};
    Waiter w;

    QMutexLocker lock(&m_mutex);
    auto& waiters = m_waiters[key];
    waiters.append(&w);
    // STREAM_STALL_WATCHDOG_FIX 2026-04-21 — contains-check preserves the
    // ORIGINAL first-seen timestamp across awaitRange's wake-wait cycles.
    // Prior implementation keyed on `firstForKey = waiters.isEmpty()` BEFORE
    // append + unconditional insert, then removed the entry whenever the
    // last Waiter unregistered on timeout. Because awaitRange's loop calls
    // waitForPiece(piece, kWakeWaitCapMs=1000ms), each timeout-and-reprobe
    // cycle briefly drained the waiter list, which removed m_firstSeenMs,
    // which a fresh insertion reset to now. Net effect: continuous-wait
    // tracking maxed out at 1 s even when a single piece had been blocked
    // for 30+ s, so longestActiveWait() never exceeded the stall_detected
    // 4 s threshold. Mid-playback piece_waits of 6-8 s routinely observed
    // in telemetry with ZERO matching stall_detected events despite the
    // threshold. See agents/chat.md 2026-04-21 20:xx Agent 4 diagnosis
    // (90 piece_wait >4 s events vs 5 stall_detected today, only
    // cold-open).
    if (!m_firstSeenMs.contains(key)) {
        m_firstSeenMs.insert(key, m_clock.elapsed());
    }

    // QWaitCondition::wait releases m_mutex for the duration, re-acquires on
    // return. Returns when cond.wakeAll() is called (onPieceFinished or the
    // dtor) or timeoutMs expires. Either way the caller re-probes engine
    // state; we don't inspect w.awakened here.
    w.cond.wait(&m_mutex, static_cast<unsigned long>(timeoutMs));

    auto it = m_waiters.find(key);
    if (it != m_waiters.end()) {
        it->removeOne(&w);
        if (it->isEmpty()) {
            m_waiters.erase(it);
            // STREAM_STALL_WATCHDOG_FIX 2026-04-21 — intentionally do NOT
            // remove m_firstSeenMs here. Entry is now cleared on piece
            // arrival (onPieceFinished) or stream teardown (untrackStream),
            // which are the two events that semantically end a "continuous
            // wait on piece X". The previous timeout-driven removal was
            // the proximate cause of the watchdog-never-fires bug.
        }
    }
}

StreamPieceWaiter::LongestWait StreamPieceWaiter::longestActiveWait() const
{
    LongestWait result;
    QMutexLocker lock(&m_mutex);
    if (m_firstSeenMs.isEmpty()) return result;

    // Walk m_firstSeenMs — one entry per continuously-waited (hash, piece)
    // across Waiter create/destroy cycles. Elapsed duration here is the
    // TRUE continuous wait, not the per-Waiter cond.wait() slice.
    const qint64 now = m_clock.elapsed();
    for (auto it = m_firstSeenMs.constBegin(); it != m_firstSeenMs.constEnd(); ++it) {
        // STREAM_STALL_WATCHDOG_PREFETCH_FIX 2026-04-21 — only report a
        // piece as "blocking" if at least one awaitRange worker is currently
        // registered in m_waiters for this key. Without this filter, zombie
        // m_firstSeenMs entries (abandoned by an awaitRange call that exited
        // without the piece arriving — typical for prefetch windows the
        // demuxer moved past) dominate longestActiveWait forever, causing
        // stall_detected to latch on a prefetch piece far ahead of current
        // playback. The UI then shows "Buffering — waiting for piece N"
        // while video continues rendering a closer piece's frames.
        //
        // Safety: awaitRange's wake-wait loop briefly drains the waiter
        // list between cond.wait timeout and the next waitForPiece call
        // (microseconds under mutex-free re-probe). The 2 s stall watchdog
        // tick could theoretically sample during that gap and miss a live
        // wait; empirically the gap is sub-millisecond and even if a tick
        // collides, the next tick 2 s later catches the same wait (firstSeenMs
        // persists across those cycles per the 2026-04-21 fix above).
        auto wit = m_waiters.find(it.key());
        if (wit == m_waiters.end() || wit->isEmpty()) continue;

        const qint64 elapsed = now - it.value();
        if (elapsed > result.elapsedMs) {
            result.elapsedMs  = elapsed;
            result.infoHash   = it.key().first;
            result.pieceIndex = it.key().second;
        }
    }
    return result;
}

void StreamPieceWaiter::onPieceFinished(const QString& infoHash, int pieceIndex)
{
    const Key key{infoHash, pieceIndex};
    QMutexLocker lock(&m_mutex);

    // STREAM_STALL_WATCHDOG_FIX 2026-04-21 — piece arrived, so any
    // continuous-wait tracking on this (hash, piece) is semantically over.
    // Clear firstSeenMs here (the move from waitForPiece's waiter-drain
    // path). Safe even if m_waiters has no entry for this key — a piece
    // may finish before any worker has actually registered (e.g. priority
    // 7 + deadline 40 ms resolves in under one HTTP serving loop cycle).
    m_firstSeenMs.remove(key);

    auto it = m_waiters.find(key);
    if (it == m_waiters.end()) return;

    // Wake every waiter on this piece. Stale wakes (engine state moved
    // past the wake by the time the worker re-probes) are harmless — the
    // worker's re-probe sees contiguous bytes OR a different first-missing
    // piece and binds to that.
    for (auto* w : *it) {
        w->awakened = true;
        w->cond.wakeAll();
    }
}

void StreamPieceWaiter::untrackStream(const QString& infoHash)
{
    // STREAM_STALL_WATCHDOG_FIX 2026-04-21 — called by StreamEngine::stopStream
    // to purge continuous-wait entries for a torn-down session. Without this,
    // m_firstSeenMs entries for pieces that were blocked at stream-stop time
    // persist (piece never arrives → no onPieceFinished → entry stays), and
    // longestActiveWait() reads stale elapsed from them. The stall watchdog's
    // session-lookup filter (`state == Serving`) means a stale entry for a
    // gone session can't trigger a stall, but it CAN shadow a live shorter
    // wait on a different session and hide a legitimate mid-playback stall
    // if the stale entry wins the "longest" contest.
    QMutexLocker lock(&m_mutex);
    auto it = m_firstSeenMs.begin();
    while (it != m_firstSeenMs.end()) {
        if (it.key().first == infoHash) {
            it = m_firstSeenMs.erase(it);
        } else {
            ++it;
        }
    }
}
