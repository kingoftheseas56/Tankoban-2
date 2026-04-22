#include "StreamPrioritizer.h"

#include "StreamTelemetryWriter.h"
#include "core/torrent/TorrentEngine.h"

#include <QDateTime>

namespace {

constexpr qint64 kDefaultSeekPrefetchBytes = 3LL * 1024 * 1024;
constexpr qint64 kRetryEscalationMs = 1500;
constexpr qint64 kTailMetadataBytes = 10LL * 1024 * 1024;

QString seekTypeName(StreamSeekClassifier::SeekType type)
{
    switch (type) {
    case StreamSeekClassifier::Sequential:
        return QStringLiteral("Sequential");
    case StreamSeekClassifier::InitialPlayback:
        return QStringLiteral("InitialPlayback");
    case StreamSeekClassifier::UserScrub:
        return QStringLiteral("UserScrub");
    case StreamSeekClassifier::ContainerMetadata:
        return QStringLiteral("ContainerMetadata");
    }
    return QStringLiteral("Unknown");
}

int seekBaseDeadlineForRetry(int retries)
{
    switch (retries) {
    case 0:
        return 500;
    case 1:
        return 300;
    default:
        return 200;
    }
}

}

StreamPrioritizer::StreamPrioritizer(TorrentEngine* engine)
    : m_engine(engine)
{
    m_clock.start();
}

void StreamPrioritizer::bindTorrent(TorrentEngine* engine,
                                    const QString& infoHash,
                                    int fileIndex,
                                    qint64 fileSize)
{
    m_engine = engine;
    m_infoHash = infoHash;
    m_fileIndex = fileIndex;
    m_fileSize = fileSize;
    resetSeekState();
    m_headPieces.clear();
    m_seekPieces.clear();
    if (!m_clock.isValid()) {
        m_clock.start();
    }
}

void StreamPrioritizer::updateDownloadRateEma(qint64 downloadRateBps)
{
    constexpr double kAlpha = 0.2;
    if (downloadRateBps <= 0) return;
    if (m_downloadRateEma <= 0.0) {
        m_downloadRateEma = static_cast<double>(downloadRateBps);
        return;
    }
    m_downloadRateEma =
        (kAlpha * static_cast<double>(downloadRateBps))
        + ((1.0 - kAlpha) * m_downloadRateEma);
}

int StreamPrioritizer::urgencyWindowPieces(qint64 bitrateBytesPerSec,
                                           qint64 pieceLength)
{
    if (pieceLength <= 0) return 15;
    if (bitrateBytesPerSec <= 0) return 15;
    const qint64 pieces = (bitrateBytesPerSec * 15 + pieceLength - 1) / pieceLength;
    return qBound(15, static_cast<int>(pieces), 60);
}

int StreamPrioritizer::headWindowPieces(qint64 bitrateBytesPerSec,
                                        qint64 pieceLength)
{
    if (pieceLength <= 0) return 5;
    if (bitrateBytesPerSec <= 0) return 5;
    const qint64 pieces = (bitrateBytesPerSec * 5 + pieceLength - 1) / pieceLength;
    return qBound(5, static_cast<int>(pieces), 250);
}

QList<QPair<int, int>> StreamPrioritizer::playbackDeadlines(int currentPiece,
                                                            int urgencyPieces,
                                                            int headPieces,
                                                            bool coldOpen)
{
    QList<QPair<int, int>> out;
    if (currentPiece < 0 || urgencyPieces <= 0) return out;

    out.reserve(urgencyPieces);
    for (int distance = 0; distance < urgencyPieces; ++distance) {
        const int piece = currentPiece + distance;
        int deadline = 0;
        if (coldOpen && distance == 0) {
            deadline = 0;
        } else if (distance < 5) {
            deadline = 10 + (distance * 50);
        } else if (distance < headPieces) {
            deadline = 250 + ((distance - 5) * 50);
        } else {
            deadline = 5000 + (distance * 20);
        }
        out.append({piece, deadline});
    }
    return out;
}

QList<QPair<int, int>> StreamPrioritizer::seekDeadlines(int startPiece,
                                                        int endPiece,
                                                        int baseDeadlineMs)
{
    QList<QPair<int, int>> out;
    if (startPiece < 0 || endPiece < startPiece) return out;

    out.reserve(endPiece - startPiece + 1);
    for (int piece = startPiece; piece <= endPiece; ++piece) {
        const int distance = piece - startPiece;
        out.append({piece, baseDeadlineMs + (distance * 10)});
    }
    return out;
}

void StreamPrioritizer::onPlaybackTick(const QString& hash,
                                       double posSec,
                                       double durSec,
                                       qint64 windowBytes)
{
    Q_UNUSED(windowBytes);

    if (!m_engine || hash != m_infoHash || m_fileIndex < 0 || m_fileSize <= 0) {
        return;
    }
    if (durSec <= 0.0 || posSec < 0.0) return;

    const qint64 byteOffset = clampByteOffset(
        static_cast<qint64>((qMin(1.0, posSec / durSec)) * m_fileSize));
    const PieceLayout layout = resolveLayout(byteOffset);
    if (layout.currentPiece < 0) return;

    const qint64 bitrate = qMax<qint64>(1, static_cast<qint64>(m_downloadRateEma));
    const int urgencyPieces = urgencyWindowPieces(bitrate, layout.pieceLength);
    const int headPieces = headWindowPieces(bitrate, layout.pieceLength);
    const bool coldOpen = posSec < 5.0 && layout.currentPiece == layout.firstPiece;

    QList<QPair<int, int>> deadlines =
        playbackDeadlines(layout.currentPiece, urgencyPieces, headPieces, coldOpen);
    if (layout.lastPiece >= layout.currentPiece) {
        while (!deadlines.isEmpty() && deadlines.last().first > layout.lastPiece) {
            deadlines.removeLast();
        }
    }
    if (deadlines.isEmpty()) return;

    m_engine->setPieceDeadlines(hash, deadlines);

    const QSet<int> nextHead = headWindowPieceSet(layout.currentPiece, headPieces);
    demoteStaleHeadPieces(hash, nextHead);
    applyPrioritySet(hash, nextHead, 7);
    m_headPieces = nextHead;

    if (layout.currentPiece == layout.firstPiece) {
        resetSeekState();
    }
}

bool StreamPrioritizer::onSeek(const QString& hash,
                               StreamSeekClassifier::SeekType type,
                               double targetPosSec,
                               qint64 targetByteOffset,
                               qint64 prefetchBytes)
{
    Q_UNUSED(targetPosSec);

    if (!m_engine || hash != m_infoHash || m_fileIndex < 0 || m_fileSize <= 0) {
        return false;
    }

    const qint64 byteOffset = clampByteOffset(targetByteOffset);
    const qint64 neededBytes = effectiveSeekBytes(byteOffset, prefetchBytes);
    if (neededBytes <= 0) return false;

    const PieceLayout layout = resolveLayout(byteOffset);
    if (layout.currentPiece < 0) return false;

    const bool ready =
        m_engine->haveContiguousBytes(hash, m_fileIndex, byteOffset, neededBytes);

    if (type == StreamSeekClassifier::Sequential
        || type == StreamSeekClassifier::InitialPlayback) {
        m_lastSeekTelemetry = {layout.currentPiece, ready, 0, -1};
        return ready;
    }

    const int endPieceRange = m_engine->pieceRangeForFileOffset(
        hash, m_fileIndex, byteOffset, neededBytes).second;
    if (endPieceRange < layout.currentPiece) return ready;

    const bool seekChanged = (m_activeSeekOffset != byteOffset);
    if (seekChanged) {
        resetSeekState();
        m_activeSeekOffset = byteOffset;
        m_seekStartedMs = m_clock.elapsed();
    }

    int retries = 0;
    if (m_seekStartedMs >= 0) {
        retries = static_cast<int>((m_clock.elapsed() - m_seekStartedMs) / kRetryEscalationMs);
    }
    retries = qBound(0, retries, 3);

    if (type == StreamSeekClassifier::UserScrub) {
        // M6 DEFENSIVE INVARIANT — clear libtorrent's global deadline
        // table at UserScrub entry. Old pre-seek deadlines otherwise
        // contend with new seek-target deadlines for time-critical
        // queue slots, causing stale ghost-deadlines to starve new
        // pieces. Mirrors pre-P3 StreamEngine behavior + Stremio
        // Reference backend/libtorrent/stream.rs:101-108. Ported from
        // pre-P3 HEAD StreamEngine.cpp:823-824.
        m_engine->clearPieceDeadlines(hash);

        demoteStaleHeadPieces(hash, {});
        applyPrioritySet(hash, m_seekPieces, 1);
        m_headPieces.clear();
        m_seekPieces.clear();

        const int peerHaveCount = m_engine->peersWithPiece(hash, layout.currentPiece);
        emitSeekTelemetry(type, layout.currentPiece, ready, retries, peerHaveCount, byteOffset);
        m_lastSeekTelemetry = {layout.currentPiece, ready, retries, peerHaveCount};

        if (!ready && retries > m_seekRetries) {
            const QList<QPair<int, int>> deadlines =
                seekDeadlines(layout.currentPiece, endPieceRange,
                              seekBaseDeadlineForRetry(retries));
            m_engine->setPieceDeadlines(hash, deadlines);

            QSet<int> nextSeekPieces;
            for (const auto& deadline : deadlines) {
                nextSeekPieces.insert(deadline.first);
            }
            applyPrioritySet(hash, nextSeekPieces, 7);
            m_seekPieces = nextSeekPieces;
            m_seekRetries = retries;
        }

        // M6 defensive tail-metadata deadline re-assertion. After the
        // clearPieceDeadlines above wipes the global table, moov/Cues
        // pieces at the tail would drop off libtorrent's time-critical
        // radar. The 3 MB tail range matches
        // StreamEngine::onMetadataReady's tail-priming block; deadline
        // gradient 6000-10000 ms linearly interpolated keeps tail
        // under time-critical pressure without competing with head.
        // Ported from pre-P3 HEAD StreamEngine.cpp:845-866.
        constexpr qint64 kTailBytes    = 3LL * 1024 * 1024;
        constexpr int    kTailFirstMs  = 6000;
        constexpr int    kTailLastMs   = 10000;
        // Matches StreamEngine kGateBytes post-P4 (1 MB tier 1).
        // Guard only matters for very small files where head + tail
        // overlap; stream-mode torrent files are always >> 4 MB so
        // this is a defensive no-op in practice.
        constexpr qint64 kGateBytes    = 1LL * 1024 * 1024;
        if (m_fileSize > kTailBytes + kGateBytes) {
            const qint64 tailOffset = m_fileSize - kTailBytes;
            const QPair<int, int> tailRange =
                m_engine->pieceRangeForFileOffset(hash, m_fileIndex,
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
                m_engine->setPieceDeadlines(hash, tailDeadlines);
            }
        }

        return ready;
    }

    const int peerHaveCount = m_engine->peersWithPiece(hash, layout.currentPiece);
    emitSeekTelemetry(type, layout.currentPiece, ready, 0, peerHaveCount, byteOffset);
    m_lastSeekTelemetry = {layout.currentPiece, ready, 0, peerHaveCount};

    if (!ready) {
        const QList<QPair<int, int>> deadlines =
            seekDeadlines(layout.currentPiece, qMin(layout.currentPiece + 1, endPieceRange), 100);
        m_engine->setPieceDeadlines(hash, deadlines);

        QSet<int> nextSeekPieces;
        for (const auto& deadline : deadlines) {
            nextSeekPieces.insert(deadline.first);
        }
        applyPrioritySet(hash, nextSeekPieces, 7);
        m_seekPieces = nextSeekPieces;
    }

    return ready;
}

void StreamPrioritizer::clearPlaybackWindow(const QString& hash)
{
    if (!m_engine || hash != m_infoHash) return;
    applyPrioritySet(hash, m_headPieces, 1);
    applyPrioritySet(hash, m_seekPieces, 1);
    m_headPieces.clear();
    m_seekPieces.clear();
    m_engine->clearPieceDeadlines(hash);
    resetSeekState();
}

StreamPrioritizer::PieceLayout StreamPrioritizer::resolveLayout(qint64 byteOffset) const
{
    PieceLayout layout;
    if (!m_engine || m_fileIndex < 0 || m_fileSize <= 0) return layout;

    const auto current = m_engine->pieceRangeForFileOffset(
        m_infoHash, m_fileIndex, byteOffset, 1);
    const auto first = m_engine->pieceRangeForFileOffset(
        m_infoHash, m_fileIndex, 0, 1);
    const auto last = m_engine->pieceRangeForFileOffset(
        m_infoHash, m_fileIndex, qMax<qint64>(0, m_fileSize - 1), 1);

    if (current.first < 0 || first.first < 0 || last.second < first.first) {
        return layout;
    }

    layout.currentPiece = current.first;
    layout.firstPiece = first.first;
    layout.lastPiece = last.second;
    layout.totalPieces = (layout.lastPiece - layout.firstPiece) + 1;
    layout.pieceLength = qMax<qint64>(
        1, (m_fileSize + layout.totalPieces - 1) / layout.totalPieces);
    return layout;
}

qint64 StreamPrioritizer::clampByteOffset(qint64 byteOffset) const
{
    if (m_fileSize <= 0) return 0;
    if (byteOffset < 0) return 0;
    if (byteOffset >= m_fileSize) return m_fileSize - 1;
    return byteOffset;
}

qint64 StreamPrioritizer::effectiveSeekBytes(qint64 byteOffset,
                                             qint64 requestedBytes) const
{
    const qint64 effectiveRequested =
        (requestedBytes > 0) ? requestedBytes : kDefaultSeekPrefetchBytes;
    return qMin(effectiveRequested, qMax<qint64>(0, m_fileSize - byteOffset));
}

QSet<int> StreamPrioritizer::headWindowPieceSet(int currentPiece,
                                                int headPieces) const
{
    QSet<int> pieces;
    if (currentPiece < 0 || headPieces <= 0) return pieces;

    const int lastHeadPiece = qMin(currentPiece + headPieces - 1,
                                   resolveLayout(clampByteOffset(0)).lastPiece);
    for (int piece = currentPiece; piece <= lastHeadPiece; ++piece) {
        pieces.insert(piece);
    }
    return pieces;
}

QSet<int> StreamPrioritizer::tailPieceSet() const
{
    QSet<int> pieces;
    if (!m_engine || m_fileIndex < 0 || m_fileSize <= kTailMetadataBytes) {
        return pieces;
    }

    const qint64 tailOffset = qMax<qint64>(0, m_fileSize - kTailMetadataBytes);
    const auto range = m_engine->pieceRangeForFileOffset(
        m_infoHash, m_fileIndex, tailOffset, kTailMetadataBytes);
    for (int piece = range.first; piece >= 0 && piece <= range.second; ++piece) {
        pieces.insert(piece);
    }
    return pieces;
}

void StreamPrioritizer::applyPrioritySet(const QString& hash,
                                         const QSet<int>& pieces,
                                         int priority) const
{
    if (!m_engine) return;
    for (int piece : pieces) {
        m_engine->setPiecePriority(hash, piece, priority);
    }
}

void StreamPrioritizer::demoteStaleHeadPieces(const QString& hash,
                                              const QSet<int>& nextHeadPieces)
{
    QSet<int> stale = m_headPieces;
    stale.subtract(nextHeadPieces);
    stale.subtract(tailPieceSet());
    applyPrioritySet(hash, stale, 1);
}

void StreamPrioritizer::emitSeekTelemetry(StreamSeekClassifier::SeekType type,
                                          int pieceIndex,
                                          bool ready,
                                          int retries,
                                          int peerHaveCount,
                                          qint64 byteOffset) const
{
    if (!streamTelemetryEnabled()) return;

    const QString line = QStringLiteral("[")
        + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + QStringLiteral("] event=seek_target hash=") + m_infoHash.left(8)
        + QStringLiteral(" piece=") + QString::number(pieceIndex)
        + QStringLiteral(" ready=") + (ready ? QStringLiteral("1") : QStringLiteral("0"))
        + QStringLiteral(" retries=") + QString::number(retries)
        + QStringLiteral(" peer_have_count=") + QString::number(peerHaveCount)
        + QStringLiteral(" type=") + seekTypeName(type)
        + QStringLiteral(" byteOffset=") + QString::number(byteOffset)
        + QStringLiteral("\n");
    appendStreamTelemetryLine(line);
}

void StreamPrioritizer::resetSeekState()
{
    m_activeSeekOffset = -1;
    m_seekStartedMs = -1;
    m_seekRetries = -1;
    m_lastSeekTelemetry = {};
}
