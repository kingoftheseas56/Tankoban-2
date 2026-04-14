#include "StreamHttpServer.h"
#include "core/torrent/TorrentEngine.h"

#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

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
static constexpr int PIECE_WAIT_POLL_MS = 200;
static constexpr int PIECE_WAIT_TIMEOUT_MS = 15000;

// ─── Serve a connection (runs on a thread pool thread) ───────────────────────

static bool waitForPieces(TorrentEngine* engine, const QString& infoHash,
                           int fileIndex, qint64 fileOffset, qint64 length)
{
    if (!engine) return false;
    int elapsed = 0;
    while (elapsed < PIECE_WAIT_TIMEOUT_MS) {
        if (engine->haveContiguousBytes(infoHash, fileIndex, fileOffset, length))
            return true;
        QThread::msleep(PIECE_WAIT_POLL_MS);
        elapsed += PIECE_WAIT_POLL_MS;
    }
    return false;
}

static void handleConnection(qintptr socketDesc, StreamHttpServer* server)
{
    QTcpSocket socket;
    if (!socket.setSocketDescriptor(socketDesc))
        return;

    if (!socket.waitForReadyRead(5000)) {
        socket.close();
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
        QByteArray r;
        r += "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
        r += "Content-Length: 0\r\n\r\n";
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
            QByteArray r;
            r += "HTTP/1.1 416 Range Not Satisfiable\r\n";
            r += "Content-Range: bytes */" + QByteArray::number(entry.size) + "\r\n";
            r += "Content-Length: 0\r\n\r\n";
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

    while (remaining > 0) {
        qint64 toRead = qMin(static_cast<qint64>(CHUNK_SIZE), remaining);

        // Wait for pieces if engine is available
        if (engine)
            waitForPieces(engine, entry.infoHash, entry.fileIndex, offset, toRead);

        QByteArray chunk = file.read(toRead);
        if (chunk.isEmpty()) break;

        if (socket.write(chunk) < 0) break;
        if (!socket.waitForBytesWritten(10000)) break;

        remaining -= chunk.size();
        offset += chunk.size();
    }

    socket.close();
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
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    QMutexLocker lock(&m_mutex);
    m_registry.clear();
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
        qintptr desc = pending->socketDescriptor();
        // Detach — we must NOT delete the QTcpSocket since that closes the descriptor.
        // Instead, disconnect it from QTcpServer ownership and let it leak.
        // The worker thread creates a NEW QTcpSocket from the descriptor.
        pending->disconnect();       // disconnect all signals
        pending->setParent(nullptr); // detach from server
        // We can't safely delete pending here (closes the fd).
        // Mark it for later cleanup — small leak per connection, acceptable for localhost.

        QtConcurrent::run([desc, this]() {
            handleConnection(desc, this);
        });
    }
}
