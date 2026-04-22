#include "StreamHttpServer.h"
#include "StreamEngine.h"
#include "StreamPieceWaiter.h"
#include "core/torrent/TorrentEngine.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

#ifdef _WIN32
#  include <winsock2.h>
#  include <windows.h>
#endif

// ─── Content-Type map ────────────────────────────────────────────────────────

static const QHash<QString, QString>& contentTypeMap()
{
    static const QHash<QString, QString> map = {
        { "mp4",  "video/mp4" },
        { "m4v",  "video/x-m4v" },
        { "mkv",  "video/x-matroska" },
        { "webm", "video/webm" },
        { "avi",  "video/x-msvideo" },
        { "mov",  "video/quicktime" },
        { "wmv",  "video/x-ms-wmv" },
        { "flv",  "video/x-flv" },
        { "ts",   "video/mp2t" },
        { "m2ts", "video/mp2t" },
    };
    return map;
}

static QString contentTypeForPath(const QString& path)
{
    QString ext = QFileInfo(path).suffix().toLower();
    return contentTypeMap().value(ext, QStringLiteral("application/octet-stream"));
}

static QPair<qint64, qint64> parseRange(const QString& header, qint64 fileSize)
{
    QString h = header.trimmed();
    if (!h.startsWith("bytes=", Qt::CaseInsensitive))
        return { -1, -1 };
    h = h.mid(6);
    int dash = h.indexOf('-');
    if (dash < 0)
        return { -1, -1 };

    QString startStr = h.left(dash).trimmed();
    QString endStr = h.mid(dash + 1).trimmed();

    if (startStr.isEmpty()) {
        bool ok = false;
        qint64 suffix = endStr.toLongLong(&ok);
        if (!ok || suffix <= 0) return { -1, -1 };
        return { qMax(0LL, fileSize - suffix), fileSize - 1 };
    }

    bool okS = false;
    qint64 start = startStr.toLongLong(&okS);
    if (!okS || start < 0) return { -1, -1 };

    if (endStr.isEmpty())
        return { start, fileSize - 1 };

    bool okE = false;
    qint64 end = endStr.toLongLong(&okE);
    if (!okE) return { -1, -1 };
    return { start, qMin(end, fileSize - 1) };
}

static constexpr int CHUNK_SIZE = 256 * 1024;
static constexpr int PIECE_WAIT_TIMEOUT_MS = 15000;

// STREAM_ENGINE_REBUILD P2/P6 — per-chunk piece wait. Thin adapter over
// StreamPieceWaiter so the call site keeps the same shape as the pre-rebuild
// static waitForPieces (same 15 s timeout, same cancellation-token fast path,
// same bool return). Waiter is always wired post-P6; standalone-callers
// inline-poll fallback removed along with the STREAM_PIECE_WAITER_POLL
// env flag.
static bool waitForPiecesChunk(StreamPieceWaiter* waiter, TorrentEngine* engine,
                                const QString& infoHash, int fileIndex,
                                qint64 fileOffset, qint64 length,
                                const std::shared_ptr<std::atomic<bool>>& cancelled)
{
    if (!engine || !waiter) return false;
    return waiter->awaitRange(infoHash, fileIndex, fileOffset, length,
                              PIECE_WAIT_TIMEOUT_MS, cancelled);
}

// STREAM_PLAYBACK_FIX Batch 1.3 — RAII counter so shutdown drain sees
// connection exit regardless of which loop branch we break from. Also
// guarantees the socket closes cleanly (TCP FIN) even on early-return.
namespace {
struct ConnectionGuard {
    StreamHttpServer* server;
    QTcpSocket*       socket;
    explicit ConnectionGuard(StreamHttpServer* s, QTcpSocket* sk)
        : server(s), socket(sk) { if (server) server->connectionStarted(); }
    ~ConnectionGuard() {
        // Best-effort graceful close: disconnectFromHost queues a FIN, then
        // waitForDisconnected bounds how long we wait for the peer to ack
        // before the socket destructor force-closes on scope exit.
        if (socket && socket->state() != QAbstractSocket::UnconnectedState) {
            socket->disconnectFromHost();
            if (socket->state() != QAbstractSocket::UnconnectedState) {
                socket->waitForDisconnected(2000);
            }
        }
        if (server) server->connectionEnded();
    }
};
}  // namespace

static void handleConnection(qintptr socketDesc, StreamHttpServer* server)
{
    QTcpSocket socket;
    if (!socket.setSocketDescriptor(socketDesc))
        return;

    ConnectionGuard guard(server, &socket);

    // Early exit if a shutdown arrived between pendingConnection accept and
    // this thread being scheduled.
    if (server && server->isShuttingDown()) {
        return;
    }

    if (!socket.waitForReadyRead(5000)) {
        return;
    }

    // Read request line + headers
    QByteArray requestLine = socket.readLine(8192).trimmed();
    QHash<QString, QString> headers;
    while (socket.canReadLine()) {
        QByteArray line = socket.readLine(8192).trimmed();
        if (line.isEmpty()) break;
        int colon = line.indexOf(':');
        if (colon > 0) {
            headers[QString::fromLatin1(line.left(colon)).trimmed().toLower()]
                = QString::fromUtf8(line.mid(colon + 1)).trimmed();
        }
    }

    auto sendErr = [&](int code, const char* reason) {
        // STREAM_PLAYBACK_FIX Batch 1.2 — include Content-Type + Connection
        // on error responses. Some ffmpeg builds warn / retry unexpectedly
        // on type-less 4xx/5xx bodies; explicit Connection: close lets the
        // client tear down cleanly instead of sitting on a half-open socket.
        QByteArray r;
        r += "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
        r += "Content-Type: text/plain\r\n";
        r += "Content-Length: 0\r\n";
        r += "Connection: close\r\n\r\n";
        socket.write(r);
        socket.waitForBytesWritten(3000);
        socket.close();
    };

    QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) { sendErr(400, "Bad Request"); return; }

    QString method = QString::fromLatin1(parts[0]);
    if (method != "GET" && method != "HEAD") { sendErr(405, "Method Not Allowed"); return; }

    QStringList pathParts = QString::fromUtf8(parts[1]).split('/', Qt::SkipEmptyParts);
    if (pathParts.size() < 3 || pathParts[0] != "stream") { sendErr(404, "Not Found"); return; }

    QString infoHash = pathParts[1].toLower();
    bool ok = false;
    int fileIndex = pathParts[2].toInt(&ok);
    if (!ok) { sendErr(404, "Not Found"); return; }

    auto entry = server->lookupFile(infoHash, fileIndex);
    if (entry.path.isEmpty()) { sendErr(404, "Not Found"); return; }

    // STREAM_LIFECYCLE_FIX Phase 5 Batch 5.2 — grab the per-stream
    // cancellation token alongside the FileEntry. Captured in a local so
    // the shared_ptr stays alive for the duration of this worker (and
    // every waitForPiecesChunk call within the serve loop below). If
    // StreamEngine isn't wired (standalone server), token is empty and
    // waitForPiecesChunk falls through to pre-5.2 behavior.
    //
    // STREAM_ENGINE_REBUILD P2 — same idea for the shared StreamPieceWaiter.
    // Non-null in the StreamEngine-wired path (the engine creates the
    // waiter in its ctor); nullptr for standalone callers, in which case
    // waitForPiecesChunk falls through to the inline 200 ms poll.
    std::shared_ptr<std::atomic<bool>> cancelled;
    StreamPieceWaiter* pieceWaiter = nullptr;
    if (auto* se = server->streamEngine()) {
        cancelled = se->cancellationToken(infoHash);
        pieceWaiter = se->pieceWaiter();
    }

    QString contentType = contentTypeForPath(entry.path);

    // Parse Range
    QString rangeHeader = headers.value("range");
    qint64 start = 0;
    qint64 end = entry.size - 1;
    bool hasRange = false;

    if (!rangeHeader.isEmpty()) {
        auto [rs, re] = parseRange(rangeHeader, entry.size);
        if (rs >= 0 && re >= 0 && rs <= re && rs < entry.size) {
            start = rs;
            end = qMin(re, entry.size - 1);
            hasRange = true;
        } else {
            // STREAM_PLAYBACK_FIX Batch 1.2 — 416 also gains Content-Type +
            // Connection: close for consistency with sendErr.
            QByteArray r;
            r += "HTTP/1.1 416 Range Not Satisfiable\r\n";
            r += "Content-Range: bytes */" + QByteArray::number(entry.size) + "\r\n";
            r += "Content-Type: text/plain\r\n";
            r += "Content-Length: 0\r\n";
            r += "Connection: close\r\n\r\n";
            socket.write(r);
            socket.waitForBytesWritten(3000);
            socket.close();
            return;
        }
    }

    qint64 length = end - start + 1;

    // Build response header
    QByteArray respHeader;
    if (hasRange) {
        respHeader += "HTTP/1.1 206 Partial Content\r\n";
        respHeader += "Content-Range: bytes " + QByteArray::number(start)
                      + "-" + QByteArray::number(end)
                      + "/" + QByteArray::number(entry.size) + "\r\n";
    } else {
        respHeader += "HTTP/1.1 200 OK\r\n";
    }
    respHeader += "Content-Type: " + contentType.toUtf8() + "\r\n";
    respHeader += "Content-Length: " + QByteArray::number(length) + "\r\n";
    respHeader += "Accept-Ranges: bytes\r\n";
    respHeader += "Cache-Control: no-store\r\n";
    respHeader += "Connection: close\r\n";
    respHeader += "\r\n";

    if (method == "HEAD") {
        socket.write(respHeader);
        socket.waitForBytesWritten(3000);
        socket.close();
        return;
    }

    // Send header
    if (socket.write(respHeader) < 0) { socket.close(); return; }
    socket.waitForBytesWritten(5000);

    // Open file and serve data
    QFile file(entry.path);
    if (!file.open(QIODevice::ReadOnly)) { socket.close(); return; }
    if (!file.seek(start)) { socket.close(); return; }

    TorrentEngine* engine = server->engine();
    qint64 remaining = length;
    qint64 offset = start;

    // STREAM_PLAYBACK_FIX Batch 1.3 — idle-progress watchdog. The timer is
    // reset on every successful chunk write. If we go 30s without progress
    // (not bytes-on-wire, but piece availability + write completion), the
    // connection is considered dead — break so the guard releases the
    // socket instead of hanging the thread indefinitely.
    QElapsedTimer idleTimer;
    idleTimer.start();
    constexpr qint64 kIdleTimeoutMs = 30000;

    while (remaining > 0) {
        // Cooperative shutdown check. Set by StreamHttpServer::stop().
        if (server && server->isShuttingDown()) {
            qWarning() << "StreamHttpServer: shutdown requested, closing active connection";
            break;
        }

        // Idle-progress watchdog.
        if (idleTimer.elapsed() > kIdleTimeoutMs) {
            qWarning().nospace()
                << "StreamHttpServer: idle timeout ("
                << kIdleTimeoutMs << "ms) — no progress, closing connection";
            break;
        }

        // Client disconnected (user closed the player, sidecar gave up).
        // Caught here before any further waitForPieces / file.read work.
        //
        // STREAM_HTTP_SERVE_INTEGRITY 2026-04-21 — this branch was
        // silent pre-fix; the 2026-04-21 freeze investigation needed to
        // know whether ffmpeg was closing its own socket (rw_timeout
        // firing + reconnect_streamed kicking in) vs the server closing
        // for some other reason. qWarning below reports the close path
        // + how far we got; next freeze repro will cleanly indict the
        // client-side close if this branch fires.
        if (socket.state() != QAbstractSocket::ConnectedState) {
            qWarning().nospace()
                << "StreamHttpServer: client disconnected mid-stream for "
                << entry.infoHash << " file=" << entry.fileIndex
                << " offset=" << offset
                << " delivered=" << (length - remaining)
                << "/" << length << " bytes — closing";
            break;
        }

        qint64 toRead = qMin(static_cast<qint64>(CHUNK_SIZE), remaining);

        // STREAM_PLAYBACK_FIX Batch 1.2 — honor the piece-wait return.
        //
        // Before: the timeout was silently ignored and the serve loop fell
        // through to file.read(), returning sparse-zero bytes from the
        // un-downloaded region. The decoder saw a zero-filled hole,
        // interpreted it as corrupt stream, and resynced forward to the
        // next keyframe — producing the user-visible "0:05 → 5:16" jumps.
        //
        // After: break the loop cleanly on timeout. The socket closes
        // (Connection: close header is already set). The sidecar's
        // av_read_frame sees AVERROR(EIO), which routes into the existing
        // HTTP retry/stall-buffering path at
        // native_sidecar/src/video_decoder.cpp:834-849 — user sees a
        // buffering indicator and playback resumes when pieces catch up.
        //
        // qWarning fires on timeout so piece-starvation is observable in
        // logs (previously silent failure).
        //
        // STREAM_ENGINE_REBUILD P2/P6 — waitForPiecesChunk dispatches
        // through StreamPieceWaiter's notification-driven wake; the
        // 200 ms poll-fallback was removed in P6 along with the
        // STREAM_PIECE_WAITER_POLL env flag. Same bool return / same
        // timeout budget / same cancellation semantics.
        if (engine) {
            if (!waitForPiecesChunk(pieceWaiter, engine, entry.infoHash,
                                    entry.fileIndex, offset, toRead, cancelled))
            {
                // Batch 5.2 — distinguish cancellation from timeout in the log
                // so close-while-buffering stress shows up as a different
                // signal vs weak-swarm starvation. Both return false from
                // waitForPiecesChunk; cancelled->load() tells us which.
                const bool wasCancelled =
                    cancelled && cancelled->load(std::memory_order_acquire);
                qWarning().nospace()
                    << "StreamHttpServer: piece wait "
                    << (wasCancelled ? "cancelled" : "timed out")
                    << " for " << entry.infoHash << " file=" << entry.fileIndex
                    << " offset=" << offset << " toRead=" << toRead
                    << " — closing connection"
                    << (wasCancelled ? " (stopStream)" : " to trigger decoder retry");
                break;
            }
        }

        QByteArray chunk = file.read(toRead);
        if (chunk.isEmpty()) {
            qWarning().nospace()
                << "StreamHttpServer: read 0 bytes at offset=" << offset
                << " toRead=" << toRead << " path=" << entry.path;
            break;
        }

        if (socket.write(chunk) < 0) {
            qWarning() << "StreamHttpServer: socket.write failed — client gone";
            break;
        }
        if (!socket.waitForBytesWritten(10000)) {
            qWarning() << "StreamHttpServer: waitForBytesWritten timed out";
            break;
        }

        remaining -= chunk.size();
        offset += chunk.size();
        idleTimer.restart();   // Batch 1.3 — successful progress resets the watchdog.
    }

    // STREAM_HTTP_SERVE_INTEGRITY 2026-04-21 — pair to the break-path
    // qWarning logs above: qInfo on successful completion. Next freeze
    // repro's log tail will cleanly show, per-connection, either:
    //   "StreamHttpServer: complete delivery ..."                          (normal)
    //   "StreamHttpServer: client disconnected mid-stream ..."             (ffmpeg closed)
    //   "StreamHttpServer: idle timeout ..."                               (30s server-side)
    //   "StreamHttpServer: piece wait timed out/cancelled ..."             (engine)
    //   "StreamHttpServer: read 0 bytes ..."                               (QFile mid-file EOF)
    //   "StreamHttpServer: socket.write failed ..."                        (client-side TCP gone)
    //   "StreamHttpServer: waitForBytesWritten timed out"                  (10s client TCP-stall)
    // Observability closes the classification gap noted in the plan.
    if (remaining == 0) {
        qInfo().nospace()
            << "StreamHttpServer: complete delivery for " << entry.infoHash
            << " file=" << entry.fileIndex << " bytes=" << length;
    }

    // Batch 1.3 — ConnectionGuard handles socket close + FIN on scope exit.
}

// ═══════════════════════════════════════════════════════════════════════════

StreamHttpServer::StreamHttpServer(TorrentEngine* engine, QObject* parent)
    : QObject(parent)
    , m_engine(engine)
{
}

StreamHttpServer::~StreamHttpServer()
{
    stop();
}

bool StreamHttpServer::start()
{
    if (m_server) return true;

    // STREAM_PLAYBACK_FIX Batch 1.3 — clear any shutdown flag set by a
    // prior stop() so this start() re-enables the serve loop.
    m_shuttingDown.store(false);

    m_server = new QTcpServer(this);
    m_server->setMaxPendingConnections(30);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        delete m_server;
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QTcpServer::newConnection,
            this, &StreamHttpServer::onNewConnection);
    return true;
}

void StreamHttpServer::stop()
{
    // STREAM_PLAYBACK_FIX Batch 1.3 — cooperative shutdown drain.
    //
    // Set the shutdown flag first so any in-flight handleConnection loops
    // notice on their next iteration and break cleanly (freeing the socket
    // and file descriptor). Then wait up to 2s for active connections to
    // drain before tearing down the QTcpServer. Without this, shutdown on
    // player-close could leave worker threads mid-serve — the file handle
    // stayed open and restarting a stream immediately hit a locked-file
    // error (audit P1 #2 regression class).
    m_shuttingDown.store(true);

    QElapsedTimer drainTimer;
    drainTimer.start();
    constexpr qint64 kDrainTimeoutMs = 2000;
    while (m_activeConnections.load() > 0 && drainTimer.elapsed() < kDrainTimeoutMs) {
        QThread::msleep(25);
    }
    if (m_activeConnections.load() > 0) {
        qWarning().nospace()
            << "StreamHttpServer::stop: drain timeout — "
            << m_activeConnections.load() << " connection(s) still active after "
            << kDrainTimeoutMs << "ms; proceeding with teardown";
    }

    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    {
        QMutexLocker lock(&m_mutex);
        m_registry.clear();
    }

    // Deliberately do NOT reset m_shuttingDown here — a worker thread stuck
    // in waitForPiecesChunk (up to 15 s waiting on a QWaitCondition, or the
    // same budget in poll-fallback mode) could miss its loop-iteration
    // shutdown check, unblock after drain timeout, then read a reset flag
    // and keep serving on a torn-down server. start() below is the only
    // place that flips the flag back to false, and that only happens on a
    // fresh restart path.
}

int StreamHttpServer::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

void StreamHttpServer::registerFile(const QString& infoHash, int fileIndex,
                                     const QString& filePath, qint64 fileSize)
{
    QMutexLocker lock(&m_mutex);
    FileEntry e;
    e.infoHash = infoHash;
    e.fileIndex = fileIndex;
    e.path = filePath;
    e.size = fileSize;
    m_registry[registryKey(infoHash, fileIndex)] = e;
}

void StreamHttpServer::unregisterFile(const QString& infoHash, int fileIndex)
{
    QMutexLocker lock(&m_mutex);
    m_registry.remove(registryKey(infoHash, fileIndex));
}

void StreamHttpServer::clear()
{
    QMutexLocker lock(&m_mutex);
    m_registry.clear();
}

StreamHttpServer::FileEntry StreamHttpServer::lookupFile(const QString& infoHash, int fileIndex) const
{
    QMutexLocker lock(&m_mutex);
    return m_registry.value(registryKey(infoHash, fileIndex));
}

QString StreamHttpServer::registryKey(const QString& infoHash, int fileIndex)
{
    return infoHash.toLower() + ':' + QString::number(fileIndex);
}

void StreamHttpServer::onNewConnection()
{
    while (auto* pending = m_server->nextPendingConnection()) {
        const qintptr originalDesc = pending->socketDescriptor();

        // STREAM_PLAYBACK_FIX — socket-handoff bugfix (audit P0 #2, trace at
        // _player_debug.txt 2026-04-15 15:48/15:54 confirmed).
        //
        // Previous code detached the original QTcpSocket and let it leak so
        // the descriptor wouldn't close, then wrapped the same fd with a
        // second QTcpSocket in a worker thread. The orphan's internal
        // socket notifier stayed armed in the main thread — kernel read
        // readiness events landed on the orphan, bytes were buffered
        // inside the orphan's inaccessible QIODevice buffer, and the
        // worker's waitForReadyRead timed out with 0 bytes. ffmpeg on
        // the other end read EOF before any response header arrived,
        // producing "Cannot open file: probe failed".
        //
        // Fix: duplicate the socket with WSADuplicateSocketW. The worker
        // gets an independent SOCKET handle; the original QTcpSocket is
        // closed + deleted cleanly (which closes only its handle). No
        // competing read notifiers. Windows-only — app is Windows-only;
        // ws2_32 already linked in CMakeLists.txt.
        qintptr workerDesc = -1;
#ifdef _WIN32
        WSAPROTOCOL_INFOW info{};
        if (::WSADuplicateSocketW(
                static_cast<SOCKET>(originalDesc),
                ::GetCurrentProcessId(),
                &info) == 0)
        {
            SOCKET dup = ::WSASocketW(info.iAddressFamily,
                                      info.iSocketType,
                                      info.iProtocol,
                                      &info, 0,
                                      WSA_FLAG_OVERLAPPED);
            if (dup != INVALID_SOCKET) {
                workerDesc = static_cast<qintptr>(dup);
            } else {
                qWarning().nospace()
                    << "StreamHttpServer: WSASocketW failed after duplicate: "
                    << int(::WSAGetLastError());
            }
        } else {
            qWarning().nospace()
                << "StreamHttpServer: WSADuplicateSocketW failed: "
                << int(::WSAGetLastError());
        }
#endif

        if (workerDesc == -1) {
            // Fallback to the old (broken) behavior if duplicate fails.
            // Shouldn't hit this path on any modern Windows stack; keeping
            // it as a safety valve rather than dropping the connection.
            qWarning() << "StreamHttpServer: falling back to legacy 2-socket handoff";
            pending->disconnect();
            pending->setParent(nullptr);
            const qintptr desc = originalDesc;
            QtConcurrent::run([desc, this]() {
                handleConnection(desc, this);
            });
            continue;
        }

        // Happy path — worker owns a duplicated fd; close + delete the
        // original cleanly. The original's close() closes its OWN handle,
        // not the duplicate; worker stays unaffected.
        pending->disconnect();
        pending->close();
        pending->deleteLater();

        QtConcurrent::run([workerDesc, this]() {
            handleConnection(workerDesc, this);
        });
    }
}
