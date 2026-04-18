#include "StreamPlayerController.h"

#include "core/CoreBridge.h"
#include "core/stream/StreamEngine.h"
#include "core/stream/StreamProgress.h"

#include <QDateTime>

using tankostream::addon::Stream;
using tankostream::addon::StreamSource;

StreamPlayerController::StreamPlayerController(CoreBridge* bridge, StreamEngine* engine,
                                               QObject* parent)
    : QObject(parent)
    , m_bridge(bridge)
    , m_engine(engine)
{
    m_pollTimer.setSingleShot(false);
    connect(&m_pollTimer, &QTimer::timeout, this, &StreamPlayerController::pollStreamStatus);

    // STREAM_LIFECYCLE_FIX Phase 3 Batch 3.3 — wire engine-side error signal
    // into the controller. One-time connect here (controller lives for the
    // app session; no per-startStream re-wire needed). Emit sites in
    // StreamEngine: StreamEngine.cpp:517 (no-video torrent) + :620 (generic
    // engine error). Pre-3.3 both were dangling — zero connections found in
    // repo. The 120s HARD_TIMEOUT was the only way out.
    if (m_engine) {
        connect(m_engine, &StreamEngine::streamError, this,
                &StreamPlayerController::onEngineStreamError);
    }
}

void StreamPlayerController::startStream(const QString& imdbId, const QString& mediaType,
                                          int season, int episode,
                                          const Stream& selectedStream)
{
    // STREAM_LIFECYCLE_FIX Phase 2 Batch 2.1 — flag this defensive stop as
    // Replacement so StreamPage's handler can (once Batch 2.2 lands) skip
    // session-teardown UX that the incoming session will re-use.
    stopStream(StopReason::Replacement);

    m_active = true;
    m_imdbId = imdbId;
    m_mediaType = mediaType;
    m_season = season;
    m_episode = episode;
    m_selectedStream = selectedStream;
    m_pollCount = 0;
    m_startTimeMs = QDateTime::currentMSecsSinceEpoch();
    m_lastMetadataChangeMs = m_startTimeMs;
    m_lastErrorCode.clear();

    // Direct URL/HTTP: one-shot handoff, skip polling.
    if (selectedStream.source.kind == StreamSource::Kind::Http
        || selectedStream.source.kind == StreamSource::Kind::Url) {
        const StreamFileResult oneShot = m_engine->streamFile(m_selectedStream);
        if (oneShot.ok && oneShot.readyToStart) {
            onStreamReady(oneShot.url);
            return;
        }
        // Batch 2.2 — stopStream(Failure) clears m_infoHash + m_selectedStream
        // + m_active and emits streamStopped(Failure). onStreamFailed below
        // then drives the user-facing failure UX. Emit order matters: the
        // stopStream call is gated on m_active == true, which is still true
        // here (set in startStream's entry block), so stopStream's body
        // actually executes. Pre-2.2 these paths just set m_active = false
        // inline + emitted streamFailed, leaving m_infoHash / m_selectedStream
        // stale for a subsequent retry (audit P1-1 class).
        stopStream(StopReason::Failure);
        emit streamFailed(oneShot.errorMessage.isEmpty()
                              ? QStringLiteral("Failed to start direct stream")
                              : oneShot.errorMessage);
        return;
    }

    if (selectedStream.source.kind == StreamSource::Kind::YouTube) {
        stopStream(StopReason::Failure);
        emit streamFailed(QStringLiteral("YouTube playback not yet supported"));
        return;
    }

    // Magnet path: kick the polling loop.
    pollStreamStatus();
    m_pollTimer.start(POLL_FAST_MS);
}

void StreamPlayerController::stopStream(StopReason reason)
{
    if (!m_active)
        return;

    m_pollTimer.stop();
    m_active = false;

    // Only magnet streams have an engine-side torrent to stop. Read m_infoHash
    // + m_selectedStream BEFORE clearSessionState clears them.
    if (m_selectedStream.source.kind == StreamSource::Kind::Magnet && !m_infoHash.isEmpty()) {
        m_engine->stopStream(m_infoHash);
    }

    // STREAM_LIFECYCLE_FIX Phase 3 Batch 3.1 — route the per-session clears
    // through the named helper so Batch 3.3's StreamEngine::streamError slot
    // + any future failure-flow consumer can call the same shape without
    // duplicating the field list.
    clearSessionState();

    emit streamStopped(reason);
}

void StreamPlayerController::clearSessionState()
{
    m_infoHash.clear();
    m_selectedStream = {};
    m_pollCount = 0;
    m_lastErrorCode.clear();
    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.2 — reset buffered-range
    // dedup state + file-size cache so the next session's first emit
    // always fires (forces SeekSlider to paint from a clean slate instead
    // of inheriting the previous session's last snapshot as a false
    // "unchanged" match), and the fileSize cache doesn't leak across
    // sessions.
    m_lastBufferedRanges.clear();
    m_lastBufferedFileSize = 0;
    m_currentFileSize      = 0;
}

void StreamPlayerController::onEngineStreamError(const QString& infoHash,
                                                 const QString& message)
{
    // Hash-gate: StreamEngine may emit streamError for streams this controller
    // doesn't own (multi-stream engine lifetime is plausible). Drop silently
    // on mismatch — the responsible controller (if any) will hear its own.
    // Empty m_infoHash is also a mismatch (direct HTTP/URL or YouTube streams
    // never populate m_infoHash; their failure paths are inline in startStream).
    if (infoHash.isEmpty() || m_infoHash != infoHash) return;
    if (!m_active) return;  // stopStream already ran — avoid double-emit.

    // Same failure-dispatch shape as the poll-timeout + unsupported-source paths
    // in pollStreamStatus: route state-clear through stopStream(Failure) so
    // streamStopped(Failure) fires + m_infoHash clears + engine-side torrent
    // stop fires (if magnet), then emit streamFailed for StreamPage.onStreamFailed
    // to drive the 3s display window.
    stopStream(StopReason::Failure);
    emit streamFailed(message);
}

void StreamPlayerController::pollStreamStatus()
{
    if (!m_active)
        return;

    // Polling is magnet-only; direct paths returned early from startStream.
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startTimeMs;
    if (elapsed > HARD_TIMEOUT_MS) {
        // Batch 2.2 — route timeout through stopStream(Failure) so StreamEngine
        // gets the torrent-stop call + m_infoHash clears. Pre-2.2 set
        // m_active = false inline, leaving the torrent running in engine.
        stopStream(StopReason::Failure);
        emit streamFailed("Stream timed out after "
                          + QString::number(HARD_TIMEOUT_MS / 1000) + "s");
        return;
    }

    auto result = m_engine->streamFile(m_selectedStream);

    if (m_infoHash.isEmpty() && !result.infoHash.isEmpty())
        m_infoHash = result.infoHash;

    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.2 — refresh file-size cache
    // on every poll while we're still in startup phase. Value stabilizes
    // once metadata lands + stays valid through playback (file byte-size
    // is immutable post-metadata-ready). pollBufferedRangesOnce reads
    // this cache in place of a fresh streamFile() call.
    if (result.fileSize > 0)
        m_currentFileSize = result.fileSize;

    if (result.ok && result.readyToStart) {
        m_pollTimer.stop();
        onStreamReady(result.url);
        return;
    }

    QString statusText;
    auto torrentStatus = m_engine->torrentStatus(m_infoHash);

    if (result.errorCode == "METADATA_NOT_READY") {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastErrorCode != "METADATA_NOT_READY") {
            m_lastMetadataChangeMs = now;
        } else if (now - m_lastMetadataChangeMs > METADATA_STALL_MS) {
            statusText = "Metadata stalled. Torrent may be dead.";
            emit bufferUpdate(statusText, 0.0);
            m_lastErrorCode = result.errorCode;
            ++m_pollCount;
            return;
        }
        statusText = "Resolving metadata...";
    } else if (result.errorCode == "FILE_NOT_READY") {
        // STREAM_PLAYBACK_FIX Phase 3 Batch 3.2 — fileProgress +
        // downloadedBytes are gate progress (contiguous head / 5 MB target),
        // not whole-file progress. So "Buffering 60% (3.0 MB)" now means
        // "head window is 60% contiguous, 3.0 MB of head is on disk" —
        // reaches 100% exactly when ffmpeg's probe can run without HTTP
        // piece-waits. Previously this read as whole-file progress which
        // misled users into seeing e.g. 15% while real readiness lagged.
        int pct = static_cast<int>(result.fileProgress * 100.0);
        double mbDownloaded = result.downloadedBytes / (1024.0 * 1024.0);
        double speedMBs = torrentStatus.dlSpeed / (1024.0 * 1024.0);

        statusText = QString("Buffering... %1% (%2 MB)")
            .arg(pct)
            .arg(mbDownloaded, 0, 'f', 1);

        if (torrentStatus.peers > 0 || speedMBs > 0.01) {
            statusText += QString(" \u2014 %1 peers, %2 MB/s")
                .arg(torrentStatus.peers)
                .arg(speedMBs, 0, 'f', 1);
        }

        int elapsedSec = static_cast<int>(elapsed / 1000);
        if (elapsedSec >= 5)
            statusText += QString(" [%1s]").arg(elapsedSec);
    } else if (result.errorCode == "ENGINE_ERROR"
               || result.errorCode == "UNSUPPORTED_SOURCE") {
        // Batch 2.2 — engine error / unsupported source: route through
        // stopStream(Failure) for consistent cleanup (torrent stop + hash
        // clear + streamStopped(Failure) observability signal).
        stopStream(StopReason::Failure);
        emit streamFailed(result.errorMessage);
        return;
    } else {
        statusText = "Connecting...";
    }

    m_lastErrorCode = result.errorCode;

    double bufferPercent = result.fileProgress * 100.0;
    emit bufferUpdate(statusText, bufferPercent);

    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.2 — startup-phase buffered-
    // range emit. Same call shape as the playback-phase call from
    // StreamPage's progressUpdated lambda, so the SeekSlider receives
    // consistent updates across both lifecycle phases via a single signal.
    pollBufferedRangesOnce();

    ++m_pollCount;
    if (m_pollCount == POLL_SLOW_AFTER && m_pollTimer.interval() != POLL_SLOW_MS)
        m_pollTimer.setInterval(POLL_SLOW_MS);
}

// PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.2 — on-demand buffered-range
// snapshot + emit. Called from two sites:
//   1. pollStreamStatus (startup phase, until readyToStart stops the timer)
//   2. StreamPage's progressUpdated lambda (playback phase — 2s-rate-
//      limited at the same site as updatePlaybackWindow). StreamPage owns
//      the playback lifecycle hook so this method stays pull-driven
//      rather than timer-pushed, avoiding a lifecycle change in
//      StreamPlayerController that Agent 4 flagged as a sensitive site
//      post-STREAM_LIFECYCLE_FIX (Rule 10 chat.md:3354).
// Reads m_currentFileSize (cached from pollStreamStatus's streamFile
// result). Equality-deduped against m_lastBufferedRanges +
// m_lastBufferedFileSize so steady-state polls don't trigger SeekSlider
// repaints.
void StreamPlayerController::pollBufferedRangesOnce()
{
    if (!m_active) return;
    if (m_infoHash.isEmpty()) return;  // Pre-metadata or non-magnet stream.
    if (!m_engine) return;
    if (m_currentFileSize <= 0) return;  // Metadata not ready yet; next poll.

    QList<QPair<qint64, qint64>> ranges = m_engine->contiguousHaveRanges(m_infoHash);

    if (ranges == m_lastBufferedRanges && m_currentFileSize == m_lastBufferedFileSize)
        return;  // Unchanged — skip emit.

    m_lastBufferedRanges   = ranges;
    m_lastBufferedFileSize = m_currentFileSize;
    emit bufferedRangesChanged(m_infoHash, ranges, m_currentFileSize);
}

void StreamPlayerController::onStreamReady(const QString& url)
{
    emit readyToPlay(url);
}
