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
}

void StreamPlayerController::startStream(const QString& imdbId, const QString& mediaType,
                                          int season, int episode,
                                          const Stream& selectedStream)
{
    stopStream();

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
        m_active = false;
        emit streamFailed(oneShot.errorMessage.isEmpty()
                              ? QStringLiteral("Failed to start direct stream")
                              : oneShot.errorMessage);
        return;
    }

    if (selectedStream.source.kind == StreamSource::Kind::YouTube) {
        m_active = false;
        emit streamFailed(QStringLiteral("YouTube playback not yet supported"));
        return;
    }

    // Magnet path: kick the polling loop.
    pollStreamStatus();
    m_pollTimer.start(POLL_FAST_MS);
}

void StreamPlayerController::stopStream()
{
    if (!m_active)
        return;

    m_pollTimer.stop();
    m_active = false;

    // Only magnet streams have an engine-side torrent to stop.
    if (m_selectedStream.source.kind == StreamSource::Kind::Magnet && !m_infoHash.isEmpty()) {
        m_engine->stopStream(m_infoHash);
    }

    m_infoHash.clear();
    m_selectedStream = {};

    emit streamStopped();
}

void StreamPlayerController::pollStreamStatus()
{
    if (!m_active)
        return;

    // Polling is magnet-only; direct paths returned early from startStream.
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startTimeMs;
    if (elapsed > HARD_TIMEOUT_MS) {
        m_pollTimer.stop();
        m_active = false;
        emit streamFailed("Stream timed out after "
                          + QString::number(HARD_TIMEOUT_MS / 1000) + "s");
        return;
    }

    auto result = m_engine->streamFile(m_selectedStream);

    if (m_infoHash.isEmpty() && !result.infoHash.isEmpty())
        m_infoHash = result.infoHash;

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
        m_pollTimer.stop();
        m_active = false;
        emit streamFailed(result.errorMessage);
        return;
    } else {
        statusText = "Connecting...";
    }

    m_lastErrorCode = result.errorCode;

    double bufferPercent = result.fileProgress * 100.0;
    emit bufferUpdate(statusText, bufferPercent);

    ++m_pollCount;
    if (m_pollCount == POLL_SLOW_AFTER && m_pollTimer.interval() != POLL_SLOW_MS)
        m_pollTimer.setInterval(POLL_SLOW_MS);
}

void StreamPlayerController::onStreamReady(const QString& url)
{
    emit readyToPlay(url);
}
