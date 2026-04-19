#include "StreamPrioritizer.h"

#include <QtGlobal>

#include <algorithm>

namespace StreamPrioritizer {

namespace {

// Stremio priorities.rs:78 — base urgent window is the max of 15 pieces OR
// `bitrate * 15s / piece_length`. With unknown bitrate (bitrate = 0) the
// 15-piece floor applies.
int urgentBasePieces(qint64 bitrate, qint64 pieceLength)
{
    int base = 15;
    if (bitrate > 0 && pieceLength > 0) {
        const qint64 pieces15s = (bitrate * 15) / pieceLength;
        base = qMax<int>(base, static_cast<int>(pieces15s));
    }
    return base;
}

// Stremio priorities.rs:87-100 — if download outpaces bitrate by 1.5×, add
// a 45 s proactive lookahead window. If bitrate is unknown and speed is
// high (> 5 MB/s), add a fixed 20 pieces.
int proactiveBonusPieces(qint64 bitrate, qint64 downloadSpeed, qint64 pieceLength)
{
    if (pieceLength <= 0) return 0;
    if (bitrate > 0) {
        if (downloadSpeed > (bitrate * 15 / 10)) {  // 1.5× bitrate
            return static_cast<int>((bitrate * 45) / pieceLength);
        }
        return 0;
    }
    // Bitrate unknown fallback
    if (downloadSpeed > 5LL * 1024 * 1024) return 20;
    return 0;
}

}  // namespace

QList<QPair<int, int>> calculateStreamingPriorities(const Params& p)
{
    QList<QPair<int, int>> out;
    if (p.pieceLength <= 0 || p.currentPiece < 0 || p.totalPieces <= 0) {
        return out;
    }

    // § 1. Dynamic window sizing (priorities.rs:72-122).
    const int urgentBase = urgentBasePieces(p.bitrate, p.pieceLength);
    const int proactive  = proactiveBonusPieces(p.bitrate, p.downloadSpeed, p.pieceLength);

    int urgentWindow = 0;
    int bufferWindow = 0;
    int maxBufferPieces = 0;

    if (p.cacheEnabled) {
        const int maxPieces = static_cast<int>(p.cacheSizeBytes / p.pieceLength);
        int urgent = urgentBase + proactive;
        urgent = qMin(urgent, maxPieces);
        urgent = qMin(urgent, 300);  // absolute cap
        urgentWindow = urgent;

        const int remaining = qMax(0, maxPieces - urgent);
        bufferWindow = qMin(15, remaining);
        maxBufferPieces = maxPieces;
    } else {
        // Cache-disabled "strict streaming" mode (priorities.rs:117-122).
        urgentWindow = qMin(urgentBase + proactive, 50);
        bufferWindow = 0;
        maxBufferPieces = urgentWindow;
    }

    // § 2. Head window (priorities.rs:126-147). 5 s of bitrate OR download
    // speed, bounded to [5 MB, 50 MB], then divided by piece length and
    // clamped to [5, 250] pieces.
    constexpr qint64 kTargetBufferSeconds = 5;
    constexpr qint64 kMinBufferBytes = 5LL * 1024 * 1024;
    constexpr qint64 kMaxBufferBytes = 50LL * 1024 * 1024;

    qint64 targetHeadBytes = 0;
    if (p.bitrate > 0) {
        targetHeadBytes = qBound(kMinBufferBytes,
                                  p.bitrate * kTargetBufferSeconds,
                                  kMaxBufferBytes);
    } else {
        targetHeadBytes = qBound(kMinBufferBytes,
                                  p.downloadSpeed * kTargetBufferSeconds,
                                  kMaxBufferBytes);
    }

    int headWindow = static_cast<int>(targetHeadBytes / p.pieceLength);
    headWindow = qBound(5, headWindow, 250);

    // Stremio priorities.rs:150: urgent must be at least head + 15.
    urgentWindow = qMax(urgentWindow, headWindow + 15);

    const int totalWindow = urgentWindow + bufferWindow;
    if (totalWindow <= 0) return out;

    // § 3. End-piece clamping (priorities.rs:158-167).
    const int startPiece = p.currentPiece;
    int endPiece = qMin(startPiece + totalWindow - 1, p.totalPieces - 1);

    int allowedEnd = (maxBufferPieces > 0)
        ? qMin(startPiece + maxBufferPieces - 1, p.totalPieces - 1)
        : (startPiece - 1);

    const int effectiveEnd = qMin(endPiece, allowedEnd);
    if (effectiveEnd < startPiece) return out;

    // § 4. Per-piece deadline staircase (priorities.rs:180-222).
    out.reserve(effectiveEnd - startPiece + 1);
    for (int pidx = startPiece; pidx <= effectiveEnd; ++pidx) {
        const int distance = pidx - startPiece;
        int deadline = 0;

        if (p.priorityLevel >= 250) {
            // Metadata / probes — absolute priority
            deadline = 50;
        } else if (p.priorityLevel >= 100) {
            // Seeking tier
            deadline = 10 + (distance * 10);
        } else if (p.priorityLevel == 0) {
            // Background pre-cache — lazy deadlines
            deadline = 20000 + (distance * 200);
        } else {
            // Normal streaming (priority=1 default)
            if (distance < 5) {
                // CRITICAL HEAD staircase: 10/60/110/160/210 ms
                // (M5 note: this is NOT the cold-open 0ms URGENT path —
                // that's handled via seekDeadlines(InitialPlayback) which
                // ports handle.rs instead of priorities.rs.)
                deadline = 10 + (distance * 50);
            } else if (distance < headWindow) {
                deadline = 250 + ((distance - 5) * 50);
            } else {
                const bool isProactive = (distance > urgentBase);
                deadline = isProactive
                    ? (10000 + (distance * 50))
                    : (5000 + (distance * 20));
            }
        }

        out.append({ pidx, deadline });
    }

    return out;
}

int initialPlaybackWindowSize(qint64 fileSize, qint64 pieceLength)
{
    if (pieceLength <= 0 || fileSize <= 0) return kMinStartupPieces;

    // handle.rs:266-268: effective target = min(MIN_STARTUP_BYTES, fileSize/20).max(pieceLength)
    const qint64 fileSizeFifth = fileSize / 20;  // 5 % of file
    qint64 effective = qMin(kMinStartupBytes, fileSizeFifth);
    effective = qMax(effective, pieceLength);

    // handle.rs:270-272: pieces_needed = ceil(effective / piece_length)
    const qint64 piecesNeeded = (effective + pieceLength - 1) / pieceLength;

    // handle.rs:273-274: clamp to [MIN_STARTUP_PIECES, MAX_STARTUP_PIECES]
    return qBound<int>(kMinStartupPieces,
                        static_cast<int>(piecesNeeded),
                        kMaxStartupPieces);
}

QList<QPair<int, int>> seekDeadlines(StreamSeekType type,
                                      int startPiece,
                                      int lastPiece,
                                      qint64 fileSize,
                                      qint64 pieceLength,
                                      double speedFactor)
{
    QList<QPair<int, int>> out;
    if (startPiece < 0 || lastPiece < startPiece) return out;

    int baseDeadline = 0;
    int windowSize = 0;

    switch (type) {
    case StreamSeekType::InitialPlayback:
        baseDeadline = 0;  // URGENT — stays 0 ms regardless of speedFactor
        windowSize = initialPlaybackWindowSize(fileSize, pieceLength);
        break;
    case StreamSeekType::UserScrub:
        baseDeadline = 300;  // CRITICAL
        windowSize = 4;
        break;
    case StreamSeekType::ContainerMetadata:
        baseDeadline = 100;  // CONTAINER-INDEX
        windowSize = 2;
        break;
    case StreamSeekType::Sequential:
        // Normal-streaming path — return empty; caller uses
        // calculateStreamingPriorities for this seek type.
        return out;
    }

    // handle.rs:287-292: speedFactor multiplies base deadline except for
    // URGENT (base 0 stays at 0 under any factor because 0 * x = 0).
    const int adjustedBase = (baseDeadline == 0)
        ? 0
        : static_cast<int>(baseDeadline * speedFactor);

    out.reserve(windowSize);
    for (int i = 0; i < windowSize; ++i) {
        const int pidx = startPiece + i;
        if (pidx > lastPiece) break;
        const int deadline = adjustedBase + (i * 10);  // handle.rs:309 staircase
        out.append({ pidx, deadline });
    }

    return out;
}

QList<QPair<int, int>> initialPlaybackTailDeadlines(int startPiece,
                                                     int lastPiece,
                                                     int headWindowSize)
{
    QList<QPair<int, int>> out;
    // handle.rs:324-331: only set tail deadlines if the tail is strictly
    // beyond the head window (`last_piece > actual_start_piece + window_size`).
    if (lastPiece <= startPiece + headWindowSize) return out;

    // last_piece @ 1200 ms, last_piece-1 @ 1250 ms (handle.rs:329-331).
    out.append({ lastPiece, 1200 });
    if (lastPiece - 1 > startPiece + headWindowSize) {
        out.append({ lastPiece - 1, 1250 });
    }
    return out;
}

}  // namespace StreamPrioritizer
