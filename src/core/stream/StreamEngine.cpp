#include "StreamEngine.h"
#include "StreamHttpServer.h"
#include "StreamPieceWaiter.h"
#include "StreamPrioritizer.h"
#include "StreamSeekClassifier.h"
#include "core/torrent/TorrentEngine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

static const QStringList VIDEO_EXTENSIONS = {
    "mp4", "mkv", "avi", "webm", "mov", "wmv", "flv", "m4v", "ts", "m2ts"
};

// ═══════════════════════════════════════════════════════════════════════════
// STREAM_ENGINE_FIX Phase 1.2 — structured telemetry log facility.
//
// Gated on env var TANKOBAN_STREAM_TELEMETRY=1 read once at process start
// (cached in g_telemetryEnabled). Output to stream_telemetry.log next to the
// running executable (QCoreApplication::applicationDirPath), matching
// sidecar_debug_live.log conventions.
//
// Format: "[ISO8601-millis] event=<name> hash=<8> field=value ..." — one
// record per line, key=value pairs space-separated, grep-friendly + readable
// to future tooling (no JSON-per-line tax to pay yet, per Agent 4 Rule-14
// pick at TODO open-questions §).
//
// Thread-safety: writes serialized via g_telemetryMutex. Called from engine
// thread (onMetadataReady, periodic timer) and from streamFile callers
// (poll-driven, may be on any thread). Mutex is fast-path-cheap because
// when telemetry is disabled the writes short-circuit on the cached flag
// before any lock acquisition.
//
// Cadence: event-driven for lifecycle transitions (metadata-ready /
// first-piece-arrival / cancellation / stop) + periodic via QTimer in
// StreamEngine for active streams (every 5s while gate-open; every 15s
// while serving). Never busy-logs on idle.
namespace {

bool g_telemetryEnabled = qgetenv("TANKOBAN_STREAM_TELEMETRY") == "1";
QMutex g_telemetryMutex;
QString g_telemetryPath;  // resolved lazily on first write to avoid early
                          // QCoreApplication ordering issues at static-init.

QString resolveTelemetryPath()
{
    if (!g_telemetryPath.isEmpty()) return g_telemetryPath;
    QString dir = QCoreApplication::applicationDirPath();
    if (dir.isEmpty()) dir = QDir::currentPath();
    g_telemetryPath = dir + QStringLiteral("/stream_telemetry.log");
    return g_telemetryPath;
}

// kv-pair record emit. `event` is the load-bearing label (greppable);
// `body` is space-separated key=value pairs the caller composes. Caller
// owns formatting of the body (StreamEngine has the typed values; this
// helper stays format-agnostic).
void writeTelemetry(const QString& event, const QString& body)
{
    if (!g_telemetryEnabled) return;
    const QString line = QStringLiteral("[")
        + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + QStringLiteral("] event=") + event
        + (body.isEmpty() ? QString() : (QStringLiteral(" ") + body))
        + QStringLiteral("\n");

    QMutexLocker lock(&g_telemetryMutex);
    QFile f(resolveTelemetryPath());
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream out(&f);
    out << line;
}

// Compose a key=value record from a StreamEngineStats. Used by lifecycle
// emit sites + periodic timer; keeps formatting in one place so future
// schema evolution is single-edit.
QString telemetryBodyFromStats(const StreamEngineStats& s)
{
    return QStringLiteral("hash=") + s.infoHash.left(8)
        + QStringLiteral(" idx=") + QString::number(s.activeFileIndex)
        + QStringLiteral(" mdReadyMs=") + QString::number(s.metadataReadyMs)
        + QStringLiteral(" firstPieceMs=") + QString::number(s.firstPieceArrivalMs)
        + QStringLiteral(" gateBytes=") + QString::number(s.gateProgressBytes)
        + QStringLiteral("/") + QString::number(s.gateSizeBytes)
        + QStringLiteral(" gatePct=") + QString::number(s.gateProgressPct, 'f', 1)
        + QStringLiteral(" pieces=[") + QString::number(s.prioritizedPieceRangeFirst)
        + QStringLiteral(",") + QString::number(s.prioritizedPieceRangeLast)
        + QStringLiteral("] peers=") + QString::number(s.peers)
        + QStringLiteral(" dlBps=") + QString::number(s.dlSpeedBps)
        + QStringLiteral(" cancelled=") + (s.cancelled ? QStringLiteral("1") : QStringLiteral("0"))
        + QStringLiteral(" trackerSources=") + QString::number(s.trackerSourceCount);
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════

StreamEngine::StreamEngine(TorrentEngine* engine, const QString& cacheDir,
                           QObject* parent)
    : QObject(parent)
    , m_torrentEngine(engine)
    // Waiter before server — see declaration-order comment in StreamEngine.h.
    , m_pieceWaiter(new StreamPieceWaiter(engine, this))
    , m_httpServer(new StreamHttpServer(engine, this))
    , m_cacheDir(cacheDir)
{
    QDir().mkpath(m_cacheDir);

    // STREAM_ENGINE_FIX Phase 1.1 — start monotonic clock supplying timestamps
    // for StreamSession observability fields. Started here so all per-stream
    // ms values share a common origin (engine startup).
    m_clock.start();

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

    // STREAM_ENGINE_FIX Phase 1.2 — periodic telemetry timer. Always started
    // (cheap when telemetry disabled — slot short-circuits on the env-var
    // flag before any work). 5000ms interval matches Phase 1 spec for
    // "every 5s during gate-open phase"; serving phase tolerates the same
    // cadence (over-emission acceptable, log volume bounded under typical
    // 1-3 active stream load).
    m_telemetryTimer = new QTimer(this);
    m_telemetryTimer->setInterval(5000);
    connect(m_telemetryTimer, &QTimer::timeout,
            this, &StreamEngine::emitTelemetrySnapshots);
    m_telemetryTimer->start();

    // STREAM_ENGINE_REBUILD P3 — 1 Hz re-assert tick. Walks m_streams on
    // every fire; for each Serving session with a recent position feed,
    // re-dispatches the Prioritizer's normal-streaming window so
    // libtorrent's time-critical table stays warm between StreamPage's
    // 2 s tick. See onReassertTick for the skip-predicates that keep the
    // tick cheap when no stream is active.
    m_reassertTimer = new QTimer(this);
    m_reassertTimer->setInterval(1000);
    connect(m_reassertTimer, &QTimer::timeout,
            this, &StreamEngine::onReassertTick);
    m_reassertTimer->start();

    // STREAM_ENGINE_REBUILD P5/P6 — 2 s stall watchdog tick. P6 removed
    // the STREAM_STALL_WATCHDOG rollback env flag — the watchdog is the
    // only path post-P6.
    m_stallTimer = new QTimer(this);
    m_stallTimer->setInterval(2000);
    connect(m_stallTimer, &QTimer::timeout,
            this, &StreamEngine::onStallTick);
    m_stallTimer->start();

    // One-shot startup line so the log opens visibly when telemetry is on
    // — confirms env-var gate worked + filesystem path resolved.
    writeTelemetry(QStringLiteral("engine_started"),
        QStringLiteral("cacheDir=") + m_cacheDir);
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

        // STREAM_ENGINE_FIX Phase 2.6.3 — sequential_download RE-ENABLED.
        //
        // Phase 2.6.2 (2026-04-16) disabled sequential thinking it was the
        // cause of seek_target ready=0 storms. Validation re-test (Phase
        // 2.6.1 telemetry on 1575eafa hash 07:03:25Z) DISPROVED that
        // hypothesis — even with sequential off, seek pieces [21,22] showed
        // have=[1,0] for the full 9-second storm despite 5-9 MB/s sustained
        // bandwidth. Piece size ~2.7MB → should download in 0.5s at that
        // rate; the deadline alone wasn't strong enough to override
        // libtorrent's general piece selection across 90+ peers serving
        // varied pieces in parallel.
        //
        // Sequential mode WAS doing useful work for head delivery though
        // (Phase 2.6.2 test on c23b316b S02E04 — 28-42 peers — showed gate
        // stuck at 48.9% for 25s because libtorrent fetched non-contiguous
        // head pieces without sequential bias). So sequential off was a
        // net regression, not a fix.
        //
        // Phase 2.6.3 fix path: keep sequential ON + add per-piece priority
        // boost on seek pieces (priority 7 + tight deadline) so seek pieces
        // unambiguously win the scheduler. See prepareSeekTarget below for
        // the priority boost; see TorrentEngine::setPiecePriority for the
        // new API surface (Axis 1 territory, Agent 4B pre-offered HELP).
        m_torrentEngine->setSequentialDownload(addedHash, true);

        StreamSession rec;
        rec.infoHash = addedHash;
        rec.magnetUri = magnetUri;
        rec.savePath = m_cacheDir;
        rec.requestedFileIndex = fileIndex;
        rec.fileNameHint = fileNameHint;
        // STREAM_ENGINE_FIX Phase 1.1 — pre-augmentation tracker count.
        // Heuristic: count "tr=" substrings (covers both "&tr=" canonical
        // form produced by StreamSource::toMagnetUri and "?tr=" if any
        // caller hands us a non-canonical magnet). Phase 3.2 will re-store
        // post-augmentation magnetUri + count when fallback-pool injection
        // fires for tracker-light add-on responses.
        rec.trackerSourceCount =
            static_cast<int>(magnetUri.count(QStringLiteral("tr=")));
        m_streams.insert(addedHash, rec);

        result.queued = true;
        result.errorCode = QStringLiteral("METADATA_NOT_READY");
        result.errorMessage = QStringLiteral("Metadata not ready");
        return result;
    }

    auto it = m_streams.find(existingHash);

    // Existing stream — check status
    StreamSession& rec = *it;
    result.infoHash = rec.infoHash;

    if (rec.state == StreamSession::State::Pending) {
        result.queued = true;
        result.errorCode = QStringLiteral("METADATA_NOT_READY");
        result.errorMessage = QStringLiteral("Metadata not ready");
        return result;
    }

    // Metadata is ready — check if file is registered with HTTP server
    if (rec.state != StreamSession::State::Serving) {
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
    //
    // STREAM_ENGINE_FIX Phase 1.1 — kGateBytes hoisted to StreamEngine class
    // constant (StreamEngine.h) so statsSnapshot reports against the same
    // gate the streaming path enforces. Phase 2.1 may tune the constant
    // informed by Phase 1 telemetry; this site picks it up automatically.
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

    // STREAM_ENGINE_FIX Phase 1.1 — first-piece-arrival timestamp. The
    // streamFile poll cadence (StreamPlayerController, ~1-2 Hz) bounds the
    // slop between actual piece-finished alert and this observation. Phase
    // 2.3 will refine via TorrentEngine piece_finished_alert subscription
    // once the cross-domain HELP API lands. Until then this is the best
    // monotonic signal available without touching Agent 4B's domain.
    if (contiguousHead > 0 && rec.firstPieceArrivalMs < 0) {
        rec.firstPieceArrivalMs = m_clock.elapsed();
        // STREAM_ENGINE_FIX Phase 1.2 — first-piece-arrival event. Emits
        // the metadataReady→firstPiece delta which is the load-bearing
        // telemetry datum for the gate-conservatism hypothesis (Axis 1).
        writeTelemetry(QStringLiteral("first_piece"),
            QStringLiteral("hash=") + rec.infoHash.left(8)
            + QStringLiteral(" arrivalMs=") + QString::number(rec.firstPieceArrivalMs)
            + QStringLiteral(" mdReadyMs=") + QString::number(rec.metadataReadyMs)
            + QStringLiteral(" deltaMs=")
            + QString::number(rec.firstPieceArrivalMs - rec.metadataReadyMs));
    }

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

    // STREAM_ENGINE_FIX Phase 1.2 — cancellation event. Emitted under lock
    // so subsequent stop event sees post-cancellation state.
    writeTelemetry(QStringLiteral("cancelled"),
        QStringLiteral("hash=") + infoHash.left(8)
        + QStringLiteral(" idx=") + QString::number(it->selectedFileIndex)
        + QStringLiteral(" lifetimeMs=") + QString::number(m_clock.elapsed() - it->metadataReadyMs));

    StreamSession rec = *it;
    m_streams.erase(it);
    lock.unlock();

    // STREAM_PLAYBACK_FIX Phase 2 Batch 2.3 — clear any lingering piece
    // deadlines (head from 2.2, sliding window from 2.3) so the torrent
    // stops pre-fetching pieces for a playback session that no longer
    // exists. Safe to call even when none were set.
    m_torrentEngine->clearPieceDeadlines(infoHash);

    // Unregister from HTTP server
    if (rec.state == StreamSession::State::Serving)
        m_httpServer->unregisterFile(rec.infoHash, rec.selectedFileIndex);

    // Remove torrent and delete downloaded data
    // (deleteFiles=true tells libtorrent to remove the downloaded content)
    m_torrentEngine->removeTorrent(rec.infoHash, true);

    // STREAM_ENGINE_FIX Phase 1.2 — stop event after libtorrent removal.
    // Closes the lifecycle log thread for this stream.
    writeTelemetry(QStringLiteral("stopped"),
        QStringLiteral("hash=") + rec.infoHash.left(8)
        + QStringLiteral(" idx=") + QString::number(rec.selectedFileIndex));
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

// STREAM_ENGINE_FIX Phase 1.1 — substrate observability snapshot.
//
// Pure-read projection of StreamSession state plus 1-2 calls into TorrentEngine
// for piece-coverage data. Safe at telemetry cadence (5-15s). Returns
// sentinel-defaulted struct for unknown infoHash so callers can treat
// "no record" identically to "freshly-added record with nothing observed yet."
//
// Locking: takes m_mutex for record lookup + record-field reads. TorrentEngine
// calls (contiguousBytesFromOffset, pieceRangeForFileOffset) acquire their
// own internal lock; calling them while holding m_mutex is safe because
// TorrentEngine never calls back into StreamEngine while holding its own lock
// (TorrentEngine→StreamEngine is signal/slot via QueuedConnection across
// threads).
StreamEngineStats StreamEngine::statsSnapshot(const QString& infoHash) const
{
    StreamEngineStats s;
    s.infoHash = infoHash;

    QMutexLocker lock(&m_mutex);
    auto it = m_streams.find(infoHash);
    if (it == m_streams.end()) {
        return s;  // sentinel struct (all -1 / 0 / false / empty)
    }

    const StreamSession& rec = *it;

    s.activeFileIndex     = rec.selectedFileIndex;
    s.metadataReadyMs     = rec.metadataReadyMs;
    s.firstPieceArrivalMs = rec.firstPieceArrivalMs;
    s.peers               = rec.peers;
    s.dlSpeedBps          = rec.dlSpeed;
    s.cancelled           = rec.cancelled
        ? rec.cancelled->load(std::memory_order_acquire) : false;
    s.trackerSourceCount  = rec.trackerSourceCount;

    // STREAM_ENGINE_REBUILD P5 — stall projection. Session fields are set
    // by onStallTick under m_mutex so the read here is coherent without
    // extra locking.
    s.stalled             = (rec.stallStartMs >= 0);
    if (s.stalled) {
        s.stallElapsedMs      = m_clock.elapsed() - rec.stallStartMs;
        s.stallPiece          = rec.stallPiece;
        s.stallPeerHaveCount  = rec.stallPeerHaveCount;
    }

    // Gate progress + prioritized piece range require TorrentEngine reads;
    // gated on metadata-ready + valid file selection so pre-metadata polls
    // return clean sentinels.
    if (rec.state != StreamSession::State::Pending
        && rec.selectedFileIndex >= 0
        && rec.selectedFileSize > 0 && m_torrentEngine) {
        const qint64 gateSize =
            qMin(kGateBytes, qMax<qint64>(rec.selectedFileSize, 1));
        const qint64 contig = m_torrentEngine->contiguousBytesFromOffset(
            rec.infoHash, rec.selectedFileIndex, 0);

        s.gateSizeBytes     = gateSize;
        s.gateProgressBytes = qMin(contig, gateSize);
        s.gateProgressPct   = gateSize > 0
            ? qMin(100.0, 100.0 * static_cast<double>(contig) / gateSize)
            : 0.0;

        // Prioritized piece range: the head 5 MB region (mirrors the head
        // deadlines set in onMetadataReady's head-deadlines block). Phase 2.2
        // will add the tail-metadata range; Phase 2.3 will add dynamic seek
        // window; statsSnapshot will report whichever is widest at that
        // point. For Phase 1.1 the head region is the only prioritized
        // zone, so reporting just that is accurate.
        const QPair<int, int> headRange =
            m_torrentEngine->pieceRangeForFileOffset(
                rec.infoHash, rec.selectedFileIndex, 0, kGateBytes);
        s.prioritizedPieceRangeFirst = headRange.first;
        s.prioritizedPieceRangeLast  = headRange.second;
    }

    return s;
}

// PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.1 — buffered-range projection
// for the active stream. Pure lookup: resolve selected file index under
// m_mutex, then delegate the piece-walk + byte-range merge to TorrentEngine.
// Returns empty for unknown or not-metadata-ready streams so the caller's
// SeekSlider paint path can treat empty == "no overlay" (local-file mode).
QList<QPair<qint64, qint64>> StreamEngine::contiguousHaveRanges(
    const QString& infoHash) const
{
    int fileIndex = -1;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_streams.find(infoHash);
        if (it == m_streams.end()) return {};
        if (it->state == StreamSession::State::Pending) return {};
        fileIndex = it->selectedFileIndex;
    }
    if (fileIndex < 0 || !m_torrentEngine) return {};
    return m_torrentEngine->fileByteRangesOfHavePieces(infoHash, fileIndex);
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
                                         qint64 /*windowBytes*/)
{
    // STREAM_ENGINE_REBUILD P3 — delegates to StreamPrioritizer +
    // StreamSeekClassifier. Cached position state persists on the
    // StreamSession so the 1 Hz re-assert timer (onReassertTick) can
    // re-dispatch between StreamPage's 2 s telemetry ticks. The legacy
    // `windowBytes` parameter is preserved on the signature but ignored
    // here — Prioritizer computes window size from bitrate + EMA speed
    // per Stremio `priorities.rs:72-122`.
    if (durationSec <= 0.0) return;
    if (positionSec < 0.0) positionSec = 0.0;

    QMutexLocker lock(&m_mutex);
    auto it = m_streams.find(infoHash);
    if (it == m_streams.end()) return;
    if (it->state == StreamSession::State::Pending) return;

    StreamSession& s = *it;
    const int fileIndex = s.selectedFileIndex;
    const qint64 fileSize = s.selectedFileSize;
    if (fileIndex < 0 || fileSize <= 0) return;

    // Linear position-to-byte approximation; error over a 20 MB window
    // stays within one piece boundary at typical video bitrates.
    const double fraction = qMin(1.0, positionSec / durationSec);
    const qint64 byteOffset = static_cast<qint64>(fraction * fileSize);
    if (byteOffset >= fileSize) {
        // Past end — clear any lingering deadlines so the next episode's
        // head fetch (via onMetadataReady) isn't pre-empted by stale
        // late-file deadlines.
        m_torrentEngine->clearPieceDeadlines(infoHash);
        s.lastPlaybackTickMs = m_clock.elapsed();
        s.lastPlaybackPosSec = positionSec;
        s.lastDurationSec = durationSec;
        s.lastPlaybackOffset = fileSize;
        return;
    }

    // Classify this call. Sequential vs UserScrub is distinguished by
    // comparing against the cached lastPlaybackOffset — small forward
    // jumps are Sequential, large jumps (or backward) are UserScrub.
    constexpr qint64 kSequentialForwardBudgetBytes = 5LL * 1024 * 1024;
    const qint64 prevOffset = s.lastPlaybackOffset;
    const qint64 delta = byteOffset - prevOffset;

    StreamSeekType seekType = StreamSeekClassifier::classifySeek(
        byteOffset, fileSize, s.firstClassification);
    if (!s.firstClassification) {
        // Refine classifySeek's UserScrub return for the Sequential case:
        // forward delta within kSequentialForwardBudgetBytes = still
        // Sequential (matches Stremio stream.rs:84-90 "just extend window,
        // no cleanup"). Backward or large-forward = UserScrub.
        if (seekType == StreamSeekType::UserScrub
            && delta >= 0 && delta <= kSequentialForwardBudgetBytes) {
            seekType = StreamSeekType::Sequential;
        }
    }
    s.firstClassification = false;

    // Cache position state for the 1 Hz re-assert tick.
    s.lastPlaybackTickMs = m_clock.elapsed();
    s.lastPlaybackPosSec = positionSec;
    s.lastDurationSec    = durationSec;
    s.lastPlaybackOffset = byteOffset;
    s.lastSeekType       = seekType;
    // Feed EMA from the cached dlSpeed (already updated by
    // TorrentEngine::torrentProgress). Keeps the filter warm even when
    // no seek has fired since the last tick.
    s.updateSpeedEma(s.dlSpeed);

    // UserScrub: Stremio `stream.rs:101-108` clears all deadlines first so
    // the old window doesn't contend with the new one. Defensive
    // tail-metadata preservation per M6: we re-set the tail deadlines
    // immediately after the clear (below) so libtorrent's overdue-deadline
    // semantic doesn't demote moov-atom work. ContainerMetadata
    // (stream.rs:92-99) PRESERVES head deadlines — no clear.
    if (seekType == StreamSeekType::UserScrub) {
        m_torrentEngine->clearPieceDeadlines(infoHash);
    }

    // Fetch the current piece range for the target offset.
    const QPair<int, int> pieceRange = m_torrentEngine->pieceRangeForFileOffset(
        infoHash, fileIndex, byteOffset, qMax<qint64>(s.selectedFileSize - byteOffset, 1));
    if (pieceRange.first < 0) {
        lock.unlock();
        return;
    }

    // Reassert streaming priorities via the Prioritizer (normal-streaming
    // window). Runs on every call regardless of seek type — the window
    // follows the cursor.
    reassertStreamingPriorities(s);

    // M6 defensive invariant — after UserScrub clear, re-set tail-metadata
    // deadlines so moov/Cues pieces don't drop off libtorrent's
    // time-critical radar. Tail-metadata range matches onMetadataReady's
    // block (last 3 MB). Zero cost if tail and head overlap (small files
    // with fileSize < kGateBytes + 3 MB).
    if (seekType == StreamSeekType::UserScrub) {
        constexpr qint64 kTailBytes    = 3LL * 1024 * 1024;
        constexpr int    kTailFirstMs  = 6000;
        constexpr int    kTailLastMs   = 10000;
        if (fileSize > kTailBytes + kGateBytes) {
            const qint64 tailOffset = fileSize - kTailBytes;
            const QPair<int, int> tailRange =
                m_torrentEngine->pieceRangeForFileOffset(infoHash, fileIndex,
                                                          tailOffset, kTailBytes);
            if (tailRange.first >= 0 && tailRange.second >= tailRange.first) {
                const int tailPieceCount = tailRange.second - tailRange.first + 1;
                QList<QPair<int, int>> tailDeadlines;
                tailDeadlines.reserve(tailPieceCount);
                for (int i = 0; i < tailPieceCount; ++i) {
                    const int ms = (tailPieceCount <= 1)
                        ? kTailFirstMs
                        : kTailFirstMs + ((kTailLastMs - kTailFirstMs) * i)
                                         / (tailPieceCount - 1);
                    tailDeadlines.append({ tailRange.first + i, ms });
                }
                m_torrentEngine->setPieceDeadlines(infoHash, tailDeadlines);
            }
        }
    }

    // Reset to Sequential after handling the seek — matches Stremio
    // `stream.rs:112` self.seek_type = SeekType::Sequential so subsequent
    // ticks don't re-trigger the UserScrub clear / ContainerMetadata
    // preservation path.
    if (seekType != StreamSeekType::Sequential) {
        s.lastSeekType = StreamSeekType::Sequential;
    }
}

void StreamEngine::reassertStreamingPriorities(StreamSession& s)
{
    // Caller holds m_mutex. Dispatches Stremio's calculate_priorities
    // port against the cached session state.
    if (!m_torrentEngine) return;
    if (s.state == StreamSession::State::Pending || s.selectedFileSize <= 0) return;

    // Resolve the current piece from the cached offset.
    const QPair<int, int> headRange = m_torrentEngine->pieceRangeForFileOffset(
        s.infoHash, s.selectedFileIndex, s.lastPlaybackOffset, 1);
    if (headRange.first < 0) return;

    // Total pieces = last piece index + 1. Derived from the file-end
    // offset so Prioritizer can clamp its window correctly.
    const QPair<int, int> endRange = m_torrentEngine->pieceRangeForFileOffset(
        s.infoHash, s.selectedFileIndex, s.selectedFileSize - 1, 1);
    if (endRange.first < 0) return;
    const int totalPieces = endRange.second + 1;

    // Piece length — derive from the cached range step. Simpler: ask
    // TorrentEngine for one piece range + compute from byte-extents. For
    // now, use the byte-offset-to-piece mapping from pieceRangeForFileOffset
    // which already returns the right index. Piece length is needed for
    // Prioritizer's window sizing, so derive it from the first piece's
    // byte span. Fallback: 2 MB typical torrent piece size.
    // (A dedicated TorrentEngine::pieceLength() accessor would be cleaner;
    // adding one post-P3 under integration-memo §8 cross-slice cleanups.)
    constexpr qint64 kAssumedPieceLength = 2LL * 1024 * 1024;

    StreamPrioritizer::Params p;
    p.currentPiece     = headRange.first;
    p.totalPieces      = totalPieces;
    p.pieceLength      = kAssumedPieceLength;
    p.priorityLevel    = 1;  // normal streaming
    p.downloadSpeed    = static_cast<qint64>(s.downloadSpeedEma);
    p.bitrate          = s.bitrateHint;

    const QList<QPair<int, int>> deadlines =
        StreamPrioritizer::calculateStreamingPriorities(p);
    if (deadlines.isEmpty()) return;

    m_torrentEngine->setPieceDeadlines(s.infoHash, deadlines);
}

void StreamEngine::onReassertTick()
{
    // 1 Hz walk. Cheap: we skip sessions without a recent position feed
    // (cold-open pre-metadata, or paused streams). Held under m_mutex
    // because reassertStreamingPriorities reads session state; the inner
    // setPieceDeadlines call drops into TorrentEngine's own m_mutex
    // without nesting against ours (order: StreamEngine::m_mutex →
    // TorrentEngine::m_mutex via setPieceDeadlines; never reversed).
    constexpr qint64 kStaleFeedCutoffMs = 10000;  // skip if no feed in 10 s
    const qint64 now = m_clock.elapsed();

    QMutexLocker lock(&m_mutex);
    for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
        StreamSession& s = *it;
        if (s.state != StreamSession::State::Serving) continue;
        if (s.lastPlaybackTickMs < 0) continue;
        if (now - s.lastPlaybackTickMs > kStaleFeedCutoffMs) continue;
        reassertStreamingPriorities(s);
    }
}

void StreamEngine::onStallTick()
{
    // STREAM_ENGINE_REBUILD P5 — every 2 s, read the waiter's longest in-
    // flight wait. Crosses the threshold (4000 ms) = stall. Clears the
    // threshold = recovery. Both transitions emit telemetry; the stalled
    // flag on StreamSession drives the statsSnapshot UI projection.
    //
    // Lock-ordering: m_pieceWaiter->longestActiveWait() acquires the
    // waiter's own m_mutex. We call it OUTSIDE our own m_mutex scope to
    // avoid any risk of nesting (StreamPieceWaiter never holds its mutex
    // while calling back into StreamEngine, but keeping them cleanly
    // separated here preserves that property in both directions).
    constexpr qint64 kStallThresholdMs = 4000;

    if (!m_pieceWaiter || !m_torrentEngine) return;
    const auto lw = m_pieceWaiter->longestActiveWait();

    QMutexLocker lock(&m_mutex);
    const qint64 now = m_clock.elapsed();

    // Match the active waiter to its session (lw.infoHash may not point
    // at a stream we still own — e.g. stopStream raced with a wedged
    // worker; skip gracefully in that case).
    StreamSession* stallSession = nullptr;
    if (lw.pieceIndex >= 0 && !lw.infoHash.isEmpty()) {
        auto it = m_streams.find(lw.infoHash);
        if (it != m_streams.end() && it->state == StreamSession::State::Serving) {
            stallSession = &(*it);
        }
    }

    // Detection: longest wait has crossed the 4 s threshold and we have
    // not already flagged this session as stalled.
    if (stallSession && lw.elapsedMs >= kStallThresholdMs
        && !stallSession->stallEmitted) {
        // R3 falsification data: how many peers claim the blocked piece?
        // 0 = swarm-starvation (no amount of priority wins); > 0 =
        // scheduler-starvation (priority 7 can move the piece into the
        // next request window).
        const int peerHave = m_torrentEngine->peersWithPiece(
            lw.infoHash, lw.pieceIndex);

        stallSession->stallStartMs       = now - lw.elapsedMs;
        stallSession->stallPiece         = lw.pieceIndex;
        stallSession->stallPeerHaveCount = peerHave;
        stallSession->stallEmitted       = true;

        // Re-assert priority 7 on the blocked piece. One retry beyond the
        // 3-retry budget P3 burned through; if this also doesn't unblock,
        // the session waits for P5.2 recovery semantics (piece arrives
        // naturally, user cancels, or source-switch).
        m_torrentEngine->setPiecePriority(lw.infoHash, lw.pieceIndex, 7);

        // Emit outside the lock to avoid holding m_mutex across file I/O.
        const QString hash  = lw.infoHash;
        const int piece     = lw.pieceIndex;
        const qint64 waitMs = lw.elapsedMs;
        lock.unlock();
        writeTelemetry(QStringLiteral("stall_detected"),
            QStringLiteral("hash=") + hash.left(8)
            + QStringLiteral(" piece=") + QString::number(piece)
            + QStringLiteral(" wait_ms=") + QString::number(waitMs)
            + QStringLiteral(" peer_have_count=") + QString::number(peerHave));
        return;
    }

    // Recovery: session was stalled but the longest wait has dropped below
    // threshold OR the blocked piece is no longer the longest (it arrived
    // and a new wait is now the tail). Either way, the prior stall is
    // cleared.
    for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
        StreamSession& s = *it;
        if (!s.stallEmitted) continue;
        const bool sameStallStillActive = stallSession == &s
            && lw.pieceIndex == s.stallPiece
            && lw.elapsedMs >= kStallThresholdMs;
        if (sameStallStillActive) continue;

        const qint64 elapsedSinceStall = now - s.stallStartMs;
        const QString hash = s.infoHash;
        const int piece    = s.stallPiece;
        const char* via    = (stallSession == &s && lw.pieceIndex != piece)
            ? "piece_arrival"                                 // the blocked piece landed
            : (s.state != StreamSession::State::Serving
                 ? "replacement"                              // session torn down
                 : (s.cancelled && s.cancelled->load()
                      ? "cancelled"                           // explicit cancel
                      : "piece_arrival"));                    // default

        s.stallStartMs       = -1;
        s.stallPiece         = -1;
        s.stallPeerHaveCount = -1;
        s.stallEmitted       = false;

        lock.unlock();
        writeTelemetry(QStringLiteral("stall_recovered"),
            QStringLiteral("hash=") + hash.left(8)
            + QStringLiteral(" piece=") + QString::number(piece)
            + QStringLiteral(" elapsed_ms=") + QString::number(elapsedSinceStall)
            + QStringLiteral(" via=") + QString::fromLatin1(via));
        return;  // one recovery per tick; any other sessions wait for the next fire
    }
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
    StreamSeekType   seekType = StreamSeekType::UserScrub;
    bool             firstClass = false;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_streams.find(infoHash);
        if (it == m_streams.end()) return false;
        if (it->state == StreamSession::State::Pending) return false;
        fileIndex  = it->selectedFileIndex;
        fileSize   = it->selectedFileSize;
        firstClass = it->firstClassification;
    }
    if (fileIndex < 0 || fileSize <= 0) return false;
    if (durationSec <= 0.0 || positionSec < 0.0) return false;
    if (prefetchBytes <= 0) return false;

    // Linear position-to-byte mapping; acceptable for 3 MB target window.
    const double fraction = qMin(1.0, positionSec / durationSec);
    const qint64 byteOffset = static_cast<qint64>(fraction * fileSize);
    if (byteOffset >= fileSize) return false;

    // STREAM_ENGINE_REBUILD P3 — classify the seek so telemetry captures
    // which Stremio-shape branch we took (and so the session's lastSeekType
    // tracks correctly for the M6 defensive tail-preservation path). The
    // deadline gradient below stays verbatim from Phase 2.6.3 (empirically
    // won against libtorrent's scheduler at 18 MB/s on a 200-500 ms
    // staircase across the full prefetch window); switching to Stremio's
    // CRITICAL 300 ms × 4-piece shape is a post-P3 tuning question if the
    // current gradient regresses.
    seekType = StreamSeekClassifier::classifySeek(byteOffset, fileSize, firstClass);

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

    // STREAM_ENGINE_FIX Phase 2.6.3 — per-piece priority boost on seek
    // pieces. setPieceDeadlines alone wasn't strong enough to override
    // libtorrent's general piece selection (Phase 2.6.1 telemetry on
    // 1575eafa hash 07:03:25Z proved have=[1,0] for full 9-second storm
    // despite tight 200ms deadline + 5-9 MB/s sustained bandwidth). Adding
    // priority 7 (max) on each seek piece gives them unambiguous scheduler
    // win — both priority AND deadline favor them over any other piece
    // libtorrent considers. Idempotent (repeat-call sets same priority);
    // safe to call on every prepareSeekTarget poll-retry.
    for (int i = 0; i < pieceCount; ++i) {
        m_torrentEngine->setPiecePriority(infoHash, range.first + i, 7);
    }

    // Is the target window already contiguous? If yes, caller launches
    // immediately; if no, caller retries and we'll re-affirm the deadlines
    // on the next call.
    const bool ready = m_torrentEngine->haveContiguousBytes(infoHash, fileIndex,
                                                             byteOffset, effective);

    // STREAM_ENGINE_FIX Phase 1.3 — seek_target event. Captures every
    // prepareSeekTarget call (including poll-retry calls every 300ms during
    // the 9s wait window). ready=1 means caller will launch player
    // immediately; ready=0 means caller polls again.
    //
    // Phase 2.6.1 (2026-04-16) — added per-piece `have` field + head-piece
    // `headHave` field for context. Diagnoses the seek_target ready=0 storms
    // observed at 18 MB/s sustained (e.g., One Piece d2403b7a 20:06:44-53)
    // where seek pieces 27-29 took 10+ seconds to become ready despite
    // bandwidth far exceeding their 6MB total. Hypothesis: sequential_download
    // (set at StreamEngine.cpp:156) is fighting prepareSeekTarget's
    // 200-500ms deadlines — libtorrent prefers piece N+1 over far-away
    // deadlined piece N+27 even with the tighter deadline. `have` field shows
    // whether seek pieces are slowly arriving (one by one over poll cycles)
    // or not arriving at all (libtorrent ignoring our deadlines entirely).
    QString haveStr = QStringLiteral("[");
    for (int p = range.first; p <= range.second; ++p) {
        if (p != range.first) haveStr += QStringLiteral(",");
        haveStr += m_torrentEngine->havePiece(infoHash, p)
            ? QStringLiteral("1") : QStringLiteral("0");
    }
    haveStr += QStringLiteral("]");

    // Head 5MB piece state for context — was head fully done when seek fired?
    // Range derived same way as onMetadataReady's head_deadlines block.
    QString headHaveStr = QStringLiteral("[");
    const QPair<int, int> headRange =
        m_torrentEngine->pieceRangeForFileOffset(infoHash, fileIndex,
                                                  0, kGateBytes);
    if (headRange.first >= 0 && headRange.second >= headRange.first) {
        for (int p = headRange.first; p <= headRange.second; ++p) {
            if (p != headRange.first) headHaveStr += QStringLiteral(",");
            headHaveStr += m_torrentEngine->havePiece(infoHash, p)
                ? QStringLiteral("1") : QStringLiteral("0");
        }
    }
    headHaveStr += QStringLiteral("]");

    writeTelemetry(QStringLiteral("seek_target"),
        QStringLiteral("hash=") + infoHash.left(8)
        + QStringLiteral(" positionSec=") + QString::number(positionSec, 'f', 2)
        + QStringLiteral(" byteOffset=") + QString::number(byteOffset)
        + QStringLiteral(" prefetchBytes=") + QString::number(effective)
        + QStringLiteral(" pieces=[") + QString::number(range.first)
        + QStringLiteral(",") + QString::number(range.second)
        + QStringLiteral("] pieceCount=") + QString::number(pieceCount)
        + QStringLiteral(" ready=") + (ready ? QStringLiteral("1") : QStringLiteral("0"))
        + QStringLiteral(" have=") + haveStr
        + QStringLiteral(" headHave=") + headHaveStr);

    return ready;
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

// STREAM_ENGINE_FIX Phase 1.2 — periodic telemetry snapshot emit.
//
// Called by m_telemetryTimer every 5s. Cheap when telemetry disabled
// (early-out via the cached env-var flag inside writeTelemetry, which
// short-circuits before any allocation). When enabled: snapshot the active
// stream-key list under lock, release, then call statsSnapshot per key
// (re-locks internally). Two locks per stream per tick is fine — telemetry
// is bounded to 1-3 active streams typically; mutex is uncontended.
//
// Streams pre-metadata get sentinel-default snapshots — skip emission for
// those (activeFileIndex < 0) to avoid empty-record noise. metadata_ready
// event from onMetadataReady covers the early-life telemetry.
void StreamEngine::emitTelemetrySnapshots()
{
    QStringList hashes;
    {
        QMutexLocker lock(&m_mutex);
        hashes = m_streams.keys();
    }
    for (const QString& h : hashes) {
        const StreamEngineStats s = statsSnapshot(h);
        if (s.activeFileIndex < 0) continue;
        writeTelemetry(QStringLiteral("snapshot"), telemetryBodyFromStats(s));
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

    StreamSession& rec = *it;
    if (rec.state == StreamSession::State::Pending)
        rec.state = StreamSession::State::MetadataOnly;

    // STREAM_ENGINE_FIX Phase 1.1 — metadata-ready timestamp. Set once;
    // never overwritten (onMetadataReady can theoretically fire multiple
    // times for resumed torrents, but the first observation is the
    // load-bearing one for telemetry deltas like metadataReady→firstPiece
    // and metadataReady→gatePassed).
    if (rec.metadataReadyMs < 0) {
        rec.metadataReadyMs = m_clock.elapsed();
        // STREAM_ENGINE_FIX Phase 1.2 — metadata_ready event. First
        // structured-log entry per stream lifecycle; opens the log thread.
        // file_selected event below carries the autoSelectVideoFile result
        // (filename + size + whether hint matched).
        writeTelemetry(QStringLiteral("metadata_ready"),
            QStringLiteral("hash=") + infoHash.left(8)
            + QStringLiteral(" mdReadyMs=") + QString::number(rec.metadataReadyMs)
            + QStringLiteral(" trackerSources=") + QString::number(rec.trackerSourceCount)
            + QStringLiteral(" totalFiles=") + QString::number(files.size()));
    }

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

    // STREAM_ENGINE_FIX Phase 1.3 — file_selected event. Captures what
    // autoSelectVideoFile actually picked so future diagnostic passes can
    // verify selection matches the user's intent (vs the largest-video
    // fallback that mismatches when behaviorHints.filename doesn't
    // exact-match a file path in the torrent metadata). hintProvided
    // distinguishes "no hint, autoSelect ran free" from "hint provided
    // but autoSelect picked something else (mismatch case)" from "hint
    // matched cleanly". requestedFileIndex>=0 means addon set fileIdx
    // explicitly (skips autoSelect entirely).
    {
        const bool hintProvided = !rec.fileNameHint.isEmpty();
        const bool requestedExplicit = (rec.requestedFileIndex >= 0);
        const bool hintMatched = hintProvided
            && QString::compare(rec.selectedFileName, rec.fileNameHint,
                                Qt::CaseInsensitive) == 0;
        QString selectionMode;
        if (requestedExplicit) selectionMode = QStringLiteral("addon_index");
        else if (hintMatched)  selectionMode = QStringLiteral("hint_matched");
        else if (hintProvided) selectionMode = QStringLiteral("hint_missed_largest_fallback");
        else                   selectionMode = QStringLiteral("largest_video_no_hint");

        writeTelemetry(QStringLiteral("file_selected"),
            QStringLiteral("hash=") + infoHash.left(8)
            + QStringLiteral(" idx=") + QString::number(fileIdx)
            + QStringLiteral(" size=") + QString::number(rec.selectedFileSize)
            + QStringLiteral(" mode=") + selectionMode
            + QStringLiteral(" hint=\"") + rec.fileNameHint
            + QStringLiteral("\" picked=\"") + rec.selectedFileName
            + QStringLiteral("\""));
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
        // STREAM diagnostic (Agent 4B — temporary trace for stream-head-gate
        // regression; remove after that bug closes).
        const int diagPieceCount = (headRange.first >= 0 && headRange.second >= headRange.first)
            ? (headRange.second - headRange.first + 1) : 0;
        qDebug().nospace() << "[STREAM] head-deadlines infoHash="
            << infoHash.left(8) << " file=" << fileIdx
            << " headRange=[" << headRange.first << "," << headRange.second
            << "] pieceCount=" << diagPieceCount;
        // STREAM_ENGINE_FIX Phase 1.2 — head_deadlines event. Promotes the
        // Agent 4B temporary qDebug above to structured form. Both stay in
        // place per Phase 1.2 spec (qDebug as redundant safety net through
        // Phase 2-3 stabilization); Phase 4.1 removes the qDebug after
        // structured logs prove sufficient.
        writeTelemetry(QStringLiteral("head_deadlines"),
            QStringLiteral("hash=") + infoHash.left(8)
            + QStringLiteral(" idx=") + QString::number(fileIdx)
            + QStringLiteral(" pieces=[") + QString::number(headRange.first)
            + QStringLiteral(",") + QString::number(headRange.second)
            + QStringLiteral("] pieceCount=") + QString::number(diagPieceCount)
            + QStringLiteral(" headBytes=") + QString::number(kHeadBytes));

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

            // STREAM_ENGINE_REBUILD M3 (integration-memo §5) — per-piece
            // priority=7 pairing on head pieces. STREAM_ENGINE_FIX Phase
            // 2.6.3 empirically proved deadline-alone loses to libtorrent's
            // general scheduler under swarm pressure (priority+deadline
            // together win, deadline alone doesn't — applied verbatim in
            // prepareSeekTarget). Same invariant applies on cold-open head
            // pieces: priority=7 guarantees libtorrent schedules head
            // pieces before non-selected-file priority=1 pieces from
            // applyStreamPriorities, removing a scheduler-starvation path
            // under the old cold-open 15 s poll ceiling. Idempotent;
            // collapse-safe on repeat calls.
            for (int p = headRange.first; p <= headRange.second; ++p) {
                m_torrentEngine->setPiecePriority(infoHash, p, 7);
            }
        }
    }

    // STREAM_ENGINE_FIX Phase 2.2 — tail-metadata head deadline.
    //
    // MP4 containers without `+faststart` keep the `moov` atom at file end.
    // MKV may have cues at end. WebM similar. Pre-2.2 we set head deadlines
    // only; the sidecar's ffmpeg probe stalls minute-scale pulling the moov
    // from the slow tail of the file because libtorrent sees no urgency for
    // those pieces.
    //
    // Reference comparison: perpetus stream-server-master at
    // enginefs/src/backend/libtorrent/handle.rs:322-334 sets deadlines on
    // the LAST 2 PIECES with values 1200ms / 1250ms. They can use those
    // tight values because their head uses 0/10/20ms staircase across
    // MAX_STARTUP_PIECES=2 (priorities.rs:9), so 1200ms tail loses to
    // head easily — guaranteed by ~1180ms separation.
    //
    // Our head shape is different: 500ms→5000ms gradient across the first
    // 5MB (typically 2-3 pieces at 2-4MB each). The hard constraint is
    // tail's TIGHTEST deadline must be GREATER than head's SLOWEST (5000ms)
    // — libtorrent prioritizes by absolute deadline-ms, lower wins.
    //
    // Initial Phase 2.2 used 3000-6000ms for tail. EMPIRICAL REGRESSION
    // observed 2026-04-16 on hash=1575eafa (single-file 2.5GB source):
    // gate-pass took 86s vs prior run's <1s because tail piece 1023
    // (deadline 3000ms) OUTRANKED head piece 1 (deadline 5000ms);
    // libtorrent fed bandwidth to tail piece 1023 instead of completing
    // head piece 1. Gate stuck at 51% for ~60s with healthy 2-7 MB/s
    // bandwidth being spent on wrong pieces.
    //
    // Fix: bump tail to 6000-10000ms gradient. Tail's tightest (6000ms)
    // is now strictly greater than head's slowest (5000ms) → head ALWAYS
    // wins. Tail still gets aggressive coverage (within 6-10s) — sufficient
    // for moov-atom delivery before sidecar probe reaches it.
    //
    // 3 MB tail window covers typical MP4 moov atoms (100KB-2MB) + MKV
    // cue tables + WebM seek index tables with margin. Spans 1-2 pieces
    // at typical 2-4MB piece size — empirically similar coverage to
    // perpetus's "last 2 pieces" approach despite the different math.
    //
    // Skipped when file is smaller than tail window — head deadlines already
    // cover the whole file. Tail-deadline is set ONCE at stream start; not
    // re-applied on seek (seek has its own deadline logic via
    // prepareSeekTarget). Idempotency cost is zero since the function is
    // single-shot per onMetadataReady fire.
    if (rec.selectedFileSize > 0) {
        constexpr qint64 kTailBytes    = 3LL * 1024 * 1024;   // 3 MB
        // Phase 2.2 hotfix 2026-04-16: tail-FIRST must exceed head-LAST
        // (5000ms) by at least 1000ms so libtorrent's deadline-min-wins
        // scheduler always picks head pieces over tail pieces. Was
        // 3000-6000ms; observed gate-pass regression 1s → 86s on a
        // single-file 2.5GB source (hash=1575eafa).
        constexpr int    kTailFirstMs  = 6000;
        constexpr int    kTailLastMs   = 10000;
        if (rec.selectedFileSize > kTailBytes + kGateBytes) {
            // File is large enough to have a distinct tail region beyond
            // the head's 5 MB. kGateBytes (5 MB, hoisted class constant)
            // matches the head-deadline window size kHeadBytes used in the
            // local block above. Otherwise head + tail would overlap, and
            // head deadlines (more urgent) already cover everything.
            const qint64 tailOffset = rec.selectedFileSize - kTailBytes;
            const QPair<int, int> tailRange =
                m_torrentEngine->pieceRangeForFileOffset(infoHash, fileIdx,
                                                          tailOffset, kTailBytes);
            const int tailPieceCount =
                (tailRange.first >= 0 && tailRange.second >= tailRange.first)
                ? (tailRange.second - tailRange.first + 1) : 0;
            writeTelemetry(QStringLiteral("tail_deadlines"),
                QStringLiteral("hash=") + infoHash.left(8)
                + QStringLiteral(" idx=") + QString::number(fileIdx)
                + QStringLiteral(" pieces=[") + QString::number(tailRange.first)
                + QStringLiteral(",") + QString::number(tailRange.second)
                + QStringLiteral("] pieceCount=") + QString::number(tailPieceCount)
                + QStringLiteral(" tailBytes=") + QString::number(kTailBytes)
                + QStringLiteral(" tailOffset=") + QString::number(tailOffset));
            if (tailRange.first >= 0 && tailRange.second >= tailRange.first) {
                QList<QPair<int, int>> tailDeadlines;
                tailDeadlines.reserve(tailPieceCount);
                for (int i = 0; i < tailPieceCount; ++i) {
                    const int ms = (tailPieceCount <= 1)
                        ? kTailFirstMs
                        : kTailFirstMs + ((kTailLastMs - kTailFirstMs) * i)
                                         / (tailPieceCount - 1);
                    tailDeadlines.append({ tailRange.first + i, ms });
                }
                m_torrentEngine->setPieceDeadlines(infoHash, tailDeadlines);
            }
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
    rec.state = StreamSession::State::Serving;
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
    // STREAM_ENGINE_FIX Phase 2.4 — multi-file post-gate peer-collapse fix.
    //
    // Pre-2.4: non-selected files were priority 0 (skip). For single-file
    // torrents this is correct. For multi-file folder torrents (TV season
    // packs, anime collections, etc.) this triggered a catastrophic peer-
    // disconnect cascade: libtorrent's choking algorithm treats peers
    // serving exclusively-non-selected files as "unproductive" and
    // disconnects them within seconds of head-pieces being satisfied,
    // because there's no reciprocal benefit + we're not requesting
    // their pieces.
    //
    // Empirical evidence (Phase 1 telemetry, 2026-04-16, stream_telemetry.log
    // hash=4ad25536 — One Piece S02 8-episode pack, 24 files total): peer
    // count crashed 59→7 within 5s of gate-pass; bandwidth crashed 2.1MB/s
    // → 441KB/s → 1KB/s within 30s; stream effectively stalled for the
    // remainder of the 193s session. Despite swarm-level seeder count of
    // 500-1000+, only ~7 peers held pieces beyond head 5MB of file 0
    // because the rest had been disconnected on choking grounds.
    //
    // Fix: non-selected files use priority 1 (very low) instead of 0 (skip).
    // libtorrent treats us as still "interested" in those peers, preserving
    // connections + reciprocal-unchoke. Piece scheduling stays correct
    // because 7 (max for selected) >> 1 (non-selected) in libtorrent's
    // deadline + priority math — selected file pieces still win every
    // scheduling decision.
    //
    // Tradeoff: we'll trickle ~1-2% of bandwidth share toward non-selected
    // files (libtorrent allocates roughly proportional to priority weight).
    // Acceptable cost; the alternative is the peer-collapse symptom Hemanth
    // reported on the One Piece S02 EZTV pack 2026-04-16. Phase 1 telemetry
    // re-smoke confirms via stream_telemetry.log peer-count + dlBps fields
    // staying healthy post-gate-pass on multi-file sources.
    QVector<int> priorities(totalFiles, 1);  // very-low default keeps peers
    if (fileIndex >= 0 && fileIndex < totalFiles)
        priorities[fileIndex] = 7;  // max priority for selected file

    // STREAM diagnostic (Agent 4B — temporary trace for stream-head-gate
    // regression; remove after that bug closes).
    qDebug().nospace() << "[STREAM] applyPriorities infoHash="
        << infoHash.left(8) << " selected=" << fileIndex
        << " totalFiles=" << totalFiles;
    // STREAM_ENGINE_FIX Phase 1.2 — priorities event. Promotes the temp
    // qDebug above to structured form. Phase 2.4 default field captures
    // the priority-1-on-non-selected fix being active so future telemetry
    // reads can verify wiring.
    writeTelemetry(QStringLiteral("priorities"),
        QStringLiteral("hash=") + infoHash.left(8)
        + QStringLiteral(" selected=") + QString::number(fileIndex)
        + QStringLiteral(" totalFiles=") + QString::number(totalFiles)
        + QStringLiteral(" nonSelectedPriority=1"));

    m_torrentEngine->setFilePriorities(infoHash, priorities);
}
