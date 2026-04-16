#include "StreamEngine.h"
#include "StreamHttpServer.h"
#include "core/torrent/TorrentEngine.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>

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

    // STREAM_LIFECYCLE_FIX Phase 5 Batch 5.2 — wire StreamEngine back-pointer
    // so handleConnection can call cancellationToken(infoHash). Done after
    // construction (not as a ctor arg) to keep StreamHttpServer's constructor
    // signature stable — historical callers that instantiate StreamHttpServer
    // directly (tests, etc.) don't need to thread StreamEngine through.
    m_httpServer->setStreamEngine(this);

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

StreamFileResult StreamEngine::streamFile(const tankostream::addon::Stream& stream)
{
    using tankostream::addon::StreamSource;

    switch (stream.source.kind) {
    case StreamSource::Kind::Magnet: {
        const QString magnetUri = stream.source.toMagnetUri();
        if (magnetUri.isEmpty()) {
            StreamFileResult result;
            result.playbackMode = StreamPlaybackMode::LocalHttp;
            result.errorCode = QStringLiteral("ENGINE_ERROR");
            result.errorMessage = QStringLiteral("Magnet stream missing infoHash");
            return result;
        }
        const QString hint = !stream.source.fileNameHint.isEmpty()
            ? stream.source.fileNameHint
            : stream.behaviorHints.filename;
        return streamFile(magnetUri, stream.source.fileIndex, hint);
    }
    case StreamSource::Kind::Http:
    case StreamSource::Kind::Url: {
        StreamFileResult result;
        result.playbackMode = StreamPlaybackMode::DirectUrl;
        if (!stream.source.url.isValid() || stream.source.url.scheme().isEmpty()) {
            result.errorCode = QStringLiteral("ENGINE_ERROR");
            result.errorMessage = QStringLiteral("Direct stream URL is invalid");
            return result;
        }
        result.ok = true;
        result.readyToStart = true;
        result.url = stream.source.url.toString(QUrl::FullyEncoded);
        return result;
    }
    case StreamSource::Kind::YouTube: {
        StreamFileResult result;
        result.playbackMode = StreamPlaybackMode::DirectUrl;
        result.errorCode = QStringLiteral("UNSUPPORTED_SOURCE");
        result.errorMessage = QStringLiteral("YouTube playback not yet supported");
        return result;
    }
    }
    StreamFileResult empty;
    empty.errorCode = QStringLiteral("ENGINE_ERROR");
    empty.errorMessage = QStringLiteral("Unknown stream source kind");
    return empty;
}

StreamFileResult StreamEngine::streamFile(const QString& magnetUri,
                                           int fileIndex,
                                           const QString& fileNameHint)
{
    StreamFileResult result;
    result.playbackMode = StreamPlaybackMode::LocalHttp;

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

    // STREAM_PLAYBACK_FIX Phase 3 Batch 3.2 — buffering % is gate progress.
    //
    // Pre-3.2: fileProgress = downloaded / totalSize (whole-file progress).
    // On the old 2 MB gate this produced the misleading "Buffering 15%"
    // symptom — the user saw 15% while real gate-progress was stuck at
    // 80% or so (depending on piece-selection fairness).
    //
    // Post-3.2: fileProgress = contiguousHeadBytes / 5 MB (head-gate
    // progress). Reaches 100% exactly when the head window is fully
    // contiguous, which is when ffmpeg's probe can read without HTTP
    // piece-waits. Downloaded-bytes field reflects contiguous head too
    // so the "X MB" display in StreamPlayerController is meaningful.
    //
    // 5 MB target matches the sidecar's local-path probe size
    // (native_sidecar/src/demuxer.cpp:15). If fileSize is smaller, we
    // clamp so % still reaches 100 on micro files.
    constexpr qint64 kGateBytes = 5LL * 1024 * 1024;
    qint64 totalSize = rec.selectedFileSize;
    QJsonArray files = m_torrentEngine->torrentFiles(rec.infoHash);
    for (const auto& f : files) {
        QJsonObject fo = f.toObject();
        if (fo.value("index").toInt() == rec.selectedFileIndex) {
            totalSize = fo.value("size").toInteger(totalSize);
            break;
        }
    }
    const qint64 gateSize = qMin(kGateBytes, qMax<qint64>(totalSize, 1));
    const qint64 contiguousHead = m_torrentEngine->contiguousBytesFromOffset(
        rec.infoHash, rec.selectedFileIndex, 0);

    result.selectedFileIndex = rec.selectedFileIndex;
    result.selectedFileName = rec.selectedFileName;
    result.fileSize = totalSize;
    result.downloadedBytes = qMin(contiguousHead, gateSize);
    result.fileProgress = gateSize > 0
        ? qMin(1.0, static_cast<double>(contiguousHead) / gateSize)
        : 0.0;

    // STREAM_PLAYBACK_FIX Phase 3 Batch 3.1 — startup gate RESTORED at
    // 5 MB after probe regression.
    //
    // Initial 3.1 removed the gate entirely, relying on Phase 1 HTTP
    // piece-wait + Phase 2 head deadline to gate playback at the HTTP
    // layer. The theory was sound but empirically broke: ffmpeg's HTTP
    // probe reads up to 20 MB (video_decoder.cpp:175) with a 30s
    // rw_timeout. On cold-start, no pieces exist yet — the Batch 2.2
    // head deadline prioritizes piece 0 but libtorrent still needs
    // peer-handshake + request + response + piece-verify, typically
    // 1-3s. During that window, every HTTP chunk read hits the 15s
    // waitForPieces wall. Probe cumulative time exceeded rw_timeout
    // before enough bytes arrived → "cannot open probe file".
    //
    // Fix: restore the gate at 5 MB (matches sidecar's local-path
    // probesize at demuxer.cpp:15-21; within ffmpeg's probe budget
    // when reading over HTTP). With the gate back:
    //   - streamFile returns FILE_NOT_READY until 5 MB contiguous
    //     head is available → buffering UX shows Batch 3.2 progress
    //     toward the 5 MB target.
    //   - Batch 2.2 head deadline pulls those 5 MB aggressively; this
    //     typically resolves in 2-8s on well-seeded torrents.
    //   - When ok=true fires, ffmpeg's probe reads land-in-cache for
    //     the first 5 MB; subsequent probe reads (up to 20 MB) still
    //     enter Batch 1.2's HTTP piece-wait but those bytes are close
    //     behind the download frontier, so waits are short.
    //
    // Net vs pre-Phase-1: still far faster (no direct-file sparse-read
    // bug, head deadlines instead of rarest-first, 5 MB gate instead
    // of 2 MB with misleading whole-file %). Net vs 3.1-removed gate:
    // slower click-to-play by ~1-5s but reliable probe.
    //
    // flushCache retained — HTTP serving still reads through libtorrent's
    // disk layer, and the have_piece() → on-disk contract matters for
    // any external reader that might later peek at the cache file.

    // The gate: block until the probe window is contiguous. Status text
    // inherits Batch 3.2's gate-progress % (computed above), so the user
    // sees "Buffering N% (M MB)" with N climbing monotonically to 100.
    if (contiguousHead < gateSize) {
        result.queued = true;
        result.errorCode = QStringLiteral("FILE_NOT_READY");
        result.errorMessage =
            QString("Buffering... %1%")
                .arg(static_cast<int>(result.fileProgress * 100.0));
        return result;
    }

    m_torrentEngine->flushCache(rec.infoHash);

    result.ok = true;
    result.readyToStart = true;
    result.url = buildStreamUrl(rec.infoHash, rec.selectedFileIndex);
    return result;
}

void StreamEngine::stopStream(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_streams.find(infoHash);
    if (it == m_streams.end())
        return;

    // STREAM_LIFECYCLE_FIX Phase 5 Batch 5.1 — cancel BEFORE erase so any
    // HTTP worker currently inside waitForPieces (holding the shared_ptr
    // via handleConnection capture) observes cancellation on its next poll
    // iteration. Ordering matters: if we erase first, workers would still
    // see `cancelled==false` when they check, then our subsequent
    // removeTorrent(deleteFiles=true) could invalidate libtorrent state
    // that haveContiguousBytes reads. Setting cancelled first + letting
    // workers short-circuit closes that race at the cost of one atomic
    // store per stopStream call. The cancelled shared_ptr lives past
    // the erase via the worker's own reference count.
    if (it->cancelled) {
        it->cancelled->store(true);
    }

    StreamRecord rec = *it;
    m_streams.erase(it);
    lock.unlock();

    // STREAM_PLAYBACK_FIX Phase 2 Batch 2.3 — clear any lingering piece
    // deadlines (head from 2.2, sliding window from 2.3) so the torrent
    // stops pre-fetching pieces for a playback session that no longer
    // exists. Safe to call even when none were set.
    m_torrentEngine->clearPieceDeadlines(infoHash);

    // Unregister from HTTP server
    if (rec.registered)
        m_httpServer->unregisterFile(rec.infoHash, rec.selectedFileIndex);

    // Remove torrent and delete downloaded data
    // (deleteFiles=true tells libtorrent to remove the downloaded content)
    m_torrentEngine->removeTorrent(rec.infoHash, true);
}

std::shared_ptr<std::atomic<bool>> StreamEngine::cancellationToken(const QString& infoHash) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_streams.find(infoHash);
    if (it == m_streams.end()) return {};
    return it->cancelled;
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

// STREAM_PLAYBACK_FIX Phase 2 Batch 2.3 — sliding-window deadline retargeting.
//
// Called from StreamPage's progressUpdated lambda at ~1Hz during playback
// (rate-limited to once per 2s on the caller side to avoid thrashing
// libtorrent's deadline table). Converts the current playback position to
// a byte offset, asks TorrentEngine for the piece range covering the next
// `windowBytes`, and sets a gradient of deadlines across that range so
// libtorrent's piece scheduler pulls the next 20 MB ahead of the reader.
//
// Deadlines are additive to the Batch 2.2 head deadlines — early-in-file
// playback re-affirms the existing deadlines with updated (closer) ms
// values. libtorrent treats repeated set_piece_deadline() on the same
// piece as an update, not a conflict.
//
// On any out-of-band condition (unknown infoHash, zero file size, zero
// duration, position past end-of-file), returns silently. The caller's
// rate-limit guard means small flakes don't cascade into spam.

void StreamEngine::updatePlaybackWindow(const QString& infoHash,
                                         double positionSec,
                                         double durationSec,
                                         qint64 windowBytes)
{
    int    fileIndex = -1;
    qint64 fileSize  = 0;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_streams.find(infoHash);
        if (it == m_streams.end()) return;
        if (!it->metadataReady) return;
        fileIndex = it->selectedFileIndex;
        fileSize  = it->selectedFileSize;
    }

    if (fileIndex < 0 || fileSize <= 0) return;
    if (durationSec <= 0.0) return;
    if (positionSec < 0.0) positionSec = 0.0;
    if (windowBytes <= 0) return;

    // Convert playback time to byte offset. This is a linear
    // approximation — true byte position depends on codec bitrate
    // distribution, but over a 20 MB window the error is well within
    // one piece boundary for typical video bitrates (5-50 Mbps).
    const double fraction = qMin(1.0, positionSec / durationSec);
    const qint64 byteOffset = static_cast<qint64>(fraction * fileSize);
    if (byteOffset >= fileSize) {
        // Past end — clear any lingering deadlines so the next episode's
        // head fetch (via onMetadataReady) isn't pre-empted by stale
        // late-file deadlines.
        m_torrentEngine->clearPieceDeadlines(infoHash);
        return;
    }

    const qint64 effectiveWindow = qMin(windowBytes, fileSize - byteOffset);
    const QPair<int, int> windowRange =
        m_torrentEngine->pieceRangeForFileOffset(infoHash, fileIndex,
                                                  byteOffset, effectiveWindow);
    if (windowRange.first < 0 || windowRange.second < windowRange.first) return;

    // Gradient: 1000ms at the head of the window → 8000ms at the tail.
    // More urgent than the Batch 2.2 5000ms tail because the reader is
    // actively approaching these pieces; less aggressive than 500ms so
    // the deadline table isn't saturated with every progress tick.
    constexpr int kWindowFirstMs = 1000;
    constexpr int kWindowLastMs  = 8000;
    QList<QPair<int, int>> deadlines;
    const int pieceCount = windowRange.second - windowRange.first + 1;
    deadlines.reserve(pieceCount);
    for (int i = 0; i < pieceCount; ++i) {
        const int ms = (pieceCount <= 1)
            ? kWindowFirstMs
            : kWindowFirstMs + ((kWindowLastMs - kWindowFirstMs) * i)
                               / (pieceCount - 1);
        deadlines.append({ windowRange.first + i, ms });
    }
    m_torrentEngine->setPieceDeadlines(infoHash, deadlines);
}

void StreamEngine::clearPlaybackWindow(const QString& infoHash)
{
    // Cheap even on unknown infoHash — TorrentEngine::clearPieceDeadlines
    // is a no-op if the handle isn't found.
    m_torrentEngine->clearPieceDeadlines(infoHash);
}

bool StreamEngine::prepareSeekTarget(const QString& infoHash,
                                      double positionSec,
                                      double durationSec,
                                      qint64 prefetchBytes)
{
    int    fileIndex = -1;
    qint64 fileSize  = 0;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_streams.find(infoHash);
        if (it == m_streams.end()) return false;
        if (!it->metadataReady) return false;
        fileIndex = it->selectedFileIndex;
        fileSize  = it->selectedFileSize;
    }
    if (fileIndex < 0 || fileSize <= 0) return false;
    if (durationSec <= 0.0 || positionSec < 0.0) return false;
    if (prefetchBytes <= 0) return false;

    // Linear position-to-byte mapping; acceptable for 3 MB target window.
    const double fraction = qMin(1.0, positionSec / durationSec);
    const qint64 byteOffset = static_cast<qint64>(fraction * fileSize);
    if (byteOffset >= fileSize) return false;

    const qint64 effective = qMin(prefetchBytes, fileSize - byteOffset);
    const QPair<int, int> range =
        m_torrentEngine->pieceRangeForFileOffset(infoHash, fileIndex,
                                                  byteOffset, effective);
    if (range.first < 0 || range.second < range.first) return false;

    // Urgent deadline gradient 200ms → 500ms. Tighter than the 1000ms→8000ms
    // sliding window (Batch 2.3) because the user is explicitly blocked on
    // these pieces landing — a "Seeking..." overlay is visible while we
    // poll. Calling setPieceDeadlines on every retry is idempotent
    // (updates in place), so a poll-and-retry cadence from StreamPage
    // keeps urgency fresh without saturation.
    constexpr int kSeekFirstMs = 200;
    constexpr int kSeekLastMs  = 500;
    QList<QPair<int, int>> deadlines;
    const int pieceCount = range.second - range.first + 1;
    deadlines.reserve(pieceCount);
    for (int i = 0; i < pieceCount; ++i) {
        const int ms = (pieceCount <= 1)
            ? kSeekFirstMs
            : kSeekFirstMs + ((kSeekLastMs - kSeekFirstMs) * i)
                             / (pieceCount - 1);
        deadlines.append({ range.first + i, ms });
    }
    m_torrentEngine->setPieceDeadlines(infoHash, deadlines);

    // Is the target window already contiguous? If yes, caller launches
    // immediately; if no, caller retries and we'll re-affirm the deadlines
    // on the next call.
    return m_torrentEngine->haveContiguousBytes(infoHash, fileIndex,
                                                 byteOffset, effective);
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

    // STREAM_PLAYBACK_FIX Phase 2 Batch 2.2 — head deadline on stream start.
    //
    // libtorrent's own streaming guidance
    // (https://www.libtorrent.org/streaming.html) calls sequential_download
    // "sub-optimal for streaming" because rarest-first can still request an
    // early piece from a slow peer while faster peers get later pieces.
    // set_piece_deadline() tells libtorrent which pieces the player needs
    // soonest; the scheduler then assigns them to the peers most likely to
    // deliver in time. This is the primitive Peerflix / torrent-stream /
    // Webtor all use; our missing deadlines are audit P0 #2.
    //
    // Strategy: cover the first 5 MB of the selected file with a linear
    // deadline gradient from 500ms (piece 0) to 5000ms (piece N-1). 5 MB
    // is the sidecar's local-path probe size
    // (native_sidecar/src/demuxer.cpp:15), so by the time ffmpeg's probe
    // starts reading, the head window should be sitting on the disk.
    // sequential_download stays set as a tiebreaker for pieces outside
    // this window — deadlines supersede but don't invalidate the flag.
    //
    // Batches 2.3 (sliding window on playback progress) + 2.4 (seek
    // pre-gate) layer additional deadlines on top of this initial set.
    {
        constexpr qint64 kHeadBytes    = 5LL * 1024 * 1024;   // 5 MB
        constexpr int    kHeadFirstMs  = 500;
        constexpr int    kHeadLastMs   = 5000;
        const QPair<int, int> headRange =
            m_torrentEngine->pieceRangeForFileOffset(infoHash, fileIdx,
                                                      0, kHeadBytes);
        if (headRange.first >= 0 && headRange.second >= headRange.first) {
            QList<QPair<int, int>> deadlines;
            const int pieceCount = headRange.second - headRange.first + 1;
            deadlines.reserve(pieceCount);
            for (int i = 0; i < pieceCount; ++i) {
                const int ms = (pieceCount <= 1)
                    ? kHeadFirstMs
                    : kHeadFirstMs + ((kHeadLastMs - kHeadFirstMs) * i)
                                     / (pieceCount - 1);
                deadlines.append({ headRange.first + i, ms });
            }
            m_torrentEngine->setPieceDeadlines(infoHash, deadlines);
        }
    }

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
