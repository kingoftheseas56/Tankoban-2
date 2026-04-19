#include "StreamPieceWaiter.h"

#include "core/torrent/TorrentEngine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMutexLocker>
#include <QTextStream>
#include <QThread>

namespace {

constexpr int kWakeWaitCapMs  = 1000;  // cap one QWaitCondition::wait call
                                       // so cancellation + overall timeout
                                       // progress on a bounded cadence even
                                       // if pieceFinished never fires.

// Mirror StreamEngine.cpp's telemetry facility locally so the piece_wait
// event short-circuits on TANKOBAN_STREAM_TELEMETRY=1 without leaking a
// writer across translation units. Cadence-gated by env var; zero cost
// when disabled (cached flag check before any lock).
bool g_telemetryEnabled = qgetenv("TANKOBAN_STREAM_TELEMETRY") == "1";
QMutex g_telemetryMutex;
QString g_telemetryPath;

QString resolveTelemetryPath()
{
    if (!g_telemetryPath.isEmpty()) return g_telemetryPath;
    QString dir = QCoreApplication::applicationDirPath();
    if (dir.isEmpty()) dir = QDir::currentPath();
    g_telemetryPath = dir + QStringLiteral("/stream_telemetry.log");
    return g_telemetryPath;
}

void emitPieceWait(const QString& hash, int pieceIdx, qint64 elapsedMs,
                   bool ok, bool cancelled)
{
    if (!g_telemetryEnabled) return;
    const QString line = QStringLiteral("[")
        + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + QStringLiteral("] event=piece_wait hash=") + hash.left(8)
        + QStringLiteral(" piece=") + QString::number(pieceIdx)
        + QStringLiteral(" elapsedMs=") + QString::number(elapsedMs)
        + QStringLiteral(" ok=") + (ok ? QStringLiteral("1") : QStringLiteral("0"))
        + QStringLiteral(" cancelled=") + (cancelled ? QStringLiteral("1") : QStringLiteral("0"))
        + QStringLiteral("\n");

    QMutexLocker lock(&g_telemetryMutex);
    QFile f(resolveTelemetryPath());
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream out(&f);
    out << line;
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
    const bool firstForKey = waiters.isEmpty();
    waiters.append(&w);
    if (firstForKey) {
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
            m_firstSeenMs.remove(key);
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
