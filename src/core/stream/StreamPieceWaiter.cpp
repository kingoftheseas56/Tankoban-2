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

constexpr int kPollIntervalMs = 200;   // fallback polling cadence (matches
                                       // the pre-rebuild waitForPieces loop)
constexpr int kWakeWaitCapMs  = 1000;  // cap one QWaitCondition::wait call
                                       // so cancellation + overall timeout
                                       // progress on a bounded cadence even
                                       // if pieceFinished never fires.

// STREAM_PIECE_WAITER_POLL=1 at process start forces async-disable.
bool g_pollFallback = qgetenv("STREAM_PIECE_WAITER_POLL") == "1";

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
                   bool ok, bool cancelled, const char* mode)
{
    if (!g_telemetryEnabled) return;
    const QString line = QStringLiteral("[")
        + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + QStringLiteral("] event=piece_wait hash=") + hash.left(8)
        + QStringLiteral(" piece=") + QString::number(pieceIdx)
        + QStringLiteral(" elapsedMs=") + QString::number(elapsedMs)
        + QStringLiteral(" ok=") + (ok ? QStringLiteral("1") : QStringLiteral("0"))
        + QStringLiteral(" cancelled=") + (cancelled ? QStringLiteral("1") : QStringLiteral("0"))
        + QStringLiteral(" mode=") + QString::fromLatin1(mode)
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
    , m_pollFallback(g_pollFallback)
{
    if (m_engine) {
        // AutoConnection from AlertWorker (QThread) to this main-thread
        // QObject resolves to QueuedConnection. onPieceFinished runs on the
        // main thread, so touching m_waiters + waking conditions is safe
        // against worker threads blocked in waitForPiece.
        connect(m_engine, &TorrentEngine::pieceFinished,
                this, &StreamPieceWaiter::onPieceFinished);
    }

    if (m_pollFallback) {
        qInfo() << "StreamPieceWaiter: STREAM_PIECE_WAITER_POLL=1"
                << "— async-wake disabled, 200ms poll mode";
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
            emitPieceWait(infoHash, firstWaitedPiece, t.elapsed(),
                          false, true, m_pollFallback ? "poll" : "async");
            return false;
        }

        if (m_engine->haveContiguousBytes(infoHash, fileIndex,
                                          fileOffset, length)) {
            emitPieceWait(infoHash, firstWaitedPiece, t.elapsed(),
                          true, false, m_pollFallback ? "poll" : "async");
            return true;
        }

        // Resolve the piece range covering [fileOffset, fileOffset+length).
        // pieceRangeForFileOffset returns {-1,-1} on unknown torrent / file
        // / bad offset; treat that as "no piece to bind to" → short sleep,
        // loop, and let cancellation or havebytes drive the exit.
        const QPair<int, int> range = m_engine->pieceRangeForFileOffset(
            infoHash, fileIndex, fileOffset, length);
        if (range.first < 0 || range.second < range.first) {
            QThread::msleep(kPollIntervalMs);
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

        const qint64 remaining =
            static_cast<qint64>(timeoutMs) - t.elapsed();
        if (remaining <= 0) break;
        const int waitMs = static_cast<int>(qMin<qint64>(
            m_pollFallback ? kPollIntervalMs : kWakeWaitCapMs, remaining));

        if (m_pollFallback) {
            QThread::msleep(waitMs);
        } else {
            waitForPiece(infoHash, missingPiece, waitMs);
        }
    }

    emitPieceWait(infoHash, firstWaitedPiece, t.elapsed(),
                  false, false, m_pollFallback ? "poll" : "async");
    return false;
}

void StreamPieceWaiter::waitForPiece(const QString& infoHash, int pieceIdx,
                                      int timeoutMs)
{
    const Key key{infoHash, pieceIdx};
    Waiter w;

    QMutexLocker lock(&m_mutex);
    m_waiters[key].append(&w);

    // QWaitCondition::wait releases m_mutex for the duration, re-acquires on
    // return. Returns when cond.wakeAll() is called (onPieceFinished or the
    // dtor) or timeoutMs expires. Either way the caller re-probes engine
    // state; we don't inspect w.awakened here.
    w.cond.wait(&m_mutex, static_cast<unsigned long>(timeoutMs));

    auto it = m_waiters.find(key);
    if (it != m_waiters.end()) {
        it->removeOne(&w);
        if (it->isEmpty()) m_waiters.erase(it);
    }
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
