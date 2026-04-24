#include "core/stream/stremio/StreamServerEngine.h"

#include "core/stream/StreamTelemetryWriter.h"
#include "core/stream/stremio/StreamServerClient.h"
#include "core/stream/stremio/StreamServerProcess.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QPointer>

namespace {

// Video-extension heuristic for pickFileIndex's auto-select. Matches what
// StreamEngine::autoSelectVideoFile uses at StreamEngine.cpp ish for the
// libtorrent path — keep the two in lockstep so the same torrent selects the
// same file across backends.
bool isVideoExt(const QString& path)
{
    static const QStringList kVideoExts = {
        QStringLiteral("mkv"), QStringLiteral("mp4"),  QStringLiteral("avi"),
        QStringLiteral("mov"), QStringLiteral("m4v"),  QStringLiteral("webm"),
        QStringLiteral("wmv"), QStringLiteral("flv"),  QStringLiteral("ts"),
        QStringLiteral("m2ts"), QStringLiteral("mpg"), QStringLiteral("mpeg"),
    };
    const int dot = path.lastIndexOf('.');
    if (dot < 0 || dot == path.length() - 1) return false;
    const QString ext = path.mid(dot + 1).toLower();
    return kVideoExts.contains(ext);
}

}  // namespace

StreamServerEngine::StreamServerEngine(const QString& cacheDir, QObject* parent)
    : QObject(parent)
    , m_cacheDir(cacheDir)
{
    m_process = new StreamServerProcess(this);
    m_client  = new StreamServerClient(this);

    connect(m_process, &StreamServerProcess::ready,
            this, &StreamServerEngine::onProcessReady);
    connect(m_process, &StreamServerProcess::crashed,
            this, &StreamServerEngine::onProcessCrashed);
    connect(m_process, &StreamServerProcess::errorOccurred,
            this, &StreamServerEngine::onProcessError);
}

StreamServerEngine::~StreamServerEngine()
{
    // Process owns its own lifecycle dtor-shutdown; just make sure we don't
    // leak contexts (cancel tokens get flipped so any stray poll sees stop).
    // Phase 2B tuning — explicit stop() may not have been called (graceful
    // Qt X-button close tears us down via chained dtors). Emit `stopped`
    // here too so the telemetry log records the shutdown. Guarded on
    // m_started to avoid double-emit when stop() was called explicitly.
    if (m_started) {
        writeTelemetry(QStringLiteral("stopped"), QStringLiteral("via=dtor"));
        m_started = false;
    }
    stopAll();
}

// ═══════════ Engine lifecycle ═══════════

bool StreamServerEngine::start()
{
    if (m_started) return true;
    m_started = true;
    writeTelemetry(QStringLiteral("engine_started"),
                   QStringLiteral("backend=stream_server"));
    // Kicks subprocess; readiness fires async via onProcessReady().
    return m_process->start(m_cacheDir);
}

void StreamServerEngine::stop()
{
    if (!m_started) return;
    m_started = false;
    writeTelemetry(QStringLiteral("stopped"), QString());
    stopAll();
    m_process->shutdown();
}

void StreamServerEngine::writeTelemetry(const QString& event,
                                         const QString& body) const
{
    // Mirrors StreamEngine::writeTelemetry signature + line shape exactly so
    // existing parsers + scripts/runtime-health.ps1 keep working. Env gate
    // (TANKOBAN_STREAM_TELEMETRY=1) lives inside appendStreamTelemetryLine —
    // no-op when disabled.
    const QString line = QStringLiteral("[")
        + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + QStringLiteral("] event=") + event
        + (body.isEmpty() ? QString() : (QStringLiteral(" ") + body))
        + QStringLiteral("\n");
    appendStreamTelemetryLine(line);
}

void StreamServerEngine::cleanupOrphans()
{
    // Phase 1: no-op. stream-server manages its own cache under m_cacheDir.
    // Phase 2 can sweep empty .tankoboan_server_cache/<hash>/ subdirs if
    // desired; for now we let stream-server's LRU do its thing.
}

void StreamServerEngine::startPeriodicCleanup()
{
    // Phase 1: no-op. See cleanupOrphans.
}

// ═══════════ Process lifecycle slots ═══════════

void StreamServerEngine::onProcessReady(int port)
{
    m_processReady = true;
    m_client->setPort(port);
    qInfo() << "StreamServerEngine: stream-server ready on port" << port;
}

void StreamServerEngine::onProcessCrashed(int exitCode)
{
    m_processReady = false;
    m_startupError = QStringLiteral("stream-server exited with code %1").arg(exitCode);
    // Emit streamError per active stream so UI can clear state.
    for (auto it = m_contexts.begin(); it != m_contexts.end(); ++it) {
        if (it->cancelToken) {
            it->cancelToken->store(true, std::memory_order_release);
        }
        emit streamError(it.key(), m_startupError);
    }
}

void StreamServerEngine::onProcessError(const QString& message)
{
    m_startupError = message;
    qWarning().noquote() << "StreamServerEngine:" << message;
    // If start-up failed before any streamFile calls, no contexts exist yet
    // to emit on — the error surfaces on the next streamFile return
    // (errorCode=ENGINE_ERROR).
}

// ═══════════ Streaming API ═══════════

int StreamServerEngine::pickFileIndex(const QJsonObject& stats,
                                       const QString& hint,
                                       int callerOverride) const
{
    // 1. Caller-specified fileIndex wins if valid.
    const QJsonArray files = stats.value(QStringLiteral("files")).toArray();
    if (callerOverride >= 0 && callerOverride < files.size()) {
        return callerOverride;
    }

    // 2. Hint (filename substring) wins if it matches a file entry.
    if (!hint.isEmpty()) {
        for (int i = 0; i < files.size(); ++i) {
            const QString path = files.at(i).toObject()
                .value(QStringLiteral("path")).toString();
            if (path.contains(hint, Qt::CaseInsensitive)) {
                return i;
            }
        }
    }

    // 3. Auto-select: largest video file. If none is a known video
    //    extension, fall back to largest file overall.
    int bestVideoIdx = -1;
    qint64 bestVideoSize = 0;
    int bestAnyIdx = -1;
    qint64 bestAnySize = 0;
    for (int i = 0; i < files.size(); ++i) {
        const QJsonObject f = files.at(i).toObject();
        const qint64 len = static_cast<qint64>(
            f.value(QStringLiteral("length")).toDouble());
        const QString path = f.value(QStringLiteral("path")).toString();
        if (len > bestAnySize) { bestAnyIdx = i; bestAnySize = len; }
        if (isVideoExt(path) && len > bestVideoSize) {
            bestVideoIdx = i;
            bestVideoSize = len;
        }
    }
    if (bestVideoIdx >= 0) return bestVideoIdx;
    return bestAnyIdx;
}

bool StreamServerEngine::resolveSelectedFile(Context& ctx,
                                              const QJsonObject& stats) const
{
    const QJsonArray files = stats.value(QStringLiteral("files")).toArray();
    if (files.isEmpty()) return false;

    int idx = ctx.selectedFileIndex;
    if (idx < 0) {
        idx = pickFileIndex(stats, ctx.fileNameHint, ctx.requestedFileIndex);
    }
    if (idx < 0 || idx >= files.size()) return false;

    const QJsonObject f = files.at(idx).toObject();
    ctx.selectedFileIndex = idx;
    ctx.selectedFileSize  = static_cast<qint64>(
        f.value(QStringLiteral("length")).toDouble());
    ctx.selectedFileName  = f.value(QStringLiteral("path")).toString();
    return true;
}

void StreamServerEngine::refreshStats(const QString& infoHash)
{
    if (!m_client->isReady()) return;
    QPointer<StreamServerEngine> guard(this);
    m_client->getStats(infoHash,
        [guard, infoHash](bool ok, const QJsonObject& stats, const QString& err) {
            if (!guard) return;
            auto it = guard->m_contexts.find(infoHash);
            if (it == guard->m_contexts.end()) return;   // context cleaned up
            if (!ok) {
                it->lastError = err;
                return;
            }
            it->lastStats = stats;
            it->lastError.clear();
            if (it->selectedFileIndex < 0) {
                // Try to resolve now if we couldn't earlier (pre-metadata).
                guard->resolveSelectedFile(*it, stats);
                if (it->selectedFileIndex >= 0 && !it->telemFileSelected) {
                    it->telemFileSelected = true;
                    guard->writeTelemetry(
                        QStringLiteral("file_selected"),
                        QStringLiteral("hash=%1 idx=%2 name=%3 size=%4")
                            .arg(infoHash)
                            .arg(it->selectedFileIndex)
                            .arg(it->selectedFileName)
                            .arg(it->selectedFileSize));
                }
            }
            if (it->selectedFileIndex < 0) return;

            // Chain /:hash/:idx/stats.json for per-file streamProgress.
            // The top-level `downloaded` field is swarm-wide; we need the
            // file-specific fraction (availablePieces/filePieces) to paint
            // the seek-slider gray bar correctly. Fresh QPointer capture —
            // the outer guard is already in scope but its lifetime crosses
            // the nested callback boundary without issue.
            const int fileIdx = it->selectedFileIndex;
            guard->m_client->getFileStats(infoHash, fileIdx,
                [guard, infoHash, fileIdx](bool fok, const QJsonObject& fstats,
                                             const QString& ferr) {
                    if (!guard) return;
                    auto it2 = guard->m_contexts.find(infoHash);
                    if (it2 == guard->m_contexts.end()) return;
                    if (!fok) {
                        // Soft-fail — we keep the top-level stats. The gray
                        // bar will stay blank until a subsequent poll
                        // succeeds. Don't clobber top-level lastError.
                        qWarning().noquote()
                            << "StreamServerEngine: /:hash/:idx/stats.json failed"
                            << ferr;
                        return;
                    }
                    const double progress = fstats.value(
                        QStringLiteral("streamProgress")).toDouble(0.0);
                    it2->streamProgress = qBound(0.0, progress, 1.0);
                    // Derived byte count for UI + Phase 1's downloadedBytes
                    // consumers (replaces the zero-returning files[].downloaded
                    // read). Head-contiguous approximation — stream-server's
                    // bitfield may have gaps on mid-file seeks, but typical
                    // sequential-streaming playback makes this a good signal
                    // for "how far along is the download."
                    if (it2->selectedFileSize > 0) {
                        it2->downloadedBytes = static_cast<qint64>(
                            it2->streamProgress * static_cast<double>(it2->selectedFileSize));
                    }
                    // first_piece telemetry — fire once per context when we
                    // first observe any downloaded bytes.
                    if (!it2->telemFirstPiece && it2->downloadedBytes > 0) {
                        it2->telemFirstPiece = true;
                        guard->writeTelemetry(
                            QStringLiteral("first_piece"),
                            QStringLiteral("hash=%1 bytes=%2 progress=%3")
                                .arg(infoHash)
                                .arg(it2->downloadedBytes)
                                .arg(QString::number(it2->streamProgress, 'f', 4)));
                    }
                });
        });
}

StreamFileResult StreamServerEngine::streamFile(const QString& magnetUri,
                                                  int fileIndex,
                                                  const QString& fileNameHint)
{
    StreamFileResult r;

    if (!m_started) {
        r.errorCode    = QStringLiteral("ENGINE_ERROR");
        r.errorMessage = QStringLiteral("Stream engine not started");
        return r;
    }
    if (!m_processReady) {
        r.errorCode    = QStringLiteral("METADATA_NOT_READY");
        r.errorMessage = m_startupError.isEmpty()
            ? QStringLiteral("stream-server subprocess starting up")
            : m_startupError;
        return r;
    }

    // Phase 1 flow:
    //   First call on a new magnet: register context keyed BY magnetUri
    //     (tentative), kick POST /create. Callback migrates the entry to be
    //     keyed by the real infoHash once stream-server returns it. Return
    //     METADATA_NOT_READY on this call.
    //   Subsequent calls: find context by its magnetUri FIELD (not map key,
    //     since the key may have migrated). Refresh stats; return
    //     readyToStart=true once selected file has downloaded >= kReadyByteThreshold.

    // Find existing context for this magnet (match by magnetUri field).
    QString foundKey;
    for (auto it = m_contexts.begin(); it != m_contexts.end(); ++it) {
        if (it->magnetUri == magnetUri) {
            foundKey = it.key();
            break;
        }
    }

    if (foundKey.isEmpty()) {
        // First call — kick /create and stash a tentative entry keyed by
        // magnetUri. The callback below migrates the entry to be keyed by
        // the real infoHash once /create responds.
        Context ctx;
        ctx.magnetUri          = magnetUri;
        ctx.fileNameHint       = fileNameHint;
        ctx.requestedFileIndex = fileIndex;
        ctx.cancelToken        = std::make_shared<std::atomic<bool>>(false);
        ctx.createSent         = true;
        m_contexts[magnetUri] = ctx;

        QPointer<StreamServerEngine> guard(this);
        m_client->createTorrent(magnetUri,
            [guard, magnetUri](bool ok, const QString& hash,
                                const QJsonObject& body, const QString& err) {
                if (!guard) return;
                auto magIt = guard->m_contexts.find(magnetUri);
                if (!ok || hash.isEmpty()) {
                    qWarning().noquote()
                        << "StreamServerEngine: /create failed for" << magnetUri
                        << "err=" << err;
                    if (magIt != guard->m_contexts.end()) {
                        magIt->lastError = err;
                        emit guard->streamError(magnetUri, err);
                    }
                    return;
                }
                // Migrate: move tentative magnet-keyed context to real-hash key,
                // preserving the cancellation token shared_ptr so any worker
                // that captured it still observes stopStream's cancel flip.
                Context ctx2;
                if (magIt != guard->m_contexts.end()) {
                    ctx2 = *magIt;
                    guard->m_contexts.erase(magIt);
                }
                if (!ctx2.cancelToken) {
                    ctx2.cancelToken = std::make_shared<std::atomic<bool>>(false);
                }
                ctx2.createCompleted = true;
                ctx2.lastStats       = body;
                guard->resolveSelectedFile(ctx2, body);
                guard->m_contexts[hash] = ctx2;
                // metadata_ready telemetry (one-shot per context).
                auto& stored = guard->m_contexts[hash];
                if (!stored.telemMetadataReady) {
                    stored.telemMetadataReady = true;
                    guard->writeTelemetry(
                        QStringLiteral("metadata_ready"),
                        QStringLiteral("hash=%1 files=%2")
                            .arg(hash)
                            .arg(body.value(QStringLiteral("files")).toArray().size()));
                }
                // file_selected (if resolveSelectedFile picked a file at
                // /create time — common case when metadata is fully in the
                // create response).
                if (stored.selectedFileIndex >= 0 && !stored.telemFileSelected) {
                    stored.telemFileSelected = true;
                    guard->writeTelemetry(
                        QStringLiteral("file_selected"),
                        QStringLiteral("hash=%1 idx=%2 name=%3 size=%4")
                            .arg(hash)
                            .arg(stored.selectedFileIndex)
                            .arg(stored.selectedFileName)
                            .arg(stored.selectedFileSize));
                }
            });

        r.errorCode    = QStringLiteral("METADATA_NOT_READY");
        r.errorMessage = QStringLiteral("Starting stream...");
        return r;
    }

    // Subsequent poll: found a context.
    Context& ctx = m_contexts[foundKey];

    // If still keyed under magnetUri (callback hasn't fired yet), stay
    // METADATA_NOT_READY. `foundKey == magnetUri` when the /create callback
    // hasn't migrated the entry yet.
    if (!ctx.createCompleted || foundKey == magnetUri) {
        r.errorCode = QStringLiteral("METADATA_NOT_READY");
        return r;
    }

    // Bump the stats freshness so we see downloaded-bytes progress.
    refreshStats(foundKey);

    // Resolve selected file if not yet done.
    if (ctx.selectedFileIndex < 0) {
        r.errorCode = QStringLiteral("METADATA_NOT_READY");
        return r;
    }

    // STREAM_SERVER_PIVOT Phase 1 fix (2026-04-24 19:14) — chicken-and-egg
    // caught in the 18:45 smoke: stream-server doesn't START downloading
    // pieces until an HTTP consumer opens the stream URL via a Range GET.
    // Tankoban's sidecar is that consumer, but it only opens after we
    // return readyToStart=true. If we gate readyToStart on downloaded >=
    // 1 MB, the two halves deadlock: stream-server waits for a reader,
    // reader waits for bytes. Empirical evidence: 28 peers / 5 unchoked /
    // 0 downloaded over 30s in the smoke. Resolution: flip readyToStart
    // the moment metadata + file selection are resolved. Sidecar opens
    // URL, stream-server starts downloading, first-frame lands when
    // enough bytes arrive (stream-server's own read gate handles back-
    // pressure via HTTP 503 / slow reply on Range GET — ffmpeg tolerates
    // that via rw_timeout retry).
    r.ok                  = true;
    r.playbackMode        = StreamPlaybackMode::LocalHttp;
    r.url                 = m_client->buildStreamUrl(foundKey, ctx.selectedFileIndex);
    r.infoHash            = foundKey;
    r.readyToStart        = true;
    r.fileSize            = ctx.selectedFileSize;
    r.downloadedBytes     = ctx.downloadedBytes;
    r.fileProgress        = (ctx.selectedFileSize > 0)
        ? qMin(1.0, static_cast<double>(ctx.downloadedBytes) / static_cast<double>(ctx.selectedFileSize))
        : 0.0;
    r.selectedFileIndex   = ctx.selectedFileIndex;
    r.selectedFileName    = ctx.selectedFileName;

    // Emit streamReady exactly once per context when readyToStart flips true.
    if (!ctx.readyEmitted) {
        ctx.readyEmitted = true;
        emit streamReady(foundKey, r.url);
    }
    return r;
}

StreamFileResult StreamServerEngine::streamFile(
    const tankostream::addon::Stream& stream)
{
    // Delegate by source kind. Phase 1 supports magnet only; direct-URL
    // and http-url paths return an UNSUPPORTED_SOURCE for now (legacy
    // StreamEngine handles those via DirectUrl playback mode; stream-server
    // pivot intentionally stays narrow for Phase 1 — the streaming torrents
    // are what motivated the pivot).
    //
    // Actually — httpSource / urlSource are useful for trailers etc. So
    // bypass stream-server and return a DirectUrl result without going
    // through the subprocess at all.
    using namespace tankostream::addon;
    StreamFileResult r;
    switch (stream.source.kind) {
    case StreamSource::Kind::Magnet: {
        const QString magnet = stream.source.toMagnetUri();
        if (magnet.isEmpty()) {
            r.errorCode    = QStringLiteral("ENGINE_ERROR");
            r.errorMessage = QStringLiteral("Empty magnet URI");
            return r;
        }
        const int hintIdx     = stream.source.fileIndex;
        const QString hint    = stream.source.fileNameHint.isEmpty()
                                    ? stream.behaviorHints.filename
                                    : stream.source.fileNameHint;
        return streamFile(magnet, hintIdx, hint);
    }
    case StreamSource::Kind::Http:
    case StreamSource::Kind::Url: {
        r.ok           = true;
        r.playbackMode = StreamPlaybackMode::DirectUrl;
        r.url          = stream.source.url.toString();
        r.readyToStart = true;
        return r;
    }
    case StreamSource::Kind::YouTube:
    default:
        r.errorCode    = QStringLiteral("UNSUPPORTED_SOURCE");
        r.errorMessage = QStringLiteral("YouTube/unknown sources are not supported by stream-server pivot");
        return r;
    }
}

void StreamServerEngine::stopStream(const QString& infoHash)
{
    auto it = m_contexts.find(infoHash);
    if (it == m_contexts.end()) return;

    if (it->cancelToken) {
        it->cancelToken->store(true, std::memory_order_release);
    }

    if (!it->telemCancelledOrStopped) {
        it->telemCancelledOrStopped = true;
        writeTelemetry(QStringLiteral("cancelled"),
                       QStringLiteral("hash=%1 progress=%2 bytes=%3")
                           .arg(infoHash)
                           .arg(QString::number(it->streamProgress, 'f', 4))
                           .arg(it->downloadedBytes));
    }

    // Fire-and-forget /remove.
    if (m_client->isReady()) {
        m_client->removeTorrent(infoHash, [infoHash](bool ok, const QString& err) {
            if (!ok) {
                qWarning().noquote() << "StreamServerEngine: /remove failed for"
                                      << infoHash << "err=" << err;
            }
        });
    }
    m_contexts.erase(it);
}

void StreamServerEngine::stopAll()
{
    const auto keys = m_contexts.keys();
    for (const QString& k : keys) {
        stopStream(k);
    }
}

// ═══════════ Query API ═══════════

StreamTorrentStatus StreamServerEngine::torrentStatus(const QString& infoHash) const
{
    StreamTorrentStatus s;
    auto it = m_contexts.constFind(infoHash);
    if (it == m_contexts.constEnd()) return s;
    s.peers   = it->lastStats.value(QStringLiteral("peers")).toInt();
    s.dlSpeed = static_cast<int>(
        it->lastStats.value(QStringLiteral("downloadSpeed")).toDouble());
    return s;
}

StreamEngineStats StreamServerEngine::statsSnapshot(const QString& infoHash) const
{
    StreamEngineStats s;
    s.infoHash = infoHash;
    auto it = m_contexts.constFind(infoHash);
    if (it == m_contexts.constEnd()) return s;

    s.activeFileIndex   = it->selectedFileIndex;
    s.metadataReadyMs   = it->createCompleted ? 0 : -1;  // sentinel: we don't track ms since engine-start
    s.firstPieceArrivalMs = (it->downloadedBytes > 0) ? 0 : -1;
    s.gateProgressBytes = qMin(it->downloadedBytes, static_cast<qint64>(1LL * 1024 * 1024));
    s.gateSizeBytes     = 1LL * 1024 * 1024;
    s.gateProgressPct   = (s.gateSizeBytes > 0)
        ? qMin(100.0, 100.0 * static_cast<double>(s.gateProgressBytes) / static_cast<double>(s.gateSizeBytes))
        : 0.0;
    s.peers             = it->lastStats.value(QStringLiteral("peers")).toInt();
    s.dlSpeedBps        = static_cast<qint64>(
        it->lastStats.value(QStringLiteral("downloadSpeed")).toDouble());
    // stalled / stallElapsedMs / stallPiece / stallPeerHaveCount: default
    // sentinels (false / 0 / -1 / -1) — Phase 1 does not populate these.
    return s;
}

QList<QPair<qint64, qint64>> StreamServerEngine::contiguousHaveRanges(
    const QString& infoHash) const
{
    // Phase 2B — head-contiguous approximation.
    //
    // stream-server's /:hash/:idx/stats.json exposes a single
    // `streamProgress` scalar (fractional availablePieces/filePieces for the
    // file's piece range — server.js:18336) but NOT a per-piece bitfield.
    // That means we can compute "how many bytes are downloaded" but not
    // "where in the file those bytes live."
    //
    // The return contract for the seek-slider gray-paint is a list of
    // (startByte, endByte) pairs; we approximate that as a single range
    // from 0 to streamProgress * fileSize, ASSUMING stream-server's
    // piece-picker favors sequential head-to-tail delivery. That assumption
    // holds for typical playback-from-start; it under-paints (bar stops
    // short of the user's current position) right after a mid-file seek
    // until stream-server's new head-position stabilizes. Not-ideal but
    // strictly better than blank.
    //
    // If stream-server ever exposes a per-piece bitfield endpoint (the
    // code to emit it already exists internally at server.js:18427), we'd
    // replace this with a proper merged-contiguous-ranges walker.
    auto it = m_contexts.constFind(infoHash);
    if (it == m_contexts.constEnd()) return {};
    if (it->selectedFileSize <= 0 || it->streamProgress <= 0.0) return {};
    const qint64 end = static_cast<qint64>(
        it->streamProgress * static_cast<double>(it->selectedFileSize));
    if (end <= 0) return {};
    const qint64 endClamped = (end < it->selectedFileSize) ? end : it->selectedFileSize;
    QList<QPair<qint64, qint64>> ranges;
    ranges.append(QPair<qint64, qint64>(static_cast<qint64>(0), endClamped));
    return ranges;
}

// ═══════════ Playback-window / seek hooks (no-ops) ═══════════

void StreamServerEngine::updatePlaybackWindow(const QString& /*infoHash*/,
                                                double /*positionSec*/,
                                                double /*durationSec*/,
                                                qint64 /*windowBytes*/)
{
    // Phase 1: no-op. stream-server's internal piece-picker auto-extends
    // prefetch on Range-GET reads.
}

void StreamServerEngine::clearPlaybackWindow(const QString& /*infoHash*/)
{
    // Phase 1: no-op.
}

bool StreamServerEngine::prepareSeekTarget(const QString& /*infoHash*/,
                                             double /*positionSec*/,
                                             double /*durationSec*/,
                                             qint64 /*prefetchBytes*/)
{
    // Phase 1: always-true. stream-server services the sidecar's Range GET
    // directly; explicit seek pre-gating is unnecessary.
    return true;
}

std::shared_ptr<std::atomic<bool>> StreamServerEngine::cancellationToken(
    const QString& infoHash) const
{
    auto it = m_contexts.constFind(infoHash);
    if (it == m_contexts.constEnd()) return {};
    return it->cancelToken;
}
