#include "StreamPlayerController.h"

#include "core/CoreBridge.h"
#include "core/stream/StreamEngine.h"
#include "core/stream/StreamProgress.h"

#include <QDateTime>

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
                                          const QString& magnetUri, int fileIndex,
                                          const QString& fileNameHint)
{
    stopStream();

    m_active = true;
    m_imdbId = imdbId;
    m_mediaType = mediaType;
    m_season = season;
    m_episode = episode;
    m_magnetUri = magnetUri;
    m_fileIndex = fileIndex;
    m_fileNameHint = fileNameHint;
    m_pollCount = 0;
    m_startTimeMs = QDateTime::currentMSecsSinceEpoch();
    m_lastMetadataChangeMs = m_startTimeMs;
    m_lastErrorCode.clear();

    // First poll immediately
    pollStreamStatus();

    // Start polling timer
    m_pollTimer.start(POLL_FAST_MS);
}

void StreamPlayerController::stopStream()
{
    if (!m_active)
        return;

    m_pollTimer.stop();
    m_active = false;

    if (!m_infoHash.isEmpty()) {
        m_engine->stopStream(m_infoHash);
        m_infoHash.clear();
    }

    emit streamStopped();
}

void StreamPlayerController::pollStreamStatus()
{
    if (!m_active)
        return;

    // Check hard timeout
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startTimeMs;
    if (elapsed > HARD_TIMEOUT_MS) {
        m_pollTimer.stop();
        m_active = false;
        emit streamFailed("Stream timed out after " + QString::number(HARD_TIMEOUT_MS / 1000) + "s");
        return;
    }

    // Poll the engine
    auto result = m_engine->streamFile(m_magnetUri, m_fileIndex, m_fileNameHint);

    // Track the canonical info hash from the engine result
    if (m_infoHash.isEmpty() && !result.infoHash.isEmpty())
        m_infoHash = result.infoHash;

    if (result.ok && result.readyToStart) {
        // Stream is ready
        m_pollTimer.stop();
        onStreamReady(result.url);
        return;
    }

    // Build status text for UI
    QString statusText;
    auto torrentStatus = m_engine->torrentStatus(m_infoHash);

    if (result.errorCode == "METADATA_NOT_READY") {
        // Detect metadata stall
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastErrorCode != "METADATA_NOT_READY") {
            m_lastMetadataChangeMs = now;
        } else if (now - m_lastMetadataChangeMs > METADATA_STALL_MS) {
            statusText = "Metadata stalled. Torrent may be dead.";
            emit bufferUpdate(statusText, 0.0);
            m_lastErrorCode = result.errorCode;
            // Don't abort — let hard timeout handle it, but notify UI
            ++m_pollCount;
            return;
        }
        statusText = "Resolving metadata...";
    } else if (result.errorCode == "FILE_NOT_READY") {
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

        // Append elapsed time after 5s
        int elapsedSec = static_cast<int>(elapsed / 1000);
        if (elapsedSec >= 5)
            statusText += QString(" [%1s]").arg(elapsedSec);
    } else if (result.errorCode == "ENGINE_ERROR") {
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

    // Switch to slow polling after ~30s
    ++m_pollCount;
    if (m_pollCount == POLL_SLOW_AFTER && m_pollTimer.interval() != POLL_SLOW_MS)
        m_pollTimer.setInterval(POLL_SLOW_MS);
}

void StreamPlayerController::onStreamReady(const QString& url)
{
    emit readyToPlay(url);
}
