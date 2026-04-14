#include "StreamEngine.h"
#include "StreamHttpServer.h"
#include "core/torrent/TorrentEngine.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>

static const QStringList VIDEO_EXTENSIONS = {
    "mp4", "mkv", "avi", "webm", "mov", "wmv", "flv", "m4v", "ts", "m2ts"
};

// ═══════════════════════════════════════════════════════════════════════════

StreamEngine::StreamEngine(TorrentEngine* engine, const QString& cacheDir,
                           QObject* parent)
    : QObject(parent)
    , m_torrentEngine(engine)
    , m_httpServer(new StreamHttpServer(engine, this))
    , m_cacheDir(cacheDir)
{
    QDir().mkpath(m_cacheDir);

    // Connect to TorrentEngine signals — we filter by our own m_streams set
    connect(m_torrentEngine, &TorrentEngine::metadataReady,
            this, &StreamEngine::onMetadataReady);
    connect(m_torrentEngine, &TorrentEngine::torrentProgress,
            this, &StreamEngine::onTorrentProgress);
    connect(m_torrentEngine, &TorrentEngine::torrentError,
            this, &StreamEngine::onTorrentError);
}

StreamEngine::~StreamEngine()
{
    stopAll();
}

bool StreamEngine::start()
{
    return m_httpServer->start();
}

void StreamEngine::stop()
{
    stopAll();
    m_httpServer->stop();
}

int StreamEngine::httpPort() const
{
    return m_httpServer->port();
}

// ─── Main streaming API ──────────────────────────────────────────────────────

StreamFileResult StreamEngine::streamFile(const QString& magnetUri,
                                           int fileIndex,
                                           const QString& fileNameHint)
{
    StreamFileResult result;

    if (!m_torrentEngine || !m_torrentEngine->isRunning()) {
        result.errorCode = QStringLiteral("ENGINE_ERROR");
        result.errorMessage = QStringLiteral("Torrent engine not running");
        return result;
    }

    // Check if we already have a stream for this magnet.
    // We key by the hash returned by TorrentEngine::addMagnet() (always 40-char
    // lowercase hex), NOT by what's in the magnet URI (which could be base32).
    // On first call, we add the magnet and store the canonical hash.
    // On subsequent calls, we look up by magnetUri match.

    QMutexLocker lock(&m_mutex);

    // Find existing stream by magnet URI
    QString existingHash;
    for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
        if (it->magnetUri == magnetUri) {
            existingHash = it.key();
            break;
        }
    }

    if (existingHash.isEmpty()) {
        // Add torrent with save path = cache dir. libtorrent will create
        // a subdirectory named after the torrent for multi-file torrents.
        // We add in non-paused mode so downloading starts immediately.
        QString addedHash = m_torrentEngine->addMagnet(magnetUri, m_cacheDir, false);
        if (addedHash.isEmpty()) {
            result.errorCode = QStringLiteral("ENGINE_ERROR");
            result.errorMessage = QStringLiteral("Failed to add magnet to torrent engine");
            return result;
        }

        // Enable sequential download
        m_torrentEngine->setSequentialDownload(addedHash, true);

        StreamRecord rec;
        rec.infoHash = addedHash;
        rec.magnetUri = magnetUri;
        rec.savePath = m_cacheDir;
        rec.requestedFileIndex = fileIndex;
        rec.fileNameHint = fileNameHint;
        m_streams.insert(addedHash, rec);

        result.queued = true;
        result.errorCode = QStringLiteral("METADATA_NOT_READY");
        result.errorMessage = QStringLiteral("Metadata not ready");
        return result;
    }

    auto it = m_streams.find(existingHash);

    // Existing stream — check status
    StreamRecord& rec = *it;
    result.infoHash = rec.infoHash;

    if (!rec.metadataReady) {
        result.queued = true;
        result.errorCode = QStringLiteral("METADATA_NOT_READY");
        result.errorMessage = QStringLiteral("Metadata not ready");
        return result;
    }

    // Metadata is ready — check if file is registered with HTTP server
    if (!rec.registered) {
        result.queued = true;
        result.errorCode = QStringLiteral("FILE_NOT_READY");
        result.errorMessage = QStringLiteral("File not ready");
        return result;
    }

    // Check file progress via TorrentEngine
    QJsonArray files = m_torrentEngine->torrentFiles(rec.infoHash);
    qint64 downloaded = 0;
    qint64 totalSize = rec.selectedFileSize;

    for (const auto& f : files) {
        QJsonObject fo = f.toObject();
        if (fo.value("index").toInt() == rec.selectedFileIndex) {
            double prog = fo.value("progress").toDouble(0.0);
            totalSize = fo.value("size").toInteger(totalSize);
            downloaded = static_cast<qint64>(prog * totalSize);
            break;
        }
    }

    result.selectedFileIndex = rec.selectedFileIndex;
    result.selectedFileName = rec.selectedFileName;
    result.fileSize = totalSize;
    result.downloadedBytes = downloaded;
    result.fileProgress = totalSize > 0 ? static_cast<double>(downloaded) / totalSize : 0.0;

    // Ready when: the first 2MB of pieces for this file are confirmed downloaded.
    // The HTTP server is piece-aware and blocks on each chunk, but ffmpeg's initial
    // probe needs the first few MB available without delay to avoid timeout.
    static constexpr qint64 MIN_HEADER_BYTES = 2 * 1024 * 1024;
    qint64 checkLen = qMin(MIN_HEADER_BYTES, totalSize);
    if (!m_torrentEngine->haveContiguousBytes(rec.infoHash, rec.selectedFileIndex, 0, checkLen)) {
        int pct = totalSize > 0 ? static_cast<int>(100.0 * downloaded / totalSize) : 0;
        result.queued = true;
        result.errorCode = QStringLiteral("FILE_NOT_READY");
        result.errorMessage = QString("Buffering... %1%").arg(pct);
        return result;
    }

    // Ready to play! Return the direct file path instead of HTTP URL.
    // The sidecar handles local files natively and this avoids all the HTTP
    // server complexity (concurrent connections, piece-aware serving, etc.)
    // Sequential download ensures data is available from the start.

    // CRITICAL: flush libtorrent's write cache so the file on disk reflects
    // all downloaded pieces. Without this, have_piece() may return true but
    // the data is still in libtorrent's memory cache, and external readers
    // (like the sidecar) see sparse zeros at those offsets.
    m_torrentEngine->flushCache(rec.infoHash);

    QString filePath;
    QJsonArray fileList = m_torrentEngine->torrentFiles(rec.infoHash);
    for (const auto& f : fileList) {
        QJsonObject fo = f.toObject();
        if (fo.value("index").toInt() == rec.selectedFileIndex) {
            filePath = rec.savePath + "/" + fo.value("name").toString();
            break;
        }
    }
    if (filePath.isEmpty())
        filePath = rec.savePath + "/" + rec.selectedFileName;

    result.ok = true;
    result.readyToStart = true;
    result.url = filePath;
    return result;
}

void StreamEngine::stopStream(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_streams.find(infoHash);
    if (it == m_streams.end())
        return;

    StreamRecord rec = *it;
    m_streams.erase(it);
    lock.unlock();

    // Unregister from HTTP server
    if (rec.registered)
        m_httpServer->unregisterFile(rec.infoHash, rec.selectedFileIndex);

    // Remove torrent and delete downloaded data
    // (deleteFiles=true tells libtorrent to remove the downloaded content)
    m_torrentEngine->removeTorrent(rec.infoHash, true);
}

void StreamEngine::stopAll()
{
    QStringList hashes;
    {
        QMutexLocker lock(&m_mutex);
        hashes = m_streams.keys();
    }

    for (const QString& hash : hashes)
        stopStream(hash);
}

StreamTorrentStatus StreamEngine::torrentStatus(const QString& infoHash) const
{
    StreamTorrentStatus status;
    QMutexLocker lock(&m_mutex);
    auto it = m_streams.find(infoHash);
    if (it != m_streams.end()) {
        status.peers = it->peers;
        status.dlSpeed = it->dlSpeed;
    }
    return status;
}

void StreamEngine::cleanupOrphans()
{
    QDir cacheDir(m_cacheDir);
    if (!cacheDir.exists())
        return;

    QMutexLocker lock(&m_mutex);
    QStringList subdirs = cacheDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& subdir : subdirs) {
        // If it's not an active stream, delete it
        if (!m_streams.contains(subdir)) {
            QDir(cacheDir.filePath(subdir)).removeRecursively();
        }
    }
}

void StreamEngine::startPeriodicCleanup()
{
    if (m_cleanupTimer)
        return;
    m_cleanupTimer = new QTimer(this);
    m_cleanupTimer->setInterval(5 * 60 * 1000); // 5 minutes
    connect(m_cleanupTimer, &QTimer::timeout, this, &StreamEngine::cleanupOrphans);
    m_cleanupTimer->start();
}

// ─── TorrentEngine signal handlers (filtered to our streams) ────────────────

void StreamEngine::onMetadataReady(const QString& infoHash, const QString& /*name*/,
                                    qint64 /*totalSize*/, const QJsonArray& files)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_streams.find(infoHash);
    if (it == m_streams.end())
        return;  // Not our torrent

    StreamRecord& rec = *it;
    rec.metadataReady = true;

    // Select the video file
    int fileIdx = rec.requestedFileIndex;
    if (fileIdx < 0)
        fileIdx = autoSelectVideoFile(files, rec.fileNameHint);

    if (fileIdx < 0) {
        lock.unlock();
        emit streamError(infoHash, QStringLiteral("No video file found in torrent"));
        return;
    }

    rec.selectedFileIndex = fileIdx;

    // Get file info
    for (const auto& f : files) {
        QJsonObject fo = f.toObject();
        if (fo.value("index").toInt() == fileIdx) {
            rec.selectedFileName = QFileInfo(fo.value("name").toString()).fileName();
            rec.selectedFileSize = fo.value("size").toInteger(0);
            break;
        }
    }

    // Set file priorities: max for selected file, skip everything else
    int totalFiles = files.size();
    applyStreamPriorities(infoHash, fileIdx, totalFiles);

    // Compute the actual file path on disk
    // TorrentEngine saves to savePath, file is at savePath/fileName
    QString filePath = rec.savePath + "/" + rec.selectedFileName;

    // For multi-file torrents, the file might be in a subdirectory
    // Use the full name from the torrent file list
    for (const auto& f : files) {
        QJsonObject fo = f.toObject();
        if (fo.value("index").toInt() == fileIdx) {
            QString torrentPath = fo.value("name").toString();
            if (!torrentPath.isEmpty())
                filePath = rec.savePath + "/" + torrentPath;
            break;
        }
    }

    // Register with HTTP server
    m_httpServer->registerFile(infoHash, fileIdx, filePath, rec.selectedFileSize);
    rec.registered = true;
}

void StreamEngine::onTorrentProgress(const QString& infoHash, float /*progress*/,
                                      int dlSpeed, int /*ulSpeed*/, int peers, int /*seeds*/)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_streams.find(infoHash);
    if (it == m_streams.end())
        return;

    it->peers = peers;
    it->dlSpeed = dlSpeed;
}

void StreamEngine::onTorrentError(const QString& infoHash, const QString& message)
{
    QMutexLocker lock(&m_mutex);
    if (!m_streams.contains(infoHash))
        return;

    lock.unlock();
    emit streamError(infoHash, message);
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

int StreamEngine::autoSelectVideoFile(const QJsonArray& files, const QString& hint) const
{
    // If hint matches a filename, prefer that
    if (!hint.isEmpty()) {
        for (const auto& f : files) {
            QJsonObject fo = f.toObject();
            QString name = fo.value("name").toString();
            if (QFileInfo(name).fileName().compare(hint, Qt::CaseInsensitive) == 0)
                return fo.value("index").toInt(-1);
        }
    }

    // Otherwise pick the largest video file
    int bestIndex = -1;
    qint64 bestSize = 0;

    for (const auto& f : files) {
        QJsonObject fo = f.toObject();
        QString name = fo.value("name").toString();
        qint64 size = fo.value("size").toInteger(0);

        if (!isVideoExtension(name))
            continue;

        // Skip sample files
        if (name.contains("sample", Qt::CaseInsensitive) && size < 100 * 1024 * 1024)
            continue;

        if (size > bestSize) {
            bestSize = size;
            bestIndex = fo.value("index").toInt(-1);
        }
    }

    return bestIndex;
}

bool StreamEngine::isVideoExtension(const QString& path)
{
    QString ext = QFileInfo(path).suffix().toLower();
    return VIDEO_EXTENSIONS.contains(ext);
}

QString StreamEngine::buildStreamUrl(const QString& infoHash, int fileIndex) const
{
    return QStringLiteral("http://127.0.0.1:")
           + QString::number(m_httpServer->port())
           + "/stream/" + infoHash + "/"
           + QString::number(fileIndex);
}

void StreamEngine::applyStreamPriorities(const QString& infoHash, int fileIndex, int totalFiles)
{
    QVector<int> priorities(totalFiles, 0);  // skip all files
    if (fileIndex >= 0 && fileIndex < totalFiles)
        priorities[fileIndex] = 7;  // max priority for selected file

    m_torrentEngine->setFilePriorities(infoHash, priorities);
}
